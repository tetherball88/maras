#include "core/SkyrimNetProfileService.h"

#include "core/HomeCellService.h"
#include "core/LoggingService.h"
#include "core/NPCRelationshipManager.h"
#include "core/PlayerHouseService.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SkyrimNetMemoryService.h"
#include "services/ActorService.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace MARAS {

    namespace {

        using PublicFormIDToUUID_t         = uint64_t (*)(uint32_t formId);
        using PublicGetBioTemplateName_t   = std::string (*)(uint32_t formId);
        using PublicGetSaveUniqueID_t      = std::string (*)();
        using PublicRequestProfileUpdate_t = bool (*)(uint32_t formId, const char* additionalInstructions, const char* updatableBlocksCSV);
        using PublicGetMemoriesForActor_t  = std::string (*)(uint32_t formId, int maxCount, const char* contextQuery);

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
{{ render_character_profile("ttm_gm", adult.uuid) }}

                {% endif %}
            {% endfor %}
        {% endif %}
        {% if existsIn(parents, "recent_events_participants") and length(parents.recent_events_participants) > 0 %}
## Family & Household Recent Events (Chronological)
{{ render_template("helpers\\maras_actor_bio_recent_events") }}
        {% endif %}
    {% endif %}
{% endif %})";

        PublicRequestProfileUpdate_t FindRequestProfileUpdate() {
            HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
            if (!hMod) return nullptr;
            return reinterpret_cast<PublicRequestProfileUpdate_t>(GetProcAddress(hMod, "PublicRequestProfileUpdate"));
        }

        std::string SafeJsonDump(const nlohmann::json& j) {
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        uint64_t ResolvePlacedActorUUID(RE::FormID actorFormID) {
            if (actorFormID == 0 || actorFormID == kUnknownFatherFormID) return 0;
            HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
            if (!hMod) return 0;
            if (auto* fn = reinterpret_cast<PublicFormIDToUUID_t>(GetProcAddress(hMod, "PublicFormIDToUUID"))) {
                // Pass the placed RE::Actor reference FormID directly first so SkyrimNet's
                // ResolveActorEntity(formId) registers the live Actor in EntityMap and UUIDResolver.
                uint64_t uuid = fn(actorFormID);
                if (uuid != 0) {
                    return uuid;
                }
            }
            return GetActorUUID(actorFormID);
        }

        std::string ResolvePlacedActorBioTemplateName(RE::FormID actorFormID) {
            if (actorFormID == 0 || actorFormID == kUnknownFatherFormID) return "";
            if (actorFormID == kPlayerFormID) {
                return "player_special";
            }
            HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
            if (!hMod) return "";
            if (auto* fn = reinterpret_cast<PublicGetBioTemplateName_t>(GetProcAddress(hMod, "PublicGetBioTemplateName"))) {
                std::string name = fn(actorFormID);
                if (!name.empty()) {
                    return name;
                }
            }
            return "";
        }

        std::string ResolveActiveSaveUniqueID() {
            HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
            if (!hMod) return "";
            if (auto* fn = reinterpret_cast<PublicGetSaveUniqueID_t>(GetProcAddress(hMod, "PublicGetSaveUniqueID"))) {
                return fn();
            }
            return "";
        }

        struct WebServerEndpoint {
            bool enabled = true;
            std::string host = "127.0.0.1";
            int port = 8080;
        };

        WebServerEndpoint ReadSkyrimNetWebServerConfig() {
            WebServerEndpoint cfg;
            try {
                std::filesystem::path configPath = "Data/SKSE/Plugins/SkyrimNet/config/WebServer.yaml";
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
                    std::string trimmed = StringUtils::TrimStd(line);
                    if (trimmed.empty() || trimmed.front() == '#') {
                        continue;
                    }
                    auto colonPos = trimmed.find(':');
                    if (colonPos == std::string::npos) {
                        continue;
                    }
                    std::string key = StringUtils::ToLowerStd(StringUtils::TrimStd(trimmed.substr(0, colonPos)));
                    std::string val = StringUtils::TrimStd(trimmed.substr(colonPos + 1));
                    // Strip inline comment
                    if (auto hashPos = val.find('#'); hashPos != std::string::npos) {
                        val = StringUtils::TrimStd(val.substr(0, hashPos));
                    }
                    // Strip surrounding quotes
                    if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
                        val = val.substr(1, val.size() - 2);
                    }

                    if (key == "enabled") {
                        std::string vLower = StringUtils::ToLowerStd(val);
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
            int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (needed <= 0) return {};
            std::wstring out(static_cast<size_t>(needed), L'\0');
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

            std::wstring wHost = Utf8ToWide(host);
            std::wstring wMethod = Utf8ToWide(method);
            std::wstring wPath = Utf8ToWide(pathAndQuery);
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

            WinHttpSetTimeouts(hSession, 3000, 3000, timeoutMs, timeoutMs);

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
            DWORD headersLen = jsonBody.empty() ? 0 : static_cast<DWORD>(-1L);

            BOOL sent = WinHttpSendRequest(
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

        struct InvolvedAdultProfileRef {
            RE::FormID formID = 0;
            uint64_t uuid = 0;
            std::string bioTemplateName;
            std::string name;
            std::string raceName;
            std::string roleLabel;
            bool isGuardian = false;
        };

        struct ChildProfileContextData {
            RE::FormID childID = 0;
            ChildStage stage = ChildStage::Child;
            uint64_t actorUUID = 0;
            std::string bioTemplateName;
            std::string saveId;
            std::string childName;
            std::string raceName;
            bool isFemale = false;
            bool isAdopted = false;
            bool isBiologicalChild = false;

            // Biological Mother
            RE::FormID bioMotherID = 0;
            uint64_t bioMotherUUID = 0;
            std::string motherName;
            std::string motherRace;
            bool motherIsGuardian = false;

            // True Biological Father
            RE::FormID bioFatherID = 0;
            uint64_t bioFatherUUID = 0;
            std::string bioFatherName;
            std::string bioFatherRace;
            bool bioFatherIsGuardian = false;

            // Presumed Father (Mother's partner at conception/birth if distinct from bioFatherID)
            RE::FormID presumedFatherID = 0;
            uint64_t presumedFatherUUID = 0;
            std::string presumedFatherName;
            std::string presumedFatherRace;
            bool presumedFatherIsGuardian = false;
            std::string motherPresumedRelStatus;

            // Custodial Guardians
            RE::FormID guardianAID = 0;
            RE::FormID guardianBID = 0;
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
            std::string generationGuidance;
        };

        std::string JoinNamesForProse(const std::vector<std::string>& names) {
            if (names.empty()) return "";
            if (names.size() == 1) return names[0];
            if (names.size() == 2) return names[0] + " and " + names[1];
            std::string out;
            for (size_t i = 0; i < names.size(); ++i) {
                if (i > 0) {
                    out += (i + 1 == names.size()) ? ", and " : ", ";
                }
                out += names[i];
            }
            return out;
        }

        std::string BuildProfileGenerationGuidance(const ChildProfileContextData& d) {
            const std::string relWord = d.isFemale ? "daughter" : "son";
            const std::string pronounSubj = d.isFemale ? "She" : "He";
            const std::string pronounPoss = d.isFemale ? "her" : "his";
            std::ostringstream ss;

            if (d.stage == ChildStage::Adult) {
                ss << "IMPORTANT: " << d.childName << " is a GROWN YOUNG ADULT " << relWord
                   << " who was born and raised in this playthrough and has now completed training and come of age as an adult follower/citizen in Skyrim. "
                   << "Do NOT describe " << d.childName << " as a small child.\n";
            } else {
                ss << "IMPORTANT: " << d.childName << " is a YOUNG CHILD (" << relWord
                   << ", roughly 8-11 years old) who was born in this playthrough and has just grown from infancy into a young child living at home. "
                   << "Write the profile, occupation, skills, aspirations, and speech_style specifically for a young child in Skyrim, NOT an adult.\n";
            }

            ss << "\n## Involved Parties at the Moment of Bio Generation/Update\n";
            if (!d.motherName.empty()) {
                ss << "- **Biological Mother**: " << d.motherName;
                if (!d.motherRace.empty()) ss << " (" << d.motherRace << ")";
                ss << (d.motherIsGuardian ? " — Custodial Household Guardian raising " + d.childName : " — Non-custodial biological mother") << ".\n";
            }
            if (!d.bioFatherName.empty()) {
                ss << "- **True Biological Father**: " << d.bioFatherName;
                if (!d.bioFatherRace.empty()) ss << " (" << d.bioFatherRace << ")";
                if (d.hasPaternitySecret) {
                    ss << " — Genetic sire of " << d.childName << " ("
                       << (d.bioFatherKnowsPaternity ? "KNOWS he is the biological father" : "DOES NOT KNOW he is the biological father")
                       << (d.bioFatherIsGuardian ? "; also Custodial Household Guardian" : "") << ").\n";
                } else if (d.isSingleMotherSecretFromBioFather) {
                    ss << " — Genetic sire of " << d.childName << " (DOES NOT KNOW he is the biological father).\n";
                } else {
                    ss << (d.bioFatherIsGuardian ? " — Custodial Household Guardian raising " + d.childName : " — Non-custodial biological father") << ".\n";
                }
            }
            if (d.hasPresumedFather && !d.presumedFatherName.empty()) {
                ss << "- **Presumed Father (Mother's Partner)**: " << d.presumedFatherName;
                if (!d.presumedFatherRace.empty()) ss << " (" << d.presumedFatherRace << ")";
                if (!d.motherPresumedRelStatus.empty() && d.motherPresumedRelStatus != "Unknown") {
                    ss << " [Relationship with " << d.motherName << ": " << d.motherPresumedRelStatus << "]";
                }
                ss << " — "
                   << (d.partnerKnowsPaternity
                           ? "KNOWS that " + d.childName + " is NOT his biological child and was fathered by " + (d.bioFatherName.empty() ? "another man" : d.bioFatherName)
                           : "BELIEVES " + d.childName + " is his own biological " + relWord)
                   << (d.presumedFatherIsGuardian
                           ? "; actively raising " + d.childName + " as a Household Guardian"
                           : "; NOT a current household guardian")
                   << ".\n";
            }

            if (!d.guardianAName.empty() || !d.guardianBName.empty()) {
                std::vector<std::string> gNames;
                if (!d.guardianAName.empty()) gNames.push_back(d.guardianAName);
                if (!d.guardianBName.empty() && d.guardianBName != d.guardianAName) gNames.push_back(d.guardianBName);
                ss << "- **Custodial Household Guardians (Upbringing)**: " << JoinNamesForProse(gNames) << ".\n";
            }

            if (!d.siblingNames.empty()) {
                ss << "- **Siblings**: " << JoinNamesForProse(d.siblingNames) << ".\n";
            }
            if (!d.homeLocationName.empty()) {
                ss << "- **Family Home / Upbringing Location**: " << d.homeLocationName << ".\n";
            }
            if (d.stage == ChildStage::Adult && !d.trainingLocation.empty()) {
                ss << "- **Completed Adolescent Training At**: " << d.trainingLocation
                   << " (reflect this training background in their skills, personality, background, and occupation).\n";
            }

            ss << "\n## Biological Inheritance vs. Household Upbringing Rules\n";
            ss << "1. **Genetic & Supernatural Inheritance (Strictly from True Biological Parents)**:\n";
            if (!d.motherName.empty() || !d.bioFatherName.empty()) {
                ss << "   - " << d.childName << "'s biological, physical, and innate heritage comes strictly from ";
                if (!d.motherName.empty() && !d.bioFatherName.empty()) {
                    ss << "**" << d.motherName << "** (Biological Mother) and **" << d.bioFatherName << "** (True Biological Father)";
                } else if (!d.motherName.empty()) {
                    ss << "**" << d.motherName << "** (Biological Mother)";
                } else {
                    ss << "**" << d.bioFatherName << "** (True Biological Father)";
                }
                if (d.hasPresumedFather && !d.presumedFatherName.empty()) {
                    ss << " — NEVER genetically from the non-biological presumed father (" << d.presumedFatherName << ")";
                }
                ss << ".\n";
            }
            ss << "   - Carefully inspect the full rendered character profiles of the biological parents below—especially their **Background**, **Appearance**, **Occupation**, **Skills & Abilities**, and **Personality** sections (do NOT rely solely on a parent's brief `Summary`, which conceals hidden traits such as vampirism, lycanthropy, or secret practices due to the Stranger Test; account for any supernatural nature, bloodline, or magical/martial lineage present in their Background, Occupation, and Skills).\n";
            ss << "   - Reflect inherited physical traits (eye color, hair, complexion, facial structure, build, subtle vampiric/elven/beast/bloodline traits), innate temperament, and latent magical or martial affinities in " << d.childName << "'s `appearance`, `personality`, `background`, and `skills`.\n";
            if (d.hasPhenotypeMismatch && !d.visualDiscrepancyDescription.empty()) {
                ss << "   - **CRITICAL PHENOTYPE DISCREPANCY**: " << d.visualDiscrepancyDescription
                   << " Ensure " << d.childName << "'s `appearance` accurately reflects this biological heritage.\n";
            }
            const std::string bioFatherLabel = d.bioFatherName.empty() ? "the biological father" : d.bioFatherName;
            const std::string bioMotherLabel = d.motherName.empty() ? "the biological mother" : d.motherName;
            const bool bioFatherUnaware = !d.bioFatherKnowsPaternity || d.isSingleMotherSecretFromBioFather;

            ss << "2. **Biological Father's Impact (" << bioFatherLabel << ")**:\n"
               << "   - **Innate Traits**: " << d.childName << " inherits genetic appearance, bloodline/supernatural traits, and innate temperament tendencies from "
               << bioFatherLabel << " and " << bioMotherLabel << ".\n";
            if (bioFatherUnaware) {
                ss << "   - **Behavioral & Mentorship Impact**: Because " << bioFatherLabel << " does **NOT** know he is " << d.childName << "'s father, he has **NOT** mentored or shaped " << d.childName << "'s learned habits, speech style, or upbringing; his contribution is strictly genetic and innate.\n";
            } else if (d.hasPaternitySecret && !d.partnerKnowsPaternity) {
                ss << "   - **Behavioral & Mentorship Impact**: Because " << bioFatherLabel << " secretly knows " << d.childName << " is his biological " << relWord << " while " << d.presumedFatherName << " remains unaware, " << bioFatherLabel << " subtly influences " << d.childName << " from the sidelines (quiet protectiveness, gifts, occasional advice, or watchful interest consistent with his rendered personality) without exposing the secret.\n";
            } else if (d.hasPaternitySecret) {
                ss << "   - **Behavioral & Mentorship Impact**: Because " << bioFatherLabel << " is known to be " << d.childName << "'s biological father"
                   << (d.bioFatherIsGuardian ? " and is raising " + d.childName + " as a household guardian" : "")
                   << ", his personality, mentorship, and example in his rendered profile directly shape " << d.childName << "'s behavior, learned skills, speech style, aspirations, and self-identity.\n";
            } else if (d.bioFatherIsGuardian) {
                ss << "   - **Behavioral & Mentorship Impact**: " << bioFatherLabel << " is actively raising " << d.childName << " in the household; his personality, mentorship, trade, and speech patterns in his rendered profile directly shape " << d.childName << "'s behavior, learned skills, speech style, aspirations, and values.\n";
            } else {
                ss << "   - **Behavioral & Mentorship Impact**: " << bioFatherLabel << " is " << d.childName << "'s known non-custodial biological father; reflect how his identity and example in his rendered profile influence " << d.childName << "'s self-identity, aspirations, and relationship with him.\n";
            }

            ss << "3. **Custodial Guardians & Presumed Father's Impact on Raising " << d.childName << "**:\n"
               << "   - " << d.childName << "'s daily habits, manners, speech style, childhood memories, values, and household relationships are shaped by the adults who actually raised "
               << (d.isFemale ? "her" : "him") << " and by the family's chronological recent events below.\n";
            if (d.hasPresumedFather && !d.presumedFatherName.empty()) {
                ss << "   - **Presumed Father (" << d.presumedFatherName << ") Upbringing Impact**: ";
                if (!d.partnerKnowsPaternity) {
                    ss << d.presumedFatherName << " **does NOT know** the paternity truth and believes " << d.childName << " is his own biological " << relWord << ". He raises " << d.childName << " wholeheartedly as his own flesh and blood; " << d.childName << " absorbs his habits, values, trade/interests, and speech patterns from his rendered profile, viewing him with uncomplicated filial devotion.\n";
                } else if (d.presumedFatherIsGuardian) {
                    ss << d.presumedFatherName << " **KNOWS** that " << d.childName << " is not biologically his (fathered by " << bioFatherLabel << ") while raising " << d.childName << " as a household guardian. Examine " << d.presumedFatherName << "'s personality and temperament in his rendered profile to determine how knowing the child is not biologically his shapes the way he raises or treats " << d.childName << " (devoted, forgiving father figure vs. wounded pride, sternness, emotional distance, or household tension with " << d.motherName << "), and reflect that impact on " << d.childName << "'s behavior, emotional outlook, and relationship with him.\n";
                } else {
                    ss << d.presumedFatherName << " **KNOWS** that " << d.childName << " is not biologically his (fathered by " << bioFatherLabel << ") and is not a current household guardian. Reflect how the history of him knowing the truth (and any resulting separation or tension with " << d.motherName << " in his rendered profile and recent events) shapes " << d.childName << "'s family background and view of him.\n";
                }
            }

            if (d.hasPaternitySecret && !d.presumedFatherName.empty() && !d.bioFatherName.empty()) {
                ss << "\n## Paternity Secret & Knowledge Permutation Rules (CRITICAL)\n";
                if (!d.partnerKnowsPaternity && !d.bioFatherKnowsPaternity) {
                    // Permutation 1: Neither Presumed Father nor Biological Father knows
                    ss << "- **Active Permutation — NEITHER Presumed Father (" << d.presumedFatherName << ") NOR Biological Father (" << d.bioFatherName << ") Knows the Truth**:\n"
                       << "  - Only " << d.motherName << " knows that " << d.bioFatherName << " is " << d.childName << "'s true biological father.\n"
                       << "  - **Presumed Father (" << d.presumedFatherName << ")**: Completely unaware; genuinely believes " << d.childName << " is his own biological " << relWord << " and raises " << (d.isFemale ? "her" : "him") << " wholeheartedly as his own child.\n"
                       << "  - **Biological Father (" << d.bioFatherName << ")**: Completely unaware that " << d.childName << " is his biological " << relWord << "; he contributes ONLY genetic/innate traits (never paternal mentorship or learned behavior).\n"
                       << "  - **Child (" << d.childName << ")**: Genuinely believes " << d.presumedFatherName << " is " << pronounPoss << " biological father, emulates " << d.presumedFatherName << "'s learned habits and values, and has NO idea that " << d.bioFatherName << " is " << pronounPoss << " sire.\n"
                       << "  - **How to write the bio blocks**:\n"
                       << "    - `summary` & `interject_summary`: Present " << d.childName << " as the " << relWord << " of " << d.motherName << " and " << d.presumedFatherName << ". Do NOT mention " << d.bioFatherName << " or the secret in `summary`.\n"
                       << "    - `appearance`, `personality`, & `skills`: " << d.childName << " STILL biologically inherits physical features, innate temperament, and latent affinities from **" << d.motherName << "** and **" << d.bioFatherName << "** (describe these traits naturally as part of " << d.childName << " without " << d.childName << " knowing they come from " << d.bioFatherName << "), while learned habits and childhood lessons come from **" << d.motherName << "** and **" << d.presumedFatherName << "**.\n"
                       << "    - `background`, `relationships`, & `aspirations`: Write from " << d.childName << "'s lived experience believing " << d.presumedFatherName << " is " << pronounPoss << " father. In `relationships`, list " << d.presumedFatherName << " as Father; do NOT list " << d.bioFatherName << " as biological father (or note that " << d.childName << " is completely unaware of any blood tie to " << d.bioFatherName << ").\n";
                } else if (!d.partnerKnowsPaternity && d.bioFatherKnowsPaternity) {
                    // Permutation 2: Only Biological Father knows; Presumed Father does NOT know
                    ss << "- **Active Permutation — ONLY Biological Father (" << d.bioFatherName << ") Knows; Presumed Father (" << d.presumedFatherName << ") Does NOT Know**:\n"
                       << "  - Both " << d.motherName << " and " << d.bioFatherName << " secretly know that " << d.bioFatherName << " is " << d.childName << "'s true biological father.\n"
                       << "  - **Presumed Father (" << d.presumedFatherName << ") & Child (" << d.childName << ")**: Remain unaware; " << d.presumedFatherName << " raises " << d.childName << " believing " << (d.isFemale ? "she" : "he") << " is his own biological child, and " << d.childName << " believes " << d.presumedFatherName << " is " << pronounPoss << " biological father.\n"
                       << "  - **Biological Father (" << d.bioFatherName << ")**: Contributes genetic/innate traits AND, because he secretly knows " << d.childName << " is his child, may subtly influence " << d.childName << " from the sidelines (e.g., quiet protectiveness, gifts, occasional advice, or watchful interest consistent with " << d.bioFatherName << "'s rendered personality) without exposing the secret.\n"
                       << "  - **How to write the bio blocks**:\n"
                       << "    - `summary` & `interject_summary`: Present " << d.childName << " publicly as the " << relWord << " of " << d.motherName << " and " << d.presumedFatherName << ". Do NOT expose the secret in `summary`.\n"
                       << "    - `appearance`, `personality`, & `skills`: Biologically inherited from **" << d.motherName << "** and **" << d.bioFatherName << "**, combined with household upbringing from **" << d.motherName << "** and **" << d.presumedFatherName << "**.\n"
                       << "    - `background` & `relationships`: " << d.childName << " views " << d.presumedFatherName << " as " << pronounPoss << " biological father. However, because " << d.bioFatherName << " knows the truth, `background` and `relationships` may reflect that " << d.bioFatherName << " has shown a quiet, watchful fondness or special interest toward " << d.childName << " (while " << d.childName << " and " << d.presumedFatherName << " remain unaware that " << d.bioFatherName << " is the biological sire).\n";
                } else if (d.partnerKnowsPaternity && !d.bioFatherKnowsPaternity) {
                    // Permutation 3: Only Presumed Father knows; Biological Father does NOT know
                    ss << "- **Active Permutation — ONLY Presumed Father (" << d.presumedFatherName << ") Knows; Biological Father (" << d.bioFatherName << ") Does NOT Know**:\n"
                       << "  - Both " << d.motherName << " and " << d.presumedFatherName << " know that " << d.childName << " is NOT " << d.presumedFatherName << "'s biological child and was fathered by " << d.bioFatherName << ".\n"
                       << "  - **Biological Father (" << d.bioFatherName << ")**: Has never been told and remains completely unaware that " << d.childName << " is his biological " << relWord << "; he contributes genetic/innate traits, but has NOT actively mentored or parented " << d.childName << ".\n"
                       << "  - **How to write the bio blocks**:\n"
                       << "    - `appearance`, `personality`, & `skills`: Genetic appearance and innate affinities come from **" << d.motherName << "** and **" << d.bioFatherName << "**.\n"
                       << "    - `background`, `personality`, `aspirations`, & `relationships`: ";
                    if (d.presumedFatherIsGuardian) {
                        ss << d.presumedFatherName << " continues raising " << d.childName << " in the household alongside " << d.motherName << " while knowing " << d.childName << " was biologically fathered by " << d.bioFatherName << ". Based on " << d.presumedFatherName << "'s rendered profile, reflect how his knowledge of the affair/paternity shapes his parenting (whether he raises " << d.childName << " with forgiving warmth as a step-father/guardian or with guarded pride/tension) and how that shapes " << d.childName << "'s behavior. Also reflect that while the household knows " << d.bioFatherName << " is the biological father, " << d.bioFatherName << " himself remains unaware.\n";
                    } else {
                        ss << d.presumedFatherName << " was initially presumed to be " << d.childName << "'s father and knows the truth, but is not a current household guardian; meanwhile, " << d.bioFatherName << " still does not know he is " << d.childName << "'s biological father.\n";
                    }
                    ss << "    - `summary`: Keep `summary` focused on publicly observable facts (age, race, mother, and current household guardians), keeping private paternity details in `background` and `relationships`.\n";
                } else {
                    // Permutation 4: Both Presumed Father and Biological Father know
                    ss << "- **Active Permutation — BOTH Presumed Father (" << d.presumedFatherName << ") AND Biological Father (" << d.bioFatherName << ") Know the Truth**:\n"
                       << "  - The paternity truth is known to both " << d.presumedFatherName << " and " << d.bioFatherName << " (as well as " << d.motherName << " and the family): " << d.bioFatherName << " is openly known to be " << d.childName << "'s true biological father, while " << d.presumedFatherName << " was initially presumed to be the father.\n"
                       << "  - **How to write the bio blocks**:\n"
                       << "    - `appearance`, `personality`, & `skills`: Biologically inherited from **" << d.motherName << "** and **" << d.bioFatherName << "**.\n"
                       << "    - `background`, `personality`, `speech_style`, `aspirations`, & `relationships`: Because the truth is known to both fathers and the family, **" << d.bioFatherName << "** impacts not only " << d.childName << "'s innate traits but also " << pronounPoss << " behavior, self-identity, and aspirations (e.g., emulating, seeking approval from, or reacting to " << d.bioFatherName << "). ";
                    if (d.presumedFatherIsGuardian) {
                        ss << "Meanwhile, in the household, **" << d.presumedFatherName << "** raises " << d.childName << " as a guardian and father figure alongside " << d.motherName << " while knowing the truth; reflect how both " << d.presumedFatherName << "'s day-to-day raising (shaped by his personality and knowledge of the truth) and " << d.bioFatherName << "'s known biological fatherhood shape " << d.childName << "'s behavior and distinct bonds with both men.\n";
                    } else if (d.bioFatherIsGuardian) {
                        ss << d.motherName << " and " << d.bioFatherName << " are raising " << d.childName << " as custodial guardians (so " << d.bioFatherName << " shapes both genetic traits and daily upbringing), with " << d.presumedFatherName << " as " << d.motherName << "'s former partner who was once presumed to be the father.\n";
                    } else {
                        ss << "Reflect " << d.childName << "'s upbringing by " << pronounPoss << " household guardian(s) and how knowing both biological father " << d.bioFatherName << " and former presumed father " << d.presumedFatherName << " shapes " << d.childName << "'s behavior and relationships.\n";
                    }
                    ss << "    - `summary`: Keep `summary` focused on publicly observable facts, placing detailed family history in `background` and `relationships`.\n";
                }
            } else if (d.isSingleMotherSecretFromBioFather && !d.bioFatherName.empty()) {
                ss << "\n## Paternity Secret Rules\n"
                   << "- **" << d.motherName << " is keeping " << d.childName << "'s paternity a secret from Biological Father (" << d.bioFatherName << ")**:\n"
                   << "  - " << d.bioFatherName << " does not know that " << d.childName << " is his biological " << relWord << ", so he contributes ONLY genetic/physical/supernatural traits and innate temperament (not paternal mentorship or learned behavior).\n"
                   << "  - " << d.childName << "'s upbringing, learned habits, and daily behavior are shaped by " << d.motherName << " (and any household guardian).\n";
            }

            return ss.str();
        }

        std::string ExtractPromptBlock(const std::string& rawPrompt, std::string_view blockName) {
            const std::string openTag = std::format("{{% block {} %}}", blockName);
            const std::string closeTag = "{% endblock %}";
            auto startPos = rawPrompt.find(openTag);
            if (startPos == std::string::npos) {
                return "";
            }
            startPos += openTag.size();
            auto endPos = rawPrompt.find(closeTag, startPos);
            if (endPos == std::string::npos) {
                return "";
            }
            return StringUtils::TrimStd(rawPrompt.substr(startPos, endPos - startPos));
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
                {"skills", "Skills & Abilities"},
                {"relationships", "Relationships"}
            };

            std::ostringstream out;
            for (const auto& b : kBlocks) {
                std::string blockText = ExtractPromptBlock(rawContent, b.key);
                if (!blockText.empty()) {
                    out << "#### " << b.title << "\n" << blockText << "\n";
                }
            }

            HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
            if (hMod && adult.formID != 0) {
                if (auto* memFn = reinterpret_cast<PublicGetMemoriesForActor_t>(GetProcAddress(hMod, "PublicGetMemoriesForActor"))) {
                    std::string memJsonStr = memFn(adult.formID, 5, "");
                    if (!memJsonStr.empty() && memJsonStr != "[]") {
                        auto memArr = nlohmann::json::parse(memJsonStr, nullptr, false);
                        if (memArr.is_array() && !memArr.empty()) {
                            out << "#### Memories\n";
                            for (const auto& m : memArr) {
                                if (m.is_object()) {
                                    std::string content = m.value("content", "");
                                    if (!content.empty()) {
                                        out << "- " << content << "\n";
                                    }
                                }
                            }
                        }
                    }
                }
            }

            return StringUtils::TrimStd(out.str());
        }

        std::string RenderInvolvedPartiesContext(
            const ChildProfileContextData& taskData,
            const WebServerEndpoint& endpoint)
        {
            // 1. Primary path: render helpers/maras_child_bio_context via SkyrimNet's ContextEngine
            //    through POST /prompts?api=render-template-preview so all involved adults' bios
            //    (render_character_profile("ttm_gm", adult.uuid)) and family recent events are rendered natively.
            if (endpoint.enabled && taskData.actorUUID != 0) {
                auto tryRenderPreview = [&](const nlohmann::json& payload) -> std::string {
                    auto resp = SendLocalHttpRequest(
                        "POST",
                        endpoint.host,
                        endpoint.port,
                        "/prompts?api=render-template-preview",
                        SafeJsonDump(payload),
                        15000);
                    if (resp.statusCode == 200 && !resp.body.empty()) {
                        nlohmann::json parsed = nlohmann::json::parse(resp.body, nullptr, false);
                        if (parsed.is_object() && parsed.value("success", false)) {
                            std::string rendered = StringUtils::TrimStd(parsed.value("renderedContent", ""));
                            if (!rendered.empty()) {
                                return rendered;
                            }
                        }
                    }
                    return "";
                };

                const std::string actorUUIDHex = std::format("{:X}", taskData.actorUUID);
                std::string rendered = tryRenderPreview({
                    {"templateName", "helpers/maras_child_bio_context"},
                    {"actorUUID", actorUUIDHex}
                });
                if (rendered.empty()) {
                    rendered = tryRenderPreview({
                        {"content", kInlineChildBioContextTemplate},
                        {"actorUUID", actorUUIDHex}
                    });
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
            std::string result = StringUtils::TrimStd(fallbackOut.str());
            if (!result.empty()) {
                MARAS_LOG_INFO("[SkyrimNetProfile] Built fallback involved parties bio context ({} bytes) for {:08X} ('{}')",
                               result.size(), taskData.childID, taskData.childName);
            }
            return result;
        }

        ChildProfileContextData GatherChildProfileContextOnGameThread(
            RE::FormID childID,
            ChildStage stage,
            RE::Actor* actor)
        {
            ChildProfileContextData data;
            data.childID = childID;
            data.stage = stage;
            data.actorUUID = ResolvePlacedActorUUID(childID);
            data.bioTemplateName = ResolvePlacedActorBioTemplateName(childID);
            data.saveId = ResolveActiveSaveUniqueID();

            data.childName = ActorSvc::GetName(childID);
            data.isFemale = (ActorSvc::GetSex(childID) == 1);
            data.raceName = ActorSvc::GetRaceName(childID);

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
                        data.homeLocationName = StringUtils::TrimStd(rawLoc);
                    }
                } else if (auto* cell = actor->GetParentCell()) {
                    const char* rawCell = cell->GetName();
                    if (rawCell && rawCell[0] != '\0') {
                        data.homeLocationName = StringUtils::TrimStd(rawCell);
                    }
                }
            }

            auto& mgr = NPCRelationshipManager::GetSingleton();
            if (auto entryOpt = mgr.GetChildEntry(childID)) {
                const auto& entry = *entryOpt;
                if (data.childName.empty() && !entry.name.empty()) {
                    data.childName = StringUtils::TrimStd(entry.name);
                }
                if (entry.gender == 1) {
                    data.isFemale = true;
                } else if (entry.gender == 0) {
                    data.isFemale = false;
                }
                data.isAdopted = entry.isAdopted;
                data.isBiologicalChild = entry.isBiologicalChild;
                data.trainingLocation = StringUtils::TrimStd(entry.trainingLocation);

                const auto& sec = entry.paternitySecret;
                data.bioMotherID = mgr.GetBioMother(childID);
                data.bioFatherID = mgr.GetBioFather(childID);

                RE::FormID explicitPresumedFatherID = mgr.GetPresumedFather(childID);
                RE::FormID motherPartnerID = mgr.GetMotherPartner(childID);
                RE::FormID presFatherID = explicitPresumedFatherID != 0 ? explicitPresumedFatherID : motherPartnerID;
                if (presFatherID != 0 && data.bioMotherID != 0 && explicitPresumedFatherID == 0 &&
                    !sec.isSecretForPartner && !mgr.IsGuardian(childID, presFatherID)) {
                    auto relSt = mgr.GetRelationshipStatus(data.bioMotherID, presFatherID);
                    if (relSt == RelationshipStatus::ExDating || relSt == RelationshipStatus::ExLover ||
                        relSt == RelationshipStatus::Jilted || relSt == RelationshipStatus::Divorced) {
                        presFatherID = 0;
                    }
                }

                const bool isBioSameAsPresumed = (presFatherID != 0 && data.bioFatherID != 0 && presFatherID == data.bioFatherID);
                data.hasPresumedFather = (presFatherID != 0 && !isBioSameAsPresumed && presFatherID != data.bioMotherID);
                if (data.hasPresumedFather) {
                    data.presumedFatherID = presFatherID;
                }

                data.hasPaternitySecret = data.hasPresumedFather && (sec.isSecret || sec.discovered || explicitPresumedFatherID != 0);
                data.partnerKnowsPaternity = !data.hasPaternitySecret || sec.discovered || sec.discoveredByPartner || !sec.isSecretForPartner;
                data.bioFatherKnowsPaternity = sec.discovered || sec.discoveredByBioFather || !sec.isSecretForBioFather;
                data.isSingleMotherSecretFromBioFather = (!data.hasPresumedFather && data.bioFatherID != 0 &&
                                                          sec.isSecret && sec.isSecretForBioFather && !sec.discoveredByBioFather && !sec.discovered);
                data.hasPhenotypeMismatch = sec.hasPhenotypeMismatch;
                if (data.hasPhenotypeMismatch && data.bioMotherID != 0 && data.presumedFatherID != 0 && data.bioFatherID != 0) {
                    data.visualDiscrepancyDescription = mgr.GetVisualDiscrepancyDescription(
                        data.bioMotherID, data.presumedFatherID, data.bioFatherID);
                }

                if (data.bioMotherID != 0) {
                    data.bioMotherUUID = ResolvePlacedActorUUID(data.bioMotherID);
                    data.motherName = ActorSvc::GetName(data.bioMotherID);
                    data.motherRace = ActorSvc::GetRaceName(data.bioMotherID);
                    data.motherIsGuardian = mgr.IsGuardian(childID, data.bioMotherID);
                }

                if (data.bioFatherID != 0 && data.bioFatherID != kUnknownFatherFormID) {
                    data.bioFatherUUID = ResolvePlacedActorUUID(data.bioFatherID);
                    data.bioFatherName = ActorSvc::GetName(data.bioFatherID);
                    data.bioFatherRace = ActorSvc::GetRaceName(data.bioFatherID);
                    data.bioFatherIsGuardian = mgr.IsGuardian(childID, data.bioFatherID);
                } else if (!sec.bioFatherName.empty() && sec.bioFatherName != "Unknown") {
                    data.bioFatherName = sec.bioFatherName;
                }

                if (data.hasPresumedFather && data.presumedFatherID != 0) {
                    data.presumedFatherUUID = ResolvePlacedActorUUID(data.presumedFatherID);
                    data.presumedFatherName = ActorSvc::GetName(data.presumedFatherID);
                    data.presumedFatherRace = ActorSvc::GetRaceName(data.presumedFatherID);
                    data.presumedFatherIsGuardian = mgr.IsGuardian(childID, data.presumedFatherID);
                    if (data.bioMotherID != 0) {
                        data.motherPresumedRelStatus = std::string(Utils::RelationshipStatusToString(
                            mgr.GetRelationshipStatus(data.bioMotherID, data.presumedFatherID)));
                    }
                }

                data.guardianAID = entry.guardianAID;
                data.guardianBID = entry.guardianBID;
                if (entry.guardianAID != 0) {
                    data.guardianAName = ActorSvc::GetName(entry.guardianAID);
                }
                if (entry.guardianBID != 0 && entry.guardianBID != entry.guardianAID) {
                    data.guardianBName = ActorSvc::GetName(entry.guardianBID);
                }

                // Populate all involved adults at the moment of generating/updating the bio
                std::vector<RE::FormID> seenAdults;
                auto addAdultRef = [&](RE::FormID adultID, const std::string& roleLabel, bool isGuardian) {
                    if (adultID == 0 || adultID == kUnknownFatherFormID || adultID == childID) return;
                    for (RE::FormID seen : seenAdults) {
                        if (seen == adultID) return;
                    }
                    seenAdults.push_back(adultID);
                    InvolvedAdultProfileRef ref;
                    ref.formID = adultID;
                    ref.uuid = ResolvePlacedActorUUID(adultID);
                    ref.bioTemplateName = ResolvePlacedActorBioTemplateName(adultID);
                    ref.name = ActorSvc::GetName(adultID);
                    ref.raceName = ActorSvc::GetRaceName(adultID);
                    ref.roleLabel = roleLabel;
                    ref.isGuardian = isGuardian;
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
                if (data.bioFatherID != 0 && data.bioFatherID != kUnknownFatherFormID) {
                    std::string bioFatherRole;
                    if (data.hasPaternitySecret) {
                        if (!data.partnerKnowsPaternity && !data.bioFatherKnowsPaternity) {
                            bioFatherRole = "True Biological Father (Paternity Secret — Unaware of Paternity; Presumed Father Also Unaware)";
                        } else if (!data.partnerKnowsPaternity && data.bioFatherKnowsPaternity) {
                            bioFatherRole = "True Biological Father (Paternity Secret — Secretly Knows Truth; Presumed Father Unaware)";
                        } else if (data.partnerKnowsPaternity && !data.bioFatherKnowsPaternity) {
                            bioFatherRole = "True Biological Father (Paternity Secret — Unaware of Paternity; Presumed Father Knows)";
                        } else {
                            bioFatherRole = data.bioFatherIsGuardian
                                ? "True Biological Father & Household Guardian (Paternity Known to Both Fathers)"
                                : "True Biological Father (Paternity Known to Both Fathers)";
                        }
                    } else if (data.isSingleMotherSecretFromBioFather) {
                        bioFatherRole = "True Biological Father (Paternity Secret — Unaware of Paternity)";
                    } else {
                        bioFatherRole = data.bioFatherIsGuardian
                            ? "Biological Father & Household Guardian"
                            : "Biological Father (Non-Custodial)";
                    }
                    addAdultRef(data.bioFatherID, bioFatherRole, data.bioFatherIsGuardian);
                }
                if (data.hasPresumedFather && data.presumedFatherID != 0) {
                    std::string presFatherRole;
                    if (!data.partnerKnowsPaternity && !data.bioFatherKnowsPaternity) {
                        presFatherRole = data.presumedFatherIsGuardian
                            ? "Presumed Father & Household Guardian (Believes Child Is Biologically His; Bio Father Also Unaware)"
                            : "Presumed Father (Believes Child Is Biologically His; Bio Father Also Unaware)";
                    } else if (!data.partnerKnowsPaternity && data.bioFatherKnowsPaternity) {
                        presFatherRole = data.presumedFatherIsGuardian
                            ? "Presumed Father & Household Guardian (Believes Child Is Biologically His; Bio Father Secretly Knows)"
                            : "Presumed Father (Believes Child Is Biologically His; Bio Father Secretly Knows)";
                    } else if (data.partnerKnowsPaternity && !data.bioFatherKnowsPaternity) {
                        presFatherRole = data.presumedFatherIsGuardian
                            ? "Presumed Father & Household Guardian (Knows Child Was Fathered by Bio Father; Bio Father Unaware)"
                            : "Former Presumed Father / Mother's Partner (Knows Child Was Fathered by Bio Father; Non-Custodial; Bio Father Unaware)";
                    } else {
                        presFatherRole = data.presumedFatherIsGuardian
                            ? "Presumed Father & Household Guardian / Step-Father Figure (Both Fathers Know True Paternity)"
                            : "Former Presumed Father / Mother's Partner (Both Fathers Know True Paternity; Non-Custodial)";
                    }
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

                // Collect siblings from biological mother, known father, or guardians
                std::unordered_set<RE::FormID> seenSiblings;
                auto collectSiblingsFrom = [&](RE::FormID parentID) {
                    if (parentID == 0 || parentID == kUnknownFatherFormID) return;
                    for (RE::FormID sibID : mgr.GetChildrenOfActor(parentID)) {
                        if (sibID == 0 || sibID == childID || !seenSiblings.insert(sibID).second) {
                            continue;
                        }
                        if (auto sibEntry = mgr.GetChildEntry(sibID)) {
                            if (sibEntry->stage == ChildStage::Unborn) {
                                continue;
                            }
                            std::string sibName = !sibEntry->name.empty()
                                ? StringUtils::TrimStd(sibEntry->name)
                                : ActorSvc::GetName(sibID);
                            if (!sibName.empty()) {
                                data.siblingNames.push_back(std::move(sibName));
                            }
                        }
                    }
                };
                collectSiblingsFrom(data.bioMotherID);
                collectSiblingsFrom(data.guardianAID);
                collectSiblingsFrom(data.guardianBID);
                if (data.hasPresumedFather && !data.partnerKnowsPaternity) {
                    collectSiblingsFrom(data.presumedFatherID);
                } else if (!data.isSingleMotherSecretFromBioFather) {
                    collectSiblingsFrom(data.bioFatherID);
                }
            }

            if (data.childName.empty()) {
                data.childName = data.isFemale ? "Daughter" : "Son";
            }

            data.generationGuidance = BuildProfileGenerationGuidance(data);
            return data;
        }

        void ExecuteAsyncChildProfileFlow(ChildProfileContextData taskData) {
            try {
                auto* requestProfileUpdateFn = FindRequestProfileUpdate();
                if (!requestProfileUpdateFn) {
                    MARAS_LOG_WARN("[SkyrimNetProfile] PublicRequestProfileUpdate not available in SkyrimNet.dll; skipping profile update for {:08X} ('{}', stage={})",
                                   taskData.childID, taskData.childName, static_cast<int>(taskData.stage));
                    return;
                }

                const auto endpoint = ReadSkyrimNetWebServerConfig();

                // 1. Render all involved parties' character bios (ttm_gm) and chronological family recent events
                //    and append them directly to generationGuidance so the LLM has full parent/guardian profiles.
                std::string renderedPartiesContext = RenderInvolvedPartiesContext(taskData, endpoint);
                if (!renderedPartiesContext.empty()) {
                    taskData.generationGuidance += "\n\n" + renderedPartiesContext;
                }

                // 2. Request profile generation/update via SkyrimNet Public API PublicRequestProfileUpdate.
                if (requestProfileUpdateFn(taskData.childID, taskData.generationGuidance.c_str(), kAllProfileBlocksCSV)) {
                    MARAS_LOG_INFO("[SkyrimNetProfile] Queued profile update via PublicRequestProfileUpdate for spawned child actor {:08X} ('{}', stage={}, involvedAdults={})",
                                   taskData.childID, taskData.childName, static_cast<int>(taskData.stage), taskData.involvedAdults.size());
                } else {
                    MARAS_LOG_WARN("[SkyrimNetProfile] PublicRequestProfileUpdate returned false for {:08X} ('{}', stage={})",
                                   taskData.childID, taskData.childName, static_cast<int>(taskData.stage));
                }
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[SkyrimNetProfile] Exception in ExecuteAsyncChildProfileFlow: {}", e.what());
            } catch (...) {
                MARAS_LOG_ERROR("[SkyrimNetProfile] Unknown exception in ExecuteAsyncChildProfileFlow");
            }
        }

    }  // namespace

    void SkyrimNetProfileService::Revert() {
        std::lock_guard<std::mutex> lock(mutex_);
        updatedActorStages_.clear();
        MARAS_LOG_INFO("[SkyrimNetProfile] Reverted session profile update tracking state");
    }

    void SkyrimNetProfileService::MarkActorStageUpdated(RE::FormID childID, ChildStage stage) {
        if (childID == 0 || childID == kPlayerFormID || IsSyntheticChildID(childID)) {
            return;
        }
        if (stage != ChildStage::Child && stage != ChildStage::Adult) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        updatedActorStages_.insert(MakeStageKey(childID, stage));
    }

    bool SkyrimNetProfileService::HasUpdatedActorStage(RE::FormID childID, ChildStage stage) const {
        if (childID == 0) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return updatedActorStages_.contains(MakeStageKey(childID, stage));
    }

    void SkyrimNetProfileService::RequestChildProfileUpdate(RE::FormID childID, ChildStage stage) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }
        if (IsSuppressed() || SkyrimNetMemoryService::IsSuppressed()) {
            return;
        }
        // Strictly placed actors only (never synthetic child records)
        if (childID == 0 || childID == kPlayerFormID || IsSyntheticChildID(childID)) {
            return;
        }
        // Strictly Young Child spawn (Stage 2) or Adult Child spawn (Stage 4) only
        if (stage != ChildStage::Child && stage != ChildStage::Adult) {
            return;
        }
        if (!FindRequestProfileUpdate()) {
            MARAS_LOG_WARN("[SkyrimNetProfile] PublicRequestProfileUpdate not available in SkyrimNet.dll; skipping child profile update for {:08X}",
                           childID);
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(childID);
        if (!actor || actor->IsDeleted()) {
            MARAS_LOG_WARN("[SkyrimNetProfile] Skipping profile update for {:08X}: not a valid placed RE::Actor", childID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const uint64_t stageKey = MakeStageKey(childID, stage);
            if (!updatedActorStages_.insert(stageKey).second) {
                MARAS_LOG_DEBUG("[SkyrimNetProfile] Profile update already requested for actor {:08X} at stage {}; skipping",
                                childID, static_cast<int>(stage));
                return;
            }
        }

        ChildProfileContextData taskData = GatherChildProfileContextOnGameThread(childID, stage, actor);

        MARAS_LOG_INFO("[SkyrimNetProfile] Queuing async profile update for spawned child actor {:08X} ('{}', stage={}, uuid={:X}, bio='{}', involvedAdults={})",
                       childID, taskData.childName, static_cast<int>(stage), taskData.actorUUID, taskData.bioTemplateName, taskData.involvedAdults.size());

        std::thread([data = std::move(taskData)]() mutable {
            ExecuteAsyncChildProfileFlow(std::move(data));
        }).detach();
    }

    bool SkyrimNetProfileService::RequestActorProfileUpdate(
        RE::FormID actorFormID,
        const std::string& additionalInstructions,
        const std::string& updatableBlocksCSV)
    {
        if (!IsSkyrimNetAvailable() || IsSuppressed() || SkyrimNetMemoryService::IsSuppressed()) {
            return false;
        }
        if (actorFormID == 0 || IsSyntheticChildID(actorFormID)) {
            return false;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
        if (!actor || actor->IsDeleted()) {
            return false;
        }

        auto* requestProfileUpdateFn = FindRequestProfileUpdate();
        if (!requestProfileUpdateFn) {
            MARAS_LOG_WARN("[SkyrimNetProfile] PublicRequestProfileUpdate not available in SkyrimNet.dll; cannot update profile for {:08X}",
                           actorFormID);
            return false;
        }

        const char* instrPtr = additionalInstructions.empty() ? nullptr : additionalInstructions.c_str();
        const char* blocksPtr = updatableBlocksCSV.empty() ? nullptr : updatableBlocksCSV.c_str();
        if (requestProfileUpdateFn(actorFormID, instrPtr, blocksPtr)) {
            MARAS_LOG_INFO("[SkyrimNetProfile] Queued profile update via PublicRequestProfileUpdate for {:08X} ('{}')",
                           actorFormID, ActorSvc::GetName(actorFormID));
            return true;
        }

        MARAS_LOG_WARN("[SkyrimNetProfile] PublicRequestProfileUpdate returned false for {:08X} ('{}')",
                       actorFormID, ActorSvc::GetName(actorFormID));
        return false;
    }

}  // namespace MARAS
