#include "maras/bridge_skyrimnet/SkyrimNetApiLoader.h"

#include "PCH.h"
#include "maras/bridge_skyrimnet/SkyrimNetMemoryService.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <Windows.h>
#include <algorithm>
#include <exception>

namespace MARAS::Bridge::SkyrimNet {

    namespace {

        using PublicRegisterDecorator_t = bool (*)(
            const char* name,
            const char* description,
            std::function<std::string(RE::Actor*)> callback);
        using PublicFormIDToUUID_t = std::uint64_t (*)(std::uint32_t formId);
        using PublicGetPluginConfigValue_t = std::string (*)(const char* plugin,
                                                           const char* path,
                                                           const char* defaultValue);
        using PublicRegisterEvent_t = bool (*)(const char* eventType,
                                               const char* content,
                                               std::uint32_t originatorFormId,
                                               std::uint32_t targetFormId,
                                               const std::uint32_t* audienceFormIds,
                                               int audienceCount);
        using PublicSendCustomPromptToLLM_t = bool (*)(
            const char* promptName,
            const char* variant,
            const char* contextJson,
            std::function<void(const char* response, int success)> callback);
        using PublicSendCustomDecisionToLLM_t = bool (*)(
            const char* templateName,
            const char* contextJson,
            int maxProbabilities,
            std::function<void(const char* answer, const char* resultJson, int success)> callback);
        using PublicAddMemory_t = int (*)(
            std::uint32_t formId,
            const char* contentText,
            float importance,
            const char* memoryType,
            const char* emotion,
            const char* location,
            const char* tagsJSON,
            const char* relatedActorsJSON);
        using PublicRequestProfileUpdate_t = bool (*)(
            std::uint32_t formId,
            const char* additionalInstructions,
            const char* updatableBlocksCSV);
        using PublicGetBioTemplateName_t = std::string (*)(std::uint32_t formId);
        using PublicGetSaveUniqueID_t = std::string (*)();
        using PublicGetMemoriesForActor_t = std::string (*)(
            std::uint32_t formId,
            int maxCount,
            const char* contextQuery);

        std::string GetModelSlotDisplayName(std::string_view llmVariant) {
            if (llmVariant == "memory") {
                return "Memory";
            }
            if (llmVariant == "decisions") {
                return "Decisions";
            }
            return "GameMaster";
        }

        template <typename FnPtr>
        FnPtr LookupSkyrimNetExport(const char* exportName) {
            HMODULE hMod = ::GetModuleHandleW(L"SkyrimNet.dll");
            if (!hMod) {
                return nullptr;
            }
            return reinterpret_cast<FnPtr>(::GetProcAddress(hMod, exportName));
        }

    }  // namespace

    bool IsSkyrimNetAvailable() {
        if (MARAS::State::WorldStore::GetSingleton().Snapshot()->settings->simulateNoSkyrimNet) {
            return false;
        }
        return ::GetModuleHandleW(L"SkyrimNet.dll") != nullptr;
    }

    bool IsDecisionsExportAvailable() {
        if (!IsSkyrimNetAvailable()) {
            return false;
        }
        return LookupSkyrimNetExport<PublicSendCustomDecisionToLLM_t>("PublicSendCustomDecisionToLLM") != nullptr;
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

        HMODULE hMod = ::GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: SkyrimNet.dll not found in process");
            return false;
        }

        auto* fnRegisterEvent = reinterpret_cast<PublicRegisterEvent_t>(
            ::GetProcAddress(hMod, "PublicRegisterEvent"));
        if (!fnRegisterEvent) {
            MARAS_LOG_WARN("[SkyrimNet] RegisterSkyrimNetEvent: PublicRegisterEvent export not found in SkyrimNet.dll");
            return false;
        }

        if (MARAS::IsSyntheticChildID(originatorFormID)) {
            originatorFormID = 0;
        }
        if (MARAS::IsSyntheticChildID(targetFormID)) {
            targetFormID = 0;
        }

        std::string payloadToSend = content;
        if (eventType == "ttm_event") {
            nlohmann::json parsed = nlohmann::json::parse(content, nullptr, /*allow_exceptions=*/false);
            if (parsed.is_object() && parsed.contains("_marasMemory")) {
                const nlohmann::json memMeta = parsed["_marasMemory"];
                std::string ttmType;
                if (auto itType = parsed.find("type"); itType != parsed.end() && itType->is_string()) {
                    ttmType = itType->get<std::string>();
                }
                parsed.erase("_marasMemory");
                payloadToSend = parsed.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

                if (memMeta.is_object()) {
                    auto readFormId = [&](std::string_view key) -> std::uint32_t {
                        auto it = memMeta.find(key);
                        if (it != memMeta.end() && (it->is_number_unsigned() || it->is_number_integer())) {
                            return static_cast<std::uint32_t>(it->get<std::uint64_t>());
                        }
                        return 0;
                    };

                    if (ttmType == "ttm_pregnancy_announced") {
                        const std::uint32_t motherID = readFormId("motherId");
                        const std::uint32_t partnerID = readFormId("partnerId");
                        const std::uint32_t fallbackBioID = readFormId("fallbackBioId");
                        std::string fallbackBioName;
                        if (auto itName = memMeta.find("fallbackBioName"); itName != memMeta.end() && itName->is_string()) {
                            fallbackBioName = itName->get<std::string>();
                        }
                        std::int32_t childCount = 1;
                        if (auto itCnt = memMeta.find("childCount");
                            itCnt != memMeta.end() && (itCnt->is_number_integer() || itCnt->is_number_unsigned())) {
                            childCount = itCnt->get<std::int32_t>();
                        }
                        if (motherID != 0) {
                            std::lock_guard lock(MARAS::GetPapyrusMutex());
                            SkyrimNetMemoryService::GetSingleton().RecordPregnancyDiscovered(
                                motherID, partnerID, fallbackBioID, fallbackBioName, childCount);
                        }
                    } else if (ttmType == "ttm_child_birth") {
                        const std::uint32_t motherID = readFormId("motherId");
                        std::vector<std::uint32_t> childIDs;
                        if (auto itIds = memMeta.find("childIds"); itIds != memMeta.end() && itIds->is_array()) {
                            childIDs.reserve(itIds->size());
                            for (const auto& elem : *itIds) {
                                if (elem.is_number_unsigned() || elem.is_number_integer()) {
                                    childIDs.push_back(static_cast<std::uint32_t>(elem.get<std::uint64_t>()));
                                }
                            }
                        }
                        if (motherID != 0) {
                            std::lock_guard lock(MARAS::GetPapyrusMutex());
                            SkyrimNetMemoryService::GetSingleton().RecordChildBirth(motherID, childIDs);
                        }
                    }
                }
            }
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
        const int audienceCount = static_cast<int>(exactAudienceIds.size());

        const bool ok = fnRegisterEvent(
            eventType.c_str(),
            payloadToSend.c_str(),
            originatorFormID,
            targetFormID,
            audiencePtr,
            audienceCount);

        MARAS_LOG_DEBUG("[SkyrimNet] RegisterSkyrimNetEvent('{}', orig=0x{:08X}, target=0x{:08X}, exactAudience={}, audienceCount={}) -> {}",
                        eventType, originatorFormID, targetFormID, exactAudience || audienceCount > 0, audienceCount, ok);
        return ok;
    }

    bool RegisterDecoratorExport(const char* name,
                                 const char* description,
                                 std::function<std::string(RE::Actor*)> callback) {
        if (!name || name[0] == '\0') {
            return false;
        }
        HMODULE hMod = ::GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            MARAS_LOG_WARN("[SkyrimNet] SkyrimNet.dll not found in process - decorator '{}' skipped.", name);
            return false;
        }
        auto* fn = reinterpret_cast<PublicRegisterDecorator_t>(
            ::GetProcAddress(hMod, "PublicRegisterDecorator"));
        if (!fn) {
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
        if (fn(name, safeDesc, std::move(wrappedCallback))) {
            MARAS_LOG_INFO("[SkyrimNet] Registered decorator '{}'", name);
            return true;
        }
        MARAS_LOG_WARN("[SkyrimNet] Failed to register decorator '{}' (duplicate or invalid name?)", name);
        return false;
    }

    std::uint64_t GetActorUUID(std::uint32_t formID) {
        if (formID == 0 || formID == MARAS::kUnknownFatherFormID || MARAS::IsSyntheticChildID(formID)) {
            return 0;
        }
        auto* fn = LookupSkyrimNetExport<PublicFormIDToUUID_t>("PublicFormIDToUUID");
        if (!fn) {
            return 0;
        }
        std::uint64_t uuid = 0;
        if (auto* base = MARAS::ActorSvc::GetActorBase(formID)) {
            uuid = fn(base->GetFormID());
        }
        if (uuid == 0) {
            uuid = fn(formID);
        }
        return uuid;
    }

    std::uint64_t ResolvePlacedActorUUID(std::uint32_t actorFormID) {
        if (actorFormID == 0 || actorFormID == MARAS::kUnknownFatherFormID || MARAS::IsSyntheticChildID(actorFormID)) {
            return 0;
        }
        if (auto* fn = LookupSkyrimNetExport<PublicFormIDToUUID_t>("PublicFormIDToUUID")) {
            // Pass the placed RE::Actor reference FormID directly first so SkyrimNet's
            // ResolveActorEntity(formId) registers the live Actor in EntityMap and UUIDResolver.
            const std::uint64_t uuid = fn(actorFormID);
            if (uuid != 0) {
                return uuid;
            }
        }
        return GetActorUUID(actorFormID);
    }

    std::string GetPluginConfigValueExport(const char* plugin,
                                           const char* path,
                                           const char* defaultValue) {
        const std::string fallback = defaultValue ? defaultValue : "";
        if (!plugin || !path || !IsSkyrimNetAvailable()) {
            return fallback;
        }
        auto* fn = LookupSkyrimNetExport<PublicGetPluginConfigValue_t>("PublicGetPluginConfigValue");
        if (!fn) {
            return fallback;
        }
        return fn(plugin, path, fallback.c_str());
    }

    bool SendCustomPromptToLLM(const std::string& promptName,
                               const std::string& variant,
                               const std::string& contextJson,
                               std::function<void(const char* response, int success)> callback) {
        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: SkyrimNet is not available or simulated as disabled");
            return false;
        }

        HMODULE hMod = ::GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: SkyrimNet.dll not found in process");
            return false;
        }

        auto* fnSendLLM = reinterpret_cast<PublicSendCustomPromptToLLM_t>(
            ::GetProcAddress(hMod, "PublicSendCustomPromptToLLM"));
        if (!fnSendLLM) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomPromptToLLM: PublicSendCustomPromptToLLM export not found in SkyrimNet.dll");
            return false;
        }

        return fnSendLLM(promptName.c_str(), variant.c_str(), contextJson.c_str(), std::move(callback));
    }

    bool SendCustomDecisionToLLM(const std::string& templateName,
                                 const std::string& contextJson,
                                 int maxProbabilities,
                                 std::function<void(const char* answer, const char* resultJson, int success)> callback) {
        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: SkyrimNet is not available or simulated as disabled");
            return false;
        }

        HMODULE hMod = ::GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: SkyrimNet.dll not found in process");
            return false;
        }

        auto* fnSendDecision = reinterpret_cast<PublicSendCustomDecisionToLLM_t>(
            ::GetProcAddress(hMod, "PublicSendCustomDecisionToLLM"));
        if (!fnSendDecision) {
            MARAS_LOG_WARN("[SkyrimNet] SendCustomDecisionToLLM: PublicSendCustomDecisionToLLM export not found in SkyrimNet.dll");
            return false;
        }

        return fnSendDecision(templateName.c_str(), contextJson.c_str(), maxProbabilities, std::move(callback));
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

        auto* fnAddMemory = LookupSkyrimNetExport<PublicAddMemory_t>("PublicAddMemory");
        if (!fnAddMemory) {
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

        const int memId = fnAddMemory(
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
        auto* fn = LookupSkyrimNetExport<PublicRequestProfileUpdate_t>("PublicRequestProfileUpdate");
        if (!fn) {
            return false;
        }
        const char* instrPtr = additionalInstructions.empty() ? nullptr : additionalInstructions.c_str();
        const char* blocksPtr = updatableBlocksCSV.empty() ? nullptr : updatableBlocksCSV.c_str();
        return fn(formID, instrPtr, blocksPtr);
    }

    std::string GetBioTemplateNameExport(std::uint32_t formID) {
        if (formID == 0 || formID == MARAS::kUnknownFatherFormID || MARAS::IsSyntheticChildID(formID)) {
            return "";
        }
        if (formID == MARAS::kPlayerFormID) {
            return "player_special";
        }
        if (!IsSkyrimNetAvailable()) {
            return "";
        }
        if (auto* fn = LookupSkyrimNetExport<PublicGetBioTemplateName_t>("PublicGetBioTemplateName")) {
            return fn(formID);
        }
        return "";
    }

    std::string GetSaveUniqueIDExport() {
        if (!IsSkyrimNetAvailable()) {
            return "";
        }
        if (auto* fn = LookupSkyrimNetExport<PublicGetSaveUniqueID_t>("PublicGetSaveUniqueID")) {
            return fn();
        }
        return "";
    }

    std::vector<std::string> QueryActorMemoriesExport(std::uint32_t formID,
                                                      int maxCount,
                                                      const char* contextQuery) {
        std::vector<std::string> memories;
        if (formID == 0 || formID == MARAS::kUnknownFatherFormID ||
            MARAS::IsSyntheticChildID(formID) || !IsSkyrimNetAvailable()) {
            return memories;
        }

        auto* memFn = LookupSkyrimNetExport<PublicGetMemoriesForActor_t>("PublicGetMemoriesForActor");
        if (!memFn) {
            return memories;
        }

        const std::string memJsonStr = memFn(formID, maxCount, contextQuery ? contextQuery : "");
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
                return "What Does This Mean? (Broken JSON Response)";
            case MARAS::App::LLMFailureType::RequestFailed:
            default:
                return "What Does This Mean? (HTTP / Connection Failure)";
        }
    }

    std::string GetLLMFailureGuidance(MARAS::App::LLMFailureType failureType, std::string_view llmVariant) {
        const std::string slotName = GetModelSlotDisplayName(llmVariant);
        if (failureType == MARAS::App::LLMFailureType::InvalidJson) {
            return fmt::format(
                "Your AI model replied, but its response was formatted incorrectly (broken JSON) and could not be read.\n\n"
                "• Why this happens: M.A.R.A.S requires the AI to reply in a strict structured data format. Even good models occasionally make a formatting mistake, and clicking Retry usually fixes it.\n"
                "• Seeing this error too often? If this happens frequently, the AI model assigned to your '{}' slot in SkyrimNet is incapable of returning valid JSON reliably most of the time. Consider switching that slot in SkyrimNet to a more capable model.",
                slotName);
        }

        return fmt::format(
            "SkyrimNet could not get a response from your AI provider for the '{}' model slot (HTTP / network error or empty reply).\n\n"
            "• Cloud AI (OpenRouter, OpenAI, etc.): The service may have timed out, hit a rate limit, run out of API credits, or your API key / model name in SkyrimNet may be invalid.\n"
            "• Local AI (Ollama, KoboldCPP, LM Studio): Your local AI server may be offline, still loading a model, or overloaded.\n"
            "• What to do: Click Retry to send the request again. If it keeps failing, check your internet connection or SkyrimNet provider settings.",
            slotName);
    }

    std::string GetLLMBatchFailureGuidance(bool hasRequestFailure,
                                            bool hasInvalidJson,
                                            std::string_view llmVariant) {
        if (hasRequestFailure && hasInvalidJson) {
            const std::string slotName = GetModelSlotDisplayName(llmVariant);
            return fmt::format(
                "Some AI requests failed to connect, while others returned unreadable data:\n\n"
                "• HTTP / Connection Failed: SkyrimNet could not reach your AI provider or received an empty reply (timeout, rate limit, out of API credits, invalid API key, or local AI server offline).\n"
                "• Broken JSON Response: The AI model in your SkyrimNet '{}' slot replied with broken formatting. If you see broken JSON errors too often, your '{}' model is incapable of returning valid JSON reliably and should be changed to a stronger model.",
                slotName, slotName);
        }
        if (hasInvalidJson) {
            return GetLLMFailureGuidance(MARAS::App::LLMFailureType::InvalidJson, llmVariant);
        }
        return GetLLMFailureGuidance(MARAS::App::LLMFailureType::RequestFailed, llmVariant);
    }

}  // namespace MARAS::Bridge::SkyrimNet
