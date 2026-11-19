#include "maras/domain/relationships/DialogueAndActionRules.h"

#include "maras/foundation/StringUtils.h"

#include <array>
#include <format>

namespace MARAS::Domain::Relationships {

    namespace {

        namespace StringUtils = Foundation::StringUtils;
        using Foundation::RelationshipStatus;

        [[nodiscard]] std::optional<RelationshipStatus> MapActiveBreakupTarget(
            RelationshipStatus currentStatus) noexcept {
            switch (currentStatus) {
                case RelationshipStatus::Engaged:
                    return RelationshipStatus::Jilted;
                case RelationshipStatus::Married:
                    return RelationshipStatus::Divorced;
                case RelationshipStatus::Dating:
                    return RelationshipStatus::ExDating;
                case RelationshipStatus::Lover:
                    return RelationshipStatus::ExLover;
                default:
                    return std::nullopt;
            }
        }

        struct BeginRuleEntry {
            std::string_view promptKey{};
            DialogueTopicBeginOutcome outcome{};
            bool conditionalBreakup{ false };
        };

        constexpr BeginRuleEntry MakeOpenCloseRule(
            std::string_view promptKey,
            DialogueSpeechAction speech,
            float affectionDelta,
            std::optional<bool> setOpen,
            DialogueSceneAction scene = DialogueSceneAction::None,
            bool clearIntent = false,
            bool openBeforeAffection = false,
            bool conditionalBreakup = false) noexcept {
            DialogueTopicBeginOutcome out{};
            out.clearPartnerIntent = clearIntent;
            out.affectionDelta = affectionDelta;
            out.setOpenRelationship = setOpen;
            out.setOpenBeforeAffection = openBeforeAffection;
            out.speechAction = speech;
            out.sceneAction = scene;
            return BeginRuleEntry{ promptKey, out, conditionalBreakup };
        }

        constexpr BeginRuleEntry MakeCheatingRule(
            std::string_view promptKey,
            DialogueCheatingTarget cheatingTarget,
            std::string_view resolution,
            float affectionDelta = 0.0f,
            DialogueSpeechAction speech = DialogueSpeechAction::None,
            DialogueSceneAction scene = DialogueSceneAction::None,
            std::optional<bool> setOpen = std::nullopt,
            bool clearIntent = false) noexcept {
            DialogueTopicBeginOutcome out{};
            out.clearPartnerIntent = clearIntent;
            out.affectionDelta = affectionDelta;
            out.cheatingTarget = cheatingTarget;
            out.cheatingStatusOrResolution = resolution;
            out.setOpenRelationship = setOpen;
            out.speechAction = speech;
            out.sceneAction = scene;
            return BeginRuleEntry{ promptKey, out, false };
        }

        constexpr std::array kBeginRules = std::to_array<BeginRuleEntry>({
            // Registration & direct status promotions
            { "maras_enable_candidate",
              DialogueTopicBeginOutcome{ .registerCandidate = true },
              false },
            { "maras_start_dating",
              DialogueTopicBeginOutcome{ .promoteSpeakerStatus = RelationshipStatus::Dating },
              false },
            { "maras_no_ai_dating",
              DialogueTopicBeginOutcome{ .promoteSpeakerStatus = RelationshipStatus::Dating },
              false },
            { "maras_start_lover",
              DialogueTopicBeginOutcome{ .promoteSpeakerStatus = RelationshipStatus::Lover },
              false },
            { "maras_no_ai_lover",
              DialogueTopicBeginOutcome{ .promoteSpeakerStatus = RelationshipStatus::Lover },
              false },
            { "maras_end_premarital",
              DialogueTopicBeginOutcome{ .promoteSpeakerStatus = RelationshipStatus::Candidate },
              false },
            { "maras_no_ai_engagement",
              DialogueTopicBeginOutcome{ .promoteSpeakerStatus = RelationshipStatus::Engaged },
              false },
            { "maras_breakup_reconcile_accept",
              DialogueTopicBeginOutcome{ .promoteSpeakerStatus = RelationshipStatus::Engaged },
              false },
            { "maras_breakup", DialogueTopicBeginOutcome{}, true },
            { "maras_no_ai_cheating_breakup", DialogueTopicBeginOutcome{}, true },

            // No-AI cheating demand
            MakeCheatingRule("maras_no_ai_cheating_demand_success",
                             DialogueCheatingTarget::SpeakerIsCheater, "confronted"),
            MakeCheatingRule("maras_no_ai_cheating_demand_fail",
                             DialogueCheatingTarget::None, {}, -50.0f),
            MakeCheatingRule("maras_no_ai_cheating_demand_intimidate_success",
                             DialogueCheatingTarget::SpeakerIsCheater, "confronted", -20.0f,
                             DialogueSpeechAction::Intimidate),
            MakeCheatingRule("maras_no_ai_cheating_demand_intimidate_fail",
                             DialogueCheatingTarget::None, {}, -25.0f),
            MakeCheatingRule("maras_no_ai_cheating_demand_persuade_success",
                             DialogueCheatingTarget::SpeakerIsCheater, "confronted", 0.0f,
                             DialogueSpeechAction::Persuade),
            MakeCheatingRule("maras_no_ai_cheating_demand_persuade_fail",
                             DialogueCheatingTarget::None, {}, -15.0f),
            MakeCheatingRule("maras_no_ai_cheating_demand_or",
                             DialogueCheatingTarget::SpeakerIsCheater, "allowed", 0.0f,
                             DialogueSpeechAction::None, DialogueSceneAction::None, true),
            MakeCheatingRule("maras_no_ai_cheating_demand_giveup_continue",
                             DialogueCheatingTarget::SpeakerIsCheater, "allowed"),
            MakeCheatingRule("maras_no_ai_cheating_demand_giveup_stop",
                             DialogueCheatingTarget::SpeakerIsCheater, "confronted"),

            // No-AI cheating interrogation / insist
            MakeCheatingRule("maras_no_ai_cheating_insist_success",
                             DialogueCheatingTarget::SpeakerIsCheater, "confessed"),
            MakeCheatingRule("maras_no_ai_cheating_insist_fail",
                             DialogueCheatingTarget::None, {}, -5.0f),
            MakeCheatingRule("maras_no_ai_cheating_insist_no_cheating",
                             DialogueCheatingTarget::None, {}, -25.0f),
            MakeCheatingRule("maras_no_ai_cheating_intimidate_success",
                             DialogueCheatingTarget::SpeakerIsCheater, "confessed", -5.0f,
                             DialogueSpeechAction::Intimidate),
            MakeCheatingRule("maras_no_ai_cheating_intimidate_fail",
                             DialogueCheatingTarget::None, {}, -15.0f),
            MakeCheatingRule("maras_no_ai_cheating_intimidate_no_cheating",
                             DialogueCheatingTarget::None, {}, -40.0f),
            MakeCheatingRule("maras_no_ai_cheating_persuade_success",
                             DialogueCheatingTarget::SpeakerIsCheater, "confessed", 0.0f,
                             DialogueSpeechAction::Persuade),
            MakeCheatingRule("maras_no_ai_cheating_persuade_fail",
                             DialogueCheatingTarget::None, {}, -15.0f),
            MakeCheatingRule("maras_no_ai_cheating_persuade_no_cheating",
                             DialogueCheatingTarget::None, {}, -25.0f),

            // No-AI open relationship / close relationship negotiations
            MakeOpenCloseRule("maras_no_ai_or_clarification_success",
                              DialogueSpeechAction::None, 0.0f, true),
            MakeOpenCloseRule("maras_no_ai_or_intimidate_success",
                              DialogueSpeechAction::Intimidate, -10.0f, true),
            MakeOpenCloseRule("maras_no_ai_or_intimidate_fail",
                              DialogueSpeechAction::None, -25.0f, std::nullopt),
            MakeOpenCloseRule("maras_no_ai_or_persuade_success",
                              DialogueSpeechAction::Persuade, 0.0f, true),
            MakeOpenCloseRule("maras_no_ai_or_persuade_fail",
                              DialogueSpeechAction::None, -15.0f, std::nullopt),
            MakeOpenCloseRule("maras_no_ai_cr_clarification_success",
                              DialogueSpeechAction::None, 0.0f, false),
            MakeOpenCloseRule("maras_no_ai_cr_intimidate_success",
                              DialogueSpeechAction::Intimidate, -10.0f, false),
            MakeOpenCloseRule("maras_no_ai_cr_intimidate_fail",
                              DialogueSpeechAction::None, -35.0f, std::nullopt),
            MakeOpenCloseRule("maras_no_ai_cr_persuade_success",
                              DialogueSpeechAction::Persuade, 0.0f, false),
            MakeOpenCloseRule("maras_no_ai_cr_persuade_fail",
                              DialogueSpeechAction::None, -25.0f, std::nullopt),

            // Intimate walk-in / caught scenes
            MakeCheatingRule("maras_sex_walkin_cheating_sorry_yes",
                             DialogueCheatingTarget::PlayerIsCheater, "confronted", 0.0f,
                             DialogueSpeechAction::None,
                             DialogueSceneAction::FinishPlayerThreadAndFreeNoticeCheating),
            MakeCheatingRule("maras_sex_walkin_cheating_sorry_no",
                             DialogueCheatingTarget::PlayerIsCheater, "confronted", 0.0f,
                             DialogueSpeechAction::None,
                             DialogueSceneAction::FinishPlayerThreadAndFreeNoticeCheating),
            MakeCheatingRule("maras_sex_walkin_cheating_sorry_nothing",
                             DialogueCheatingTarget::PlayerIsCheater, "confronted", 0.0f,
                             DialogueSpeechAction::None,
                             DialogueSceneAction::FinishPlayerThreadAndFreeNoticeCheating),
            { "maras_sex_walkin_cheating_sorry_no_persuade_success",
              DialogueTopicBeginOutcome{ .speechAction = DialogueSpeechAction::Persuade },
              false },
            { "maras_sex_walkin_cheating_sorry_no_persuade_failure",
              DialogueTopicBeginOutcome{},
              false },
            { "maras_sex_walkin_cheating_join_yes",
              DialogueTopicBeginOutcome{ .sceneAction = DialogueSceneAction::FreeNoticeCheatingPlayer },
              false },
            { "maras_sex_walkin_cheating_join_no",
              DialogueTopicBeginOutcome{ .affectionDelta = -20.0f },
              false },
            { "maras_sex_walkin_cheating_join_no_persuade_success",
              DialogueTopicBeginOutcome{ .speechAction = DialogueSpeechAction::Persuade,
                                           .sceneAction = DialogueSceneAction::FreeNoticeCheatingPlayer },
              false },
            { "maras_sex_walkin_cheating_continue",
              DialogueTopicBeginOutcome{ .breakupWithSpeaker = true,
                                           .sceneAction = DialogueSceneAction::FreeNoticeCheatingPlayer },
              false },
            { "maras_sex_walkin_or_join_yes",
              DialogueTopicBeginOutcome{ .sceneAction = DialogueSceneAction::FreeNoticeOpenRelPlayer },
              false },
            { "maras_sex_walkin_or_join_no",
              DialogueTopicBeginOutcome{ .sceneAction = DialogueSceneAction::FreeNoticeOpenRelPlayer },
              false },
            { "maras_sex_walkin_or_join_no_persuade_success",
              DialogueTopicBeginOutcome{ .speechAction = DialogueSpeechAction::Persuade,
                                           .sceneAction = DialogueSceneAction::FreeNoticeOpenRelPlayer },
              false },
            { "maras_sex_walkin_or_join_no_persuade_failure",
              DialogueTopicBeginOutcome{ .affectionDelta = -10.0f,
                                           .sceneAction = DialogueSceneAction::FreeNoticeOpenRelPlayer },
              false },

            // Player caught NPC cheating
            MakeCheatingRule("maras_player_caught_cheater_join__persuade_success",
                             DialogueCheatingTarget::SpeakerIsCheater, "", 0.0f,
                             DialogueSpeechAction::Persuade),
            MakeCheatingRule("maras_player_caught_cheater_join_failure",
                             DialogueCheatingTarget::SpeakerIsCheater, "", -10.0f),
            MakeCheatingRule("maras_player_caught_cheater_join__persuade_failure",
                             DialogueCheatingTarget::SpeakerIsCheater, "", -10.0f),
            { "maras_no_ai_initiate_sex",
              DialogueTopicBeginOutcome{ .sceneAction = DialogueSceneAction::SetupQuickSexScene },
              false },

            // Partner suggests opening relationship
            MakeOpenCloseRule("maras_partner_suggest_open_agree",
                              DialogueSpeechAction::None, 10.0f, true,
                              DialogueSceneAction::FreeSuggestOpen, true, true),
            MakeOpenCloseRule("maras_partner_suggest_open_promise_agree",
                              DialogueSpeechAction::None, 10.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_promise_persuade_success",
                              DialogueSpeechAction::Persuade, 10.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_promise_persuade_fail",
                              DialogueSpeechAction::None, -15.0f, true,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_promise_intimidate_success",
                              DialogueSpeechAction::Intimidate, -15.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_promise_intimidate_fail",
                              DialogueSpeechAction::None, -30.0f, true,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_scold_agree",
                              DialogueSpeechAction::None, -10.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_scold_persuade_success",
                              DialogueSpeechAction::Persuade, 0.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_scold_persuade_fail",
                              DialogueSpeechAction::None, -15.0f, true,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_scold_intimidate_success",
                              DialogueSpeechAction::Intimidate, -25.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestOpen, true),
            MakeOpenCloseRule("maras_partner_suggest_open_scold_intimidate_fail",
                              DialogueSpeechAction::None, 0.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestOpen, true, false, true),

            // Partner suggests closing relationship
            MakeOpenCloseRule("maras_partner_suggest_close_agree",
                              DialogueSpeechAction::None, 10.0f, false,
                              DialogueSceneAction::FreeSuggestClose, true, true),
            MakeOpenCloseRule("maras_partner_suggest_close_refuse_agree",
                              DialogueSpeechAction::None, 0.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_refuse_persuade_success",
                              DialogueSpeechAction::Persuade, 10.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_refuse_persuade_fail",
                              DialogueSpeechAction::None, -15.0f, false,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_refuse_intimidate_success",
                              DialogueSpeechAction::Intimidate, -15.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_refuse_intimidate_fail",
                              DialogueSpeechAction::None, -30.0f, false,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_scold_agree",
                              DialogueSpeechAction::None, -10.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_scold_persuade_success",
                              DialogueSpeechAction::Persuade, 0.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_scold_persuade_fail",
                              DialogueSpeechAction::None, -15.0f, false,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_scold_intimidate_success",
                              DialogueSpeechAction::Intimidate, -25.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestClose, true),
            MakeOpenCloseRule("maras_partner_suggest_close_scold_intimidate_fail",
                              DialogueSpeechAction::None, 0.0f, std::nullopt,
                              DialogueSceneAction::FreeSuggestClose, true, false, true),

            // Partner confessions & player caught cheater misc
            MakeCheatingRule("maras_partner_confess_talking",
                             DialogueCheatingTarget::SpeakerIsCheater, "confessed", 0.0f,
                             DialogueSpeechAction::None, DialogueSceneAction::FreeConfessTalking,
                             std::nullopt, true),
            MakeCheatingRule("maras_partner_confess_physical",
                             DialogueCheatingTarget::SpeakerIsCheater, "confessed", 0.0f,
                             DialogueSpeechAction::None, DialogueSceneAction::FreeConfessPhysical,
                             std::nullopt, true),
            MakeCheatingRule("maras_player_caught_cheater_confront",
                             DialogueCheatingTarget::SpeakerIsCheater, "confessed"),
            MakeCheatingRule("maras_player_caught_cheater_join_success",
                             DialogueCheatingTarget::SpeakerIsCheater, ""),
            { "maras_non_marriage_management", DialogueTopicBeginOutcome{}, false },
        });

    }  // namespace

    ActorNameAliasKind ClassifyActorNameAlias(
        std::string_view rawName,
        std::string_view playerDisplayName) {
        const std::string_view trimmed = StringUtils::TrimView(rawName);
        if (trimmed.empty() || StringUtils::CaseInsensitiveEquals(trimmed, "none")) {
            return ActorNameAliasKind::NoneSentinel;
        }
        if (StringUtils::CaseInsensitiveEquals(trimmed, "player") ||
            StringUtils::CaseInsensitiveEquals(trimmed, "dragonborn") ||
            StringUtils::CaseInsensitiveEquals(trimmed, "the dragonborn") ||
            (!playerDisplayName.empty() && StringUtils::CaseInsensitiveEquals(trimmed, playerDisplayName))) {
            return ActorNameAliasKind::Player;
        }
        return ActorNameAliasKind::NamedNpc;
    }

    std::optional<RelationshipStatus> MapBreakupTargetToRequiredCurrentStatus(
        std::string_view breakupStatus) {
        const std::string_view trimmed = StringUtils::TrimView(breakupStatus);
        if (StringUtils::CaseInsensitiveEquals(trimmed, "exdating")) {
            return RelationshipStatus::Dating;
        }
        if (StringUtils::CaseInsensitiveEquals(trimmed, "jilted")) {
            return RelationshipStatus::Engaged;
        }
        if (StringUtils::CaseInsensitiveEquals(trimmed, "exlover")) {
            return RelationshipStatus::Lover;
        }
        if (StringUtils::CaseInsensitiveEquals(trimmed, "divorced")) {
            return RelationshipStatus::Married;
        }
        return std::nullopt;
    }

    std::string BuildNpcProposalNarration(
        std::string_view proposerName,
        std::string_view partnerName,
        std::string_view suggestedStatus) {
        std::string_view relationshipPhrase{};
        if (StringUtils::CaseInsensitiveEquals(suggestedStatus, "dating")) {
            relationshipPhrase = "a dating relationship";
        } else if (StringUtils::CaseInsensitiveEquals(suggestedStatus, "engaged")) {
            relationshipPhrase = "an engagement relationship";
        } else if (StringUtils::CaseInsensitiveEquals(suggestedStatus, "lover")) {
            relationshipPhrase = "a lover relationship";
        }
        return std::format(
            "{} proposes {} to {}. {} must answer and accept or reject the proposal.",
            proposerName,
            relationshipPhrase,
            partnerName,
            partnerName);
    }

    std::int32_t ParseDivorceResolutionQuestStage(std::string_view decision) {
        const std::string_view trimmed = StringUtils::TrimView(decision);
        if (StringUtils::CaseInsensitiveEquals(trimmed, "reconcile")) {
            return 100;
        }
        if (StringUtils::CaseInsensitiveEquals(trimmed, "divorce")) {
            return 200;
        }
        return 0;
    }

    DialogueTopicEndOutcome EvaluateDialogueTopicEndOutcome(
        std::string_view promptKey,
        bool isSpeakerFamilyWithPlayer) {
        DialogueTopicEndOutcome out{};
        if (StringUtils::CaseInsensitiveEquals(promptKey, "maras_sex_walkin_cheating_sorry_no_persuade_failure") ||
            StringUtils::CaseInsensitiveEquals(promptKey, "maras_sex_walkin_cheating_sorry_didnt_forgave_persuade_failure") ||
            StringUtils::CaseInsensitiveEquals(promptKey, "maras_sex_walkin_cheating_join_no_persuade_failure")) {
            out = DialogueTopicEndOutcome{ .breakupWithSpeaker = true };
        } else if (StringUtils::CaseInsensitiveEquals(promptKey, "maras_sex_walkin_cheating_join_yes") ||
                   StringUtils::CaseInsensitiveEquals(promptKey, "maras_sex_walkin_cheating_join_no_persuade_success") ||
                   StringUtils::CaseInsensitiveEquals(promptKey, "maras_sex_walkin_or_join_yes") ||
                   StringUtils::CaseInsensitiveEquals(promptKey, "maras_sex_walkin_or_join_no_persuade_success")) {
            out = DialogueTopicEndOutcome{
                .papyrusSideEffect = DialogueEndSideEffect::AddParticipantPlayerSpeaker
            };
        } else if (StringUtils::CaseInsensitiveEquals(promptKey, "maras_no_ai_cheating_demand_success") ||
                   StringUtils::CaseInsensitiveEquals(promptKey, "maras_no_ai_cheating_demand_intimidate_success") ||
                   StringUtils::CaseInsensitiveEquals(promptKey, "maras_no_ai_cheating_demand_persuade_success") ||
                   StringUtils::CaseInsensitiveEquals(promptKey, "maras_no_ai_cheating_demand_giveup_stop")) {
            out = DialogueTopicEndOutcome{
                .papyrusSideEffect = DialogueEndSideEffect::EndCaughtCheaterThread,
                .requiresCaughtFactionForSideEffect = true
            };
        } else if (StringUtils::CaseInsensitiveEquals(promptKey, "maras_player_caught_cheater_join__persuade_success") ||
                   StringUtils::CaseInsensitiveEquals(promptKey, "maras_player_caught_cheater_join_success")) {
            out = DialogueTopicEndOutcome{
                .papyrusSideEffect = DialogueEndSideEffect::AddParticipantSpeakerPlayer
            };
        }

        if (isSpeakerFamilyWithPlayer &&
            (out.papyrusSideEffect == DialogueEndSideEffect::AddParticipantPlayerSpeaker ||
             out.papyrusSideEffect == DialogueEndSideEffect::AddParticipantSpeakerPlayer)) {
            out.papyrusSideEffect = DialogueEndSideEffect::None;
        }
        return out;
    }

    DialogueTopicBeginOutcome EvaluateDialogueTopicBeginOutcome(
        std::string_view promptKey,
        RelationshipStatus currentSpeakerPlayerStatus,
        bool isSpeakerFamilyWithPlayer) {
        for (const auto& rule : kBeginRules) {
            if (StringUtils::CaseInsensitiveEquals(promptKey, rule.promptKey)) {
                DialogueTopicBeginOutcome out = rule.outcome;
                if (rule.conditionalBreakup) {
                    out.promoteSpeakerStatus = MapActiveBreakupTarget(currentSpeakerPlayerStatus);
                }
                if (isSpeakerFamilyWithPlayer) {
                    const bool isBlockedScene =
                        out.sceneAction == DialogueSceneAction::SetupQuickSexScene ||
                        ((out.sceneAction == DialogueSceneAction::FreeNoticeCheatingPlayer ||
                          out.sceneAction == DialogueSceneAction::FreeNoticeOpenRelPlayer) &&
                         !out.breakupWithSpeaker);
                    const bool isBlockedStatus =
                        out.promoteSpeakerStatus.has_value() &&
                        Foundation::IsActivePartnerStatus(*out.promoteSpeakerStatus);
                    if (out.registerCandidate || isBlockedScene || isBlockedStatus) {
                        return {};
                    }
                }
                return out;
            }
        }
        return {};
    }

}  // namespace MARAS::Domain::Relationships
