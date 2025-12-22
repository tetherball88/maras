#pragma once

#include "core/NPCRelationshipManager.h"

// Example Papyrus interface functions for the NPC Relationship Manager
// These would be called from your Papyrus scripts via native functions

namespace MARAS::PapyrusInterface {

    // Registration functions
    bool RegisterCandidate(RE::StaticFunctionTag*, RE::Actor* npc);
    bool UnregisterNPC(RE::StaticFunctionTag*, RE::Actor* npc);

    // Consolidated faction management - supports both enum and string type
    bool SetNpcCharacteristics(RE::StaticFunctionTag*, RE::Actor* npc, std::string factionType, std::int32_t rank);

    // Consolidated status checks - supports both enum and string type
    bool IsNPCStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType);
    bool IsNPCStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum);

    // Consolidated status transitions - supports both enum and string type
    bool PromoteNPCToStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType);
    bool PromoteNPCToStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum);

    // Consolidated bulk retrieval - supports both enum and string type
    std::vector<RE::Actor*> GetNPCsByStatus(RE::StaticFunctionTag*, std::string statusType);
    std::vector<RE::Actor*> GetNPCsByStatusEnum(RE::StaticFunctionTag*, std::int32_t statusEnum);

    // Returns currently-detected teammates (actors following/teammates of the player)
    std::vector<RE::Actor*> GetCurrentTeammates(RE::StaticFunctionTag*);

    // Check if a specific actor is currently a player teammate
    bool IsPlayerTeammate(RE::StaticFunctionTag*, RE::Actor* npc);

    // Consolidated statistics - supports both enum and string type
    int GetStatusCount(RE::StaticFunctionTag*, std::string statusType);
    int GetStatusCountByEnum(RE::StaticFunctionTag*, std::int32_t statusEnum);

    // Debug functions
    void LogNPCStatistics(RE::StaticFunctionTag*);
    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc);
    void SetLogLevel(RE::StaticFunctionTag*, std::int32_t logLevel);
    std::int32_t GetLogLevel(RE::StaticFunctionTag*);

    // Spouse hierarchy bindings
    bool SetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t rank);
    std::int32_t GetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc);

    // Spouse buff/service bindings
    float GetSpouseMultiplier(RE::StaticFunctionTag*, RE::Actor* spouse);
    std::vector<float> GetFollowersMultipliers(RE::StaticFunctionTag*, std::vector<RE::Actor*> followers);
    std::vector<float> GetPermanentMultipliers(RE::StaticFunctionTag*);

    // Bonuses JSON runtime accessors
    int GetBonusCount(RE::StaticFunctionTag*, std::string type);
    RE::BGSPerk* GetBonusPerk(RE::StaticFunctionTag*, std::string type, int index = 0);
    int GetBonusEffectIndex(RE::StaticFunctionTag*, std::string type, int index = 0);
    float GetBonusPerkValue(RE::StaticFunctionTag*, std::string type, int index = 0);
    std::string GetBonusPerkType(RE::StaticFunctionTag*, std::string type, int index = 0);
    std::string GetBonusPerkUnit(RE::StaticFunctionTag*, std::string type, int index = 0);
    std::string GetBonusPerkDescription(RE::StaticFunctionTag*, std::string type, int index = 0);

    // Affection system bindings
    void AddAffection(RE::StaticFunctionTag*, RE::Actor* npc, float amount, std::string type);
    float GetAffection(RE::StaticFunctionTag*, RE::Actor* npc, std::string type);
    int GetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc);
    void SetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc, int amount);
    void SetAffectionMinMax(RE::StaticFunctionTag*, std::string type, int minVal, int maxVal);
    void ApplyDailyAffection(RE::StaticFunctionTag*);
    void SetAffectionDecayMultiplier(RE::StaticFunctionTag*, float multiplier);
    float GetAffectionDecayMultiplier(RE::StaticFunctionTag*);

    // Player house papyrus bindings
    bool RegisterPlayerHouseCell(RE::StaticFunctionTag*, RE::BGSLocation* loc, RE::TESObjectREFR* homeMarker);
    std::vector<RE::BGSLocation*> GetAllPlayerHouses(RE::StaticFunctionTag*);
    std::vector<std::string> GetAllPlayerHousesNames(RE::StaticFunctionTag*);
    bool RegisterTenantInPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse, RE::BGSLocation* playerHouse);
    bool RemoveTenantFromPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse);
    std::vector<RE::Actor*> GetPlayerHouseTenants(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse);
    RE::TESObjectREFR* GetHouseMarker(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse);
    RE::BGSLocation* GetTenantHouse(RE::StaticFunctionTag*, RE::Actor* npc);
    int CountPlayerHouses(RE::StaticFunctionTag*);

    // Home cell helpers
    RE::TESObjectCELL* GetNpcOriginalHouse(RE::StaticFunctionTag*, RE::Actor* npc);
    std::vector<RE::TESObjectREFR*> GetNpcBeds(RE::StaticFunctionTag*, RE::Actor* npc);
    RE::TESObjectREFR* GetNpcOriginalHouseCenterMarker(RE::StaticFunctionTag*, RE::Actor* npc);

    // Home marker management (replaces PO3's SetLinkedRef and StorageUtil)
    RE::TESObjectREFR* GetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc);
    bool SetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc, RE::TESObjectREFR* marker);

    // Marriage difficulty calculation
    float CalculateMarriageSuccessChance(RE::StaticFunctionTag*, RE::Actor* npc, float intimacyAdjustment,
                                         float mostGold, float housesOwned, float horsesOwned, float questsCompleted,
                                         float dungeonsCleared, float dragonSoulsCollected, bool playerKiller);

    // Registration function for SKSE
    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm);

    // NPC Type and Status Queries
    std::string GetNpcStatusName(RE::StaticFunctionTag*, RE::Actor* npc);
    std::int32_t GetNpcStatusEnum(RE::StaticFunctionTag*, RE::Actor* npc);
    std::vector<std::string> GetNpcTypes(RE::StaticFunctionTag*, std::string type);
    std::int32_t GetNpcTypeEnum(RE::StaticFunctionTag*, std::string type, std::string name);
    std::int32_t GetNpcCurrentTypeEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::string type);
    std::string GetNpcCurrentTypeName(RE::StaticFunctionTag*, RE::Actor* npc, std::string type);

}  // namespace MARAS::PapyrusInterface
