#pragma once

#include "RE/Skyrim.h"
#include "maras/app/ModalTypes.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MARAS::App {

    class AgencyWorkflowCoordinator {
    public:
        static AgencyWorkflowCoordinator& GetSingleton();

        AgencyWorkflowCoordinator(const AgencyWorkflowCoordinator&) = delete;
        AgencyWorkflowCoordinator& operator=(const AgencyWorkflowCoordinator&) = delete;
        AgencyWorkflowCoordinator(AgencyWorkflowCoordinator&&) = delete;
        AgencyWorkflowCoordinator& operator=(AgencyWorkflowCoordinator&&) = delete;

        // =====================================================================
        // NPC Matchmaking Workflows
        // =====================================================================

        void TryScheduledMatchmaking(float currentGameDay);
        void RunLLMScheduledMatchmaking(float currentGameDay);
        void RunDiceRollMatchmaking(float currentGameDay);

        bool RunManualMatchmaking(const std::vector<RE::FormID>& npcFormIDs);
        bool RunGlobalMatchmaking(std::optional<std::size_t> maxResultsOverride = std::nullopt);

        void ProcessManualResults(RE::FormID targetNpcId, const nlohmann::json& matchesArray);
        void ProcessGlobalManualResults(const nlohmann::json& matchesArray);

        [[nodiscard]] bool IsMatchmakingRunning() const noexcept;
        void ClearRunningBatchState() noexcept;

        // =====================================================================
        // Player-Partner Agency (Infidelity / Open-Close Intents) & Home Arrival
        // =====================================================================

        void TryScheduledPlayerPartnerInfidelity(float currentGameDay);
        [[nodiscard]] bool EvaluatePartnerHomeArrival(RE::BGSLocation* newLocation = nullptr);
        [[nodiscard]] bool IsPartnerAgencyRunning() const noexcept;

        // =====================================================================
        // Matchmaking Cooldown & Persistent Day Accessors (WorldStore backed)
        // =====================================================================

        void RecordNpcCooldown(RE::FormID npcFormID, float currentGameDay);
        [[nodiscard]] bool IsNpcOnCooldown(RE::FormID npcFormID, float currentGameDay) const;
        [[nodiscard]] int GetNpcCooldownRemaining(RE::FormID npcFormID, float currentGameDay) const;
        void ClearNpcCooldowns();

        [[nodiscard]] float GetLastMatchmakingDay() const;
        void SetLastMatchmakingDay(float day);

        [[nodiscard]] float GetLastPlayerPartnerCheatingDay() const;
        void SetLastPlayerPartnerCheatingDay(float day);

        // =====================================================================
        // NPC-NPC Banter Coordination
        // =====================================================================

        void TryScheduledBanter(float currentGameDay);
        [[nodiscard]] bool TryTriggerBanter();
        [[nodiscard]] std::vector<RE::Actor*> FindBanterCoupleCandidates(float scanRadius = 3000.0f) const;
        void RecordScenePlayed(RE::Actor* actorA, RE::Actor* actorB);

        [[nodiscard]] bool IsBanterSceneOnCooldown(float currentDay) const;
        [[nodiscard]] bool IsCoupleOnBanterCooldown(RE::FormID actorA, RE::FormID actorB, float currentDay) const;
        void SetBanterCooldownHours(float hours);
        [[nodiscard]] float GetBanterCooldownHours() const;

        // =====================================================================
        // Session Lifecycle
        // =====================================================================

        void Revert();

    private:
        AgencyWorkflowCoordinator() = default;
        ~AgencyWorkflowCoordinator() = default;

        struct MatchBatchOptions {
            float currentGameDay{ 0.0f };
            bool isGlobal{ false };
            bool isManual{ false };
            bool skipConfirmationModal{ false };
        };

        [[nodiscard]] bool ValidateCandidate(MatchmakingResult& result,
                                             const State::WorldStateSnapshot& snap,
                                             float currentGameDay) const;
        [[nodiscard]] bool ApplyMatch(MatchmakingResult& result, float currentGameDay);
        void ApplyMatchmakingResults(std::vector<MatchmakingResult> results, const MatchBatchOptions& opts);

        void StartVerificationBatch(std::vector<MatchmakingResult> candidates, const MatchBatchOptions& opts);
        void ProceedAfterVerification(std::vector<MatchmakingResult> results, const MatchBatchOptions& opts);
        void ShowMatchmakingReviewModal(std::vector<MatchmakingResult> results, const MatchBatchOptions& opts);
        void StartMemoryBatchOrApply(std::vector<MatchmakingResult> results, const MatchBatchOptions& opts);

        std::atomic<bool> runningMatchmaking_{ false };
        std::atomic<bool> runningPartnerAgency_{ false };
    };

}  // namespace MARAS::App
