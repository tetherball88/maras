#pragma once

#include "RE/Skyrim.h"

#include <cstdint>

namespace MARAS::App {

    /**
     * Layer 4 application coordinator for player-NPC relationship interactions:
     * dialogue-open No-AI chance evaluation + optional TT_LoversLedger intimacy
     * refresh, spouse income sharing, divorce settlement modal/bounty execution,
     * and co-save-backed relationship flags (brokeup time, player killer,
     * wedding history, skip-wedding suppression).
     */
    class RelationshipInteractionCoordinator {
    public:
        static RelationshipInteractionCoordinator& GetSingleton();

        void OnDialogueOpened(RE::FormID npcFormID);

        void ApplyDialogueLoversLedgerBonus(RE::FormID npcFormID,
                                            int exclusiveSex,
                                            int groupSex,
                                            int playerTopLoverRank);

        void ShareSpouseIncome(RE::FormID spouseFormID);

        void ProcessDivorceSettlement(RE::FormID exSpouseFormID, RE::TESQuest* divorceQuest);

        [[nodiscard]] float GetBrokeupTime(RE::FormID npcFormID) const;

        [[nodiscard]] bool GetPlayerKiller() const;
        void SetPlayerKiller(bool value);

        [[nodiscard]] bool GetPlayerHadWedding() const;
        void SetPlayerHadWedding(bool value);

        [[nodiscard]] bool ShouldPromptSkipWedding() const;
        void SetDontShowSkipWeddingMessage(bool value);

        void NotifyEngagementCeremonyStarted(RE::FormID npcFormID) const;

    private:
        RelationshipInteractionCoordinator() = default;
        ~RelationshipInteractionCoordinator() = default;

        RelationshipInteractionCoordinator(const RelationshipInteractionCoordinator&) = delete;
        RelationshipInteractionCoordinator& operator=(const RelationshipInteractionCoordinator&) = delete;
        RelationshipInteractionCoordinator(RelationshipInteractionCoordinator&&) = delete;
        RelationshipInteractionCoordinator& operator=(RelationshipInteractionCoordinator&&) = delete;
    };

}  // namespace MARAS::App
