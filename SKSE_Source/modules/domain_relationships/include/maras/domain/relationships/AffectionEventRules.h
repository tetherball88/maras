#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Domain::Relationships {

    inline constexpr int kDefaultAffectionScore = 50;

    /// Planned permanent affection mutation for a single NPC partner toward the Player.
    struct PartnerAffectionAdjustment {
        State::ActorId partner{};
        Foundation::Temperament temperament{ Foundation::Temperament::Proud };
        float baseDelta{ 0.0f };
        int appliedDelta{ 0 };
        int oldScore{ kDefaultAffectionScore };
        int newScore{ kDefaultAffectionScore };
        std::string reason{};

        bool operator==(const PartnerAffectionAdjustment&) const = default;
    };

    // =========================================================================
    // Pure Daily Affection Calculators
    // =========================================================================

    [[nodiscard]] float CalculateIntimacyDailyDelta(
        Foundation::Temperament temperament,
        float baseAffection) noexcept;

    [[nodiscard]] float CalculateGiftDailyDelta(
        Foundation::SocialClass socialClass,
        Foundation::Temperament temperament,
        float goldValue) noexcept;

    [[nodiscard]] constexpr float CalculateDialogueStartedDailyDelta() noexcept {
        return 1.0f;
    }

    [[nodiscard]] float CalculatePromotionDailyDelta(
        Foundation::Temperament temperament,
        float rankDiff) noexcept;

    [[nodiscard]] float CalculateSleptDailyDelta(
        Foundation::Temperament temperament) noexcept;

    // =========================================================================
    // Pure Snapshot Lookup & Life-Event Scaling Helpers
    // =========================================================================

    [[nodiscard]] Foundation::RelationshipStatus ResolvePlayerRelativeStatus(
        const State::WorldStateSnapshot& snap,
        State::ActorId npc) noexcept;

    [[nodiscard]] constexpr bool IsEligiblePlayerAffectionStatus(
        Foundation::RelationshipStatus status) noexcept {
        return status == Foundation::RelationshipStatus::Married ||
               status == Foundation::RelationshipStatus::Engaged ||
               status == Foundation::RelationshipStatus::Dating ||
               status == Foundation::RelationshipStatus::Lover ||
               status == Foundation::RelationshipStatus::Candidate;
    }

    [[nodiscard]] Foundation::Temperament ResolveActorTemperament(
        const State::WorldStateSnapshot& snap,
        State::ActorId npc) noexcept;

    [[nodiscard]] Foundation::SocialClass ResolveActorSocialClass(
        const State::WorldStateSnapshot& snap,
        State::ActorId npc) noexcept;

    [[nodiscard]] int ScalePlayerPartnerLifeEventDelta(
        Foundation::Temperament temperament,
        float baseDelta) noexcept;

    [[nodiscard]] std::optional<PartnerAffectionAdjustment> PlanPlayerPartnerAffectionAdjustment(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        float baseDelta,
        std::string_view reason);

    [[nodiscard]] State::ActorId ResolveChildMotherPartner(
        const State::WorldStateSnapshot& snap,
        const State::ChildRecord& rec) noexcept;

    [[nodiscard]] bool IsMotherPregnancySecretForPartner(
        const State::WorldStateSnapshot& snap,
        State::ActorId mother) noexcept;

    [[nodiscard]] bool IsChildSecretForBioFather(
        const State::WorldStateSnapshot& snap,
        const State::ChildRecord& rec) noexcept;

    // =========================================================================
    // Pure Life-Event Planners (Zero Side Effects)
    // =========================================================================

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanPregnancyConceivedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        bool isPlayerPregnant = false);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanPregnancyLostAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        std::string_view reason = "miscarriage");

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanChildBirthAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        int childCount = 1);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanChildAdoptedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanChildMilestoneAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        std::string_view milestone = "child");

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanChildMilestoneForParents(
        const State::WorldStateSnapshot& snap,
        State::ChildId childId,
        std::string_view milestone = "child");

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanCheatingCaughtAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanOpenRelationshipToggleAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId actorA,
        State::ActorId actorB,
        bool isOpen);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanOpenRelationshipWalkInAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId partner,
        State::ActorId witness);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanAffairConfessedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanAffairExposedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim,
        State::ActorId affairPartner);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanAffairResolvedAffection(
        const State::WorldStateSnapshot& snap,
        State::ActorId cheater,
        State::ActorId victim,
        std::string_view resolution);

    [[nodiscard]] std::vector<PartnerAffectionAdjustment> PlanPaternitySecretAffection(
        const State::WorldStateSnapshot& snap,
        State::ChildId childId,
        std::string_view outcomeType);

}  // namespace MARAS::Domain::Relationships
