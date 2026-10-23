#pragma once

#include "core/NPCRelationshipManager.h"

namespace MARAS::PapyrusInterface {

    // Shared recursive mutex serializing concurrent Papyrus VM worker thread calls into MARAS domain services
    inline std::recursive_mutex& GetPapyrusMutex() noexcept {
        static std::recursive_mutex mtx;
        return mtx;
    }

    // ========================================
    // Registration & Characteristics (PapyrusRelationship.cpp)
    // ========================================
    bool RegisterCandidate(RE::StaticFunctionTag*, RE::Actor* npc);
    bool UnregisterNPC(RE::StaticFunctionTag*, RE::Actor* npc);
    bool SetNpcCharacteristics(RE::StaticFunctionTag*, RE::Actor* npc, std::string factionType, std::int32_t rank);

    std::vector<std::string> GetNpcTypes(RE::StaticFunctionTag*, std::string type);
    std::int32_t GetNpcTypeEnum(RE::StaticFunctionTag*, std::string type, std::string name);
    std::int32_t GetNpcCurrentTypeEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::string type);
    std::string GetNpcCurrentTypeName(RE::StaticFunctionTag*, RE::Actor* npc, std::string type);

    std::int32_t DetermineSocialClass(RE::StaticFunctionTag*, RE::Actor* npc);
    std::int32_t DetermineSkillType(RE::StaticFunctionTag*, RE::Actor* npc);
    std::int32_t DetermineTemperament(RE::StaticFunctionTag*, RE::Actor* npc);

    // ========================================
    // Relationship Status Checks & Transitions (PapyrusRelationship.cpp)
    // ========================================
    bool IsNPCStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType);
    bool IsNPCStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum);
    bool IsNPCStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string statusType);
    bool IsNPCStatusWithByEnum(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::int32_t statusEnum);
    std::int32_t GetOriginalRelationshipStatus(RE::StaticFunctionTag*, RE::Actor* npc1, RE::Actor* npc2);
    std::string GetOriginalRelationshipStatusString(RE::StaticFunctionTag*, RE::Actor* npc1, RE::Actor* npc2);

    std::string GetNpcStatusName(RE::StaticFunctionTag*, RE::Actor* npc);
    std::int32_t GetNpcStatusEnum(RE::StaticFunctionTag*, RE::Actor* npc);
    std::string GetNpcStatusNameWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target);
    std::int32_t GetNpcStatusEnumWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target);

    bool PromoteNPCToStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType, bool sendEvent = true);
    bool PromoteNPCToStatusByEnum(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t statusEnum, bool sendEvent = true);
    bool PromoteNPCToStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string statusType, bool sendEvent = true);
    bool PromoteNPCToStatusWithByEnum(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::int32_t statusEnum, bool sendEvent = true);
    bool EvaluateRelationshipStatusChange(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* partner, std::string targetStatus);
    bool Breakup(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB);
    std::int32_t BreakAllLoverConnections(RE::StaticFunctionTag*, RE::Actor* npc, bool onlyCheating);

    // ========================================
    // Bulk Retrieval, Statistics & History (PapyrusRelationship.cpp)
    // ========================================
    std::vector<RE::Actor*> GetNPCsByStatus(RE::StaticFunctionTag*, std::string statusType);
    std::vector<RE::Actor*> GetNPCsByStatusEnum(RE::StaticFunctionTag*, std::int32_t statusEnum);
    std::vector<RE::Actor*> GetNPCsByStatusWith(RE::StaticFunctionTag*, RE::Actor* target, std::string statusType);
    std::vector<RE::Actor*> GetRelationshipPartners(RE::StaticFunctionTag*, RE::Actor* npc, std::string statusType);
    std::vector<RE::Actor*> GetCurrentTeammates(RE::StaticFunctionTag*);
    bool IsPlayerFollower(RE::StaticFunctionTag*, RE::Actor* npc);

    int GetStatusCount(RE::StaticFunctionTag*, std::string statusType);
    int GetStatusCountByEnum(RE::StaticFunctionTag*, std::int32_t statusEnum);

    int GetStatusHistoryCount(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target = nullptr);
    std::string GetStatusHistoryEntry(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target = nullptr, int index = 0);

    // ========================================
    // Availability, Open Relationships & Infidelity (PapyrusRelationship.cpp)
    // ========================================
    bool IsNPCInOpenRelationship(RE::StaticFunctionTag*, RE::Actor* npc);
    bool GetPairIsOpen(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB);
    bool SetPairIsOpen(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB, bool isOpen);
    bool IsAvailableForStatus(RE::StaticFunctionTag*, RE::Actor* npc, std::string status);
    bool IsAvailableForStatusWith(RE::StaticFunctionTag*, RE::Actor* npc, RE::Actor* target, std::string status);
    bool CanFormRelationship(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB, std::string status);

    bool RecordCheating(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner, bool isSecret);
    bool SetCheatingDiscovered(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* affairPartner, RE::BSFixedString statusOrResolution);
    bool SetCheatingAffairStatus(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner, std::int32_t affairStatus);
    std::int32_t GetCheatingAffairStatus(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner);
    std::int32_t GetAffairStage(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner);
    bool SetAffairStage(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim, RE::Actor* affairPartner, std::int32_t stage);
    std::string GetPartnerPendingIntent(RE::StaticFunctionTag*, RE::Actor* partner);
    void ClearPartnerIntent(RE::StaticFunctionTag*, RE::Actor* partner);
    bool EvaluatePartnerHomeArrival(RE::StaticFunctionTag*, RE::BGSLocation* akNewLoc);
    bool HasCheatedOn(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim);
    bool IsNPCCheating(RE::StaticFunctionTag*, RE::Actor* npc);
    bool IsRelationshipCheating(RE::StaticFunctionTag*, RE::Actor* actorA, RE::Actor* actorB);
    std::int32_t GetCheatingCount(RE::StaticFunctionTag*, RE::Actor* cheater, RE::Actor* victim);
    std::vector<RE::Actor*> GetCheatedOnPartners(RE::StaticFunctionTag*, RE::Actor* cheater);
    std::vector<RE::Actor*> GetCheatersWhoCheatedOn(RE::StaticFunctionTag*, RE::Actor* victim);

    // ========================================
    // Hierarchy, Marriage Difficulty, Buffs, Bonuses & Affection (PapyrusRelationship.cpp)
    // ========================================
    bool SetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc, std::int32_t rank);
    std::int32_t GetHierarchyRank(RE::StaticFunctionTag*, RE::Actor* npc);

    float CalculateMarriageSuccessChance(RE::StaticFunctionTag*, RE::Actor* npc, float intimacyAdjustment,
                                         float mostGold, float housesOwned, float horsesOwned, float questsCompleted,
                                         float dungeonsCleared, float dragonSoulsCollected, bool playerKiller);

    float GetSpouseMultiplier(RE::StaticFunctionTag*, RE::Actor* spouse);
    std::vector<float> GetFollowersMultipliers(RE::StaticFunctionTag*, std::vector<RE::Actor*> followers);
    std::vector<float> GetPermanentMultipliers(RE::StaticFunctionTag*);

    int GetBonusCount(RE::StaticFunctionTag*, std::string type);
    RE::BGSPerk* GetBonusPerk(RE::StaticFunctionTag*, std::string type, int index = 0);
    int GetBonusEffectIndex(RE::StaticFunctionTag*, std::string type, int index = 0);
    float GetBonusPerkValue(RE::StaticFunctionTag*, std::string type, int index = 0);
    std::string GetBonusPerkType(RE::StaticFunctionTag*, std::string type, int index = 0);
    std::string GetBonusPerkUnit(RE::StaticFunctionTag*, std::string type, int index = 0);
    std::string GetBonusPerkDescription(RE::StaticFunctionTag*, std::string type, int index = 0);

    void AddAffection(RE::StaticFunctionTag*, RE::Actor* npc, float amount, std::string type);
    float GetAffection(RE::StaticFunctionTag*, RE::Actor* npc, std::string type);
    int GetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc);
    void SetPermanentAffection(RE::StaticFunctionTag*, RE::Actor* npc, int amount, bool sendEvent = true);
    void SetAffectionMinMax(RE::StaticFunctionTag*, std::string type, int minVal, int maxVal);
    void ApplyDailyAffection(RE::StaticFunctionTag*);
    void SetAffectionDecayMultiplier(RE::StaticFunctionTag*, float multiplier);
    float GetAffectionDecayMultiplier(RE::StaticFunctionTag*);

    // ========================================
    // Deceased Registry Queries (PapyrusRelationship.cpp)
    // ========================================
    bool IsWidow(RE::StaticFunctionTag*, RE::Actor* actor);
    bool IsNPCDeceased(RE::StaticFunctionTag*, RE::Actor* actor);
    bool WasMarriedTo(RE::StaticFunctionTag*, RE::Actor* deceased, RE::Actor* partner);
    bool WasEngagedTo(RE::StaticFunctionTag*, RE::Actor* deceased, RE::Actor* partner);
    bool WasExOf(RE::StaticFunctionTag*, RE::Actor* deceased, RE::Actor* partner);
    std::vector<std::string> GetDeceasedSpouseNames(RE::StaticFunctionTag*, RE::Actor* actor);
    std::vector<std::string> GetDeceasedChildrenNames(RE::StaticFunctionTag*, RE::Actor* actor);
    bool HasDeceasedChildren(RE::StaticFunctionTag*, RE::Actor* actor);
    std::int32_t GetDeceasedCount(RE::StaticFunctionTag*);

    // ========================================
    // Player Houses, Spouse Assets & Home Markers (PapyrusHousehold.cpp)
    // ========================================
    bool RegisterPlayerHouseCell(RE::StaticFunctionTag*, RE::BGSLocation* loc, RE::TESObjectREFR* homeMarker);
    std::vector<RE::BGSLocation*> GetAllPlayerHouses(RE::StaticFunctionTag*);
    std::vector<std::string> GetAllPlayerHousesNames(RE::StaticFunctionTag*);
    bool RegisterTenantInPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse, RE::BGSLocation* playerHouse, bool stayAtHome);
    bool RemoveTenantFromPlayerHouse(RE::StaticFunctionTag*, RE::Actor* spouse);
    void SetTenantStayAtHome(RE::StaticFunctionTag*, RE::Actor* spouse, bool stayAtHome);
    bool GetTenantStayAtHome(RE::StaticFunctionTag*, RE::Actor* spouse);
    std::vector<RE::Actor*> GetPlayerHouseTenants(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse);
    RE::TESObjectREFR* GetHouseMarker(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse);
    RE::BGSLocation* GetTenantHouse(RE::StaticFunctionTag*, RE::Actor* npc);
    int CountPlayerHouses(RE::StaticFunctionTag*);
    void SetHouseInitialMoveDone(RE::StaticFunctionTag*, RE::BGSLocation* houseLoc, bool done);
    bool IsHouseInitialMoveDone(RE::StaticFunctionTag*, RE::BGSLocation* houseLoc);

    std::vector<RE::TESObjectREFR*> GetCellDoors(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell);
    bool ShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc);
    bool StopShareHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* npc);
    bool IsHouseSharedWithPlayer(RE::StaticFunctionTag*, RE::TESObjectCELL* currentCell);
    bool HasSpouseSharedHouseWithPlayer(RE::StaticFunctionTag*, RE::Actor* spouse);

    RE::TESObjectCELL* GetNpcOriginalHouse(RE::StaticFunctionTag*, RE::Actor* npc);
    std::vector<RE::TESObjectREFR*> GetNpcBeds(RE::StaticFunctionTag*, RE::Actor* npc);
    RE::TESObjectREFR* GetNpcOriginalHouseCenterMarker(RE::StaticFunctionTag*, RE::Actor* npc);

    RE::TESObjectREFR* GetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc);
    bool SetTrackedNpcHomeMarker(RE::StaticFunctionTag*, RE::Actor* npc, RE::TESObjectREFR* marker);
    RE::TESObjectREFR* GetChildHomeMarker(RE::StaticFunctionTag*, RE::Actor* child);

    // ========================================
    // Children, Adoption, Pets, Critters, Fertility & Names (PapyrusChildren.cpp)
    // ========================================
    bool RegisterChildActor(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* guardianA, RE::Actor* guardianB = nullptr, RE::BGSLocation* houseLoc = nullptr, RE::Actor* bioMother = nullptr, RE::Actor* bioFather = nullptr, int32_t syntheticID = 0, int32_t stage = 2, bool sendEvent = true);
    bool RemoveChild(RE::StaticFunctionTag*, RE::Actor* child);
    std::vector<RE::Actor*> GetChildren(RE::StaticFunctionTag*, RE::Actor* parentA, RE::Actor* parentB);
    std::vector<RE::Actor*> GetChildrenOfActor(RE::StaticFunctionTag*, RE::Actor* parent);
    void UpdateSpouseChildrenFaction(RE::StaticFunctionTag*, RE::Actor* spouse);

    bool AssignChildToHouse(RE::StaticFunctionTag*, RE::Actor* child, RE::BGSLocation* houseLoc, bool moveChild = false);
    bool IssueChildOrder(RE::StaticFunctionTag*, RE::Actor* child, float orderValue, float durationHours);
    void ResetChildForceGreeting(RE::StaticFunctionTag*, RE::Actor* child);

    bool AssignChildPet(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* pet);
    RE::Actor* GetChildPet(RE::StaticFunctionTag*, RE::Actor* child);
    RE::Actor* GetChildForPet(RE::StaticFunctionTag*, RE::Actor* pet);
    bool RemoveChildPet(RE::StaticFunctionTag*, RE::Actor* child);
    void SetChildPetBanned(RE::StaticFunctionTag*, RE::Actor* child, bool banned);
    bool IsChildPetBanned(RE::StaticFunctionTag*, RE::Actor* child);
    bool IsPetBanned(RE::StaticFunctionTag*, RE::Actor* child);
    void SetGlobalPetBanned(RE::StaticFunctionTag*, bool banned);
    bool IsGlobalPetBanned(RE::StaticFunctionTag*);
    void SetChildPetRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child, float timestamp);
    void RecordChildPetRetry(RE::StaticFunctionTag*, RE::Actor* child);
    float GetChildPetRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child);
    bool IsPetRetryAvailable(RE::StaticFunctionTag*, RE::Actor* child);

    bool AssignChildCritter(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* critter);
    RE::Actor* GetChildCritter(RE::StaticFunctionTag*, RE::Actor* child);
    RE::Actor* GetChildForCritter(RE::StaticFunctionTag*, RE::Actor* critter);
    RE::Actor* GetChildForPetOrCritter(RE::StaticFunctionTag*, RE::Actor* petOrCritter);
    bool RemoveChildCritter(RE::StaticFunctionTag*, RE::Actor* child);
    void SetChildCritterBanned(RE::StaticFunctionTag*, RE::Actor* child, bool banned);
    bool IsChildCritterBanned(RE::StaticFunctionTag*, RE::Actor* child);
    bool IsCritterBanned(RE::StaticFunctionTag*, RE::Actor* child);
    void SetGlobalCritterBanned(RE::StaticFunctionTag*, bool banned);
    bool IsGlobalCritterBanned(RE::StaticFunctionTag*);
    void SetChildCritterRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child, float timestamp);
    void RecordChildCritterRetry(RE::StaticFunctionTag*, RE::Actor* child);
    float GetChildCritterRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child);
    bool IsChildCritterRetryAvailable(RE::StaticFunctionTag*, RE::Actor* child);

    void SetChildIsNewlyAdopted(RE::StaticFunctionTag*, RE::Actor* child, bool isNewlyAdopted, bool clearSameHouse);
    bool IsChildNewlyAdopted(RE::StaticFunctionTag*, RE::Actor* child);
    bool IsChildPlayerAdopted(RE::StaticFunctionTag*, RE::Actor* child);
    void SetChildChestIntroduced(RE::StaticFunctionTag*, bool introduced);
    bool IsChildChestIntroduced(RE::StaticFunctionTag*);
    std::vector<RE::Actor*> GetAllPlayerChildren(RE::StaticFunctionTag*);
    std::vector<RE::Actor*> GetChildrenAssignedToHouse(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse);
    RE::Actor* GetNewlyAdoptedChild(RE::StaticFunctionTag*, RE::BGSLocation* loc);
    RE::Actor* GetChildWithoutPet(RE::StaticFunctionTag*, RE::BGSLocation* loc);
    RE::Actor* GetChildWithoutCritter(RE::StaticFunctionTag*, RE::BGSLocation* loc);
    std::vector<RE::Actor*> GetChildrenWithoutScene(RE::StaticFunctionTag*, RE::BGSLocation* loc, std::int32_t sceneID);
    RE::Actor* GetWeightedRandomChild(RE::StaticFunctionTag*, RE::BGSLocation* loc);
    void SetChildPlayedScene(RE::StaticFunctionTag*, RE::Actor* child, std::int32_t sceneID, bool played);
    bool HasChildPlayedScene(RE::StaticFunctionTag*, RE::Actor* child, std::int32_t sceneID);
    void RecordChildInteraction(RE::StaticFunctionTag*, RE::Actor* child);
    float GetChildLastInteractionTimestamp(RE::StaticFunctionTag*, RE::Actor* child);

    RE::Actor* FindActorByName(RE::StaticFunctionTag*, std::string name);
    void SetNPCPregnant(RE::StaticFunctionTag*, RE::Actor* mother, bool pregnant, RE::Actor* bioFather = nullptr, RE::Actor* partner = nullptr, float conceptionTime = 0.0f);
    bool IsNPCPregnant(RE::StaticFunctionTag*, RE::Actor* mother);
    int32_t RegisterUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* bioFather, float conceptionTime, bool evaluateStepIn);
    int32_t PromoteUnbornToInfant(RE::StaticFunctionTag*, RE::Actor* mother, float birthTime, std::string name, RE::Actor* bioFather);
    bool RemoveUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* bioFather);
    int32_t FindUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother);
    std::vector<int32_t> GetUnbornChildren(RE::StaticFunctionTag*, RE::Actor* mother);
    int32_t GetUnbornChildCount(RE::StaticFunctionTag*, RE::Actor* mother);
    int32_t RegisterFertilityChild(RE::StaticFunctionTag*, int32_t syntheticID, RE::Actor* mother, RE::Actor* partner, RE::Actor* bioFather, float conception, float birth, std::string name);
    bool ResolvePaternitySecret(RE::StaticFunctionTag*, int32_t childID, bool discoveredByPartner);
    bool ResolvePaternitySecretForPartner(RE::StaticFunctionTag*, int32_t childID, bool discoveredByPartner);
    bool ResolvePaternitySecretForBioFather(RE::StaticFunctionTag*, int32_t childID, bool discoveredByBioFather);
    bool SetPaternitySecretEdges(RE::StaticFunctionTag*, int32_t childID, bool secretForBioFather, bool secretForPartner);
    bool IsSecretForBioFather(RE::StaticFunctionTag*, int32_t childID);
    bool IsSecretForPartner(RE::StaticFunctionTag*, int32_t childID);
    bool SetChildStage(RE::StaticFunctionTag*, int32_t childID, int32_t stage);
    int32_t GetChildStage(RE::StaticFunctionTag*, int32_t childID);
    bool SetChildTrainingLocation(RE::StaticFunctionTag*, int32_t childID, std::string location);
    std::string GetChildTrainingLocation(RE::StaticFunctionTag*, int32_t childID);
    bool SetChildGender(RE::StaticFunctionTag*, int32_t childID, int32_t gender);
    int32_t GetChildGender(RE::StaticFunctionTag*, int32_t childID);
    bool SetChildGuardian(RE::StaticFunctionTag*, int32_t childID, int32_t slot, RE::Actor* guardian);
    int32_t FindSyntheticChild(RE::StaticFunctionTag*, RE::Actor* mother, std::string name);
    bool UpdateChildName(RE::StaticFunctionTag*, int32_t childID, std::string newName);
    RE::Actor* GetPrimaryPartner(RE::StaticFunctionTag*, RE::Actor* actor);
    RE::Actor* GetGuardianA(RE::StaticFunctionTag*, int32_t childID);
    RE::Actor* GetGuardianB(RE::StaticFunctionTag*, int32_t childID);
    std::vector<RE::Actor*> GetGuardians(RE::StaticFunctionTag*, int32_t childID);
    RE::Actor* GetCoGuardian(RE::StaticFunctionTag*, int32_t childID, RE::Actor* actor);
    bool IsGuardian(RE::StaticFunctionTag*, int32_t childID, RE::Actor* actor);
    RE::Actor* GetBioMother(RE::StaticFunctionTag*, int32_t childID);
    RE::Actor* GetBioFather(RE::StaticFunctionTag*, int32_t childID);
    RE::Actor* GetMotherPartner(RE::StaticFunctionTag*, int32_t childID);
    bool SetBioMother(RE::StaticFunctionTag*, int32_t childID, RE::Actor* mother);
    bool SetBioFather(RE::StaticFunctionTag*, int32_t childID, RE::Actor* father);
    std::string GetChildName(RE::StaticFunctionTag*, int32_t childID);
    int32_t FindChildPaternitySecret(RE::StaticFunctionTag*, RE::Actor* parent1, RE::Actor* parent2, std::string childName);
    std::string BuildPregnancyAnnouncedMessage(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* partner, RE::Actor* bioFather, std::string bioFatherName, int32_t childCount);
    std::string BuildPregnancyAnnouncedJson(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* partner, RE::Actor* bioFather, std::string bioFatherName, int32_t childCount);
    std::string BuildChildBirthMessage(RE::StaticFunctionTag*, RE::Actor* mother, std::vector<int32_t> childIDs);
    std::string BuildChildBirthJson(RE::StaticFunctionTag*, RE::Actor* mother, std::vector<int32_t> childIDs);

    std::string GenerateRandomChildName(RE::StaticFunctionTag*, std::string raceName, bool isFemale, bool isUnique);
    std::string GenerateRandomChildNameForActor(RE::StaticFunctionTag*, RE::Actor* actor, bool isUnique);
    std::string GenerateRandomChildNameForRace(RE::StaticFunctionTag*, RE::TESForm* raceForm, bool isFemale, bool isUnique);
    std::vector<std::string> GetChildNames(RE::StaticFunctionTag*, std::string raceName, bool isFemale);
    bool ReloadChildNames(RE::StaticFunctionTag*);

    std::string GenerateRandomCritterName(RE::StaticFunctionTag*, std::string raceName);
    std::string GenerateRandomCritterNameForActor(RE::StaticFunctionTag*, RE::Actor* actor);
    std::string GenerateRandomCritterNameForRace(RE::StaticFunctionTag*, RE::TESForm* raceForm);
    std::vector<std::string> GetCritterNames(RE::StaticFunctionTag*, std::string raceName);
    bool ReloadCritterNames(RE::StaticFunctionTag*);

    // ========================================
    // Settings & Migration (PapyrusSettings.cpp)
    // ========================================
    void SetSettingBool(RE::StaticFunctionTag*, std::string key, bool value);
    bool GetSettingBool(RE::StaticFunctionTag*, std::string key);
    void SetSettingInt(RE::StaticFunctionTag*, std::string key, int32_t value);
    int32_t GetSettingInt(RE::StaticFunctionTag*, std::string key);
    void SetSettingFloat(RE::StaticFunctionTag*, std::string key, float value);
    float GetSettingFloat(RE::StaticFunctionTag*, std::string key);

    bool IsSettingsMigrated(RE::StaticFunctionTag*);
    void MarkSettingsMigrated(RE::StaticFunctionTag*);

    int32_t GetStartDialGender(RE::StaticFunctionTag*);
    void SetStartDialGender(RE::StaticFunctionTag*, int32_t value);
    bool GetEnablePolygamy(RE::StaticFunctionTag*);
    void SetEnablePolygamy(RE::StaticFunctionTag*, bool value);
    bool GetDisablePolygamyQuest(RE::StaticFunctionTag*);
    void SetDisablePolygamyQuest(RE::StaticFunctionTag*, bool value);
    int32_t GetMinRelRankForDial(RE::StaticFunctionTag*);
    void SetMinRelRankForDial(RE::StaticFunctionTag*, int32_t value);

    // ========================================
    // Core Diagnostics, Keywords, JSON, UI, Intimate & Banter (PapyrusInterface.cpp)
    // ========================================
    void Log(RE::StaticFunctionTag*, std::string msg, std::int32_t logLevel);
    void LogNPCStatistics(RE::StaticFunctionTag*);
    void LogNPCDetails(RE::StaticFunctionTag*, RE::Actor* npc);
    void SetLogLevel(RE::StaticFunctionTag*, std::int32_t logLevel);
    std::int32_t GetLogLevel(RE::StaticFunctionTag*);

    bool HasNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword);
    bool AddNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword);
    bool RemoveNpcKeyword(RE::StaticFunctionTag*, RE::Actor* npc, RE::BGSKeyword* keyword);

    std::string GetJsonString(RE::StaticFunctionTag*, std::string json, std::string path, std::string defaultVal);
    RE::Actor* GetJsonActor(RE::StaticFunctionTag*, std::string json, std::string path);
    RE::TESForm* GetJsonForm(RE::StaticFunctionTag*, std::string json, std::string path);
    int32_t GetJsonInt(RE::StaticFunctionTag*, std::string json, std::string path, int32_t defaultVal);
    float GetJsonFloat(RE::StaticFunctionTag*, std::string json, std::string path, float defaultVal);
    bool GetJsonBool(RE::StaticFunctionTag*, std::string json, std::string path, bool defaultVal);
    int32_t GetJsonArrayLength(RE::StaticFunctionTag*, std::string json, std::string path);
    std::vector<RE::Actor*> GetJsonActorArray(RE::StaticFunctionTag*, std::string json, std::string path);

    void ShowUI(RE::StaticFunctionTag*);
    void HideUI(RE::StaticFunctionTag*);
    bool IsUIVisible(RE::StaticFunctionTag*);

    RE::BSScript::LatentStatus ShowConfirmationModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                     RE::VMStackID stackID,
                                                     RE::StaticFunctionTag*,
                                                     RE::BSFixedString actionType,
                                                     std::vector<RE::Actor*> mainActors,
                                                     std::vector<RE::Actor*> secondaryActors,
                                                     RE::BSFixedString paramsJson);
    RE::BSScript::LatentStatus ShowSkipWeddingModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                    RE::VMStackID stackID,
                                                    RE::StaticFunctionTag*,
                                                    RE::Actor* fiance);
    RE::BSScript::LatentStatus ShowDivorceSettlementModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                          RE::VMStackID stackID,
                                                          RE::StaticFunctionTag*,
                                                          RE::Actor* exSpouse,
                                                          int32_t totalFee,
                                                          int32_t goldShare,
                                                          int32_t assetsShare,
                                                          bool canPayDirectly);
    RE::BSScript::LatentStatus ShowChooseHomeModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                   RE::VMStackID stackID,
                                                   RE::StaticFunctionTag*,
                                                   RE::Actor* targetActor,
                                                   bool isChild);
    RE::BSScript::LatentStatus ShowChooseAdoptionSpouseModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                             RE::VMStackID stackID,
                                                             RE::StaticFunctionTag*,
                                                             RE::Actor* child);
    RE::BSScript::LatentStatus ShowDebugStatusModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                    RE::VMStackID stackID,
                                                    RE::StaticFunctionTag*,
                                                    RE::Actor* target);
    RE::BSScript::LatentStatus ShowChildMigrationAuditModal(RE::BSScript::Internal::VirtualMachine* vm,
                                                            RE::VMStackID stackID,
                                                            RE::StaticFunctionTag*);
    int32_t CheckAndSetActionCooldown(RE::StaticFunctionTag*,
                                      RE::BSFixedString actionType,
                                      std::vector<RE::Actor*> actors);

    bool RegisterIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, std::vector<RE::Actor*> actors);
    void ReportIntimateInteraction(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID, RE::Actor* actorA, RE::Actor* actorB);
    void ClearIntimateThread(RE::StaticFunctionTag*, RE::BSFixedString framework, int32_t threadID);

    std::vector<RE::Actor*> FindBanterCoupleCandidates(RE::StaticFunctionTag*, float afScanRadius = 3000.0f);
    bool IsBanterPartnerPresent(RE::StaticFunctionTag*, RE::Actor* akPartner, float afScanRadius = 3000.0f);
    void ResetBanterCoupleCooldown(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB);
    void ClearAllBanterCooldowns(RE::StaticFunctionTag*);
    float GetBanterCoupleCooldownHours(RE::StaticFunctionTag*);
    void SetBanterCoupleCooldownHours(RE::StaticFunctionTag*, float afHours);
    void RecordScenePlayed(RE::StaticFunctionTag*, RE::Actor* akActorA, RE::Actor* akActorB);
    bool IsLocationAllowedForScenes(RE::StaticFunctionTag*, RE::BGSLocation* akLocation);

    // Registration function for SKSE
    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm);

    // Workaround for CommonLibSSE-NG NativeLatentFunction bug
    template <class LatentR, class Fn, class Cls, class... Args>
    class FixedNativeLatentFunction :
        public RE::BSScript::NativeFunction<
            true,
            RE::BSScript::LatentStatus(RE::BSScript::Internal::VirtualMachine*, RE::VMStackID, Cls, Args...),
            std::underlying_type_t<RE::BSScript::LatentStatus>,
            Cls, Args...>
    {
        using Super = RE::BSScript::NativeFunction<
            true,
            RE::BSScript::LatentStatus(RE::BSScript::Internal::VirtualMachine*, RE::VMStackID, Cls, Args...),
            std::underlying_type_t<RE::BSScript::LatentStatus>,
            Cls, Args...>;
    public:
        FixedNativeLatentFunction(std::string_view fnName, std::string_view className, Fn* callback)
            : Super(fnName, className, callback)
        {
            this->_retType  = RE::BSScript::GetRawType<LatentR>{}();
            this->_isLatent = true;
        }
    };

    template <class LatentR, class... Args>
    static void RegisterLatentFixed(RE::BSScript::IVirtualMachine* vm,
                                     std::string_view fnName,
                                     std::string_view className,
                                     RE::BSScript::LatentStatus(*callback)(RE::BSScript::Internal::VirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*, Args...)) {
        vm->BindNativeMethod(
            new FixedNativeLatentFunction<LatentR,
                RE::BSScript::LatentStatus(RE::BSScript::Internal::VirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*, Args...),
                RE::StaticFunctionTag*,
                Args...>(fnName, className, callback));
    }

}  // namespace MARAS::PapyrusInterface
