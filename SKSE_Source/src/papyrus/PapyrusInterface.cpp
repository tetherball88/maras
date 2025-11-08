#include "papyrus/PapyrusInterface.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>

#include "core/MarriageDifficulty.h"
#include "core/SpouseBuffService.h"
#include "core/SpouseHierarchyManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"

namespace MARAS::PapyrusInterface {

    // Helper function to convert string to lowercase
    std::string ToLower(std::string_view str) {
        std::string result(str);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    // ========================================
    // Registration functions
    // ========================================

    bool RegisterCandidate(RE::StaticFunctionTag*, RE::Actor* npc) {
        auto startTime = std::chrono::high_resolution_clock::now();

        if (!npc) {
            SKSE::log::warn("RegisterCandidate: null actor provided");
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        bool result = manager.RegisterAsCandidate(npc->GetFormID());

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        SKSE::log::info("Papyrus RegisterCandidate for {} completed in {} microseconds (success: {})",
                        npc->GetDisplayFullName(), duration.count(), result);

        return result;
    }

    bool UnregisterNPC(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            SKSE::log::warn("UnregisterNPC: null actor provided");
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        return manager.UnregisterNPC(npc->GetFormID());
    }

    // ========================================
    // Consolidated faction management
    // ========================================

    bool AddToFaction(RE::StaticFunctionTag*, RE::Actor* npc, std::string factionType, std::int32_t rank) {
        if (!npc) {
            SKSE::log::warn("AddToFaction: null actor provided");
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = ToLower(factionType);

        if (lower == "socialclass") {
            return manager.AddToSocialClassFaction(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else if (lower == "skilltype") {
            return manager.AddToSkillTypeFaction(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else if (lower == "temperament") {
            return manager.AddToTemperamentFaction(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else {
            SKSE::log::warn("AddToFaction: invalid faction type '{}'", factionType);
            return false;
        }
    }

    // ========================================
    // Consolidated status checks
    // ========================================

    bool IsNPCStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType) {
        if (!npc) {
            SKSE::log::warn("IsNPCStatus: null actor provided");
            return false;
        }

        // Special case for "any"
        if (ToLower(statusType) == "any") {
            auto& manager = NPCRelationshipManager::GetSingleton();
            return manager.IsRegistered(npc->GetFormID());
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
        auto& manager = NPCRelationshipManager::GetSingleton();
        switch (status) {
            case RelationshipStatus::Candidate:
                return manager.IsCandidate(npc->GetFormID());
            case RelationshipStatus::Engaged:
                return manager.IsEngaged(npc->GetFormID());
            case RelationshipStatus::Married:
                return manager.IsMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:
                return manager.IsDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:
                return manager.IsJilted(npc->GetFormID());
            case RelationshipStatus::Deceased:
                return manager.IsDeceased(npc->GetFormID());
        }
        return false;
    }

    bool IsNPCStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum) {
        if (!npc) {
            SKSE::log::warn("IsNPCStatusByEnum: null actor provided");
            return false;
        }

        // Special case for -1 = "any"
        if (statusEnum == -1) {
            auto& manager = NPCRelationshipManager::GetSingleton();
            return manager.IsRegistered(npc->GetFormID());
        }

        if (statusEnum < 0 || statusEnum > 5) {
            SKSE::log::warn("IsNPCStatusByEnum: invalid status enum {}", statusEnum);
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate:
                return manager.IsCandidate(npc->GetFormID());
            case RelationshipStatus::Engaged:
                return manager.IsEngaged(npc->GetFormID());
            case RelationshipStatus::Married:
                return manager.IsMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:
                return manager.IsDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:
                return manager.IsJilted(npc->GetFormID());
            case RelationshipStatus::Deceased:
                return manager.IsDeceased(npc->GetFormID());
        }
        return false;
    }

    // ========================================
    // Consolidated status transitions
    // ========================================

    bool PromoteNPCToStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType) {
        if (!npc) {
            SKSE::log::warn("PromoteNPCToStatus: null actor provided");
            return false;
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
        auto& manager = NPCRelationshipManager::GetSingleton();
        switch (status) {
            case RelationshipStatus::Candidate:
                return manager.DemoteToCandidate(npc->GetFormID());
            case RelationshipStatus::Engaged:
                return manager.PromoteToEngaged(npc->GetFormID());
            case RelationshipStatus::Married:
                return manager.PromoteToMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:
                return manager.PromoteToDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:
                return manager.PromoteToJilted(npc->GetFormID());
            case RelationshipStatus::Deceased:
                return manager.PromoteToDeceased(npc->GetFormID());
        }
        return false;
    }

    bool PromoteNPCToStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum) {
        if (!npc) {
            SKSE::log::warn("PromoteNPCToStatusByEnum: null actor provided");
            return false;
        }

        if (statusEnum < 0 || statusEnum > 5) {
            SKSE::log::warn("PromoteNPCToStatusByEnum: invalid status enum {}", statusEnum);
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate:
                return manager.DemoteToCandidate(npc->GetFormID());
            case RelationshipStatus::Engaged:
                return manager.PromoteToEngaged(npc->GetFormID());
            case RelationshipStatus::Married:
                return manager.PromoteToMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:
                return manager.PromoteToDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:
                return manager.PromoteToJilted(npc->GetFormID());
            case RelationshipStatus::Deceased:
                return manager.PromoteToDeceased(npc->GetFormID());
        }
        return false;
    }

    // ========================================
    // Consolidated bulk retrieval
    // ========================================

    // Helper function to convert FormID vector to Actor array
    std::vector<RE::Actor*> ConvertFormIDsToActors(const std::vector<FormID>& formIDs) {
        std::vector<RE::Actor*> actors;
        actors.reserve(formIDs.size());

        for (auto formID : formIDs) {
            if (auto form = RE::TESForm::LookupByID(formID)) {
                if (auto actor = form->As<RE::Actor>()) {
                    actors.push_back(actor);
                }
            }
        }

        return actors;
    }

    std::vector<RE::Actor*> GetNPCsByStatus(RE::StaticFunctionTag*, std::string statusType) {
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for "all"
        if (ToLower(statusType) == "all") {
            return ConvertFormIDsToActors(manager.GetAllRegisteredNPCs());
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
        switch (status) {
            case RelationshipStatus::Candidate:
                return ConvertFormIDsToActors(manager.GetAllCandidates());
            case RelationshipStatus::Engaged:
                return ConvertFormIDsToActors(manager.GetAllEngaged());
            case RelationshipStatus::Married:
                return ConvertFormIDsToActors(manager.GetAllMarried());
            case RelationshipStatus::Divorced:
                return ConvertFormIDsToActors(manager.GetAllDivorced());
            case RelationshipStatus::Jilted:
                return ConvertFormIDsToActors(manager.GetAllJilted());
            case RelationshipStatus::Deceased:
                return ConvertFormIDsToActors(manager.GetAllDeceased());
        }
        return {};
    }

    std::vector<RE::Actor*> GetNPCsByStatusEnum(RE::StaticFunctionTag*, std::int32_t statusEnum) {
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for -1 = "all"
        if (statusEnum == -1) {
            return ConvertFormIDsToActors(manager.GetAllRegisteredNPCs());
        }

        if (statusEnum < 0 || statusEnum > 5) {
            SKSE::log::warn("GetNPCsByStatusEnum: invalid status enum {}", statusEnum);
            return {};
        }

        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate:
                return ConvertFormIDsToActors(manager.GetAllCandidates());
            case RelationshipStatus::Engaged:
                return ConvertFormIDsToActors(manager.GetAllEngaged());
            case RelationshipStatus::Married:
                return ConvertFormIDsToActors(manager.GetAllMarried());
            case RelationshipStatus::Divorced:
                return ConvertFormIDsToActors(manager.GetAllDivorced());
            case RelationshipStatus::Jilted:
                return ConvertFormIDsToActors(manager.GetAllJilted());
            case RelationshipStatus::Deceased:
                return ConvertFormIDsToActors(manager.GetAllDeceased());
        }
        return {};
    }

    // ========================================
    // Consolidated statistics
    // ========================================

    int GetStatusCount(RE::StaticFunctionTag*, std::string statusType) {
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for "all"
        if (ToLower(statusType) == "all") {
            return static_cast<int>(manager.GetTotalRegisteredCount());
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
        switch (status) {
            case RelationshipStatus::Candidate:
                return static_cast<int>(manager.GetCandidateCount());
            case RelationshipStatus::Engaged:
                return static_cast<int>(manager.GetEngagedCount());
            case RelationshipStatus::Married:
                return static_cast<int>(manager.GetMarriedCount());
            case RelationshipStatus::Divorced:
                return static_cast<int>(manager.GetDivorcedCount());
            case RelationshipStatus::Jilted:
                return static_cast<int>(manager.GetJiltedCount());
            case RelationshipStatus::Deceased:
                return static_cast<int>(manager.GetDeceasedCount());
        }
        return 0;
    }

    int GetStatusCountByEnum(RE::StaticFunctionTag*, std::int32_t statusEnum) {
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for -1 = "all"
        if (statusEnum == -1) {
            return static_cast<int>(manager.GetTotalRegisteredCount());
        }

        if (statusEnum < 0 || statusEnum > 5) {
            SKSE::log::warn("GetStatusCountByEnum: invalid status enum {}", statusEnum);
            return 0;
        }

        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Candidate:
                return static_cast<int>(manager.GetCandidateCount());
            case RelationshipStatus::Engaged:
                return static_cast<int>(manager.GetEngagedCount());
            case RelationshipStatus::Married:
                return static_cast<int>(manager.GetMarriedCount());
            case RelationshipStatus::Divorced:
                return static_cast<int>(manager.GetDivorcedCount());
            case RelationshipStatus::Jilted:
                return static_cast<int>(manager.GetJiltedCount());
            case RelationshipStatus::Deceased:
                return static_cast<int>(manager.GetDeceasedCount());
        }
        return 0;
    }

    // ========================================
    // Debug functions
    // ========================================

    void LogNPCStatistics(RE::StaticFunctionTag*) {
        auto& manager = NPCRelationshipManager::GetSingleton();
        manager.LogStatistics();
    }

    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            SKSE::log::warn("LogNPCDetails called with null NPC");
            return;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        manager.LogNPCDetails(npc->GetFormID());
    }

    // ========================================
    // Spouse hierarchy bindings
    // ========================================

    bool SetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t rank) {
        if (!npc) {
            SKSE::log::warn("SetHierarchyRank called with null actor");
            return false;
        }

        auto& mgr = MARAS::SpouseHierarchyManager::GetSingleton();
        return mgr.SetRank(npc->GetFormID(), static_cast<int>(rank));
    }

    std::int32_t GetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            SKSE::log::warn("GetHierarchyRank called with null actor");
            return 4;
        }

        auto& mgr = MARAS::SpouseHierarchyManager::GetSingleton();
        return mgr.GetRank(npc->GetFormID());
    }

    // ========================================
    // Marriage Difficulty Calculation
    // ========================================

    float CalculateMarriageSuccessChance(RE::StaticFunctionTag*, RE::Actor* npc, float intimacyAdjustment,
                                         float mostGold, float housesOwned, float horsesOwned, float questsCompleted,
                                         float dungeonsCleared, float dragonSoulsCollected) {
        if (!npc) {
            SKSE::log::warn("CalculateMarriageSuccessChance called with null NPC");
            return 0.0f;
        }

        return MarriageDifficulty::CalculateMarriageSuccessChance(npc, intimacyAdjustment, mostGold, housesOwned,
                                                                  horsesOwned, questsCompleted, dungeonsCleared,
                                                                  dragonSoulsCollected);
    }

    // ========================================
    // Spouse buff/service bindings
    // ========================================

    float GetSpouseMultiplier(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            SKSE::log::warn("GetSpouseMultiplier called with null actor");
            return 0.0f;
        }

        return MARAS::SpouseBuffService::GetSpouseMultiplier(spouse);
    }

    std::vector<float> GetFollowersMultipliers(RE::StaticFunctionTag*, std::vector<RE::Actor*> followers) {
        return MARAS::SpouseBuffService::GetFollowersMultipliers(followers);
    }

    std::vector<float> GetPermanentMultipliers(RE::StaticFunctionTag*) {
        return MARAS::SpouseBuffService::GetPermanentMultipliers();
    }

    // ========================================
    // Registration function for SKSE
    // ========================================

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
        // Core registration functions
        vm->RegisterFunction("RegisterCandidate", "MARAS", RegisterCandidate);
        vm->RegisterFunction("UnregisterNPC", "MARAS", UnregisterNPC);

        // Consolidated faction management functions
        vm->RegisterFunction("AddToFaction", "MARAS", AddToFaction);

        // Consolidated status check functions
        vm->RegisterFunction("IsNPCStatus", "MARAS", IsNPCStatus);
        vm->RegisterFunction("IsNPCStatusByEnum", "MARAS", IsNPCStatusByEnum);

        // Consolidated status transition functions
        vm->RegisterFunction("PromoteNPCToStatus", "MARAS", PromoteNPCToStatus);
        vm->RegisterFunction("PromoteNPCToStatusByEnum", "MARAS", PromoteNPCToStatusByEnum);

        // Consolidated bulk retrieval functions
        vm->RegisterFunction("GetNPCsByStatus", "MARAS", GetNPCsByStatus);
        vm->RegisterFunction("GetNPCsByStatusEnum", "MARAS", GetNPCsByStatusEnum);

        // Consolidated statistics functions
        vm->RegisterFunction("GetStatusCount", "MARAS", GetStatusCount);
        vm->RegisterFunction("GetStatusCountByEnum", "MARAS", GetStatusCountByEnum);

        // Debug functions
        vm->RegisterFunction("LogNPCStatistics", "MARAS", LogNPCStatistics);
        vm->RegisterFunction("LogNPCDetails", "MARAS", LogNPCDetails);

        // Marriage difficulty calculation
        vm->RegisterFunction("CalculateMarriageSuccessChance", "MARAS", CalculateMarriageSuccessChance);

        // Spouse hierarchy
        vm->RegisterFunction("SetHierarchyRank", "MARAS", SetHierarchyRank);
        vm->RegisterFunction("GetHierarchyRank", "MARAS", GetHierarchyRank);

        // Spouse buff/service bindings
        vm->RegisterFunction("GetSpouseMultiplier", "MARAS", GetSpouseMultiplier);
        vm->RegisterFunction("GetFollowersMultipliers", "MARAS", GetFollowersMultipliers);
        vm->RegisterFunction("GetPermanentMultipliers", "MARAS", GetPermanentMultipliers);
        SKSE::log::info("Registered {} MARAS Papyrus functions", 23);
        return true;
    }

}  // namespace MARAS::PapyrusInterface
