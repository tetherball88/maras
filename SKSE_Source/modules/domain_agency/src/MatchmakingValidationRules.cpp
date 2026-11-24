#include "maras/domain/agency/MatchmakingValidationRules.h"

#include "maras/domain/agency/MatchmakingRules.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

#include <utility>

namespace MARAS::Domain::Agency {

    namespace {

        [[nodiscard]] bool HasAnyMainPartnerSnap(
            const State::WorldStateSnapshot& snap,
            ActorId actorId) noexcept {
            return snap.relationships && !snap.relationships->GetPrimaryPartner(actorId).IsNull();
        }

        [[nodiscard]] bool IsPlayerActiveMainPartnerSnap(
            const State::WorldStateSnapshot& snap,
            ActorId id) noexcept {
            if (!snap.relationships || id.IsNull() || id.IsPlayer()) {
                return false;
            }
            return Foundation::IsMainTierStatus(
                snap.relationships->GetStatus(ActorId::Player(), id));
        }

        [[nodiscard]] bool IsActorInExclusiveMainRelationshipSnap(
            const State::WorldStateSnapshot& snap,
            ActorId actorId) noexcept {
            if (!snap.relationships || actorId.IsNull()) {
                return false;
            }
            return HasAnyMainPartnerSnap(snap, actorId) &&
                   !snap.relationships->HasAnyOpenMainPair(actorId);
        }

    }  // namespace

    std::string NormalizeMatchmakingAction(
        std::string_view raw,
        std::string_view targetStatusHint,
        std::optional<bool> openTargetHint) {
        const std::string lower = Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(raw));
        if (lower == "openclose" || lower == "open_close" || lower == "open/close" || lower == "toggle_open") {
            if (openTargetHint.has_value()) {
                return *openTargetHint ? "open" : "close";
            }
            const std::string stLower =
                Foundation::StringUtils::ToLowerStd(Foundation::StringUtils::TrimView(targetStatusHint));
            if (stLower == "close" || stLower == "closed" || stLower == "exclusive") {
                return "close";
            }
            return "open";
        }
        if (lower.find("create") != std::string::npos || lower == "new") {
            return "new";
        }
        if (lower.find("advance") != std::string::npos) {
            return "advance";
        }
        if (lower.find("breakup") != std::string::npos || lower.find("break") != std::string::npos) {
            return "breakup";
        }
        if (lower == "open" || lower.find("open") != std::string::npos) {
            return "open";
        }
        if (lower == "close" || lower.find("close") != std::string::npos) {
            return "close";
        }
        return lower;
    }

    bool IsPairCheatingTransition(
        const State::WorldStateSnapshot& snap,
        ActorId actorA,
        ActorId actorB,
        std::string_view targetStatus) noexcept {
        if (!Foundation::StringUtils::CaseInsensitiveEquals(targetStatus, "lover") ||
            actorA.IsNull() || actorB.IsNull()) {
            return false;
        }
        if (IsPlayerActiveMainPartnerSnap(snap, actorA) && IsPlayerActiveMainPartnerSnap(snap, actorB)) {
            return false;
        }
        return IsActorInExclusiveMainRelationshipSnap(snap, actorA) ||
               IsActorInExclusiveMainRelationshipSnap(snap, actorB);
    }

    CandidateValidationOutcome ValidateMatchmakingCandidate(
        const State::WorldStateSnapshot& snap,
        const CandidateValidationInput& in) {
        CandidateValidationOutcome out{};
        out.normalizedAction = in.action;
        out.normalizedTargetStatus = in.targetStatus;

        auto reject = [&](std::string reason) -> CandidateValidationOutcome {
            out.valid = false;
            out.skipReason = std::move(reason);
            return out;
        };

        if (!snap.settings || !snap.relationships) {
            return reject("World state unavailable");
        }
        if (in.actorA.IsNull() || in.actorB.IsNull()) {
            return reject("Could not resolve FormIDs for names: " + in.actorAName + " / " + in.actorBName);
        }
        if (in.actorA == in.actorB) {
            return reject("Actor cannot be paired with themselves");
        }
        if (in.actorA.IsPlayer() || in.actorB.IsPlayer()) {
            return reject("Player character is excluded from automated matchmaking");
        }
        if (!in.actorAEligibleAdult || !in.actorBEligibleAdult) {
            return reject("Actor is deceased, child, or invalid");
        }

        if (in.checkCooldown) {
            if (IsNpcOnMatchmakingCooldown(snap, in.actorA, in.currentGameDay) ||
                IsNpcOnMatchmakingCooldown(snap, in.actorB, in.currentGameDay)) {
                return reject("One or both actors are on matchmaking cooldown");
            }
        }

        const std::string action = NormalizeMatchmakingAction(in.action, in.targetStatus);
        out.normalizedAction = action;

        if (action == "new" || action == "advance") {
            if (in.areFamily) {
                return reject(in.familyReason.empty() ? "Actors are family members" : in.familyReason);
            }
            if (!IsOrientationCompatible(snap.settings->matchmakingOrientation, in.actorASex, in.actorBSex)) {
                return reject("Pair does not match the configured matchmaking orientation setting");
            }

            RelationshipStatus targetSt = Foundation::StringToRelationshipStatus(in.targetStatus);
            if (action == "new" &&
                (targetSt == RelationshipStatus::Candidate || targetSt == RelationshipStatus::Unknown)) {
                targetSt = RelationshipStatus::Dating;
                out.normalizedTargetStatus = "dating";
            }
            if (targetSt == RelationshipStatus::Unknown || targetSt == RelationshipStatus::Candidate) {
                return reject("Unknown target relationship status");
            }

            const bool wouldCheat = IsPairCheatingTransition(
                snap,
                in.actorA,
                in.actorB,
                Foundation::StringUtils::ToLowerStd(Foundation::RelationshipStatusToString(targetSt)));
            if (wouldCheat && !snap.settings->matchmakingAllowCheating &&
                targetSt == RelationshipStatus::Lover) {
                return reject("Cheating is disabled in matchmaking settings");
            }

            if (!in.canForm) {
                return reject(in.canFormReason.empty() ? "Relationship transition not allowed" : in.canFormReason);
            }

            out.isCheating = wouldCheat;
            out.valid = true;
            return out;
        }

        if (action == "breakup") {
            if (!snap.settings->matchmakingAllowBreakups) {
                return reject("Breakups are disabled in matchmaking settings");
            }
            const auto curStatus = snap.relationships->GetStatus(in.actorA, in.actorB);
            const auto [targetStr, targetEnum] = GetBreakupTargetStatus(curStatus);
            if (targetEnum == RelationshipStatus::Unknown) {
                return reject("No active relationship to break up");
            }
            out.normalizedTargetStatus = std::string(targetStr);
            out.isCheating = false;
            out.valid = true;
            return out;
        }

        if (action == "open") {
            if (!snap.settings->matchmakingAllowOpenClose) {
                return reject("Open/close modifications are disabled in matchmaking settings");
            }
            if (!in.canOpen) {
                return reject("Pair is not eligible to be opened");
            }
            out.normalizedTargetStatus = "open";
            out.isCheating = false;
            out.valid = true;
            return out;
        }

        if (action == "close") {
            if (!snap.settings->matchmakingAllowOpenClose) {
                return reject("Open/close modifications are disabled in matchmaking settings");
            }
            if (!in.canClose) {
                return reject("Pair is not eligible to be closed");
            }
            out.normalizedTargetStatus = "closed";
            out.isCheating = false;
            out.valid = true;
            return out;
        }

        return reject("Unknown action: " + action);
    }

    VerifyItemDecision EvaluateVerificationBatchItem(
        bool preSkipped,
        bool taskSuccess,
        bool evalSuitable,
        std::string_view failureReason,
        std::string_view evalReason,
        std::string_view existingMemory) {
        VerifyItemDecision out{};
        if (preSkipped) {
            out.skipped = true;
            return out;
        }
        if (!taskSuccess || !evalSuitable) {
            out.skipped = true;
            if (!taskSuccess) {
                out.skipReason = failureReason.empty() ? std::string("Verification failed") : std::string(failureReason);
            } else {
                out.skipReason = evalReason.empty()
                    ? std::string("Rejected during narrative verification")
                    : std::string(evalReason);
            }
            return out;
        }
        out.skipped = false;
        if (existingMemory.empty() && !evalReason.empty() && evalReason != "No reason provided") {
            out.memoryToApply = std::string(evalReason);
        }
        return out;
    }

    bool ShouldShowMatchmakingReviewModal(
        bool isPartnerAgency,
        bool skipConfirmationModal,
        const State::SettingsData& settings) noexcept {
        if (skipConfirmationModal) {
            return false;
        }
        return isPartnerAgency ? settings.playerPartnerCheatingConfirmResults
                               : settings.matchmakingConfirmResults;
    }

    bool ShouldExposeMatchmakingCheatingBadge(
        bool isCheating,
        bool revealSecretsInUI) noexcept {
        return isCheating && revealSecretsInUI;
    }

    ResolvedShortlistChoice ResolvePartnerAgencyCandidate(
        std::int32_t chosenCandidateIndex,
        std::string_view chosenCandidateName,
        const std::vector<ShortlistCandidateRef>& shortlist,
        const std::unordered_set<ActorId>& usedCandidates) {
        ResolvedShortlistChoice out{};
        if (chosenCandidateIndex >= 0 &&
            static_cast<std::size_t>(chosenCandidateIndex) < shortlist.size()) {
            const auto& c = shortlist[static_cast<std::size_t>(chosenCandidateIndex)];
            if (!c.actorId.IsNull() && !usedCandidates.contains(c.actorId)) {
                out.actorId = c.actorId;
                out.name = c.name;
                out.connectionTag = c.connectionTag;
                out.matchedByIndex = true;
            }
            return out;
        }

        const std::string_view trimmed = Foundation::StringUtils::TrimView(chosenCandidateName);
        if (trimmed.empty()) {
            return out;
        }

        for (const auto& c : shortlist) {
            if (Foundation::StringUtils::CaseInsensitiveEquals(c.name, trimmed) ||
                Foundation::StringUtils::Contains(c.name, trimmed)) {
                if (!c.actorId.IsNull() && !usedCandidates.contains(c.actorId)) {
                    out.actorId = c.actorId;
                    out.name = c.name;
                    out.connectionTag = c.connectionTag;
                }
                return out;
            }
        }

        out.needsExternalNameLookup = true;
        return out;
    }

}  // namespace MARAS::Domain::Agency
