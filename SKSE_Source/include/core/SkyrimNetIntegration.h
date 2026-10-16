#pragma once

#include "PCH.h"

namespace MARAS {

    // Checks if SkyrimNet.dll is currently loaded into the process.
    bool IsSkyrimNetAvailable();

    // Attempts to locate SkyrimNet.dll in the current process and registers
    // MARAS decorators via its PublicRegisterDecorator export.
    // Must be called at kPostPostLoad (after all SKSE plugins are loaded).
    void RegisterSkyrimNetDecorators();

    // Sends an LLM prompt to SkyrimNet to evaluate a proposed relationship status change between two actors.
    // Includes in-game MessageBox retry/ignore modal on LLM request or parse failure.
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
