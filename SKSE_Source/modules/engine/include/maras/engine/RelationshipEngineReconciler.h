#pragma once

#include "maras/domain/relationships/MarriageDifficultyRules.h"
#include "maras/engine/EngineLogging.h"
#include "maras/foundation/DomainEnums.h"

#include <string_view>

namespace MARAS {

    /**
     * RelationshipEngineReconciler
     * ----------------------------
     * Executes engine-side relationship side-effects (vanilla relationship rank
     * reads/writes, cooldown/pacify spell application, $TTM_MSG_* HUD notifications,
     * and No-AI dialogue chance TESGlobal updates) safely with SessionEpoch & main-thread
     * awareness.
     */
    class RelationshipEngineReconciler {
    public:
        static RelationshipEngineReconciler& GetSingleton();

        [[nodiscard]] int GetVanillaRelationshipRank(RE::FormID npcID, RE::FormID targetID) const;

        void SetVanillaRelationshipRank(RE::Actor* npc, RE::Actor* target, int rank) const;

        void PacifyFianceOrSpouse(RE::FormID npcID, bool enable) const;

        void ReconcilePlayerStatusTransition(RE::FormID npcID,
                                             Foundation::RelationshipStatus newStatus,
                                             bool appliedBreakupCooldown,
                                             bool sendNotifications) const;

        void NotifyEngagementCeremonyStarted(RE::FormID npcID) const;

        void ReconcileHierarchyRankChange(RE::FormID spouseID, float rankDiff) const;

        void NotifyAffectionThresholdChange(RE::FormID spouseID,
                                            std::string_view level,
                                            float affectionDiff) const;

        void ApplyNoAiDialogueChanceGlobals(
            const Domain::Relationships::NoAiDialogueChances& chances) const;

    private:
        RelationshipEngineReconciler() = default;
        ~RelationshipEngineReconciler() = default;

        RelationshipEngineReconciler(const RelationshipEngineReconciler&) = delete;
        RelationshipEngineReconciler& operator=(const RelationshipEngineReconciler&) = delete;
        RelationshipEngineReconciler(RelationshipEngineReconciler&&) = delete;
        RelationshipEngineReconciler& operator=(RelationshipEngineReconciler&&) = delete;
    };

}  // namespace MARAS
