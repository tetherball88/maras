#pragma once

#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace MARAS::Domain::Agency {

    using Foundation::ActorId;
    using Foundation::ActorSex;
    using Foundation::RelationshipStatus;

    /// Normalizes raw LLM/UI matchmaking action labels (`"create"`, `"new"`, `"advance"`, `"breakup"`,
    /// `"open"`, `"close"`, legacy `"open_close"`) using optional `targetStatusHint` and `openTargetHint`.
    [[nodiscard]] std::string NormalizeMatchmakingAction(
        std::string_view raw,
        std::string_view targetStatusHint = "",
        std::optional<bool> openTargetHint = std::nullopt);

    /// Pure check whether forming a `Lover` pair between `actorA` and `actorB` constitutes cheating
    /// (co-spouses of Player are exempt; an actor in an exclusive main relationship is cheating).
    [[nodiscard]] bool IsPairCheatingTransition(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB,
        std::string_view targetStatus) noexcept;

    struct CandidateValidationInput {
        ActorId actorA{};
        ActorId actorB{};
        std::string actorAName{};
        std::string actorBName{};
        std::string action{};
        std::string targetStatus{};
        float currentGameDay{ 0.0f };
        bool checkCooldown{ true };
        bool actorAEligibleAdult{ true };
        bool actorBEligibleAdult{ true };
        ActorSex actorASex{ ActorSex::Unknown };
        ActorSex actorBSex{ ActorSex::Unknown };
        bool areFamily{ false };
        std::string familyReason{};
        bool canForm{ true };
        std::string canFormReason{};
        bool canOpen{ false };
        bool canClose{ false };
    };

    struct CandidateValidationOutcome {
        bool valid{ false };
        std::string normalizedAction{};
        std::string normalizedTargetStatus{};
        bool isCheating{ false };
        std::string skipReason{};
    };

    /// Pure matchmaking candidate validation covering adult/player/self/cooldown guards,
    /// orientation & family checks on `new`/`advance` only (never blocking `breakup`/`open`/`close`),
    /// cheating gate, `canForm`/`canOpen`/`canClose`, and `GetBreakupTargetStatus`.
    [[nodiscard]] CandidateValidationOutcome ValidateMatchmakingCandidate(
        const State::WorldStateSnapshot& snap,
        const CandidateValidationInput& in);

    struct VerifyItemDecision {
        bool skipped{ false };
        std::string skipReason{};
        std::string memoryToApply{};
    };

    /// Pure per-item verification outcome reducer: rejects BOTH failed tasks (`!taskSuccess`)
    /// and unsuitable LLM verdicts (`!evalSuitable`), preserving existing non-empty memories.
    [[nodiscard]] VerifyItemDecision EvaluateVerificationBatchItem(
        bool preSkipped,
        bool taskSuccess,
        bool evalSuitable,
        std::string_view failureReason,
        std::string_view evalReason,
        std::string_view existingMemory);

    [[nodiscard]] bool ShouldShowMatchmakingReviewModal(
        bool isPartnerAgency,
        bool skipConfirmationModal,
        const State::SettingsData& settings) noexcept;

    [[nodiscard]] bool ShouldExposeMatchmakingCheatingBadge(
        bool isCheating,
        bool revealSecretsInUI) noexcept;

    struct ShortlistCandidateRef {
        ActorId actorId{};
        std::string name{};
        std::string connectionTag{};
    };

    struct ResolvedShortlistChoice {
        ActorId actorId{};
        std::string name{};
        std::string connectionTag{};
        bool matchedByIndex{ false };
        bool needsExternalNameLookup{ false };
    };

    /// Resolves Jev Decisions `chosenCandidateIndex` FIRST before falling back to case-insensitive
    /// exact/substring match against `shortlist`, skipping already-used candidates.
    [[nodiscard]] ResolvedShortlistChoice ResolvePartnerAgencyCandidate(
        std::int32_t chosenCandidateIndex,
        std::string_view chosenCandidateName,
        const std::vector<ShortlistCandidateRef>& shortlist,
        const std::unordered_set<ActorId>& usedCandidates);

}  // namespace MARAS::Domain::Agency
