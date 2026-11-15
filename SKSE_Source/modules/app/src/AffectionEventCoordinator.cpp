#include "maras/app/AffectionEventCoordinator.h"

#include "maras/app/RelationshipUseCases.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/WorldStore.h"

namespace MARAS::App {

    AffectionEventCoordinator& AffectionEventCoordinator::GetSingleton() {
        static AffectionEventCoordinator instance;
        return instance;
    }

    void AffectionEventCoordinator::ApplyAdjustments(
        const std::vector<Domain::Relationships::PartnerAffectionAdjustment>& adjustments) {
        if (adjustments.empty()) {
            return;
        }
        auto& relUseCases = RelationshipUseCases::GetSingleton();
        for (const auto& adj : adjustments) {
            if (adj.partner.IsNull() || adj.partner.IsPlayer()) {
                continue;
            }
            relUseCases.SetPermanentAffection(adj.partner.raw(), adj.newScore, /*sendEvent=*/true);
            MARAS_LOG_INFO(
                "[AffectionEventCoordinator] {:08X} ({}) delta={} ({}, temp={}) {} -> {}",
                adj.partner.raw(),
                ActorSvc::GetName(adj.partner.raw()),
                adj.appliedDelta,
                adj.reason,
                Foundation::TemperamentToString(adj.temperament),
                adj.oldScore,
                adj.newScore);
        }
    }

    // ── Daily Affection Entry Points ─────────────────────────────────────────

    void AffectionEventCoordinator::AddIntimacyAffection(RE::FormID spouseId, float baseAffection) {
        if (spouseId == 0 || spouseId == RelationshipUseCases::kPlayerFormID) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto temperament =
            Domain::Relationships::ResolveActorTemperament(*snap, State::ActorId{ spouseId });
        const float delta =
            Domain::Relationships::CalculateIntimacyDailyDelta(temperament, baseAffection);
        RelationshipUseCases::GetSingleton().AddAffection(spouseId, delta, "Intimacy");
    }

    void AffectionEventCoordinator::AddGiftAffection(RE::FormID spouseId, float goldValue) {
        if (spouseId == 0 || spouseId == RelationshipUseCases::kPlayerFormID) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto actorKey = State::ActorId{ spouseId };
        const auto socialClass = Domain::Relationships::ResolveActorSocialClass(*snap, actorKey);
        const auto temperament = Domain::Relationships::ResolveActorTemperament(*snap, actorKey);
        const float delta =
            Domain::Relationships::CalculateGiftDailyDelta(socialClass, temperament, goldValue);
        RelationshipUseCases::GetSingleton().AddAffection(spouseId, delta, "Gift");
    }

    void AffectionEventCoordinator::AddSleptAffection(RE::FormID spouseId) {
        if (spouseId == 0 || spouseId == RelationshipUseCases::kPlayerFormID) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto temperament =
            Domain::Relationships::ResolveActorTemperament(*snap, State::ActorId{ spouseId });
        const float delta = Domain::Relationships::CalculateSleptDailyDelta(temperament);
        RelationshipUseCases::GetSingleton().AddAffection(spouseId, delta, "Slept");
    }

    void AffectionEventCoordinator::OnDialogueOpened(RE::FormID npcId) {
        if (State::IsLegacyMigrationActive() ||
            npcId == 0 || npcId == RelationshipUseCases::kPlayerFormID) {
            return;
        }
        auto& relUseCases = RelationshipUseCases::GetSingleton();
        if (relUseCases.GetRelationshipStatus(npcId, RelationshipUseCases::kPlayerFormID) ==
            Foundation::RelationshipStatus::Unknown) {
            return;
        }
        relUseCases.AddAffection(
            npcId, Domain::Relationships::CalculateDialogueStartedDailyDelta(), "DialogueStarted");
    }

    void AffectionEventCoordinator::OnHierarchyRankChanged(RE::FormID spouseId, std::int32_t rankDiff) {
        if (State::IsLegacyMigrationActive() || spouseId == 0 || rankDiff == 0) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto temperament =
            Domain::Relationships::ResolveActorTemperament(*snap, State::ActorId{ spouseId });
        const float delta =
            Domain::Relationships::CalculatePromotionDailyDelta(temperament, static_cast<float>(rankDiff));
        RelationshipUseCases::GetSingleton().AddAffection(spouseId, delta, "Promotion");
    }

    // ── Permanent Life-Event Entry Points ────────────────────────────────────

    void AffectionEventCoordinator::AdjustPlayerPartnerAffection(
        RE::FormID partnerId,
        float baseDelta,
        std::string_view contextReason) {
        if (partnerId == 0 || partnerId == RelationshipUseCases::kPlayerFormID) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (auto planned = Domain::Relationships::PlanPlayerPartnerAffectionAdjustment(
                *snap, State::ActorId{ partnerId }, baseDelta, contextReason)) {
            ApplyAdjustments({ *planned });
        }
    }

    void AffectionEventCoordinator::ApplyPregnancyConceivedAffection(
        RE::FormID partnerId,
        bool isPlayerPregnant) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanPregnancyConceivedAffection(
            *snap, State::ActorId{ partnerId }, isPlayerPregnant));
    }

    void AffectionEventCoordinator::ApplyPregnancyLostAffection(
        RE::FormID partnerId,
        std::string_view reason) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanPregnancyLostAffection(
            *snap, State::ActorId{ partnerId }, reason));
    }

    void AffectionEventCoordinator::ApplyChildBirthAffection(
        RE::FormID partnerId,
        std::int32_t childCount) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanChildBirthAffection(
            *snap, State::ActorId{ partnerId }, childCount));
    }

    void AffectionEventCoordinator::ApplyChildMilestoneForParents(
        RE::FormID childId,
        std::string_view milestone) {
        if (childId == 0) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanChildMilestoneForParents(
            *snap, State::ChildId{ childId }, milestone));
    }

    // ── Engine / Domain Transition Hooks ─────────────────────────────────────

    void AffectionEventCoordinator::OnCheatingCaught(RE::FormID cheaterId, RE::FormID victimId) {
        if (State::IsLegacyMigrationActive()) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanCheatingCaughtAffection(
            *snap, State::ActorId{ cheaterId }, State::ActorId{ victimId }));
    }

    void AffectionEventCoordinator::OnOpenRelationshipWalkIn(RE::FormID partnerId, RE::FormID witnessId) {
        if (State::IsLegacyMigrationActive()) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanOpenRelationshipWalkInAffection(
            *snap, State::ActorId{ partnerId }, State::ActorId{ witnessId }));
    }

    void AffectionEventCoordinator::OnOpenRelationshipToggled(
        RE::FormID actorA,
        RE::FormID actorB,
        bool isOpen) {
        if (State::IsLegacyMigrationActive()) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanOpenRelationshipToggleAffection(
            *snap, State::ActorId{ actorA }, State::ActorId{ actorB }, isOpen));
    }

    void AffectionEventCoordinator::OnAffairDiscovered(
        RE::FormID cheaterId,
        RE::FormID victimId,
        RE::FormID affairPartnerId,
        Foundation::AffairDiscovery discovery) {
        if (State::IsLegacyMigrationActive() || cheaterId == 0 || victimId == 0) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (discovery == Foundation::AffairDiscovery::Confessed) {
            ApplyAdjustments(Domain::Relationships::PlanAffairConfessedAffection(
                *snap, State::ActorId{ cheaterId }, State::ActorId{ victimId }));
        } else if (discovery == Foundation::AffairDiscovery::Exposed) {
            ApplyAdjustments(Domain::Relationships::PlanAffairExposedAffection(
                *snap, State::ActorId{ cheaterId }, State::ActorId{ victimId },
                State::ActorId{ affairPartnerId }));
        }
    }

    void AffectionEventCoordinator::OnAffairResolved(
        RE::FormID cheaterId,
        RE::FormID victimId,
        std::string_view resolution) {
        if (State::IsLegacyMigrationActive() || cheaterId == 0 || victimId == 0) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanAffairResolvedAffection(
            *snap, State::ActorId{ cheaterId }, State::ActorId{ victimId }, resolution));
    }

    void AffectionEventCoordinator::OnPaternitySecretResolved(
        RE::FormID childId,
        std::string_view outcomeType) {
        if (State::IsLegacyMigrationActive() || childId == 0) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanPaternitySecretAffection(
            *snap, State::ChildId{ childId }, outcomeType));
    }

    void AffectionEventCoordinator::OnChildAdopted(RE::FormID partnerId) {
        if (State::IsLegacyMigrationActive() || partnerId == 0) {
            return;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        ApplyAdjustments(Domain::Relationships::PlanChildAdoptedAffection(
            *snap, State::ActorId{ partnerId }));
    }

    void AffectionEventCoordinator::OnChildMilestone(RE::FormID childId, std::string_view milestone) {
        if (State::IsLegacyMigrationActive() || childId == 0) {
            return;
        }
        ApplyChildMilestoneForParents(childId, milestone);
    }

}  // namespace MARAS::App
