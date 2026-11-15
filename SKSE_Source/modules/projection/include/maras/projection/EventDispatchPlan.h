#pragma once

#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Projection {

    enum class SkyrimNetEventKind : std::uint8_t {
        DirectNarration = 0,
        PolygamyBlessing,
        AffectionGift,
        MaritalStatusChange,
        OpenRelationshipChange,
        SpouseHierarchyChange,
        SpouseAffectionChange,
        ProposalDecision,
        ActionDecline,
        ChildAdopted,
        ChildInherited,
        ChildParentsChanged,
        PregnancyAnnounced,
        PregnancyLost,
        ChildBirth,
        ChildGrewUp,
        ChildSentToTraining,
        ChildBecameAdult,
        PaternitySecretResolved,
        AffairConfessed,
        AffairExposed,
        AffairResolved,
        CheatingCaught,
        OpenRelationshipWalkIn,
        LaborStarted,
        LaborProgress
    };

    enum class DispatchMaskMode : std::uint8_t {
        Unmasked = 0,
        MaskForPartner = 1,
        MaskForChildOnly = 2
    };

    struct EventDeliveryLeg {
        State::ActorId originator{};
        State::ActorId target{};
        bool exactAudience{ false };
        std::vector<State::ActorId> audience{};
        bool skipTrigger{ true };
        DispatchMaskMode maskMode{ DispatchMaskMode::Unmasked };
    };

    struct EventDispatchPlan {
        SkyrimNetEventKind kind{ SkyrimNetEventKind::DirectNarration };
        bool shouldEmit{ false };
        std::vector<EventDeliveryLeg> legs{};
        float postSuppressNarrationSeconds{ 0.0f };
        std::string canonicalOutcome{};
        bool wasPregnancyUnannounced{ false };
    };

    /**
     * Pure snapshot resolution of parental roles and active paternity/pregnancy
     * secrecy for a child, filtered exclusively through `PerspectiveView`.
     */
    struct ResolvedChildSecrecy {
        bool found{ false };
        State::ChildStage stage{ State::ChildStage::Child };
        State::ActorId mother{};
        State::ActorId presumedFather{};
        State::ActorId bioFather{};
        bool isSecretForAssumed{ false };
        bool isSecretForBio{ false };
        bool isPregnancySecretForAssumed{ false };
    };

    using NarrationProximityFn = std::function<bool(State::ActorId speaker, State::ActorId target)>;

    [[nodiscard]] ResolvedChildSecrecy ResolveChildSecrecy(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId);

    [[nodiscard]] EventDispatchPlan PlanDirectNarration(
        State::ActorId originator,
        State::ActorId target,
        bool exactAudience,
        const std::vector<State::ActorId>& rawAudience,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanPolygamyBlessing(
        State::ActorId player,
        State::ActorId spouse,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanAffectionGift(
        State::ActorId player,
        State::ActorId receiver,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanMaritalStatusChange(
        State::ActorId initiator,
        State::ActorId acceptor,
        std::string_view newStatus,
        bool forceSkipTrigger,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanOpenRelationshipChange(
        State::ActorId initiator,
        State::ActorId partner,
        bool forceSkipTrigger,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanSpouseHierarchyChange(
        State::ActorId player,
        State::ActorId spouse,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanSpouseAffectionChange(
        State::ActorId spouse,
        State::ActorId player,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanProposalDecision(
        State::ActorId initiator,
        State::ActorId acceptor,
        bool forceSkipTrigger,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanActionDecline(
        State::ActorId speaker,
        State::ActorId target,
        bool skipTrigger,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanChildGuardianEvent(
        SkyrimNetEventKind kind,
        State::ActorId childActor,
        State::ActorId primaryGuardian,
        State::ActorId secondaryGuardian,
        std::string_view reason,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanPregnancyAnnounced(
        State::ActorId mother,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanPregnancyLost(
        State::ActorId mother,
        State::ActorId partner,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanLaborStarted(
        State::ActorId mother,
        State::ActorId partner,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanLaborProgress(State::ActorId mother);

    [[nodiscard]] EventDispatchPlan PlanChildBirth(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ActorId mother,
        const std::vector<State::ChildId>& childIds,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanChildGrewUp(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanChildSentToTraining(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanChildBecameAdult(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanPaternitySecretResolved(
        std::shared_ptr<const State::WorldStateSnapshot> world,
        std::shared_ptr<const State::SpatialActorSnapshot> spatial,
        State::ChildId childId,
        std::string_view rawCaseType,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanAffairConfessed(
        State::ActorId cheater,
        State::ActorId victim,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanAffairExposed(
        State::ActorId affairPartner,
        State::ActorId victim,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanAffairResolved(
        State::ActorId initiator,
        State::ActorId otherParty,
        bool narrationSuppressed,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanCheatingCaught(
        State::ActorId victim,
        State::ActorId cheater,
        const NarrationProximityFn& canTrigger);

    [[nodiscard]] EventDispatchPlan PlanOpenRelationshipWalkIn(
        State::ActorId witness,
        State::ActorId partner,
        const NarrationProximityFn& canTrigger);

}  // namespace MARAS::Projection
