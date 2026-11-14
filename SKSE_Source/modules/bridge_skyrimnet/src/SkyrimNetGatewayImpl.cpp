#include "maras/bridge_skyrimnet/SkyrimNetGatewayImpl.h"

#include "maras/bridge_skyrimnet/SkyrimNetApiLoader.h"
#include "maras/bridge_skyrimnet/SkyrimNetDecisions.h"
#include "maras/bridge_skyrimnet/SkyrimNetDecorators.h"
#include "maras/bridge_skyrimnet/SkyrimNetMemoryService.h"
#include "maras/bridge_skyrimnet/SkyrimNetProfileService.h"

namespace MARAS::Bridge::SkyrimNet {

    namespace {

        class SkyrimNetGatewayImpl final : public MARAS::App::ISkyrimNetGateway {
        public:
            bool IsAvailable() const override {
                return MARAS::Bridge::SkyrimNet::IsSkyrimNetAvailable();
            }

            bool ShouldUseDecisions() const override {
                return MARAS::Bridge::SkyrimNet::Decisions::ShouldUseDecisions();
            }

            bool ShouldTriggerNarration(RE::Actor* speaker, RE::Actor* target = nullptr) const override {
                return MARAS::Bridge::SkyrimNet::ShouldTriggerSkyrimNetNarration(speaker, target);
            }

            bool RegisterEvent(const std::string& eventType,
                               const std::string& content,
                               std::uint32_t originatorFormID = 0,
                               std::uint32_t targetFormID = 0,
                               bool exactAudience = false,
                               const std::vector<std::uint32_t>& audienceFormIDs = {}) override {
                return MARAS::Bridge::SkyrimNet::RegisterSkyrimNetEvent(
                    eventType, content, originatorFormID, targetFormID, exactAudience, audienceFormIDs);
            }

            void RegisterDecorators() override {
                MARAS::Bridge::SkyrimNet::RegisterSkyrimNetDecorators();
            }

            bool EvaluatePaternitySecret(std::uint32_t childID) override {
                return MARAS::Bridge::SkyrimNet::EvaluatePaternitySecret(childID);
            }

            bool SendCustomPromptToLLM(const std::string& promptName,
                                       const std::string& variant,
                                       const std::string& contextJson,
                                       std::function<void(const char* response, int success)> callback) override {
                return MARAS::Bridge::SkyrimNet::SendCustomPromptToLLM(
                    promptName, variant, contextJson, std::move(callback));
            }

            bool EvaluateStepIn(const std::string& contextJson,
                                std::function<void(const MARAS::App::StepInEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluateStepIn(
                    contextJson, std::move(callback));
            }

            bool EvaluatePaternity(const std::string& contextJson,
                                   bool isSecretForPresumedFather,
                                   std::function<void(const MARAS::App::PaternityEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluatePaternity(
                    contextJson, isSecretForPresumedFather, std::move(callback));
            }

            bool EvaluatePartnerAgency(const std::string& contextJson,
                                       const std::string& evalMode,
                                       bool canConfess,
                                       const std::string& partnerName,
                                       const std::string& sideLoverName,
                                       const std::string& affairStage,
                                       std::function<void(const MARAS::App::PartnerAgencyEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluatePartnerAgency(
                    contextJson, evalMode, canConfess, partnerName, sideLoverName, affairStage, std::move(callback));
            }

            bool EvaluateMatchmakingVerify(
                const std::string& contextJson,
                const std::string& action,
                const std::string& targetStatus,
                const std::string& actorAName,
                const std::string& actorBName,
                std::function<void(const MARAS::App::MatchmakingVerifyEvalResult&)> callback) override {
                return MARAS::Bridge::SkyrimNet::Decisions::EvaluateMatchmakingVerify(
                    contextJson, action, targetStatus, actorAName, actorBName, std::move(callback));
            }

            std::string SanitizeSingleLine(std::string_view text) const override {
                return MARAS::Bridge::SkyrimNet::Decisions::SanitizeSingleLine(text);
            }

            std::string SanitizeKey(std::string_view rawKey) const override {
                return MARAS::Bridge::SkyrimNet::Decisions::SanitizeKey(rawKey);
            }

            std::string GetLLMFailureGuidanceTitle(MARAS::App::LLMFailureType failureType) const override {
                return MARAS::Bridge::SkyrimNet::GetLLMFailureGuidanceTitle(failureType);
            }

            std::string GetLLMFailureGuidance(
                MARAS::App::LLMFailureType failureType,
                std::string_view llmVariant = "gamemaster_evaluation") const override {
                return MARAS::Bridge::SkyrimNet::GetLLMFailureGuidance(failureType, llmVariant);
            }

            std::string GetLLMBatchFailureGuidance(
                bool hasRequestFailure,
                bool hasInvalidJson,
                std::string_view llmVariant = "gamemaster_evaluation") const override {
                return MARAS::Bridge::SkyrimNet::GetLLMBatchFailureGuidance(
                    hasRequestFailure, hasInvalidJson, llmVariant);
            }

            void TickMemories() override {
                SkyrimNetMemoryService::GetSingleton().Tick();
            }

            void RevertMemories() override {
                SkyrimNetMemoryService::GetSingleton().Revert();
            }

            bool IsMemoriesSuppressed() const override {
                return SkyrimNetMemoryService::IsSuppressed();
            }

            void PushSuppressMemories() override {
                SkyrimNetMemoryService::PushSuppress();
            }

            void PopSuppressMemories() override {
                SkyrimNetMemoryService::PopSuppress();
            }

            bool IsMatchmakingApplyActive() const override {
                return SkyrimNetMemoryService::IsMatchmakingApplyActive();
            }

            void PushMatchmakingApply() override {
                SkyrimNetMemoryService::PushMatchmakingApply();
            }

            void PopMatchmakingApply() override {
                SkyrimNetMemoryService::PopMatchmakingApply();
            }

            void RecordRelationshipStatusChange(std::uint32_t initiatorID,
                                                std::uint32_t partnerID,
                                                MARAS::Foundation::RelationshipStatus oldStatus,
                                                MARAS::Foundation::RelationshipStatus newStatus) override {
                SkyrimNetMemoryService::GetSingleton().RecordRelationshipStatusChange(
                    initiatorID, partnerID, oldStatus, newStatus);
            }

            void RecordOpenRelationshipToggle(std::uint32_t actorA,
                                              std::uint32_t actorB,
                                              bool isOpen) override {
                SkyrimNetMemoryService::GetSingleton().RecordOpenRelationshipToggle(actorA, actorB, isOpen);
            }

            void RecordInfidelityEvent(std::uint32_t cheaterID,
                                       std::uint32_t victimID,
                                       std::uint32_t affairPartnerID,
                                       const std::string& eventSubType,
                                       bool isWalkIn = false) override {
                SkyrimNetMemoryService::GetSingleton().RecordInfidelityEvent(
                    cheaterID, victimID, affairPartnerID, eventSubType, isWalkIn);
            }

            void RecordPregnancyDiscovered(std::uint32_t motherID,
                                           std::uint32_t partnerID = 0,
                                           std::uint32_t fallbackBioID = 0,
                                           const std::string& fallbackBioName = "",
                                           std::int32_t childCount = 1) override {
                SkyrimNetMemoryService::GetSingleton().RecordPregnancyDiscovered(
                    motherID, partnerID, fallbackBioID, fallbackBioName, childCount);
            }

            void RecordPaternityReveal(std::uint32_t childID,
                                       const std::string& outcome,
                                       const std::string& partnerReaction = "none",
                                       bool wasPregnancySecretForPartner = false) override {
                SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(
                    childID, outcome, partnerReaction, wasPregnancySecretForPartner);
            }

            void RecordChildBirth(std::uint32_t motherID,
                                  const std::vector<std::uint32_t>& childIDs) override {
                SkyrimNetMemoryService::GetSingleton().RecordChildBirth(motherID, childIDs);
            }

            void RecordChildCustodyEvent(const std::string& eventType,
                                         std::uint32_t childID,
                                         std::uint32_t guardianAID,
                                         std::uint32_t guardianBID,
                                         std::uint32_t otherID,
                                         const std::string& reason) override {
                SkyrimNetMemoryService::GetSingleton().RecordChildCustodyEvent(
                    eventType, childID, guardianAID, guardianBID, otherID, reason);
            }

            void PreviewRelationshipMemories(std::uint32_t actorA,
                                             std::uint32_t actorB,
                                             const std::string& targetStatus,
                                             const std::string& action) override {
                SkyrimNetMemoryService::GetSingleton().PreviewRelationshipMemories(
                    actorA, actorB, targetStatus, action);
            }

            void IngestMemories(const std::vector<MARAS::App::GeneratedMemoryEntry>& memories,
                                const std::vector<std::uint32_t>& fallbackRelatedActors = {}) override {
                SkyrimNetMemoryService::GetSingleton().IngestMemories(memories, fallbackRelatedActors);
            }

            void RevertProfiles() override {
                SkyrimNetProfileService::GetSingleton().Revert();
            }

            bool AreProfilesSuppressed() const override {
                return SkyrimNetProfileService::IsSuppressed();
            }

            void PushSuppressProfiles() override {
                SkyrimNetProfileService::PushSuppress();
            }

            void PopSuppressProfiles() override {
                SkyrimNetProfileService::PopSuppress();
            }

            void MarkActorStageUpdated(std::uint32_t childID,
                                       MARAS::Foundation::ChildStage stage) override {
                SkyrimNetProfileService::GetSingleton().MarkActorStageUpdated(childID, stage);
            }

            void RequestChildProfileUpdate(std::uint32_t childID,
                                           MARAS::Foundation::ChildStage stage) override {
                SkyrimNetProfileService::GetSingleton().RequestChildProfileUpdate(childID, stage);
            }

            std::string GetActorBioTemplateName(std::uint32_t actorFormID) const override {
                return SkyrimNetProfileService::GetSingleton().GetActorBioTemplateName(actorFormID);
            }

            std::uint64_t GetActorUUID(std::uint32_t actorFormID) const override {
                return MARAS::Bridge::SkyrimNet::GetActorUUID(actorFormID);
            }

            bool RequestActorProfileUpdate(std::uint32_t actorFormID,
                                           const std::string& additionalInstructions = "",
                                           const std::string& updatableBlocksCSV = "") override {
                return SkyrimNetProfileService::GetSingleton().RequestActorProfileUpdate(
                    actorFormID, additionalInstructions, updatableBlocksCSV);
            }
        };

    }  // namespace

    MARAS::App::ISkyrimNetGateway& GetSkyrimNetGatewayImpl() {
        static SkyrimNetGatewayImpl impl;
        return impl;
    }

}  // namespace MARAS::Bridge::SkyrimNet
