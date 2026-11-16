#include "maras/bridge_skyrimnet/SkyrimNetApiLoader.h"

#include "maras/app/AppContext.h"
#include "maras/bridge_skyrimnet/SkyrimNetDecisions.h"
#include "maras/bridge_skyrimnet/SkyrimNetDecorators.h"
#include "maras/bridge_skyrimnet/SkyrimNetEventService.h"
#include "maras/bridge_skyrimnet/SkyrimNetMemoryService.h"
#include "maras/bridge_skyrimnet/SkyrimNetProfileService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <PublicAPI.h>
#include <algorithm>
#include <exception>
#include <mutex>

namespace MARAS::Bridge::SkyrimNet {

    namespace {

        bool EnsureSkyrimNetApiResolved() {
            if (::GetModuleHandleW(L"SkyrimNet.dll") == nullptr) {
                return false;
            }
            static std::once_flag s_initFlag;
            static bool s_resolved = false;
            std::call_once(s_initFlag, []() {
                s_resolved = FindFunctions();
                if (s_resolved && PublicGetVersion) {
                    MARAS_LOG_INFO("[SkyrimNet] Resolved SkyrimNet Public API v{}", PublicGetVersion());
                } else {
                    MARAS_LOG_WARN("[SkyrimNet] Failed to resolve SkyrimNet Public API via FindFunctions()");
                }
            });
            return s_resolved;
        }

        std::string GetModelSlotDisplayName(std::string_view llmVariant) {
            if (llmVariant == "memory") {
                return MARAS::Tr("modal.llm_guidance.slot_memory");
            }
            if (llmVariant == "decisions") {
                return MARAS::Tr("modal.llm_guidance.slot_decisions");
            }
            return MARAS::Tr("modal.llm_guidance.slot_gamemaster");
        }

    }  // namespace

    bool IsSkyrimNetAvailable() {
        if (MARAS::State::WorldStore::GetSingleton().Snapshot()->settings->simulateNoSkyrimNet) {
            return false;
        }
        return ::GetModuleHandleW(L"SkyrimNet.dll") != nullptr;
    }

    bool IsDecisionsExportAvailable() {
        if (!IsSkyrimNetAvailable() || !EnsureSkyrimNetApiResolved()) {
            return false;
        }
        return PublicSendCustomDecisionToLLM != nullptr;
    }

    bool ShouldTriggerSkyrimNetNarration(RE::Actor* speaker, RE::Actor* target) {
        if (!IsSkyrimNetAvailable() || !speaker) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        float radius = MARAS::State::WorldStore::GetSingleton().Snapshot()->settings->skyrimnetActionRadius;
        if (radius <= 0.0f) {
            radius = 2000.0f;
        }
        const float radiusSq = radius * radius;
        const auto playerPos = player->GetPosition();
        auto* playerCell = player->GetParentCell();
        if (!playerCell) {
            return false;
        }

        auto isActorNearPlayer = [&](RE::Actor* actor) -> bool {
            if (!actor) return false;
            if (actor == player) return true;
            if (!actor->Is3DLoaded() || actor->IsDead() || actor->IsDisabled() || actor->IsDeleted()) {
                return false;
            }
            auto* actorCell = actor->GetParentCell();
            if (!actorCell) return false;
            if (playerCell->IsInteriorCell() || actorCell->IsInteriorCell()) {
                if (playerCell != actorCell) return false;
            } else {
                if (player->GetWorldspace() != actor->GetWorldspace()) return false;
            }
            if (playerPos.GetSquaredDistance(actor->GetPosition()) > radiusSq) {
                return false;
            }
            return true;
        };

        if (!isActorNearPlayer(speaker)) {
            return false;
        }

        if (target && target != speaker) {
            if (!isActorNearPlayer(target)) {
                return false;
            }
        }

        // If only the Player was passed as speaker/target, require at least one nearby NPC to be present
        if (speaker == player && (!target || target == player)) {
            bool hasNearbyNpc = false;
            const auto spatialSnap = MARAS::State::SpatialSnapshotStore::GetSingleton().Snapshot();
            spatialSnap->ForEachNearbyCandidate(MARAS::State::RefId{ MARAS::kPlayerFormID }, radius,
                [&](const MARAS::State::LoadedActorSample&) {
                    hasNearbyNpc = true;
                });
            if (!hasNearbyNpc) {
                return false;
            }
        }

        return true;
    }

    bool RegisterSkyrimNetEvent(const std::string& eventType,
                                const std::string& content,
                                std::uint32_t originatorFormID,
                                std::uint32_t targetFormID,
                                bool exactAudience,
                                const std::vector<std::uint32_t>& audienceFormIDs) {
        if (!IsSkyrimNetAvailable()) {
            return false;
        }
        if (eventType.empty() || content.empty()) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: empty eventType or content");
            return false;
        }

        if (!EnsureSkyrimNetApiResolved()) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: SkyrimNet.dll not found in process");
            return false;
        }
        if (!PublicRegisterEvent) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: PublicRegisterEvent export not found in SkyrimNet.dll");
            return false;
        }

        if (MARAS::IsSyntheticChildID(originatorFormID)) {
            originatorFormID = 0;
        }
        if (MARAS::IsSyntheticChildID(targetFormID)) {
            targetFormID = 0;
        }

        std::vector<std::uint32_t> exactAudienceIds;
        auto addUniqueValidActor = [&](std::uint32_t id) {
            if (id == 0 || MARAS::IsSyntheticChildID(id)) return;
            if (std::find(exactAudienceIds.begin(), exactAudienceIds.end(), id) == exactAudienceIds.end()) {
                exactAudienceIds.push_back(id);
            }
        };

        for (std::uint32_t id : audienceFormIDs) {
            addUniqueValidActor(id);
        }

        if (exactAudience || !exactAudienceIds.empty()) {
            addUniqueValidActor(originatorFormID);
            addUniqueValidActor(targetFormID);

            if (exactAudienceIds.empty()) {
                MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent('{}'): exactAudience requested but no valid actors provided; skipping to avoid nearby broadcast", eventType);
                return false;
            }
        }

        const std::uint32_t* audiencePtr = exactAudienceIds.empty() ? nullptr : exactAudienceIds.data();
        const std::uint32_t audienceCount = static_cast<std::uint32_t>(exactAudienceIds.size());

        const int eventId = PublicRegisterEvent(
            eventType.c_str(),
            content.c_str(),
            originatorFormID,
            targetFormID,
            audiencePtr,
            audienceCount);
        const bool ok = (eventId > 0);

        MARAS_LOG_DEBUG("[SkyrimNet] RegisterSkyrimNetEvent('{}', orig=0x{:08X}, target=0x{:08X}, exactAudience={}, audienceCount={}) -> {} (id={})",
                        eventType, originatorFormID, targetFormID, exactAudience || audienceCount > 0, audienceCount, ok, eventId);
        return ok;
    }

    bool RegisterDecoratorExport(const char* name,
                                 const char* description,
                                 std::function<std::string(RE::Actor*)> callback) {
        if (!name || name[0] == '\0') {
            return false;
        }
        if (!EnsureSkyrimNetApiResolved()) {
            MARAS_LOG_WARN("[SkyrimNet] SkyrimNet.dll not found in process - decorator '{}' skipped.", name);
            return false;
        }
        if (!PublicRegisterDecorator) {
            MARAS_LOG_WARN("[SkyrimNet] PublicRegisterDecorator export not found in SkyrimNet.dll.");
            return false;
        }

        std::string decName = name;
        auto wrappedCallback = [decName, cb = std::move(callback)](RE::Actor* actor) -> std::string {
            const std::uint32_t actorFormId = actor ? actor->GetFormID() : 0u;
            MARAS_LOG_TRACE("[SkyrimNet] Invoking decorator '{}' for actor formId 0x{:08X}.", decName, actorFormId);
            try {
                std::string result = cb ? cb(actor) : std::string{};
                MARAS_LOG_TRACE("[SkyrimNet] Decorator '{}' returned for actor formId 0x{:08X}: '{}'.", decName, actorFormId, result);
                return result;
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("[SkyrimNet] Exception in decorator '{}' for actor 0x{:08X}: {}", decName, actorFormId, e.what());
                return "";
            } catch (...) {
                MARAS_LOG_ERROR("[SkyrimNet] Unknown exception in decorator '{}' for actor 0x{:08X}", decName, actorFormId);
                return "";
            }
        };

        const char* safeDesc = description ? description : "";
        if (PublicRegisterDecorator(name, safeDesc, std::move(wrappedCallback))) {
            MARAS_LOG_INFO("[SkyrimNet] Registered decorator '{}'", name);
            return true;
        }
        MARAS_LOG_WARN("[SkyrimNet] Failed to register decorator '{}' (duplicate or invalid name?)", name);
        return false;
    }

    std::uint64_t GetActorUUID(std::uint32_t formID) {
        if (!IsSkyrimNetAvailable() || formID == 0 ||
            formID == MARAS::kUnknownFatherFormID || MARAS::IsSyntheticChildID(formID)) {
            return 0;
        }
        if (!EnsureSkyrimNetApiResolved() || !PublicFormIDToUUID) {
            return 0;
        }
        return PublicFormIDToUUID(formID);
    }

    bool IsActorSuccessionAvailable() {
        if (!IsSkyrimNetAvailable() || !EnsureSkyrimNetApiResolved()) {
            return false;
        }
        return PublicRecordActorSuccessionByUUID != nullptr;
    }

    bool RecordActorSuccession(std::uint32_t fromFormID, std::uint32_t toFormID) {
        if (!IsSkyrimNetAvailable() || fromFormID == 0 || toFormID == 0 || fromFormID == toFormID ||
            fromFormID == MARAS::kPlayerFormID || toFormID == MARAS::kPlayerFormID ||
            MARAS::IsSyntheticChildID(fromFormID) || MARAS::IsSyntheticChildID(toFormID)) {
            return false;
        }
        if (!EnsureSkyrimNetApiResolved() || !PublicRecordActorSuccession) {
            return false;
        }
        const bool ok = PublicRecordActorSuccession(fromFormID, toFormID);
        MARAS_LOG_INFO("[SkyrimNet] RecordActorSuccession(0x{:08X} -> 0x{:08X}) -> {}", fromFormID, toFormID, ok);
        return ok;
    }

    bool RecordActorSuccessionByUUID(std::uint64_t fromUuid, std::uint64_t toUuid) {
        if (!IsSkyrimNetAvailable() || fromUuid == 0 || toUuid == 0 || fromUuid == toUuid) {
            return false;
        }
        if (!EnsureSkyrimNetApiResolved() || !PublicRecordActorSuccessionByUUID) {
            return false;
        }
        const bool ok = PublicRecordActorSuccessionByUUID(fromUuid, toUuid);
        MARAS_LOG_INFO("[SkyrimNet] RecordActorSuccessionByUUID({:016X} -> {:016X}) -> {}", fromUuid, toUuid, ok);
        return ok;
    }

    std::string GetPluginConfigValueExport(const char* plugin,
                                           const char* path,
                                           const char* defaultValue) {
        const std::string fallback = defaultValue ? defaultValue : "";
        if (!plugin || !path || !IsSkyrimNetAvailable()) {
            return fallback;
        }
        if (!EnsureSkyrimNetApiResolved() || !PublicGetPluginConfigValue) {
            return fallback;
        }
        return PublicGetPluginConfigValue(plugin, path, fallback.c_str());
    }

    bool SendCustomPromptToLLM(const std::string& promptName,
                               const std::string& variant,
                               const std::string& contextJson,
                               std::function<void(const char* response, int success)> callback) {
        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: SkyrimNet is not available or simulated as disabled");
            return false;
        }

        if (!EnsureSkyrimNetApiResolved()) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: SkyrimNet.dll not found in process");
            return false;
        }

        if (!PublicSendCustomPromptToLLM) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: PublicSendCustomPromptToLLM export not found in SkyrimNet.dll");
            return false;
        }

        return PublicSendCustomPromptToLLM(promptName.c_str(), variant.c_str(), contextJson.c_str(), std::move(callback));
    }

    bool SendCustomDecisionToLLM(const std::string& templateName,
                                 const std::string& contextJson,
                                 int maxProbabilities,
                                 std::function<void(const char* answer, const char* resultJson, int success)> callback) {
        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: SkyrimNet is not available or simulated as disabled");
            return false;
        }

        if (!EnsureSkyrimNetApiResolved()) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: SkyrimNet.dll not found in process");
            return false;
        }

        if (!PublicSendCustomDecisionToLLM) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: PublicSendCustomDecisionToLLM export not found in SkyrimNet.dll");
            return false;
        }

        return PublicSendCustomDecisionToLLM(templateName.c_str(), contextJson.c_str(), maxProbabilities, std::move(callback));
    }

    bool AddActorMemory(std::uint32_t actorFormID,
                        const std::string& content,
                        float importance,
                        const std::string& memoryType,
                        const std::string& emotion,
                        const std::string& location,
                        const std::vector<std::string>& tags,
                        const std::vector<std::uint32_t>& relatedActorFormIDs) {
        if (!IsSkyrimNetAvailable()) {
            return false;
        }
        if (actorFormID == 0 || MARAS::IsSyntheticChildID(actorFormID) || content.empty()) {
            return false;
        }

        if (!EnsureSkyrimNetApiResolved() || !PublicAddMemory) {
            MARAS_LOG_WARN("[SkyrimNet] AddActorMemory: PublicAddMemory export not found in SkyrimNet.dll");
            return false;
        }

        nlohmann::json tagsJson = tags.empty() ? nlohmann::json::array() : nlohmann::json(tags);
        const std::string tagsStr = tagsJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        nlohmann::json relatedJson = nlohmann::json::array();
        for (std::uint32_t relID : relatedActorFormIDs) {
            if (relID != 0 && relID != actorFormID && !MARAS::IsSyntheticChildID(relID)) {
                relatedJson.push_back(relID);
            }
        }
        const std::string relatedStr = relatedJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        const int memId = PublicAddMemory(
            actorFormID,
            content.c_str(),
            importance,
            memoryType.empty() ? "RELATIONSHIP" : memoryType.c_str(),
            emotion.empty() ? "content" : emotion.c_str(),
            location.empty() ? "Skyrim" : location.c_str(),
            tagsStr.c_str(),
            relatedStr.c_str());

        if (memId > 0) {
            MARAS_LOG_INFO("[SkyrimNet] Successfully added memory ID {} for actor 0x{:08X}", memId, actorFormID);
            return true;
        }

        MARAS_LOG_WARN("[SkyrimNet] Failed to add memory for actor 0x{:08X}", actorFormID);
        return false;
    }

    bool RequestProfileUpdateExport(std::uint32_t formID,
                                    const std::string& additionalInstructions,
                                    const std::string& updatableBlocksCSV) {
        if (!IsSkyrimNetAvailable() || formID == 0 || MARAS::IsSyntheticChildID(formID)) {
            return false;
        }
        if (!EnsureSkyrimNetApiResolved() || !PublicRequestProfileUpdate) {
            return false;
        }
        const char* instrPtr = additionalInstructions.empty() ? nullptr : additionalInstructions.c_str();
        const char* blocksPtr = updatableBlocksCSV.empty() ? nullptr : updatableBlocksCSV.c_str();
        return PublicRequestProfileUpdate(formID, instrPtr, blocksPtr);
    }

    std::string GetBioTemplateNameExport(std::uint32_t formID) {
        if (formID == 0 || formID == MARAS::kUnknownFatherFormID || MARAS::IsSyntheticChildID(formID)) {
            return "";
        }
        if (formID == MARAS::kPlayerFormID) {
            return "player_special";
        }
        if (!IsSkyrimNetAvailable() || !EnsureSkyrimNetApiResolved() || !PublicGetBioTemplateName) {
            return "";
        }
        return PublicGetBioTemplateName(formID);
    }

    std::string GetSaveUniqueIDExport() {
        if (!IsSkyrimNetAvailable() || !EnsureSkyrimNetApiResolved() || !PublicGetSaveUniqueID) {
            return "";
        }
        return PublicGetSaveUniqueID();
    }

    std::vector<std::string> QueryActorMemoriesExport(std::uint32_t formID,
                                                      int maxCount,
                                                      const char* contextQuery) {
        std::vector<std::string> memories;
        if (formID == 0 || formID == MARAS::kUnknownFatherFormID ||
            MARAS::IsSyntheticChildID(formID) || !IsSkyrimNetAvailable()) {
            return memories;
        }

        if (!EnsureSkyrimNetApiResolved() || !PublicGetMemoriesForActor) {
            return memories;
        }

        const std::string memJsonStr = PublicGetMemoriesForActor(formID, maxCount, contextQuery ? contextQuery : "");
        if (memJsonStr.empty() || memJsonStr == "[]") {
            return memories;
        }

        const auto memArr = nlohmann::json::parse(memJsonStr, nullptr, false);
        if (!memArr.is_array()) {
            return memories;
        }

        for (const auto& m : memArr) {
            if (m.is_object() && m.contains("content") && m["content"].is_string()) {
                std::string content = m["content"].get<std::string>();
                if (!content.empty()) {
                    memories.push_back(std::move(content));
                }
            }
        }
        return memories;
    }

    std::string GetLLMFailureGuidanceTitle(MARAS::App::LLMFailureType failureType) {
        switch (failureType) {
            case MARAS::App::LLMFailureType::InvalidJson:
                return MARAS::Tr("modal.llm_guidance.title_invalid_json");
            case MARAS::App::LLMFailureType::RequestFailed:
            default:
                return MARAS::Tr("modal.llm_guidance.title_request_failed");
        }
    }

    std::string GetLLMFailureGuidance(MARAS::App::LLMFailureType failureType, std::string_view llmVariant) {
        const std::string slotName = GetModelSlotDisplayName(llmVariant);
        if (failureType == MARAS::App::LLMFailureType::InvalidJson) {
            return MARAS::TrFmt("modal.llm_guidance.body_invalid_json", { { "slot", slotName } });
        }

        return MARAS::TrFmt("modal.llm_guidance.body_request_failed", { { "slot", slotName } });
    }

    std::string GetLLMBatchFailureGuidance(bool hasRequestFailure,
                                            bool hasInvalidJson,
                                            std::string_view llmVariant) {
        if (hasRequestFailure && hasInvalidJson) {
            const std::string slotName = GetModelSlotDisplayName(llmVariant);
            return MARAS::TrFmt("modal.llm_guidance.body_mixed_batch", { { "slot", slotName } });
        }
        if (hasInvalidJson) {
            return GetLLMFailureGuidance(MARAS::App::LLMFailureType::InvalidJson, llmVariant);
        }
        return GetLLMFailureGuidance(MARAS::App::LLMFailureType::RequestFailed, llmVariant);
    }

    namespace {

        class SkyrimNetGatewayImpl final : public MARAS::App::ISkyrimNetGateway {
        public:
            bool IsAvailable() const override {
                return MARAS::Bridge::SkyrimNet::IsSkyrimNetAvailable();
            }

            bool ShouldUseDecisions() const override {
                return MARAS::Bridge::SkyrimNet::Decisions::ShouldUseDecisions();
            }

            bool ShouldTriggerNarration(RE::Actor* speaker, RE::Actor* target = nullptr) const override {
                return MARAS::Bridge::SkyrimNet::ShouldTriggerSkyrimNetNarration(speaker, target);
            }

            bool RegisterEvent(const std::string& eventType,
                               const std::string& content,
                               std::uint32_t originatorFormID = 0,
                               std::uint32_t targetFormID = 0,
                               bool exactAudience = false,
                               const std::vector<std::uint32_t>& audienceFormIDs = {}) override {
                return MARAS::Bridge::SkyrimNet::RegisterSkyrimNetEvent(
                    eventType, content, originatorFormID, targetFormID, exactAudience, audienceFormIDs);
            }

            void RegisterDecorators() override {
                MARAS::Bridge::SkyrimNet::RegisterSkyrimNetDecorators();
            }

            bool EvaluatePaternitySecret(std::uint32_t childID) override {
                return MARAS::Bridge::SkyrimNet::EvaluatePaternitySecret(childID);
            }

            bool SendCustomPromptToLLM(const std::string& promptName,
                                       const std::string& variant,
                                       const std::string& contextJson,
                                       std::function<void(const char* response, int success)> callback) override {
                return MARAS::Bridge::SkyrimNet::SendCustomPromptToLLM(
                    promptName, variant, contextJson, std::move(callback));
            }

            bool EvaluateStepIn(const std::string& contextJson,
                                std::function<void(const MARAS::App::StepInEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluateStepIn(
                    contextJson, std::move(callback));
            }

            bool EvaluatePaternity(const std::string& contextJson,
                                   bool isSecretForPresumedFather,
                                   std::function<void(const MARAS::App::PaternityEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluatePaternity(
                    contextJson, isSecretForPresumedFather, std::move(callback));
            }

            bool EvaluatePartnerAgency(const std::string& contextJson,
                                       const std::string& evalMode,
                                       bool canConfess,
                                       const std::string& partnerName,
                                       const std::string& sideLoverName,
                                       const std::string& affairStage,
                                       std::function<void(const MARAS::App::PartnerAgencyEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluatePartnerAgency(
                    contextJson, evalMode, canConfess, partnerName, sideLoverName, affairStage, std::move(callback));
            }

            bool EvaluateMatchmakingVerify(
                const std::string& contextJson,
                const std::string& action,
                const std::string& targetStatus,
                const std::string& actorAName,
                const std::string& actorBName,
                std::function<void(const MARAS::App::MatchmakingVerifyEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluateMatchmakingVerify(
                    contextJson, action, targetStatus, actorAName, actorBName, std::move(callback));
            }

            std::string SanitizeSingleLine(std::string_view text) const override {
                return MARAS::Bridge::SkyrimNet::Decisions::SanitizeSingleLine(text);
            }

            std::string SanitizeKey(std::string_view rawKey) const override {
                return MARAS::Bridge::SkyrimNet::Decisions::SanitizeKey(rawKey);
            }

            std::string GetLLMFailureGuidanceTitle(MARAS::App::LLMFailureType failureType) const override {
                return MARAS::Bridge::SkyrimNet::GetLLMFailureGuidanceTitle(failureType);
            }

            std::string GetLLMFailureGuidance(
                MARAS::App::LLMFailureType failureType,
                std::string_view llmVariant = "gamemaster_evaluation") const override {
                return MARAS::Bridge::SkyrimNet::GetLLMFailureGuidance(failureType, llmVariant);
            }

            std::string GetLLMBatchFailureGuidance(
                bool hasRequestFailure,
                bool hasInvalidJson,
                std::string_view llmVariant = "gamemaster_evaluation") const override {
                return MARAS::Bridge::SkyrimNet::GetLLMBatchFailureGuidance(
                    hasRequestFailure, hasInvalidJson, llmVariant);
            }

            void TickMemories() override {
                SkyrimNetMemoryService::GetSingleton().Tick();
            }

            void RevertMemories() override {
                SkyrimNetMemoryService::GetSingleton().Revert();
            }

            bool IsMemoriesSuppressed() const override {
                return SkyrimNetMemoryService::IsSuppressed();
            }

            void PushSuppressMemories() override {
                SkyrimNetMemoryService::PushSuppress();
            }

            void PopSuppressMemories() override {
                SkyrimNetMemoryService::PopSuppress();
            }

            bool IsMatchmakingApplyActive() const override {
                return SkyrimNetMemoryService::IsMatchmakingApplyActive();
            }

            void PushMatchmakingApply() override {
                SkyrimNetMemoryService::PushMatchmakingApply();
            }

            void PopMatchmakingApply() override {
                SkyrimNetMemoryService::PopMatchmakingApply();
            }

            void RecordRelationshipStatusChange(std::uint32_t initiatorID,
                                                std::uint32_t partnerID,
                                                MARAS::Foundation::RelationshipStatus oldStatus,
                                                MARAS::Foundation::RelationshipStatus newStatus) override {
                SkyrimNetMemoryService::GetSingleton().RecordRelationshipStatusChange(
                    initiatorID, partnerID, oldStatus, newStatus);
            }

            void RecordOpenRelationshipToggle(std::uint32_t actorA,
                                              std::uint32_t actorB,
                                              bool isOpen) override {
                SkyrimNetMemoryService::GetSingleton().RecordOpenRelationshipToggle(actorA, actorB, isOpen);
            }

            void RecordInfidelityEvent(std::uint32_t cheaterID,
                                       std::uint32_t victimID,
                                       std::uint32_t affairPartnerID,
                                       const std::string& eventSubType,
                                       bool isWalkIn = false) override {
                SkyrimNetMemoryService::GetSingleton().RecordInfidelityEvent(
                    cheaterID, victimID, affairPartnerID, eventSubType, isWalkIn);
            }

            void RecordPregnancyDiscovered(std::uint32_t motherID,
                                           std::uint32_t partnerID = 0,
                                           std::uint32_t fallbackBioID = 0,
                                           const std::string& fallbackBioName = "",
                                           std::int32_t childCount = 1) override {
                SkyrimNetMemoryService::GetSingleton().RecordPregnancyDiscovered(
                    motherID, partnerID, fallbackBioID, fallbackBioName, childCount);
            }

            void RecordPaternityReveal(std::uint32_t childID,
                                       const std::string& outcome,
                                       const std::string& partnerReaction = "none",
                                       bool wasPregnancySecretForPartner = false) override {
                SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(
                    childID, outcome, partnerReaction, wasPregnancySecretForPartner);
            }

            void RecordChildBirth(std::uint32_t motherID,
                                  const std::vector<std::uint32_t>& childIDs) override {
                SkyrimNetMemoryService::GetSingleton().RecordChildBirth(motherID, childIDs);
            }

            void RecordChildCustodyEvent(const std::string& eventType,
                                         std::uint32_t childID,
                                         std::uint32_t guardianAID,
                                         std::uint32_t guardianBID,
                                         std::uint32_t otherID,
                                         const std::string& reason) override {
                SkyrimNetMemoryService::GetSingleton().RecordChildCustodyEvent(
                    eventType, childID, guardianAID, guardianBID, otherID, reason);
            }

            void PreviewRelationshipMemories(std::uint32_t actorA,
                                             std::uint32_t actorB,
                                             const std::string& targetStatus,
                                             const std::string& action) override {
                SkyrimNetMemoryService::GetSingleton().PreviewRelationshipMemories(
                    actorA, actorB, targetStatus, action);
            }

            void IngestMemories(const std::vector<MARAS::App::GeneratedMemoryEntry>& memories,
                                const std::vector<std::uint32_t>& fallbackRelatedActors = {}) override {
                SkyrimNetMemoryService::GetSingleton().IngestMemories(memories, fallbackRelatedActors);
            }

            void RevertProfiles() override {
                SkyrimNetProfileService::GetSingleton().Revert();
            }

            bool AreProfilesSuppressed() const override {
                return SkyrimNetProfileService::IsSuppressed();
            }

            void PushSuppressProfiles() override {
                SkyrimNetProfileService::PushSuppress();
            }

            void PopSuppressProfiles() override {
                SkyrimNetProfileService::PopSuppress();
            }

            void MarkActorStageUpdated(std::uint32_t childID,
                                       MARAS::Foundation::ChildStage stage) override {
                SkyrimNetProfileService::GetSingleton().MarkActorStageUpdated(childID, stage);
            }

            void RequestChildProfileUpdate(std::uint32_t childID,
                                           MARAS::Foundation::ChildStage stage) override {
                SkyrimNetProfileService::GetSingleton().RequestChildProfileUpdate(childID, stage);
            }

            std::string GetActorBioTemplateName(std::uint32_t actorFormID) const override {
                return SkyrimNetProfileService::GetSingleton().GetActorBioTemplateName(actorFormID);
            }

            std::uint64_t GetActorUUID(std::uint32_t actorFormID) const override {
                return MARAS::Bridge::SkyrimNet::GetActorUUID(actorFormID);
            }

            bool IsActorSuccessionAvailable() const override {
                return MARAS::Bridge::SkyrimNet::IsActorSuccessionAvailable();
            }

            bool RecordActorSuccession(std::uint32_t fromFormID, std::uint32_t toFormID) override {
                return MARAS::Bridge::SkyrimNet::RecordActorSuccession(fromFormID, toFormID);
            }

            bool RecordActorSuccessionByUUID(std::uint64_t fromUuid, std::uint64_t toUuid) override {
                return MARAS::Bridge::SkyrimNet::RecordActorSuccessionByUUID(fromUuid, toUuid);
            }

            bool RequestActorProfileUpdate(std::uint32_t actorFormID,
                                           const std::string& additionalInstructions = "",
                                           const std::string& updatableBlocksCSV = "") override {
                return SkyrimNetProfileService::GetSingleton().RequestActorProfileUpdate(
                    actorFormID, additionalInstructions, updatableBlocksCSV);
            }
            void RevertEvents() noexcept override {
                SkyrimNetEventService::GetSingleton().Revert();
            }
            void SuppressNextActionNarration(float durationSeconds) noexcept override {
                SkyrimNetEventService::GetSingleton().SuppressNextActionNarration(durationSeconds);
            }
            bool IsActionNarrationSuppressed() const noexcept override {
                return SkyrimNetEventService::GetSingleton().IsActionNarrationSuppressed();
            }
            void EmitDirectNarration(
                std::string_view content,
                std::uint32_t originatorFormId,
                std::uint32_t targetFormId,
                bool exactAudience,
                const std::vector<std::uint32_t>& audienceFormIds) override {
                SkyrimNetEventService::GetSingleton().EmitDirectNarration(
                    content, originatorFormId, targetFormId, exactAudience, audienceFormIds);
            }
            void EmitPolygamyBlessing(std::uint32_t spouseFormId) override {
                SkyrimNetEventService::GetSingleton().EmitPolygamyBlessing(spouseFormId);
            }
            void EmitAffectionGift(std::string_view msg, std::uint32_t receiverFormId) override {
                SkyrimNetEventService::GetSingleton().EmitAffectionGift(msg, receiverFormId);
            }
            void EmitMaritalStatusChange(
                std::uint32_t initiatorFormId,
                std::uint32_t acceptorFormId,
                std::string_view newStatus,
                bool forceSkipTrigger) override {
                SkyrimNetEventService::GetSingleton().EmitMaritalStatusChange(
                    initiatorFormId, acceptorFormId, newStatus, forceSkipTrigger);
            }
            void EmitOpenRelationshipChange(
                std::uint32_t initiatorFormId,
                std::uint32_t partnerFormId,
                bool isOpen,
                bool forceSkipTrigger) override {
                SkyrimNetEventService::GetSingleton().EmitOpenRelationshipChange(
                    initiatorFormId, partnerFormId, isOpen, forceSkipTrigger);
            }
            void EmitSpouseHierarchyChange(std::uint32_t spouseFormId, bool isDemoted) override {
                SkyrimNetEventService::GetSingleton().EmitSpouseHierarchyChange(spouseFormId, isDemoted);
            }
            void EmitSpouseAffectionChange(
                std::uint32_t spouseFormId,
                std::string_view affectionLevel,
                std::string_view direction) override {
                SkyrimNetEventService::GetSingleton().EmitSpouseAffectionChange(
                    spouseFormId, affectionLevel, direction);
            }
            void EmitProposalDecision(
                std::uint32_t initiatorFormId,
                std::uint32_t acceptorFormId,
                std::string_view status,
                std::string_view decision,
                bool forceSkipTrigger) override {
                SkyrimNetEventService::GetSingleton().EmitProposalDecision(
                    initiatorFormId, acceptorFormId, status, decision, forceSkipTrigger);
            }
            void EmitActionDecline(
                std::uint32_t speakerFormId,
                std::uint32_t targetFormId,
                std::uint32_t initiatorFormId,
                std::uint32_t acceptorFormId,
                std::string_view declinedAction,
                std::string_view msg,
                bool skipTrigger) override {
                SkyrimNetEventService::GetSingleton().EmitActionDecline(
                    speakerFormId, targetFormId, initiatorFormId, acceptorFormId, declinedAction, msg, skipTrigger);
            }
            void EmitChildAdopted(
                std::uint32_t childActorId,
                std::uint32_t parentAId,
                std::uint32_t parentBId) override {
                SkyrimNetEventService::GetSingleton().EmitChildAdopted(childActorId, parentAId, parentBId);
            }
            void EmitChildInherited(
                std::uint32_t childActorId,
                std::uint32_t deceasedParentId,
                std::uint32_t newParentId,
                std::uint32_t coParentId) override {
                SkyrimNetEventService::GetSingleton().EmitChildInherited(
                    childActorId, deceasedParentId, newParentId, coParentId);
            }
            void EmitChildParentsChanged(
                std::uint32_t childActorId,
                std::uint32_t parentAId,
                std::uint32_t parentBId,
                std::string_view reason) override {
                SkyrimNetEventService::GetSingleton().EmitChildParentsChanged(
                    childActorId, parentAId, parentBId, reason);
            }
            void EmitPregnancyAnnounced(
                std::uint32_t motherId,
                std::uint32_t partnerId,
                std::uint32_t fallbackBioId,
                std::string_view fallbackBioName,
                std::int32_t childCount) override {
                SkyrimNetEventService::GetSingleton().EmitPregnancyAnnounced(
                    motherId, partnerId, fallbackBioId, fallbackBioName, childCount);
            }
            void EmitPregnancyLost(
                std::uint32_t motherId,
                std::uint32_t partnerId,
                std::string_view reason) override {
                SkyrimNetEventService::GetSingleton().EmitPregnancyLost(motherId, partnerId, reason);
            }
            void EmitLaborStarted(
                std::uint32_t motherId,
                std::uint32_t partnerId,
                std::int32_t childCount) override {
                SkyrimNetEventService::GetSingleton().EmitLaborStarted(motherId, partnerId, childCount);
            }
            void EmitLaborProgress(
                std::uint32_t motherId,
                std::int32_t bornCount,
                std::int32_t totalCount) override {
                SkyrimNetEventService::GetSingleton().EmitLaborProgress(motherId, bornCount, totalCount);
            }
            void EmitChildBirthBatch(
                std::uint32_t motherId,
                const std::vector<std::uint32_t>& childIds) override {
                SkyrimNetEventService::GetSingleton().EmitChildBirthBatch(motherId, childIds);
            }
            void EmitChildBirth(std::uint32_t childId, std::string_view childName) override {
                SkyrimNetEventService::GetSingleton().EmitChildBirth(childId, childName);
            }
            void EmitChildGrewUp(std::uint32_t childId, std::string_view childName) override {
                SkyrimNetEventService::GetSingleton().EmitChildGrewUp(childId, childName);
            }
            void EmitChildSentToTraining(
                std::uint32_t childId,
                std::string_view childName,
                std::string_view trainingLocation) override {
                SkyrimNetEventService::GetSingleton().EmitChildSentToTraining(childId, childName, trainingLocation);
            }
            void EmitChildBecameAdult(std::uint32_t childId, std::string_view childName) override {
                SkyrimNetEventService::GetSingleton().EmitChildBecameAdult(childId, childName);
            }
            void EmitPaternitySecretResolved(std::uint32_t childId, std::string_view rawCaseType) override {
                SkyrimNetEventService::GetSingleton().EmitPaternitySecretResolved(childId, rawCaseType);
            }
            void EmitAffairConfessed(
                std::uint32_t cheaterId,
                std::uint32_t victimId,
                std::uint32_t affairPartnerId) override {
                SkyrimNetEventService::GetSingleton().EmitAffairConfessed(cheaterId, victimId, affairPartnerId);
            }
            void EmitAffairExposed(
                std::uint32_t affairPartnerId,
                std::uint32_t victimId,
                std::uint32_t cheaterId) override {
                SkyrimNetEventService::GetSingleton().EmitAffairExposed(affairPartnerId, victimId, cheaterId);
            }
            void EmitAffairResolved(
                std::uint32_t initiatorId,
                std::uint32_t otherPartyId,
                std::uint32_t affairPartnerId,
                std::string_view resolution) override {
                SkyrimNetEventService::GetSingleton().EmitAffairResolved(
                    initiatorId, otherPartyId, affairPartnerId, resolution);
            }
            void EmitCheatingCaught(
                std::uint32_t victimId,
                std::uint32_t cheaterId,
                std::uint32_t affairPartnerId,
                bool isWalkIn) override {
                SkyrimNetEventService::GetSingleton().EmitCheatingCaught(
                    victimId, cheaterId, affairPartnerId, isWalkIn);
            }
            void EmitOpenRelationshipWalkIn(
                std::uint32_t witnessId,
                std::uint32_t partnerId,
                std::uint32_t sexPartnerId,
                bool isWalkIn) override {
                SkyrimNetEventService::GetSingleton().EmitOpenRelationshipWalkIn(
                    witnessId, partnerId, sexPartnerId, isWalkIn);
            }
        };

    }  // namespace

    void RegisterAppGateway() noexcept {
        static SkyrimNetGatewayImpl impl;
        MARAS::App::SetSkyrimNetGateway(&impl);
    }

}  // namespace MARAS::Bridge::SkyrimNet
