#include "maras/domain/household/ChildForcegreetRules.h"

#include "maras/domain/lineage/LineageRules.h"
#include "maras/state/WorldTables.h"

#include <algorithm>

namespace MARAS::Domain::Household {

    namespace {

        [[nodiscard]] std::vector<ChildId> CollectPhysicalHouseChildren(
            const State::WorldStateSnapshot& snap,
            HouseId houseId) {
            std::vector<ChildId> out;
            if (!snap.household) {
                return out;
            }
            for (const auto& [cid, st] : snap.household->AllChildStates()) {
                if (cid.IsNull() || cid.IsSynthetic()) {
                    continue;
                }
                if (snap.lineage) {
                    if (const auto* rec = snap.lineage->FindChild(cid);
                        rec != nullptr && rec->stage != Foundation::ChildStage::Child) {
                        continue;
                    }
                }
                const HouseId effectiveHouse = !st.currentHouseId.IsNull()
                                                   ? st.currentHouseId
                                                   : snap.household->GetTenantHouse(cid.raw());
                if (houseId.IsNull() || effectiveHouse == houseId) {
                    out.push_back(cid);
                }
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        void ShuffleChildren(std::vector<ChildId>& candidates, Foundation::IRng& rng) noexcept {
            for (std::size_t i = candidates.size(); i > 1; --i) {
                const auto j = static_cast<std::size_t>(
                    rng.NextInt(0, static_cast<std::int32_t>(i - 1)));
                std::swap(candidates[i - 1], candidates[j]);
            }
        }

    }  // namespace

    ForcegreetSelectionPlan PlanReadyForcegreetEvent(
        const State::WorldStateSnapshot& snap,
        const ForcegreetSelectionInput& input,
        Foundation::IRng& rng) {
        ForcegreetSelectionPlan plan{};
        if (!snap.household) {
            return plan;
        }

        const auto houseChildren = CollectPhysicalHouseChildren(snap, input.houseId);
        if (houseChildren.empty()) {
            return plan;
        }

        const bool nameCallingWasLast = snap.household->IsNameCallingUsedLast();
        const bool chestIntroduced = snap.household->IsChildChestIntroduced();

        // EVENTS 1 & 2: Newly adopted child welcomes player home (with/without chest intro)
        {
            std::vector<ChildId> newlyAdopted;
            for (const ChildId cid : houseChildren) {
                const auto* st = snap.household->FindChildState(cid);
                if (st && st->isNewlyAdopted) {
                    newlyAdopted.push_back(cid);
                }
            }
            if (!newlyAdopted.empty()) {
                const ChildId chosen =
                    Lineage::PickWeightedChild(snap, newlyAdopted, input.currentGameDays, rng);
                if (!chosen.IsNull()) {
                    plan.eventChild = chosen;
                    plan.clearNewlyAdoptedForHouse = true;
                    plan.clearNameCallingUsedLast = nameCallingWasLast;
                    if (!chestIntroduced) {
                        plan.eventNumber = 2;
                        plan.markChestIntroduced = true;
                    } else {
                        plan.eventNumber = 1;
                    }
                    return plan;
                }
            }
        }

        // EVENT 3: Child chest tutorial if not yet introduced
        if (!chestIntroduced) {
            const ChildId chosen =
                Lineage::PickWeightedChild(snap, houseChildren, input.currentGameDays, rng);
            if (!chosen.IsNull()) {
                plan.eventNumber = 3;
                plan.eventChild = chosen;
                plan.markChestIntroduced = true;
                plan.clearNameCallingUsedLast = nameCallingWasLast;
                return plan;
            }
        }

        // EVENT 4: Adopt animal companion pet
        const bool globalPetBanned = snap.settings && snap.settings->bGlobalBanPet;
        if (input.hasAnimalCompanion && !globalPetBanned) {
            std::vector<ChildId> petCandidates;
            for (const ChildId cid : houseChildren) {
                const auto* st = snap.household->FindChildState(cid);
                if (st && st->petId.IsNull() &&
                    !Lineage::IsPetBannedForChild(snap, cid, input.currentGameDays)) {
                    petCandidates.push_back(cid);
                }
            }
            if (!petCandidates.empty()) {
                const ChildId chosen =
                    Lineage::PickWeightedChild(snap, petCandidates, input.currentGameDays, rng);
                if (!chosen.IsNull()) {
                    plan.eventNumber = 4;
                    plan.eventChild = chosen;
                    plan.clearNameCallingUsedLast = nameCallingWasLast;
                    return plan;
                }
            }
        }

        // EVENT 5: Adopt wild critter
        const bool globalCritterBanned = snap.settings && snap.settings->bGlobalBanCritter;
        if (!globalCritterBanned) {
            std::vector<ChildId> critterCandidates;
            for (const ChildId cid : houseChildren) {
                const auto* st = snap.household->FindChildState(cid);
                if (st && st->critterId.IsNull() &&
                    !Lineage::IsCritterBannedForChild(snap, cid, input.currentGameDays)) {
                    critterCandidates.push_back(cid);
                }
            }
            if (!critterCandidates.empty() && rng.NextInt(1, 100) < input.critterChance) {
                const ChildId chosen =
                    Lineage::PickWeightedChild(snap, critterCandidates, input.currentGameDays, rng);
                if (!chosen.IsNull()) {
                    plan.eventNumber = 5;
                    plan.eventChild = chosen;
                    plan.clearNameCallingUsedLast = nameCallingWasLast;
                    return plan;
                }
            }
        }

        // EVENTS 6 & 7: Two-child name-calling scenes (caller commits nameCallingUsedLast on quest start)
        if (!nameCallingWasLast) {
            std::vector<ChildId> scene1Children;
            for (const ChildId cid : houseChildren) {
                const auto* st = snap.household->FindChildState(cid);
                if (st && !st->playedScene1) {
                    scene1Children.push_back(cid);
                }
            }
            if (scene1Children.size() >= 2 && rng.NextInt(1, 100) < input.nameCallingChance) {
                if (scene1Children.size() > 2) {
                    ShuffleChildren(scene1Children, rng);
                }
                plan.eventNumber = 6;
                plan.sceneChild1 = scene1Children[0];
                plan.sceneChild2 = scene1Children[1];
                return plan;
            }

            std::vector<ChildId> scene2Children;
            for (const ChildId cid : houseChildren) {
                const auto* st = snap.household->FindChildState(cid);
                if (st && !st->playedScene2) {
                    scene2Children.push_back(cid);
                }
            }
            if (scene2Children.size() >= 2 && rng.NextInt(1, 100) < input.nameCallingChance) {
                if (scene2Children.size() > 2) {
                    ShuffleChildren(scene2Children, rng);
                }
                plan.eventNumber = 7;
                plan.sceneChild1 = scene2Children[0];
                plan.sceneChild2 = scene2Children[1];
                return plan;
            }
        }

        // EVENTS 10-13: Generic weighted fallback
        plan.eventNumber = rng.NextInt(10, 13);
        plan.eventChild = Lineage::PickWeightedChild(snap, houseChildren, input.currentGameDays, rng);
        plan.clearNameCallingUsedLast = nameCallingWasLast;
        return plan;
    }

    ForcegreetSelectionPlan PlanGenericForcegreetFallback(
        const State::WorldStateSnapshot& snap,
        HouseId houseId,
        float currentGameDays,
        Foundation::IRng& rng) {
        ForcegreetSelectionPlan plan{};
        if (!snap.household) {
            return plan;
        }
        const auto houseChildren = CollectPhysicalHouseChildren(snap, houseId);
        if (houseChildren.empty()) {
            return plan;
        }
        plan.eventNumber = rng.NextInt(10, 13);
        plan.eventChild = Lineage::PickWeightedChild(snap, houseChildren, currentGameDays, rng);
        plan.clearNameCallingUsedLast = snap.household->IsNameCallingUsedLast();
        return plan;
    }

    PetSuitabilityDecision EvaluatePetRaceSuitability(
        FormRawId petRaceId,
        std::span<const FormRawId> dogRaceIds,
        std::span<const FormRawId> allowedRaceIds) noexcept {
        PetSuitabilityDecision decision{};
        if (petRaceId.IsNull()) {
            return decision;
        }
        decision.isDog = std::find(dogRaceIds.begin(), dogRaceIds.end(), petRaceId) != dogRaceIds.end();
        const bool isAllowed =
            std::find(allowedRaceIds.begin(), allowedRaceIds.end(), petRaceId) != allowedRaceIds.end();
        decision.suitabilityRank = isAllowed ? 3 : 1;
        return decision;
    }

    std::int32_t CalculateAllowancePoorDelta(std::int32_t allowanceAmount) noexcept {
        if (allowanceAmount <= 0) {
            return 1;
        }
        if (allowanceAmount <= 25) {
            return -1;
        }
        if (allowanceAmount <= 100) {
            return -2;
        }
        if (allowanceAmount <= 500) {
            return -3;
        }
        return -5;
    }

    std::int32_t ApplyPoorCounterDelta(std::int32_t currentCounter, std::int32_t delta) noexcept {
        return std::max(0, currentCounter + delta);
    }

    ChildGiftTier SelectChildGiftTier(std::int32_t childGold) noexcept {
        if (childGold < 25) {
            return ChildGiftTier{ 0, 1 };
        }
        if (childGold < 50) {
            return ChildGiftTier{ 1, 25 };
        }
        if (childGold < 100) {
            return ChildGiftTier{ 2, 50 };
        }
        if (childGold < 250) {
            return ChildGiftTier{ 3, 100 };
        }
        if (childGold < 500) {
            return ChildGiftTier{ 4, 250 };
        }
        if (childGold < 1000) {
            return ChildGiftTier{ 5, 500 };
        }
        return ChildGiftTier{ 6, 1000 };
    }

    RegularGiftPlan PlanChildRegularGift(
        std::int32_t childGold,
        Foundation::IRng& rng) noexcept {
        const auto tier = SelectChildGiftTier(childGold);
        const std::int32_t count = (rng.NextInt(0, 100) > 90) ? 2 : 1;
        const std::int32_t half = tier.baseDeduction / 2;
        const std::int32_t ded = half + rng.NextInt(0, half);
        const std::int32_t clamped = std::clamp(ded * count, 0, std::max(0, childGold));
        return RegularGiftPlan{ tier.tierIndex, count, clamped };
    }

    ChestRefillPlan PlanChildChestRefill(
        std::span<const std::pair<ChildId, std::int32_t>> childGolds,
        Foundation::IRng& rng) {
        ChestRefillPlan plan{};

        const std::int32_t junkRolls = rng.NextInt(3, 5);
        plan.junkItemCounts.reserve(static_cast<std::size_t>(junkRolls));
        for (std::int32_t i = 0; i < junkRolls; ++i) {
            plan.junkItemCounts.push_back(rng.NextInt(1, 3));
        }

        plan.tier0025RollCount = rng.NextInt(1, 3);

        ChildId maxChild{};
        std::int32_t maxGold = 0;
        for (const auto& [cid, gold] : childGolds) {
            if (!cid.IsNull() && gold > maxGold) {
                maxGold = gold;
                maxChild = cid;
            }
        }

        if (!maxChild.IsNull() && maxGold > 25) {
            const auto tier = SelectChildGiftTier(maxGold);
            const std::int32_t half = tier.baseDeduction / 2;
            const std::int32_t ded = half + rng.NextInt(0, half);
            plan.hasBonusGift = true;
            plan.richestChild = maxChild;
            plan.bonusGiftTierIndex = tier.tierIndex;
            plan.bonusGoldDeduction = std::clamp(ded, 0, maxGold);
        }

        return plan;
    }

}  // namespace MARAS::Domain::Household
