#include "maras/bridge_papyrus/PapyrusInterface.h"

#include "maras/app/FertilityLifecycleCoordinator.h"
#include "maras/app/HouseholdUseCases.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/domain/lineage/LineageRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/NamePoolLoader.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <vector>

namespace MARAS::PapyrusInterface {

    namespace {
        [[nodiscard]] RE::Actor* ResolveActorById(RE::FormID id) {
            if (!HasPhysicalActor(id)) {
                return nullptr;
            }
            if (id == kPlayerFormID) {
                return RE::PlayerCharacter::GetSingleton();
            }
            return SpatialWorldIndex::GetSingleton().GetActorFromFormId(id);
        }

        [[nodiscard]] std::vector<RE::Actor*> ResolveActorsById(const std::vector<RE::FormID>& ids) {
            std::vector<RE::Actor*> result;
            result.reserve(ids.size());
            for (const RE::FormID id : ids) {
                if (auto* actor = ResolveActorById(id)) {
                    result.push_back(actor);
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<RE::Actor*> ResolveActorsByChildIds(const std::vector<State::ChildId>& ids) {
            std::vector<RE::Actor*> result;
            result.reserve(ids.size());
            for (const State::ChildId cid : ids) {
                if (auto* actor = ResolveActorById(cid.raw())) {
                    result.push_back(actor);
                }
            }
            return result;
        }

        [[nodiscard]] float GetCurrentGameDays() {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetDaysPassed() : 0.0f;
        }

        template <class Fn>
        bool MutateChildHouseholdState(RE::FormID childID, Fn&& fn) {
            if (childID == 0) {
                return false;
            }
            bool updated = false;
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                if (auto* st = ws.household.Write().FindChildStateMutable(State::ChildId{ childID })) {
                    fn(*st);
                    updated = true;
                }
            });
            return updated;
        }

        void EnsureChildNamesLoaded() {
            auto& loader = Engine::NamePoolLoader::GetSingleton();
            if (!loader.IsChildLoaded()) {
                (void)loader.LoadChildNamesFromFile();
            }
        }

        void EnsureCritterNamesLoaded() {
            auto& loader = Engine::NamePoolLoader::GetSingleton();
            if (!loader.IsCritterLoaded()) {
                (void)loader.LoadCritterNamesFromFile();
            }
        }

        [[nodiscard]] std::string ExtractRaceLabelFromForm(RE::TESForm* form) {
            if (!form) {
                return "";
            }
            if (auto* race = form->As<RE::TESRace>()) {
                if (const char* edid = race->GetFormEditorID(); edid && *edid) {
                    return edid;
                }
                if (const char* fullName = race->GetFullName(); fullName && *fullName) {
                    return fullName;
                }
            }
            if (auto* npc = form->As<RE::TESNPC>()) {
                if (auto* race = npc->GetRace()) {
                    if (const char* edid = race->GetFormEditorID(); edid && *edid) {
                        return edid;
                    }
                    if (const char* fullName = race->GetFullName(); fullName && *fullName) {
                        return fullName;
                    }
                }
            }
            return ActorSvc::GetRaceName(form->GetFormID());
        }
    }  // namespace

    // ========================================
    // Children System
    // ========================================

    bool RegisterChildActor(RE::StaticFunctionTag*,
                            RE::Actor* child,
                            RE::Actor* guardianA,
                            RE::Actor* guardianB,
                            RE::BGSLocation* houseLocation,
                            RE::Actor* bioMother,
                            RE::Actor* bioFather,
                            int32_t syntheticID,
                            int32_t stage,
                            bool sendEvent) {
        if (!child) {
            MARAS_LOG_WARN("RegisterChildActor: null child actor provided");
            return false;
        }
        if (stage < 0 || stage >= static_cast<int32_t>(Foundation::ChildStage::_Count)) {
            MARAS_LOG_WARN("RegisterChildActor: out-of-range stage {} for child {:08X}",
                           stage, child->GetFormID());
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().RegisterChildActor(
            child->GetFormID(),
            guardianA ? guardianA->GetFormID() : 0,
            guardianB ? guardianB->GetFormID() : 0,
            houseLocation ? houseLocation->GetFormID() : 0,
            bioMother ? bioMother->GetFormID() : 0,
            bioFather ? bioFather->GetFormID() : 0,
            syntheticID,
            static_cast<std::uint8_t>(stage),
            std::nullopt,
            sendEvent);
    }

    bool RemoveChild(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            MARAS_LOG_WARN("RemoveChild: null child actor provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().RemoveChild(child->GetFormID());
    }

    std::vector<RE::Actor*> GetChildren(RE::StaticFunctionTag*, RE::Actor* parentA, RE::Actor* parentB) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID idA = parentA ? parentA->GetFormID() : 0;
        const RE::FormID idB = parentB ? parentB->GetFormID() : 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->lineage) {
            return {};
        }
        return ResolveActorsByChildIds(
            snap->lineage->GetChildrenWithGuardianSet(State::ActorId{ idA }, State::ActorId{ idB }));
    }

    std::vector<RE::Actor*> GetChildrenOfActor(RE::StaticFunctionTag*, RE::Actor* parent) {
        if (!parent) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(
            App::LineageUseCases::GetSingleton().GetChildrenOfParent(parent->GetFormID()));
    }

    void UpdateSpouseChildrenFaction(RE::StaticFunctionTag*, RE::Actor* spouse) {
        if (!spouse) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::LineageUseCases::GetSingleton().UpdateSpouseChildrenFaction(spouse->GetFormID());
    }

    // ========================================
    // Player Children Household & Companions
    // ========================================

    bool AssignChildToHouse(RE::StaticFunctionTag*, RE::Actor* child, RE::BGSLocation* houseLoc, bool /*moveChild*/) {
        if (!child || !houseLoc) {
            MARAS_LOG_WARN("AssignChildToHouse: null child or houseLoc provided");
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto res = App::HouseholdUseCases::GetSingleton().RelocateFamilyUnit(
            child->GetFormID(), houseLoc->GetFormID(), /*moveParents=*/true);
        return !res.empty();
    }

    bool IssueChildOrder(RE::StaticFunctionTag*, RE::Actor* child, float orderValue, float durationHours) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IssueOrder(child->GetFormID(), orderValue, durationHours);
    }

    void ResetChildForceGreeting(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::LineageUseCases::GetSingleton().ClearForcegreet(child->GetFormID());
    }

    bool AssignChildPet(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* pet) {
        if (!child) {
            return false;
        }

        const RE::FormID petID = pet ? pet->GetFormID() : 0;
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().AssignPet(child->GetFormID(), petID);
    }

    RE::Actor* GetChildPet(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(App::LineageUseCases::GetSingleton().GetPet(child->GetFormID()));
    }

    RE::Actor* GetChildForPet(RE::StaticFunctionTag*, RE::Actor* pet) {
        if (!pet) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(App::LineageUseCases::GetSingleton().GetChildForPet(pet->GetFormID()));
    }

    bool RemoveChildPet(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().RemovePet(child->GetFormID());
    }

    void SetChildPetBanned(RE::StaticFunctionTag*, RE::Actor* child, bool banned) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        if (MutateChildHouseholdState(child->GetFormID(), [banned](State::ChildHouseholdRecord& st) {
                st.petBanned = banned;
            })) {
            MARAS_LOG_INFO("SetChildPetBanned: child {:08X} petBanned={}", child->GetFormID(), banned);
        }
    }

    bool IsChildPetBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsChildPetBanned(child->GetFormID());
    }

    bool IsPetBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsPetBanned(child->GetFormID());
    }

    void SetGlobalPetBanned(RE::StaticFunctionTag*, bool banned) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::SettingsCoordinator::GetSingleton().SetBoolByKey("bGlobalBanPet", banned);
    }

    bool IsGlobalPetBanned(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetBoolByKey("bGlobalBanPet");
    }

    void SetChildPetRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child, float timestamp) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        if (MutateChildHouseholdState(child->GetFormID(), [timestamp](State::ChildHouseholdRecord& st) {
                st.petRetryTimestamp = timestamp;
            })) {
            MARAS_LOG_INFO("SetChildPetRetryTimestamp: child {:08X} petRetryTimestamp={:.2f}",
                           child->GetFormID(), timestamp);
        }
    }

    void RecordChildPetRetry(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::LineageUseCases::GetSingleton().RecordPetRejected(child->GetFormID());
    }

    float GetChildPetRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return 0.0f;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household ? snap->household->FindChildState(State::ChildId{ child->GetFormID() }) : nullptr;
        return st ? st->petRetryTimestamp : 0.0f;
    }

    bool IsPetRetryAvailable(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return true;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household ? snap->household->FindChildState(State::ChildId{ child->GetFormID() }) : nullptr;
        if (!st) {
            return true;
        }
        return Domain::Lineage::IsCompanionRetryCooldownElapsed(st->petRetryTimestamp, GetCurrentGameDays());
    }

    bool AssignChildCritter(RE::StaticFunctionTag*, RE::Actor* child, RE::Actor* critter) {
        if (!child) {
            return false;
        }

        const RE::FormID critterID = critter ? critter->GetFormID() : 0;
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().AssignCritter(child->GetFormID(), critterID);
    }

    RE::Actor* GetChildCritter(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(App::LineageUseCases::GetSingleton().GetCritter(child->GetFormID()));
    }

    RE::Actor* GetChildForCritter(RE::StaticFunctionTag*, RE::Actor* critter) {
        if (!critter) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(App::LineageUseCases::GetSingleton().GetChildForCritter(critter->GetFormID()));
    }

    RE::Actor* GetChildForPetOrCritter(RE::StaticFunctionTag*, RE::Actor* petOrCritter) {
        if (!petOrCritter) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetChildForPetOrCritter(petOrCritter->GetFormID()));
    }

    bool RemoveChildCritter(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().RemoveCritter(child->GetFormID());
    }

    void SetChildCritterBanned(RE::StaticFunctionTag*, RE::Actor* child, bool banned) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        if (MutateChildHouseholdState(child->GetFormID(), [banned](State::ChildHouseholdRecord& st) {
                st.critterBanned = banned;
            })) {
            MARAS_LOG_INFO("SetChildCritterBanned: child {:08X} critterBanned={}", child->GetFormID(), banned);
        }
    }

    bool IsChildCritterBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsChildCritterBanned(child->GetFormID());
    }

    bool IsCritterBanned(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsCritterBanned(child->GetFormID());
    }

    void SetGlobalCritterBanned(RE::StaticFunctionTag*, bool banned) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::SettingsCoordinator::GetSingleton().SetBoolByKey("bGlobalBanCritter", banned);
    }

    bool IsGlobalCritterBanned(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::SettingsCoordinator::GetSingleton().GetBoolByKey("bGlobalBanCritter");
    }

    void SetChildCritterRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child, float timestamp) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        if (MutateChildHouseholdState(child->GetFormID(), [timestamp](State::ChildHouseholdRecord& st) {
                st.critterRetryTimestamp = timestamp;
            })) {
            MARAS_LOG_INFO("SetChildCritterRetryTimestamp: child {:08X} critterRetryTimestamp={:.2f}",
                           child->GetFormID(), timestamp);
        }
    }

    void RecordChildCritterRetry(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::LineageUseCases::GetSingleton().RecordCritterRejected(child->GetFormID());
    }

    float GetChildCritterRetryTimestamp(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return 0.0f;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household ? snap->household->FindChildState(State::ChildId{ child->GetFormID() }) : nullptr;
        return st ? st->critterRetryTimestamp : 0.0f;
    }

    bool IsChildCritterRetryAvailable(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return true;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* st = snap->household ? snap->household->FindChildState(State::ChildId{ child->GetFormID() }) : nullptr;
        if (!st) {
            return true;
        }
        return Domain::Lineage::IsCompanionRetryCooldownElapsed(st->critterRetryTimestamp, GetCurrentGameDays());
    }

    void SetChildIsNewlyAdopted(RE::StaticFunctionTag*, RE::Actor* child, bool isNewlyAdopted, bool clearSameHouse) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::LineageUseCases::GetSingleton().SetIsNewlyAdopted(
            child->GetFormID(), isNewlyAdopted, clearSameHouse);
    }

    bool IsChildNewlyAdopted(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsNewlyAdopted(child->GetFormID());
    }

    bool IsChildPlayerAdopted(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID childID = child->GetFormID();
        if (App::LineageUseCases::GetSingleton().IsGuardian(childID, kPlayerFormID)) {
            return true;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household && snap->household->FindChildState(State::ChildId{ childID }) != nullptr;
    }

    void SetHouseInitialMoveDone(RE::StaticFunctionTag*, RE::BGSLocation* houseLoc, bool done) {
        if (!houseLoc) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::HouseholdUseCases::GetSingleton().SetHouseInitialMoveDone(houseLoc->GetFormID(), done);
    }

    bool IsHouseInitialMoveDone(RE::StaticFunctionTag*, RE::BGSLocation* houseLoc) {
        if (!houseLoc) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::HouseholdUseCases::GetSingleton().IsHouseInitialMoveDone(houseLoc->GetFormID());
    }

    void SetChildChestIntroduced(RE::StaticFunctionTag*, bool introduced) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        State::WorldStore::GetSingleton().Modify([introduced](State::WorldState& ws) {
            ws.household.Write().SetChildChestIntroduced(introduced);
        });
        MARAS_LOG_INFO("SetChildChestIntroduced: childChestIntroduced={}", introduced);
    }

    bool IsChildChestIntroduced(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->household && snap->household->IsChildChestIntroduced();
    }

    std::vector<RE::Actor*> GetAllPlayerChildren(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(App::LineageUseCases::GetSingleton().GetAllPlayerChildren(/*onlyRealActors=*/true));
    }

    std::vector<RE::Actor*> GetChildrenAssignedToHouse(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(App::LineageUseCases::GetSingleton().GetChildrenForHouse(
            playerHouse->GetFormID(), /*onlyRealActors=*/true));
    }

    RE::Actor* GetNewlyAdoptedChild(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetNewlyAdoptedChild(playerHouse->GetFormID()));
    }

    RE::Actor* GetChildWithoutPet(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetChildWithoutPet(playerHouse->GetFormID()));
    }

    RE::Actor* GetChildWithoutCritter(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetChildWithoutCritter(playerHouse->GetFormID()));
    }

    std::vector<RE::Actor*> GetChildrenWithoutScene(RE::StaticFunctionTag*,
                                                    RE::BGSLocation* playerHouse,
                                                    int32_t sceneID) {
        if (!playerHouse) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorsById(
            App::LineageUseCases::GetSingleton().GetChildrenWithoutScene(playerHouse->GetFormID(), sceneID));
    }

    RE::Actor* GetWeightedRandomChild(RE::StaticFunctionTag*, RE::BGSLocation* playerHouse) {
        if (!playerHouse) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetWeightedRandomChild(playerHouse->GetFormID()));
    }

    void SetChildPlayedScene(RE::StaticFunctionTag*, RE::Actor* child, int32_t sceneID, bool played) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::LineageUseCases::GetSingleton().SetChildPlayedScene(child->GetFormID(), sceneID, played);
    }

    bool HasChildPlayedScene(RE::StaticFunctionTag*, RE::Actor* child, int32_t sceneID) {
        if (!child) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().HasChildPlayedScene(child->GetFormID(), sceneID);
    }

    void RecordChildInteraction(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::LineageUseCases::GetSingleton().RecordChildInteraction(child->GetFormID());
    }

    float GetChildLastInteractionTimestamp(RE::StaticFunctionTag*, RE::Actor* child) {
        if (!child) {
            return 0.0f;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().GetLastInteractionTimestamp(child->GetFormID());
    }

    // ========================================
    // Fertility Mode Reloaded (FMR) & Paternity Secrets
    // ========================================

    void SetNPCPregnant(RE::StaticFunctionTag*,
                        RE::Actor* mother,
                        bool pregnant,
                        RE::Actor* bioFather,
                        RE::Actor* partner,
                        float conceptionTime) {
        if (!mother) {
            return;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::PaternityWorkflowCoordinator::GetSingleton().SetNPCPregnant(
            mother->GetFormID(),
            pregnant,
            bioFather ? bioFather->GetFormID() : 0,
            partner ? partner->GetFormID() : 0,
            conceptionTime);
    }

    bool IsNPCPregnant(RE::StaticFunctionTag*, RE::Actor* mother) {
        if (!mother) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().IsNPCPregnant(mother->GetFormID());
    }

    int32_t RegisterUnbornChild(RE::StaticFunctionTag*,
                                RE::Actor* mother,
                                RE::Actor* bioFather,
                                float conceptionTime,
                                bool evaluateStepIn) {
        if (!mother) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::PaternityWorkflowCoordinator::GetSingleton().RegisterUnbornChild(
            mother->GetFormID(),
            bioFather ? bioFather->GetFormID() : 0,
            conceptionTime,
            evaluateStepIn));
    }

    int32_t PromoteUnbornToInfant(RE::StaticFunctionTag*,
                                  RE::Actor* mother,
                                  float birthTime,
                                  std::string name,
                                  RE::Actor* bioFather) {
        if (!mother) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::PaternityWorkflowCoordinator::GetSingleton().PromoteUnbornToInfant(
            mother->GetFormID(),
            birthTime,
            name,
            bioFather ? bioFather->GetFormID() : 0));
    }

    bool RemoveUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother, RE::Actor* bioFather) {
        if (!mother) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().RemoveUnbornChild(
            mother->GetFormID(), bioFather ? bioFather->GetFormID() : 0);
    }

    int32_t FindUnbornChild(RE::StaticFunctionTag*, RE::Actor* mother) {
        if (!mother) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::PaternityWorkflowCoordinator::GetSingleton().FindUnbornChild(mother->GetFormID()));
    }

    std::vector<int32_t> GetUnbornChildren(RE::StaticFunctionTag*, RE::Actor* mother) {
        if (!mother) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto ids = App::PaternityWorkflowCoordinator::GetSingleton().GetUnbornChildren(mother->GetFormID());
        std::vector<int32_t> result;
        result.reserve(ids.size());
        for (const RE::FormID id : ids) {
            result.push_back(static_cast<int32_t>(id));
        }
        return result;
    }

    int32_t GetUnbornChildCount(RE::StaticFunctionTag*, RE::Actor* mother) {
        if (!mother) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::PaternityWorkflowCoordinator::GetSingleton().GetUnbornChildren(mother->GetFormID()).size());
    }

    int32_t RegisterFertilityChild(RE::StaticFunctionTag*,
                                   int32_t syntheticID,
                                   RE::Actor* mother,
                                   RE::Actor* partner,
                                   RE::Actor* bioFather,
                                   float conception,
                                   float birth,
                                   std::string name) {
        if (!mother) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::PaternityWorkflowCoordinator::GetSingleton().RegisterFertilityChild(
            static_cast<RE::FormID>(syntheticID),
            mother->GetFormID(),
            partner ? partner->GetFormID() : 0,
            bioFather ? bioFather->GetFormID() : 0,
            conception,
            birth,
            name));
    }

    bool UpdateChildName(RE::StaticFunctionTag*, int32_t childID, std::string newName) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().UpdateChildName(static_cast<RE::FormID>(childID), newName);
    }

    bool SetChildStage(RE::StaticFunctionTag*, int32_t childID, int32_t stage) {
        if (childID == 0 || stage < 0 || stage >= static_cast<int32_t>(Foundation::ChildStage::_Count)) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().SetChildStage(
            static_cast<RE::FormID>(childID), static_cast<Foundation::ChildStage>(stage));
    }

    int32_t GetChildStage(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return 0;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto entryOpt = App::LineageUseCases::GetSingleton().GetChildEntry(static_cast<RE::FormID>(childID));
        return entryOpt.has_value() ? static_cast<int32_t>(entryOpt->stage) : 0;
    }

    bool SetChildTrainingLocation(RE::StaticFunctionTag*, int32_t childID, std::string location) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().SetChildTrainingLocation(
            static_cast<RE::FormID>(childID), location);
    }

    std::string GetChildTrainingLocation(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return "";
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().GetChildTrainingLocation(static_cast<RE::FormID>(childID));
    }

    int32_t GetChildTrainingClass(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return -1;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().GetChildTrainingClass(static_cast<RE::FormID>(childID));
    }

    int32_t GetChildSavedRaceIndex(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return 255;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::LineageUseCases::GetSingleton().GetChildSavedRaceIndex(static_cast<RE::FormID>(childID)));
    }

    bool SetChildGender(RE::StaticFunctionTag*, int32_t childID, int32_t gender) {
        if (childID == 0 || gender < 0 || gender > 255) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().SetChildGender(
            static_cast<RE::FormID>(childID), static_cast<std::uint8_t>(gender));
    }

    int32_t GetChildGender(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return 2;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(
            App::LineageUseCases::GetSingleton().GetChildGender(static_cast<RE::FormID>(childID)));
    }

    bool SetChildGuardian(RE::StaticFunctionTag*, int32_t childID, int32_t slot, RE::Actor* guardian) {
        if (childID == 0) {
            return false;
        }
        if (slot < 0 || slot > 1) {
            MARAS_LOG_WARN("SetChildGuardian: invalid slot {} for child {:08X}", slot, static_cast<RE::FormID>(childID));
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        auto& lineage = App::LineageUseCases::GetSingleton();
        const RE::FormID cID = static_cast<RE::FormID>(childID);
        if (!lineage.GetChildEntry(cID).has_value()) {
            return false;
        }

        const RE::FormID guardianID = guardian ? guardian->GetFormID() : 0;
        const auto [slotA, slotB] = lineage.GetGuardians(cID);
        const RE::FormID currentAtSlot = (slot == 0) ? slotA : slotB;

        if (guardianID == 0) {
            return currentAtSlot == 0 ? true : lineage.RemoveChildGuardian(cID, currentAtSlot);
        }

        if (lineage.IsGuardian(cID, guardianID)) {
            return true;
        }

        if (currentAtSlot != 0 && !Utils::MatchesActor(currentAtSlot, guardianID)) {
            (void)lineage.RemoveChildGuardian(cID, currentAtSlot);
        }
        return lineage.AddChildGuardian(cID, guardianID);
    }

    int32_t FindSyntheticChild(RE::StaticFunctionTag*, RE::Actor* mother, std::string name) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const RE::FormID motherID = mother ? mother->GetFormID() : 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->lineage) {
            return 0;
        }

        const auto& lineage = App::LineageUseCases::GetSingleton();
        const auto& allChildren = snap->lineage->AllChildren();
        auto matchesMother = [&](RE::FormID cID, const State::ChildRecord& rec) -> bool {
            return rec.guardians.HasGuardian(State::ActorId{ motherID }) ||
                   Utils::MatchesActor(rec.bioMother.raw(), motherID) ||
                   Utils::MatchesActor(lineage.GetBioMother(cID), motherID);
        };

        // Pass 1: synthetic child with exact mother (if given) and name
        if (!name.empty()) {
            for (const auto& [cIdKey, rec] : allChildren) {
                const RE::FormID cID = cIdKey.raw();
                if (!IsSyntheticChildID(cID)) {
                    continue;
                }
                if (motherID != 0 && !matchesMother(cID, rec)) {
                    continue;
                }
                if (Foundation::StringUtils::CaseInsensitiveEquals(rec.name, name)) {
                    return static_cast<int32_t>(cID);
                }
            }
        }

        // Pass 2: motherID provided and name empty -> first synthetic child for mother
        if (motherID != 0 && name.empty()) {
            for (const auto& [cIdKey, rec] : allChildren) {
                const RE::FormID cID = cIdKey.raw();
                if (!IsSyntheticChildID(cID)) {
                    continue;
                }
                if (matchesMother(cID, rec)) {
                    return static_cast<int32_t>(cID);
                }
            }
        }

        return 0;
    }

    bool ResolvePaternitySecret(RE::StaticFunctionTag*, int32_t childID, bool discoveredByPartner) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().ResolvePaternitySecret(
            static_cast<RE::FormID>(childID), discoveredByPartner, true);
    }

    bool ResolvePaternitySecretForPartner(RE::StaticFunctionTag*, int32_t childID, bool discoveredByPartner) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().ResolvePaternitySecretForPartner(
            static_cast<RE::FormID>(childID), discoveredByPartner, true);
    }

    bool ResolvePaternitySecretForBioFather(RE::StaticFunctionTag*, int32_t childID, bool discoveredByBioFather) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().ResolvePaternitySecretForBioFather(
            static_cast<RE::FormID>(childID), discoveredByBioFather, true);
    }

    bool RevealPregnancyToPartner(RE::StaticFunctionTag*, int32_t childID, bool byVisibleBelly) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().RevealPregnancyToPartner(
            static_cast<RE::FormID>(childID), byVisibleBelly, true);
    }

    bool SetPaternitySecretEdges(RE::StaticFunctionTag*,
                                 int32_t childID,
                                 bool secretForBioFather,
                                 bool secretForPartner) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().SetPaternitySecretEdges(
            static_cast<RE::FormID>(childID), secretForBioFather, secretForPartner);
    }

    bool IsSecretForBioFather(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().IsSecretForBioFather(
            static_cast<RE::FormID>(childID));
    }

    bool IsSecretForPartner(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().IsSecretForPartner(
            static_cast<RE::FormID>(childID));
    }

    bool IsPregnancySecretForPartner(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::PaternityWorkflowCoordinator::GetSingleton().IsPregnancySecretForPartner(
            static_cast<RE::FormID>(childID));
    }

    int32_t FindChildPaternitySecret(RE::StaticFunctionTag*,
                                       RE::Actor* parent1,
                                       RE::Actor* parent2,
                                       std::string childName) {
        const RE::FormID p1 = parent1 ? parent1->GetFormID() : 0;
        const RE::FormID p2 = parent2 ? parent2->GetFormID() : 0;
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return static_cast<int32_t>(App::PaternityWorkflowCoordinator::GetSingleton().FindChildPaternitySecret(
            p1, p2, childName));
    }

    RE::Actor* GetPrimaryPartner(RE::StaticFunctionTag*, RE::Actor* akActor) {
        if (!akActor) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::RelationshipUseCases::GetSingleton().GetPrimaryPartner(akActor->GetFormID()));
    }

    RE::Actor* GetGuardianA(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetGuardianA(static_cast<RE::FormID>(childID)));
    }

    RE::Actor* GetGuardianB(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetGuardianB(static_cast<RE::FormID>(childID)));
    }

    std::vector<RE::Actor*> GetGuardians(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return {};
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const auto [gA, gB] = App::LineageUseCases::GetSingleton().GetGuardians(static_cast<RE::FormID>(childID));
        std::vector<RE::Actor*> result;
        if (auto* actorA = ResolveActorById(gA)) {
            result.push_back(actorA);
        }
        if (gB != 0 && gB != gA) {
            if (auto* actorB = ResolveActorById(gB)) {
                result.push_back(actorB);
            }
        }
        return result;
    }

    RE::Actor* GetCoGuardian(RE::StaticFunctionTag*, int32_t childID, RE::Actor* actor) {
        if (childID == 0 || !actor) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetCoGuardian(static_cast<RE::FormID>(childID), actor->GetFormID()));
    }

    bool IsGuardian(RE::StaticFunctionTag*, int32_t childID, RE::Actor* actor) {
        if (childID == 0 || !actor) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().IsGuardian(
            static_cast<RE::FormID>(childID), actor->GetFormID());
    }

    RE::Actor* GetBioMother(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetBioMother(static_cast<RE::FormID>(childID)));
    }

    RE::Actor* GetBioFather(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetBioFather(static_cast<RE::FormID>(childID)));
    }

    RE::Actor* GetMotherPartner(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return nullptr;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return ResolveActorById(
            App::LineageUseCases::GetSingleton().GetMotherPartner(static_cast<RE::FormID>(childID)));
    }

    bool SetBioMother(RE::StaticFunctionTag*, int32_t childID, RE::Actor* mother) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().SetBioMother(
            static_cast<RE::FormID>(childID), mother ? mother->GetFormID() : 0);
    }

    bool SetBioFather(RE::StaticFunctionTag*, int32_t childID, RE::Actor* father) {
        if (childID == 0) {
            return false;
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().SetBioFather(
            static_cast<RE::FormID>(childID), father ? father->GetFormID() : 0);
    }

    std::string GetChildName(RE::StaticFunctionTag*, int32_t childID) {
        if (childID == 0) {
            return "";
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::LineageUseCases::GetSingleton().GetChildName(static_cast<RE::FormID>(childID));
    }

    // ========================================
    // Random Name Generation (Child & Critter)
    // ========================================

    std::string GenerateRandomChildName(RE::StaticFunctionTag*, std::string raceName, bool isFemale, bool isUnique) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureChildNamesLoaded();
        return Engine::NamePoolLoader::GetSingleton().GetRandomChildName(raceName, isFemale, isUnique);
    }

    std::string GenerateRandomChildNameForActor(RE::StaticFunctionTag*, RE::Actor* akActor, bool isUnique) {
        if (!akActor) {
            return "";
        }

        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureChildNamesLoaded();
        const auto* base = akActor->GetActorBase();
        const bool isFemale = base && (base->GetSex() == RE::SEX::kFemale);
        return Engine::NamePoolLoader::GetSingleton().GetRandomChildName(akActor, isFemale, isUnique);
    }

    std::string GenerateRandomChildNameForRace(RE::StaticFunctionTag* tag,
                                               RE::TESForm* akRace,
                                               bool isFemale,
                                               bool isUnique) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureChildNamesLoaded();
        if (!akRace) {
            return Engine::NamePoolLoader::GetSingleton().GetRandomChildName("", isFemale, isUnique);
        }
        if (auto* actor = akRace->As<RE::Actor>()) {
            return GenerateRandomChildNameForActor(tag, actor, isUnique);
        }
        return Engine::NamePoolLoader::GetSingleton().GetRandomChildName(
            ExtractRaceLabelFromForm(akRace), isFemale, isUnique);
    }

    std::vector<std::string> GetChildNames(RE::StaticFunctionTag*, std::string raceName, bool isFemale) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureChildNamesLoaded();
        return Engine::NamePoolLoader::GetSingleton().GetChildNames(raceName, isFemale);
    }

    bool ReloadChildNames(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return Engine::NamePoolLoader::GetSingleton().LoadChildNamesFromFile();
    }

    std::string GenerateRandomCritterName(RE::StaticFunctionTag*, std::string raceName) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureCritterNamesLoaded();
        return Engine::NamePoolLoader::GetSingleton().GetRandomCritterName(raceName);
    }

    std::string GenerateRandomCritterNameForActor(RE::StaticFunctionTag*, RE::Actor* akActor) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureCritterNamesLoaded();
        return Engine::NamePoolLoader::GetSingleton().GetRandomCritterName(akActor);
    }

    std::string GenerateRandomCritterNameForRace(RE::StaticFunctionTag*, RE::TESForm* akRace) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureCritterNamesLoaded();
        return Engine::NamePoolLoader::GetSingleton().GetRandomCritterName(akRace);
    }

    std::vector<std::string> GetCritterNames(RE::StaticFunctionTag*, std::string raceName) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        EnsureCritterNamesLoaded();
        return Engine::NamePoolLoader::GetSingleton().GetCritterNames(raceName);
    }

    bool ReloadCritterNames(RE::StaticFunctionTag*) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return Engine::NamePoolLoader::GetSingleton().LoadCritterNamesFromFile();
    }

    void OnFertilityConception(RE::StaticFunctionTag*,
                               std::string provider,
                               RE::Actor* mother,
                               std::vector<RE::Actor*> bioFathers,
                               int32_t trackingOrChildCount,
                               float currentGameDay) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const float effectiveGameDay = currentGameDay <= 0.0f ? GetCurrentGameDays() : currentGameDay;
        App::FertilityLifecycleCoordinator::GetSingleton().OnConception(
            provider, mother, bioFathers, trackingOrChildCount, effectiveGameDay);
    }

    void OnFertilityProgressStatus(RE::StaticFunctionTag*,
                                   std::string provider,
                                   RE::Actor* mother,
                                   int32_t stageOrRank,
                                   float currentGameDay) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const float effectiveGameDay = currentGameDay <= 0.0f ? GetCurrentGameDays() : currentGameDay;
        App::FertilityLifecycleCoordinator::GetSingleton().OnProgressStatus(
            provider, mother, stageOrRank, effectiveGameDay);
    }

    void OnFertilityLaborStarted(RE::StaticFunctionTag*,
                                 RE::Actor* mother,
                                 int32_t childCount,
                                 RE::Actor* fallbackFather,
                                 float currentGameDay) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const float effectiveGameDay = currentGameDay <= 0.0f ? GetCurrentGameDays() : currentGameDay;
        App::FertilityLifecycleCoordinator::GetSingleton().OnLaborStarted(
            mother, childCount, fallbackFather, effectiveGameDay);
    }

    void OnFertilityBirth(RE::StaticFunctionTag*,
                          std::string provider,
                          RE::Actor* mother,
                          RE::Actor* bioFather,
                          std::string childName,
                          int32_t babySex,
                          float currentGameDay) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const float effectiveGameDay = currentGameDay <= 0.0f ? GetCurrentGameDays() : currentGameDay;
        App::FertilityLifecycleCoordinator::GetSingleton().OnBirth(
            provider, mother, bioFather, childName, babySex, effectiveGameDay);
    }

    void OnFertilityPregnancyLoss(RE::StaticFunctionTag*,
                                  std::string provider,
                                  RE::Actor* mother,
                                  RE::Actor* bioFather,
                                  std::string reason,
                                  bool requireRemovedRecord) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::FertilityLifecycleCoordinator::GetSingleton().OnPregnancyLoss(
            provider, mother, bioFather, reason, requireRemovedRecord);
    }

    void OnFertilityChildGrewUp(RE::StaticFunctionTag*,
                                RE::Actor* mother,
                                std::string childName,
                                int32_t gender,
                                bool isNpcNpc,
                                RE::Actor* fatherHint) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::FertilityLifecycleCoordinator::GetSingleton().OnChildGrewUp(
            mother, childName, gender, isNpcNpc, fatherHint);
    }

    void OnFertilityChildGrewUpActor(RE::StaticFunctionTag*,
                                     RE::Actor* childActor,
                                     std::string childName) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::FertilityLifecycleCoordinator::GetSingleton().OnChildGrewUpActor(childActor, childName);
    }

    void OnFertilityChildSentToTraining(RE::StaticFunctionTag*,
                                        std::string childName,
                                        std::string trainingLocation,
                                        std::string className) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::FertilityLifecycleCoordinator::GetSingleton().OnChildSentToTraining(
            childName, trainingLocation, className);
    }

    void OnFertilityChildFinishedTraining(RE::StaticFunctionTag*,
                                            std::string childName,
                                            std::string trainingLocation,
                                            std::string className) {
        (void)trainingLocation;
        (void)className;
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::FertilityLifecycleCoordinator::GetSingleton().OnChildFinishedTraining(childName);
    }

    void OnFertilityAdultChildSpawned(RE::StaticFunctionTag*,
                                      std::string provider,
                                      RE::Actor* adultActor,
                                      RE::Actor* oldChildActor,
                                      RE::Actor* motherHint,
                                      RE::Actor* fatherHint,
                                      std::string childName,
                                      bool allowFallbackToAdultId,
                                      bool fireEvent) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        App::FertilityLifecycleCoordinator::GetSingleton().OnAdultChildSpawned(
            provider, adultActor, oldChildActor, motherHint, fatherHint, childName,
            allowFallbackToAdultId, fireEvent);
    }

    int32_t FindFertilityInfantForSpawn(RE::StaticFunctionTag*,
                                        RE::Actor* mother,
                                        std::string childName) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::FertilityLifecycleCoordinator::GetSingleton().FindInfantForSpawn(mother, childName);
    }

    int32_t ResolveFertilitySpawnSyntheticId(RE::StaticFunctionTag*,
                                             RE::Actor* childActor,
                                             RE::Actor* bioMother,
                                             RE::Actor* bioFather,
                                             int32_t existingSyntheticId,
                                             float dob,
                                             std::string childName) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        const float effectiveDob = dob <= 0.0f ? GetCurrentGameDays() : dob;
        return App::FertilityLifecycleCoordinator::GetSingleton().ResolveFertilitySpawnSyntheticId(
            childActor, bioMother, bioFather, existingSyntheticId, effectiveDob, childName);
    }

    bool RegisterNpcFertilityChildSpawn(RE::StaticFunctionTag*,
                                        RE::Actor* childActor,
                                        int32_t childId,
                                        RE::Actor* bioMother,
                                        RE::Actor* bioFather,
                                        int32_t stage,
                                        bool isMigrating) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::FertilityLifecycleCoordinator::GetSingleton().RegisterNpcFertilityChildSpawn(
            childActor, childId, bioMother, bioFather, stage, isMigrating);
    }

    RE::Actor* GetFertilitySpawnCoSpouse(RE::StaticFunctionTag*,
                                         int32_t childId,
                                         RE::Actor* bioMother,
                                         RE::Actor* bioFather) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::FertilityLifecycleCoordinator::GetSingleton().GetFertilitySpawnCoSpouse(
            childId, bioMother, bioFather);
    }

    int32_t MigrateCompletedFmrNpcChild(RE::StaticFunctionTag*,
                                        RE::Actor* mother,
                                        RE::Actor* father,
                                        float conceptionTime,
                                        float birthTime) {
        std::lock_guard<MARAS::PapyrusDomainMutex> lock(GetPapyrusMutex());
        return App::FertilityLifecycleCoordinator::GetSingleton().MigrateCompletedFmrNpcChild(
            mother, father, conceptionTime, birthTime);
    }

    float GetActorRefScale(RE::StaticFunctionTag*, RE::Actor* actor) {
        if (!actor || actor->IsDeleted()) {
            return 1.0f;
        }
        return Domain::Lineage::RefScaleRawToFloat(actor->GetReferenceRuntimeData().refScale);
    }

}  // namespace MARAS::PapyrusInterface
