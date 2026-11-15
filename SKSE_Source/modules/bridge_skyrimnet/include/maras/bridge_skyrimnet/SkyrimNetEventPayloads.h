#pragma once

#include "maras/projection/EventDispatchPlan.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Bridge::SkyrimNet {

    struct ChildBirthBatchPayloadResult {
        std::string json{};
        std::vector<std::uint32_t> resolvedChildIds{};
    };

    [[nodiscard]] std::string BuildPolygamyBlessingPayload(
        std::string_view playerName,
        std::string_view spouseName,
        bool skipTrigger);

    [[nodiscard]] std::string BuildAffectionGiftPayload(
        std::string_view msg,
        bool skipTrigger);

    [[nodiscard]] std::string BuildMaritalStatusChangePayload(
        std::string_view initiatorName,
        std::string_view acceptorName,
        std::string_view newStatus,
        bool skipTrigger);

    [[nodiscard]] std::string BuildOpenRelationshipChangePayload(
        std::string_view initiatorName,
        std::string_view partnerName,
        bool isOpen,
        bool skipTrigger);

    [[nodiscard]] std::string BuildSpouseHierarchyChangePayload(
        std::string_view playerName,
        std::string_view spouseName,
        bool isDemoted,
        bool skipTrigger);

    [[nodiscard]] std::string BuildSpouseAffectionChangePayload(
        std::string_view spouseName,
        std::string_view playerName,
        std::string_view affectionLevel,
        std::string_view direction,
        bool skipTrigger);

    [[nodiscard]] std::string BuildProposalDecisionPayload(
        std::string_view initiatorName,
        std::string_view acceptorName,
        std::string_view status,
        std::string_view decision,
        bool skipTrigger);

    [[nodiscard]] std::string BuildActionDeclinePayload(
        std::string_view speakerName,
        std::uint32_t initiatorFormId,
        std::uint32_t targetFormId,
        std::string_view declinedAction,
        std::string_view msg,
        bool skipTrigger);

    [[nodiscard]] std::string BuildChildAdoptedPayload(
        std::string_view childName,
        std::string_view parentAName,
        std::string_view parentBName,
        bool hasParentB,
        bool skipTrigger);

    [[nodiscard]] std::string BuildChildInheritedPayload(
        std::string_view childName,
        std::string_view deceasedName,
        std::string_view newParentName,
        std::string_view coParentName,
        bool hasDistinctCoParent,
        bool skipTrigger);

    [[nodiscard]] std::string BuildChildParentsChangedPayload(
        std::string_view childName,
        std::string_view parentAName,
        std::string_view parentBName,
        bool hasParentB,
        std::string_view reason,
        bool skipTrigger);

    [[nodiscard]] std::string BuildPregnancyAnnouncedPayload(
        std::shared_ptr<const State::WorldStateSnapshot> worldSnap,
        std::shared_ptr<const State::SpatialActorSnapshot> spatialSnap,
        std::uint32_t motherID,
        std::uint32_t partnerID,
        std::uint32_t fallbackBioID,
        std::string_view fallbackBioName,
        std::int32_t childCount,
        bool skipTrigger);

    [[nodiscard]] std::string BuildPregnancyLostPayload(
        std::string_view motherName,
        std::string_view partnerName,
        std::string_view reason,
        bool skipTrigger);

    [[nodiscard]] std::string BuildLaborStartedPayload(
        std::string_view motherName,
        std::string_view partnerName,
        std::int32_t childCount,
        bool skipTrigger);

    [[nodiscard]] std::string BuildLaborProgressPayload(
        std::string_view motherName,
        std::int32_t bornCount,
        std::int32_t totalCount);

    [[nodiscard]] ChildBirthBatchPayloadResult BuildChildBirthBatchPayload(
        std::shared_ptr<const State::WorldStateSnapshot> worldSnap,
        std::shared_ptr<const State::SpatialActorSnapshot> spatialSnap,
        std::uint32_t motherID,
        const std::vector<std::uint32_t>& childIDs,
        bool maskSecrets,
        bool skipTrigger);

    [[nodiscard]] std::string BuildChildBirthSingleMotherlessPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        bool hasGivenName,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger);

    [[nodiscard]] std::string BuildChildGrewUpPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger);

    [[nodiscard]] std::string BuildChildSentToTrainingPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        std::string_view trainingLocation,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger);

    [[nodiscard]] std::string BuildChildBecameAdultPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger);

    [[nodiscard]] std::string BuildPaternitySecretResolvedPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view canonicalOutcome,
        bool wasPregUnannounced,
        std::string_view rawChildName,
        std::string_view motherName,
        std::string_view rawPresumedFatherName,
        std::string_view rawBioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger);

    [[nodiscard]] std::string BuildAffairConfessedPayload(
        std::string_view cheaterName,
        std::string_view victimName,
        std::string_view partnerName,
        bool skipTrigger);

    [[nodiscard]] std::string BuildAffairExposedPayload(
        std::string_view partnerName,
        std::string_view victimName,
        std::string_view cheaterName,
        bool skipTrigger);

    [[nodiscard]] std::string BuildAffairResolvedPayload(
        std::string_view initName,
        std::string_view otherName,
        std::string_view partnerName,
        std::string_view resolution,
        bool skipTrigger);

    [[nodiscard]] std::string BuildCheatingCaughtPayload(
        std::string_view victimName,
        std::string_view cheaterName,
        std::string_view partnerName,
        bool isWalkIn,
        bool skipTrigger);

    [[nodiscard]] std::string BuildOpenRelationshipWalkInPayload(
        std::string_view witnessName,
        std::string_view partnerName,
        std::string_view sexPartnerName,
        bool isWalkIn,
        bool skipTrigger);

}  // namespace MARAS::Bridge::SkyrimNet
