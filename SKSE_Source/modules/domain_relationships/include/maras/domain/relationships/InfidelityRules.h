#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MARAS::Domain::Relationships {

    using Foundation::ActorId;
    using Foundation::AffairDiscovery;
    using Foundation::AffairResolution;
    using Foundation::AffairStage;
    using Foundation::AffairStatus;
    using Foundation::AffairType;
    using Foundation::PartnerIntentType;
    using Foundation::RelationshipStatus;

    struct ClearedIntentDelta {
        ActorId partnerId{};
        PartnerIntentType previousType{ PartnerIntentType::None };

        bool operator==(const ClearedIntentDelta&) const noexcept = default;
    };

    struct PairOpenOutcome {
        bool changed{ false };
        bool wasOpen{ false };
        RelationshipStatus pairStatus{ RelationshipStatus::Candidate };
        std::vector<std::pair<ActorId, ActorId>> brokenLovers{};
        std::vector<ActorId> clearedIntents{};
        std::vector<ActorId> allowedCheatersToRefresh{};
    };

    struct CheatingIncidentOutcome {
        bool recorded{ false };
        bool intentUpgraded{ false };
        AffairStage recordedStage{ AffairStage::Physical };
    };

    struct AffairStageUpdateOutcome {
        bool updated{ false };
        bool intentUpgraded{ false };
        ActorId upgradedLover{};
        std::vector<ActorId> updatedParamours{};
        AffairStage appliedStage{ AffairStage::Physical };
    };

    struct AffairDiscoveryEvent {
        ActorId cheater{};
        ActorId victim{};
        ActorId paramour{};
    };

    struct ExposeAffairOutcome {
        bool updated{ false };
        bool isDiscovery{ false };
        AffairDiscovery discovery{ AffairDiscovery::None };
        AffairResolution resolution{ AffairResolution::None };
        std::vector<AffairDiscoveryEvent> events{};
        std::vector<ActorId> clearedIntents{};
        std::vector<ActorId> victimsToUpdate{};
        std::vector<ActorId> victimsToOpen{};
        std::vector<ActorId> victimsToSplit{};
        std::vector<ActorId> partnersToBreak{};
    };

    struct CleanupAffairsOutcome {
        std::vector<ClearedIntentDelta> clearedIntents{};
        std::vector<ActorId> affectedActors{};
    };

    [[nodiscard]] bool IsActorCheating(
        const State::WorldStateSnapshot& snap,
        ActorId actor) noexcept;

    [[nodiscard]] bool IsRelationshipCheating(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB) noexcept;

    PairOpenOutcome ApplySetPairOpen(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB,
        bool isOpen,
        float currentGameDay);

    CheatingIncidentOutcome ApplyRecordCheating(
        State::WorldState& ws,
        ActorId cheater,
        ActorId victim,
        ActorId paramour,
        bool isSecret,
        AffairType affairType,
        AffairStatus affairStatus,
        AffairStage affairStage,
        float currentGameDay);

    AffairStageUpdateOutcome ApplySetAffairStage(
        State::WorldState& ws,
        ActorId cheater,
        ActorId victim,
        ActorId affairPartnerFilter,
        AffairStage stage,
        float currentGameDay);

    [[nodiscard]] AffairStage GetEffectiveAffairStage(
        const State::WorldStateSnapshot& snap,
        ActorId cheater,
        ActorId victim,
        ActorId affairPartnerFilter) noexcept;

    ExposeAffairOutcome ApplyAffairDiscoveryOrResolution(
        State::WorldState& ws,
        ActorId cheater,
        ActorId affairPartner,
        std::string_view statusOrResolution,
        ActorId targetVictim = ActorId{});

    std::vector<ClearedIntentDelta> EvaluateBreakupAffairsAndIntents(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB);

    CleanupAffairsOutcome ApplyCleanupActorIntentsAndThirdPartyAffairs(
        State::WorldState& ws,
        ActorId target,
        bool eraseRecords);

    std::vector<ActorId> MarkCheatingAllowedForPair(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB);

    std::size_t MarkPlayerCheatingAllowedOnPolygamy(State::WorldState& ws);

}  // namespace MARAS::Domain::Relationships
