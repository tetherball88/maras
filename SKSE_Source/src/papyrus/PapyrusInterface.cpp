#include "papyrus/PapyrusInterface.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

#include "core/AffectionService.h"
#include "core/BonusesService.h"
#include "core/HomeCellService.h"
#include "core/LoggingService.h"
#include "core/MarriageDifficulty.h"
#include "core/PlayerHouseService.h"
#include "core/PollingService.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseBuffService.h"
#include "core/SpouseHierarchyManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

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
            MARAS_LOG_WARN("RegisterCandidate: null actor provided");
            return false;
        }

        // Defensive validation: ensure we have a valid actor base/form so we don't
        // cause the engine to deref invalid pointers later (observed in VR build).
        if (!npc->GetActorBase()) {
            MARAS_LOG_WARN("RegisterCandidate: actor has no base data (possible invalid actor) for form {:08X}",
                           npc->GetFormID());
            return false;
        }

        // Avoid registering the player or invalid actor refs
        if (npc->IsPlayerRef()) {
            MARAS_LOG_WARN("RegisterCandidate: cannot register player (form {:08X}) as candidate", npc->GetFormID());
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        bool result = false;
        try {
            result = manager.RegisterAsCandidate(npc->GetFormID());
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("RegisterCandidate: exception when registering {:08X}: {}", npc->GetFormID(), e.what());
            result = false;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO("Papyrus RegisterCandidate for {} completed in {} microseconds (success: {})",
                       npc->GetDisplayFullName(), duration.count(), result);

        return result;
    }

    bool UnregisterNPC(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("UnregisterNPC: null actor provided");
            return false;
        }

        if (!npc->GetActorBase()) {
            MARAS_LOG_WARN("UnregisterNPC: actor has no base data (possible invalid actor) for form {:08X}",
                           npc->GetFormID());
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        return manager.UnregisterNPC(npc->GetFormID());
    }

    // ========================================
    // Consolidated faction management
    // ========================================

    bool SetNpcCharacteristics(RE::StaticFunctionTag*, RE::Actor* npc, std::string factionType, std::int32_t rank) {
        if (!npc) {
            MARAS_LOG_WARN("SetNpcCharacteristics: null actor provided");
            return false;
        }

        // New behavior: change the corresponding attribute on the NPC (and update factions)
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = ToLower(factionType);

        if (lower == "socialclass") {
            return manager.SetSocialClass(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else if (lower == "skilltype") {
            return manager.SetSkillType(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else if (lower == "temperament") {
            return manager.SetTemperament(npc->GetFormID(), static_cast<std::int8_t>(rank));
        } else {
            MARAS_LOG_WARN("SetNpcCharacteristics (attribute change): invalid type '{}'", factionType);
            return false;
        }
    }

    // ========================================
    // Consolidated status checks
    // ========================================

    bool IsNPCStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType) {
        if (!npc) {
            MARAS_LOG_WARN("IsNPCStatus: null actor provided");
            return false;
        }

        auto lower = ToLower(statusType);
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for "any"
        if (lower == "any") {
            return manager.IsRegistered(npc->GetFormID());
        }

        auto status = Utils::StringToRelationshipStatus(statusType);
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
            default:
                return false;
        }
    }

    bool IsNPCStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum) {
        if (!npc) {
            MARAS_LOG_WARN("IsNPCStatusByEnum: null actor provided");
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for -1 = "any"
        if (statusEnum == -1) {
            return manager.IsRegistered(npc->GetFormID());
        }

        if (statusEnum < 0 || statusEnum > 4) {
            MARAS_LOG_WARN("IsNPCStatusByEnum: invalid status enum {}", statusEnum);
            return false;
        }

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
            default:
                return false;
        }
    }

    // ========================================
    // Consolidated status transitions
    // ========================================

    bool PromoteNPCToStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType) {
        if (!npc) {
            MARAS_LOG_WARN("PromoteNPCToStatus: null actor provided");
            return false;
        }

        auto lower = ToLower(statusType);
        auto& manager = NPCRelationshipManager::GetSingleton();

        auto status = Utils::StringToRelationshipStatus(statusType);
        switch (status) {
            case RelationshipStatus::Engaged:
                return manager.PromoteToEngaged(npc->GetFormID());
            case RelationshipStatus::Married:
                return manager.PromoteToMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:
                return manager.PromoteToDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:
                return manager.PromoteToJilted(npc->GetFormID());
            default:
                MARAS_LOG_WARN("PromoteNPCToStatus: invalid status '{}'", statusType);
                return false;
        }
    }

    bool PromoteNPCToStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum) {
        if (!npc) {
            MARAS_LOG_WARN("PromoteNPCToStatusByEnum: null actor provided");
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();

        if (statusEnum < 1 || statusEnum > 4) {
            MARAS_LOG_WARN("PromoteNPCToStatusByEnum: invalid status enum {}", statusEnum);
            return false;
        }

        switch (static_cast<RelationshipStatus>(statusEnum)) {
            case RelationshipStatus::Engaged:
                return manager.PromoteToEngaged(npc->GetFormID());
            case RelationshipStatus::Married:
                return manager.PromoteToMarried(npc->GetFormID());
            case RelationshipStatus::Divorced:
                return manager.PromoteToDivorced(npc->GetFormID());
            case RelationshipStatus::Jilted:
                return manager.PromoteToJilted(npc->GetFormID());
            default:
                return false;
        }
    }

    // ========================================
    // Consolidated bulk retrieval
    // ========================================

    // Helper function to convert FormID vector to Actor array
    std::vector<RE::Actor*> ConvertFormIDsToActors(const std::vector<RE::FormID>& formIDs) {
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
        auto lower = ToLower(statusType);

        // Special case for "all"
        if (lower == "all") {
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
            default:
                return {};
        }
    }

    std::vector<RE::Actor*> GetNPCsByStatusEnum(RE::StaticFunctionTag*, std::int32_t statusEnum) {
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for -1 = "all"
        if (statusEnum == -1) {
            return ConvertFormIDsToActors(manager.GetAllRegisteredNPCs());
        }

        if (statusEnum < 0 || statusEnum > 4) {
            MARAS_LOG_WARN("GetNPCsByStatusEnum: invalid status enum {}", statusEnum);
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
            default:
                return {};
        }
    }

    // Returns the currently-detected teammates (actors following the player or flagged as teammates)
    std::vector<RE::Actor*> GetCurrentTeammates(RE::StaticFunctionTag*) {
        std::vector<RE::Actor*> result;

        auto idsSet = MARAS::PollingService::GetSingleton().GetCurrentTeammates();
        result.reserve(idsSet.size());

        for (auto id : idsSet) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto actor = form->As<RE::Actor>()) result.push_back(actor);
            }
        }

        return result;
    }

    // Check if a specific actor is currently a player teammate
    bool IsPlayerTeammate(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("IsPlayerTeammate: null actor provided");
            return false;
        }

        return MARAS::PollingService::GetSingleton().IsPlayerTeammate(npc);
    }

    // ========================================
    // Consolidated statistics
    // ========================================

    int GetStatusCount(RE::StaticFunctionTag*, std::string statusType) {
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = ToLower(statusType);

        // Special case for "all"
        if (lower == "all") {
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
            default:
                return 0;
        }
    }

    int GetStatusCountByEnum(RE::StaticFunctionTag*, std::int32_t statusEnum) {
        auto& manager = NPCRelationshipManager::GetSingleton();

        // Special case for -1 = "all"
        if (statusEnum == -1) {
            return static_cast<int>(manager.GetTotalRegisteredCount());
        }

        if (statusEnum < 0 || statusEnum > 4) {
            MARAS_LOG_WARN("GetStatusCountByEnum: invalid status enum {}", statusEnum);
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
            default:
                return 0;
        }
    }

    // ========================================
    // Debug functions
    // ========================================

    void Log(RE::StaticFunctionTag*, std::string msg, std::int32_t logLevel) {
        switch (logLevel) {
            case 0:  // trace
                MARAS_LOG_TRACE("Papyrus: {}", msg);
                break;
            case 1:  // debug
                MARAS_LOG_DEBUG("Papyrus: {}", msg);
                break;
            case 2:  // warning
                MARAS_LOG_WARN("Papyrus: {}", msg);
                break;
            case 3:  // error
                MARAS_LOG_ERROR("Papyrus: {}", msg);
                break;
            default:  // none or invalid
                break;
        }
    }

    void LogNPCStatistics(RE::StaticFunctionTag*) {
        auto& manager = NPCRelationshipManager::GetSingleton();
        manager.LogStatistics();
    }

    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("LogNPCDetails called with null NPC");
            return;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        manager.LogNPCDetails(npc->GetFormID());
    }

    void SetLogLevel(RE::StaticFunctionTag*, std::int32_t logLevel) {
        // Persist and apply via LoggingService
        MARAS::LoggingService::GetSingleton().SetLogLevel(static_cast<int32_t>(logLevel));
    }

    std::int32_t GetLogLevel(RE::StaticFunctionTag*) { return MARAS::LoggingService::GetSingleton().GetLogLevel(); }

    // ========================================
    // Spouse hierarchy bindings
    // ========================================

    bool SetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t rank) {
        if (!npc) {
            MARAS_LOG_WARN("SetHierarchyRank called with null actor");
            return false;
        }

        auto& mgr = MARAS::SpouseHierarchyManager::GetSingleton();
        return mgr.SetRank(npc->GetFormID(), static_cast<int>(rank));
    }

    std::int32_t GetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetHierarchyRank called with null actor");
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
                                         float dungeonsCleared, float dragonSoulsCollected, bool playerKiller) {
        if (!npc) {
            MARAS_LOG_WARN("CalculateMarriageSuccessChance called with null NPC");
            return 0.0f;
        }

        return MarriageDifficulty::CalculateMarriageSuccessChance(npc, intimacyAdjustment, mostGold, housesOwned,
                                                                  horsesOwned, questsCompleted, dungeonsCleared,
                                                                  dragonSoulsCollected, playerKiller);
    }

    // ========================================
    // Spouse buff/service bindings
    // ========================================

    float GetSpouseMultiplier(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("GetSpouseMultiplier called with null actor");
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
    // Affection system bindings
    // ========================================

    void AddAffection(RE::StaticFunctionTag*, RE::Actor* npc, float amount, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("AddAffection: null actor provided");
            return;
        }
        MARAS::AffectionService::GetSingleton().AddAffection(npc->GetFormID(), amount, type);
    }

    float GetAffection(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("GetAffection: null actor provided");
            return 0.0f;
        }
        return MARAS::AffectionService::GetSingleton().GetDailyAffection(npc->GetFormID(), type);
    }

    int GetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetPermanentAffection: null actor provided");
            return 0;
        }
        return MARAS::AffectionService::GetSingleton().GetPermanentAffection(npc->GetFormID());
    }

    void SetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc, int amount) {
        if (!npc) {
            MARAS_LOG_WARN("SetPermanentAffection: null actor provided");
            return;
        }
        MARAS::AffectionService::GetSingleton().SetPermanentAffection(npc->GetFormID(), amount);
    }

    void SetAffectionMinMax(RE::StaticFunctionTag*, std::string type, int minVal, int maxVal) {
        MARAS::AffectionService::GetSingleton().SetAffectionMinMax(type, minVal, maxVal);
    }

    void ApplyDailyAffection(RE::StaticFunctionTag*) {
        MARAS::AffectionService::GetSingleton().ApplyDailyAffectionsForAll();
    }

    void SetAffectionDecayMultiplier(RE::StaticFunctionTag*, float multiplier) {
        MARAS::AffectionService::GetSingleton().SetDecayMultiplier(multiplier);
    }

    float GetAffectionDecayMultiplier(RE::StaticFunctionTag*) {
        return MARAS::AffectionService::GetSingleton().GetDecayMultiplier();
    }

    // ========================================
    // Player house bindings
    // ========================================

    bool RegisterPlayerHouseCell(RE::StaticFunctionTag*, RE::BGSLocation* loc, RE::TESObjectREFR* homeMarker) {
        if (!loc) {
            MARAS_LOG_WARN("RegisterPlayerHouseCell: null location provided");
            return false;
        }

        RE::FormID markerId = 0;
        if (homeMarker) markerId = homeMarker->GetFormID();

        return MARAS::PlayerHouseService::GetSingleton().RegisterPlayerHouseCell(loc->GetFormID(), markerId);
    }

    std::vector<RE::BGSLocation*> GetAllPlayerHouses(RE::StaticFunctionTag*) {
        std::vector<RE::BGSLocation*> result;
        auto ids = MARAS::PlayerHouseService::GetSingleton().GetAllPlayerHouses();
        result.reserve(ids.size());
        for (auto id : ids) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto loc = form->As<RE::BGSLocation>()) result.push_back(loc);
            }
        }
        return result;
    }

    bool RegisterTenantInPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse, RE::BGSLocation* playerHouse) {
        if (!spouse || !playerHouse) {
            MARAS_LOG_WARN("RegisterTenantInPlayerHouse: null argument(s)");
            return false;
        }
        return MARAS::PlayerHouseService::GetSingleton().RegisterTenantInPlayerHouse(spouse->GetFormID(),
                                                                                     playerHouse->GetFormID());
    }

    bool RemoveTenantFromPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("RemoveTenantFromPlayerHouse: null spouse");
            return false;
        }
        return MARAS::PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(spouse->GetFormID());
    }

    std::vector<RE::Actor*> GetPlayerHouseTenants(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) return {};
        std::vector<RE::Actor*> result;
        auto ids = MARAS::PlayerHouseService::GetSingleton().GetPlayerHouseTenants(playerHouse->GetFormID());
        result.reserve(ids.size());
        for (auto id : ids) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto actor = form->As<RE::Actor>()) result.push_back(actor);
            }
        }
        return result;
    }

    // Returns the stored marker object (base object) for a given player house, or nullptr
    RE::TESObjectREFR* GetHouseMarker(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) return nullptr;
        RE::FormID markerId = MARAS::PlayerHouseService::GetSingleton().GetHouseMarkerFormID(playerHouse->GetFormID());
        if (markerId == 0) return nullptr;
        if (auto form = RE::TESForm::LookupByID(markerId)) {
            return form->As<RE::TESObjectREFR>();
        }
        return nullptr;
    }

    // Returns an array of player house names
    std::vector<std::string> GetAllPlayerHousesNames(RE::StaticFunctionTag*) {
        return MARAS::PlayerHouseService::GetSingleton().GetAllPlayerHousesNames();
    }

    // ========================================
    // Spouse assets bindings
    // ========================================

    std::vector<RE::TESObjectREFR*> GetCellDoors(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell) {
        if (!currentCell) return {};
        std::vector<RE::TESObjectREFR*> result;
        auto doorIDs = MARAS::HomeCellService::GetSingleton().GetCellDoors(currentCell->GetFormID());
        result.reserve(doorIDs.size());
        for (auto id : doorIDs) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto ref = form->As<RE::TESObjectREFR>()) result.push_back(ref);
            }
        }
        return result;
    }

    // Returns the original cell (interior) that an NPC was recorded in when the index was built, or nullptr
    RE::TESObjectCELL* GetNpcOriginalHouse(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return nullptr;
        RE::FormID cellId = MARAS::HomeCellService::GetSingleton().GetNpcHome(npc);
        if (cellId == 0) return nullptr;
        if (auto form = RE::TESForm::LookupByID(cellId)) {
            return form->As<RE::TESObjectCELL>();
        }
        return nullptr;
    }

    // Returns the LocationCenterMarker reference of an NPC's original home, or nullptr
    RE::TESObjectREFR* GetNpcOriginalHouseCenterMarker(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcOriginalHouseCenterMarker: null NPC provided");
            return nullptr;
        }

        RE::FormID markerId = MARAS::HomeCellService::GetSingleton().GetNpcOriginalHouseCenterMarker(npc);
        MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker (Papyrus): NPC {:08X} -> Marker {:08X}", npc->GetFormID(),
                       markerId);

        if (markerId == 0) {
            MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker: No marker found for NPC {:08X}", npc->GetFormID());
            return nullptr;
        }

        auto form = RE::TESForm::LookupByID(markerId);
        if (!form) {
            MARAS_LOG_WARN("GetNpcOriginalHouseCenterMarker: Failed to lookup form {:08X}", markerId);
            return nullptr;
        }

        auto ref = form->As<RE::TESObjectREFR>();
        if (!ref) {
            MARAS_LOG_WARN("GetNpcOriginalHouseCenterMarker: Form {:08X} is not a TESObjectREFR", markerId);
            return nullptr;
        }

        MARAS_LOG_INFO("GetNpcOriginalHouseCenterMarker: Returning reference {:08X}", ref->GetFormID());
        return ref;
    }

    // Returns a list of bed references that were recorded as owned by the given NPC
    std::vector<RE::TESObjectREFR*> GetNpcBeds(RE::StaticFunctionTag*, RE::Actor* npc) {
        std::vector<RE::TESObjectREFR*> result;
        if (!npc) return result;

        auto bedIDs = MARAS::HomeCellService::GetSingleton().GetNpcBeds(npc);
        result.reserve(bedIDs.size());
        for (auto id : bedIDs) {
            if (auto form = RE::TESForm::LookupByID(id)) {
                if (auto ref = form->As<RE::TESObjectREFR>()) result.push_back(ref);
            }
        }
        return result;
    }

    bool ShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("ShareHouseWithPlayer: null argument(s)");
            return false;
        }
        // Service will look up the NPC's home via HomeCellService; pass the actor pointer
        return MARAS::SpouseAssetsService::GetSingleton().ShareHouseWithPlayer(npc->GetFormID());
    }

    bool StopShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("StopShareHouseWithPlayer: null npc");
            return false;
        }
        return MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npc->GetFormID());
    }

    bool IsHouseSharedWithPlayer(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell) {
        if (!currentCell) return false;
        return MARAS::SpouseAssetsService::GetSingleton().IsHouseSharedWithPlayer(currentCell->GetFormID());
    }

    // Returns whether the specified spouse's original home is currently shared with the player
    bool HasSpouseSharedHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            MARAS_LOG_WARN("HasSpouseSharedHouseWithPlayer: null spouse provided");
            return false;
        }

        RE::FormID houseId = MARAS::HomeCellService::GetSingleton().GetNpcHome(spouse);
        if (houseId == 0) return false;
        return MARAS::SpouseAssetsService::GetSingleton().IsHouseSharedWithPlayer(houseId);
    }

    // Returns the house location for a tenant, or nullptr
    RE::BGSLocation* GetTenantHouse(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) return nullptr;
        RE::FormID houseId = MARAS::PlayerHouseService::GetSingleton().GetTenantHouseFormID(npc->GetFormID());
        if (houseId == 0) return nullptr;
        if (auto form = RE::TESForm::LookupByID(houseId)) {
            if (auto loc = form->As<RE::BGSLocation>()) return loc;
        }
        return nullptr;
    }

    int CountPlayerHouses(RE::StaticFunctionTag*) {
        return MARAS::PlayerHouseService::GetSingleton().CountPlayerHouses();
    }

    // ========================================
    // Home marker management (replaces PO3's SetLinkedRef and StorageUtil)
    // ========================================

    RE::TESObjectREFR* GetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            return nullptr;
        }

        auto markerOpt = NPCRelationshipManager::GetSingleton().GetHomeMarker(npc->GetFormID());
        return markerOpt.has_value() ? RE::TESForm::LookupByID<RE::TESObjectREFR>(markerOpt.value()) : nullptr;
    }

    bool SetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc, RE::TESObjectREFR* marker) {
        if (!npc) {
            MARAS_LOG_WARN("SetTrackedNpcHomeMarker: null NPC provided");
            return false;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        RE::FormID markerID = marker ? marker->GetFormID() : 0;
        return manager.SetHomeMarker(npc->GetFormID(), markerID);
    }

    // ========================================
    // NPC Type and Status Queries
    // ========================================

    std::string GetNpcStatusName(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcStatusName: null actor provided");
            return "Unknown";
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        auto status = manager.GetRelationshipStatus(npc->GetFormID());
        return std::string(Utils::RelationshipStatusToString(status));
    }

    std::int32_t GetNpcStatusEnum(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcStatusEnum: null actor provided");
            return -1;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        if (!manager.IsRegistered(npc->GetFormID())) {
            return -1;
        }

        auto status = manager.GetRelationshipStatus(npc->GetFormID());
        return static_cast<std::int32_t>(status);
    }

    std::vector<std::string> GetNpcTypes(RE::StaticFunctionTag*, std::string type) {
        auto lower = ToLower(type);

        if (lower == "socialclass") {
            return {ToLower(Utils::SocialClassToString(SocialClass::Outcast)),
                    ToLower(Utils::SocialClassToString(SocialClass::Poverty)),
                    ToLower(Utils::SocialClassToString(SocialClass::Working)),
                    ToLower(Utils::SocialClassToString(SocialClass::Middle)),
                    ToLower(Utils::SocialClassToString(SocialClass::Wealthy)),
                    ToLower(Utils::SocialClassToString(SocialClass::Religious)),
                    ToLower(Utils::SocialClassToString(SocialClass::Nobles)),
                    ToLower(Utils::SocialClassToString(SocialClass::Rulers))};
        } else if (lower == "skilltype") {
            return {ToLower(Utils::SkillTypeToString(SkillType::Warrior)),
                    ToLower(Utils::SkillTypeToString(SkillType::Mage)),
                    ToLower(Utils::SkillTypeToString(SkillType::Rogue)),
                    ToLower(Utils::SkillTypeToString(SkillType::Craftsman)),
                    ToLower(Utils::SkillTypeToString(SkillType::Ranger)),
                    ToLower(Utils::SkillTypeToString(SkillType::Orator))};
        } else if (lower == "temperament") {
            return {ToLower(Utils::TemperamentToString(Temperament::Proud)),
                    ToLower(Utils::TemperamentToString(Temperament::Humble)),
                    ToLower(Utils::TemperamentToString(Temperament::Jealous)),
                    ToLower(Utils::TemperamentToString(Temperament::Romantic)),
                    ToLower(Utils::TemperamentToString(Temperament::Independent))};
        }

        MARAS_LOG_WARN("GetNpcTypes: invalid type '{}'", type);
        return {};
    }

    std::int32_t GetNpcTypeEnum(RE::StaticFunctionTag*, std::string type, std::string name) {
        auto lower = ToLower(type);

        if (lower == "socialclass") {
            auto socialClass = Utils::StringToSocialClass(name);
            return static_cast<std::int32_t>(socialClass);
        } else if (lower == "skilltype") {
            auto skillType = Utils::StringToSkillType(name);
            return static_cast<std::int32_t>(skillType);
        } else if (lower == "temperament") {
            auto temperament = Utils::StringToTemperament(name);
            return static_cast<std::int32_t>(temperament);
        }

        MARAS_LOG_WARN("GetNpcTypeEnum: invalid type '{}'", type);
        return -1;
    }

    std::int32_t GetNpcCurrentTypeEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcCurrentTypeEnum: null actor provided");
            return -1;
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = ToLower(type);

        if (lower == "socialclass") {
            auto socialClass = manager.GetSocialClass(npc->GetFormID());
            return static_cast<std::int32_t>(socialClass);
        } else if (lower == "skilltype") {
            auto skillType = manager.GetSkillType(npc->GetFormID());
            return static_cast<std::int32_t>(skillType);
        } else if (lower == "temperament") {
            auto temperament = manager.GetTemperament(npc->GetFormID());
            return static_cast<std::int32_t>(temperament);
        }

        MARAS_LOG_WARN("GetNpcCurrentTypeEnum: invalid type '{}'", type);
        return -1;
    }

    std::string GetNpcCurrentTypeName(RE::StaticFunctionTag*, RE::Actor* npc, std::string type) {
        if (!npc) {
            MARAS_LOG_WARN("GetNpcCurrentTypeName: null actor provided");
            return "Unknown";
        }

        auto& manager = NPCRelationshipManager::GetSingleton();
        auto lower = ToLower(type);

        if (lower == "socialclass") {
            auto socialClass = manager.GetSocialClass(npc->GetFormID());
            return std::string(Utils::SocialClassToString(socialClass));
        } else if (lower == "skilltype") {
            auto skillType = manager.GetSkillType(npc->GetFormID());
            return std::string(Utils::SkillTypeToString(skillType));
        } else if (lower == "temperament") {
            auto temperament = manager.GetTemperament(npc->GetFormID());
            return std::string(Utils::TemperamentToString(temperament));
        }

        MARAS_LOG_WARN("GetNpcCurrentTypeName: invalid type '{}'", type);
        return "Unknown";
    }

    // ========================================
    // Determination helpers (expose determiner to Papyrus)
    // ========================================

    std::int32_t DetermineSocialClass(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("DetermineSocialClass: null actor provided");
            return static_cast<std::int32_t>(SocialClass::Outcast);
        }
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto sc = manager.DetermineSocialClass(npc->GetFormID());
        return static_cast<std::int32_t>(sc);
    }

    std::int32_t DetermineSkillType(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("DetermineSkillType: null actor provided");
            return static_cast<std::int32_t>(SkillType::Warrior);
        }
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto st = manager.DetermineSkillType(npc->GetFormID());
        return static_cast<std::int32_t>(st);
    }

    std::int32_t DetermineTemperament(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("DetermineTemperament: null actor provided");
            return static_cast<std::int32_t>(Temperament::Proud);
        }
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto t = manager.DetermineTemperament(npc->GetFormID());
        return static_cast<std::int32_t>(t);
    }

    // ========================================
    // Bonuses JSON runtime accessors
    // ========================================

    int GetBonusCount(RE::StaticFunctionTag*, std::string type) {
        // normalize to lowercase for lookup
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        return MARAS::BonusesService::GetSingleton().GetBonusCount(type);
    }

    RE::BGSPerk* GetBonusPerk(RE::StaticFunctionTag*, std::string type, int index) {
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(type, index);
        if (!be) return nullptr;

        if (be->perk.empty()) return nullptr;

        auto parsed = MARAS::Utils::ParseFormKey(be->perk);
        if (!parsed.isValid) return nullptr;

        return MARAS::Utils::LookupForm<RE::BGSPerk>(parsed.localFormID, parsed.pluginName);
    }

    int GetBonusEffectIndex(RE::StaticFunctionTag*, std::string type, int index) {
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(type, index);
        if (!be) return 0;
        return be->effectIndex;
    }

    float GetBonusPerkValue(RE::StaticFunctionTag*, std::string type, int index) {
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(type, index);
        if (!be) return 0.0f;
        return be->value;
    }

    std::string GetBonusPerkType(RE::StaticFunctionTag*, std::string type, int index) {
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(type, index);
        if (!be) return std::string();
        return be->type;
    }

    std::string GetBonusPerkUnit(RE::StaticFunctionTag*, std::string type, int index) {
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(type, index);
        if (!be) return std::string();
        return be->unit;
    }

    std::string GetBonusPerkDescription(RE::StaticFunctionTag*, std::string type, int index) {
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        const auto* be = MARAS::BonusesService::GetSingleton().GetBonusEntry(type, index);
        if (!be) return std::string();
        return be->description;
    }

    // ========================================
    // Registration function for SKSE
    // ========================================

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
        // Core registration functions
        vm->RegisterFunction("RegisterCandidate", "MARAS", RegisterCandidate);
        vm->RegisterFunction("UnregisterNPC", "MARAS", UnregisterNPC);

        // Consolidated faction management functions
        vm->RegisterFunction("SetNpcCharacteristics", "MARAS", SetNpcCharacteristics);

        // Consolidated status check functions
        vm->RegisterFunction("IsNPCStatus", "MARAS", IsNPCStatus);
        vm->RegisterFunction("IsNPCStatusByEnum", "MARAS", IsNPCStatusByEnum);

        // Consolidated status transition functions
        vm->RegisterFunction("PromoteNPCToStatus", "MARAS", PromoteNPCToStatus);
        vm->RegisterFunction("PromoteNPCToStatusByEnum", "MARAS", PromoteNPCToStatusByEnum);

        // Consolidated bulk retrieval functions
        vm->RegisterFunction("GetNPCsByStatus", "MARAS", GetNPCsByStatus);
        vm->RegisterFunction("GetNPCsByStatusEnum", "MARAS", GetNPCsByStatusEnum);
        vm->RegisterFunction("GetCurrentTeammates", "MARAS", GetCurrentTeammates);
        vm->RegisterFunction("IsPlayerTeammate", "MARAS", IsPlayerTeammate);

        // Consolidated statistics functions
        vm->RegisterFunction("GetStatusCount", "MARAS", GetStatusCount);
        vm->RegisterFunction("GetStatusCountByEnum", "MARAS", GetStatusCountByEnum);

        // Debug functions
        vm->RegisterFunction("LogNPCStatistics", "MARAS", LogNPCStatistics);
        vm->RegisterFunction("LogNPCDetails", "MARAS", LogNPCDetails);
        vm->RegisterFunction("Log", "MARAS", Log);
        vm->RegisterFunction("SetLogLevel", "MARAS", SetLogLevel);
        vm->RegisterFunction("GetLogLevel", "MARAS", GetLogLevel);

        // Marriage difficulty calculation
        vm->RegisterFunction("CalculateMarriageSuccessChance", "MARAS", CalculateMarriageSuccessChance);

        // Spouse hierarchy
        vm->RegisterFunction("SetHierarchyRank", "MARAS", SetHierarchyRank);
        vm->RegisterFunction("GetHierarchyRank", "MARAS", GetHierarchyRank);

        // Player house functions
        vm->RegisterFunction("RegisterPlayerHouseCell", "MARAS", RegisterPlayerHouseCell);
        vm->RegisterFunction("GetAllPlayerHouses", "MARAS", GetAllPlayerHouses);
        vm->RegisterFunction("GetAllPlayerHousesNames", "MARAS", GetAllPlayerHousesNames);
        vm->RegisterFunction("RegisterTenantInPlayerHouse", "MARAS", RegisterTenantInPlayerHouse);
        vm->RegisterFunction("RemoveTenantFromPlayerHouse", "MARAS", RemoveTenantFromPlayerHouse);
        vm->RegisterFunction("GetPlayerHouseTenants", "MARAS", GetPlayerHouseTenants);
        vm->RegisterFunction("GetHouseMarker", "MARAS", GetHouseMarker);
        vm->RegisterFunction("GetTenantHouse", "MARAS", GetTenantHouse);
        vm->RegisterFunction("CountPlayerHouses", "MARAS", CountPlayerHouses);

        // Spouse assets functions
        vm->RegisterFunction("GetCellDoors", "MARAS", GetCellDoors);
        vm->RegisterFunction("ShareHouseWithPlayer", "MARAS", ShareHouseWithPlayer);
        vm->RegisterFunction("StopShareHouseWithPlayer", "MARAS", StopShareHouseWithPlayer);
        vm->RegisterFunction("IsHouseSharedWithPlayer", "MARAS", IsHouseSharedWithPlayer);
        vm->RegisterFunction("HasSpouseSharedHouseWithPlayer", "MARAS", HasSpouseSharedHouseWithPlayer);

        // Home cell helpers
        vm->RegisterFunction("GetNpcOriginalHouse", "MARAS", GetNpcOriginalHouse);
        vm->RegisterFunction("GetNpcBeds", "MARAS", GetNpcBeds);
        vm->RegisterFunction("GetNpcOriginalHouseCenterMarker", "MARAS", GetNpcOriginalHouseCenterMarker);

        // Home marker management
        vm->RegisterFunction("GetTrackedNpcHomeMarker", "MARAS", GetTrackedNpcHomeMarker);
        vm->RegisterFunction("SetTrackedNpcHomeMarker", "MARAS", SetTrackedNpcHomeMarker);

        // Spouse buff/service bindings
        vm->RegisterFunction("GetSpouseMultiplier", "MARAS", GetSpouseMultiplier);
        vm->RegisterFunction("GetFollowersMultipliers", "MARAS", GetFollowersMultipliers);
        vm->RegisterFunction("GetPermanentMultipliers", "MARAS", GetPermanentMultipliers);
        // Affection system
        vm->RegisterFunction("AddAffection", "MARAS", AddAffection);
        vm->RegisterFunction("GetAffection", "MARAS", GetAffection);
        vm->RegisterFunction("GetPermanentAffection", "MARAS", GetPermanentAffection);
        vm->RegisterFunction("SetPermanentAffection", "MARAS", SetPermanentAffection);
        vm->RegisterFunction("SetAffectionMinMax", "MARAS", SetAffectionMinMax);
        vm->RegisterFunction("ApplyDailyAffection", "MARAS", ApplyDailyAffection);
        vm->RegisterFunction("SetAffectionDecayMultiplier", "MARAS", SetAffectionDecayMultiplier);
        vm->RegisterFunction("GetAffectionDecayMultiplier", "MARAS", GetAffectionDecayMultiplier);

        // NPC Type and Status Queries
        vm->RegisterFunction("GetNpcStatusName", "MARAS", GetNpcStatusName);
        vm->RegisterFunction("GetNpcStatusEnum", "MARAS", GetNpcStatusEnum);
        vm->RegisterFunction("GetNpcTypes", "MARAS", GetNpcTypes);
        vm->RegisterFunction("GetNpcTypeEnum", "MARAS", GetNpcTypeEnum);
        vm->RegisterFunction("GetNpcCurrentTypeEnum", "MARAS", GetNpcCurrentTypeEnum);
        vm->RegisterFunction("GetNpcCurrentTypeName", "MARAS", GetNpcCurrentTypeName);

        // Determination helpers
        vm->RegisterFunction("DetermineSocialClass", "MARAS", DetermineSocialClass);
        vm->RegisterFunction("DetermineSkillType", "MARAS", DetermineSkillType);
        vm->RegisterFunction("DetermineTemperament", "MARAS", DetermineTemperament);

        // Bonuses JSON accessors
        vm->RegisterFunction("GetBonusCount", "MARAS", GetBonusCount);
        vm->RegisterFunction("GetBonusPerk", "MARAS", GetBonusPerk);
        vm->RegisterFunction("GetBonusEffectIndex", "MARAS", GetBonusEffectIndex);
        vm->RegisterFunction("GetBonusPerkValue", "MARAS", GetBonusPerkValue);
        vm->RegisterFunction("GetBonusPerkType", "MARAS", GetBonusPerkType);
        vm->RegisterFunction("GetBonusPerkUnit", "MARAS", GetBonusPerkUnit);
        vm->RegisterFunction("GetBonusPerkDescription", "MARAS", GetBonusPerkDescription);

        MARAS_LOG_INFO("Registered {} MARAS Papyrus functions", 45);
        return true;
    }

}  // namespace MARAS::PapyrusInterface
