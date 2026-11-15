#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MARAS::Domain::Relationships {

    using Foundation::ActorId;
    using Foundation::CanonicalPairKey;
    using Foundation::RelationshipStatus;

    /// Returns true if the status clears `isOpen` during transitions (includes Candidate demotions).
    [[nodiscard]] constexpr bool IsBreakupStatusIncludingCandidate(RelationshipStatus status) noexcept {
        return Foundation::IsBreakupStatus(status) || status == RelationshipStatus::Candidate;
    }

    /// Context injected by the engine layer when engine-only checks apply (child status, BGSRelationship consanguinity).
    struct RelationshipCheckContext {
        bool isChildA{ false };
        bool isChildB{ false };
        bool areEngineFamily{ false };
        int playerRelationshipRank{ 0 };
        bool partnerCheatedOnPlayer{ false };
        std::string engineFamilyReason{};
    };

    /// Outcome produced by ApplyRelationshipTransition for downstream engine sync & notifications.
    struct RelationshipTransitionOutcome {
        bool allowed{ false };
        bool skippedDuplicate{ false };
        bool recordedTransition{ false };
        bool appliedAutoOpen{ false };
        bool resetOpenOnBreakup{ false };
        bool recordMemory{ false };
        bool reconcilePlayerBuffs{ false };
        bool appliedBreakupCooldown{ false };
        RelationshipStatus oldStatus{ RelationshipStatus::Candidate };
        RelationshipStatus newStatus{ RelationshipStatus::Candidate };
        std::optional<int> offlineSyncedAffection{};
        std::string blockReason{};
        State::WorldMutationResult mutations{};
    };

    /// Candidate NPC-NPC engaged couple ready for auto-marriage or engagement timestamp backfill.
    struct AutoMarriageCandidate {
        CanonicalPairKey pair{};
        bool needsTimestampBackfill{ false };
        bool readyForAutoMarriage{ false };
    };

    [[nodiscard]] bool IsPlayerActiveMainPartner(const State::WorldStateSnapshot& snap, ActorId actor) noexcept;

    [[nodiscard]] bool CanOpenRelationship(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB) noexcept;

    [[nodiscard]] bool CanCloseRelationship(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB) noexcept;

    [[nodiscard]] bool AreMarasFamilyMembers(
        const State::WorldStateSnapshot& snap,
        const State::IActorMetadataProvider& meta,
        ActorId actorA,
        ActorId actorB,
        std::string* outReason = nullptr);

    [[nodiscard]] bool CanFormRelationship(
        const State::WorldStateSnapshot& snap,
        const State::IActorMetadataProvider& meta,
        ActorId actorA,
        ActorId actorB,
        RelationshipStatus targetStatus,
        const RelationshipCheckContext& ctx = {},
        std::string* outReason = nullptr);

    [[nodiscard]] bool IsAvailableForStatus(
        const State::WorldStateSnapshot& snap,
        ActorId actor,
        RelationshipStatus status) noexcept;

    /// Performs canonical status transition inside `ws`, applying forward-only auto-open,
    /// `isOpen` reset on breakup/Candidate, custody & hierarchy side effects, and offline affection sync.
    RelationshipTransitionOutcome ApplyRelationshipTransition(
        State::WorldState& ws,
        ActorId actorA,
        ActorId actorB,
        RelationshipStatus targetStatus,
        float currentGameDay,
        ActorId initiator,
        const State::IActorMetadataProvider& meta,
        const RelationshipCheckContext& ctx = {},
        Foundation::IRng* optionalRng = nullptr);

    [[nodiscard]] std::vector<AutoMarriageCandidate> CollectAutoMarriageCandidates(
        const State::WorldStateSnapshot& snap,
        float currentGameDay,
        float thresholdDays = 5.0f);

    void BackfillMissingEngagementTimestamps(
        State::WorldState& ws,
        std::span<const CanonicalPairKey> pairs,
        float currentGameDay);

}  // namespace MARAS::Domain::Relationships
