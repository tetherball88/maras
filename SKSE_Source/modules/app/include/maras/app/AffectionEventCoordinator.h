#pragma once

#include "RE/Skyrim.h"
#include "maras/domain/relationships/AffectionEventRules.h"
#include "maras/foundation/DomainEnums.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace MARAS::App {

    /**
     * Layer 4 application coordinator translating gameplay/lifecycle events into
     * pure `AffectionEventRules` decisions and applying daily or permanent affection
     * changes through `RelationshipUseCases`.
     */
    class AffectionEventCoordinator {
    public:
        static AffectionEventCoordinator& GetSingleton();

        AffectionEventCoordinator(const AffectionEventCoordinator&) = delete;
        AffectionEventCoordinator& operator=(const AffectionEventCoordinator&) = delete;
        AffectionEventCoordinator(AffectionEventCoordinator&&) = delete;
        AffectionEventCoordinator& operator=(AffectionEventCoordinator&&) = delete;

        // ── Daily Affection Entry Points ─────────────────────────────────────
        void AddIntimacyAffection(RE::FormID spouseId, float baseAffection);
        void AddGiftAffection(RE::FormID spouseId, float goldValue);
        void AddSleptAffection(RE::FormID spouseId);
        void OnDialogueOpened(RE::FormID npcId);
        void OnHierarchyRankChanged(RE::FormID spouseId, std::int32_t rankDiff);

        // ── Permanent Life-Event Entry Points ────────────────────────────────
        void AdjustPlayerPartnerAffection(RE::FormID partnerId,
                                          float baseDelta,
                                          std::string_view contextReason = "");

        void ApplyPregnancyConceivedAffection(RE::FormID partnerId, bool isPlayerPregnant = false);
        void ApplyPregnancyLostAffection(RE::FormID partnerId, std::string_view reason = "miscarriage");
        void ApplyChildBirthAffection(RE::FormID partnerId, std::int32_t childCount = 1);
        void ApplyChildMilestoneForParents(RE::FormID childId, std::string_view milestone = "child");

        // ── Engine / Domain Transition Hooks ─────────────────────────────────
        void OnCheatingCaught(RE::FormID cheaterId, RE::FormID victimId);
        void OnOpenRelationshipWalkIn(RE::FormID partnerId, RE::FormID witnessId);
        void OnOpenRelationshipToggled(RE::FormID actorA, RE::FormID actorB, bool isOpen);
        void OnAffairDiscovered(RE::FormID cheaterId,
                                RE::FormID victimId,
                                RE::FormID affairPartnerId,
                                Foundation::AffairDiscovery discovery);
        void OnAffairResolved(RE::FormID cheaterId,
                              RE::FormID victimId,
                              std::string_view resolution);
        void OnPaternitySecretResolved(RE::FormID childId, std::string_view outcomeType);
        void OnChildAdopted(RE::FormID partnerId);
        void OnChildMilestone(RE::FormID childId, std::string_view milestone);

    private:
        AffectionEventCoordinator() = default;
        ~AffectionEventCoordinator() = default;

        void ApplyAdjustments(
            const std::vector<Domain::Relationships::PartnerAffectionAdjustment>& adjustments);
    };

}  // namespace MARAS::App
