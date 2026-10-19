#pragma once

#include "PCH.h"

namespace MARAS {

    // Checks if SkyrimNet.dll is currently loaded into the process.
    bool IsSkyrimNetAvailable();

    // Attempts to locate SkyrimNet.dll in the current process and registers
    // MARAS decorators via its PublicRegisterDecorator export.
    // Must be called at kPostPostLoad (after all SKSE plugins are loaded).
    void RegisterSkyrimNetDecorators();

    enum class LLMFailureType : uint8_t {
        RequestFailed = 0,
        InvalidJson = 1
    };

    // Returns a user-friendly title and plain-English troubleshooting explanation for an LLM failure.
    std::string GetLLMFailureGuidanceTitle(LLMFailureType failureType);
    std::string GetLLMFailureGuidance(LLMFailureType failureType, std::string_view llmVariant = "gamemaster_evaluation");
    std::string GetLLMBatchFailureGuidance(bool hasRequestFailure, bool hasInvalidJson, std::string_view llmVariant = "gamemaster_evaluation");

    // Displays a Prisma UI retry/ignore modal when a SkyrimNet LLM request or JSON parse fails.
    void ShowLLMRetryModal(const std::string& evaluationType,
                           const std::string& description,
                           LLMFailureType failureType,
                           std::function<void()> onRetry,
                           std::function<void()> onIgnore,
                           std::string_view llmVariant = "gamemaster_evaluation");

    // Sends an LLM prompt to SkyrimNet to evaluate a proposed relationship status change between two actors.
    // Includes a Prisma UI retry/ignore modal on LLM request or parse failure.
    bool EvaluateRelationshipStatusChange(RE::FormID actorFormID, RE::FormID partnerFormID, const std::string& targetStatus, const std::string& evalId = "");

    // Evaluates secret paternity resolution (confession / discovery / remains hidden) for a child.
    bool EvaluatePaternitySecret(RE::FormID childID);

    // Sends an arbitrary custom prompt to SkyrimNet LLM with async callback.
    bool SendCustomPromptToLLM(const std::string& promptName, const std::string& variant, const std::string& contextJson, std::function<void(const char* response, int success)> callback);

    // Ingests a new memory record into SkyrimNet's memory database for the specified actor.
    bool AddActorMemory(RE::FormID actorFormID, const std::string& content, float importance,
                        const std::string& memoryType, const std::string& emotion, const std::string& location,
                        const std::vector<std::string>& tags, RE::FormID relatedActorFormID = 0);

    // Returns the SkyrimNet UUID for the specified Actor FormID.
    uint64_t GetActorUUID(RE::FormID formID);

}  // namespace MARAS
