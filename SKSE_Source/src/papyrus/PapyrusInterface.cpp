#include "papyrus/PapyrusInterface.h"

#include "core/BanterCoordinatorService.h"
#include "core/ChildrenFleeService.h"
#include "core/IntimateThreadService.h"
#include "core/LoggingService.h"
#include "services/ActorService.h"
#include "ui/ConfirmationModal.h"
#include "ui/PrismaUIManager.h"
#include "utils/Common.h"
#include "utils/JsonService.h"

namespace MARAS::PapyrusInterface {

    // ========================================
    // Debug & logging functions
    // ========================================

    void Log(RE::StaticFunctionTag*, std::string msg, std::int32_t logLevel) {
        switch (logLevel) {
            case 0:
                MARAS_LOG_TRACE("Papyrus: {}", msg);
                break;
            case 1:
                MARAS_LOG_DEBUG("Papyrus: {}", msg);
                break;
            case 2:
                MARAS_LOG_WARN("Papyrus: {}", msg);
                break;
            case 3:
                MARAS_LOG_ERROR("Papyrus: {}", msg);
                break;
            default:
                break;
        }
    }

    void LogNPCStatistics(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        NPCRelationshipManager::GetSingleton().LogStatistics();
    }

    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc) {
        if (!npc) {
            MARAS_LOG_WARN("LogNPCDetails called with null NPC");
            return;
        }
        std::lock_guard lock(GetPapyrusMutex());
        NPCRelationshipManager::GetSingleton().LogNPCDetails(npc->GetFormID());
    }

    void SetLogLevel(RE::StaticFunctionTag*, std::int32_t logLevel) {
        MARAS::LoggingService::GetSingleton().SetLogLevel(static_cast<int32_t>(logLevel));
    }

    std::int32_t GetLogLevel(RE::StaticFunctionTag*) {
        return MARAS::LoggingService::GetSingleton().GetLogLevel();
    }

    // ========================================
    // Keyword management bindings
    // ========================================

    bool HasNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword) {
        if (!npc || !keyword) {
            MARAS_LOG_WARN("HasNpcKeyword: null argument(s) provided");
            return false;
        }
        return npc->HasKeyword(keyword);
    }

    bool AddNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword) {
        if (!npc || !keyword) {
            MARAS_LOG_WARN("AddNpcKeyword: null argument(s) provided");
            return false;
        }
        auto formID = npc->GetFormID();
        auto base = ActorSvc::GetActorBase(formID);
        if (!base) {
            MARAS_LOG_WARN("AddNpcKeyword: actor has no base data for form {:08X}", formID);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        if (!base->AddKeyword(keyword)) {
            return false;
        }
        NPCRelationshipManager::GetSingleton().AddTrackedKeyword(formID, keyword->GetFormID());
        return true;
    }

    bool RemoveNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword) {
        if (!npc || !keyword) {
            MARAS_LOG_WARN("RemoveNpcKeyword: null argument(s) provided");
            return false;
        }
        auto formID = npc->GetFormID();
        auto base = ActorSvc::GetActorBase(formID);
        if (!base) {
            MARAS_LOG_WARN("RemoveNpcKeyword: actor has no base data for form {:08X}", formID);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        if (!base->RemoveKeyword(keyword)) {
            return false;
        }
        NPCRelationshipManager::GetSingleton().RemoveTrackedKeyword(formID, keyword->GetFormID());
        return true;
    }

    // ========================================
    // JSON string utility bindings
    // ========================================

    std::string GetJsonString(RE::StaticFunctionTag*, std::string json, std::string path, std::string defaultVal) {
        return MARAS::JsonService::GetString(json.c_str(), path.c_str(), defaultVal.c_str());
    }

    RE::Actor* GetJsonActor(RE::StaticFunctionTag*, std::string json, std::string path) {
        return MARAS::JsonService::GetActor(json.c_str(), path.c_str());
    }

    int32_t GetJsonInt(RE::StaticFunctionTag*, std::string json, std::string path, int32_t defaultVal) {
        return MARAS::JsonService::GetInt(json.c_str(), path.c_str(), defaultVal);
    }

    float GetJsonFloat(RE::StaticFunctionTag*, std::string json, std::string path, float defaultVal) {
        return MARAS::JsonService::GetFloat(json.c_str(), path.c_str(), defaultVal);
    }

    bool GetJsonBool(RE::StaticFunctionTag*, std::string json, std::string path, bool defaultVal) {
        return MARAS::JsonService::GetBool(json.c_str(), path.c_str(), defaultVal);
    }

    int32_t GetJsonArrayLength(RE::StaticFunctionTag*, std::string json, std::string path) {
        return MARAS::JsonService::GetArrayLength(json.c_str(), path.c_str());
    }

    std::vector<RE::Actor*> GetJsonActorArray(RE::StaticFunctionTag*, std::string json, std::string path) {
        return MARAS::JsonService::GetActorArray(json.c_str(), path.c_str());
    }

    // ========================================
    // Prisma UI & Confirmation Modal bindings
    // ========================================

    RE::BSScript::LatentStatus ShowConfirmationModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                     RE::VMStackID stackID,
                                                     RE::StaticFunctionTag*,
                                                     RE::BSFixedString actionType,
                                                     std::vector<RE::Actor*> mainActors,
                                                     std::vector<RE::Actor*> secondaryActors,
                                                     RE::BSFixedString paramsJson) {
        MARAS_LOG_INFO("PapyrusInterface: ShowConfirmationModal actionType='{}' mainActors={} secondaryActors={} paramsJson='{}'",
                       actionType.c_str(), mainActors.size(), secondaryActors.size(), paramsJson.c_str());
        return ConfirmationModal::GetSingleton().Show(
            vm, stackID,
            actionType.c_str(), mainActors, secondaryActors,
            paramsJson.c_str());
    }

    int32_t CheckAndSetActionCooldown(RE::StaticFunctionTag*,
                                      RE::BSFixedString actionType,
                                      std::vector<RE::Actor*> actors) {
        MARAS_LOG_INFO("PapyrusInterface: CheckAndSetActionCooldown actionType='{}' actors={}",
                       actionType.c_str(), actors.size());
        return ConfirmationModal::GetSingleton().CheckAndSet(actionType.c_str(), actors);
    }

    void ShowUI(RE::StaticFunctionTag*) {
        MARAS_LOG_INFO("Papyrus::ShowUI called");
        PrismaUIManager::GetSingleton().Show();
    }

    void HideUI(RE::StaticFunctionTag*) {
        MARAS_LOG_INFO("Papyrus::HideUI called");
        PrismaUIManager::GetSingleton().Hide();
    }

    bool IsUIVisible(RE::StaticFunctionTag*) {
        bool visible = PrismaUIManager::GetSingleton().IsVisible();
        MARAS_LOG_INFO("Papyrus::IsUIVisible called -> {}", visible);
        return visible;
    }

    // ========================================
    // Intimate scene & Banter Coordinator bindings
    // ========================================

    bool RegisterIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, std::vector<RE::Actor*> actors) {
        return IntimateThreadService::GetSingleton().RegisterIntimateThread(framework.c_str(), threadID, actors);
    }

    void ReportIntimateInteraction(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, RE::Actor* actorA, RE::Actor* actorB) {
        IntimateThreadService::GetSingleton().ReportIntimateInteraction(framework.c_str(), threadID, actorA, actorB);
    }

    void ClearIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID) {
        IntimateThreadService::GetSingleton().ClearIntimateThread(framework.c_str(), threadID);
    }

    void FleeChildrenFromPlayerCell(RE::StaticFunctionTag*) {
        ChildrenFleeService::GetSingleton().FleeFromPlayerCell();
    }

    void StopChildrenFleeing(RE::StaticFunctionTag*) {
        ChildrenFleeService::GetSingleton().StopFleeing();
    }

    std::vector<RE::Actor*> FindBanterCoupleCandidates(RE::StaticFunctionTag*, float afScanRadius) {
        return BanterCoordinatorService::GetSingleton().FindBanterCoupleCandidates(afScanRadius);
    }

    bool IsBanterPartnerPresent(RE::StaticFunctionTag*, RE::Actor* akPartner, float afScanRadius) {
        return BanterCoordinatorService::GetSingleton().IsBanterPartnerPresent(akPartner, afScanRadius);
    }

    void ResetBanterCoupleCooldown(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB) {
        if (!akActorA || !akActorB) return;
        BanterCoordinatorService::GetSingleton().ResetCooldown(akActorA->GetFormID(), akActorB->GetFormID());
    }

    void ClearAllBanterCooldowns(RE::StaticFunctionTag*) {
        BanterCoordinatorService::GetSingleton().ClearAllCooldowns();
    }

    float GetBanterCoupleCooldownHours(RE::StaticFunctionTag*) {
        return BanterCoordinatorService::GetSingleton().GetCooldownHours();
    }

    void SetBanterCoupleCooldownHours(RE::StaticFunctionTag*, float afHours) {
        BanterCoordinatorService::GetSingleton().SetCooldownHours(afHours);
    }

    void RecordScenePlayed(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB) {
        BanterCoordinatorService::GetSingleton().RecordScenePlayed(akActorA, akActorB);
    }

    bool IsLocationAllowedForScenes(RE::StaticFunctionTag*, RE::BGSLocation* akLocation) {
        return BanterCoordinatorService::GetSingleton().IsLocationAllowedForScenes(akLocation);
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
        vm->RegisterFunction("IsNPCStatusWith", "MARAS", IsNPCStatusWith);
        vm->RegisterFunction("IsNPCStatusWithByEnum", "MARAS", IsNPCStatusWithByEnum);
        vm->RegisterFunction("GetOriginalRelationshipStatus", "MARAS", GetOriginalRelationshipStatus);
        vm->RegisterFunction("GetOriginalRelationshipStatusString", "MARAS", GetOriginalRelationshipStatusString);

        // Consolidated status transition functions
        vm->RegisterFunction("PromoteNPCToStatus", "MARAS", PromoteNPCToStatus);
        vm->RegisterFunction("PromoteNPCToStatusByEnum", "MARAS", PromoteNPCToStatusByEnum);
        vm->RegisterFunction("PromoteNPCToStatusWith", "MARAS", PromoteNPCToStatusWith);
        vm->RegisterFunction("PromoteNPCToStatusWithByEnum", "MARAS", PromoteNPCToStatusWithByEnum);

        // Consolidated bulk retrieval functions
        vm->RegisterFunction("GetNPCsByStatus", "MARAS", GetNPCsByStatus);
        vm->RegisterFunction("GetNPCsByStatusEnum", "MARAS", GetNPCsByStatusEnum);
        vm->RegisterFunction("GetNPCsByStatusWith", "MARAS", GetNPCsByStatusWith);
        vm->RegisterFunction("GetRelationshipPartners", "MARAS", GetRelationshipPartners);
        vm->RegisterFunction("GetCurrentTeammates", "MARAS", GetCurrentTeammates);
        vm->RegisterFunction("IsPlayerFollower", "MARAS", IsPlayerFollower);

        // Consolidated statistics functions
        vm->RegisterFunction("GetStatusCount", "MARAS", GetStatusCount);
        vm->RegisterFunction("GetStatusCountByEnum", "MARAS", GetStatusCountByEnum);

        // Status history functions
        vm->RegisterFunction("GetStatusHistoryCount", "MARAS", GetStatusHistoryCount);
        vm->RegisterFunction("GetStatusHistoryEntry", "MARAS", GetStatusHistoryEntry);

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
        vm->RegisterFunction("SetTenantStayAtHome", "MARAS", SetTenantStayAtHome);
        vm->RegisterFunction("GetTenantStayAtHome", "MARAS", GetTenantStayAtHome);
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
        vm->RegisterFunction("GetChildHomeMarker", "MARAS", GetChildHomeMarker);

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
        vm->RegisterFunction("GetNpcStatusNameWith", "MARAS", GetNpcStatusNameWith);
        vm->RegisterFunction("GetNpcStatusEnumWith", "MARAS", GetNpcStatusEnumWith);
        vm->RegisterFunction("EvaluateRelationshipStatusChange", "MARAS", EvaluateRelationshipStatusChange);
        vm->RegisterFunction("IsNPCInOpenRelationship", "MARAS", IsNPCInOpenRelationship);
        vm->RegisterFunction("GetPairIsOpen", "MARAS", GetPairIsOpen);
        vm->RegisterFunction("SetPairIsOpen", "MARAS", SetPairIsOpen);
        vm->RegisterFunction("RecordCheating", "MARAS", RecordCheating);
        vm->RegisterFunction("SetCheatingDiscovered", "MARAS", SetCheatingDiscovered);
        vm->RegisterFunction("RegisterIntimateThread", "MARAS", RegisterIntimateThread);
        vm->RegisterFunction("ReportIntimateInteraction", "MARAS", ReportIntimateInteraction);
        vm->RegisterFunction("ClearIntimateThread", "MARAS", ClearIntimateThread);
        vm->RegisterFunction("FleeChildrenFromPlayerCell", "MARAS", FleeChildrenFromPlayerCell);
        vm->RegisterFunction("StopChildrenFleeing", "MARAS", StopChildrenFleeing);
        vm->RegisterFunction("Breakup", "MARAS", Breakup);
        vm->RegisterFunction("BreakAllLoverConnections", "MARAS", BreakAllLoverConnections);
        vm->RegisterFunction("SetCheatingAffairStatus", "MARAS", SetCheatingAffairStatus);
        vm->RegisterFunction("GetCheatingAffairStatus", "MARAS", GetCheatingAffairStatus);
        vm->RegisterFunction("HasCheatedOn", "MARAS", HasCheatedOn);
        vm->RegisterFunction("IsNPCCheating", "MARAS", IsNPCCheating);
        vm->RegisterFunction("IsRelationshipCheating", "MARAS", IsRelationshipCheating);
        vm->RegisterFunction("GetCheatingCount", "MARAS", GetCheatingCount);
        vm->RegisterFunction("GetCheatedOnPartners", "MARAS", GetCheatedOnPartners);
        vm->RegisterFunction("GetCheatersWhoCheatedOn", "MARAS", GetCheatersWhoCheatedOn);
        vm->RegisterFunction("RegisterChildActor", "MARAS", RegisterChildActor);
        vm->RegisterFunction("RemoveChild", "MARAS", RemoveChild);
        vm->RegisterFunction("GetChildren", "MARAS", GetChildren);
        vm->RegisterFunction("GetChildrenOfActor", "MARAS", GetChildrenOfActor);
        vm->RegisterFunction("UpdateSpouseChildrenFaction", "MARAS", UpdateSpouseChildrenFaction);
        vm->RegisterFunction("IsAvailableForStatus", "MARAS", IsAvailableForStatus);
        vm->RegisterFunction("IsAvailableForStatusWith", "MARAS", IsAvailableForStatusWith);
        vm->RegisterFunction("CanFormRelationship", "MARAS", CanFormRelationship);
        vm->RegisterFunction("GetJsonString", "MARAS", GetJsonString);
        vm->RegisterFunction("GetJsonActor", "MARAS", GetJsonActor);
        vm->RegisterFunction("GetJsonInt", "MARAS", GetJsonInt);
        vm->RegisterFunction("GetJsonFloat", "MARAS", GetJsonFloat);
        vm->RegisterFunction("GetJsonBool", "MARAS", GetJsonBool);
        vm->RegisterFunction("GetJsonArrayLength", "MARAS", GetJsonArrayLength);
        vm->RegisterFunction("GetJsonActorArray", "MARAS", GetJsonActorArray);
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

        // Keyword management
        vm->RegisterFunction("HasNpcKeyword", "MARAS", HasNpcKeyword);
        vm->RegisterFunction("AddNpcKeyword", "MARAS", AddNpcKeyword);
        vm->RegisterFunction("RemoveNpcKeyword", "MARAS", RemoveNpcKeyword);

        // Prisma UI functions
        vm->RegisterFunction("ShowUI", "MARAS", ShowUI);
        vm->RegisterFunction("HideUI", "MARAS", HideUI);
        vm->RegisterFunction("IsUIVisible", "MARAS", IsUIVisible);

        // Confirmation modal functions
        RegisterLatentFixed<int32_t>(vm, "ShowConfirmationModal", "MARAS", ShowConfirmationModal);
        vm->RegisterFunction("CheckAndSetActionCooldown", "MARAS", CheckAndSetActionCooldown);

        // Settings & Migration functions
        vm->RegisterFunction("SetSettingBool", "MARAS", SetSettingBool);
        vm->RegisterFunction("GetSettingBool", "MARAS", GetSettingBool);
        vm->RegisterFunction("SetSettingInt", "MARAS", SetSettingInt);
        vm->RegisterFunction("GetSettingInt", "MARAS", GetSettingInt);
        vm->RegisterFunction("SetSettingFloat", "MARAS", SetSettingFloat);
        vm->RegisterFunction("GetSettingFloat", "MARAS", GetSettingFloat);
        vm->RegisterFunction("IsSettingsMigrated", "MARAS", IsSettingsMigrated);
        vm->RegisterFunction("MarkSettingsMigrated", "MARAS", MarkSettingsMigrated);

        vm->RegisterFunction("GetStartDialGender", "MARAS", GetStartDialGender);
        vm->RegisterFunction("SetStartDialGender", "MARAS", SetStartDialGender);
        vm->RegisterFunction("GetEnablePolygamy", "MARAS", GetEnablePolygamy);
        vm->RegisterFunction("SetEnablePolygamy", "MARAS", SetEnablePolygamy);
        vm->RegisterFunction("GetDisablePolygamyQuest", "MARAS", GetDisablePolygamyQuest);
        vm->RegisterFunction("SetDisablePolygamyQuest", "MARAS", SetDisablePolygamyQuest);
        vm->RegisterFunction("GetMinRelRankForDial", "MARAS", GetMinRelRankForDial);
        vm->RegisterFunction("SetMinRelRankForDial", "MARAS", SetMinRelRankForDial);

        // Player children / household adoption functions
        vm->RegisterFunction("AssignChildToHouse", "MARAS", AssignChildToHouse);
        vm->RegisterFunction("IssueChildOrder", "MARAS", IssueChildOrder);
        vm->RegisterFunction("ResetChildForceGreeting", "MARAS", ResetChildForceGreeting);
        vm->RegisterFunction("AssignChildPet", "MARAS", AssignChildPet);
        vm->RegisterFunction("GetChildPet", "MARAS", GetChildPet);
        vm->RegisterFunction("GetChildForPet", "MARAS", GetChildForPet);
        vm->RegisterFunction("RemoveChildPet", "MARAS", RemoveChildPet);
        vm->RegisterFunction("SetChildPetBanned", "MARAS", SetChildPetBanned);
        vm->RegisterFunction("IsChildPetBanned", "MARAS", IsChildPetBanned);
        vm->RegisterFunction("IsPetBanned", "MARAS", IsPetBanned);
        vm->RegisterFunction("SetGlobalPetBanned", "MARAS", SetGlobalPetBanned);
        vm->RegisterFunction("IsGlobalPetBanned", "MARAS", IsGlobalPetBanned);
        vm->RegisterFunction("SetChildPetRetryTimestamp", "MARAS", SetChildPetRetryTimestamp);
        vm->RegisterFunction("RecordChildPetRetry", "MARAS", RecordChildPetRetry);
        vm->RegisterFunction("GetChildPetRetryTimestamp", "MARAS", GetChildPetRetryTimestamp);
        vm->RegisterFunction("IsPetRetryAvailable", "MARAS", IsPetRetryAvailable);
        vm->RegisterFunction("AssignChildCritter", "MARAS", AssignChildCritter);
        vm->RegisterFunction("GetChildCritter", "MARAS", GetChildCritter);
        vm->RegisterFunction("GetChildForCritter", "MARAS", GetChildForCritter);
        vm->RegisterFunction("GetChildForPetOrCritter", "MARAS", GetChildForPetOrCritter);
        vm->RegisterFunction("RemoveChildCritter", "MARAS", RemoveChildCritter);
        vm->RegisterFunction("SetChildCritterBanned", "MARAS", SetChildCritterBanned);
        vm->RegisterFunction("IsChildCritterBanned", "MARAS", IsChildCritterBanned);
        vm->RegisterFunction("IsCritterBanned", "MARAS", IsCritterBanned);
        vm->RegisterFunction("SetChildIsNewlyAdopted", "MARAS", SetChildIsNewlyAdopted);
        vm->RegisterFunction("IsChildNewlyAdopted", "MARAS", IsChildNewlyAdopted);
        vm->RegisterFunction("IsChildPlayerAdopted", "MARAS", IsChildPlayerAdopted);
        vm->RegisterFunction("SetHouseInitialMoveDone", "MARAS", SetHouseInitialMoveDone);
        vm->RegisterFunction("IsHouseInitialMoveDone", "MARAS", IsHouseInitialMoveDone);
        vm->RegisterFunction("SetChildChestIntroduced", "MARAS", SetChildChestIntroduced);
        vm->RegisterFunction("IsChildChestIntroduced", "MARAS", IsChildChestIntroduced);
        vm->RegisterFunction("SetGlobalCritterBanned", "MARAS", SetGlobalCritterBanned);
        vm->RegisterFunction("IsGlobalCritterBanned", "MARAS", IsGlobalCritterBanned);
        vm->RegisterFunction("SetChildCritterRetryTimestamp", "MARAS", SetChildCritterRetryTimestamp);
        vm->RegisterFunction("RecordChildCritterRetry", "MARAS", RecordChildCritterRetry);
        vm->RegisterFunction("GetChildCritterRetryTimestamp", "MARAS", GetChildCritterRetryTimestamp);
        vm->RegisterFunction("IsChildCritterRetryAvailable", "MARAS", IsChildCritterRetryAvailable);
        vm->RegisterFunction("GetAllPlayerChildren", "MARAS", GetAllPlayerChildren);
        vm->RegisterFunction("GetChildrenAssignedToHouse", "MARAS", GetChildrenAssignedToHouse);
        vm->RegisterFunction("GetNewlyAdoptedChild", "MARAS", GetNewlyAdoptedChild);
        vm->RegisterFunction("GetChildWithoutPet", "MARAS", GetChildWithoutPet);
        vm->RegisterFunction("GetChildWithoutCritter", "MARAS", GetChildWithoutCritter);
        vm->RegisterFunction("GetChildrenWithoutScene", "MARAS", GetChildrenWithoutScene);
        vm->RegisterFunction("GetWeightedRandomChild", "MARAS", GetWeightedRandomChild);
        vm->RegisterFunction("SetChildPlayedScene", "MARAS", SetChildPlayedScene);
        vm->RegisterFunction("HasChildPlayedScene", "MARAS", HasChildPlayedScene);
        vm->RegisterFunction("RecordChildInteraction", "MARAS", RecordChildInteraction);
        vm->RegisterFunction("GetChildLastInteractionTimestamp", "MARAS", GetChildLastInteractionTimestamp);

        // Deceased Registry queries
        vm->RegisterFunction("IsWidow", "MARAS", IsWidow);
        vm->RegisterFunction("IsNPCDeceased", "MARAS", IsNPCDeceased);
        vm->RegisterFunction("WasMarriedTo", "MARAS", WasMarriedTo);
        vm->RegisterFunction("WasEngagedTo", "MARAS", WasEngagedTo);
        vm->RegisterFunction("WasExOf", "MARAS", WasExOf);
        vm->RegisterFunction("GetDeceasedSpouseNames", "MARAS", GetDeceasedSpouseNames);
        vm->RegisterFunction("GetDeceasedChildrenNames", "MARAS", GetDeceasedChildrenNames);
        vm->RegisterFunction("HasDeceasedChildren", "MARAS", HasDeceasedChildren);
        vm->RegisterFunction("GetDeceasedCount", "MARAS", GetDeceasedCount);

        // Fertility Mode Reloaded & Paternity Intrigue
        vm->RegisterFunction("FindActorByName", "MARAS", FindActorByName);
        vm->RegisterFunction("SetNPCPregnant", "MARAS", SetNPCPregnant);
        vm->RegisterFunction("IsNPCPregnant", "MARAS", IsNPCPregnant);
        vm->RegisterFunction("RegisterUnbornChild", "MARAS", RegisterUnbornChild);
        vm->RegisterFunction("PromoteUnbornToInfant", "MARAS", PromoteUnbornToInfant);
        vm->RegisterFunction("RemoveUnbornChild", "MARAS", RemoveUnbornChild);
        vm->RegisterFunction("FindUnbornChild", "MARAS", FindUnbornChild);
        vm->RegisterFunction("GetUnbornChildren", "MARAS", GetUnbornChildren);
        vm->RegisterFunction("GetUnbornChildCount", "MARAS", GetUnbornChildCount);
        vm->RegisterFunction("RegisterFertilityChild", "MARAS", RegisterFertilityChild);
        vm->RegisterFunction("ResolvePaternitySecret", "MARAS", ResolvePaternitySecret);
        vm->RegisterFunction("ResolvePaternitySecretForPartner", "MARAS", ResolvePaternitySecretForPartner);
        vm->RegisterFunction("ResolvePaternitySecretForBioFather", "MARAS", ResolvePaternitySecretForBioFather);
        vm->RegisterFunction("SetPaternitySecretEdges", "MARAS", SetPaternitySecretEdges);
        vm->RegisterFunction("IsSecretForBioFather", "MARAS", IsSecretForBioFather);
        vm->RegisterFunction("IsSecretForPartner", "MARAS", IsSecretForPartner);
        vm->RegisterFunction("SetChildStage", "MARAS", SetChildStage);
        vm->RegisterFunction("GetChildStage", "MARAS", GetChildStage);
        vm->RegisterFunction("SetChildTrainingLocation", "MARAS", SetChildTrainingLocation);
        vm->RegisterFunction("GetChildTrainingLocation", "MARAS", GetChildTrainingLocation);
        vm->RegisterFunction("SetChildGender", "MARAS", SetChildGender);
        vm->RegisterFunction("GetChildGender", "MARAS", GetChildGender);
        vm->RegisterFunction("SetChildGuardian", "MARAS", SetChildGuardian);
        vm->RegisterFunction("FindSyntheticChild", "MARAS", FindSyntheticChild);
        vm->RegisterFunction("UpdateChildName", "MARAS", UpdateChildName);
        vm->RegisterFunction("GetPrimaryPartner", "MARAS", GetPrimaryPartner);
        vm->RegisterFunction("GetGuardianA", "MARAS", GetGuardianA);
        vm->RegisterFunction("GetGuardianB", "MARAS", GetGuardianB);
        vm->RegisterFunction("GetGuardians", "MARAS", GetGuardians);
        vm->RegisterFunction("GetCoGuardian", "MARAS", GetCoGuardian);
        vm->RegisterFunction("IsGuardian", "MARAS", IsGuardian);
        vm->RegisterFunction("GetBioMother", "MARAS", GetBioMother);
        vm->RegisterFunction("GetBioFather", "MARAS", GetBioFather);
        vm->RegisterFunction("GetMotherPartner", "MARAS", GetMotherPartner);
        vm->RegisterFunction("SetBioMother", "MARAS", SetBioMother);
        vm->RegisterFunction("SetBioFather", "MARAS", SetBioFather);
        vm->RegisterFunction("GetChildName", "MARAS", GetChildName);
        vm->RegisterFunction("FindChildPaternitySecret", "MARAS", FindChildPaternitySecret);
        vm->RegisterFunction("BuildPregnancyAnnouncedMessage", "MARAS", BuildPregnancyAnnouncedMessage);
        vm->RegisterFunction("BuildPregnancyAnnouncedJson", "MARAS", BuildPregnancyAnnouncedJson);
        vm->RegisterFunction("BuildChildBirthMessage", "MARAS", BuildChildBirthMessage);
        vm->RegisterFunction("BuildChildBirthJson", "MARAS", BuildChildBirthJson);

        // Child name generation functions
        vm->RegisterFunction("GenerateRandomChildName", "MARAS", GenerateRandomChildName);
        vm->RegisterFunction("GenerateRandomChildNameForActor", "MARAS", GenerateRandomChildNameForActor);
        vm->RegisterFunction("GenerateRandomChildNameForRace", "MARAS", GenerateRandomChildNameForRace);
        vm->RegisterFunction("GetChildNames", "MARAS", GetChildNames);
        vm->RegisterFunction("ReloadChildNames", "MARAS", ReloadChildNames);

        // Critter name generation functions
        vm->RegisterFunction("GenerateRandomCritterName", "MARAS", GenerateRandomCritterName);
        vm->RegisterFunction("GenerateRandomCritterNameForActor", "MARAS", GenerateRandomCritterNameForActor);
        vm->RegisterFunction("GenerateRandomCritterNameForRace", "MARAS", GenerateRandomCritterNameForRace);
        vm->RegisterFunction("GetCritterNames", "MARAS", GetCritterNames);
        vm->RegisterFunction("ReloadCritterNames", "MARAS", ReloadCritterNames);

        // Banter Coordinator functions
        vm->RegisterFunction("FindBanterCoupleCandidates", "MARAS", FindBanterCoupleCandidates);
        vm->RegisterFunction("IsBanterPartnerPresent", "MARAS", IsBanterPartnerPresent);
        vm->RegisterFunction("ResetBanterCoupleCooldown", "MARAS", ResetBanterCoupleCooldown);
        vm->RegisterFunction("ClearAllBanterCooldowns", "MARAS", ClearAllBanterCooldowns);
        vm->RegisterFunction("GetBanterCoupleCooldownHours", "MARAS", GetBanterCoupleCooldownHours);
        vm->RegisterFunction("SetBanterCoupleCooldownHours", "MARAS", SetBanterCoupleCooldownHours);
        vm->RegisterFunction("RecordScenePlayed", "MARAS", RecordScenePlayed);
        vm->RegisterFunction("IsLocationAllowedForScenes", "MARAS", IsLocationAllowedForScenes);

        MARAS_LOG_INFO("Registered MARAS Papyrus functions including Confirmation Modal and Settings");
        return true;
    }

}  // namespace MARAS::PapyrusInterface
