#pragma once

#include "core/NPCRelationshipManager.h"

// Example Papyrus interface functions for the NPC Relationship Manager
// These would be called from your Papyrus scripts via native functions

namespace MARAS::PapyrusInterface {

    // Registration functions
    bool RegisterCandidate(RE::StaticFunctionTag*, RE::Actor* npc);
    bool UnregisterNPC(RE::StaticFunctionTag*, RE::Actor* npc);

    // Consolidated faction management - supports both enum and string type
    bool AddToFaction(RE::StaticFunctionTag*, RE::Actor* npc, std::string factionType, std::int32_t rank);

    // Consolidated status checks - supports both enum and string type
    bool IsNPCStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType);
    bool IsNPCStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum);

    // Consolidated status transitions - supports both enum and string type
    bool PromoteNPCToStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType);
    bool PromoteNPCToStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum);

    // Consolidated bulk retrieval - supports both enum and string type
    std::vector<RE::Actor*> GetNPCsByStatus(RE::StaticFunctionTag*, std::string statusType);
    std::vector<RE::Actor*> GetNPCsByStatusEnum(RE::StaticFunctionTag*, std::int32_t statusEnum);

    // Consolidated statistics - supports both enum and string type
    int GetStatusCount(RE::StaticFunctionTag*, std::string statusType);
    int GetStatusCountByEnum(RE::StaticFunctionTag*, std::int32_t statusEnum);

    // Debug functions
    void LogNPCStatistics(RE::StaticFunctionTag*);
    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc);

    // Spouse hierarchy bindings
    bool SetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t rank);
    std::int32_t GetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc);

    // Spouse buff/service bindings
    float GetSpouseMultiplier(RE::StaticFunctionTag*, RE::Actor* spouse);
    std::vector<float> GetFollowersMultipliers(RE::StaticFunctionTag*, std::vector<RE::Actor*> followers);
    std::vector<float> GetPermanentMultipliers(RE::StaticFunctionTag*);

    // Affection system bindings
    void AddAffection(RE::StaticFunctionTag*, RE::Actor* npc, float amount, std::string type);
    float GetAffection(RE::StaticFunctionTag*, RE::Actor* npc, std::string type);
    int GetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc);
    void SetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc, int amount);
    void SetAffectionMinMax(RE::StaticFunctionTag*, std::string type, int minVal, int maxVal);
    void ApplyDailyAffection(RE::StaticFunctionTag*);

    // Marriage difficulty calculation
    float CalculateMarriageSuccessChance(RE::StaticFunctionTag*, RE::Actor* npc, float intimacyAdjustment,
                                         float mostGold, float housesOwned, float horsesOwned, float questsCompleted,
                                         float dungeonsCleared, float dragonSoulsCollected);

    // Registration function for SKSE
    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm);

}  // namespace MARAS::PapyrusInterface
