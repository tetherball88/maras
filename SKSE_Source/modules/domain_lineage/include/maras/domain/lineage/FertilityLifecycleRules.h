#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Lineage {

    using Foundation::ActorId;
    using Foundation::ChildId;
    using Foundation::ChildStage;

    enum class FertilityProvider : std::uint8_t {
        Unknown = 0,
        Fmr = 1,
        BeeingFemale = 2
    };

    enum class BfCycleStateAction : std::uint8_t {
        None = 0,
        VisibleTrimesterReveal = 1,
        RecoveryDelivery = 2
    };

    enum class BlockedFertilityMcmAction : std::uint8_t {
        SpawnYoungChild = 0,
        SendToTraining = 1,
        SendToOrphanage = 2,
        SpawnAdultChild = 3,
        DismissAdultChild = 4,
        GrowOrHatchChild = 5
    };

    struct BlockedFertilityMcmClick {
        FertilityProvider provider{ FertilityProvider::Unknown };
        BlockedFertilityMcmAction action{ BlockedFertilityMcmAction::SpawnYoungChild };

        bool operator==(const BlockedFertilityMcmClick&) const = default;
    };

    struct FertilityAffectionTarget {
        ActorId targetActor{};
        bool isPlayerPregnant{ false };

        bool operator==(const FertilityAffectionTarget&) const = default;
    };

    struct BeginLaborBatchOutcome {
        bool startedNewBatch{ false };
        std::int32_t totalCount{ 0 };

        bool operator==(const BeginLaborBatchOutcome&) const = default;
    };

    struct RecordDeliveredChildOutcome {
        bool batchCompleted{ false };
        std::vector<ChildId> completedChildIds{};
        std::int32_t bornSoFar{ 0 };
        std::int32_t laborTotal{ 0 };

        bool operator==(const RecordDeliveredChildOutcome&) const = default;
    };

    struct SpawnedChildLineagePlan {
        bool isPlayerChild{ false };
        ActorId bioMother{};
        ActorId bioFather{};
        ActorId presumedFather{};
        ActorId coSpouse{};
        ActorId npcGuardianA{};
        ActorId npcGuardianB{};
        ActorId tenantHouseLookupActor{};

        bool operator==(const SpawnedChildLineagePlan&) const = default;
    };

    struct AdultGraduationPlan {
        ChildId previousId{};
        ChildStage previousStage{ ChildStage::_Count };
        bool alreadyAdultSameActor{ false };
        ActorId bioMother{};
        ActorId bioFather{};

        bool operator==(const AdultGraduationPlan&) const = default;
    };

    [[nodiscard]] FertilityProvider ParseFertilityProvider(std::string_view raw) noexcept;

    [[nodiscard]] constexpr bool IsFmrVisiblePregnancyRank(std::int32_t factionRank) noexcept {
        return (factionRank >= 34 && factionRank <= 100) || factionRank == 120;
    }

    [[nodiscard]] BfCycleStateAction ClassifyBfCycleState(std::int32_t currentState) noexcept;

    [[nodiscard]] std::optional<BlockedFertilityMcmClick> ClassifyBlockedFertilityMcmClick(
        std::string_view configScriptName,
        std::string_view optionStateName) noexcept;

    [[nodiscard]] ChildId FindSyntheticChildByName(
        const State::LineageTable& lineage,
        ActorId motherId,
        std::string_view name,
        std::optional<ChildStage> requiredStage = std::nullopt);

    [[nodiscard]] ChildId FindSyntheticInfantForSpawn(
        const State::LineageTable& lineage,
        ActorId motherId,
        std::string_view name);

    [[nodiscard]] bool HasDuplicateFmrNewbornInfant(
        const State::LineageTable& lineage,
        ActorId motherId);

    [[nodiscard]] bool CanRegisterFertilityConception(
        const State::WorldStateSnapshot& snap,
        ActorId motherId,
        FertilityProvider provider);

    [[nodiscard]] FertilityAffectionTarget ResolveFertilityAffectionTarget(
        ActorId motherId,
        ActorId presumedFatherId,
        ActorId bioFatherId) noexcept;

    [[nodiscard]] ActorId ResolveFertilityEventPartner(
        ActorId presumedFatherId,
        ActorId bioFatherId) noexcept;

    BeginLaborBatchOutcome ApplyBeginLaborBatch(
        State::LineageTable& lineage,
        ActorId motherId,
        std::int32_t reportedChildCount,
        float currentGameDay);

    [[nodiscard]] ChildId ResolveBabyNtrCorrectionTarget(
        const State::LineageTable& lineage,
        ActorId motherId,
        ActorId bioFatherId);

    bool ApplyAppendBornToDeliveryBatch(
        State::LineageTable& lineage,
        ActorId motherId,
        ChildId childId);

    RecordDeliveredChildOutcome ApplyRecordDeliveredChild(
        State::LineageTable& lineage,
        ActorId motherId,
        ChildId childId,
        bool fromUnborn);

    std::optional<State::ActiveDeliveryBatch> ApplyPopDeliveryBatch(
        State::LineageTable& lineage,
        ActorId motherId);

    [[nodiscard]] SpawnedChildLineagePlan PlanSpawnedChildLineage(
        const State::WorldStateSnapshot& snap,
        ChildId syntheticOrExistingId,
        ActorId bioMotherHint,
        ActorId bioFatherHint,
        ChildStage stage = ChildStage::Child) noexcept;

    [[nodiscard]] AdultGraduationPlan ResolveAdultGraduationSource(
        const State::LineageTable& lineage,
        ActorId adultActorId,
        ActorId oldChildActorId,
        ActorId motherHintId,
        ActorId fatherHintId,
        std::string_view childName,
        bool allowFallbackToAdultId);

}  // namespace MARAS::Domain::Lineage
