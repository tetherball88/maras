#include "papyrus/PapyrusInterface.h"

#include "core/NameService.h"
#include "core/NPCIndex.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "services/ActorService.h"
#include "utils/Common.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

namespace MARAS::PapyrusInterface {

    // ========================================
    // Children registry & household management
    // ========================================

    bool RegisterChildActor(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* guardianA, RE::Actor* guardianB, RE::BGSLocation* houseLoc, RE::Actor* bioMother, RE::Actor* bioFather, int32_t syntheticID, int32_t stage, bool sendEvent) {
        if (!child) return false;
        if (stage < 0 || stage > static_cast<int32_t>(ChildStage::Adult)) {
            MARAS_LOG_WARN("RegisterChildActor: out-of-range ChildStage {}", stage);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().RegisterChildActor(
            child->GetFormID(),
            guardianA ? guardianA->GetFormID() : 0,
            guardianB ? guardianB->GetFormID() : 0,
            houseLoc ? houseLoc->GetFormID() : 0,
            bioMother ? bioMother->GetFormID() : 0,
            bioFather ? bioFather->GetFormID() : 0,
            syntheticID,
            static_cast<uint8_t>(stage),
            std::nullopt,
            sendEvent);
    }

    bool RemoveChild(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().RemoveChild(child->GetFormID());
    }

    std::vector<RE::Actor*> GetChildren(RE::StaticFunctionTag*, RE::Actor* parentA, RE::Actor* parentB) {
        std::vector<RE::Actor*> result;
        if (!parentA) return result;
        RE::FormID pAID = parentA->GetFormID();
        RE::FormID pBID = parentB ? parentB->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        auto childIDs = NPCRelationshipManager::GetSingleton().GetChildren(pAID, pBID);
        for (auto cID : childIDs) {
            if (auto* a = NPCIndex::GetSingleton().GetActorFromFormId(cID)) {
                result.push_back(a);
            }
        }
        return result;
    }

    std::vector<RE::Actor*> GetChildrenOfActor(RE::StaticFunctionTag*, RE::Actor* parent) {
        std::vector<RE::Actor*> result;
        if (!parent) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = NPCRelationshipManager::GetSingleton().GetChildrenOfActor(parent->GetFormID());
        for (auto id : ids) {
            if (auto* a = NPCIndex::GetSingleton().GetActorFromFormId(id)) {
                result.push_back(a);
            }
        }
        return result;
    }

    void UpdateSpouseChildrenFaction(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) return;
        std::lock_guard lock(GetPapyrusMutex());
        NPCRelationshipManager::GetSingleton().UpdateSpouseChildrenFaction(spouse->GetFormID());
    }

    bool AssignChildToHouse(RE::StaticFunctionTag*, RE::Actor* child, RE::BGSLocation* houseLoc, bool /*moveChild*/) {
        if (!child || !houseLoc) return false;
        std::lock_guard lock(GetPapyrusMutex());
        auto res = MARAS::PlayerHouseService::GetSingleton().RelocateFamilyUnit(child->GetFormID(), houseLoc->GetFormID(), true);
        return !res.empty();
    }

    bool IssueChildOrder(RE::StaticFunctionTag*, RE::Actor* child, float orderValue, float durationHours) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IssueOrder(child->GetFormID(), orderValue, durationHours);
    }

    void ResetChildForceGreeting(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().ResetForceGreeting(child->GetFormID());
    }

    bool AssignChildPet(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* pet) {
        if (!child) return false;
        RE::FormID petID = pet ? pet->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().AssignPet(child->GetFormID(), petID);
    }

    RE::Actor* GetChildPet(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID petID = PlayerChildrenService::GetSingleton().GetPet(child->GetFormID());
        return petID != 0 ? NPCIndex::GetSingleton().GetActorFromFormId(petID) : nullptr;
    }

    RE::Actor* GetChildForPet(RE::StaticFunctionTag*, RE::Actor* pet) {
        if (!pet) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID childID = PlayerChildrenService::GetSingleton().GetChildForPet(pet->GetFormID());
        return childID != 0 ? NPCIndex::GetSingleton().GetActorFromFormId(childID) : nullptr;
    }

    bool RemoveChildPet(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().RemovePet(child->GetFormID());
    }

    void SetChildPetBanned(RE::StaticFunctionTag*, RE::Actor* child, bool banned) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().SetChildPetBanned(child->GetFormID(), banned);
    }

    bool IsChildPetBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsChildPetBanned(child->GetFormID());
    }

    bool IsPetBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        RE::FormID childID = child ? child->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsPetBanned(childID);
    }

    void SetGlobalPetBanned(RE::StaticFunctionTag*, bool banned) {
        Settings::GetSingleton().SetBoolByKey("bGlobalBanPet", banned);
    }

    bool IsGlobalPetBanned(RE::StaticFunctionTag*) {
        return Settings::GetSingleton().GetBoolByKey("bGlobalBanPet");
    }

    void SetChildPetRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child, float timestamp) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().SetPetRetryTimestamp(child->GetFormID(), timestamp);
    }

    void RecordChildPetRetry(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().RecordPetRejected(child->GetFormID());
    }

    float GetChildPetRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return 0.0f;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().GetPetRetryTimestamp(child->GetFormID());
    }

    bool IsPetRetryAvailable(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return true;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsPetRetryAvailable(child->GetFormID());
    }

    bool AssignChildCritter(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* critter) {
        if (!child) return false;
        RE::FormID critterID = critter ? critter->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().AssignCritter(child->GetFormID(), critterID);
    }

    RE::Actor* GetChildCritter(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID critterID = PlayerChildrenService::GetSingleton().GetCritter(child->GetFormID());
        return critterID != 0 ? NPCIndex::GetSingleton().GetActorFromFormId(critterID) : nullptr;
    }

    RE::Actor* GetChildForCritter(RE::StaticFunctionTag*, RE::Actor* critter) {
        if (!critter) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID childID = PlayerChildrenService::GetSingleton().GetChildForCritter(critter->GetFormID());
        return childID != 0 ? NPCIndex::GetSingleton().GetActorFromFormId(childID) : nullptr;
    }

    RE::Actor* GetChildForPetOrCritter(RE::StaticFunctionTag*, RE::Actor* petOrCritter) {
        if (!petOrCritter) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID childID = PlayerChildrenService::GetSingleton().GetChildForPetOrCritter(petOrCritter->GetFormID());
        return childID != 0 ? NPCIndex::GetSingleton().GetActorFromFormId(childID) : nullptr;
    }

    bool RemoveChildCritter(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().RemoveCritter(child->GetFormID());
    }

    void SetChildCritterBanned(RE::StaticFunctionTag*, RE::Actor* child, bool banned) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().SetChildCritterBanned(child->GetFormID(), banned);
    }

    bool IsChildCritterBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsChildCritterBanned(child->GetFormID());
    }

    bool IsCritterBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        RE::FormID childID = child ? child->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsCritterBanned(childID);
    }

    void SetGlobalCritterBanned(RE::StaticFunctionTag*, bool banned) {
        Settings::GetSingleton().SetBoolByKey("bGlobalBanCritter", banned);
    }

    bool IsGlobalCritterBanned(RE::StaticFunctionTag*) {
        return Settings::GetSingleton().GetBoolByKey("bGlobalBanCritter");
    }

    void SetChildCritterRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child, float timestamp) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().SetCritterRetryTimestamp(child->GetFormID(), timestamp);
    }

    void RecordChildCritterRetry(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().RecordCritterRejected(child->GetFormID());
    }

    float GetChildCritterRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return 0.0f;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().GetCritterRetryTimestamp(child->GetFormID());
    }

    bool IsChildCritterRetryAvailable(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return true;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsCritterRetryAvailable(child->GetFormID());
    }

    void SetChildIsNewlyAdopted(RE::StaticFunctionTag*, RE::Actor* child, bool isNewlyAdopted, bool clearSameHouse) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().SetIsNewlyAdopted(child->GetFormID(), isNewlyAdopted, clearSameHouse);
    }

    bool IsChildNewlyAdopted(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsNewlyAdopted(child->GetFormID());
    }

    bool IsChildPlayerAdopted(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return false;
        RE::FormID childID = child->GetFormID();
        std::lock_guard lock(GetPapyrusMutex());
        if (NPCRelationshipManager::GetSingleton().IsGuardian(childID, kPlayerFormID)) {
            MARAS_LOG_DEBUG("Child {:08X} has player as guardian", childID);
            return true;
        }
        if (PlayerChildrenService::GetSingleton().GetState(childID).has_value()) {
            MARAS_LOG_DEBUG("Child {:08X} in PlayerChildrenService", childID);
            return true;
        }
        return false;
    }

    void SetChildChestIntroduced(RE::StaticFunctionTag*, bool introduced) {
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().SetChildChestIntroduced(introduced);
    }

    bool IsChildChestIntroduced(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().IsChildChestIntroduced();
    }

    std::vector<RE::Actor*> GetAllPlayerChildren(RE::StaticFunctionTag*) {
        std::vector<RE::Actor*> result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = PlayerChildrenService::GetSingleton().GetAllPlayerChildren(true);
        for (auto id : ids) {
            if (auto* a = NPCIndex::GetSingleton().GetActorFromFormId(id)) {
                result.push_back(a);
            }
        }
        return result;
    }

    std::vector<RE::Actor*> GetChildrenAssignedToHouse(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        std::vector<RE::Actor*> result;
        if (!playerHouse) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = PlayerChildrenService::GetSingleton().GetChildrenForHouse(playerHouse->GetFormID(), true);
        result.reserve(ids.size());
        for (auto id : ids) {
            if (auto* a = NPCIndex::GetSingleton().GetActorFromFormId(id)) {
                result.push_back(a);
            }
        }
        return result;
    }

    RE::Actor* GetNewlyAdoptedChild(RE::StaticFunctionTag*, RE::BGSLocation* loc) {
        if (!loc) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        auto id = PlayerChildrenService::GetSingleton().GetNewlyAdoptedChild(loc->GetFormID());
        return NPCIndex::GetSingleton().GetActorFromFormId(id);
    }

    RE::Actor* GetChildWithoutPet(RE::StaticFunctionTag*, RE::BGSLocation* loc) {
        if (!loc) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        auto id = PlayerChildrenService::GetSingleton().GetChildWithoutPet(loc->GetFormID());
        return NPCIndex::GetSingleton().GetActorFromFormId(id);
    }

    RE::Actor* GetChildWithoutCritter(RE::StaticFunctionTag*, RE::BGSLocation* loc) {
        if (!loc) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        auto id = PlayerChildrenService::GetSingleton().GetChildWithoutCritter(loc->GetFormID());
        return NPCIndex::GetSingleton().GetActorFromFormId(id);
    }

    std::vector<RE::Actor*> GetChildrenWithoutScene(RE::StaticFunctionTag*, RE::BGSLocation* loc, std::int32_t sceneID) {
        std::vector<RE::Actor*> result;
        if (!loc) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto ids = PlayerChildrenService::GetSingleton().GetChildrenWithoutScene(loc->GetFormID(), sceneID);
        result.reserve(ids.size());
        for (auto id : ids) {
            if (auto* a = NPCIndex::GetSingleton().GetActorFromFormId(id)) {
                result.push_back(a);
            }
        }
        return result;
    }

    RE::Actor* GetWeightedRandomChild(RE::StaticFunctionTag*, RE::BGSLocation* loc) {
        if (!loc) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        auto id = PlayerChildrenService::GetSingleton().GetWeightedRandomChild(loc->GetFormID());
        return NPCIndex::GetSingleton().GetActorFromFormId(id);
    }

    void SetChildPlayedScene(RE::StaticFunctionTag*, RE::Actor* child, std::int32_t sceneID, bool played) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().SetChildPlayedScene(child->GetFormID(), sceneID, played);
    }

    bool HasChildPlayedScene(RE::StaticFunctionTag*, RE::Actor* child, std::int32_t sceneID) {
        if (!child) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().HasChildPlayedScene(child->GetFormID(), sceneID);
    }

    void RecordChildInteraction(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return;
        std::lock_guard lock(GetPapyrusMutex());
        PlayerChildrenService::GetSingleton().RecordChildInteraction(child->GetFormID());
    }

    float GetChildLastInteractionTimestamp(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) return 0.0f;
        std::lock_guard lock(GetPapyrusMutex());
        return PlayerChildrenService::GetSingleton().GetLastInteractionTimestamp(child->GetFormID());
    }

    // ========================================
    // Fertility Mode Reloaded & Paternity Intrigue
    // ========================================

    RE::Actor* FindActorByName(RE::StaticFunctionTag*, std::string name) {
        if (name.empty()) return nullptr;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            std::string pName = ActorSvc::GetName(kPlayerFormID);
            if (StringUtils::CaseInsensitiveEquals(name, pName) ||
                StringUtils::CaseInsensitiveEquals(name, "Player") ||
                StringUtils::CaseInsensitiveEquals(name, "The Dragonborn")) {
                return player;
            }
        }

        auto entry = NPCIndex::GetSingleton().FindByName(name);
        if (!entry) return nullptr;

        if (!entry->isUnique) {
            MARAS_LOG_INFO("[NPCIndex] FindActorByName: '{}' is non-unique NPC (guard/soldier/generic); returning none", name);
            return nullptr;
        }

        RE::FormID refID = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
        return NPCIndex::GetSingleton().GetActorFromFormId(refID);
    }

    void SetNPCPregnant(RE::StaticFunctionTag*, RE::Actor* mother, bool pregnant, RE::Actor* bioFather, RE::Actor* partner, float conceptionTime) {
        if (!mother) return;
        RE::FormID bID = bioFather ? bioFather->GetFormID() : 0;
        RE::FormID pID = partner ? partner->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        NPCRelationshipManager::GetSingleton().SetNPCPregnant(mother->GetFormID(), pregnant, bID, pID, conceptionTime);
    }

    bool IsNPCPregnant(RE::StaticFunctionTag*, RE::Actor* mother) {
        if (!mother) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsNPCPregnant(mother->GetFormID());
    }

    int32_t RegisterUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* bioFather, float conceptionTime, bool evaluateStepIn) {
        if (!mother) return 0;
        RE::FormID mID = mother->GetFormID();
        RE::FormID bID = bioFather ? bioFather->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        auto assignedID = NPCRelationshipManager::GetSingleton().RegisterUnbornChild(mID, bID, conceptionTime, evaluateStepIn);
        return static_cast<int32_t>(assignedID);
    }

    int32_t PromoteUnbornToInfant(RE::StaticFunctionTag*, RE::Actor* mother, float birthTime, std::string name, RE::Actor* bioFather) {
        if (!mother) return 0;
        RE::FormID bID = bioFather ? bioFather->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int32_t>(NPCRelationshipManager::GetSingleton().PromoteUnbornToInfant(mother->GetFormID(), birthTime, name, bID));
    }

    bool RemoveUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* bioFather) {
        if (!mother) return false;
        RE::FormID bID = bioFather ? bioFather->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().RemoveUnbornChild(mother->GetFormID(), bID);
    }

    int32_t FindUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother) {
        if (!mother) return 0;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int32_t>(NPCRelationshipManager::GetSingleton().FindUnbornChild(mother->GetFormID()));
    }

    std::vector<int32_t> GetUnbornChildren(RE::StaticFunctionTag*, RE::Actor* mother) {
        std::vector<int32_t> result;
        if (!mother) return result;
        std::lock_guard lock(GetPapyrusMutex());
        auto unborn = NPCRelationshipManager::GetSingleton().GetUnbornChildren(mother->GetFormID());
        result.reserve(unborn.size());
        for (auto id : unborn) {
            result.push_back(static_cast<int32_t>(id));
        }
        return result;
    }

    int32_t GetUnbornChildCount(RE::StaticFunctionTag*, RE::Actor* mother) {
        if (!mother) return 0;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int32_t>(NPCRelationshipManager::GetSingleton().GetUnbornChildren(mother->GetFormID()).size());
    }

    int32_t RegisterFertilityChild(RE::StaticFunctionTag*, int32_t syntheticID, RE::Actor* mother, RE::Actor* partner, RE::Actor* bioFather, float conception, float birth, std::string name) {
        if (!mother) return 0;
        RE::FormID mID = mother->GetFormID();
        RE::FormID pID = partner ? partner->GetFormID() : 0;
        RE::FormID bID = bioFather ? bioFather->GetFormID() : 0;
        RE::FormID cID = static_cast<RE::FormID>(syntheticID);
        std::lock_guard lock(GetPapyrusMutex());
        auto assignedID = NPCRelationshipManager::GetSingleton().RegisterFertilityChild(cID, mID, pID, bID, conception, birth, name);
        return static_cast<int32_t>(assignedID);
    }

    bool ResolvePaternitySecret(RE::StaticFunctionTag*, int32_t childID, bool discoveredByPartner) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().ResolvePaternitySecret(static_cast<RE::FormID>(childID), discoveredByPartner);
    }

    bool ResolvePaternitySecretForPartner(RE::StaticFunctionTag*, int32_t childID, bool discoveredByPartner) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().ResolvePaternitySecretForPartner(static_cast<RE::FormID>(childID), discoveredByPartner);
    }

    bool ResolvePaternitySecretForBioFather(RE::StaticFunctionTag*, int32_t childID, bool discoveredByBioFather) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().ResolvePaternitySecretForBioFather(static_cast<RE::FormID>(childID), discoveredByBioFather);
    }

    bool SetPaternitySecretEdges(RE::StaticFunctionTag*, int32_t childID, bool secretForBioFather, bool secretForPartner) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetPaternitySecretEdges(static_cast<RE::FormID>(childID), secretForBioFather, secretForPartner);
    }

    bool IsSecretForBioFather(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsSecretForBioFather(static_cast<RE::FormID>(childID));
    }

    bool IsSecretForPartner(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsSecretForPartner(static_cast<RE::FormID>(childID));
    }

    bool SetChildStage(RE::StaticFunctionTag*, int32_t childID, int32_t stage) {
        if (stage < 0 || stage > static_cast<int32_t>(ChildStage::Adult)) {
            MARAS_LOG_WARN("SetChildStage: out-of-range ChildStage {}", stage);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetChildStage(
            static_cast<RE::FormID>(childID),
            static_cast<ChildStage>(stage));
    }

    int32_t GetChildStage(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int32_t>(NPCRelationshipManager::GetSingleton().GetChildStage(
            static_cast<RE::FormID>(childID)));
    }

    bool SetChildTrainingLocation(RE::StaticFunctionTag*, int32_t childID, std::string location) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetChildTrainingLocation(
            static_cast<RE::FormID>(childID),
            location);
    }

    std::string GetChildTrainingLocation(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().GetChildTrainingLocation(static_cast<RE::FormID>(childID));
    }

    bool SetChildGender(RE::StaticFunctionTag*, int32_t childID, int32_t gender) {
        if (gender < 0 || gender > 2) {
            MARAS_LOG_WARN("SetChildGender: out-of-range gender {} (expected 0..2)", gender);
            return false;
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetChildGender(
            static_cast<RE::FormID>(childID), static_cast<uint8_t>(gender));
    }

    int32_t GetChildGender(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int32_t>(NPCRelationshipManager::GetSingleton().GetChildGender(
            static_cast<RE::FormID>(childID)));
    }

    bool SetChildGuardian(RE::StaticFunctionTag*, int32_t childID, int32_t slot, RE::Actor* guardian) {
        if (slot != 0 && slot != 1) {
            MARAS_LOG_WARN("SetChildGuardian: invalid guardian slot {} (expected 0 or 1)", slot);
            return false;
        }
        RE::FormID gID = guardian ? guardian->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetChildGuardian(static_cast<RE::FormID>(childID), slot, gID);
    }

    int32_t FindSyntheticChild(RE::StaticFunctionTag*, RE::Actor* mother, std::string name) {
        RE::FormID mID = mother ? mother->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int32_t>(NPCRelationshipManager::GetSingleton().FindSyntheticChild(mID, name));
    }

    bool UpdateChildName(RE::StaticFunctionTag*, int32_t childID, std::string newName) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().UpdateChildName(
            static_cast<RE::FormID>(childID),
            newName);
    }

    RE::Actor* GetPrimaryPartner(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID partnerID = NPCRelationshipManager::GetSingleton().GetPrimaryPartner(actor->GetFormID());
        if (partnerID == 0) return nullptr;
        auto* refr = ActorSvc::GetReference(partnerID);
        return refr ? refr->As<RE::Actor>() : nullptr;
    }

    RE::Actor* GetGuardianA(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID gID = NPCRelationshipManager::GetSingleton().GetGuardianA(static_cast<RE::FormID>(childID));
        if (gID == 0) return nullptr;
        auto* refr = ActorSvc::GetReference(gID);
        return refr ? refr->As<RE::Actor>() : nullptr;
    }

    RE::Actor* GetGuardianB(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID gID = NPCRelationshipManager::GetSingleton().GetGuardianB(static_cast<RE::FormID>(childID));
        if (gID == 0) return nullptr;
        auto* refr = ActorSvc::GetReference(gID);
        return refr ? refr->As<RE::Actor>() : nullptr;
    }

    std::vector<RE::Actor*> GetGuardians(RE::StaticFunctionTag*, int32_t childID) {
        std::vector<RE::Actor*> result;
        std::lock_guard lock(GetPapyrusMutex());
        auto [gA, gB] = NPCRelationshipManager::GetSingleton().GetGuardians(static_cast<RE::FormID>(childID));
        for (auto gID : { gA, gB }) {
            if (gID != 0) {
                if (auto* refr = ActorSvc::GetReference(gID)) {
                    if (auto* act = refr->As<RE::Actor>()) {
                        result.push_back(act);
                    }
                }
            }
        }
        return result;
    }

    RE::Actor* GetCoGuardian(RE::StaticFunctionTag*, int32_t childID, RE::Actor* actor) {
        if (!actor) return nullptr;
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID coID = NPCRelationshipManager::GetSingleton().GetCoGuardian(static_cast<RE::FormID>(childID), actor->GetFormID());
        if (coID == 0) return nullptr;
        auto* refr = ActorSvc::GetReference(coID);
        return refr ? refr->As<RE::Actor>() : nullptr;
    }

    bool IsGuardian(RE::StaticFunctionTag*, int32_t childID, RE::Actor* actor) {
        if (!actor) return false;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().IsGuardian(static_cast<RE::FormID>(childID), actor->GetFormID());
    }

    RE::Actor* GetBioMother(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID mID = NPCRelationshipManager::GetSingleton().GetBioMother(static_cast<RE::FormID>(childID));
        if (mID == 0) return nullptr;
        auto* refr = ActorSvc::GetReference(mID);
        return refr ? refr->As<RE::Actor>() : nullptr;
    }

    RE::Actor* GetBioFather(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID bID = NPCRelationshipManager::GetSingleton().GetBioFather(static_cast<RE::FormID>(childID));
        if (bID == 0) return nullptr;
        auto* refr = ActorSvc::GetReference(bID);
        return refr ? refr->As<RE::Actor>() : nullptr;
    }

    RE::Actor* GetMotherPartner(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        RE::FormID pID = NPCRelationshipManager::GetSingleton().GetMotherPartner(static_cast<RE::FormID>(childID));
        if (pID == 0) return nullptr;
        auto* refr = ActorSvc::GetReference(pID);
        return refr ? refr->As<RE::Actor>() : nullptr;
    }

    bool SetBioMother(RE::StaticFunctionTag*, int32_t childID, RE::Actor* mother) {
        RE::FormID mID = mother ? mother->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetBioMother(static_cast<RE::FormID>(childID), mID);
    }

    bool SetBioFather(RE::StaticFunctionTag*, int32_t childID, RE::Actor* father) {
        RE::FormID fID = father ? father->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().SetBioFather(static_cast<RE::FormID>(childID), fID);
    }

    std::string GetChildName(RE::StaticFunctionTag*, int32_t childID) {
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().GetChildName(static_cast<RE::FormID>(childID));
    }

    int32_t FindChildPaternitySecret(RE::StaticFunctionTag*, RE::Actor* parent1, RE::Actor* parent2, std::string childName) {
        RE::FormID p1 = parent1 ? parent1->GetFormID() : 0;
        RE::FormID p2 = parent2 ? parent2->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return static_cast<int32_t>(NPCRelationshipManager::GetSingleton().FindChildPaternitySecret(p1, p2, childName));
    }

    std::string BuildPregnancyAnnouncedMessage(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* partner, RE::Actor* bioFather, std::string bioFatherName, int32_t childCount) {
        if (!mother) return "";
        RE::FormID mID = mother->GetFormID();
        RE::FormID pID = partner ? partner->GetFormID() : 0;
        RE::FormID bID = bioFather ? bioFather->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().BuildPregnancyAnnouncedMessage(mID, pID, bID, bioFatherName, childCount);
    }

    std::string BuildPregnancyAnnouncedJson(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* partner, RE::Actor* bioFather, std::string bioFatherName, int32_t childCount) {
        if (!mother) return "";
        RE::FormID mID = mother->GetFormID();
        RE::FormID pID = partner ? partner->GetFormID() : 0;
        RE::FormID bID = bioFather ? bioFather->GetFormID() : 0;
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().BuildPregnancyAnnouncedJson(mID, pID, bID, bioFatherName, childCount);
    }

    std::string BuildChildBirthMessage(RE::StaticFunctionTag*, RE::Actor* mother, std::vector<int32_t> childIDs) {
        if (!mother) return "";
        RE::FormID mID = mother->GetFormID();
        std::vector<RE::FormID> cIDs;
        cIDs.reserve(childIDs.size());
        for (int32_t id : childIDs) {
            cIDs.push_back(static_cast<RE::FormID>(id));
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().BuildChildBirthMessage(mID, cIDs);
    }

    std::string BuildChildBirthJson(RE::StaticFunctionTag*, RE::Actor* mother, std::vector<int32_t> childIDs) {
        if (!mother) return "";
        RE::FormID mID = mother->GetFormID();
        std::vector<RE::FormID> cIDs;
        cIDs.reserve(childIDs.size());
        for (int32_t id : childIDs) {
            cIDs.push_back(static_cast<RE::FormID>(id));
        }
        std::lock_guard lock(GetPapyrusMutex());
        return NPCRelationshipManager::GetSingleton().BuildChildBirthJson(mID, cIDs);
    }

    // ========================================
    // Child & Critter name generation
    // ========================================

    std::string GenerateRandomChildName(RE::StaticFunctionTag*, std::string raceName, bool isFemale, bool isUnique) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetRandomChildName(raceName, isFemale, isUnique);
    }

    std::string GenerateRandomChildNameForActor(RE::StaticFunctionTag*, RE::Actor* actor, bool isUnique) {
        if (!actor) return "";
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetRandomChildName(actor, isUnique);
    }

    std::string GenerateRandomChildNameForRace(RE::StaticFunctionTag*, RE::TESForm* raceForm, bool isFemale, bool isUnique) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetRandomChildName(raceForm, isFemale, isUnique);
    }

    std::vector<std::string> GetChildNames(RE::StaticFunctionTag*, std::string raceName, bool isFemale) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetChildNames(raceName, isFemale);
    }

    bool ReloadChildNames(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().LoadChildNamesFromFile();
    }

    std::string GenerateRandomCritterName(RE::StaticFunctionTag*, std::string raceName) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetRandomCritterName(raceName);
    }

    std::string GenerateRandomCritterNameForActor(RE::StaticFunctionTag*, RE::Actor* actor) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetRandomCritterName(actor);
    }

    std::string GenerateRandomCritterNameForRace(RE::StaticFunctionTag*, RE::TESForm* raceForm) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetRandomCritterName(raceForm);
    }

    std::vector<std::string> GetCritterNames(RE::StaticFunctionTag*, std::string raceName) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().GetCritterNames(raceName);
    }

    bool ReloadCritterNames(RE::StaticFunctionTag*) {
        std::lock_guard lock(GetPapyrusMutex());
        return NameService::GetSingleton().LoadCritterNamesFromFile();
    }

}  // namespace MARAS::PapyrusInterface

