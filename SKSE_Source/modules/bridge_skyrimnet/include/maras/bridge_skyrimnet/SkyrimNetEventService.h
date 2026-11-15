#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Bridge::SkyrimNet {

    class SkyrimNetEventService {
    public:
        static SkyrimNetEventService& GetSingleton() {
            static SkyrimNetEventService instance;
            return instance;
        }

        SkyrimNetEventService(const SkyrimNetEventService&) = delete;
        SkyrimNetEventService& operator=(const SkyrimNetEventService&) = delete;

        void SuppressNextActionNarration(float durationSeconds = 5.0f) noexcept;
        [[nodiscard]] bool IsActionNarrationSuppressed() const noexcept;
        void Revert() noexcept;

        void EmitDirectNarration(
            std::string_view content,
            std::uint32_t originatorFormId = 0,
            std::uint32_t targetFormId = 0,
            bool exactAudience = false,
            const std::vector<std::uint32_t>& audienceFormIds = {});

        void EmitPolygamyBlessing(std::uint32_t spouseFormId);
        void EmitAffectionGift(std::string_view msg, std::uint32_t receiverFormId);

        void EmitMaritalStatusChange(
            std::uint32_t initiatorFormId,
            std::uint32_t acceptorFormId,
            std::string_view newStatus,
            bool forceSkipTrigger = false);

        void EmitOpenRelationshipChange(
            std::uint32_t initiatorFormId,
            std::uint32_t partnerFormId,
            bool isOpen,
            bool forceSkipTrigger = false);

        void EmitSpouseHierarchyChange(std::uint32_t spouseFormId, bool isDemoted);

        void EmitSpouseAffectionChange(
            std::uint32_t spouseFormId,
            std::string_view affectionLevel,
            std::string_view direction);

        void EmitProposalDecision(
            std::uint32_t initiatorFormId,
            std::uint32_t acceptorFormId,
            std::string_view status,
            std::string_view decision,
            bool forceSkipTrigger = true);

        void EmitActionDecline(
            std::uint32_t speakerFormId,
            std::uint32_t targetFormId,
            std::uint32_t initiatorFormId,
            std::uint32_t acceptorFormId,
            std::string_view declinedAction,
            std::string_view msg,
            bool skipTrigger = true);

        void EmitChildAdopted(
            std::uint32_t childActorId,
            std::uint32_t parentAId,
            std::uint32_t parentBId = 0);

        void EmitChildInherited(
            std::uint32_t childActorId,
            std::uint32_t deceasedParentId,
            std::uint32_t newParentId,
            std::uint32_t coParentId = 0);

        void EmitChildParentsChanged(
            std::uint32_t childActorId,
            std::uint32_t parentAId,
            std::uint32_t parentBId = 0,
            std::string_view reason = "marriage");

        void EmitPregnancyAnnounced(
            std::uint32_t motherId,
            std::uint32_t partnerId = 0,
            std::uint32_t fallbackBioId = 0,
            std::string_view fallbackBioName = "",
            std::int32_t childCount = 1);

        void EmitPregnancyLost(
            std::uint32_t motherId,
            std::uint32_t partnerId = 0,
            std::string_view reason = "miscarriage");

        void EmitLaborStarted(
            std::uint32_t motherId,
            std::uint32_t partnerId = 0,
            std::int32_t childCount = 1);

        void EmitLaborProgress(
            std::uint32_t motherId,
            std::int32_t bornCount,
            std::int32_t totalCount);

        void EmitChildBirthBatch(
            std::uint32_t motherId,
            const std::vector<std::uint32_t>& childIds = {});

        void EmitChildBirth(
            std::uint32_t childId,
            std::string_view childName = "");

        void EmitChildGrewUp(
            std::uint32_t childId,
            std::string_view childName = "");

        void EmitChildSentToTraining(
            std::uint32_t childId,
            std::string_view childName = "",
            std::string_view trainingLocation = "");

        void EmitChildBecameAdult(
            std::uint32_t childId,
            std::string_view childName = "");

        void EmitPaternitySecretResolved(
            std::uint32_t childId,
            std::string_view rawCaseType);

        void EmitAffairConfessed(
            std::uint32_t cheaterId,
            std::uint32_t victimId,
            std::uint32_t affairPartnerId = 0);

        void EmitAffairExposed(
            std::uint32_t affairPartnerId,
            std::uint32_t victimId,
            std::uint32_t cheaterId = 0);

        void EmitAffairResolved(
            std::uint32_t initiatorId,
            std::uint32_t otherPartyId,
            std::uint32_t affairPartnerId,
            std::string_view resolution);

        void EmitCheatingCaught(
            std::uint32_t victimId,
            std::uint32_t cheaterId,
            std::uint32_t affairPartnerId,
            bool isWalkIn);

        void EmitOpenRelationshipWalkIn(
            std::uint32_t witnessId,
            std::uint32_t partnerId,
            std::uint32_t sexPartnerId,
            bool isWalkIn);

    private:
        SkyrimNetEventService() = default;
        ~SkyrimNetEventService() = default;

        mutable std::mutex mutex_{};
        std::chrono::steady_clock::time_point suppressUntil_{};
    };

}  // namespace MARAS::Bridge::SkyrimNet
