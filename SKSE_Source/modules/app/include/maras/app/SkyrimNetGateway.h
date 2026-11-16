#pragma once

#include "RE/Skyrim.h"
#include "maras/app/ModalTypes.h"
#include "maras/foundation/DomainEnums.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::App {

    struct StepInEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        bool steppedIn{ false };
        double probability{ 0.0 };
        std::string reason;
    };

    struct PaternityEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        std::string outcome;
        std::string partnerReaction;
        double outcomeProbability{ 0.0 };
        double reactionProbability{ 0.0 };
        std::string summary;
    };

    struct PartnerAgencyEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        bool willCheat{ false };
        bool willBreakup{ false };
        bool willConfess{ false };
        bool willSuggest{ false };
        int chosenCandidateIndex{ -1 };
        std::string chosenCandidateName;
        std::string reasoning;
    };

    struct MatchmakingVerifyEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        bool suitable{ false };
        double probA{ 0.0 };
        double probB{ 0.0 };
        std::string reason;
    };

    class ISkyrimNetGateway {
    public:
        virtual ~ISkyrimNetGateway() = default;

        virtual bool IsAvailable() const = 0;
        virtual bool ShouldUseDecisions() const = 0;
        virtual bool ShouldTriggerNarration(RE::Actor* speaker, RE::Actor* target = nullptr) const = 0;
        virtual bool RegisterEvent(const std::string& eventType,
                                   const std::string& content,
                                   std::uint32_t originatorFormID = 0,
                                   std::uint32_t targetFormID = 0,
                                   bool exactAudience = false,
                                   const std::vector<std::uint32_t>& audienceFormIDs = {}) = 0;
        virtual void RegisterDecorators() = 0;
        virtual bool EvaluatePaternitySecret(std::uint32_t childID) = 0;

        virtual bool SendCustomPromptToLLM(const std::string& promptName,
                                           const std::string& variant,
                                           const std::string& contextJson,
                                           std::function<void(const char* response, int success)> callback) = 0;
        virtual bool EvaluateStepIn(const std::string& contextJson,
                                    std::function<void(const StepInEvalResult&)> callback) = 0;
        virtual bool EvaluatePaternity(const std::string& contextJson,
                                       bool isSecretForPresumedFather,
                                       std::function<void(const PaternityEvalResult&)> callback) = 0;
        virtual bool EvaluatePartnerAgency(const std::string& contextJson,
                                           const std::string& evalMode,
                                           bool canConfess,
                                           const std::string& partnerName,
                                           const std::string& sideLoverName,
                                           const std::string& affairStage,
                                           std::function<void(const PartnerAgencyEvalResult&)> callback) = 0;
        virtual bool EvaluateMatchmakingVerify(const std::string& contextJson,
                                               const std::string& action,
                                               const std::string& targetStatus,
                                               const std::string& actorAName,
                                               const std::string& actorBName,
                                               std::function<void(const MatchmakingVerifyEvalResult&)> callback) = 0;

        virtual std::string SanitizeSingleLine(std::string_view text) const = 0;
        virtual std::string SanitizeKey(std::string_view rawKey) const = 0;
        virtual std::string GetLLMFailureGuidanceTitle(LLMFailureType failureType) const = 0;
        virtual std::string GetLLMFailureGuidance(LLMFailureType failureType,
                                                  std::string_view llmVariant = "gamemaster_evaluation") const = 0;
        virtual std::string GetLLMBatchFailureGuidance(bool hasRequestFailure,
                                                       bool hasInvalidJson,
                                                       std::string_view llmVariant = "gamemaster_evaluation") const = 0;

        // Memory episode tracking and suppress scopes
        virtual void TickMemories() = 0;
        virtual void RevertMemories() = 0;
        virtual bool IsMemoriesSuppressed() const = 0;
        virtual void PushSuppressMemories() = 0;
        virtual void PopSuppressMemories() = 0;
        virtual bool IsMatchmakingApplyActive() const = 0;
        virtual void PushMatchmakingApply() = 0;
        virtual void PopMatchmakingApply() = 0;

        virtual void RecordRelationshipStatusChange(std::uint32_t initiatorID,
                                                    std::uint32_t partnerID,
                                                    Foundation::RelationshipStatus oldStatus,
                                                    Foundation::RelationshipStatus newStatus) = 0;
        virtual void RecordOpenRelationshipToggle(std::uint32_t actorA,
                                                  std::uint32_t actorB,
                                                  bool isOpen) = 0;
        virtual void RecordInfidelityEvent(std::uint32_t cheaterID,
                                           std::uint32_t victimID,
                                           std::uint32_t affairPartnerID,
                                           const std::string& eventSubType,
                                           bool isWalkIn = false) = 0;
        virtual void RecordPregnancyDiscovered(std::uint32_t motherID,
                                               std::uint32_t partnerID = 0,
                                               std::uint32_t fallbackBioID = 0,
                                               const std::string& fallbackBioName = "",
                                               std::int32_t childCount = 1) = 0;
        virtual void RecordPaternityReveal(std::uint32_t childID,
                                           const std::string& outcome,
                                           const std::string& partnerReaction = "none",
                                           bool wasPregnancySecretForPartner = false) = 0;
        virtual void RecordChildBirth(std::uint32_t motherID,
                                      const std::vector<std::uint32_t>& childIDs) = 0;
        virtual void RecordChildCustodyEvent(const std::string& eventType,
                                             std::uint32_t childID,
                                             std::uint32_t guardianAID,
                                             std::uint32_t guardianBID,
                                             std::uint32_t otherID,
                                             const std::string& reason) = 0;
        virtual void PreviewRelationshipMemories(std::uint32_t actorA,
                                                 std::uint32_t actorB,
                                                 const std::string& targetStatus,
                                                 const std::string& action) = 0;
        virtual void IngestMemories(const std::vector<GeneratedMemoryEntry>& memories,
                                    const std::vector<std::uint32_t>& fallbackRelatedActors = {}) = 0;

        // SkyrimNet profile updates and suppress scopes
        virtual void RevertProfiles() = 0;
        virtual bool AreProfilesSuppressed() const = 0;
        virtual void PushSuppressProfiles() = 0;
        virtual void PopSuppressProfiles() = 0;
        virtual void MarkActorStageUpdated(std::uint32_t childID, Foundation::ChildStage stage) = 0;
        virtual void RequestChildProfileUpdate(std::uint32_t childID, Foundation::ChildStage stage) = 0;
        virtual std::string GetActorBioTemplateName(std::uint32_t actorFormID) const = 0;
        virtual std::uint64_t GetActorUUID(std::uint32_t actorFormID) const = 0;
        virtual bool IsActorSuccessionAvailable() const = 0;
        virtual std::uint64_t ResolvePlacedActorUUID(std::uint32_t actorFormID) const = 0;
        virtual bool RecordActorSuccession(std::uint32_t fromFormID, std::uint32_t toFormID) = 0;
        virtual bool RecordActorSuccessionByUUID(std::uint64_t fromUuid, std::uint64_t toUuid) = 0;
        virtual bool RequestActorProfileUpdate(std::uint32_t actorFormID,
                                               const std::string& additionalInstructions = "",
                                               const std::string& updatableBlocksCSV = "") = 0;

        virtual void RevertEvents() noexcept = 0;
        virtual void SuppressNextActionNarration(float durationSeconds = 5.0f) noexcept = 0;
        [[nodiscard]] virtual bool IsActionNarrationSuppressed() const noexcept = 0;

        virtual void EmitDirectNarration(
            std::string_view content,
            std::uint32_t originatorFormId = 0,
            std::uint32_t targetFormId = 0,
            bool exactAudience = false,
            const std::vector<std::uint32_t>& audienceFormIds = {}) = 0;
        virtual void EmitPolygamyBlessing(std::uint32_t spouseFormId) = 0;
        virtual void EmitAffectionGift(std::string_view msg, std::uint32_t receiverFormId) = 0;
        virtual void EmitMaritalStatusChange(
            std::uint32_t initiatorFormId,
            std::uint32_t acceptorFormId,
            std::string_view newStatus,
            bool forceSkipTrigger = false) = 0;
        virtual void EmitOpenRelationshipChange(
            std::uint32_t initiatorFormId,
            std::uint32_t partnerFormId,
            bool isOpen,
            bool forceSkipTrigger = false) = 0;
        virtual void EmitSpouseHierarchyChange(std::uint32_t spouseFormId, bool isDemoted) = 0;
        virtual void EmitSpouseAffectionChange(
            std::uint32_t spouseFormId,
            std::string_view affectionLevel,
            std::string_view direction) = 0;
        virtual void EmitProposalDecision(
            std::uint32_t initiatorFormId,
            std::uint32_t acceptorFormId,
            std::string_view status,
            std::string_view decision,
            bool forceSkipTrigger = true) = 0;
        virtual void EmitActionDecline(
            std::uint32_t speakerFormId,
            std::uint32_t targetFormId,
            std::uint32_t initiatorFormId,
            std::uint32_t acceptorFormId,
            std::string_view declinedAction,
            std::string_view msg,
            bool skipTrigger = true) = 0;
        virtual void EmitChildAdopted(
            std::uint32_t childActorId,
            std::uint32_t parentAId,
            std::uint32_t parentBId = 0) = 0;
        virtual void EmitChildInherited(
            std::uint32_t childActorId,
            std::uint32_t deceasedParentId,
            std::uint32_t newParentId,
            std::uint32_t coParentId = 0) = 0;
        virtual void EmitChildParentsChanged(
            std::uint32_t childActorId,
            std::uint32_t parentAId,
            std::uint32_t parentBId = 0,
            std::string_view reason = "marriage") = 0;
        virtual void EmitPregnancyAnnounced(
            std::uint32_t motherId,
            std::uint32_t partnerId = 0,
            std::uint32_t fallbackBioId = 0,
            std::string_view fallbackBioName = "",
            std::int32_t childCount = 1) = 0;
        virtual void EmitPregnancyLost(
            std::uint32_t motherId,
            std::uint32_t partnerId = 0,
            std::string_view reason = "miscarriage") = 0;
        virtual void EmitLaborStarted(
            std::uint32_t motherId,
            std::uint32_t partnerId = 0,
            std::int32_t childCount = 1) = 0;
        virtual void EmitLaborProgress(
            std::uint32_t motherId,
            std::int32_t bornCount,
            std::int32_t totalCount) = 0;
        virtual void EmitChildBirthBatch(
            std::uint32_t motherId,
            const std::vector<std::uint32_t>& childIds = {}) = 0;
        virtual void EmitChildBirth(
            std::uint32_t childId,
            std::string_view childName = "") = 0;
        virtual void EmitChildGrewUp(
            std::uint32_t childId,
            std::string_view childName = "") = 0;
        virtual void EmitChildSentToTraining(
            std::uint32_t childId,
            std::string_view childName = "",
            std::string_view trainingLocation = "") = 0;
        virtual void EmitChildBecameAdult(
            std::uint32_t childId,
            std::string_view childName = "") = 0;
        virtual void EmitPaternitySecretResolved(
            std::uint32_t childId,
            std::string_view rawCaseType) = 0;
        virtual void EmitAffairConfessed(
            std::uint32_t cheaterId,
            std::uint32_t victimId,
            std::uint32_t affairPartnerId = 0) = 0;
        virtual void EmitAffairExposed(
            std::uint32_t affairPartnerId,
            std::uint32_t victimId,
            std::uint32_t cheaterId = 0) = 0;
        virtual void EmitAffairResolved(
            std::uint32_t initiatorId,
            std::uint32_t otherPartyId,
            std::uint32_t affairPartnerId,
            std::string_view resolution) = 0;
        virtual void EmitCheatingCaught(
            std::uint32_t victimId,
            std::uint32_t cheaterId,
            std::uint32_t affairPartnerId,
            bool isWalkIn) = 0;
        virtual void EmitOpenRelationshipWalkIn(
            std::uint32_t witnessId,
            std::uint32_t partnerId,
            std::uint32_t sexPartnerId,
            bool isWalkIn) = 0;
    };

}  // namespace MARAS::App
