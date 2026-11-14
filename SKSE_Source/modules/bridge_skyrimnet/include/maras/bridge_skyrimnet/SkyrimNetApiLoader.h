#pragma once

#include "maras/app/ModalTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace RE {
    class Actor;
}

namespace MARAS::Bridge::SkyrimNet {

    void RegisterAppGateway() noexcept;

    // Checks if SkyrimNet.dll is loaded and not simulated as disabled via SettingsData::simulateNoSkyrimNet.
    [[nodiscard]] bool IsSkyrimNetAvailable();

    // Checks if PublicSendCustomDecisionToLLM is exported by SkyrimNet.dll (SkyrimNet API v12+).
    [[nodiscard]] bool IsDecisionsExportAvailable();

    // Checks whether SkyrimNet live narration/triggers should fire for the given event participant(s).
    [[nodiscard]] bool ShouldTriggerSkyrimNetNarration(RE::Actor* speaker, RE::Actor* target = nullptr);

    // Registers an event with SkyrimNet via PublicRegisterEvent (including _marasMemory intercept).
    bool RegisterSkyrimNetEvent(const std::string& eventType,
                                const std::string& content,
                                std::uint32_t originatorFormID = 0,
                                std::uint32_t targetFormID = 0,
                                bool exactAudience = false,
                                const std::vector<std::uint32_t>& audienceFormIDs = {});

    // Registers a single prompt decorator export callback with SkyrimNet.dll.
    bool RegisterDecoratorExport(const char* name,
                                 const char* description,
                                 std::function<std::string(RE::Actor*)> callback);

    // Resolves a SkyrimNet actor UUID for the given FormID (checking actor base and ref IDs).
    [[nodiscard]] std::uint64_t GetActorUUID(std::uint32_t formID);

    // Resolves a SkyrimNet UUID prioritizing placed RE::Actor ref first so runtime entities register cleanly.
    [[nodiscard]] std::uint64_t ResolvePlacedActorUUID(std::uint32_t actorFormID);

    // Reads SkyrimNet plugin configuration values via PublicGetPluginConfigValue.
    [[nodiscard]] std::string GetPluginConfigValueExport(const char* plugin,
                                                           const char* path,
                                                           const char* defaultValue);

    // Sends an arbitrary custom prompt to SkyrimNet LLM with async callback.
    bool SendCustomPromptToLLM(const std::string& promptName,
                               const std::string& variant,
                               const std::string& contextJson,
                               std::function<void(const char* response, int success)> callback);

    // Sends a custom Jev decision template (`prompts/decisions/<templateName>.prompt`) via PublicSendCustomDecisionToLLM.
    bool SendCustomDecisionToLLM(const std::string& templateName,
                                 const std::string& contextJson,
                                 int maxProbabilities,
                                 std::function<void(const char* answer, const char* resultJson, int success)> callback);

    // Ingests a memory record into SkyrimNet's memory database for the specified actor.
    bool AddActorMemory(std::uint32_t actorFormID,
                        const std::string& content,
                        float importance,
                        const std::string& memoryType,
                        const std::string& emotion,
                        const std::string& location,
                        const std::vector<std::string>& tags,
                        const std::vector<std::uint32_t>& relatedActorFormIDs = {});

    // Dynamic exports consumed by SkyrimNetProfileService:
    bool RequestProfileUpdateExport(std::uint32_t formID,
                                    const std::string& additionalInstructions,
                                    const std::string& updatableBlocksCSV);
    [[nodiscard]] std::string GetBioTemplateNameExport(std::uint32_t formID);
    [[nodiscard]] std::string GetSaveUniqueIDExport();
    [[nodiscard]] std::vector<std::string> QueryActorMemoriesExport(std::uint32_t formID,
                                                                    int maxCount,
                                                                    const char* contextQuery = "");

    // User-facing failure guidance copy for LLM retry modals.
    [[nodiscard]] std::string GetLLMFailureGuidanceTitle(MARAS::App::LLMFailureType failureType);
    [[nodiscard]] std::string GetLLMFailureGuidance(MARAS::App::LLMFailureType failureType,
                                                    std::string_view llmVariant = "gamemaster_evaluation");
    [[nodiscard]] std::string GetLLMBatchFailureGuidance(bool hasRequestFailure,
                                                         bool hasInvalidJson,
                                                         std::string_view llmVariant = "gamemaster_evaluation");

}  // namespace MARAS::Bridge::SkyrimNet
