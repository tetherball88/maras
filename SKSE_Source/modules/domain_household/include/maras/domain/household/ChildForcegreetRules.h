#pragma once

#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace MARAS::Domain::Household {

    using Foundation::ChildId;
    using Foundation::FormRawId;
    using Foundation::HouseId;

    struct ForcegreetSelectionInput {
        HouseId houseId{};
        bool hasAnimalCompanion{ false };
        float currentGameDays{ 0.0f };
        std::int32_t critterChance{ 10 };
        std::int32_t nameCallingChance{ 10 };
    };

    struct ForcegreetSelectionPlan {
        std::int32_t eventNumber{ 0 };
        ChildId eventChild{};
        ChildId sceneChild1{};
        ChildId sceneChild2{};
        bool markChestIntroduced{ false };
        bool clearNewlyAdoptedForHouse{ false };
        bool clearNameCallingUsedLast{ false };
    };

    struct PetSuitabilityDecision {
        bool isDog{ false };
        std::int32_t suitabilityRank{ 1 };  // 3 = All Children, 1 = None
    };

    struct ChildGiftTier {
        std::int32_t tierIndex{ 0 };      // 0=0000, 1=0025, 2=0050, 3=0100, 4=0250, 5=0500, 6=1000
        std::int32_t baseDeduction{ 1 };  // 1, 25, 50, 100, 250, 500, 1000
    };

    struct RegularGiftPlan {
        std::int32_t tierIndex{ 0 };
        std::int32_t count{ 1 };
        std::int32_t goldDeduction{ 0 };
    };

    struct ChestRefillPlan {
        std::vector<std::int32_t> junkItemCounts{};
        std::int32_t tier0025RollCount{ 0 };
        bool hasBonusGift{ false };
        ChildId richestChild{};
        std::int32_t bonusGiftTierIndex{ 0 };
        std::int32_t bonusGoldDeduction{ 0 };
    };

    /**
     * Plans the welcome-home child forcegreet event (Events 1-7, 10-13) deterministically.
     * Priority: newly adopted (1/2) -> chest intro (3) -> pet (4) -> critter (5) ->
     * name-calling (6/7) -> weighted generic event (10-13).
     */
    [[nodiscard]] ForcegreetSelectionPlan PlanReadyForcegreetEvent(
        const State::WorldStateSnapshot& snap,
        const ForcegreetSelectionInput& input,
        Foundation::IRng& rng);

    /**
     * Plans a fallback generic forcegreet event (10-13) with weighted child selection.
     */
    [[nodiscard]] ForcegreetSelectionPlan PlanGenericForcegreetFallback(
        const State::WorldStateSnapshot& snap,
        HouseId houseId,
        float currentGameDays,
        Foundation::IRng& rng);

    /**
     * Evaluates whether a companion animal race is a dog (AV Variable06 = 1) and whether any
     * child can adopt it (rank 3) or not (rank 1).
     */
    [[nodiscard]] PetSuitabilityDecision EvaluatePetRaceSuitability(
        FormRawId petRaceId,
        std::span<const FormRawId> dogRaceIds,
        std::span<const FormRawId> allowedRaceIds) noexcept;

    /**
     * Computes PoorCounter delta for given allowance gold amount (<=0 -> +1, >500 -> -5, etc.).
     */
    [[nodiscard]] std::int32_t CalculateAllowancePoorDelta(std::int32_t allowanceAmount) noexcept;

    /**
     * Applies a signed PoorCounter delta, clamping at 0.
     */
    [[nodiscard]] std::int32_t ApplyPoorCounterDelta(
        std::int32_t currentCounter,
        std::int32_t delta) noexcept;

    /**
     * Maps a child's current gold count to gift list tier index (0..6) and base deduction.
     */
    [[nodiscard]] ChildGiftTier SelectChildGiftTier(std::int32_t childGold) noexcept;

    /**
     * Rolls a regular child gift (tier index, luck count 1 or 2, clamped gold deduction).
     */
    [[nodiscard]] RegularGiftPlan PlanChildRegularGift(
        std::int32_t childGold,
        Foundation::IRng& rng) noexcept;

    /**
     * Plans child chest refill quantities and optional richest-child bonus item (> 25 gold).
     */
    [[nodiscard]] ChestRefillPlan PlanChildChestRefill(
        std::span<const std::pair<ChildId, std::int32_t>> childGolds,
        Foundation::IRng& rng);

}  // namespace MARAS::Domain::Household
