#include "maras/bridge_ui/PrismaUIDataBridge.h"

#include "maras/app/AgencyWorkflowCoordinator.h"
#include "maras/app/AppContext.h"
#include "maras/app/HouseholdUseCases.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/bridge_ui/HouseholdBuffsJsonBuilder.h"
#include "maras/bridge_ui/PrismaModalManager.h"
#include "maras/bridge_ui/PrismaUIManager.h"
#include "maras/domain/lineage/TrainingCatalog.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/BedOwnershipReconciler.h"
#include "maras/engine/ChildSpawnService.h"
#include "maras/engine/ChildrenFleeService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/PerspectiveEngineAdapters.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/engine/TrainingCatalogLoader.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/CoSaveStream.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace MARAS::Bridge::Ui {

    namespace {

        constexpr std::size_t kMaxTrainingImageBytes = 4u * 1024u * 1024u;

        [[nodiscard]] std::string DumpJsonSafe(const nlohmann::json& j, int indent = -1) {
            return j.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        [[nodiscard]] std::string GetJsonString(
            const nlohmann::json& j,
            std::string_view key,
            std::string defaultVal = {}) {
            if (!j.is_object()) {
                return defaultVal;
            }
            const auto it = j.find(key);
            if (it == j.end() || it->is_null()) {
                return defaultVal;
            }
            if (it->is_string()) {
                return it->get<std::string>();
            }
            if (it->is_number_integer()) {
                return std::to_string(it->get<std::int64_t>());
            }
            if (it->is_number_unsigned()) {
                return std::to_string(it->get<std::uint64_t>());
            }
            if (it->is_boolean()) {
                return it->get<bool>() ? "true" : "false";
            }
            return defaultVal;
        }

        [[nodiscard]] bool GetJsonBool(
            const nlohmann::json& j,
            std::string_view key,
            bool defaultVal = false) {
            if (!j.is_object()) {
                return defaultVal;
            }
            const auto it = j.find(key);
            if (it == j.end() || it->is_null()) {
                return defaultVal;
            }
            if (it->is_boolean()) {
                return it->get<bool>();
            }
            if (it->is_number()) {
                return it->get<double>() != 0.0;
            }
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                if (s == "true" || s == "1") {
                    return true;
                }
                if (s == "false" || s == "0") {
                    return false;
                }
            }
            return defaultVal;
        }

        [[nodiscard]] std::int32_t GetJsonInt(
            const nlohmann::json& j,
            std::string_view key,
            std::int32_t defaultVal = 0) {
            if (!j.is_object()) {
                return defaultVal;
            }
            const auto it = j.find(key);
            if (it == j.end() || it->is_null()) {
                return defaultVal;
            }
            if (it->is_number_integer() || it->is_number_unsigned()) {
                return static_cast<std::int32_t>(it->get<std::int64_t>());
            }
            if (it->is_number_float()) {
                return static_cast<std::int32_t>(it->get<double>());
            }
            if (it->is_boolean()) {
                return it->get<bool>() ? 1 : 0;
            }
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                std::int32_t parsed = defaultVal;
                const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), parsed);
                if (ec == std::errc{} && ptr == s.data() + s.size()) {
                    return parsed;
                }
            }
            return defaultVal;
        }

        [[nodiscard]] float GetJsonFloat(
            const nlohmann::json& j,
            std::string_view key,
            float defaultVal = 0.0f) {
            if (!j.is_object()) {
                return defaultVal;
            }
            const auto it = j.find(key);
            if (it == j.end() || it->is_null()) {
                return defaultVal;
            }
            if (it->is_number()) {
                return static_cast<float>(it->get<double>());
            }
            if (it->is_boolean()) {
                return it->get<bool>() ? 1.0f : 0.0f;
            }
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                float parsed = defaultVal;
                const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), parsed);
                if (ec == std::errc{} && ptr == s.data() + s.size()) {
                    return parsed;
                }
            }
            return defaultVal;
        }

        [[nodiscard]] double RoundFloat(float val, double factor = 100.0) {
            return std::round(static_cast<double>(val) * factor) / factor;
        }

        [[nodiscard]] RE::FormID ResolveToPlacedRefID(RE::FormID id) {
            if (id == 0 || id == MARAS::Foundation::kPlayerRefRawId) {
                return id;
            }
            if (auto* refr = MARAS::ActorSvc::GetReference(id)) {
                return refr->GetFormID();
            }
            const RE::FormID ref = MARAS::SpatialWorldIndex::GetSingleton().GetRefID(id);
            return ref != 0 ? ref : id;
        }

        [[nodiscard]] MARAS::Projection::PerspectiveView MakeDashboardView() {
            return MARAS::Projection::PerspectiveView::ForDashboard(
                MARAS::State::WorldStore::GetSingleton().Snapshot(),
                MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot(),
                &MARAS::GetEngineWerewolfStateProvider());
        }

        [[nodiscard]] float GetCurrentGameDays() {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetDaysPassed() : 0.0f;
        }

        [[nodiscard]] std::string ChildStageToString(MARAS::Foundation::ChildStage stage) {
            switch (stage) {
                case MARAS::Foundation::ChildStage::Unborn:   return "unborn";
                case MARAS::Foundation::ChildStage::Infant:   return "infant";
                case MARAS::Foundation::ChildStage::Child:    return "child";
                case MARAS::Foundation::ChildStage::Training: return "training";
                case MARAS::Foundation::ChildStage::Adult:    return "adult";
                default:                                      return "child";
            }
        }

        [[nodiscard]] std::string ResolveActorCurrentPackageName(RE::FormID formID) {
            if (!MARAS::Foundation::HasPhysicalActorRawId(formID)) {
                return "None";
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
                if (auto* currentPkg = actor->GetCurrentPackage()) {
                    const char* edid = currentPkg->GetFormEditorID();
                    const char* name = currentPkg->GetName();
                    if (edid && edid[0] != '\0') {
                        return std::format("{}(0x{:08X})", edid, currentPkg->GetFormID());
                    }
                    if (name && name[0] != '\0') {
                        return std::format("{}(0x{:08X})", name, currentPkg->GetFormID());
                    }
                    return std::format("0x{:08X}", currentPkg->GetFormID());
                }
            }
            return "None";
        }

        [[nodiscard]] std::string LookupLocationOrCellName(RE::FormID id) {
            if (id == 0) {
                return {};
            }
            if (auto* loc = RE::TESForm::LookupByID<RE::BGSLocation>(id)) {
                return loc->GetName() ? std::string(loc->GetName()) : std::string{};
            }
            if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(id)) {
                return cell->GetName() ? std::string(cell->GetName()) : std::string{};
            }
            return {};
        }

        [[nodiscard]] std::string Base64EncodeBytes(const std::vector<std::uint8_t>& data) {
            static constexpr char kBase64Chars[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string encoded;
            encoded.reserve(((data.size() + 2u) / 3u) * 4u);
            std::size_t i = 0;
            while (i + 2u < data.size()) {
                const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) |
                                             (static_cast<std::uint32_t>(data[i + 1u]) << 8) |
                                             static_cast<std::uint32_t>(data[i + 2u]);
                encoded.push_back(kBase64Chars[(triple >> 18) & 0x3Fu]);
                encoded.push_back(kBase64Chars[(triple >> 12) & 0x3Fu]);
                encoded.push_back(kBase64Chars[(triple >> 6) & 0x3Fu]);
                encoded.push_back(kBase64Chars[triple & 0x3Fu]);
                i += 3u;
            }
            if (i < data.size()) {
                const std::uint32_t b0 = data[i];
                const std::uint32_t b1 = (i + 1u < data.size()) ? data[i + 1u] : 0u;
                const std::uint32_t triple = (b0 << 16) | (b1 << 8);
                encoded.push_back(kBase64Chars[(triple >> 18) & 0x3Fu]);
                encoded.push_back(kBase64Chars[(triple >> 12) & 0x3Fu]);
                encoded.push_back((i + 1u < data.size()) ? kBase64Chars[(triple >> 6) & 0x3Fu] : '=');
                encoded.push_back('=');
            }
            return encoded;
        }

        [[nodiscard]] std::vector<MARAS::App::GeneratedMemoryEntry> ParseDualMemoriesArray(
            const nlohmann::json& arr) {
            std::vector<MARAS::App::GeneratedMemoryEntry> entries;
            if (!arr.is_array()) {
                return entries;
            }
            entries.reserve(arr.size());
            for (const auto& item : arr) {
                if (!item.is_object()) {
                    continue;
                }
                MARAS::App::GeneratedMemoryEntry entry{};
                entry.actorName = GetJsonString(item, "actor");
                const std::string formHex = GetJsonString(item, "actorFormId");
                if (!formHex.empty()) {
                    RE::FormID parsed = 0;
                    std::string_view sv(formHex);
                    if (sv.starts_with("0x") || sv.starts_with("0X")) {
                        sv.remove_prefix(2);
                    }
                    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed, 16);
                    if (ec == std::errc{}) {
                        entry.actorFormID = ResolveToPlacedRefID(parsed);
                    }
                }
                entry.content = GetJsonString(item, "content");
                entry.location = GetJsonString(item, "location", "Skyrim");
                entry.emotion = GetJsonString(item, "emotion", "content");
                entry.importanceScore = GetJsonFloat(item, "importance_score", 0.7f);
                entry.type = GetJsonString(item, "type", "RELATIONSHIP");
                if (item.contains("tags") && item["tags"].is_array()) {
                    for (const auto& tag : item["tags"]) {
                        if (tag.is_string()) {
                            entry.tags.push_back(tag.get<std::string>());
                        }
                    }
                }
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        [[nodiscard]] bool IsValidActorRefId(RE::FormID id) {
            if (id == MARAS::Foundation::kPlayerRefRawId) {
                return true;
            }
            auto* form = RE::TESForm::LookupByID(id);
            if (!form) {
                return false;
            }
            if (form->GetFormType() == RE::FormType::ActorCharacter) {
                return true;
            }
            if (auto* refr = form->As<RE::TESObjectREFR>()) {
                if (auto* baseObj = refr->GetBaseObject()) {
                    return baseObj->GetFormType() == RE::FormType::NPC;
                }
            }
            return false;
        }

        [[nodiscard]] std::string NormalizeSettingKeyForCoordinator(std::string_view key) {
            const std::string lower = MARAS::Foundation::StringUtils::ToLowerStd(key);
            if (lower == "alwayssuccess") {
                return "alwayssuccessmarriage";
            }
            if (lower == "affectiondecaymult") {
                return "affectiondecaymultiplier";
            }
            if (lower == "actionradius") {
                return "skyrimnetactionradius";
            }
            return std::string(key);
        }

        [[nodiscard]] nlohmann::json BuildMatchmakingDumpJson(const MARAS::Projection::PerspectiveView& view) {
            const auto& cooldownData = view.Cooldowns();
            const auto& agency = MARAS::App::AgencyWorkflowCoordinator::GetSingleton();
            const float currentDay = GetCurrentGameDays();

            nlohmann::json j = nlohmann::json::object();
            j["lastMatchmakingDay"] = cooldownData.lastMatchmakingDay;
            j["lastPlayerPartnerCheatingDay"] =
                view.IsOmniscient() ? cooldownData.lastPlayerPartnerCheatingDay : -1.0f;

            nlohmann::json cooldowns = nlohmann::json::array();
            for (const auto& [actorKey, lastDay] : cooldownData.npcLastMatchmakingChangeDay) {
                const RE::FormID actorID = actorKey.raw();
                nlohmann::json c = nlohmann::json::object();
                c["formId"] = std::format("{:08X}", actorID);
                c["name"] = MARAS::ActorSvc::GetName(actorID);
                c["lastMatchmakingChangeDay"] = lastDay;
                c["cooldownRemainingDays"] = agency.GetNpcCooldownRemaining(actorID, currentDay);
                cooldowns.push_back(std::move(c));
            }
            j["npcCooldowns"] = std::move(cooldowns);
            return j;
        }

        [[nodiscard]] nlohmann::json BuildBanterDumpJson(const MARAS::Projection::PerspectiveView& view) {
            const auto& cd = view.Cooldowns();
            const float effectiveCooldownHours =
                MARAS::App::AgencyWorkflowCoordinator::GetSingleton().GetBanterCooldownHours();

            nlohmann::json j = nlohmann::json::object();
            j["cooldownHours"] = effectiveCooldownHours;
            j["savedCooldownHours"] = cd.savedBanterCooldownHours;
            j["lastSceneDay"] = cd.lastBanterSceneDay;

            nlohmann::json cooldownsArray = nlohmann::json::array();
            for (const auto& [key, lastDay] : cd.banterCoupleCooldowns) {
                if (!view.IsOmniscient()) {
                    const auto perceived = view.GetPerceivedPair(key.first, key.second);
                    if (!perceived.has_value() || perceived->isSecretAffair) {
                        continue;
                    }
                }
                const RE::FormID idA = key.first.raw();
                const RE::FormID idB = key.second.raw();
                nlohmann::json entry = nlohmann::json::object();
                entry["actorAFormId"] = std::format("{:08X}", idA);
                entry["actorAName"] = MARAS::ActorSvc::GetName(idA);
                entry["actorBFormId"] = std::format("{:08X}", idB);
                entry["actorBName"] = MARAS::ActorSvc::GetName(idB);
                entry["lastSceneDay"] = lastDay;
                cooldownsArray.push_back(std::move(entry));
            }
            j["coupleCooldowns"] = std::move(cooldownsArray);
            return j;
        }

        template <typename Fn>
        void DispatchBridgeMainThread(const char* tag, Fn&& fn) {
            if (!MARAS::DeferredActorResolver::GetSingleton().IsOnMainThread()) {
                MARAS::AddCurrentEpochTask(tag, [fn = std::forward<Fn>(fn)]() mutable {
                    std::lock_guard lock(MARAS::GetPapyrusMutex());
                    fn();
                });
                return;
            }
            fn();
        }

    }  // namespace

    PrismaUIDataBridge& PrismaUIDataBridge::GetSingleton() {
        static PrismaUIDataBridge instance;
        return instance;
    }

    std::string PrismaUIDataBridge::FormIDToString(RE::FormID formID) {
        return std::format("{:08X}", formID);
    }

    RE::FormID PrismaUIDataBridge::StringToFormID(const std::string& str) {
        if (str.empty()) {
            return 0;
        }
        std::string_view sv(str);
        if (sv.starts_with("0x") || sv.starts_with("0X")) {
            sv.remove_prefix(2);
        }
        if (sv.empty()) {
            return 0;
        }
        RE::FormID parsed = 0;
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed, 16);
        return (ec == std::errc{} && ptr == sv.data() + sv.size()) ? parsed : 0;
    }

    void PrismaUIDataBridge::Initialize() {
        RegisterListeners();
    }

    void PrismaUIDataBridge::RegisterListeners() {
        if (!PrismaUIManager::GetSingleton().RegisterJSListener("marasAction", &PrismaUIDataBridge::OnActionFromUI)) {
            MARAS_LOG_DEBUG("[PrismaUIDataBridge] RegisterListeners deferred (Prisma view not ready)");
        }
    }

    void PrismaUIDataBridge::Revert() {
        lastRequestedDetailNpcID_.store(0, std::memory_order_relaxed);
    }

    void PrismaUIDataBridge::OnActionFromUI(const char* argument) {
        GetSingleton().HandleActionJson(argument);
    }

    void PrismaUIDataBridge::HandleActionJson(const char* actionJsonStr) {
        if (!actionJsonStr || !*actionJsonStr) {
            return;
        }

        MARAS_LOG_DEBUG("[PrismaUIDataBridge] Action payload received: {}", actionJsonStr);

        auto json = nlohmann::json::parse(actionJsonStr, nullptr, /*exceptions=*/false);
        if (!json.is_object()) {
            MARAS_LOG_ERROR("[PrismaUIDataBridge] Invalid JSON object in action payload");
            return;
        }

        std::string action = GetJsonString(json, "action");
        nlohmann::json payload = (json.contains("payload") && json["payload"].is_object())
            ? json["payload"]
            : nlohmann::json::object();

        if (action == "uiReady") {
            auto* taskIF = SKSE::GetTaskInterface();
            if (!taskIF) {
                MARAS_LOG_ERROR("[PrismaUIDataBridge] SKSE TaskInterface unavailable for uiReady");
                return;
            }
            taskIF->AddTask([this]() {
                MARAS::ScopedMainThreadTask mainScope;
                std::lock_guard papyrusLock(MARAS::GetPapyrusMutex());
                PrismaUIManager::GetSingleton().OnUIReady();
            });
            return;
        }

        MARAS::AddCurrentEpochTask("PrismaUIDataBridge::HandleAction",
                                   [this, action = std::move(action), payload = std::move(payload)]() {
                                       try {
                                           std::lock_guard papyrusLock(MARAS::GetPapyrusMutex());
                                           HandleAction(action, payload);
                                       } catch (const std::exception& e) {
                                           MARAS_LOG_ERROR("[PrismaUIDataBridge] Exception for action '{}': {}",
                                                           action, e.what());
                                       } catch (...) {
                                           MARAS_LOG_ERROR("[PrismaUIDataBridge] Unknown exception for action '{}'",
                                                           action);
                                       }
                                   });
    }

    void PrismaUIDataBridge::HandleAction(const std::string& type, const nlohmann::json& payload) {
        if (type == "uiReady") {
            PrismaUIManager::GetSingleton().OnUIReady();
        } else if (type == "modalResponse") {
            (void)PrismaModalManager::GetSingleton().HandleModalResponse(payload);
        } else if (type == "fetchOverview") {
            SendOverviewData();
        } else if (type == "fetchHouseholdBuffs") {
            SendHouseholdBuffs();
        } else if (type == "fetchNPCList") {
            SendNPCList();
        } else if (type == "fetchAllSkyrimNpcs") {
            SendAllSkyrimNPCs();
        } else if (type == "fetchNPCDetail") {
            SendNPCDetail(StringToFormID(GetJsonString(payload, "formId")));
        } else if (type == "fetchMatchmakingData") {
            SendMatchmakingData();
        } else if (type == "closeUi") {
            PrismaUIManager::GetSingleton().Hide();
        } else if (type == "setGamePaused") {
            PrismaUIManager::GetSingleton().SetDashboardPaused(GetJsonBool(payload, "paused", false));
        } else if (type == "setAffection") {
            ProcessSetAffection(payload);
        } else if (type == "setSocialClass") {
            ProcessSetSocialClass(payload);
        } else if (type == "setSkillType") {
            ProcessSetSkillType(payload);
        } else if (type == "setTemperament") {
            ProcessSetTemperament(payload);
        } else if (type == "setPairIsOpen") {
            ProcessSetPairIsOpen(payload);
        } else if (type == "setHierarchyRank") {
            ProcessSetHierarchyRank(payload);
        } else if (type == "assignRelationship") {
            ProcessAssignRelationship(payload);
        } else if (type == "removeRelationship") {
            ProcessRemoveRelationship(payload);
        } else if (type == "updateSetting") {
            ProcessUpdateSetting(payload);
        } else if (type == "searchNPCs") {
            ProcessSearchNPCs(payload);
        } else if (type == "teleportPlayerToNpc") {
            ProcessTeleportPlayerToNpc(payload);
        } else if (type == "teleportNpcToPlayer") {
            ProcessTeleportNpcToPlayer(payload);
        } else if (type == "fleeChildrenFromCell") {
            MARAS::ChildrenFleeService::GetSingleton().FleeFromPlayerCell();
        } else if (type == "setChildCritterBanned") {
            ProcessSetChildCritterBanned(payload);
        } else if (type == "setChildPetBanned") {
            ProcessSetChildPetBanned(payload);
        } else if (type == "dismissChildCritter") {
            ProcessDismissChildCritter(payload);
        } else if (type == "dismissChildPet") {
            ProcessDismissChildPet(payload);
        } else if (type == "addChild") {
            ProcessAddChild(payload);
        } else if (type == "removeChild") {
            ProcessRemoveChild(payload);
        } else if (type == "updateChild") {
            ProcessUpdateChild(payload);
        } else if (type == "spawnChild") {
            ProcessSpawnChild(payload);
        } else if (type == "sendChildToTraining") {
            ProcessSendChildToTraining(payload);
        } else if (type == "completeChildTraining") {
            ProcessCompleteChildTraining(payload);
        } else if (type == "relocateFamilyUnit") {
            ProcessRelocateFamilyUnit(payload);
        } else if (type == "setPaternitySecretEdges") {
            ProcessSetPaternitySecretEdges(payload);
        } else if (type == "evaluateChildSecret") {
            ProcessEvaluateChildSecret(payload);
        } else if (type == "setNPCCurrentHome") {
            ProcessSetNPCCurrentHome(payload);
        } else if (type == "setNPCStayAtHome") {
            ProcessSetNPCStayAtHome(payload);
        } else if (type == "dumpData") {
            ProcessDumpData(payload);
        } else if (type == "dumpNPCIndex") {
            ProcessDumpNPCIndex(payload);
        } else if (type == "skyrimNetMatchSuggest") {
            ProcessSkyrimNetMatchSuggest(payload);
        } else if (type == "skyrimNetGlobalMatchSuggest") {
            ProcessSkyrimNetGlobalMatchSuggest(payload);
        } else if (type == "generateMatchMemories") {
            ProcessGenerateMatchMemories(payload);
        } else if (type == "updateMatchmakingSettings") {
            ProcessUpdateMatchmakingSettings(payload);
        } else {
            MARAS_LOG_WARN("[PrismaUIDataBridge] Unknown action '{}'", type);
        }
    }

    // =========================================================================
    // Outbound UI Pushes
    // =========================================================================

    void PrismaUIDataBridge::SendOverviewData() {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendOverviewData", [this]() {
            SendOverviewDataOnMainThread();
        });
    }

    void PrismaUIDataBridge::SendOverviewDataOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        PrismaUIManager::GetSingleton().SendToUI("receiveMARASOverview", DumpJsonSafe(BuildOverviewJson()));
    }

    void PrismaUIDataBridge::SendSettingsData() {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendSettingsData", [this]() {
            SendSettingsDataOnMainThread();
        });
    }

    void PrismaUIDataBridge::SendSettingsDataOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        nlohmann::json data;
        data["settings"] = BuildSettingsJson();
        data["isGamePaused"] = PrismaUIManager::GetSingleton().IsDashboardPaused();
        PrismaUIManager::GetSingleton().SendToUI("receiveMARASOverview", DumpJsonSafe(data));
    }

    void PrismaUIDataBridge::SendHouseholdBuffs() {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendHouseholdBuffs", [this]() {
            SendHouseholdBuffsOnMainThread();
        });
    }

    void PrismaUIDataBridge::SendHouseholdBuffsOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        PrismaUIManager::GetSingleton().SendToUI("receiveMARASBuffs", DumpJsonSafe(BuildHouseholdBuffsJson()));
    }

    void PrismaUIDataBridge::SendNPCList() {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendNPCList", [this]() {
            SendNPCListOnMainThread();
        });
    }

    void PrismaUIDataBridge::SendNPCListOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        const auto view = MakeDashboardView();
        const auto visibleChildren = view.GetVisibleChildren();
        const nlohmann::json list = BuildVisibleProfilesArray(view, visibleChildren);
        PrismaUIManager::GetSingleton().SendToUI("receiveNPCList", DumpJsonSafe(list));
    }

    void PrismaUIDataBridge::SendNPCDetail(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return;
        }
        lastRequestedDetailNpcID_.store(npcFormID, std::memory_order_relaxed);
        DispatchBridgeMainThread("PrismaUIDataBridge::SendNPCDetail", [this, npcFormID]() {
            SendNPCDetailOnMainThread(npcFormID);
        });
    }

    void PrismaUIDataBridge::SendNPCDetailOnMainThread(RE::FormID npcFormID) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        const auto view = MakeDashboardView();
        PrismaUIManager::GetSingleton().SendToUI(
            "receiveNPCDetail",
            DumpJsonSafe(BuildNPCDetailJson(npcFormID, view)));
    }

    void PrismaUIDataBridge::SendMatchmakingData() {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendMatchmakingData", [this]() {
            SendMatchmakingDataOnMainThread();
        });
    }

    void PrismaUIDataBridge::SendMatchmakingDataOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        SendNPCListOnMainThread();
        SendMatchmakingSettingsOnMainThread();
    }

    void PrismaUIDataBridge::SendAllSkyrimNPCs() {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendAllSkyrimNPCs", [this]() {
            SendAllSkyrimNPCsOnMainThread();
        });
    }

    void PrismaUIDataBridge::SendAllSkyrimNPCsOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        PrismaUIManager::GetSingleton().SendToUI(
            "receiveAllSkyrimNPCs",
            DumpJsonSafe(BuildAllSkyrimNPCsJson()));
    }

    void PrismaUIDataBridge::SendMatchmakingResults(
        const std::vector<MARAS::App::MatchmakingResult>& results,
        bool isGlobal) {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendMatchmakingResults", [this, results, isGlobal]() {
            SendMatchmakingResultsOnMainThread(results, isGlobal);
        });
    }

    void PrismaUIDataBridge::SendMatchmakingResultsOnMainThread(
        const std::vector<MARAS::App::MatchmakingResult>& results,
        bool isGlobal) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        const bool revealSecrets = MARAS::App::SettingsCoordinator::GetSingleton().Snapshot()->revealSecretsInUI;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : results) {
            const bool maskCheat = (!revealSecrets && r.isCheating);
            nlohmann::json item;
            item["actorAFormId"] = FormIDToString(r.actorAFormID);
            item["actorAName"] = r.actorAName;
            item["actorBFormId"] = FormIDToString(r.actorBFormID);
            item["actorBName"] = r.actorBName;
            item["formId"] = FormIDToString(r.actorBFormID);
            item["name"] = r.actorBName;
            item["action"] = r.action;
            item["targetStatus"] = r.targetStatus;
            item["suggestedStatus"] = static_cast<std::uint8_t>(
                MARAS::Foundation::StringToRelationshipStatus(r.targetStatus));
            if (maskCheat) {
                const std::string neutralText = std::format("{} and {} grew closer.", r.actorAName, r.actorBName);
                item["memory"] = neutralText;
                item["reasoning"] = neutralText;
                item["isCheating"] = false;
            } else {
                item["memory"] = r.memory;
                item["reasoning"] = r.memory;
                item["isCheating"] = r.isCheating;
            }
            item["selected"] = true;
            item["compatibilityScore"] = 85;
            arr.push_back(std::move(item));
        }

        nlohmann::json payload;
        payload["results"] = std::move(arr);
        payload["isGlobal"] = isGlobal;
        PrismaUIManager::GetSingleton().SendToUI("receiveMatchmakingResults", DumpJsonSafe(payload));
    }

    void PrismaUIDataBridge::SendGeneratedMemories(
        RE::FormID partnerFormId,
        const nlohmann::json& memoriesArray) {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendGeneratedMemories", [this, partnerFormId, memoriesArray]() {
            SendGeneratedMemoriesOnMainThread(partnerFormId, memoriesArray);
        });
    }

    void PrismaUIDataBridge::SendGeneratedMemoriesOnMainThread(
        RE::FormID partnerFormId,
        const nlohmann::json& memoriesArray) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        nlohmann::json data;
        data["partnerFormId"] = FormIDToString(partnerFormId);
        data["memories"] = memoriesArray;
        PrismaUIManager::GetSingleton().SendToUI("receiveGeneratedMemories", DumpJsonSafe(data));
    }

    void PrismaUIDataBridge::SendMatchmakingSettings() {
        DispatchBridgeMainThread("PrismaUIDataBridge::SendMatchmakingSettings", [this]() {
            SendMatchmakingSettingsOnMainThread();
        });
    }

    void PrismaUIDataBridge::SendMatchmakingSettingsOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        PrismaUIManager::GetSingleton().SendToUI(
            "receiveMatchmakingSettings",
            DumpJsonSafe(BuildMatchmakingSettingsJson()));
    }

    // =========================================================================
    // Real-Time Event Notifications
    // =========================================================================

    void PrismaUIDataBridge::OnAffectionChanged(RE::FormID npcFormID, int newValue) {
        DispatchBridgeMainThread("PrismaUIDataBridge::OnAffectionChanged", [this, npcFormID, newValue]() {
            OnAffectionChangedOnMainThread(npcFormID, newValue);
        });
    }

    void PrismaUIDataBridge::OnAffectionChangedOnMainThread(RE::FormID npcFormID, int newValue) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        nlohmann::json data;
        data["formId"] = FormIDToString(npcFormID);
        data["value"] = newValue;
        PrismaUIManager::GetSingleton().SendToUI("onAffectionChanged", DumpJsonSafe(data));
    }

    void PrismaUIDataBridge::OnStatusChanged(
        RE::FormID actorA,
        RE::FormID actorB,
        MARAS::Foundation::RelationshipStatus newStatus) {
        DispatchBridgeMainThread("PrismaUIDataBridge::OnStatusChanged", [this, actorA, actorB, newStatus]() {
            OnStatusChangedOnMainThread(actorA, actorB, newStatus);
        });
    }

    void PrismaUIDataBridge::OnStatusChangedOnMainThread(
        RE::FormID actorA,
        RE::FormID actorB,
        MARAS::Foundation::RelationshipStatus newStatus) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
        const auto view = MARAS::Projection::PerspectiveView::ForDashboard(
            worldSnap,
            MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot(),
            &MARAS::GetEngineWerewolfStateProvider());
        const auto* rawEdge = worldSnap->relationships->FindEdge(
            MARAS::State::ActorId{ actorA }, MARAS::State::ActorId{ actorB });
        const auto edge = view.GetPerceivedPair(
            MARAS::State::ActorId{ actorA }, MARAS::State::ActorId{ actorB });
        if (rawEdge != nullptr && !edge.has_value()) {
            return;
        }

        const auto visibleStatus = static_cast<std::uint8_t>(edge.has_value() ? edge->status : newStatus);
        const auto originalStatus = static_cast<std::uint8_t>(
            edge.has_value() ? edge->originalStatus : MARAS::Foundation::RelationshipStatus::Unknown);

        nlohmann::json data;
        data["actorA"] = FormIDToString(actorA);
        data["actorB"] = FormIDToString(actorB);
        data["status"] = visibleStatus;
        data["originalStatus"] = originalStatus;
        if (edge.has_value() || !view.Settings().revealSecretsInUI) {
            data["isOpen"] = edge.has_value() ? edge->isOpen : false;
        }
        if (actorA != 0) {
            data["actorAProfile"] = BuildNPCProfileJson(actorA, view);
        }
        if (actorB != 0) {
            data["actorBProfile"] = BuildNPCProfileJson(actorB, view);
        }
        PrismaUIManager::GetSingleton().SendToUI("onStatusChanged", DumpJsonSafe(data));
    }

    void PrismaUIDataBridge::OnHierarchyChanged() {
        DispatchBridgeMainThread("PrismaUIDataBridge::OnHierarchyChanged", [this]() {
            OnHierarchyChangedOnMainThread();
        });
    }

    void PrismaUIDataBridge::OnHierarchyChangedOnMainThread() {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
        nlohmann::json ranks = nlohmann::json::array();
        for (std::size_t i = 0; i < MARAS::State::SpouseHierarchyTable::kSlotCount; ++i) {
            ranks.push_back(FormIDToString(worldSnap->hierarchy->GetSpouseAtSlot(i).raw()));
        }
        nlohmann::json payload;
        payload["ranks"] = std::move(ranks);
        PrismaUIManager::GetSingleton().SendToUI("onHierarchyChanged", DumpJsonSafe(payload));
    }

    void PrismaUIDataBridge::OnChildUpdated(const MARAS::State::ChildRecord& entry) {
        DispatchBridgeMainThread("PrismaUIDataBridge::OnChildUpdated", [this, entry]() {
            OnChildUpdatedOnMainThread(entry);
        });
    }

    void PrismaUIDataBridge::OnChildUpdatedOnMainThread(const MARAS::State::ChildRecord& entry) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        const auto view = MakeDashboardView();
        const auto perceivedChild = view.GetPerceivedChild(entry.childId);
        if (!perceivedChild.has_value()) {
            return;
        }
        PrismaUIManager::GetSingleton().SendToUI(
            "onChildUpdated",
            DumpJsonSafe(BuildChildJson(*perceivedChild, view)));
    }

    void PrismaUIDataBridge::OnChildRemoved(RE::FormID childID) {
        DispatchBridgeMainThread("PrismaUIDataBridge::OnChildRemoved", [this, childID]() {
            OnChildRemovedOnMainThread(childID);
        });
    }

    void PrismaUIDataBridge::OnChildRemovedOnMainThread(RE::FormID childID) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        nlohmann::json payload;
        payload["childId"] = FormIDToString(childID);
        PrismaUIManager::GetSingleton().SendToUI("onChildRemoved", DumpJsonSafe(payload));
    }

    void PrismaUIDataBridge::OnChildPromoted(RE::FormID oldID, RE::FormID newID) {
        DispatchBridgeMainThread("PrismaUIDataBridge::OnChildPromoted", [this, oldID, newID]() {
            OnChildPromotedOnMainThread(oldID, newID);
        });
    }

    void PrismaUIDataBridge::OnChildPromotedOnMainThread(RE::FormID oldID, RE::FormID newID) {
        if (!PrismaUIManager::GetSingleton().IsDomReady()) {
            return;
        }
        nlohmann::json payload;
        payload["oldId"] = FormIDToString(oldID);
        payload["newId"] = FormIDToString(newID);
        PrismaUIManager::GetSingleton().SendToUI("onChildPromoted", DumpJsonSafe(payload));
    }

    // =========================================================================
    // Read-Only JSON Builders (Strict CQS)
    // =========================================================================

    nlohmann::json PrismaUIDataBridge::BuildOverviewJson() const {
        const auto view = MakeDashboardView();
        const auto& relUses = MARAS::App::RelationshipUseCases::GetSingleton();

        nlohmann::json data;
        auto* calendar = RE::Calendar::GetSingleton();
        data["currentGameDay"] = calendar ? calendar->GetCurrentGameTime() : 0.0f;

        std::size_t marriedCount = 0;
        std::size_t datingCount = 0;
        std::size_t loverCount = 0;
        std::size_t engagedCount = 0;
        std::size_t divorcedCount = 0;
        std::size_t jiltedCount = 0;
        for (const auto& edge : view.GetPerceivedRelationshipsOf(MARAS::State::ActorId::Player())) {
            switch (edge.status) {
                case MARAS::Foundation::RelationshipStatus::Married:  ++marriedCount; break;
                case MARAS::Foundation::RelationshipStatus::Dating:   ++datingCount; break;
                case MARAS::Foundation::RelationshipStatus::Lover:    ++loverCount; break;
                case MARAS::Foundation::RelationshipStatus::Engaged:  ++engagedCount; break;
                case MARAS::Foundation::RelationshipStatus::Divorced: ++divorcedCount; break;
                case MARAS::Foundation::RelationshipStatus::Jilted:   ++jiltedCount; break;
                default: break;
            }
        }
        data["statCounts"]["married"] = marriedCount;
        data["statCounts"]["dating"] = datingCount;
        data["statCounts"]["lover"] = loverCount;
        data["statCounts"]["engaged"] = engagedCount;
        data["statCounts"]["divorced"] = divorcedCount;
        data["statCounts"]["jilted"] = jiltedCount;

        std::vector<std::string> rankFormIDs;
        nlohmann::json rankSpouses = nlohmann::json::array();
        for (std::size_t rank = 0; rank < MARAS::State::SpouseHierarchyTable::kSlotCount; ++rank) {
            RE::FormID id = 0;
            const MARAS::State::ActorId slotActor = view.Hierarchy().GetSpouseAtSlot(rank);
            if (!slotActor.IsNull()) {
                if (const auto edge = view.GetPerceivedPair(MARAS::State::ActorId::Player(), slotActor);
                    edge.has_value() && edge->status == MARAS::Foundation::RelationshipStatus::Married) {
                    id = slotActor.raw();
                }
            }
            rankFormIDs.push_back(id != 0 ? FormIDToString(id) : "");

            if (id != 0) {
                nlohmann::json spouseObj = BuildNPCProfileJson(id, view);
                spouseObj["affection"] = view.Affection().GetPermanent(MARAS::State::ActorId{ id });
                rankSpouses.push_back(std::move(spouseObj));
            }
        }
        data["hierarchyRanks"] = std::move(rankFormIDs);
        data["hierarchySpouses"] = std::move(rankSpouses);

        const auto visibleChildren = view.GetVisibleChildren();
        data["npcProfiles"] = BuildVisibleProfilesArray(view, visibleChildren);

        nlohmann::json childrenArray = nlohmann::json::array();
        for (const auto& child : visibleChildren) {
            childrenArray.push_back(BuildChildJson(child, view));
        }
        data["children"] = std::move(childrenArray);

        nlohmann::json relsArray = nlohmann::json::array();
        for (const auto& edge : view.GetVisibleRelationshipEdges()) {
            nlohmann::json relObj;
            relObj["actorA"] = FormIDToString(edge.pair.first.raw());
            relObj["actorB"] = FormIDToString(edge.pair.second.raw());
            relObj["status"] = static_cast<std::uint8_t>(edge.status);
            relObj["isOpen"] = edge.isOpen;

            nlohmann::json history = nlohmann::json::array();
            for (const auto& h : edge.statusHistory) {
                nlohmann::json hObj;
                hObj["fromStatus"] = static_cast<std::uint8_t>(h.fromStatus);
                hObj["toStatus"] = static_cast<std::uint8_t>(h.toStatus);
                hObj["timestamp"] = (std::isnan(h.timestamp) || h.timestamp < 0.0f) ? -1.0f : h.timestamp;
                hObj["initiatorFormId"] = FormIDToString(h.initiator.raw());
                history.push_back(std::move(hObj));
            }
            relObj["statusHistory"] = std::move(history);
            relsArray.push_back(std::move(relObj));
        }
        data["relationships"] = std::move(relsArray);

        nlohmann::json cheatingArray = nlohmann::json::array();
        for (const auto& affair : view.GetAllVisibleAffairs()) {
            nlohmann::json cObj;
            cObj["cheaterId"] = FormIDToString(affair.cheater.raw());
            cObj["victimId"] = FormIDToString(affair.deceivedPartner.raw());
            cObj["affairPartnerId"] = FormIDToString(affair.paramour.raw());
            cObj["count"] = affair.incidentCount;
            cObj["lastTimestamp"] = affair.lastTimestamp;
            cObj["isSecret"] = affair.isSecret;
            cObj["affairType"] = static_cast<std::uint8_t>(affair.affairType);
            cObj["affairStatus"] = static_cast<std::uint8_t>(affair.affairStatus);
            cObj["affairStage"] = static_cast<std::uint8_t>(
                (affair.deceivedPartner.raw() == MARAS::Foundation::kPlayerRefRawId)
                    ? affair.effectiveAffairStage
                    : MARAS::Foundation::AffairStage::Physical);
            cheatingArray.push_back(std::move(cObj));
        }
        data["cheatingRecords"] = std::move(cheatingArray);

        const float decayMult = RoundFloat(relUses.GetAffectionDecayMultiplier());
        nlohmann::json affArray = nlohmann::json::array();
        for (const auto& [refId, actorProfile] : view.Actors().AllProfiles()) {
            (void)actorProfile;
            if (refId.IsNull()) {
                continue;
            }
            nlohmann::json affObj;
            affObj["formId"] = FormIDToString(refId.raw());
            affObj["permanent"] = view.Affection().GetPermanent(MARAS::State::ActorId{ refId.raw() });
            affObj["decayMultiplier"] = decayMult;
            affObj["daysSinceLastInteraction"] = std::round(relUses.GetDaysSinceLastAffection(refId.raw()));
            affArray.push_back(std::move(affObj));
        }
        data["affectionData"] = std::move(affArray);

        data["settings"] = BuildSettingsJson();
        data["isGamePaused"] = PrismaUIManager::GetSingleton().IsDashboardPaused();

        const auto& household = MARAS::App::HouseholdUseCases::GetSingleton();
        nlohmann::json homesArray = nlohmann::json::array();
        for (const RE::FormID houseID : household.GetAllPlayerHouses()) {
            const std::string houseName = MARAS::App::HouseholdUseCases::GetHouseName(houseID);
            if (!houseName.empty()) {
                nlohmann::json hObj;
                hObj["formId"] = FormIDToString(houseID);
                hObj["name"] = houseName;
                hObj["locationName"] = "";
                homesArray.push_back(std::move(hObj));
            }
        }
        data["playerHomes"] = std::move(homesArray);
        return data;
    }

    nlohmann::json PrismaUIDataBridge::BuildSettingsJson() const {
        const auto& settingsMgr = MARAS::App::SettingsCoordinator::GetSingleton();
        const bool enablePolygamy = settingsMgr.GetEnablePolygamy();
        const bool hasSkyrimNet = MARAS::App::GetSkyrimNetGateway().IsAvailable();
        const std::int32_t startDialGender = settingsMgr.GetStartDialGender();
        const std::int32_t minRelRankForDial = settingsMgr.GetMinRelRankForDial();

        nlohmann::json j;
        std::int32_t snappedTrainingCost = 0;
        {
            const auto settingsSnap = settingsMgr.Snapshot();
            const auto& settings = *settingsSnap;
            j["skipWedding"] = settings.skipWedding;
            j["affectionDecayMult"] = RoundFloat(settings.affectionDecayMultiplier);
            j["lonelinessThresholdDays"] = settings.lonelinessThresholdDays;
            j["enablePolygamy"] = enablePolygamy;
            j["disablePolygamyQuest"] = settings.disablePolygamyQuest;
            j["hasSkyrimNet"] = hasSkyrimNet;
            j["simulateNoSkyrimNet"] = settings.simulateNoSkyrimNet;
            j["startDialGender"] = startDialGender;
            j["minRelRankForDial"] = minRelRankForDial;
            j["enableConfirmPlayerNpc"] = settings.enableConfirmPlayerNpc;
            j["enableConfirmNpcNpc"] = settings.enableConfirmNpcNpc;
            j["confirmStatusChange_Dating"] = settings.confirmStatusChange_Dating;
            j["confirmStatusChange_Engagement"] = settings.confirmStatusChange_Engagement;
            j["confirmStatusChange_Lover"] = settings.confirmStatusChange_Lover;
            j["confirmStatusChange_Marry"] = settings.confirmStatusChange_Marry;
            j["confirmBreakup_Dating"] = settings.confirmBreakup_Dating;
            j["confirmBreakup_Engagement"] = settings.confirmBreakup_Engagement;
            j["confirmBreakup_Lover"] = settings.confirmBreakup_Lover;
            j["confirmDivorce"] = settings.confirmDivorce;
            j["confirmDivorceDuringLowAffectionQuest"] = settings.confirmDivorceDuringLowAffectionQuest;
            j["confirmOpenRelationship"] = settings.confirmOpenRelationship;
            j["confirmCloseRelationship"] = settings.confirmCloseRelationship;
            j["enableConfirmationModal"] = settings.enableConfirmationModal;
            j["actionsCooldown"] = settings.actionsCooldown;
            j["actionsDeclineCooldown"] = settings.actionsDeclineCooldown;
            j["skyrimnetActionRadius"] = RoundFloat(settings.skyrimnetActionRadius);
            j["skyrimnetUseLLMMemories"] = settings.skyrimnetUseLLMMemories;
            j["skyrimnetUseDecisions"] = settings.skyrimnetUseDecisions;
            j["matchmakingConfirmResults"] = settings.matchmakingConfirmResults;
            j["playerPartnerCheatingAgencyEnabled"] = settings.playerPartnerCheatingAgencyEnabled;
            j["playerPartnerOpenRelAgencyEnabled"] = settings.playerPartnerOpenRelAgencyEnabled;
            j["playerPartnerCheatingChanceMultiplier"] = RoundFloat(settings.playerPartnerCheatingChanceMultiplier);
            j["playerPartnerCheatingMaxRollCap"] = settings.playerPartnerCheatingMaxRollCap;
            j["playerPartnerCheatingMaxSecretLovers"] = settings.playerPartnerCheatingMaxSecretLovers;
            j["playerPartnerCheatingAllowWhenHappy"] = settings.playerPartnerCheatingAllowWhenHappy;
            j["playerPartnerCheatingEnableNeglect"] = settings.playerPartnerCheatingEnableNeglect;
            j["playerPartnerCheatingEnableRevenge"] = settings.playerPartnerCheatingEnableRevenge;
            j["playerPartnerCheatingEnableHierarchy"] = settings.playerPartnerCheatingEnableHierarchy;
            j["playerPartnerCheatingEnableChildrenAnchor"] = settings.playerPartnerCheatingEnableChildrenAnchor;
            j["playerPartnerCheatingUseLLM"] = settings.playerPartnerCheatingUseLLM;
            j["playerPartnerCheatingConfirmResults"] = settings.playerPartnerCheatingConfirmResults;
            j["playerPartnerAgencyOpenLoverChanceMultiplier"] = RoundFloat(settings.playerPartnerAgencyOpenLoverChanceMultiplier);
            j["playerPartnerAgencyBreakupChanceMultiplier"] = RoundFloat(settings.playerPartnerAgencyBreakupChanceMultiplier);
            j["playerPartnerAgencyEnableForceGreets"] = settings.playerPartnerAgencyEnableForceGreets;
            j["playerPartnerAgencyEnableHomeArrivalEvents"] = settings.playerPartnerAgencyEnableHomeArrivalEvents;
            j["multLonelinessDecayStart"] = RoundFloat(settings.multLonelinessDecayStart);
            j["multPartnerAgencyScanInterval"] = RoundFloat(settings.multPartnerAgencyScanInterval);
            j["multRecentAttentionWindow"] = RoundFloat(settings.multRecentAttentionWindow);
            j["multVeryRecentVisitWindow"] = RoundFloat(settings.multVeryRecentVisitWindow);
            j["multNeglectGrowthStart"] = RoundFloat(settings.multNeglectGrowthStart);
            j["multContentNeglectStart"] = RoundFloat(settings.multContentNeglectStart);
            j["multProlongedAbsencePenalty"] = RoundFloat(settings.multProlongedAbsencePenalty);
            j["multAffairTalkingEscalation"] = RoundFloat(settings.multAffairTalkingEscalation);
            j["multMinSideFlingAgeForBreakup"] = RoundFloat(settings.multMinSideFlingAgeForBreakup);
            j["multPartnerIntentTTL"] = RoundFloat(settings.multPartnerIntentTTL);
            j["multHomeArrivalCooldown"] = RoundFloat(settings.multHomeArrivalCooldown);
            j["multAffairFatigueStart"] = RoundFloat(settings.multAffairFatigueStart);
            j["multRomanticAttendedBreakup"] = RoundFloat(settings.multRomanticAttendedBreakup);
            j["multSuggestOpenNeglectMin"] = RoundFloat(settings.multSuggestOpenNeglectMin);
            j["alwaysSuccess"] = settings.alwaysSuccessMarriage;
            j["logLevel"] = settings.logLevel;
            j["logDestination"] = settings.logDestination;
            j["toggleUIKey"] = settings.toggleUIKey;
            j["toggleUIModCtrl"] = settings.toggleUIModCtrl;
            j["toggleUIModShift"] = settings.toggleUIModShift;
            j["toggleUIModAlt"] = settings.toggleUIModAlt;
            j["childCustodyGenderPolicy"] = static_cast<std::uint8_t>(settings.childCustodyGenderPolicy);
            j["childCustodyPlayerPolicy"] = static_cast<std::uint8_t>(settings.childCustodyPlayerPolicy);
            j["childCustodyPrioritizeBioParent"] = settings.childCustodyPrioritizeBioParent;
            j["globalBanCritter"] = settings.bGlobalBanCritter;
            j["globalBanPet"] = settings.bGlobalBanPet;
            j["childTrainingDurationDays"] = settings.childTrainingDurationDays;
            j["childTrainingCost"] = settings.childTrainingCost;
            j["childTrainingRequireFaction"] = settings.childTrainingRequireFaction;
            snappedTrainingCost = std::max(0, settings.childTrainingCost);

            j["paternityBaseChance"] = settings.paternityBaseChance;
            j["paternityDailyGrowth"] = RoundFloat(settings.paternityDailyGrowth);
            j["paternityMaxChance"] = settings.paternityMaxChance;
            j["paternityPhenotypeBaseChance"] = settings.paternityPhenotypeBaseChance;
            j["paternityPhenotypeDailyGrowth"] = RoundFloat(settings.paternityPhenotypeDailyGrowth);
            j["paternityPhenotypeMaxChance"] = settings.paternityPhenotypeMaxChance;
            j["paternityPhenotypeBirthChance"] = settings.paternityPhenotypeBirthChance;
            j["paternityNonSkyrimNetRevealChance"] = settings.paternityNonSkyrimNetRevealChance;
            j["paternityBreakupChance"] = settings.paternityBreakupChance;
            j["paternityBioStepUpChance"] = settings.paternityBioStepUpChance;
            j["paternityForceDiceRolls"] = settings.paternityForceDiceRolls;
            j["revealSecretsInUI"] = settings.revealSecretsInUI;
            j["uiFullScreen"] = settings.uiFullScreen;
            j["uiScale"] = RoundFloat(settings.uiScale);
            j["autoPauseOnOpen"] = settings.autoPauseOnOpen;
            j["enableScenes"] = settings.enableScenes;
            j["scenesCooldownHours"] = RoundFloat(settings.scenesCooldownHours);
            j["enableScenes_Lover"] = settings.enableScenes_Lover;
            j["enableScenes_Dating"] = settings.enableScenes_Dating;
            j["enableScenes_Engaged"] = settings.enableScenes_Engaged;
            j["enableScenes_Married"] = settings.enableScenes_Married;
            j["enableScenes_OpenRelationship"] = settings.enableScenes_OpenRelationship;
            j["enableScenes_Cheating"] = settings.enableScenes_Cheating;
            j["enableScenes_CheatingDiscovered"] = settings.enableScenes_CheatingDiscovered;
            j["enableScenes_CheatingCaught"] = settings.enableScenes_CheatingCaught;
            j["enableScenes_LocTypeTemple"] = settings.enableScenes_LocTypeTemple;
            j["enableScenes_LocTypeStore"] = settings.enableScenes_LocTypeStore;
            j["enableScenes_LocTypeStewardsDwelling"] = settings.enableScenes_LocTypeStewardsDwelling;
            j["enableScenes_LocTypeInn"] = settings.enableScenes_LocTypeInn;
            j["enableScenes_LocTypeHouse"] = settings.enableScenes_LocTypeHouse;
            j["enableScenes_LocTypeBarracks"] = settings.enableScenes_LocTypeBarracks;
            j["enableScenes_LocTypeFarm"] = settings.enableScenes_LocTypeFarm;
            j["enableScenes_LocTypeGuild"] = settings.enableScenes_LocTypeGuild;
            j["enableScenes_LocTypePlayerHouse"] = settings.enableScenes_LocTypePlayerHouse;
            j["enableScenes_BYOH_LocTypeHomestead"] = settings.enableScenes_BYOH_LocTypeHomestead;
            j["enableScenes_LocTypeTown"] = settings.enableScenes_LocTypeTown;
            j["enableScenes_LocTypeSettlement"] = settings.enableScenes_LocTypeSettlement;
            j["enableScenes_LocTypeOrcStronghold"] = settings.enableScenes_LocTypeOrcStronghold;
            j["enableScenes_LocTypeCity"] = settings.enableScenes_LocTypeCity;
            j["enableScenes_LocTypeLumberMill"] = settings.enableScenes_LocTypeLumberMill;
        }

        j["playerTrainingFactions"] = BuildPlayerTrainingEligibilityJson();
        j["trainingCatalog"] = BuildTrainingCatalogJson(snappedTrainingCost);

        bool playerHasDebug = false;
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (auto* debugSpell = MARAS::Utils::LookupForm<RE::SpellItem>(
                    0xD, "TT_MARAS.esp", "PrismaUIDataBridge: debugSpell")) {
                playerHasDebug = player->HasSpell(debugSpell);
            }
        }
        j["debugSpell"] = playerHasDebug;
        return j;
    }

    nlohmann::json PrismaUIDataBridge::BuildMatchmakingSettingsJson() const {
        const auto sSnap = MARAS::App::SettingsCoordinator::GetSingleton().Snapshot();
        const auto& s = *sSnap;

        nlohmann::json j;
        j["enabled"] = s.matchmakingEnabled;
        j["intervalDays"] = s.matchmakingIntervalDays;
        j["maxSuggestions"] = s.matchmakingMaxSuggestions;
        j["npcCooldownDays"] = s.matchmakingNpcCooldownDays;
        j["allowBreakups"] = s.matchmakingAllowBreakups;
        j["allowOpenClose"] = s.matchmakingAllowOpenClose;
        j["allowCheating"] = s.matchmakingAllowCheating;
        j["orientation"] = static_cast<std::uint8_t>(s.matchmakingOrientation);
        j["useLLM"] = s.matchmakingUseLLM;
        j["evaluationStep"] = s.matchmakingEvaluationStep;
        j["generateMemories"] = s.matchmakingGenerateMemories;
        j["confirmResults"] = s.matchmakingConfirmResults;
        j["diceRollChanceNewDating"] = s.diceRollChanceNewDating;
        j["diceRollChanceNewEngaged"] = s.diceRollChanceNewEngaged;
        j["diceRollChanceNewLoverAllowed"] = s.diceRollChanceNewLoverAllowed;
        j["diceRollChanceNewLoverCheating"] = s.diceRollChanceNewLoverCheating;
        j["diceRollChanceAdvance"] = s.diceRollChanceAdvance;
        j["diceRollChanceBreakup"] = s.diceRollChanceBreakup;
        j["diceRollChanceOpenClose"] = s.diceRollChanceOpenClose;
        j["playerPartnerCheatingAgencyEnabled"] = s.playerPartnerCheatingAgencyEnabled;
        j["playerPartnerOpenRelAgencyEnabled"] = s.playerPartnerOpenRelAgencyEnabled;
        j["playerPartnerCheatingChanceMultiplier"] = RoundFloat(s.playerPartnerCheatingChanceMultiplier);
        j["playerPartnerCheatingMaxRollCap"] = s.playerPartnerCheatingMaxRollCap;
        j["playerPartnerCheatingMaxSecretLovers"] = s.playerPartnerCheatingMaxSecretLovers;
        j["playerPartnerCheatingAllowWhenHappy"] = s.playerPartnerCheatingAllowWhenHappy;
        j["playerPartnerCheatingEnableNeglect"] = s.playerPartnerCheatingEnableNeglect;
        j["playerPartnerCheatingEnableRevenge"] = s.playerPartnerCheatingEnableRevenge;
        j["playerPartnerCheatingEnableHierarchy"] = s.playerPartnerCheatingEnableHierarchy;
        j["playerPartnerCheatingEnableChildrenAnchor"] = s.playerPartnerCheatingEnableChildrenAnchor;
        j["playerPartnerCheatingUseLLM"] = s.playerPartnerCheatingUseLLM;
        j["playerPartnerCheatingConfirmResults"] = s.playerPartnerCheatingConfirmResults;
        j["lonelinessThresholdDays"] = s.lonelinessThresholdDays;
        j["playerPartnerAgencyOpenLoverChanceMultiplier"] = RoundFloat(s.playerPartnerAgencyOpenLoverChanceMultiplier);
        j["playerPartnerAgencyBreakupChanceMultiplier"] = RoundFloat(s.playerPartnerAgencyBreakupChanceMultiplier);
        j["playerPartnerAgencyEnableForceGreets"] = s.playerPartnerAgencyEnableForceGreets;
        j["playerPartnerAgencyEnableHomeArrivalEvents"] = s.playerPartnerAgencyEnableHomeArrivalEvents;
        j["multLonelinessDecayStart"] = RoundFloat(s.multLonelinessDecayStart);
        j["multPartnerAgencyScanInterval"] = RoundFloat(s.multPartnerAgencyScanInterval);
        j["multRecentAttentionWindow"] = RoundFloat(s.multRecentAttentionWindow);
        j["multVeryRecentVisitWindow"] = RoundFloat(s.multVeryRecentVisitWindow);
        j["multNeglectGrowthStart"] = RoundFloat(s.multNeglectGrowthStart);
        j["multContentNeglectStart"] = RoundFloat(s.multContentNeglectStart);
        j["multProlongedAbsencePenalty"] = RoundFloat(s.multProlongedAbsencePenalty);
        j["multAffairTalkingEscalation"] = RoundFloat(s.multAffairTalkingEscalation);
        j["multMinSideFlingAgeForBreakup"] = RoundFloat(s.multMinSideFlingAgeForBreakup);
        j["multPartnerIntentTTL"] = RoundFloat(s.multPartnerIntentTTL);
        j["multHomeArrivalCooldown"] = RoundFloat(s.multHomeArrivalCooldown);
        j["multAffairFatigueStart"] = RoundFloat(s.multAffairFatigueStart);
        j["multRomanticAttendedBreakup"] = RoundFloat(s.multRomanticAttendedBreakup);
        j["multSuggestOpenNeglectMin"] = RoundFloat(s.multSuggestOpenNeglectMin);
        return j;
    }

    nlohmann::json PrismaUIDataBridge::BuildNPCProfileJson(
        RE::FormID npcFormID,
        const MARAS::Projection::PerspectiveView& view) const {
        const auto& relUses = MARAS::App::RelationshipUseCases::GetSingleton();
        const auto& lineage = MARAS::App::LineageUseCases::GetSingleton();

        nlohmann::json profile;
        profile["formId"] = FormIDToString(npcFormID);

        if (npcFormID == MARAS::Foundation::kPlayerRefRawId) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            profile["name"] = MARAS::ActorSvc::GetName(MARAS::Foundation::kPlayerRefRawId);
            const std::string pRace = MARAS::ActorSvc::GetRaceName(MARAS::Foundation::kPlayerRefRawId);
            profile["race"] = !pRace.empty() ? pRace : "Nord";
            profile["gender"] = (player && player->GetActorBase() &&
                                 player->GetActorBase()->GetSex() == RE::SEXES::kFemale)
                                    ? "female"
                                    : "male";
        } else {
            profile["name"] = MARAS::ActorSvc::GetName(npcFormID);
            profile["race"] = MARAS::ActorSvc::GetRaceName(npcFormID);
            profile["gender"] = (MARAS::ActorSvc::GetSex(npcFormID) == 1) ? "female" : "male";
        }

        profile["pluginName"] = MARAS::ActorSvc::GetPluginName(npcFormID);
        profile["socialClass"] = static_cast<std::uint8_t>(relUses.GetSocialClass(npcFormID));
        profile["skillType"] = static_cast<std::uint8_t>(relUses.GetSkillType(npcFormID));
        profile["temperament"] = static_cast<std::uint8_t>(relUses.GetTemperament(npcFormID));

        const auto perceivedIntent = view.GetPerceivedPendingIntent(MARAS::State::ActorId{ npcFormID });
        profile["pendingIntent"] = std::string(MARAS::Foundation::PartnerIntentTypeToString(perceivedIntent.type));
        profile["currentPackage"] = ResolveActorCurrentPackageName(npcFormID);

        const bool isChild = relUses.IsChildNPC(npcFormID);
        profile["isChild"] = isChild;

        if (isChild) {
            profile["critterBanned"] = lineage.IsChildCritterBanned(npcFormID);
            profile["petBanned"] = lineage.IsChildPetBanned(npcFormID);

            const RE::FormID critterID = lineage.GetCritter(npcFormID);
            if (critterID != 0) {
                nlohmann::json critterObj;
                critterObj["formId"] = FormIDToString(critterID);
                critterObj["name"] = MARAS::ActorSvc::GetName(critterID);
                profile["currentCritter"] = std::move(critterObj);
            } else {
                profile["currentCritter"] = nullptr;
            }

            const RE::FormID petID = lineage.GetPet(npcFormID);
            if (petID != 0) {
                nlohmann::json petObj;
                petObj["formId"] = FormIDToString(petID);
                petObj["name"] = MARAS::ActorSvc::GetName(petID);
                profile["currentPet"] = std::move(petObj);
            } else {
                profile["currentPet"] = nullptr;
            }

            std::string blockReason;
            if (lineage.IsChildBlockedFromAdoption(npcFormID, &blockReason)) {
                profile["blockedFromAdoptionReason"] = blockReason;
            }
        }

        const auto& catalog = MARAS::Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        const auto perceivedChild = view.GetPerceivedChild(MARAS::State::ChildId{ npcFormID });
        if (perceivedChild.has_value()) {
            const auto& cEntry = *perceivedChild;
            profile["isChild"] = (cEntry.stage != MARAS::Foundation::ChildStage::Adult);
            const std::string curName = GetJsonString(profile, "name");
            if (!cEntry.name.empty() && (curName.empty() || curName.rfind("NPC_", 0) == 0)) {
                profile["name"] = cEntry.name;
            }
            profile["stage"] = ChildStageToString(cEntry.stage);

            auto* actor = MARAS::Foundation::HasPhysicalActorRawId(npcFormID)
                ? MARAS::SpatialWorldIndex::GetSingleton().GetActorFromFormId(npcFormID)
                : nullptr;
            const bool hasRealActor = (actor != nullptr && !actor->IsDeleted());
            const bool isEphemeral = !hasRealActor ||
                cEntry.stage == MARAS::Foundation::ChildStage::Unborn ||
                cEntry.stage == MARAS::Foundation::ChildStage::Infant ||
                cEntry.stage == MARAS::Foundation::ChildStage::Training;
            profile["hasRefID"] = hasRealActor;
            profile["isEphemeral"] = isEphemeral;
            if (!cEntry.trainingLocation.empty()) {
                profile["trainingLocation"] = cEntry.trainingLocation;
            }
            std::int32_t resolvedProfileTrainIdx = cEntry.trainingClassIndex;
            if (!catalog.IsValidClassIndex(resolvedProfileTrainIdx) && !cEntry.trainingLocation.empty()) {
                resolvedProfileTrainIdx = catalog.FindClassByLocation(cEntry.trainingLocation);
            }
            if (catalog.IsValidClassIndex(resolvedProfileTrainIdx)) {
                profile["trainingClassIndex"] = resolvedProfileTrainIdx;
                profile["trainingClassName"] = std::string(lineage.GetTrainingClassName(resolvedProfileTrainIdx));
            }
            if (cEntry.trainingStartGameDay >= 0.0f) {
                profile["trainingStartGameDay"] = RoundFloat(cEntry.trainingStartGameDay);
            }
            profile["hasBeastblood"] = cEntry.hasBeastblood;
            if (cEntry.stage == MARAS::Foundation::ChildStage::Unborn || cEntry.gender == 2) {
                profile["gender"] = "unknown";
            } else {
                profile["gender"] = (cEntry.gender == 1) ? "female" : "male";
            }
        }

        const MARAS::State::HouseId tenantHouseID = view.Household().GetTenantHouse(npcFormID);
        std::string currentHomeName = MARAS::App::HouseholdUseCases::GetHouseName(tenantHouseID.raw());
        if (currentHomeName.empty()) {
            if (const auto* npcProf = view.Actors().Find(MARAS::State::RefId{ npcFormID })) {
                if (npcProf->legacyCurrentHome.has_value() && !npcProf->legacyCurrentHome->IsNull()) {
                    currentHomeName = MARAS::App::HouseholdUseCases::GetHouseName(
                        npcProf->legacyCurrentHome->raw());
                }
            }
        }
        if (currentHomeName.empty() && perceivedChild.has_value() &&
            perceivedChild->stage == MARAS::Foundation::ChildStage::Adult &&
            !MARAS::Foundation::IsSyntheticChildRawId(npcFormID)) {
            currentHomeName = MARAS::App::HouseholdUseCases::GetSingleton().GetCurrentDestinationName(npcFormID);
        }
        if (!currentHomeName.empty()) {
            profile["currentHome"] = currentHomeName;
        }
        profile["stayAtHome"] = view.Household().GetTenantStayAtHome(npcFormID);

        std::string origHomeName;
        if (const auto* npcProf = view.Actors().Find(MARAS::State::RefId{ npcFormID })) {
            if (npcProf->legacyOriginalHome.has_value() && !npcProf->legacyOriginalHome->IsNull()) {
                origHomeName = LookupLocationOrCellName(npcProf->legacyOriginalHome->raw());
            }
        }
        if (origHomeName.empty() && MARAS::Foundation::HasPhysicalActorRawId(npcFormID)) {
            const RE::FormID origCellID = MARAS::BedOwnershipReconciler::GetSingleton().GetNpcHomeCell(npcFormID);
            origHomeName = LookupLocationOrCellName(origCellID);
        }
        if (origHomeName.empty() && perceivedChild.has_value()) {
            const auto& cEntry = *perceivedChild;
            if (!cEntry.guardians.slotA.IsPlayer() && !cEntry.guardians.slotB.IsPlayer()) {
                RE::FormID parentID = 0;
                if (!cEntry.guardians.slotA.IsNull()) {
                    parentID = cEntry.guardians.slotA.raw();
                } else if (!cEntry.guardians.slotB.IsNull()) {
                    parentID = cEntry.guardians.slotB.raw();
                } else if (!cEntry.bioMother.IsNull() && !cEntry.bioMother.IsPlayer()) {
                    parentID = cEntry.bioMother.raw();
                } else if (!cEntry.perceivedBioFather.IsNull() &&
                           !cEntry.perceivedBioFather.IsUnknownFather() &&
                           !cEntry.perceivedBioFather.IsPlayer()) {
                    parentID = cEntry.perceivedBioFather.raw();
                }
                if (parentID != 0) {
                    if (const auto* parentProf = view.Actors().Find(MARAS::State::RefId{ parentID })) {
                        if (parentProf->legacyOriginalHome.has_value() &&
                            !parentProf->legacyOriginalHome->IsNull()) {
                            origHomeName = LookupLocationOrCellName(parentProf->legacyOriginalHome->raw());
                        }
                    }
                    if (origHomeName.empty()) {
                        const RE::FormID parentCellID =
                            MARAS::BedOwnershipReconciler::GetSingleton().GetNpcHomeCell(parentID);
                        origHomeName = LookupLocationOrCellName(parentCellID);
                    }
                }
            }
        }
        if (!origHomeName.empty()) {
            profile["originalHome"] = origHomeName;
        }
        return profile;
    }

    nlohmann::json PrismaUIDataBridge::BuildChildJson(
        const MARAS::Projection::PerceivedChildLineage& child,
        const MARAS::Projection::PerspectiveView& view) const {
        const RE::FormID childID = child.childId.raw();
        const RE::FormID bioFatherID =
            (!child.perceivedBioFather.IsNull() && !child.perceivedBioFather.IsUnknownFather())
                ? child.perceivedBioFather.raw()
                : 0;
        const RE::FormID visiblePresumedFatherID = child.presumedFather.raw();

        nlohmann::json cObj;
        cObj["childId"] = FormIDToString(childID);
        std::string cName = child.name;
        if (cName.empty() || cName.rfind("NPC_", 0) == 0) {
            cName = MARAS::ActorSvc::GetName(childID);
        }
        cObj["name"] = cName;
        cObj["timestamp"] = child.timestamp;
        cObj["isAdopted"] = child.isAdopted;
        cObj["guardianAId"] = !child.guardians.slotA.IsNull() ? FormIDToString(child.guardians.slotA.raw()) : "";
        cObj["guardianBId"] = !child.guardians.slotB.IsNull() ? FormIDToString(child.guardians.slotB.raw()) : "";
        cObj["guardianAName"] = !child.guardians.slotA.IsNull() ? MARAS::ActorSvc::GetName(child.guardians.slotA.raw()) : "";
        cObj["guardianBName"] = !child.guardians.slotB.IsNull() ? MARAS::ActorSvc::GetName(child.guardians.slotB.raw()) : "";
        cObj["bioMotherId"] = !child.bioMother.IsNull() ? FormIDToString(child.bioMother.raw()) : "";
        cObj["bioFatherId"] = bioFatherID != 0 ? FormIDToString(bioFatherID) : "";

        const std::string bioMotherName = !child.bioMother.IsNull() ? MARAS::ActorSvc::GetName(child.bioMother.raw()) : "";
        std::string bioFatherName = bioFatherID != 0 ? MARAS::ActorSvc::GetName(bioFatherID) : "";
        if (bioFatherName.empty() && !child.perceivedBioFatherFallbackName.empty()) {
            bioFatherName = child.perceivedBioFatherFallbackName;
        }
        cObj["bioMotherName"] = bioMotherName;
        cObj["bioFatherName"] = bioFatherName;
        cObj["presumedFatherId"] = visiblePresumedFatherID != 0 ? FormIDToString(visiblePresumedFatherID) : "";
        cObj["presumedFatherName"] = visiblePresumedFatherID != 0 ? MARAS::ActorSvc::GetName(visiblePresumedFatherID) : "";
        cObj["conceptionTimestamp"] = child.conceptionTimestamp;
        cObj["birthTimestamp"] = child.birthTimestamp;
        cObj["stage"] = ChildStageToString(child.stage);

        auto* actor = MARAS::Foundation::HasPhysicalActorRawId(childID)
            ? MARAS::SpatialWorldIndex::GetSingleton().GetActorFromFormId(childID)
            : nullptr;
        const bool hasRealActor = (actor != nullptr && !actor->IsDeleted());
        const bool isEphemeral = !hasRealActor ||
            child.stage == MARAS::Foundation::ChildStage::Unborn ||
            child.stage == MARAS::Foundation::ChildStage::Infant ||
            child.stage == MARAS::Foundation::ChildStage::Training;

        cObj["hasRefID"] = hasRealActor;
        cObj["isEphemeral"] = isEphemeral;
        if (!child.trainingLocation.empty()) {
            cObj["trainingLocation"] = child.trainingLocation;
        }

        const auto& catalog = MARAS::Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        std::int32_t resolvedChildTrainIdx = child.trainingClassIndex;
        if (!catalog.IsValidClassIndex(resolvedChildTrainIdx) && !child.trainingLocation.empty()) {
            resolvedChildTrainIdx = catalog.FindClassByLocation(child.trainingLocation);
        }
        if (catalog.IsValidClassIndex(resolvedChildTrainIdx)) {
            cObj["trainingClassIndex"] = resolvedChildTrainIdx;
            cObj["trainingClassName"] = std::string(
                MARAS::App::LineageUseCases::GetSingleton().GetTrainingClassName(resolvedChildTrainIdx));
        }
        if (child.trainingStartGameDay >= 0.0f) {
            cObj["trainingStartGameDay"] = RoundFloat(child.trainingStartGameDay);
        }
        cObj["isBiologicalChild"] = child.isBiologicalChild;
        cObj["hasBeastblood"] = child.hasBeastblood;
        cObj["trainingDiscounts"] = BuildChildTrainingDiscountsJson(child.childId, view);

        const bool hasVisibleSecretMeta = child.paternitySecret.has_value();
        const bool isActiveSecret = hasVisibleSecretMeta &&
            !child.paternitySecret->discovered &&
            (child.paternitySecret->isSecretForBioFather ||
             child.paternitySecret->isSecretForPartner ||
             child.paternitySecret->isPregnancySecretForPartner);
        cObj["isSecret"] = isActiveSecret;
        cObj["isSecretForBioFather"] = hasVisibleSecretMeta && child.paternitySecret->isSecretForBioFather;
        cObj["isSecretForPartner"] = hasVisibleSecretMeta && child.paternitySecret->isSecretForPartner;
        cObj["isPregnancySecretForPartner"] = hasVisibleSecretMeta && child.paternitySecret->isPregnancySecretForPartner;
        cObj["hasPhenotypeMismatch"] = hasVisibleSecretMeta && child.paternitySecret->hasPhenotypeMismatch;
        cObj["discoveredByPartner"] = hasVisibleSecretMeta && child.paternitySecret->discoveredByPartner;
        cObj["discoveredByBioFather"] = hasVisibleSecretMeta && child.paternitySecret->discoveredByBioFather;

        int childSex = 0;
        if (hasRealActor && actor->GetActorBase()) {
            childSex = (actor->GetActorBase()->GetSex() == RE::SEXES::kFemale) ? 1 : 0;
        } else {
            childSex = child.gender;
        }
        if (child.stage == MARAS::Foundation::ChildStage::Unborn || childSex == 2) {
            cObj["gender"] = "unknown";
        } else {
            cObj["gender"] = (childSex == 1) ? "female" : "male";
        }
        return cObj;
    }

    nlohmann::json PrismaUIDataBridge::BuildNPCDetailJson(
        RE::FormID npcFormID,
        const MARAS::Projection::PerspectiveView& view) const {
        const auto& relUses = MARAS::App::RelationshipUseCases::GetSingleton();

        nlohmann::json detail = BuildNPCProfileJson(npcFormID, view);
        detail["formId"] = FormIDToString(npcFormID);

        std::string actorName = MARAS::ActorSvc::GetName(npcFormID);
        if (const auto childOpt = view.GetPerceivedChild(MARAS::State::ChildId{ npcFormID });
            childOpt.has_value() && !childOpt->name.empty()) {
            actorName = childOpt->name;
        }
        if (!actorName.empty() && actorName.rfind("NPC_", 0) != 0) {
            detail["name"] = actorName;
        }

        detail["affection"]["permanent"] = view.Affection().GetPermanent(MARAS::State::ActorId{ npcFormID });
        detail["affection"]["decayMultiplier"] = RoundFloat(relUses.GetAffectionDecayMultiplier());
        detail["affection"]["daysSinceLastInteraction"] = std::round(relUses.GetDaysSinceLastAffection(npcFormID));

        detail["hierarchyRank"] = view.Hierarchy().GetRank(MARAS::State::ActorId{ npcFormID });
        auto visibleStatus = MARAS::Foundation::RelationshipStatus::Unknown;
        auto originalStatus = MARAS::Foundation::RelationshipStatus::Unknown;
        if (const auto edge = view.GetPerceivedPair(
                MARAS::State::ActorId::Player(), MARAS::State::ActorId{ npcFormID })) {
            visibleStatus = edge->status;
            originalStatus = edge->originalStatus;
        }
        detail["status"] = static_cast<std::uint8_t>(visibleStatus);
        detail["originalStatus"] = static_cast<std::uint8_t>(originalStatus);
        return detail;
    }

    nlohmann::json PrismaUIDataBridge::BuildAllSkyrimNPCsJson() const {
        nlohmann::json npcs = nlohmann::json::array();
        const auto allEntries = MARAS::SpatialWorldIndex::GetSingleton().GetAllEntries();

        for (const auto& entry : allEntries) {
            if (entry.refFormID == 0 ||
                entry.refFormID == MARAS::Foundation::kPlayerBaseRawId ||
                entry.refFormID == MARAS::Foundation::kPlayerRefRawId) {
                continue;
            }

            nlohmann::json item;
            item["formId"] = FormIDToString(entry.refFormID);
            item["name"] = entry.nameDisplay;
            item["race"] = entry.raceName;
            item["gender"] = (entry.sex == 1) ? "female" : "male";
            item["isChild"] = entry.isChild;
            item["pluginName"] = entry.pluginName.empty()
                ? MARAS::ActorSvc::GetPluginName(entry.refFormID)
                : entry.pluginName;
            npcs.push_back(std::move(item));
        }

        MARAS_LOG_INFO("[PrismaUIDataBridge] Collected {} placed Skyrim NPCs via SpatialWorldIndex", npcs.size());

        nlohmann::json response;
        response["npcs"] = std::move(npcs);
        return response;
    }

    nlohmann::json PrismaUIDataBridge::BuildVisibleProfilesArray(
        const MARAS::Projection::PerspectiveView& view,
        const std::vector<MARAS::Projection::PerceivedChildLineage>& visibleChildren) const {
        nlohmann::json profilesArray = nlohmann::json::array();
        std::unordered_set<RE::FormID> addedProfiles;

        if (addedProfiles.insert(MARAS::Foundation::kPlayerRefRawId).second) {
            profilesArray.push_back(BuildNPCProfileJson(MARAS::Foundation::kPlayerRefRawId, view));
        }

        for (const auto& [refId, actorProfile] : view.Actors().AllProfiles()) {
            (void)actorProfile;
            if (!refId.IsNull() && addedProfiles.insert(refId.raw()).second) {
                profilesArray.push_back(BuildNPCProfileJson(refId.raw(), view));
            }
        }

        auto tryAddActorProfile = [&](RE::FormID actorId) {
            if (actorId != 0 &&
                actorId != MARAS::Foundation::kPlayerRefRawId &&
                actorId != MARAS::Foundation::kUnknownFatherRawId &&
                addedProfiles.insert(actorId).second) {
                profilesArray.push_back(BuildNPCProfileJson(actorId, view));
            }
        };

        for (const auto& child : visibleChildren) {
            tryAddActorProfile(child.guardians.slotA.raw());
            tryAddActorProfile(child.guardians.slotB.raw());
            tryAddActorProfile(child.bioMother.raw());
            tryAddActorProfile(child.perceivedBioFather.raw());
            tryAddActorProfile(child.presumedFather.raw());

            const RE::FormID childId = child.childId.raw();
            if (childId != 0 && addedProfiles.insert(childId).second) {
                nlohmann::json childObj = BuildNPCProfileJson(childId, view);
                const bool isChild = (child.stage != MARAS::Foundation::ChildStage::Adult);
                childObj["isChild"] = isChild;
                const std::string currentName = GetJsonString(childObj, "name");
                if ((currentName.empty() || currentName.rfind("NPC_", 0) == 0) && !child.name.empty()) {
                    childObj["name"] = child.name;
                }
                const std::string currentRace = GetJsonString(childObj, "race");
                if (currentRace.empty() || currentRace == "Nord") {
                    const RE::FormID motherID = child.bioMother.raw();
                    const RE::FormID validPerceivedFather =
                        (!child.perceivedBioFather.IsNull() && !child.perceivedBioFather.IsUnknownFather())
                            ? child.perceivedBioFather.raw()
                            : 0;
                    const RE::FormID parentFormID = (motherID != 0) ? motherID : validPerceivedFather;
                    if (parentFormID != 0) {
                        childObj["race"] = MARAS::ActorSvc::GetRaceName(parentFormID);
                    }
                }
                if (isChild) {
                    if (child.stage == MARAS::Foundation::ChildStage::Unborn || child.gender == 2) {
                        childObj["gender"] = "unknown";
                    } else {
                        childObj["gender"] = (child.gender == 1) ? "female" : "male";
                    }
                }
                profilesArray.push_back(std::move(childObj));
            }
        }

        for (const auto& entry : MARAS::SpatialWorldIndex::GetSingleton().GetAllEntries(MARAS::NPCFilter::ChildOnly)) {
            const RE::FormID childID = entry.refFormID;
            if (childID == 0 || !addedProfiles.insert(childID).second) {
                continue;
            }
            nlohmann::json childObj = BuildNPCProfileJson(childID, view);
            childObj["isChild"] = true;
            const int sex = static_cast<int>(MARAS::ActorSvc::GetSex(childID));
            const int effectiveSex = (entry.sex <= 2) ? static_cast<int>(entry.sex) : sex;
            childObj["gender"] = (effectiveSex == 2) ? "unknown" : ((effectiveSex == 1) ? "female" : "male");
            profilesArray.push_back(std::move(childObj));
        }

        for (const auto& affair : view.GetAllVisibleAffairs()) {
            tryAddActorProfile(affair.cheater.raw());
            tryAddActorProfile(affair.deceivedPartner.raw());
            tryAddActorProfile(affair.paramour.raw());
        }

        return profilesArray;
    }

    nlohmann::json PrismaUIDataBridge::BuildPlayerTrainingEligibilityJson() const {
        const auto& loader = MARAS::Engine::TrainingCatalogLoader::GetSingleton();
        const auto& catalog = loader.GetCatalog();
        const auto& port = loader.GetAffiliationPort();
        const bool playerWerewolf = MARAS::ChildSpawnService::IsActorWerewolf(
            MARAS::Foundation::kPlayerRefRawId);

        nlohmann::json j = nlohmann::json::object();
        for (const auto& spec : catalog.AllClasses()) {
            if (!spec.requirementKey.empty()) {
                j[spec.requirementKey] = catalog.EvaluatePlayerRequirements(spec.index, playerWerewolf, port, nullptr);
            }
        }

        std::int32_t playerGold = 0;
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (auto* goldBase = MARAS::FormCache::GetSingleton().GetGoldBaseObject()) {
                auto inv = player->GetInventory();
                if (const auto it = inv.find(goldBase); it != inv.end()) {
                    playerGold = it->second.first;
                }
            }
        }
        j["playerGold"] = playerGold;
        return j;
    }

    std::string PrismaUIDataBridge::ResolveTrainingImageSrc(const std::string& imagePath) const {
        if (imagePath.empty()) {
            return {};
        }
        {
            std::lock_guard lock(mutex_);
            if (const auto it = cachedTrainingImages_.find(imagePath); it != cachedTrainingImages_.end()) {
                return it->second;
            }
        }

        std::error_code ec;
        std::filesystem::path resolvedPath(imagePath);
        if (!std::filesystem::exists(resolvedPath, ec) || !std::filesystem::is_regular_file(resolvedPath, ec)) {
            const auto parent = resolvedPath.parent_path();
            const auto stem = resolvedPath.stem().string();
            bool foundAlt = false;
            for (const std::string_view ext : { ".webp", ".png", ".jpg", ".jpeg" }) {
                auto candidate = parent / (stem + std::string(ext));
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
                    resolvedPath = std::move(candidate);
                    foundAlt = true;
                    break;
                }
            }
            if (!foundAlt) {
                return imagePath;
            }
        }

        std::ifstream ifs(resolvedPath, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            return imagePath;
        }
        const auto fileSize = ifs.tellg();
        if (fileSize <= 0 || static_cast<std::size_t>(fileSize) > kMaxTrainingImageBytes) {
            return imagePath;
        }
        ifs.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(fileSize));
        if (!ifs.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            return imagePath;
        }

        const std::string extLower = MARAS::Foundation::StringUtils::ToLowerStd(resolvedPath.extension().string());
        std::string_view mime = "image/webp";
        if (extLower == ".png") {
            mime = "image/png";
        } else if (extLower == ".jpg" || extLower == ".jpeg") {
            mime = "image/jpeg";
        } else if (extLower == ".svg") {
            mime = "image/svg+xml";
        }

        std::string encoded = std::format("data:{};base64,{}", mime, Base64EncodeBytes(buffer));
        {
            std::lock_guard lock(mutex_);
            cachedTrainingImages_.emplace(imagePath, encoded);
        }
        return encoded;
    }

    nlohmann::json PrismaUIDataBridge::BuildTrainingCatalogJson(std::int32_t baseCost) const {
        const auto& loader = MARAS::Engine::TrainingCatalogLoader::GetSingleton();
        const auto& catalog = loader.GetCatalog();
        const auto& port = loader.GetAffiliationPort();
        const bool playerWerewolf = MARAS::ChildSpawnService::IsActorWerewolf(
            MARAS::Foundation::kPlayerRefRawId);

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& spec : catalog.AllClasses()) {
            nlohmann::json item;
            item["index"] = spec.index;
            item["id"] = spec.id;
            item["className"] = spec.className;
            item["location"] = spec.locationName;
            item["description"] = spec.description;
            item["image"] = ResolveTrainingImageSrc(spec.imagePath);
            item["costMultiplier"] = RoundFloat(spec.costMultiplier);
            item["effectiveCost"] = catalog.GetEffectiveTrainingCost(spec.index, baseCost);
            item["requiresBeastblood"] = spec.requiresBeastblood;
            item["requirementMet"] = catalog.EvaluatePlayerRequirements(spec.index, playerWerewolf, port, nullptr);
            if (!spec.requirementKey.empty()) {
                item["factionKey"] = spec.requirementKey;
            }
            if (!spec.requirementLabel.empty()) {
                item["factionLabel"] = spec.requirementLabel;
            }
            arr.push_back(std::move(item));
        }
        return arr;
    }

    nlohmann::json PrismaUIDataBridge::BuildChildTrainingDiscountsJson(
        MARAS::State::ChildId childId,
        const MARAS::Projection::PerspectiveView& view) const {
        const auto& loader = MARAS::Engine::TrainingCatalogLoader::GetSingleton();
        const auto& catalog = loader.GetCatalog();
        const auto& port = loader.GetAffiliationPort();

        nlohmann::json discounts = nlohmann::json::object();
        for (const auto& spec : catalog.AllClasses()) {
            const auto info = catalog.EvaluateChildTrainingDiscount(childId, spec.index, view, port);
            if (info.discountPercent > 0) {
                discounts[std::to_string(spec.index)] = {
                    { "percent", info.discountPercent },
                    { "reason", info.reason }
                };
            }
        }
        return discounts;
    }

    // =========================================================================
    // Inbound UI Action Processors
    // =========================================================================

    void PrismaUIDataBridge::ProcessSetAffection(const nlohmann::json& payload) {
        const RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        const int value = GetJsonInt(payload, "value", 0);
        if (id == 0) {
            return;
        }
        auto& relUses = MARAS::App::RelationshipUseCases::GetSingleton();
        relUses.SetPermanentAffection(id, value);
        if (relUses.GetRelationshipStatus(id, MARAS::Foundation::kPlayerRefRawId) ==
            MARAS::Foundation::RelationshipStatus::Married) {
            SendHouseholdBuffs();
        }
        MARAS_LOG_INFO("[PrismaUIDataBridge] Set affection for {:08X} to {}", id, value);
    }

    void PrismaUIDataBridge::ProcessSetSocialClass(const nlohmann::json& payload) {
        const RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        const int val = GetJsonInt(payload, "socialClass", 0);
        if (id != 0) {
            MARAS::App::RelationshipUseCases::GetSingleton().SetSocialClass(id, static_cast<std::int8_t>(val));
            SendOverviewData();
            SendNPCDetail(id);
            SendHouseholdBuffs();
        }
    }

    void PrismaUIDataBridge::ProcessSetSkillType(const nlohmann::json& payload) {
        const RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        const int val = GetJsonInt(payload, "skillType", 0);
        if (id != 0) {
            MARAS::App::RelationshipUseCases::GetSingleton().SetSkillType(id, static_cast<std::int8_t>(val));
            SendOverviewData();
            SendNPCDetail(id);
            SendHouseholdBuffs();
        }
    }

    void PrismaUIDataBridge::ProcessSetTemperament(const nlohmann::json& payload) {
        const RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        const int val = GetJsonInt(payload, "temperament", 0);
        if (id != 0) {
            MARAS::App::RelationshipUseCases::GetSingleton().SetTemperament(id, static_cast<std::int8_t>(val));
            SendOverviewData();
            SendNPCDetail(id);
        }
    }

    void PrismaUIDataBridge::ProcessSetPairIsOpen(const nlohmann::json& payload) {
        const RE::FormID actorA = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorA")));
        const RE::FormID actorB = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorB")));
        const bool isOpen = GetJsonBool(payload, "isOpen", false);
        if (actorA == 0 || actorB == 0) {
            return;
        }

        const bool fromMatchmaking = GetJsonBool(payload, "fromMatchmaking", false);
        const bool isNpcPair = actorA != MARAS::Foundation::kPlayerRefRawId &&
                               actorB != MARAS::Foundation::kPlayerRefRawId;
        const bool sendEvent = !(fromMatchmaking && isNpcPair);
        const bool hasDualMemories = payload.contains("dualMemories") &&
                                     payload["dualMemories"].is_array() &&
                                     !payload["dualMemories"].empty();
        const bool skyrimNetAvailable = MARAS::App::GetSkyrimNetGateway().IsAvailable();

        std::optional<MARAS::App::ScopedMemorySuppression> suppressScope;
        std::optional<MARAS::App::ScopedMatchmakingApply> mmScope;
        if (hasDualMemories) {
            suppressScope.emplace();
        } else if (fromMatchmaking && skyrimNetAvailable) {
            mmScope.emplace();
        }

        MARAS::App::RelationshipUseCases::GetSingleton().SetPairIsOpen(actorA, actorB, isOpen, sendEvent);

        if (hasDualMemories && skyrimNetAvailable) {
            auto entries = ParseDualMemoriesArray(payload["dualMemories"]);
            MARAS::App::GetSkyrimNetGateway().IngestMemories(entries, { actorA, actorB });
        }
        if (fromMatchmaking) {
            const float today = GetCurrentGameDays();
            auto& agency = MARAS::App::AgencyWorkflowCoordinator::GetSingleton();
            agency.RecordNpcCooldown(actorA, today);
            agency.RecordNpcCooldown(actorB, today);
        }

        SendOverviewData();
        if (actorA != MARAS::Foundation::kPlayerRefRawId) {
            SendNPCDetail(actorA);
        }
        if (actorB != MARAS::Foundation::kPlayerRefRawId) {
            SendNPCDetail(actorB);
        }
    }

    void PrismaUIDataBridge::ProcessSetHierarchyRank(const nlohmann::json& payload) {
        const RE::FormID id = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "formId")));
        const int rank = GetJsonInt(payload, "rank", -1);
        if (id != 0) {
            MARAS::App::RelationshipUseCases::GetSingleton().SetSpouseRank(id, rank);
            OnHierarchyChanged();
            SendOverviewData();
            SendNPCDetail(id);
            SendHouseholdBuffs();
        }
    }

    void PrismaUIDataBridge::ProcessAssignRelationship(const nlohmann::json& payload) {
        const RE::FormID actorA = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorA")));
        const RE::FormID actorB = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorB")));
        const auto statusVal = static_cast<std::uint8_t>(GetJsonInt(payload, "status", 0));

        if (actorA == 0 || actorB == 0) {
            MARAS_LOG_ERROR("[PrismaUIDataBridge] ProcessAssignRelationship: actorA ({:08X}) or actorB ({:08X}) is 0",
                            actorA, actorB);
            return;
        }
        if (!IsValidActorRefId(actorA)) {
            MARAS_LOG_ERROR("[PrismaUIDataBridge] ProcessAssignRelationship: actorA {:08X} is not a valid actor ref",
                            actorA);
            return;
        }
        if (!IsValidActorRefId(actorB)) {
            MARAS_LOG_ERROR("[PrismaUIDataBridge] ProcessAssignRelationship: actorB {:08X} is not a valid actor ref",
                            actorB);
            return;
        }

        const auto status = static_cast<MARAS::Foundation::RelationshipStatus>(statusVal);
        const bool hasDualMemories = payload.contains("dualMemories") &&
                                     payload["dualMemories"].is_array() &&
                                     !payload["dualMemories"].empty();
        const bool isMatchmakingApply = GetJsonBool(payload, "fromMatchmaking", false) || payload.contains("dualMemories");
        const bool isNpcPair = actorA != MARAS::Foundation::kPlayerRefRawId &&
                               actorB != MARAS::Foundation::kPlayerRefRawId;
        const bool sendEvent = !(isMatchmakingApply && isNpcPair);
        const bool skyrimNetAvailable = MARAS::App::GetSkyrimNetGateway().IsAvailable();

        std::optional<MARAS::App::ScopedMemorySuppression> suppressScope;
        std::optional<MARAS::App::ScopedMatchmakingApply> mmScope;
        if (hasDualMemories) {
            suppressScope.emplace();
        } else if (isMatchmakingApply && skyrimNetAvailable) {
            mmScope.emplace();
        }

        auto& relUses = MARAS::App::RelationshipUseCases::GetSingleton();
        const bool success = relUses.PromoteToStatusWith(actorA, actorB, status, sendEvent);
        if (!success) {
            std::string reason;
            (void)relUses.CanFormRelationship(actorA, actorB, status, &reason);
            if (reason.empty()) {
                reason = "Relationship assignment failed.";
            }
            nlohmann::json rej;
            rej["action"] = "assignRelationship";
            rej["actorA"] = FormIDToString(actorA);
            rej["actorB"] = FormIDToString(actorB);
            rej["status"] = statusVal;
            rej["reason"] = reason;
            PrismaUIManager::GetSingleton().SendToUI("onActionRejected", DumpJsonSafe(rej));
        } else {
            if (hasDualMemories && skyrimNetAvailable) {
                auto entries = ParseDualMemoriesArray(payload["dualMemories"]);
                MARAS::App::GetSkyrimNetGateway().IngestMemories(entries, { actorA, actorB });
            }
            if (isMatchmakingApply) {
                const float today = GetCurrentGameDays();
                auto& agency = MARAS::App::AgencyWorkflowCoordinator::GetSingleton();
                agency.RecordNpcCooldown(actorA, today);
                agency.RecordNpcCooldown(actorB, today);
            }
        }

        SendOverviewData();
        if (actorA != MARAS::Foundation::kPlayerRefRawId) {
            SendNPCDetail(actorA);
        }
        if (actorB != MARAS::Foundation::kPlayerRefRawId) {
            SendNPCDetail(actorB);
        }
        if (actorA == MARAS::Foundation::kPlayerRefRawId || actorB == MARAS::Foundation::kPlayerRefRawId) {
            SendHouseholdBuffs();
        }
    }

    void PrismaUIDataBridge::ProcessRemoveRelationship(const nlohmann::json& payload) {
        const RE::FormID actorA = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorA")));
        const RE::FormID actorB = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "actorB")));
        if (actorA == 0 || actorB == 0) {
            return;
        }

        MARAS::App::RelationshipUseCases::GetSingleton().RemoveRelationship(actorA, actorB);
        if (GetJsonBool(payload, "fromMatchmaking", false)) {
            const float today = GetCurrentGameDays();
            auto& agency = MARAS::App::AgencyWorkflowCoordinator::GetSingleton();
            agency.RecordNpcCooldown(actorA, today);
            agency.RecordNpcCooldown(actorB, today);
        }

        SendOverviewData();
        if (actorA != MARAS::Foundation::kPlayerRefRawId) {
            SendNPCDetail(actorA);
        }
        if (actorB != MARAS::Foundation::kPlayerRefRawId) {
            SendNPCDetail(actorB);
        }
        if (actorA == MARAS::Foundation::kPlayerRefRawId || actorB == MARAS::Foundation::kPlayerRefRawId) {
            SendHouseholdBuffs();
        }
    }

    void PrismaUIDataBridge::ProcessUpdateSetting(const nlohmann::json& payload) {
        const std::string key = GetJsonString(payload, "key");
        if (key == "debugSpell") {
            const bool add = GetJsonBool(payload, "value", false);
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (auto* debugSpell = MARAS::Utils::LookupForm<RE::SpellItem>(
                        0xD, "TT_MARAS.esp", "PrismaUIDataBridge: debugSpell")) {
                    if (add) {
                        player->AddSpell(debugSpell);
                    } else {
                        player->RemoveSpell(debugSpell);
                    }
                }
            }
            SendSettingsData();
            return;
        }

        if (key == "confirmAcceptProposal" ||
            key == "confirmBreakupEngagement" ||
            key == "confirmLowAffectionDivorce") {
            MARAS_LOG_WARN("[PrismaUIDataBridge] Deprecated setting key '{}' ignored", key);
            SendSettingsData();
            return;
        }

        auto& settingsMgr = MARAS::App::SettingsCoordinator::GetSingleton();
        const std::string normalizedKey = NormalizeSettingKeyForCoordinator(key);
        bool updated = false;
        const auto itVal = payload.find("value");
        if (itVal != payload.end()) {
            if (itVal->is_boolean()) {
                updated = settingsMgr.SetBoolByKey(normalizedKey, itVal->get<bool>());
                if (updated && key == "autoPauseOnOpen" && PrismaUIManager::GetSingleton().IsDashboardOpen()) {
                    PrismaUIManager::GetSingleton().SetDashboardPaused(settingsMgr.Snapshot()->autoPauseOnOpen);
                }
            } else if (itVal->is_number()) {
                if (key == "affectionDecayMult" || key == "affectionDecayMultiplier" ||
                    key == "skyrimnetActionRadius" || key == "actionRadius" ||
                    key == "scenesCooldownHours" ||
                    key == "paternityDailyGrowth" || key == "paternityPhenotypeDailyGrowth" ||
                    key == "uiScale" ||
                    key == "playerPartnerCheatingChanceMultiplier" ||
                    key == "playerPartnerAgencyOpenLoverChanceMultiplier" ||
                    key == "playerPartnerAgencyBreakupChanceMultiplier" ||
                    key.starts_with("mult")) {
                    updated = settingsMgr.SetFloatByKey(normalizedKey, itVal->get<float>());
                } else {
                    updated = settingsMgr.SetIntByKey(normalizedKey, itVal->get<std::int32_t>());
                }
            }
        }

        if (!updated) {
            MARAS_LOG_WARN("[PrismaUIDataBridge] Unknown or invalid setting key '{}'", key);
        }

        const RE::FormID detailNpcId = lastRequestedDetailNpcID_.load(std::memory_order_relaxed);
        if (updated && key == "revealSecretsInUI") {
            SendOverviewData();
            SendNPCList();
            if (detailNpcId != 0) {
                SendNPCDetail(detailNpcId);
            }
        } else if (updated && key == "enablePolygamy") {
            SendOverviewData();
            SendSettingsData();
            if (detailNpcId != 0) {
                SendNPCDetail(detailNpcId);
            }
        } else if (updated &&
                   (key == "affectionDecayMult" || key == "affectionDecayMultiplier" ||
                    key == "childTrainingDurationDays" || key == "childTrainingCost" ||
                    key == "childTrainingRequireFaction" || key == "bGlobalBanCritter" ||
                    key == "globalBanCritter" || key == "bGlobalBanPet" ||
                    key == "globalBanPet")) {
            SendOverviewData();
            if (detailNpcId != 0) {
                SendNPCDetail(detailNpcId);
            }
        } else {
            SendSettingsData();
        }
    }

    void PrismaUIDataBridge::ProcessUpdateMatchmakingSettings(const nlohmann::json& payload) {
        MARAS::State::WorldStore::GetSingleton().Modify([&](MARAS::State::WorldState& ws) {
            auto& s = ws.settings.Write();
            if (payload.contains("enabled")) s.matchmakingEnabled = GetJsonBool(payload, "enabled", s.matchmakingEnabled);
            if (payload.contains("intervalDays")) s.matchmakingIntervalDays = std::clamp(GetJsonInt(payload, "intervalDays", s.matchmakingIntervalDays), 1, 30);
            if (payload.contains("maxSuggestions")) s.matchmakingMaxSuggestions = std::clamp(GetJsonInt(payload, "maxSuggestions", s.matchmakingMaxSuggestions), 1, 15);
            if (payload.contains("npcCooldownDays")) s.matchmakingNpcCooldownDays = std::clamp(GetJsonInt(payload, "npcCooldownDays", s.matchmakingNpcCooldownDays), 1, 60);
            if (payload.contains("allowBreakups")) s.matchmakingAllowBreakups = GetJsonBool(payload, "allowBreakups", s.matchmakingAllowBreakups);
            if (payload.contains("allowOpenClose")) s.matchmakingAllowOpenClose = GetJsonBool(payload, "allowOpenClose", s.matchmakingAllowOpenClose);
            if (payload.contains("allowCheating")) s.matchmakingAllowCheating = GetJsonBool(payload, "allowCheating", s.matchmakingAllowCheating);
            if (payload.contains("orientation")) {
                const std::int32_t o = GetJsonInt(payload, "orientation", static_cast<std::int32_t>(s.matchmakingOrientation));
                if (o >= 0 && o <= static_cast<std::int32_t>(MARAS::Foundation::MatchmakingOrientation::HeteroAndLesbian)) {
                    s.matchmakingOrientation = static_cast<MARAS::Foundation::MatchmakingOrientation>(o);
                }
            }
            if (payload.contains("useLLM")) s.matchmakingUseLLM = GetJsonBool(payload, "useLLM", s.matchmakingUseLLM);
            if (payload.contains("evaluationStep")) s.matchmakingEvaluationStep = GetJsonBool(payload, "evaluationStep", s.matchmakingEvaluationStep);
            if (payload.contains("generateMemories")) s.matchmakingGenerateMemories = GetJsonBool(payload, "generateMemories", s.matchmakingGenerateMemories);
            if (payload.contains("confirmResults")) s.matchmakingConfirmResults = GetJsonBool(payload, "confirmResults", s.matchmakingConfirmResults);
            if (payload.contains("diceRollChanceNewDating")) s.diceRollChanceNewDating = std::clamp(GetJsonInt(payload, "diceRollChanceNewDating", s.diceRollChanceNewDating), 0, 100);
            if (payload.contains("diceRollChanceNewEngaged")) s.diceRollChanceNewEngaged = std::clamp(GetJsonInt(payload, "diceRollChanceNewEngaged", s.diceRollChanceNewEngaged), 0, 100);
            if (payload.contains("diceRollChanceNewLoverAllowed")) s.diceRollChanceNewLoverAllowed = std::clamp(GetJsonInt(payload, "diceRollChanceNewLoverAllowed", s.diceRollChanceNewLoverAllowed), 0, 100);
            if (payload.contains("diceRollChanceNewLoverCheating")) s.diceRollChanceNewLoverCheating = std::clamp(GetJsonInt(payload, "diceRollChanceNewLoverCheating", s.diceRollChanceNewLoverCheating), 0, 100);
            if (payload.contains("diceRollChanceAdvance")) s.diceRollChanceAdvance = std::clamp(GetJsonInt(payload, "diceRollChanceAdvance", s.diceRollChanceAdvance), 0, 100);
            if (payload.contains("diceRollChanceBreakup")) s.diceRollChanceBreakup = std::clamp(GetJsonInt(payload, "diceRollChanceBreakup", s.diceRollChanceBreakup), 0, 100);
            if (payload.contains("diceRollChanceOpenClose")) s.diceRollChanceOpenClose = std::clamp(GetJsonInt(payload, "diceRollChanceOpenClose", s.diceRollChanceOpenClose), 0, 100);
            if (payload.contains("playerPartnerCheatingAgencyEnabled")) s.playerPartnerCheatingAgencyEnabled = GetJsonBool(payload, "playerPartnerCheatingAgencyEnabled", s.playerPartnerCheatingAgencyEnabled);
            if (payload.contains("playerPartnerOpenRelAgencyEnabled")) s.playerPartnerOpenRelAgencyEnabled = GetJsonBool(payload, "playerPartnerOpenRelAgencyEnabled", s.playerPartnerOpenRelAgencyEnabled);
            if (payload.contains("playerPartnerCheatingChanceMultiplier")) s.playerPartnerCheatingChanceMultiplier = std::clamp(GetJsonFloat(payload, "playerPartnerCheatingChanceMultiplier", s.playerPartnerCheatingChanceMultiplier), 0.1f, 3.0f);
            if (payload.contains("playerPartnerCheatingMaxRollCap")) s.playerPartnerCheatingMaxRollCap = std::clamp(GetJsonInt(payload, "playerPartnerCheatingMaxRollCap", s.playerPartnerCheatingMaxRollCap), 5, 100);
            if (payload.contains("playerPartnerCheatingMaxSecretLovers")) s.playerPartnerCheatingMaxSecretLovers = std::clamp(GetJsonInt(payload, "playerPartnerCheatingMaxSecretLovers", s.playerPartnerCheatingMaxSecretLovers), 1, 5);
            if (payload.contains("playerPartnerCheatingAllowWhenHappy")) s.playerPartnerCheatingAllowWhenHappy = GetJsonBool(payload, "playerPartnerCheatingAllowWhenHappy", s.playerPartnerCheatingAllowWhenHappy);
            if (payload.contains("playerPartnerCheatingEnableNeglect")) s.playerPartnerCheatingEnableNeglect = GetJsonBool(payload, "playerPartnerCheatingEnableNeglect", s.playerPartnerCheatingEnableNeglect);
            if (payload.contains("playerPartnerCheatingEnableRevenge")) s.playerPartnerCheatingEnableRevenge = GetJsonBool(payload, "playerPartnerCheatingEnableRevenge", s.playerPartnerCheatingEnableRevenge);
            if (payload.contains("playerPartnerCheatingEnableHierarchy")) s.playerPartnerCheatingEnableHierarchy = GetJsonBool(payload, "playerPartnerCheatingEnableHierarchy", s.playerPartnerCheatingEnableHierarchy);
            if (payload.contains("playerPartnerCheatingEnableChildrenAnchor")) s.playerPartnerCheatingEnableChildrenAnchor = GetJsonBool(payload, "playerPartnerCheatingEnableChildrenAnchor", s.playerPartnerCheatingEnableChildrenAnchor);
            if (payload.contains("playerPartnerCheatingUseLLM")) s.playerPartnerCheatingUseLLM = GetJsonBool(payload, "playerPartnerCheatingUseLLM", s.playerPartnerCheatingUseLLM);
            if (payload.contains("playerPartnerCheatingConfirmResults")) s.playerPartnerCheatingConfirmResults = GetJsonBool(payload, "playerPartnerCheatingConfirmResults", s.playerPartnerCheatingConfirmResults);
            if (payload.contains("lonelinessThresholdDays")) s.lonelinessThresholdDays = std::clamp(GetJsonInt(payload, "lonelinessThresholdDays", s.lonelinessThresholdDays), 1, 14);
            if (payload.contains("playerPartnerAgencyOpenLoverChanceMultiplier")) s.playerPartnerAgencyOpenLoverChanceMultiplier = std::clamp(GetJsonFloat(payload, "playerPartnerAgencyOpenLoverChanceMultiplier", s.playerPartnerAgencyOpenLoverChanceMultiplier), 0.1f, 3.0f);
            if (payload.contains("playerPartnerAgencyBreakupChanceMultiplier")) s.playerPartnerAgencyBreakupChanceMultiplier = std::clamp(GetJsonFloat(payload, "playerPartnerAgencyBreakupChanceMultiplier", s.playerPartnerAgencyBreakupChanceMultiplier), 0.1f, 3.0f);
            if (payload.contains("playerPartnerAgencyEnableForceGreets")) s.playerPartnerAgencyEnableForceGreets = GetJsonBool(payload, "playerPartnerAgencyEnableForceGreets", s.playerPartnerAgencyEnableForceGreets);
            if (payload.contains("playerPartnerAgencyEnableHomeArrivalEvents")) s.playerPartnerAgencyEnableHomeArrivalEvents = GetJsonBool(payload, "playerPartnerAgencyEnableHomeArrivalEvents", s.playerPartnerAgencyEnableHomeArrivalEvents);
            if (payload.contains("multLonelinessDecayStart")) s.multLonelinessDecayStart = std::clamp(GetJsonFloat(payload, "multLonelinessDecayStart", s.multLonelinessDecayStart), 0.1f, 10.0f);
            if (payload.contains("multPartnerAgencyScanInterval")) s.multPartnerAgencyScanInterval = std::clamp(GetJsonFloat(payload, "multPartnerAgencyScanInterval", s.multPartnerAgencyScanInterval), 0.25f, 10.0f);
            if (payload.contains("multRecentAttentionWindow")) s.multRecentAttentionWindow = std::clamp(GetJsonFloat(payload, "multRecentAttentionWindow", s.multRecentAttentionWindow), 0.1f, 10.0f);
            if (payload.contains("multVeryRecentVisitWindow")) s.multVeryRecentVisitWindow = std::clamp(GetJsonFloat(payload, "multVeryRecentVisitWindow", s.multVeryRecentVisitWindow), 0.1f, 10.0f);
            if (payload.contains("multNeglectGrowthStart")) s.multNeglectGrowthStart = std::clamp(GetJsonFloat(payload, "multNeglectGrowthStart", s.multNeglectGrowthStart), 0.1f, 10.0f);
            if (payload.contains("multContentNeglectStart")) s.multContentNeglectStart = std::clamp(GetJsonFloat(payload, "multContentNeglectStart", s.multContentNeglectStart), 0.1f, 10.0f);
            if (payload.contains("multProlongedAbsencePenalty")) s.multProlongedAbsencePenalty = std::clamp(GetJsonFloat(payload, "multProlongedAbsencePenalty", s.multProlongedAbsencePenalty), 0.1f, 10.0f);
            if (payload.contains("multAffairTalkingEscalation")) s.multAffairTalkingEscalation = std::clamp(GetJsonFloat(payload, "multAffairTalkingEscalation", s.multAffairTalkingEscalation), 0.25f, 10.0f);
            if (payload.contains("multMinSideFlingAgeForBreakup")) s.multMinSideFlingAgeForBreakup = std::clamp(GetJsonFloat(payload, "multMinSideFlingAgeForBreakup", s.multMinSideFlingAgeForBreakup), 0.1f, 10.0f);
            if (payload.contains("multPartnerIntentTTL")) s.multPartnerIntentTTL = std::clamp(GetJsonFloat(payload, "multPartnerIntentTTL", s.multPartnerIntentTTL), 0.25f, 10.0f);
            if (payload.contains("multHomeArrivalCooldown")) s.multHomeArrivalCooldown = std::clamp(GetJsonFloat(payload, "multHomeArrivalCooldown", s.multHomeArrivalCooldown), 0.1f, 10.0f);
            if (payload.contains("multAffairFatigueStart")) s.multAffairFatigueStart = std::clamp(GetJsonFloat(payload, "multAffairFatigueStart", s.multAffairFatigueStart), 0.5f, 20.0f);
            if (payload.contains("multRomanticAttendedBreakup")) s.multRomanticAttendedBreakup = std::clamp(GetJsonFloat(payload, "multRomanticAttendedBreakup", s.multRomanticAttendedBreakup), 0.1f, 10.0f);
            if (payload.contains("multSuggestOpenNeglectMin")) s.multSuggestOpenNeglectMin = std::clamp(GetJsonFloat(payload, "multSuggestOpenNeglectMin", s.multSuggestOpenNeglectMin), 0.1f, 10.0f);
        });
        SendMatchmakingSettings();
        SendSettingsData();
    }

    void PrismaUIDataBridge::ProcessSearchNPCs(const nlohmann::json& payload) {
        const std::string query = GetJsonString(payload, "query");
        const std::string side = GetJsonString(payload, "side", "A");

        nlohmann::json response;
        response["query"] = query;
        response["side"] = side;
        nlohmann::json results = nlohmann::json::array();

        if (query.length() >= 3) {
            const auto view = MakeDashboardView();
            const auto matches = MARAS::SpatialWorldIndex::GetSingleton().SearchBySubstring(query, 50);
            for (const auto& entry : matches) {
                if (entry.refFormID == MARAS::Foundation::kPlayerBaseRawId ||
                    entry.refFormID == MARAS::Foundation::kPlayerRefRawId) {
                    continue;
                }
                results.push_back(BuildNPCProfileJson(entry.refFormID, view));
            }
        }

        response["results"] = std::move(results);
        PrismaUIManager::GetSingleton().SendToUI("receiveNPCSearchResults", DumpJsonSafe(response));
    }

    void PrismaUIDataBridge::ProcessTeleportPlayerToNpc(const nlohmann::json& payload) {
        const RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        if (npcFormID == 0) {
            return;
        }
        auto* target = MARAS::ActorSvc::GetReference(npcFormID);
        if (!target) {
            target = MARAS::SpatialWorldIndex::GetSingleton().GetActorFromFormId(npcFormID);
        }
        if (target) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                player->MoveTo(target);
                MARAS_LOG_INFO("[PrismaUIDataBridge] Teleported Player to NPC {:08X}", npcFormID);
            }
        } else {
            MARAS_LOG_WARN("[PrismaUIDataBridge] Teleport Player to NPC {:08X} failed (ref not found)", npcFormID);
        }
    }

    void PrismaUIDataBridge::ProcessTeleportNpcToPlayer(const nlohmann::json& payload) {
        const RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        if (npcFormID == 0) {
            return;
        }
        auto* target = MARAS::ActorSvc::GetReference(npcFormID);
        if (!target) {
            target = MARAS::SpatialWorldIndex::GetSingleton().GetActorFromFormId(npcFormID);
        }
        if (target) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                target->MoveTo(player);
                MARAS_LOG_INFO("[PrismaUIDataBridge] Teleported NPC {:08X} to Player", npcFormID);
            }
        } else {
            MARAS_LOG_WARN("[PrismaUIDataBridge] Teleport NPC {:08X} to Player failed (ref not found)", npcFormID);
        }
    }

    void PrismaUIDataBridge::ProcessSetChildCritterBanned(const nlohmann::json& payload) {
        const RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        const bool banned = GetJsonBool(payload, "banned", false);
        if (childID == 0) {
            return;
        }
        MARAS::State::WorldStore::GetSingleton().Modify([childID, banned](MARAS::State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const MARAS::State::ChildId cid{ childID };
            if (auto* st = hh.FindChildStateMutable(cid)) {
                st->critterBanned = banned;
            } else {
                MARAS::State::ChildHouseholdRecord rec{};
                rec.childId = cid;
                rec.critterBanned = banned;
                hh.UpsertChildState(std::move(rec));
            }
        });
        MARAS_LOG_INFO("[PrismaUIDataBridge] SetChildCritterBanned {:08X} -> {}", childID, banned);
        SendNPCDetail(childID);
    }

    void PrismaUIDataBridge::ProcessSetChildPetBanned(const nlohmann::json& payload) {
        const RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        const bool banned = GetJsonBool(payload, "banned", false);
        if (childID == 0) {
            return;
        }
        MARAS::State::WorldStore::GetSingleton().Modify([childID, banned](MARAS::State::WorldState& ws) {
            auto& hh = ws.household.Write();
            const MARAS::State::ChildId cid{ childID };
            if (auto* st = hh.FindChildStateMutable(cid)) {
                st->petBanned = banned;
            } else {
                MARAS::State::ChildHouseholdRecord rec{};
                rec.childId = cid;
                rec.petBanned = banned;
                hh.UpsertChildState(std::move(rec));
            }
        });
        MARAS_LOG_INFO("[PrismaUIDataBridge] SetChildPetBanned {:08X} -> {}", childID, banned);
        SendNPCDetail(childID);
    }

    void PrismaUIDataBridge::ProcessDismissChildCritter(const nlohmann::json& payload) {
        const RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        if (childID == 0) {
            return;
        }
        auto& lineage = MARAS::App::LineageUseCases::GetSingleton();
        const RE::FormID critterID = lineage.GetCritter(childID);
        if (critterID != 0) {
            (void)MARAS::ChildSpawnService::GetSingleton().DisableAndDeleteActor(critterID);
            (void)lineage.RemoveCritter(childID);
            MARAS_LOG_INFO("[PrismaUIDataBridge] Dismissed critter {:08X} for child {:08X}", critterID, childID);
        }
        SendNPCDetail(childID);
    }

    void PrismaUIDataBridge::ProcessDismissChildPet(const nlohmann::json& payload) {
        const RE::FormID childID = StringToFormID(GetJsonString(payload, "formId"));
        if (childID == 0) {
            return;
        }
        auto& lineage = MARAS::App::LineageUseCases::GetSingleton();
        const RE::FormID petID = lineage.GetPet(childID);
        if (petID != 0) {
            (void)lineage.RemovePet(childID);
            MARAS_LOG_INFO("[PrismaUIDataBridge] Dismissed pet {:08X} for child {:08X} (retained in world)",
                           petID, childID);
        }
        SendNPCDetail(childID);
    }

    void PrismaUIDataBridge::ProcessAddChild(const nlohmann::json& payload) {
        RE::FormID actorA = StringToFormID(GetJsonString(payload, "actorA"));
        RE::FormID actorB = StringToFormID(GetJsonString(payload, "actorB"));
        if (actorA == 0) {
            actorA = StringToFormID(GetJsonString(payload, "guardianAId"));
        }
        if (actorB == 0) {
            actorB = StringToFormID(GetJsonString(payload, "guardianBId"));
        }
        const RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        const std::string houseStr = GetJsonString(payload, "houseId");
        const bool assignSpouseHome = GetJsonBool(payload, "assignSpouseHome", false);

        auto& household = MARAS::App::HouseholdUseCases::GetSingleton();
        RE::FormID explicitHouseID = 0;
        if (!houseStr.empty()) {
            explicitHouseID = StringToFormID(houseStr);
            if (explicitHouseID == 0) {
                explicitHouseID = household.FindHouseByName(houseStr);
            }
        }

        if (childID == 0 || (actorA == 0 && actorB == 0)) {
            return;
        }

        auto& lineage = MARAS::App::LineageUseCases::GetSingleton();
        std::string blockReason;
        if (lineage.IsChildBlockedFromAdoption(childID, &blockReason)) {
            MARAS_LOG_WARN("[PrismaUIDataBridge] ProcessAddChild blocked for child {:08X}: {}", childID, blockReason);
            return;
        }

        if (actorA == MARAS::Foundation::kPlayerRefRawId || actorB == MARAS::Foundation::kPlayerRefRawId) {
            const RE::FormID spouseID = (actorA == MARAS::Foundation::kPlayerRefRawId) ? actorB : actorA;
            auto* childActor = RE::TESForm::LookupByID<RE::Actor>(childID);
            if (!childActor) {
                MARAS_LOG_ERROR("[PrismaUIDataBridge] ProcessAddChild failed: child {:08X} Actor not found", childID);
                return;
            }
            MARAS_LOG_INFO("[PrismaUIDataBridge] Player adoption child {:08X} (spouse={:08X}, house={:08X}) -> AdoptChildGlobal",
                           childID, spouseID, explicitHouseID);

            MARAS::DispatchPostLock([childID, spouseID, explicitHouseID]() {
                auto* cActor = RE::TESForm::LookupByID<RE::Actor>(childID);
                auto* spActor = (spouseID != 0 && spouseID != MARAS::Foundation::kPlayerRefRawId)
                    ? RE::TESForm::LookupByID<RE::Actor>(spouseID)
                    : nullptr;
                auto* hForm = (explicitHouseID != 0) ? RE::TESForm::LookupByID(explicitHouseID) : nullptr;
                auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                if (!vm || !cActor) {
                    MARAS_LOG_ERROR("[PrismaUIDataBridge] VM or child {:08X} unavailable for AdoptChildGlobal", childID);
                    return;
                }
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> nullCallback;
                auto args = RE::MakeFunctionArguments(std::move(cActor), std::move(spActor), std::move(hForm));
                vm->DispatchStaticCall("TTM_ServiceChildrenLogic", "AdoptChildGlobal", args, nullCallback);
            }, "PrismaUIDataBridge::AdoptChildGlobal");
            return;
        }

        (void)lineage.RegisterChildActor(
            childID, actorA, actorB, explicitHouseID, 0, 0, 0,
            static_cast<std::uint8_t>(MARAS::Foundation::ChildStage::Child), true);
        MARAS_LOG_INFO("[PrismaUIDataBridge] Registered child {:08X} with guardians {:08X} & {:08X}",
                       childID, actorA, actorB);

        if (explicitHouseID != 0) {
            const auto relocated = household.RelocateFamilyUnit(childID, explicitHouseID, assignSpouseHome);
            MARAS_LOG_INFO("[PrismaUIDataBridge] Relocated family unit ({} members) for child {:08X} to house {:08X}",
                           relocated.size(), childID, explicitHouseID);
            for (const RE::FormID memberID : relocated) {
                SendNPCDetail(memberID);
            }
        }

        SendOverviewData();
        SendNPCDetail(childID);
    }

    void PrismaUIDataBridge::ProcessRemoveChild(const nlohmann::json& payload) {
        const RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID != 0) {
            (void)MARAS::App::LineageUseCases::GetSingleton().RemoveChild(childID);
            MARAS_LOG_INFO("[PrismaUIDataBridge] Removed child {:08X}", childID);
            SendOverviewData();
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessUpdateChild(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID == 0) {
            return;
        }

        auto& lineage = MARAS::App::LineageUseCases::GetSingleton();
        auto& paternity = MARAS::App::PaternityWorkflowCoordinator::GetSingleton();
        const auto worldSnap = MARAS::State::WorldStore::GetSingleton().Snapshot();
        const MARAS::State::ChildRecord* existing = worldSnap && worldSnap->lineage
            ? worldSnap->lineage->FindChild(MARAS::State::ChildId{ childID })
            : nullptr;
        const bool revealSecrets = worldSnap && worldSnap->settings
            ? worldSnap->settings->revealSecretsInUI
            : false;
        const bool hasHiddenPaternity =
            !revealSecrets && existing != nullptr && existing->paternitySecret.HasActiveSecret();

        bool changed = false;

        if (payload.contains("guardianAId") || payload.contains("guardianBId")) {
            const MARAS::State::GuardianSlots oldGuardians =
                existing ? existing->guardians : MARAS::State::GuardianSlots{};
            MARAS::State::ActorId targetA = payload.contains("guardianAId")
                ? MARAS::State::ActorId{ ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "guardianAId"))) }
                : oldGuardians.slotA;
            MARAS::State::ActorId targetB = payload.contains("guardianBId")
                ? MARAS::State::ActorId{ ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "guardianBId"))) }
                : oldGuardians.slotB;
            if (!targetA.IsNull() && targetA == targetB) {
                targetB = {};
            }

            if (!oldGuardians.slotA.IsNull() && oldGuardians.slotA != targetA && oldGuardians.slotA != targetB) {
                changed |= lineage.RemoveChildGuardian(childID, oldGuardians.slotA.raw());
            }
            if (!oldGuardians.slotB.IsNull() && oldGuardians.slotB != targetA && oldGuardians.slotB != targetB) {
                changed |= lineage.RemoveChildGuardian(childID, oldGuardians.slotB.raw());
            }
            if (!targetA.IsNull() && targetA != oldGuardians.slotA && targetA != oldGuardians.slotB) {
                changed |= lineage.AddChildGuardian(childID, targetA.raw());
            }
            if (!targetB.IsNull() && targetB != oldGuardians.slotA && targetB != oldGuardians.slotB) {
                changed |= lineage.AddChildGuardian(childID, targetB.raw());
            }
        }

        if (payload.contains("bioMotherId")) {
            const RE::FormID bioMID = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "bioMotherId")));
            changed |= lineage.SetBioMother(childID, bioMID);
        }

        const bool hasExplicitPresumedInPayload =
            payload.contains("presumedFatherId") && !GetJsonString(payload, "presumedFatherId").empty();
        if (payload.contains("bioFatherId")) {
            const RE::FormID bioFID = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "bioFatherId")));
            if (hasHiddenPaternity && existing && bioFID == existing->presumedFather.raw()) {
                MARAS_LOG_DEBUG("[PrismaUIDataBridge] Preserving hidden paternity for child {:08X} (UI echoed presumedFather {:08X})",
                                childID, bioFID);
            } else {
                changed |= lineage.SetBioFather(childID, bioFID);
                if (!revealSecrets && !hasExplicitPresumedInPayload &&
                    (!existing || bioFID != existing->bioFather.raw())) {
                    changed |= lineage.SetPresumedFather(childID, 0);
                    changed |= paternity.SetPaternitySecretEdges(childID, false, false);
                }
            }
        }

        if (payload.contains("presumedFatherId")) {
            if (hasHiddenPaternity && !hasExplicitPresumedInPayload) {
                MARAS_LOG_DEBUG("[PrismaUIDataBridge] Preserving masked presumedFatherId for child {:08X}", childID);
            } else {
                const RE::FormID presFID = ResolveToPlacedRefID(
                    StringToFormID(GetJsonString(payload, "presumedFatherId")));
                changed |= lineage.SetPresumedFather(childID, presFID);
            }
        }

        if (payload.contains("hasBeastblood")) {
            const bool reqBeast = GetJsonBool(payload, "hasBeastblood", false);
            if (hasHiddenPaternity && existing && existing->hasBeastblood && !reqBeast) {
                MARAS_LOG_DEBUG("[PrismaUIDataBridge] Preserving masked hasBeastblood for child {:08X}", childID);
            } else {
                changed |= lineage.SetChildBeastblood(childID, reqBeast);
            }
        }

        if (changed) {
            MARAS_LOG_INFO("[PrismaUIDataBridge] Updated child {:08X} parent/guardian assignments", childID);
            SendOverviewData();
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessSpawnChild(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID != 0) {
            MARAS_LOG_INFO("[PrismaUIDataBridge] Spawning synthetic child {:08X}", childID);
            std::string childIdStr = FormIDToString(childID);
            const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(childID));
            MARAS::EmitModCallbackEvent("maras_spawn_synthetic_child", std::move(childIdStr), childIdFloat, RE::FormID{ 0 });
        }
    }

    void PrismaUIDataBridge::ProcessSendChildToTraining(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        const std::int32_t classIdx = GetJsonInt(payload, "trainingClassIndex", 1);
        const auto& catalog = MARAS::Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
        if (childID == 0 || !catalog.IsValidClassIndex(classIdx)) {
            MARAS_LOG_WARN("[PrismaUIDataBridge] ProcessSendChildToTraining rejected (childID={:08X}, classIdx={})",
                           childID, classIdx);
            return;
        }
        auto& lineage = MARAS::App::LineageUseCases::GetSingleton();
        MARAS_LOG_INFO("[PrismaUIDataBridge] ProcessSendChildToTraining child {:08X} -> classIdx {} ('{}')",
                       childID, classIdx, lineage.GetTrainingClassName(classIdx));
        std::string failReason;
        if (!lineage.SendChildToTraining(childID, classIdx, &failReason)) {
            MARAS_LOG_WARN("[PrismaUIDataBridge] SendChildToTraining failed for child {:08X} -> classIdx {}: {}",
                           childID, classIdx, failReason.empty() ? "unknown reason" : failReason);
        }
    }

    void PrismaUIDataBridge::ProcessCompleteChildTraining(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID == 0) {
            MARAS_LOG_WARN("[PrismaUIDataBridge] ProcessCompleteChildTraining rejected payload with childID=0");
            return;
        }
        MARAS_LOG_INFO("[PrismaUIDataBridge] ProcessCompleteChildTraining child {:08X}", childID);
        if (!MARAS::App::LineageUseCases::GetSingleton().CompleteChildTraining(childID)) {
            MARAS_LOG_WARN("[PrismaUIDataBridge] CompleteChildTraining returned false for child {:08X}", childID);
        }
    }

    void PrismaUIDataBridge::ProcessRelocateFamilyUnit(const nlohmann::json& payload) {
        RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "npcId"));
        if (npcFormID == 0) {
            npcFormID = StringToFormID(GetJsonString(payload, "childId"));
        }
        if (npcFormID == 0) {
            npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        }
        std::string houseStr = GetJsonString(payload, "targetHouseId");
        if (houseStr.empty()) {
            houseStr = GetJsonString(payload, "home");
        }
        const bool moveParents = GetJsonBool(payload, "moveParents", true);

        auto& household = MARAS::App::HouseholdUseCases::GetSingleton();
        RE::FormID explicitHouseID = 0;
        if (!houseStr.empty()) {
            explicitHouseID = StringToFormID(houseStr);
            if (explicitHouseID == 0) {
                explicitHouseID = household.FindHouseByName(houseStr);
            }
        }

        if (npcFormID != 0 && explicitHouseID != 0) {
            const auto relocated = household.RelocateFamilyUnit(npcFormID, explicitHouseID, moveParents);
            MARAS_LOG_INFO("[PrismaUIDataBridge] Relocated family unit ({} members) for NPC {:08X} to house {:08X}",
                           relocated.size(), npcFormID, explicitHouseID);
            SendOverviewData();
            for (const RE::FormID memberID : relocated) {
                SendNPCDetail(memberID);
            }
        }
    }

    void PrismaUIDataBridge::ProcessSetPaternitySecretEdges(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID == 0) {
            return;
        }
        const bool secretBio = GetJsonBool(payload, "isSecretForBioFather", false);
        const bool secretPartner = GetJsonBool(payload, "isSecretForPartner", false);
        std::optional<bool> pregSecretPartner = std::nullopt;
        if (payload.contains("isPregnancySecretForPartner") && payload["isPregnancySecretForPartner"].is_boolean()) {
            pregSecretPartner = payload["isPregnancySecretForPartner"].get<bool>();
        }

        (void)MARAS::App::PaternityWorkflowCoordinator::GetSingleton().SetPaternitySecretEdges(
            childID, secretBio, secretPartner, pregSecretPartner);
        MARAS_LOG_INFO("[PrismaUIDataBridge] SetPaternitySecretEdges child {:08X} -> secretBio={}, secretPartner={}",
                       childID, secretBio, secretPartner);
        SendOverviewData();
        SendNPCDetail(childID);
    }

    void PrismaUIDataBridge::ProcessEvaluateChildSecret(const nlohmann::json& payload) {
        RE::FormID childID = StringToFormID(GetJsonString(payload, "childId"));
        if (childID == 0) {
            childID = StringToFormID(GetJsonString(payload, "formId"));
        }
        if (childID == 0) {
            return;
        }
        MARAS_LOG_INFO("[PrismaUIDataBridge] ProcessEvaluateChildSecret child {:08X}", childID);
        if (!MARAS::App::PaternityWorkflowCoordinator::GetSingleton().EvaluatePaternitySecret(childID)) {
            MARAS_LOG_WARN("[PrismaUIDataBridge] EvaluatePaternitySecret did not trigger for child {:08X}", childID);
            SendOverviewData();
            SendNPCDetail(childID);
        }
    }

    void PrismaUIDataBridge::ProcessSetNPCCurrentHome(const nlohmann::json& payload) {
        const RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        const std::string homeStr = GetJsonString(payload, "home");
        if (npcFormID == 0) {
            return;
        }

        auto& household = MARAS::App::HouseholdUseCases::GetSingleton();
        auto& relUses = MARAS::App::RelationshipUseCases::GetSingleton();

        if (homeStr.empty() || homeStr == "Unset") {
            if (!household.CanRemoveTenant(npcFormID)) {
                MARAS_LOG_WARN("[PrismaUIDataBridge] Cannot remove tenant {:08X} from player house due to family ties",
                               npcFormID);
                SendOverviewData();
                SendNPCDetail(npcFormID);
                return;
            }
            if (household.RemoveTenantFromPlayerHouse(npcFormID)) {
                MARAS_LOG_INFO("[PrismaUIDataBridge] Removed tenant {:08X} from player house", npcFormID);
                if (relUses.IsChildNPC(npcFormID)) {
                    (void)MARAS::App::LineageUseCases::GetSingleton().OnChildRemovedFromHousehold(npcFormID);
                }
            } else {
                MARAS_LOG_WARN("[PrismaUIDataBridge] Failed to remove tenant {:08X} from player house", npcFormID);
            }
        } else {
            RE::FormID targetHouseID = 0;
            if (homeStr.length() == 8u) {
                targetHouseID = StringToFormID(homeStr);
            }
            if (targetHouseID == 0) {
                targetHouseID = household.FindHouseByName(homeStr);
            }
            if (targetHouseID != 0) {
                const auto relocated = household.RelocateFamilyUnit(npcFormID, targetHouseID, true);
                MARAS_LOG_INFO("[PrismaUIDataBridge] SetNPCCurrentHome relocated {} family members for {:08X} to house {:08X}",
                               relocated.size(), npcFormID, targetHouseID);
                SendOverviewData();
                SendNPCDetail(npcFormID);
                for (const RE::FormID memberID : relocated) {
                    SendNPCDetail(memberID);
                }
                return;
            }
            MARAS_LOG_WARN("[PrismaUIDataBridge] Player house '{}' not found for tenant {:08X}", homeStr, npcFormID);
        }

        SendOverviewData();
        SendNPCDetail(npcFormID);
    }

    void PrismaUIDataBridge::ProcessSetNPCStayAtHome(const nlohmann::json& payload) {
        const RE::FormID npcFormID = StringToFormID(GetJsonString(payload, "formId"));
        const bool stayAtHome = GetJsonBool(payload, "stayAtHome", true);
        if (npcFormID == 0) {
            return;
        }
        MARAS::App::HouseholdUseCases::GetSingleton().SetTenantStayAtHome(npcFormID, stayAtHome);
        MARAS_LOG_INFO("[PrismaUIDataBridge] SetNPCStayAtHome tenant {:08X} stayAtHome={}", npcFormID, stayAtHome);
        SendOverviewData();
        SendNPCDetail(npcFormID);
    }

    void PrismaUIDataBridge::ProcessSkyrimNetMatchSuggest(const nlohmann::json& payload) {
        const std::string formStr = GetJsonString(payload, "formId");
        auto& agency = MARAS::App::AgencyWorkflowCoordinator::GetSingleton();
        if (formStr.empty() || formStr == "global" || formStr == "all" || formStr == "any") {
            (void)agency.RunGlobalMatchmaking();
            return;
        }
        const RE::FormID targetId = ResolveToPlacedRefID(StringToFormID(formStr));
        if (targetId != 0) {
            (void)agency.RunManualMatchmaking({ targetId });
        } else {
            (void)agency.RunGlobalMatchmaking();
        }
    }

    void PrismaUIDataBridge::ProcessSkyrimNetGlobalMatchSuggest(const nlohmann::json& /*payload*/) {
        (void)MARAS::App::AgencyWorkflowCoordinator::GetSingleton().RunGlobalMatchmaking();
    }

    void PrismaUIDataBridge::ProcessGenerateMatchMemories(const nlohmann::json& payload) {
        if (!MARAS::App::GetSkyrimNetGateway().IsAvailable()) {
            MARAS_LOG_DEBUG("[PrismaUIDataBridge] generateMatchMemories skipped: SkyrimNet unavailable");
            return;
        }
        const RE::FormID targetId = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "targetFormId")));
        const RE::FormID partnerId = ResolveToPlacedRefID(StringToFormID(GetJsonString(payload, "partnerFormId")));
        const std::string targetStatus = GetJsonString(payload, "targetStatus", "dating");
        const std::string action = GetJsonString(payload, "action", "new");
        if (targetId != 0 && partnerId != 0) {
            MARAS::App::GetSkyrimNetGateway().PreviewRelationshipMemories(
                targetId, partnerId, targetStatus, action);
        }
    }

    void PrismaUIDataBridge::ProcessDumpData(const nlohmann::json& /*payload*/) {
        try {
            nlohmann::json root;
            const auto now = std::chrono::system_clock::now();
            const auto inTimeT = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &inTimeT);

            std::ostringstream ssTime;
            std::ostringstream ssFile;
            ssTime << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            ssFile << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
            const std::string timeStr = ssTime.str();
            const std::string fileTimeStr = ssFile.str();

            const auto view = MakeDashboardView();
            const bool revealSecrets = view.Settings().revealSecretsInUI;

            nlohmann::json meta;
            meta["dumpTimestamp"] = timeStr;
            meta["serializationVersion"] = MARAS::State::CoSaveConstants::kDataVersion;
            meta["hasSkyrimNet"] = MARAS::App::GetSkyrimNetGateway().IsAvailable();
            meta["simulateNoSkyrimNet"] = view.Settings().simulateNoSkyrimNet;
            meta["secretsMasked"] = !revealSecrets;
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                meta["gameDaysPassed"] = calendar->GetDaysPassed();
                meta["gameYear"] = calendar->GetYear();
                meta["gameMonth"] = calendar->GetMonth();
                meta["gameDay"] = calendar->GetDay();
                meta["gameHour"] = calendar->GetHour();
            }
            root["meta"] = std::move(meta);

            const auto& relUses = MARAS::App::RelationshipUseCases::GetSingleton();
            const auto& household = MARAS::App::HouseholdUseCases::GetSingleton();
            const auto& lineage = MARAS::App::LineageUseCases::GetSingleton();

            root["settings"] = MARAS::App::SettingsCoordinator::GetSingleton().DumpJson();
            root["spouseHierarchy"] = relUses.DumpHierarchyJson();
            root["relationshipManager"] = relUses.DumpJson(view);
            root["affectionService"] = relUses.DumpAffectionJson();
            root["playerHouses"] = household.DumpHousesJson();
            root["spouseAssets"] = household.DumpSharedAssetsJson();
            root["playerChildrenService"] = lineage.DumpPlayerChildrenJson();
            root["matchmakingService"] = BuildMatchmakingDumpJson(view);
            root["deceasedRegistry"] = lineage.DumpDeceasedJson(view);
            root["banterCoordinator"] = BuildBanterDumpJson(view);
            root["adultChildTravel"] = household.DumpTravelJson(view);

            const std::filesystem::path dumpsDir = "Data/SKSE/Plugins/MARAS/dumps";
            std::error_code ec;
            std::filesystem::create_directories(dumpsDir, ec);

            const std::string fileName = "maras_dump_" + fileTimeStr + ".json";
            const std::filesystem::path fullPath = dumpsDir / fileName;
            std::ofstream outFile(fullPath);
            if (outFile.is_open()) {
                outFile << DumpJsonSafe(root, 4);
                outFile.close();

                const std::string absolutePathStr = std::filesystem::absolute(fullPath).string();
                MARAS_LOG_INFO("[PrismaUIDataBridge] Dumped MARAS stored data to '{}'", absolutePathStr);
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] Stored data successfully dumped to %s", fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = true;
                res["fileName"] = fileName;
                res["filePath"] = fullPath.string();
                res["absolutePath"] = absolutePathStr;
                res["timestamp"] = timeStr;
                PrismaUIManager::GetSingleton().SendToUI("onDataDumpCompleted", DumpJsonSafe(res));
            } else {
                MARAS_LOG_ERROR("[PrismaUIDataBridge] Failed to create dump file '{}'", fullPath.string());
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] Failed to dump data to %s", fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = false;
                res["error"] = "Failed to create dump file at " + fullPath.string();
                PrismaUIManager::GetSingleton().SendToUI("onDataDumpCompleted", DumpJsonSafe(res));
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("[PrismaUIDataBridge] Exception while dumping data: {}", e.what());
            nlohmann::json res;
            res["success"] = false;
            res["error"] = std::string("Exception: ") + e.what();
            PrismaUIManager::GetSingleton().SendToUI("onDataDumpCompleted", DumpJsonSafe(res));
        }
    }

    void PrismaUIDataBridge::ProcessDumpNPCIndex(const nlohmann::json& /*payload*/) {
        try {
            const auto root = MARAS::SpatialWorldIndex::GetSingleton().DumpIndexJson();
            const nlohmann::json metaObj = (root.is_object() && root.contains("meta") && root["meta"].is_object())
                ? root["meta"]
                : nlohmann::json::object();
            const std::size_t totalNPCs = static_cast<std::size_t>(std::max(0, GetJsonInt(metaObj, "totalNPCs", 0)));
            const std::string timeStr = GetJsonString(metaObj, "dumpTimestamp");

            const auto now = std::chrono::system_clock::now();
            const auto inTimeT = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &inTimeT);
            std::ostringstream ssFile;
            ssFile << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
            const std::string fileTimeStr = ssFile.str();

            const std::filesystem::path dumpsDir = "Data/SKSE/Plugins/MARAS/dumps";
            std::error_code ec;
            std::filesystem::create_directories(dumpsDir, ec);

            const std::string fileName = "maras_npc_index_" + fileTimeStr + ".json";
            const std::filesystem::path fullPath = dumpsDir / fileName;
            std::ofstream outFile(fullPath);
            if (outFile.is_open()) {
                outFile << DumpJsonSafe(root, 4);
                outFile.close();

                const std::string absolutePathStr = std::filesystem::absolute(fullPath).string();
                MARAS_LOG_INFO("[PrismaUIDataBridge] Dumped NPC Index ({} NPCs) to '{}'", totalNPCs, absolutePathStr);
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] NPC Index successfully dumped (%zu NPCs) to %s",
                                   totalNPCs, fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = true;
                res["fileName"] = fileName;
                res["filePath"] = fullPath.string();
                res["absolutePath"] = absolutePathStr;
                res["totalNPCs"] = totalNPCs;
                res["timestamp"] = timeStr;
                PrismaUIManager::GetSingleton().SendToUI("onNPCIndexDumpCompleted", DumpJsonSafe(res));
            } else {
                MARAS_LOG_ERROR("[PrismaUIDataBridge] Failed to create NPC Index dump '{}'", fullPath.string());
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("[MARAS] Failed to dump NPC Index to %s", fileName.c_str());
                }

                nlohmann::json res;
                res["success"] = false;
                res["error"] = "Failed to create dump file at " + fullPath.string();
                PrismaUIManager::GetSingleton().SendToUI("onNPCIndexDumpCompleted", DumpJsonSafe(res));
            }
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("[PrismaUIDataBridge] Exception while dumping NPC Index: {}", e.what());
            nlohmann::json res;
            res["success"] = false;
            res["error"] = std::string("Exception: ") + e.what();
            PrismaUIManager::GetSingleton().SendToUI("onNPCIndexDumpCompleted", DumpJsonSafe(res));
        }
    }

}  // namespace MARAS::Bridge::Ui
