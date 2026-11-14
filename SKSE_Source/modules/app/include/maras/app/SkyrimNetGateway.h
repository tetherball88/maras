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
        virtual bool RequestActorProfileUpdate(std::uint32_t actorFormID,
                                               const std::string& additionalInstructions = "",
                                               const std::string& updatableBlocksCSV = "") = 0;
    };

}  // namespace MARAS::App
