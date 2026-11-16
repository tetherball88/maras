#include "maras/bridge_skyrimnet/SkyrimNetProfileService.h"

#include "maras/bridge_skyrimnet/SkyrimNetApiLoader.h"
#include "maras/bridge_skyrimnet/SkyrimNetMemoryService.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/domain/lineage/ChildProfileGuidance.h"
#include "maras/domain/lineage/TrainingCatalog.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/engine/TrainingCatalogLoader.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace MARAS::Bridge::SkyrimNet {

    struct InvolvedAdultProfileRef {
        std::uint32_t formID = 0;
        std::uint64_t uuid = 0;
        std::string bioTemplateName;
        std::string name;
        std::string raceName;
        std::string roleLabel;
        bool isGuardian = false;
        std::vector<std::string> fallbackMemories;
    };

    struct ChildProfileContextData {
        MARAS::SessionEpoch sessionEpoch = 0;
        std::uint32_t childID = 0;
        MARAS::Foundation::ChildStage stage = MARAS::Foundation::ChildStage::Child;
        std::uint64_t actorUUID = 0;
        std::string bioTemplateName;
        std::string saveId;
        std::string childName;
        std::string raceName;
        bool isFemale = false;
        bool isAdopted = false;
        bool isBiologicalChild = false;
        bool hasBeastblood = false;
        bool trainedInWerewolfPack = false;

        // Biological Mother
        std::uint32_t bioMotherID = 0;
        std::uint64_t bioMotherUUID = 0;
        std::string motherName;
        std::string motherRace;
        bool motherIsGuardian = false;

        // True Biological Father
        std::uint32_t bioFatherID = 0;
        std::uint64_t bioFatherUUID = 0;
        std::string bioFatherName;
        std::string bioFatherRace;
        bool bioFatherIsGuardian = false;

        // Presumed Father (Mother's partner at conception/birth if distinct from bioFatherID)
        std::uint32_t presumedFatherID = 0;
        std::uint64_t presumedFatherUUID = 0;
        std::string presumedFatherName;
        std::string presumedFatherRace;
        bool presumedFatherIsGuardian = false;
        std::string motherPresumedRelStatus;

        // Custodial Guardians
        std::uint32_t guardianAID = 0;
        std::uint32_t guardianBID = 0;
        std::string guardianAName;
        std::string guardianBName;

        // Paternity Secret & Knowledge State
        bool hasPresumedFather = false;
        bool hasPaternitySecret = false;
        bool isSingleMotherSecretFromBioFather = false;
        bool partnerKnowsPaternity = true;
        bool bioFatherKnowsPaternity = true;
        bool hasPhenotypeMismatch = false;
        std::string visualDiscrepancyDescription;

        // All involved adults at the moment of generating/updating the bio
        std::vector<InvolvedAdultProfileRef> involvedAdults;

        std::vector<std::string> siblingNames;
        std::string homeLocationName;
        std::string trainingLocation;
        std::string trainingClassName;
        std::string trainingDescription;
        std::string savedChildhoodBioTemplateName;
        std::string generationGuidance;
        std::int64_t recentEventsMaxAgeSeconds = 0;
    };

    namespace {

        constexpr const char* kAllProfileBlocksCSV = "all";

        constexpr const char* kInlineChildBioContextTemplate = R"({% if exists("actorUUID") and actorUUID != 0 and ttm_is_registered(actorUUID) %}
    {% set npcName = decnpc(actorUUID).name %}
    {% set profile = ttm_npc_relationship_profile(actorUUID) %}
    {% if existsIn(profile, "parents") %}
        {% set parents = profile.parents %}
        {% set adultsList = [] %}
        {% if existsIn(parents, "bio_generation_adults") and length(parents.bio_generation_adults) > 0 %}
            {% set adultsList = parents.bio_generation_adults %}
        {% elif existsIn(parents, "involved_adults") and length(parents.involved_adults) > 0 %}
            {% set adultsList = parents.involved_adults %}
        {% endif %}
        {% if length(adultsList) > 0 %}
## Involved Parties — Character Profiles & Memories (Context for {{npcName}}'s Biological Heritage & Household Upbringing)
            {% for adult in adultsList %}
                {% if existsIn(adult, "uuid") and adult.uuid != 0 %}
### {{adult.name}} ({{adult.role}})
{{ render_character_profile("ttm_child_parent_bio", adult.uuid) }}

                {% endif %}
            {% endfor %}
        {% endif %}
        {% if existsIn(parents, "recent_events_participants") and length(parents.recent_events_participants) > 0 %}
{{ render_template("helpers\\maras_actor_bio_recent_events") }}
        {% endif %}
    {% endif %}
{% endif %})";

        std::string SafeJsonDump(const nlohmann::json& j) {
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        std::string ResolvePlacedActorBioTemplateName(std::uint32_t actorFormID) {
            if (actorFormID == 0 || actorFormID == MARAS::kUnknownFatherFormID) return "";
            if (actorFormID == MARAS::kPlayerFormID) {
                return "player_special";
            }
            return GetBioTemplateNameExport(actorFormID);
        }

        struct WebServerEndpoint {
            bool enabled = true;
            std::string host = "127.0.0.1";
            int port = 8080;
        };

        WebServerEndpoint ReadSkyrimNetWebServerConfig() {
            WebServerEndpoint cfg;
            try {
                const std::filesystem::path configPath = "Data/SKSE/Plugins/SkyrimNet/config/WebServer.yaml";
                std::error_code ec;
                if (!std::filesystem::exists(configPath, ec) || ec) {
                    return cfg;
                }

                std::ifstream in(configPath);
                if (!in.is_open()) {
                    return cfg;
                }

                std::string line;
                while (std::getline(in, line)) {
                    std::string trimmed = MARAS::Foundation::StringUtils::TrimStd(line);
                    if (trimmed.empty() || trimmed.front() == '#') {
                        continue;
                    }
                    const auto colonPos = trimmed.find(':');
                    if (colonPos == std::string::npos) {
                        continue;
                    }
                    std::string key = MARAS::Foundation::StringUtils::ToLowerStd(
                        MARAS::Foundation::StringUtils::TrimStd(trimmed.substr(0, colonPos)));
                    std::string val = MARAS::Foundation::StringUtils::TrimStd(trimmed.substr(colonPos + 1));
                    // Strip inline comment
                    if (const auto hashPos = val.find('#'); hashPos != std::string::npos) {
                        val = MARAS::Foundation::StringUtils::TrimStd(val.substr(0, hashPos));
                    }
                    // Strip surrounding quotes
                    if (val.size() >= 2 &&
                        ((val.front() == '"' && val.back() == '"') ||
                         (val.front() == '\'' && val.back() == '\''))) {
                        val = val.substr(1, val.size() - 2);
                    }

                    if (key == "enabled") {
                        const std::string vLower = MARAS::Foundation::StringUtils::ToLowerStd(val);
                        if (vLower == "false" || vLower == "0" || vLower == "no") {
                            cfg.enabled = false;
                        } else if (vLower == "true" || vLower == "1" || vLower == "yes") {
                            cfg.enabled = true;
                        }
                    } else if (key == "host" && !val.empty()) {
                        cfg.host = (val == "0.0.0.0" || val == "::") ? "127.0.0.1" : val;
                    } else if (key == "port" && !val.empty()) {
                        int parsedPort = 0;
                        const auto [ptr, parseEc] = std::from_chars(val.data(), val.data() + val.size(), parsedPort);
                        if (parseEc == std::errc{} && parsedPort >= 1024 && parsedPort <= 65535) {
                            cfg.port = parsedPort;
                        }
                    }
                }
            } catch (const std::exception& e) {
                MARAS_LOG_WARN("[SkyrimNetProfile] Failed to parse WebServer.yaml: {}", e.what());
            }
            return cfg;
        }

        std::wstring Utf8ToWide(std::string_view utf8) {
            if (utf8.empty()) return {};
            const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (needed <= 0) return {};
            std::wstring out(static_cast<std::size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
            return out;
        }

        struct HttpResponse {
            int statusCode = 0;
            std::string body;
        };

        HttpResponse SendLocalHttpRequest(
            std::string_view method,
            std::string_view host,
            int port,
            std::string_view pathAndQuery,
            std::string_view jsonBody,
            DWORD timeoutMs)
        {
            HttpResponse result;

            const std::wstring wHost = Utf8ToWide(host);
            const std::wstring wMethod = Utf8ToWide(method);
            const std::wstring wPath = Utf8ToWide(pathAndQuery);
            if (wHost.empty() || wMethod.empty() || wPath.empty()) {
                return result;
            }

            HINTERNET hSession = WinHttpOpen(
                L"MARAS-SkyrimNetProfileService/1.0",
                WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0);
            if (!hSession) {
                return result;
            }

            WinHttpSetTimeouts(hSession, 1000, 1000, timeoutMs, timeoutMs);

            HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), static_cast<INTERNET_PORT>(port), 0);
            if (!hConnect) {
                WinHttpCloseHandle(hSession);
                return result;
            }

            HINTERNET hRequest = WinHttpOpenRequest(
                hConnect,
                wMethod.c_str(),
                wPath.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                0);
            if (!hRequest) {
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return result;
            }

            const wchar_t* headers = jsonBody.empty()
                ? WINHTTP_NO_ADDITIONAL_HEADERS
                : L"Content-Type: application/json; charset=utf-8\r\n";
            const DWORD headersLen = jsonBody.empty() ? 0 : static_cast<DWORD>(-1L);

            const BOOL sent = WinHttpSendRequest(
                hRequest,
                headers,
                headersLen,
                jsonBody.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(jsonBody.data()),
                static_cast<DWORD>(jsonBody.size()),
                static_cast<DWORD>(jsonBody.size()),
                0);

            if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                if (WinHttpQueryHeaders(
                        hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode,
                        &statusSize,
                        WINHTTP_NO_HEADER_INDEX)) {
                    result.statusCode = static_cast<int>(statusCode);
                }

                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD bytesRead = 0;
                    if (WinHttpReadData(hRequest, chunk.data(), avail, &bytesRead) && bytesRead > 0) {
                        chunk.resize(bytesRead);
                        result.body.append(chunk);
                    } else {
                        break;
                    }
                    avail = 0;
                }
            }

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        std::string ExtractPromptBlock(const std::string& rawPrompt, std::string_view blockName) {
            const std::string openTag = std::format("{{% block {} %}}", blockName);
            const std::string closeTag = "{% endblock %}";
            auto startPos = rawPrompt.find(openTag);
            if (startPos == std::string::npos) {
                return "";
            }
            startPos += openTag.size();
            const auto endPos = rawPrompt.find(closeTag, startPos);
            if (endPos == std::string::npos) {
                return "";
            }
            return MARAS::Foundation::StringUtils::TrimStd(rawPrompt.substr(startPos, endPos - startPos));
        }

        std::string ReadAdultBioFromDiskFallback(const std::string& saveId, const InvolvedAdultProfileRef& adult) {
            if (adult.bioTemplateName.empty()) {
                return "";
            }
            const std::string fileName = adult.bioTemplateName + ".prompt";
            std::vector<std::filesystem::path> candidatePaths;
            if (!saveId.empty()) {
                candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/saves") / saveId / "prompts/characters/dynamic" / fileName);
                candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/saves") / saveId / "prompts/characters" / fileName);
            }
            candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/prompts/characters/dynamic") / fileName);
            candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/prompts/characters") / fileName);
            candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/library/skyrimnet.base/prompts/characters") / fileName);

            std::string rawContent;
            for (const auto& p : candidatePaths) {
                std::error_code ec;
                if (std::filesystem::exists(p, ec) && !ec) {
                    std::ifstream in(p, std::ios::binary);
                    if (in.is_open()) {
                        std::ostringstream buf;
                        buf << in.rdbuf();
                        rawContent = buf.str();
                        if (!rawContent.empty()) {
                            break;
                        }
                    }
                }
            }
            if (rawContent.empty()) {
                return "";
            }

            struct BlockDef {
                const char* key;
                const char* title;
            };
            constexpr BlockDef kBlocks[] = {
                {"summary", "Summary"},
                {"background", "Background"},
                {"personality", "Personality"},
                {"appearance", "Appearance"},
                {"occupation", "Occupation"},
                {"skills", "Skills & Abilities"}
            };

            std::ostringstream out;
            for (const auto& b : kBlocks) {
                std::string blockText = ExtractPromptBlock(rawContent, b.key);
                if (!blockText.empty()) {
                    out << "#### " << b.title << "\n" << blockText << "\n";
                }
            }

            if (!adult.fallbackMemories.empty()) {
                out << "#### Memories\n";
                for (const auto& memLine : adult.fallbackMemories) {
                    if (!memLine.empty()) {
                        out << "- " << memLine << "\n";
                    }
                }
            }

            return MARAS::Foundation::StringUtils::TrimStd(out.str());
        }

        std::vector<std::string> QueryAdultFallbackMemoriesOnGameThread(std::uint32_t adultFormID) {
            if (adultFormID == 0 || adultFormID == MARAS::kUnknownFatherFormID) {
                return {};
            }
            return QueryActorMemoriesExport(adultFormID, 5, "");
        }

        std::string RenderInvolvedPartiesContext(
            const ChildProfileContextData& taskData,
            const WebServerEndpoint& endpoint)
        {
            // 1. Primary path: render involved adults' bios (render_character_profile("ttm_gm", adult.uuid))
            //    and family recent events via POST /prompts?api=render-template-preview directly from
            //    taskData.involvedAdults so ttm_npc_relationship_profile stays strictly perspective-masked.
            if (endpoint.enabled && taskData.actorUUID != 0) {
                auto tryRenderPreview = [&](const nlohmann::json& payload) -> std::string {
                    auto resp = SendLocalHttpRequest(
                        "POST",
                        endpoint.host,
                        endpoint.port,
                        "/prompts?api=render-template-preview",
                        SafeJsonDump(payload),
                        5000);
                    if (resp.statusCode == 200 && !resp.body.empty()) {
                        nlohmann::json parsed = nlohmann::json::parse(resp.body, nullptr, false);
                        if (parsed.is_object() &&
                            parsed.contains("success") && parsed["success"].is_boolean() &&
                            parsed["success"].get<bool>() &&
                            parsed.contains("renderedContent") && parsed["renderedContent"].is_string()) {
                            std::string rendered = MARAS::Foundation::StringUtils::TrimStd(
                                parsed["renderedContent"].get<std::string>());
                            if (!rendered.empty()) {
                                return rendered;
                            }
                        }
                    }
                    return "";
                };

                auto sanitizeInjaLiteral = [](std::string_view text) -> std::string {
                    std::string out;
                    out.reserve(text.size());
                    for (char ch : text) {
                        if (ch == '{') out.push_back('(');
                        else if (ch == '}') out.push_back(')');
                        else out.push_back(ch);
                    }
                    return out;
                };

                const std::string actorUUIDHex = std::format("{:X}", taskData.actorUUID);
                std::ostringstream tpl;
                bool hasAnyUuidAdult = false;
                nlohmann::json recentEventsUUIDs = nlohmann::json::array();
                recentEventsUUIDs.push_back(taskData.actorUUID);

                for (const auto& adult : taskData.involvedAdults) {
                    if (adult.uuid == 0) continue;
                    if (!hasAnyUuidAdult) {
                        tpl << "## Involved Parties — Character Profiles & Memories (Context for "
                            << sanitizeInjaLiteral(taskData.childName)
                            << "'s Biological Heritage & Household Upbringing)\n";
                        hasAnyUuidAdult = true;
                    }
                    tpl << "### " << sanitizeInjaLiteral(adult.name)
                        << " (" << sanitizeInjaLiteral(adult.roleLabel) << ")\n"
                        << "{{ render_character_profile(\"ttm_child_parent_bio\", " << adult.uuid << ") }}\n\n";

                    const bool isUnawareSecretBioFather =
                        (adult.formID == taskData.bioFatherID) &&
                        !taskData.bioFatherKnowsPaternity &&
                        !taskData.partnerKnowsPaternity &&
                        !adult.isGuardian;
                    if (!isUnawareSecretBioFather) {
                        recentEventsUUIDs.push_back(adult.uuid);
                    }
                }

                std::string rendered;
                if (hasAnyUuidAdult) {
                    tpl << "{{ render_template(\"helpers\\\\maras_actor_bio_recent_events\") }}";
                    nlohmann::json previewPayload{
                        {"content", tpl.str()},
                        {"actorUUID", actorUUIDHex},
                        {"recent_events_participants", recentEventsUUIDs},
                        {"child_bio_mode", true},
                        {"recent_events_header", "Family & Household Recent Events (Chronological)"}
                    };
                    if (taskData.recentEventsMaxAgeSeconds > 0) {
                        previewPayload["recent_events_max_age_seconds"] = taskData.recentEventsMaxAgeSeconds;
                    }
                    rendered = tryRenderPreview(previewPayload);
                }
                if (rendered.empty()) {
                    nlohmann::json fallbackTplPayload{
                        {"templateName", "helpers/maras_child_bio_context"},
                        {"actorUUID", actorUUIDHex},
                        {"child_bio_mode", true},
                        {"recent_events_header", "Family & Household Recent Events (Chronological)"}
                    };
                    if (taskData.recentEventsMaxAgeSeconds > 0) {
                        fallbackTplPayload["recent_events_max_age_seconds"] = taskData.recentEventsMaxAgeSeconds;
                    }
                    rendered = tryRenderPreview(fallbackTplPayload);
                }
                if (rendered.empty()) {
                    nlohmann::json fallbackInlinePayload{
                        {"content", kInlineChildBioContextTemplate},
                        {"actorUUID", actorUUIDHex},
                        {"child_bio_mode", true},
                        {"recent_events_header", "Family & Household Recent Events (Chronological)"}
                    };
                    if (taskData.recentEventsMaxAgeSeconds > 0) {
                        fallbackInlinePayload["recent_events_max_age_seconds"] = taskData.recentEventsMaxAgeSeconds;
                    }
                    rendered = tryRenderPreview(fallbackInlinePayload);
                }
                if (!rendered.empty()) {
                    MARAS_LOG_INFO("[SkyrimNetProfile] Rendered involved parties bio context ({} bytes) for {:08X} ('{}')",
                                   rendered.size(), taskData.childID, taskData.childName);
                    return rendered;
                }
            }

            // 2. Secondary fallback (if WebServer is disabled or unreachable):
            //    Read each involved adult's character bio blocks from disk and query their memories via C++ API.
            if (taskData.involvedAdults.empty()) {
                return "";
            }

            std::ostringstream fallbackOut;
            bool anyAdultRendered = false;
            for (const auto& adult : taskData.involvedAdults) {
                std::string adultBio = ReadAdultBioFromDiskFallback(taskData.saveId, adult);
                if (!adultBio.empty()) {
                    if (!anyAdultRendered) {
                        fallbackOut << "## Involved Parties — Character Profiles & Memories (Context for "
                                    << taskData.childName << "'s Biological Heritage & Household Upbringing)\n\n";
                        anyAdultRendered = true;
                    }
                    fallbackOut << "### " << adult.name << " (" << adult.roleLabel << ")\n"
                                << adultBio << "\n\n";
                }
            }
            std::string result = MARAS::Foundation::StringUtils::TrimStd(fallbackOut.str());
            if (!result.empty()) {
                MARAS_LOG_INFO("[SkyrimNetProfile] Built fallback involved parties bio context ({} bytes) for {:08X} ('{}')",
                               result.size(), taskData.childID, taskData.childName);
            }
            return result;
        }

        ChildProfileContextData GatherChildProfileContextOnGameThread(
            std::uint32_t childID,
            MARAS::Foundation::ChildStage stage,
            RE::Actor* actor)
        {
            const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto omniscientView = MARAS::Projection::PerspectiveView::ForDashboard(
                worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &MARAS::GetEngineWerewolfStateProvider());
            const auto childView = MARAS::Projection::PerspectiveView::ForActor(
                worldSnap, spatialSnap, MARAS::State::ActorId{ childID }, &MARAS::GetEngineWerewolfStateProvider());
            const float currentGameDays = RE::Calendar::GetSingleton()
                ? RE::Calendar::GetSingleton()->GetDaysPassed()
                : std::max(0.0f, spatialSnap->CurrentGameDay());

            ChildProfileContextData data;
            data.childID = childID;
            data.stage = stage;
            data.actorUUID = ResolvePlacedActorUUID(childID);
            data.bioTemplateName = ResolvePlacedActorBioTemplateName(childID);
            data.saveId = GetSaveUniqueIDExport();

            data.childName = MARAS::ResolveActorNameSafe(childID, *worldSnap, *spatialSnap);
            data.isFemale = (MARAS::ResolveActorSexSafe(childID, *worldSnap, *spatialSnap) == MARAS::State::ActorSex::Female);
            data.raceName = MARAS::ResolveActorRaceSafe(childID, *worldSnap, *spatialSnap);

            if (actor) {
                if (auto* race = actor->GetRace()) {
                    const char* rawRace = race->GetName();
                    if (rawRace && rawRace[0] != '\0') {
                        data.raceName = std::string(rawRace);
                    }
                }
                if (auto* loc = actor->GetCurrentLocation()) {
                    const char* rawLoc = loc->GetName();
                    if (rawLoc && rawLoc[0] != '\0') {
                        data.homeLocationName = MARAS::Foundation::StringUtils::TrimStd(rawLoc);
                    }
                } else if (auto* cell = actor->GetParentCell()) {
                    const char* rawCell = cell->GetName();
                    if (rawCell && rawCell[0] != '\0') {
                        data.homeLocationName = MARAS::Foundation::StringUtils::TrimStd(rawCell);
                    }
                }
            }

            if (const auto perceivedOpt = omniscientView.GetPerceivedChild(MARAS::State::ChildId{ childID }); perceivedOpt.has_value()) {
                const auto& entry = *perceivedOpt;
                if (data.childName.empty() && !entry.name.empty()) {
                    data.childName = MARAS::Foundation::StringUtils::TrimStd(entry.name);
                }
                if (entry.gender == 1) {
                    data.isFemale = true;
                } else if (entry.gender == 0) {
                    data.isFemale = false;
                }
                data.isAdopted = entry.isAdopted;
                data.isBiologicalChild = entry.isBiologicalChild;
                data.hasBeastblood = entry.hasBeastblood;
                data.recentEventsMaxAgeSeconds = MARAS::Domain::Lineage::ComputeChildRecentEventsMaxAgeSeconds(
                    entry.isBiologicalChild,
                    entry.isAdopted,
                    entry.birthTimestamp,
                    entry.timestamp,
                    currentGameDays);

                const auto& catalog = MARAS::Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
                std::int32_t trainIdx = entry.trainingClassIndex;
                if (!catalog.IsValidClassIndex(trainIdx) && !entry.trainingLocation.empty()) {
                    trainIdx = catalog.FindClassByLocation(entry.trainingLocation);
                }
                if (const auto* classSpec = catalog.FindClass(trainIdx)) {
                    data.trainedInWerewolfPack = classSpec->requiresBeastblood;
                    data.trainingClassName = classSpec->className;
                    data.trainingDescription = classSpec->description;
                }
                data.trainingLocation = MARAS::Foundation::StringUtils::TrimStd(entry.trainingLocation);
                data.savedChildhoodBioTemplateName = MARAS::Foundation::StringUtils::TrimStd(entry.savedBioTemplateName);

                data.bioMotherID = entry.bioMother.raw();
                data.bioFatherID = entry.perceivedBioFather.raw();
                const std::uint32_t presFatherID = entry.presumedFather.raw();
                data.hasPresumedFather = (presFatherID != 0);
                if (data.hasPresumedFather) {
                    data.presumedFatherID = presFatherID;
                }

                const auto* secPtr = entry.paternitySecret.has_value() ? &(*entry.paternitySecret) : nullptr;
                const bool secIsSecret = secPtr && secPtr->isSecret;
                const bool secDiscovered = secPtr && secPtr->discovered;
                const bool secForPartner = secPtr && secPtr->isSecretForPartner;
                const bool secForBioFather = secPtr && secPtr->isSecretForBioFather;
                const bool secDiscByPartner = secPtr && secPtr->discoveredByPartner;
                const bool secDiscByBioFather = secPtr && secPtr->discoveredByBioFather;

                data.hasPaternitySecret = data.hasPresumedFather &&
                    (secIsSecret || secDiscovered || secForPartner || secForBioFather || secDiscByPartner || secDiscByBioFather);
                data.partnerKnowsPaternity = !data.hasPaternitySecret || secDiscovered || secDiscByPartner || !secForPartner;
                data.bioFatherKnowsPaternity = secDiscovered || secDiscByBioFather || !secForBioFather;
                data.isSingleMotherSecretFromBioFather =
                    (!data.hasPresumedFather && data.bioFatherID != 0 && secForBioFather && !secDiscByBioFather && !secDiscovered);
                data.hasPhenotypeMismatch = secPtr && secPtr->hasPhenotypeMismatch;
                if (data.hasPhenotypeMismatch && data.bioMotherID != 0 && data.presumedFatherID != 0 && data.bioFatherID != 0) {
                    data.visualDiscrepancyDescription =
                        MARAS::App::PaternityWorkflowCoordinator::GetSingleton().GetVisualDiscrepancyDescription(
                            data.bioMotherID, data.presumedFatherID, data.bioFatherID);
                }

                if (data.bioMotherID != 0) {
                    data.bioMotherUUID = ResolvePlacedActorUUID(data.bioMotherID);
                    data.motherName = MARAS::ResolveActorNameSafe(data.bioMotherID, *worldSnap, *spatialSnap);
                    data.motherRace = MARAS::ResolveActorRaceSafe(data.bioMotherID, *worldSnap, *spatialSnap);
                    data.motherIsGuardian = entry.guardians.HasGuardian(MARAS::State::ActorId{ data.bioMotherID });
                }

                if (data.bioFatherID != 0 && data.bioFatherID != MARAS::kUnknownFatherFormID) {
                    data.bioFatherUUID = ResolvePlacedActorUUID(data.bioFatherID);
                    data.bioFatherName = MARAS::ResolveActorNameSafe(data.bioFatherID, *worldSnap, *spatialSnap);
                    data.bioFatherRace = MARAS::ResolveActorRaceSafe(data.bioFatherID, *worldSnap, *spatialSnap);
                    data.bioFatherIsGuardian = entry.guardians.HasGuardian(MARAS::State::ActorId{ data.bioFatherID });
                } else if (!entry.perceivedBioFatherFallbackName.empty() && entry.perceivedBioFatherFallbackName != "Unknown") {
                    data.bioFatherName = entry.perceivedBioFatherFallbackName;
                }

                if (data.hasPresumedFather && data.presumedFatherID != 0) {
                    data.presumedFatherUUID = ResolvePlacedActorUUID(data.presumedFatherID);
                    data.presumedFatherName = MARAS::ResolveActorNameSafe(data.presumedFatherID, *worldSnap, *spatialSnap);
                    data.presumedFatherRace = MARAS::ResolveActorRaceSafe(data.presumedFatherID, *worldSnap, *spatialSnap);
                    data.presumedFatherIsGuardian = entry.guardians.HasGuardian(MARAS::State::ActorId{ data.presumedFatherID });
                    if (data.bioMotherID != 0) {
                        const auto pairOpt = omniscientView.GetPerceivedPair(
                            MARAS::State::ActorId{ data.bioMotherID }, MARAS::State::ActorId{ data.presumedFatherID });
                        data.motherPresumedRelStatus = pairOpt.has_value()
                            ? std::string(MARAS::Foundation::RelationshipStatusToString(pairOpt->status))
                            : "Unknown";
                    }
                }

                data.guardianAID = entry.guardians.slotA.raw();
                data.guardianBID = entry.guardians.slotB.raw();
                if (data.guardianAID != 0) {
                    data.guardianAName = MARAS::ResolveActorNameSafe(data.guardianAID, *worldSnap, *spatialSnap);
                }
                if (data.guardianBID != 0 && data.guardianBID != data.guardianAID) {
                    data.guardianBName = MARAS::ResolveActorNameSafe(data.guardianBID, *worldSnap, *spatialSnap);
                }

                // Populate all involved adults at the moment of generating/updating the bio
                std::vector<std::uint32_t> seenAdults;
                auto addAdultRef = [&](std::uint32_t adultID, const std::string& roleLabel, bool isGuardian) {
                    if (adultID == 0 || adultID == MARAS::kUnknownFatherFormID || adultID == childID) return;
                    for (std::uint32_t seen : seenAdults) {
                        if (seen == adultID) return;
                    }
                    seenAdults.push_back(adultID);
                    InvolvedAdultProfileRef ref;
                    ref.formID = adultID;
                    ref.uuid = ResolvePlacedActorUUID(adultID);
                    ref.bioTemplateName = ResolvePlacedActorBioTemplateName(adultID);
                    ref.name = MARAS::ResolveActorNameSafe(adultID, *worldSnap, *spatialSnap);
                    ref.raceName = MARAS::ResolveActorRaceSafe(adultID, *worldSnap, *spatialSnap);
                    ref.roleLabel = roleLabel;
                    ref.isGuardian = isGuardian;
                    ref.fallbackMemories = QueryAdultFallbackMemoriesOnGameThread(adultID);
                    if (!ref.name.empty()) {
                        data.involvedAdults.push_back(std::move(ref));
                    }
                };

                if (data.bioMotherID != 0) {
                    addAdultRef(
                        data.bioMotherID,
                        data.motherIsGuardian ? "Biological Mother & Household Guardian" : "Biological Mother (Non-Custodial)",
                        data.motherIsGuardian);
                }
                if (data.bioFatherID != 0 && data.bioFatherID != MARAS::kUnknownFatherFormID) {
                    const std::string bioFatherRole = MARAS::Domain::Lineage::ComputeBioFatherRoleLabel(
                        data.bioFatherIsGuardian,
                        data.hasPaternitySecret,
                        data.isSingleMotherSecretFromBioFather,
                        data.partnerKnowsPaternity,
                        data.bioFatherKnowsPaternity);
                    addAdultRef(data.bioFatherID, bioFatherRole, data.bioFatherIsGuardian);
                }
                if (data.hasPresumedFather && data.presumedFatherID != 0) {
                    const std::string presFatherRole = MARAS::Domain::Lineage::ComputePresumedFatherRoleLabel(
                        data.presumedFatherIsGuardian,
                        data.partnerKnowsPaternity,
                        data.bioFatherKnowsPaternity);
                    addAdultRef(data.presumedFatherID, presFatherRole, data.presumedFatherIsGuardian);
                }
                if (data.guardianAID != 0) {
                    addAdultRef(
                        data.guardianAID,
                        data.isAdopted ? "Adoptive Parent & Household Guardian" : "Step-Parent & Household Guardian",
                        true);
                }
                if (data.guardianBID != 0 && data.guardianBID != data.guardianAID) {
                    addAdultRef(
                        data.guardianBID,
                        data.isAdopted ? "Adoptive Parent & Household Guardian" : "Step-Parent & Household Guardian",
                        true);
                }

                // Collect siblings strictly through child-perspective lens so unaware children never receive secret half-siblings
                for (const auto& sib : childView.GetPerceivedSiblingsOf(MARAS::State::ChildId{ childID }, /*forcePublicOnly=*/false)) {
                    std::string sibName = !sib.name.empty()
                        ? MARAS::Foundation::StringUtils::TrimStd(sib.name)
                        : MARAS::ResolveActorNameSafe(sib.childId.raw(), *worldSnap, *spatialSnap);
                    if (!sibName.empty()) {
                        data.siblingNames.push_back(std::move(sibName));
                    }
                }
            }

            if (data.childName.empty()) {
                data.childName = data.isFemale ? "Daughter" : "Son";
            }

            MARAS::Domain::Lineage::ChildProfileGuidanceInput guidanceInput;
            guidanceInput.stage = data.stage;
            guidanceInput.childName = data.childName;
            guidanceInput.raceName = data.raceName;
            guidanceInput.isFemale = data.isFemale;
            guidanceInput.isAdopted = data.isAdopted;
            guidanceInput.isBiologicalChild = data.isBiologicalChild;
            guidanceInput.hasBeastblood = data.hasBeastblood;
            guidanceInput.trainedInWerewolfPack = data.trainedInWerewolfPack;
            guidanceInput.motherName = data.motherName;
            guidanceInput.motherRace = data.motherRace;
            guidanceInput.motherIsGuardian = data.motherIsGuardian;
            guidanceInput.bioFatherName = data.bioFatherName;
            guidanceInput.bioFatherRace = data.bioFatherRace;
            guidanceInput.bioFatherIsGuardian = data.bioFatherIsGuardian;
            guidanceInput.presumedFatherName = data.presumedFatherName;
            guidanceInput.presumedFatherRace = data.presumedFatherRace;
            guidanceInput.presumedFatherIsGuardian = data.presumedFatherIsGuardian;
            guidanceInput.motherPresumedRelStatus = data.motherPresumedRelStatus;
            guidanceInput.guardianAName = data.guardianAName;
            guidanceInput.guardianBName = data.guardianBName;
            guidanceInput.hasPresumedFather = data.hasPresumedFather;
            guidanceInput.hasPaternitySecret = data.hasPaternitySecret;
            guidanceInput.isSingleMotherSecretFromBioFather = data.isSingleMotherSecretFromBioFather;
            guidanceInput.partnerKnowsPaternity = data.partnerKnowsPaternity;
            guidanceInput.bioFatherKnowsPaternity = data.bioFatherKnowsPaternity;
            guidanceInput.hasPhenotypeMismatch = data.hasPhenotypeMismatch;
            guidanceInput.visualDiscrepancyDescription = data.visualDiscrepancyDescription;
            guidanceInput.siblingNames = data.siblingNames;
            guidanceInput.homeLocationName = data.homeLocationName;
            guidanceInput.trainingLocation = data.trainingLocation;
            guidanceInput.trainingClassName = data.trainingClassName;
            guidanceInput.trainingDescription = data.trainingDescription;

            data.generationGuidance = MARAS::Domain::Lineage::BuildChildProfileGenerationGuidance(guidanceInput);
            return data;
        }

        std::string CarryOverChildhoodBioFromDisk(const ChildProfileContextData& taskData) {
            if (taskData.stage != MARAS::Foundation::ChildStage::Adult || taskData.savedChildhoodBioTemplateName.empty()) {
                return "";
            }

            const std::string srcFileName = taskData.savedChildhoodBioTemplateName + ".prompt";
            std::vector<std::filesystem::path> candidatePaths;
            if (!taskData.saveId.empty()) {
                candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/saves") / taskData.saveId / "prompts/characters/dynamic" / srcFileName);
                candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/saves") / taskData.saveId / "prompts/characters" / srcFileName);
            }
            candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/prompts/characters/dynamic") / srcFileName);
            candidatePaths.push_back(std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/prompts/characters") / srcFileName);

            std::filesystem::path foundPath;
            std::string rawContent;
            for (const auto& p : candidatePaths) {
                std::error_code ec;
                if (std::filesystem::exists(p, ec) && !ec) {
                    std::ifstream in(p, std::ios::binary);
                    if (in.is_open()) {
                        std::ostringstream buf;
                        buf << in.rdbuf();
                        rawContent = buf.str();
                        if (!rawContent.empty()) {
                            foundPath = p;
                            break;
                        }
                    }
                }
            }

            if (rawContent.empty()) {
                MARAS_LOG_INFO("[SkyrimNetProfile] No existing childhood prompt file found for savedBioTemplateName='{}' (child {:08X} '{}')",
                               taskData.savedChildhoodBioTemplateName, taskData.childID, taskData.childName);
                return "";
            }

            // Copy childhood .prompt file to the new adult actor's bioTemplateName path so SkyrimNet evolves the existing profile
            if (!taskData.bioTemplateName.empty() && taskData.bioTemplateName != taskData.savedChildhoodBioTemplateName) {
                const std::string dstFileName = taskData.bioTemplateName + ".prompt";
                std::filesystem::path dstPath;
                if (!taskData.saveId.empty()) {
                    dstPath = std::filesystem::path("Data/SKSE/Plugins/SkyrimNet/saves") / taskData.saveId / "prompts/characters/dynamic" / dstFileName;
                } else {
                    dstPath = foundPath.parent_path() / dstFileName;
                }

                std::error_code ec;
                std::filesystem::create_directories(dstPath.parent_path(), ec);
                std::filesystem::copy_file(foundPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    MARAS_LOG_INFO("[SkyrimNetProfile] Copied childhood bio '{}' -> '{}' for adult child {:08X} ('{}')",
                                   foundPath.string(), dstPath.string(), taskData.childID, taskData.childName);
                } else {
                    MARAS_LOG_WARN("[SkyrimNetProfile] Failed to copy childhood bio '{}' -> '{}': {}",
                                   foundPath.string(), dstPath.string(), ec.message());
                }
            }

            struct BlockDef {
                const char* key;
                const char* title;
            };
            constexpr BlockDef kBlocks[] = {
                {"summary", "Childhood Summary"},
                {"background", "Childhood Background"},
                {"personality", "Childhood Personality"},
                {"appearance", "Childhood Appearance"},
                {"occupation", "Childhood Role"},
                {"skills", "Childhood Skills & Interests"},
                {"speech_style", "Childhood Speech Style"},
                {"aspirations", "Childhood Aspirations"},
                {"relationships", "Childhood Relationships"}
            };

            std::ostringstream out;
            out << "## Existing Childhood Biography for " << taskData.childName
                << " (CARRY OVER & EVOLVE INTO ADULTHOOD)\n"
                << "CRITICAL INSTRUCTION: Below is " << taskData.childName
                << "'s established childhood biography from before "
                << (taskData.isFemale ? "she" : "he") << " left for training";
            if (!taskData.trainingClassName.empty() || !taskData.trainingLocation.empty()) {
                out << " (" << taskData.trainingClassName
                    << (!taskData.trainingLocation.empty() ? " at " + taskData.trainingLocation : "") << ")";
            }
            out << ". You MUST preserve and carry forward " << taskData.childName
                << "'s childhood memories, core personality traits, inherited physical appearance, and family relationships, "
                << "while naturally maturing and evolving every section into a young adult who has completed training and returned home.\n\n";

            bool anyBlock = false;
            for (const auto& b : kBlocks) {
                std::string blockText = ExtractPromptBlock(rawContent, b.key);
                if (!blockText.empty()) {
                    anyBlock = true;
                    out << "### " << b.title << "\n" << blockText << "\n\n";
                }
            }

            return anyBlock ? MARAS::Foundation::StringUtils::TrimStd(out.str()) : "";
        }

        void ExecuteAsyncChildProfileFlow(ChildProfileContextData& taskData, std::stop_token stopToken) {
            try {
                if (stopToken.stop_requested() || !MARAS::IsSessionEpochCurrent(taskData.sessionEpoch)) {
                    return;
                }

                // 0. If this is an Adult Child (Stage 4) who previously had a spawned Young Child bio,
                //    copy the childhood .prompt file and inject childhood bio continuity context.
                std::string childhoodCarryOver = CarryOverChildhoodBioFromDisk(taskData);
                if (!childhoodCarryOver.empty()) {
                    taskData.generationGuidance += "\n\n" + childhoodCarryOver;
                }

                if (stopToken.stop_requested() || !MARAS::IsSessionEpochCurrent(taskData.sessionEpoch)) {
                    return;
                }

                const auto endpoint = ReadSkyrimNetWebServerConfig();

                // 1. Render all involved parties' character bios (ttm_gm) and chronological family recent events
                //    and append them directly to generationGuidance so the LLM has full parent/guardian profiles.
                std::string renderedPartiesContext = RenderInvolvedPartiesContext(taskData, endpoint);
                if (!renderedPartiesContext.empty()) {
                    taskData.generationGuidance += "\n\n" + renderedPartiesContext;
                }

                if (stopToken.stop_requested() || !MARAS::IsSessionEpochCurrent(taskData.sessionEpoch)) {
                    return;
                }

                // 2. Marshal PublicRequestProfileUpdate back onto the SKSE main thread with SessionEpoch validation.
                const MARAS::SessionEpoch expectedEpoch = taskData.sessionEpoch;
                const std::uint32_t childID = taskData.childID;
                const MARAS::Foundation::ChildStage stage = taskData.stage;
                const std::size_t adultCount = taskData.involvedAdults.size();
                std::string childName = std::move(taskData.childName);
                std::string guidance = std::move(taskData.generationGuidance);

                MARAS::AddEpochTask(
                    expectedEpoch,
                    "SkyrimNetProfileService::SubmitProfileUpdate",
                    [childID, stage, adultCount, childName = std::move(childName), guidance = std::move(guidance)]() {
                        if (!IsSkyrimNetAvailable() ||
                            SkyrimNetProfileService::IsSuppressed() ||
                            SkyrimNetMemoryService::IsSuppressed()) {
                            return;
                        }
                        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
                        if (!actor || actor->IsDeleted()) {
                            MARAS_LOG_WARN("[SkyrimNetProfile] Spawned child actor {:08X} ('{}', stage={}) no longer valid before profile submission; skipping",
                                           childID, childName, static_cast<int>(stage));
                            return;
                        }
                        if (RequestProfileUpdateExport(childID, guidance, kAllProfileBlocksCSV)) {
                            MARAS_LOG_INFO("[SkyrimNetProfile] Queued profile update via PublicRequestProfileUpdate for spawned child actor {:08X} ('{}', stage={}, involvedAdults={})",
                                           childID, childName, static_cast<int>(stage), adultCount);
                        } else {
                            MARAS_LOG_WARN("[SkyrimNetProfile] PublicRequestProfileUpdate unavailable or returned false for {:08X} ('{}', stage={})",
                                           childID, childName, static_cast<int>(stage));
                        }
                    });
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[SkyrimNetProfile] Exception in ExecuteAsyncChildProfileFlow: {}", e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[SkyrimNetProfile] Unknown exception in ExecuteAsyncChildProfileFlow");
            }
        }

    }  // namespace

    SkyrimNetProfileService& SkyrimNetProfileService::GetSingleton() {
        static SkyrimNetProfileService singleton;
        return singleton;
    }

    SkyrimNetProfileService::SkyrimNetProfileService()
        : queueState_(std::make_shared<WorkerQueueState>()) {}

    SkyrimNetProfileService::~SkyrimNetProfileService() {
        if (queueState_) {
            queueState_->shuttingDown.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(queueState_->queueMutex);
            queueState_->pendingQueue.clear();
        }
        workerThread_.request_stop();
        if (workerThread_.joinable()) {
            workerThread_.detach();
        }
    }

    std::string SkyrimNetProfileService::GetActorBioTemplateName(std::uint32_t actorFormID) const {
        if (!IsSkyrimNetAvailable() || actorFormID == 0 || MARAS::IsSyntheticChildID(actorFormID)) {
            return "";
        }
        return ResolvePlacedActorBioTemplateName(actorFormID);
    }

    void SkyrimNetProfileService::Revert() {
        if (queueState_) {
            std::lock_guard<std::mutex> qLock(queueState_->queueMutex);
            queueState_->pendingQueue.clear();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            updatedActorStages_.clear();
        }
        MARAS_LOG_INFO("[SkyrimNetProfile] Reverted session profile update tracking state");
    }

    void SkyrimNetProfileService::MarkActorStageUpdated(std::uint32_t childID, MARAS::Foundation::ChildStage stage) {
        if (childID == 0 || childID == MARAS::kPlayerFormID || MARAS::IsSyntheticChildID(childID)) {
            return;
        }
        if (stage != MARAS::Foundation::ChildStage::Child && stage != MARAS::Foundation::ChildStage::Adult) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        updatedActorStages_.insert(MakeStageKey(childID, stage));
    }

    bool SkyrimNetProfileService::HasUpdatedActorStage(std::uint32_t childID, MARAS::Foundation::ChildStage stage) const {
        if (childID == 0) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return updatedActorStages_.contains(MakeStageKey(childID, stage));
    }

    void SkyrimNetProfileService::EnqueueProfileTask(std::unique_ptr<ChildProfileContextData> taskData) {
        if (!taskData || !queueState_) {
            return;
        }
        std::jthread finishedWorkerToJoin;
        std::optional<std::uint64_t> evictedStageKey;
        {
            std::lock_guard<std::mutex> lock(queueState_->queueMutex);
            if (queueState_->shuttingDown.load(std::memory_order_acquire) ||
                !MARAS::IsSessionEpochCurrent(taskData->sessionEpoch)) {
                return;
            }
            constexpr std::size_t kMaxPendingTasks = 32;
            if (queueState_->pendingQueue.size() >= kMaxPendingTasks) {
                const auto& dropped = queueState_->pendingQueue.front();
                if (dropped) {
                    evictedStageKey = MakeStageKey(dropped->childID, dropped->stage);
                }
                MARAS_LOG_WARN("[SkyrimNetProfile] Profile task queue full ({}); dropping oldest queued task for {:08X} ('{}')",
                               queueState_->pendingQueue.size(),
                               dropped ? dropped->childID : 0,
                               dropped ? dropped->childName : "");
                queueState_->pendingQueue.pop_front();
            }
            queueState_->pendingQueue.push_back(std::move(taskData));
            if (!queueState_->workerActive) {
                finishedWorkerToJoin = std::move(workerThread_);
                queueState_->workerActive = true;
                auto state = queueState_;
                workerThread_ = std::jthread([state = std::move(state)](std::stop_token st) mutable {
                    WorkerLoop(std::move(state), st);
                });
            }
        }
        if (evictedStageKey.has_value()) {
            std::lock_guard<std::mutex> lock(mutex_);
            updatedActorStages_.erase(*evictedStageKey);
        }
        if (finishedWorkerToJoin.joinable()) {
            finishedWorkerToJoin.join();
        }
    }

    void SkyrimNetProfileService::WorkerLoop(std::shared_ptr<WorkerQueueState> queueState, std::stop_token stopToken) {
        if (!queueState) {
            return;
        }
        for (;;) {
            std::unique_ptr<ChildProfileContextData> taskData;
            {
                std::lock_guard<std::mutex> lock(queueState->queueMutex);
                if (stopToken.stop_requested() ||
                    queueState->shuttingDown.load(std::memory_order_acquire) ||
                    queueState->pendingQueue.empty()) {
                    queueState->workerActive = false;
                    return;
                }
                taskData = std::move(queueState->pendingQueue.front());
                queueState->pendingQueue.pop_front();
            }
            if (!taskData) {
                continue;
            }
            if (stopToken.stop_requested() || queueState->shuttingDown.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(queueState->queueMutex);
                queueState->workerActive = false;
                return;
            }
            if (!MARAS::IsSessionEpochCurrent(taskData->sessionEpoch)) {
                MARAS_LOG_DEBUG("[SkyrimNetProfile] Dropping queued profile task for {:08X} ('{}'): stale session epoch",
                                taskData->childID, taskData->childName);
                continue;
            }
            try {
                ExecuteAsyncChildProfileFlow(*taskData, stopToken);
            } catch (const std::exception& e) {
                if (!queueState->shuttingDown.load(std::memory_order_acquire) && !stopToken.stop_requested()) {
                    MARAS_LOG_ERROR("[SkyrimNetProfile] Exception in WorkerLoop for {:08X}: {}",
                                    taskData->childID, e.what());
                }
            } catch (...) {
                if (!queueState->shuttingDown.load(std::memory_order_acquire) && !stopToken.stop_requested()) {
                    MARAS_LOG_ERROR("[SkyrimNetProfile] Unknown exception in WorkerLoop for {:08X}",
                                    taskData->childID);
                }
            }
        }
    }

    void SkyrimNetProfileService::RequestChildProfileUpdate(std::uint32_t childID, MARAS::Foundation::ChildStage stage) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }
        if (IsSuppressed() || SkyrimNetMemoryService::IsSuppressed()) {
            return;
        }
        // Strictly placed actors only (never synthetic child records)
        if (childID == 0 || childID == MARAS::kPlayerFormID || MARAS::IsSyntheticChildID(childID)) {
            return;
        }
        // Strictly Young Child spawn (Stage 2) or Adult Child spawn (Stage 4) only
        if (stage != MARAS::Foundation::ChildStage::Child && stage != MARAS::Foundation::ChildStage::Adult) {
            return;
        }

        const std::uint64_t stageKey = MakeStageKey(childID, stage);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!updatedActorStages_.insert(stageKey).second) {
                MARAS_LOG_DEBUG("[SkyrimNetProfile] Profile update already requested for actor {:08X} at stage {}; skipping",
                                childID, static_cast<int>(stage));
                return;
            }
        }

        const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();
        if (!MARAS::AddEpochTask(expectedEpoch, "SkyrimNetProfileService::GatherChildProfileContext", [this, childID, stage, stageKey, expectedEpoch]() {
                if (!IsSkyrimNetAvailable() || IsSuppressed() || SkyrimNetMemoryService::IsSuppressed()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    updatedActorStages_.erase(stageKey);
                    return;
                }

                auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
                if (!actor || actor->IsDeleted()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    updatedActorStages_.erase(stageKey);
                    MARAS_LOG_WARN("[SkyrimNetProfile] Skipping profile update for {:08X}: not a valid placed RE::Actor", childID);
                    return;
                }

                std::unique_ptr<ChildProfileContextData> taskData;
                {
                    std::lock_guard papyrusLock(MARAS::GetPapyrusMutex());
                    taskData = std::make_unique<ChildProfileContextData>(
                        GatherChildProfileContextOnGameThread(childID, stage, actor));
                }
                taskData->sessionEpoch = expectedEpoch;

                MARAS_LOG_INFO("[SkyrimNetProfile] Queuing async profile update for spawned child actor {:08X} ('{}', stage={}, uuid={:X}, bio='{}', involvedAdults={})",
                               childID, taskData->childName, static_cast<int>(stage), taskData->actorUUID, taskData->bioTemplateName, taskData->involvedAdults.size());

                EnqueueProfileTask(std::move(taskData));
            })) {
            std::lock_guard<std::mutex> lock(mutex_);
            updatedActorStages_.erase(stageKey);
        }
    }

    bool SkyrimNetProfileService::RequestActorProfileUpdate(
        std::uint32_t actorFormID,
        const std::string& additionalInstructions,
        const std::string& updatableBlocksCSV)
    {
        if (!IsSkyrimNetAvailable() || IsSuppressed() || SkyrimNetMemoryService::IsSuppressed()) {
            return false;
        }
        if (actorFormID == 0 || MARAS::IsSyntheticChildID(actorFormID)) {
            return false;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
        if (!actor || actor->IsDeleted()) {
            return false;
        }

        if (RequestProfileUpdateExport(actorFormID, additionalInstructions, updatableBlocksCSV)) {
            MARAS_LOG_INFO("[SkyrimNetProfile] Queued profile update via PublicRequestProfileUpdate for {:08X} ('{}')",
                           actorFormID, MARAS::ActorSvc::GetName(actorFormID));
            return true;
        }

        MARAS_LOG_WARN("[SkyrimNetProfile] PublicRequestProfileUpdate unavailable or returned false for {:08X} ('{}')",
                       actorFormID, MARAS::ActorSvc::GetName(actorFormID));
        return false;
    }

}  // namespace MARAS::Bridge::SkyrimNet
