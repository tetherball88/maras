#include "maras/app/DialogueOutcomeCoordinator.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/relationships/DialogueAndActionRules.h"
#include "maras/engine/FormCache.h"
#include "maras/foundation/StrongIds.h"

namespace MARAS::App {

    DialogueOutcomeCoordinator& DialogueOutcomeCoordinator::GetSingleton() {
        static DialogueOutcomeCoordinator instance;
        return instance;
    }

    std::int32_t DialogueOutcomeCoordinator::ApplyDialoguePromptBegin(
        RE::Actor* speaker,
        std::string_view promptKey) {
        if (!speaker || promptKey.empty()) {
            return 0;
        }

        const RE::FormID speakerId = speaker->GetFormID();
        auto& relSvc = RelationshipUseCases::GetSingleton();
        const auto currentStatus = relSvc.GetRelationshipStatus(speakerId, Foundation::kPlayerRefRawId);
        const auto outcome = Domain::Relationships::EvaluateDialogueTopicBeginOutcome(promptKey, currentStatus);

        if (outcome.registerCandidate) {
            (void)relSvc.RegisterAsCandidate(speakerId);
        }
        if (outcome.clearPartnerIntent) {
            relSvc.ClearPartnerIntent(speakerId);
        }
        if (outcome.setOpenBeforeAffection && outcome.setOpenRelationship.has_value()) {
            (void)relSvc.SetPairIsOpen(Foundation::kPlayerRefRawId, speakerId, *outcome.setOpenRelationship, true);
        }
        if (outcome.affectionDelta != 0.0f) {
            AffectionEventCoordinator::GetSingleton().AdjustPlayerPartnerAffection(speakerId, outcome.affectionDelta);
        }
        if (outcome.cheatingTarget == Domain::Relationships::DialogueCheatingTarget::SpeakerIsCheater) {
            (void)relSvc.SetCheatingDiscovered(speakerId, 0, outcome.cheatingStatusOrResolution, 0);
        } else if (outcome.cheatingTarget == Domain::Relationships::DialogueCheatingTarget::PlayerIsCheater) {
            (void)relSvc.SetCheatingDiscovered(Foundation::kPlayerRefRawId, 0, outcome.cheatingStatusOrResolution, 0);
        }
        if (!outcome.setOpenBeforeAffection && outcome.setOpenRelationship.has_value()) {
            (void)relSvc.SetPairIsOpen(Foundation::kPlayerRefRawId, speakerId, *outcome.setOpenRelationship, true);
        }
        if (outcome.breakupWithSpeaker) {
            (void)relSvc.Breakup(Foundation::kPlayerRefRawId, speakerId, true);
        }
        if (outcome.promoteSpeakerStatus.has_value()) {
            (void)relSvc.PromoteToStatusWith(speakerId, Foundation::kPlayerRefRawId, *outcome.promoteSpeakerStatus, true);
        }

        return outcome.PackPapyrusEffectCode();
    }

    std::int32_t DialogueOutcomeCoordinator::ApplyDialoguePromptEnd(
        RE::Actor* speaker,
        std::string_view promptKey) {
        if (!speaker || promptKey.empty()) {
            return 0;
        }

        const auto outcome = Domain::Relationships::EvaluateDialogueTopicEndOutcome(promptKey);
        if (outcome.breakupWithSpeaker) {
            (void)RelationshipUseCases::GetSingleton().Breakup(
                Foundation::kPlayerRefRawId, speaker->GetFormID(), true);
        }
        if (outcome.requiresCaughtFactionForSideEffect) {
            auto* caughtFaction = FormCache::GetSingleton().GetCheatingNpcCaughtFaction();
            if (!caughtFaction || !speaker->IsInFaction(caughtFaction)) {
                return 0;
            }
        }

        return static_cast<std::int32_t>(outcome.papyrusSideEffect);
    }

}  // namespace MARAS::App
