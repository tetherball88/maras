#include "maras/app/ChildInteractionCoordinator.h"

#include "maras/app/LineageUseCases.h"
#include "maras/domain/household/ChildForcegreetRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/NamePoolLoader.h"
#include "maras/engine/PlayerChildrenEngineOps.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace MARAS::App {

    namespace {

        [[nodiscard]] std::string FormatHexChildId(Foundation::ChildId id) {
            return id.IsNull() ? std::string{} : std::format("0x{:08X}", id.raw());
        }

        [[nodiscard]] float GetCurrentGameDays() noexcept {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetDaysPassed() : 0.0f;
        }

        [[nodiscard]] std::vector<RE::TESForm*> CollectFormsFromList(RE::BGSListForm* list) {
            std::vector<RE::TESForm*> out;
            if (!list) {
                return out;
            }
            list->ForEachForm([&out](RE::TESForm* form) {
                if (form) {
                    out.push_back(form);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return out;
        }

        [[nodiscard]] std::vector<Foundation::FormRawId> CollectRaceIdsFromList(RE::BGSListForm* list) {
            std::vector<Foundation::FormRawId> out;
            for (RE::TESForm* form : CollectFormsFromList(list)) {
                if (auto* race = form->As<RE::TESRace>()) {
                    out.emplace_back(race->GetFormID());
                }
            }
            return out;
        }

        [[nodiscard]] std::int32_t GetActorGoldCount(RE::Actor* actor) {
            auto* goldObj = FormCache::GetSingleton().GetGoldBaseObject();
            if (!actor || !goldObj) {
                return 0;
            }
            const auto counts = actor->GetInventoryCounts(
                [goldObj](RE::TESBoundObject& obj) { return &obj == goldObj; });
            const auto it = counts.find(goldObj);
            return (it != counts.end()) ? it->second : 0;
        }

        void DeductActorGold(RE::Actor* actor, std::int32_t amount) {
            if (!actor || amount <= 0) {
                return;
            }
            if (auto* goldObj = FormCache::GetSingleton().GetGoldBaseObject()) {
                actor->RemoveItem(goldObj, amount, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            }
        }

        void AddRandomFormFromListToContainer(
            RE::TESObjectREFR* targetContainer,
            RE::BGSListForm* list,
            std::int32_t count,
            Foundation::IRng& rng) {
            const auto forms = CollectFormsFromList(list);
            if (!targetContainer || forms.empty() || count <= 0) {
                return;
            }
            RE::TESForm* form =
                forms[static_cast<std::size_t>(rng.NextInt(0, static_cast<std::int32_t>(forms.size() - 1)))];
            if (!form) {
                return;
            }
            if (auto* levItem = form->As<RE::TESLevItem>()) {
                RE::BSScrapArray<RE::CALCED_OBJECT> calced;
                levItem->CalculateCurrentFormList(1, static_cast<std::int16_t>(count), calced, 0, true);
                for (const auto& entry : calced) {
                    auto* bound = entry.form ? entry.form->As<RE::TESBoundObject>() : nullptr;
                    if (bound && entry.count > 0) {
                        targetContainer->AddObjectToContainer(bound, nullptr, entry.count, nullptr);
                    }
                }
            } else if (auto* bound = form->As<RE::TESBoundObject>()) {
                targetContainer->AddObjectToContainer(bound, nullptr, count, nullptr);
            }
        }

        [[nodiscard]] Foundation::RefId PopTransientCritterId(RE::FormID childRawId) {
            Foundation::RefId critterRefId{};
            if (childRawId == 0) {
                return critterRefId;
            }
            State::WorldStore::GetSingleton().Modify([childRawId, &critterRefId](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                if (auto* rec = hh.FindChildStateMutable(Foundation::ChildId{ childRawId })) {
                    if (!rec->transientCritterId.IsNull()) {
                        critterRefId = rec->transientCritterId;
                        rec->transientCritterId = Foundation::RefId{};
                    }
                }
            });
            return critterRefId;
        }

        [[nodiscard]] std::int32_t ApplyAndPersistPoorCounterDelta(std::int32_t delta) {
            std::int32_t next = 0;
            State::WorldStore::GetSingleton().Modify([delta, &next](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                next = Domain::Household::ApplyPoorCounterDelta(hh.GetPoorCounter(), delta);
                hh.SetPoorCounter(next);
            });
            return next;
        }

    }  // namespace

    ChildInteractionCoordinator& ChildInteractionCoordinator::GetSingleton() {
        static ChildInteractionCoordinator instance;
        return instance;
    }

    std::string ChildInteractionCoordinator::SelectChildForcegreetEvent(
        RE::BGSLocation* currentLoc,
        bool hasAnimalCompanion,
        std::int32_t critterChance,
        std::int32_t nameCallingChance) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const Foundation::HouseId houseId{ currentLoc ? currentLoc->GetFormID() : 0 };
        Domain::Household::ForcegreetSelectionPlan plan{};

        if (snap && snap->household) {
            const Domain::Household::ForcegreetSelectionInput input{
                houseId,
                hasAnimalCompanion,
                GetCurrentGameDays(),
                critterChance,
                nameCallingChance
            };
            auto rng = LineageUseCases::MakeEngineRng();
            plan = Domain::Household::PlanReadyForcegreetEvent(*snap, input, rng);
        }

        if (plan.markChestIntroduced || plan.clearNameCallingUsedLast) {
            State::WorldStore::GetSingleton().Modify([&plan](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                if (plan.markChestIntroduced) {
                    hh.SetChildChestIntroduced(true);
                }
                if (plan.clearNameCallingUsedLast) {
                    hh.SetNameCallingUsedLast(false);
                }
            });
        }

        if (plan.markChestIntroduced && !houseId.IsNull()) {
            MARAS::PlayerChildrenEngineOps::GetSingleton().SendChildChestEvent(houseId.raw(), true);
        }
        if (plan.clearNewlyAdoptedForHouse && !plan.eventChild.IsNull()) {
            LineageUseCases::GetSingleton().SetIsNewlyAdopted(plan.eventChild.raw(), false, true);
        }

        nlohmann::json result;
        result["eventNumber"] = plan.eventNumber;
        result["eventChild"] = FormatHexChildId(plan.eventChild);
        result["sceneChild1"] = FormatHexChildId(plan.sceneChild1);
        result["sceneChild2"] = FormatHexChildId(plan.sceneChild2);
        return result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void ChildInteractionCoordinator::CommitNameCallingForcegreet(std::int32_t eventNumber) {
        if (eventNumber != 6 && eventNumber != 7) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.household.Write().SetNameCallingUsedLast(true);
        });
    }

    std::string ChildInteractionCoordinator::SelectGenericChildForcegreetFallback(RE::BGSLocation* currentLoc) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const Foundation::HouseId houseId{ currentLoc ? currentLoc->GetFormID() : 0 };
        Domain::Household::ForcegreetSelectionPlan plan{};

        if (snap && snap->household) {
            auto rng = LineageUseCases::MakeEngineRng();
            plan = Domain::Household::PlanGenericForcegreetFallback(
                *snap, houseId, GetCurrentGameDays(), rng);
        }

        if (plan.clearNameCallingUsedLast) {
            State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
                ws.household.Write().SetNameCallingUsedLast(false);
            });
        }

        nlohmann::json result;
        result["eventNumber"] = plan.eventNumber;
        result["eventChild"] = FormatHexChildId(plan.eventChild);
        return result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::int32_t ChildInteractionCoordinator::EvaluateChildPetSuitability(
        RE::Actor* pet,
        RE::BGSListForm* dogRacesList,
        RE::BGSListForm* allowedRacesList,
        RE::TESFaction* potentialPetFaction) {
        if (!pet || !pet->GetRace()) {
            return 1;
        }

        const auto dogRaceIds = CollectRaceIdsFromList(dogRacesList);
        const auto allowedRaceIds = CollectRaceIdsFromList(allowedRacesList);
        const auto decision = Domain::Household::EvaluatePetRaceSuitability(
            Foundation::FormRawId{ pet->GetRace()->GetFormID() },
            dogRaceIds,
            allowedRaceIds);

        if (decision.isDog) {
            if (auto* avOwner = pet->AsActorValueOwner()) {
                avOwner->SetActorValue(RE::ActorValue::kVariable06, 1.0f);
            }
        }
        if (potentialPetFaction) {
            Utils::AddToFaction(pet, potentialPetFaction, static_cast<std::int8_t>(decision.suitabilityRank));
        }

        return decision.suitabilityRank;
    }

    RE::Actor* ChildInteractionCoordinator::SpawnChildTransientCritter(
        RE::Actor* child,
        RE::BGSListForm* maleCritters,
        RE::BGSListForm* femaleCritters) {
        if (!child) {
            return nullptr;
        }

        DismissChildTransientCritter(child);

        RE::BGSListForm* list =
            (child->GetActorBase() && child->GetActorBase()->GetSex() == RE::SEX::kFemale)
                ? femaleCritters
                : maleCritters;

        std::vector<RE::TESNPC*> critterBases;
        for (RE::TESForm* form : CollectFormsFromList(list)) {
            if (auto* npcBase = form->As<RE::TESNPC>()) {
                critterBases.push_back(npcBase);
            }
        }
        if (critterBases.empty()) {
            MARAS_LOG_WARN("[ChildInteractionCoordinator] No critter bases found for child {:08X}",
                           child->GetFormID());
            return nullptr;
        }

        auto rng = LineageUseCases::MakeEngineRng();
        RE::TESNPC* critterBase = critterBases[static_cast<std::size_t>(
            rng.NextInt(0, static_cast<std::int32_t>(critterBases.size() - 1)))];

        auto placed = child->PlaceObjectAtMe(critterBase, true);
        auto* newCritter = placed ? placed->As<RE::Actor>() : nullptr;
        if (!newCritter) {
            MARAS_LOG_WARN("[ChildInteractionCoordinator] PlaceObjectAtMe failed for critter base {:08X} near child {:08X}",
                           critterBase ? critterBase->GetFormID() : 0,
                           child->GetFormID());
            return nullptr;
        }

        auto& loader = Engine::NamePoolLoader::GetSingleton();
        if (!loader.IsCritterLoaded()) {
            (void)loader.LoadCritterNamesFromFile();
        }

        const std::string critterName = LineageUseCases::GetSingleton().PickRandomCritterName(newCritter);
        const std::string childName = ActorSvc::GetName(child->GetFormID());
        if (!childName.empty() && !critterName.empty()) {
            newCritter->SetDisplayName(std::format("{}'s {}", childName, critterName).c_str(), true);
        }

        State::WorldStore::GetSingleton().Modify(
            [childId = child->GetFormID(), critterId = newCritter->GetFormID()](State::WorldState& ws) {
                auto& hh = ws.household.Write();
                const Foundation::ChildId cid{ childId };
                if (auto* existing = hh.FindChildStateMutable(cid)) {
                    existing->transientCritterId = Foundation::RefId{ critterId };
                } else {
                    State::ChildHouseholdRecord rec{};
                    rec.childId = cid;
                    rec.transientCritterId = Foundation::RefId{ critterId };
                    hh.UpsertChildState(std::move(rec));
                }
            });

        return newCritter;
    }

    bool ChildInteractionCoordinator::AdoptChildTransientCritter(RE::Actor* child) {
        if (!child) {
            return false;
        }
        const Foundation::RefId critterRefId = PopTransientCritterId(child->GetFormID());
        if (critterRefId.IsNull()) {
            return false;
        }
        auto* critterAk = RE::TESForm::LookupByID<RE::Actor>(critterRefId.raw());
        if (!critterAk || critterAk->IsDeleted()) {
            return false;
        }
        if (!LineageUseCases::GetSingleton().AssignCritter(child->GetFormID(), critterAk->GetFormID())) {
            return false;
        }
        critterAk->EvaluatePackage();
        LineageUseCases::GetSingleton().RecordChildInteraction(child->GetFormID());
        return true;
    }

    void ChildInteractionCoordinator::DismissChildTransientCritter(RE::Actor* child) {
        if (!child) {
            return;
        }
        const Foundation::RefId critterRefId = PopTransientCritterId(child->GetFormID());
        if (critterRefId.IsNull()) {
            return;
        }
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(critterRefId.raw());
        if (ref && !ref->IsDeleted()) {
            ref->Disable();
            ref->SetDelete(true);
        }
    }

    std::int32_t ChildInteractionCoordinator::GetChildPoorCounter() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return (snap && snap->household) ? snap->household->GetPoorCounter() : 0;
    }

    std::int32_t ChildInteractionCoordinator::ProcessChildAllowance(
        RE::Actor* child,
        std::int32_t allowanceAmount) {
        const std::int32_t delta = Domain::Household::CalculateAllowancePoorDelta(allowanceAmount);
        const std::int32_t next = ApplyAndPersistPoorCounterDelta(delta);

        if (allowanceAmount > 0 && child) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* goldObj = FormCache::GetSingleton().GetGoldBaseObject();
            if (player && goldObj) {
                player->RemoveItem(goldObj, allowanceAmount, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                child->AddObjectToContainer(goldObj, nullptr, allowanceAmount, nullptr);
            }
            LineageUseCases::GetSingleton().RecordChildInteraction(child->GetFormID());
        }

        return next;
    }

    std::int32_t ChildInteractionCoordinator::ProcessChildPoorGift(
        RE::Actor* child,
        RE::BGSListForm* poorGiftsList) {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            auto rng = LineageUseCases::MakeEngineRng();
            AddRandomFormFromListToContainer(player, poorGiftsList, 1, rng);
        }
        const std::int32_t next = ApplyAndPersistPoorCounterDelta(2);
        if (child) {
            LineageUseCases::GetSingleton().RecordChildInteraction(child->GetFormID());
        }
        return next;
    }

    void ChildInteractionCoordinator::ProcessChildRegularGift(
        RE::Actor* child,
        const std::array<RE::BGSListForm*, 7>& tieredGiftLists) {
        if (!child) {
            return;
        }
        const std::int32_t childGold = GetActorGoldCount(child);
        auto rng = LineageUseCases::MakeEngineRng();
        const auto plan = Domain::Household::PlanChildRegularGift(childGold, rng);

        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (plan.tierIndex >= 0 && plan.tierIndex < 7) {
                AddRandomFormFromListToContainer(
                    player, tieredGiftLists[static_cast<std::size_t>(plan.tierIndex)], plan.count, rng);
            }
        }
        DeductActorGold(child, plan.goldDeduction);
        LineageUseCases::GetSingleton().RecordChildInteraction(child->GetFormID());
    }

    void ChildInteractionCoordinator::RefillChildChestFromLists(
        RE::TESObjectREFR* childChest,
        RE::BGSLocation* houseLoc,
        RE::BGSListForm* junkList,
        const std::array<RE::BGSListForm*, 7>& tieredGiftLists) {
        if (!childChest) {
            return;
        }

        RE::FormID houseId = 0;
        if (houseLoc) {
            houseId = houseLoc->GetFormID();
        } else if (auto* chestLoc = childChest->GetCurrentLocation()) {
            houseId = chestLoc->GetFormID();
        }

        auto houseChildIds =
            LineageUseCases::GetSingleton().GetChildrenForHouse(houseId, /*onlyRealActors=*/true);
        std::sort(houseChildIds.begin(), houseChildIds.end());

        std::vector<std::pair<Foundation::ChildId, std::int32_t>> childGolds;
        childGolds.reserve(houseChildIds.size());
        for (const RE::FormID rawChildId : houseChildIds) {
            auto* childActor = RE::TESForm::LookupByID<RE::Actor>(rawChildId);
            childGolds.emplace_back(Foundation::ChildId{ rawChildId }, GetActorGoldCount(childActor));
        }

        auto rng = LineageUseCases::MakeEngineRng();
        const auto plan = Domain::Household::PlanChildChestRefill(childGolds, rng);

        for (const std::int32_t cnt : plan.junkItemCounts) {
            AddRandomFormFromListToContainer(childChest, junkList, cnt, rng);
        }
        for (std::int32_t i = 0; i < plan.tier0025RollCount; ++i) {
            AddRandomFormFromListToContainer(childChest, tieredGiftLists[1], 1, rng);
        }
        if (plan.hasBonusGift && plan.bonusGiftTierIndex >= 0 && plan.bonusGiftTierIndex < 7) {
            AddRandomFormFromListToContainer(
                childChest, tieredGiftLists[static_cast<std::size_t>(plan.bonusGiftTierIndex)], 1, rng);
            if (plan.bonusGoldDeduction > 0 && !plan.richestChild.IsNull()) {
                auto* richestActor = RE::TESForm::LookupByID<RE::Actor>(plan.richestChild.raw());
                DeductActorGold(richestActor, plan.bonusGoldDeduction);
            }
        }
    }

}  // namespace MARAS::App
