#pragma once

#include "maras/foundation/DomainEnums.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace MARAS::Domain::Relationships {

    enum class ActorNameAliasKind : std::uint8_t {
        NoneSentinel = 0,
        Player = 1,
        NamedNpc = 2,
    };

    enum class DialogueEndSideEffect : std::int32_t {
        None = 0,
        AddParticipantPlayerSpeaker = 1,
        AddParticipantSpeakerPlayer = 2,
        EndCaughtCheaterThread = 3,
    };

    struct DialogueTopicEndOutcome {
        bool breakupWithSpeaker{ false };
        DialogueEndSideEffect papyrusSideEffect{ DialogueEndSideEffect::None };
        bool requiresCaughtFactionForSideEffect{ false };
    };

    enum class DialogueSpeechAction : std::int32_t {
        None = 0,
        Persuade = 1,
        Intimidate = 2,
    };

    enum class DialogueSceneAction : std::int32_t {
        None = 0,
        FreeNoticeCheatingPlayer = 1,
        FreeNoticeOpenRelPlayer = 2,
        FreeSuggestOpen = 3,
        FreeSuggestClose = 4,
        FreeConfessTalking = 5,
        FreeConfessPhysical = 6,
        FinishPlayerThreadAndFreeNoticeCheating = 7,
        SetupQuickSexScene = 8,
    };

    enum class DialogueCheatingTarget : std::uint8_t {
        None = 0,
        SpeakerIsCheater = 1,
        PlayerIsCheater = 2,
    };

    struct DialogueTopicBeginOutcome {
        bool registerCandidate{ false };
        bool clearPartnerIntent{ false };
        bool breakupWithSpeaker{ false };
        std::optional<Foundation::RelationshipStatus> promoteSpeakerStatus{};
        float affectionDelta{ 0.0f };
        DialogueCheatingTarget cheatingTarget{ DialogueCheatingTarget::None };
        std::string_view cheatingStatusOrResolution{};
        std::optional<bool> setOpenRelationship{};
        bool setOpenBeforeAffection{ false };
        DialogueSpeechAction speechAction{ DialogueSpeechAction::None };
        DialogueSceneAction sceneAction{ DialogueSceneAction::None };

        /// Packs speech (hundreds digit) and scene action (lower 2 digits) for Papyrus dispatch.
        [[nodiscard]] constexpr std::int32_t PackPapyrusEffectCode() const noexcept {
            return static_cast<std::int32_t>(speechAction) * 100 +
                   static_cast<std::int32_t>(sceneAction);
        }
    };

    /// Classifies an actor lookup string as empty/none sentinel, player alias, or named NPC query.
    [[nodiscard]] ActorNameAliasKind ClassifyActorNameAlias(
        std::string_view rawName,
        std::string_view playerDisplayName);

    /// Maps a breakup target status name ("exdating", "jilted", "exlover", "divorced") to the required current status.
    [[nodiscard]] std::optional<Foundation::RelationshipStatus> MapBreakupTargetToRequiredCurrentStatus(
        std::string_view breakupStatus);

    /// Formats the SkyrimNet direct narration line used when one NPC proposes to another NPC.
    [[nodiscard]] std::string BuildNpcProposalNarration(
        std::string_view proposerName,
        std::string_view partnerName,
        std::string_view suggestedStatus);

    /// Maps a SkyrimNet divorce resolution decision ("reconcile" -> 100, "divorce" -> 200, other -> 0).
    [[nodiscard]] std::int32_t ParseDivorceResolutionQuestStage(std::string_view decision);

    /// Evaluates the pure outcome table for `TTM_DialogueManagerMisc` Fragment_2 (OnEnd).
    [[nodiscard]] DialogueTopicEndOutcome EvaluateDialogueTopicEndOutcome(std::string_view promptKey);

    /// Evaluates the pure outcome table for `TTM_DialogueManagerMisc` Fragment_0 (OnBegin).
    [[nodiscard]] DialogueTopicBeginOutcome EvaluateDialogueTopicBeginOutcome(
        std::string_view promptKey,
        Foundation::RelationshipStatus currentSpeakerPlayerStatus);

}  // namespace MARAS::Domain::Relationships
