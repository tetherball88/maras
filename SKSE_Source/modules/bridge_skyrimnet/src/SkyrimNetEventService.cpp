#include "maras/bridge_skyrimnet/SkyrimNetEventService.h"

#include "maras/bridge_skyrimnet/SkyrimNetApiLoader.h"
#include "maras/bridge_skyrimnet/SkyrimNetEventPayloads.h"
#include "maras/bridge_skyrimnet/SkyrimNetMemoryService.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/projection/EventDispatchPlan.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace MARAS::Bridge::SkyrimNet {

    namespace {

        [[nodiscard]] RE::Actor* ResolveActorPtr(std::uint32_t formId) {
            if (!Foundation::HasPhysicalActorRawId(formId)) {
                return nullptr;
            }
            if (formId == Foundation::kPlayerRefRawId) {
                return RE::PlayerCharacter::GetSingleton();
            }
            if (auto* indexed = SpatialWorldIndex::GetSingleton().GetActorFromFormId(formId)) {
                return indexed;
            }
            return RE::TESForm::LookupByID<RE::Actor>(formId);
        }

        [[nodiscard]] Projection::NarrationProximityFn MakeProximityPredicate() {
            return [](State::ActorId speaker, State::ActorId target) -> bool {
                RE::Actor* speakerPtr = ResolveActorPtr(speaker.raw());
                RE::Actor* targetPtr = ResolveActorPtr(target.raw());
                return ShouldTriggerSkyrimNetNarration(speakerPtr, targetPtr);
            };
        }

        [[nodiscard]] std::string ResolveActorName(
            std::uint32_t formId,
            const State::WorldStateSnapshot& worldSnap,
            const State::SpatialActorSnapshot& spatialSnap) {
            if (!Foundation::HasPhysicalActorRawId(formId)) {
                return {};
            }
            std::string name = ResolveActorNameSafe(formId, worldSnap, spatialSnap);
            if (name.empty()) {
                name = ActorSvc::GetName(formId);
            }
            return name;
        }

        [[nodiscard]] std::string ResolveChildDisplayName(
            State::ChildId childId,
            std::string_view overrideName,
            const State::WorldStateSnapshot& worldSnap,
            const State::SpatialActorSnapshot& spatialSnap,
            const Projection::PerspectiveView& omniscient) {
            if (!overrideName.empty()) {
                return std::string(overrideName);
            }
            if (const auto perceived = omniscient.GetPerceivedChild(childId); perceived.has_value()) {
                if (!perceived->name.empty()) {
                    return perceived->name;
                }
            }
            if (childId.HasPhysicalActor()) {
                return ResolveActorName(childId.raw(), worldSnap, spatialSnap);
            }
            return {};
        }

        [[nodiscard]] std::vector<std::uint32_t> ToRawFormIds(const std::vector<State::ActorId>& actors) {
            std::vector<std::uint32_t> ids;
            ids.reserve(actors.size());
            for (const auto actor : actors) {
                if (actor.HasPhysicalActor()) {
                    ids.push_back(actor.raw());
                }
            }
            return ids;
        }

        [[nodiscard]] bool DispatchTtmLeg(
            const Projection::EventDeliveryLeg& leg,
            const std::string& jsonPayload) {
            if (State::IsLegacyMigrationActive() || jsonPayload.empty()) {
                return false;
            }
            const auto audience = ToRawFormIds(leg.audience);
            return RegisterSkyrimNetEvent(
                "ttm_event",
                jsonPayload,
                leg.originator.raw(),
                leg.target.raw(),
                leg.exactAudience,
                audience);
        }

    }  // namespace

    void SkyrimNetEventService::SuppressNextActionNarration(float durationSeconds) noexcept {
        if (durationSeconds <= 0.0f) {
            return;
        }
        const auto durationMs = static_cast<std::int64_t>(durationSeconds * 1000.0f);
        const auto candidate =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);

        std::lock_guard lock(mutex_);
        if (candidate > suppressUntil_) {
            suppressUntil_ = candidate;
        }
    }

    bool SkyrimNetEventService::IsActionNarrationSuppressed() const noexcept {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        return now < suppressUntil_;
    }

    void SkyrimNetEventService::Revert() noexcept {
        std::lock_guard lock(mutex_);
        suppressUntil_ = {};
    }

    void SkyrimNetEventService::EmitDirectNarration(
        std::string_view content,
        std::uint32_t originatorFormId,
        std::uint32_t targetFormId,
        bool exactAudience,
        const std::vector<std::uint32_t>& audienceFormIds) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive() || content.empty()) {
            return;
        }

        std::vector<State::ActorId> rawAudience;
        rawAudience.reserve(audienceFormIds.size());
        for (const std::uint32_t id : audienceFormIds) {
            rawAudience.emplace_back(id);
        }

        const auto plan = Projection::PlanDirectNarration(
            State::ActorId{ originatorFormId },
            State::ActorId{ targetFormId },
            exactAudience,
            rawAudience,
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto& leg = plan.legs.front();
        const auto audience = ToRawFormIds(leg.audience);
        (void)RegisterSkyrimNetEvent(
            "direct_narration",
            std::string(content),
            leg.originator.raw(),
            leg.target.raw(),
            leg.exactAudience,
            audience);
    }

    void SkyrimNetEventService::EmitPolygamyBlessing(std::uint32_t spouseFormId) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanPolygamyBlessing(
            State::ActorId::Player(),
            State::ActorId{ spouseFormId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string playerName = ResolveActorName(Foundation::kPlayerRefRawId, *worldSnap, *spatialSnap);
        const std::string spouseName = ResolveActorName(spouseFormId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildPolygamyBlessingPayload(playerName, spouseName, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitAffectionGift(std::string_view msg, std::uint32_t receiverFormId) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanAffectionGift(
            State::ActorId::Player(),
            State::ActorId{ receiverFormId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto& leg = plan.legs.front();
        const std::string payload = BuildAffectionGiftPayload(msg, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitMaritalStatusChange(
        std::uint32_t initiatorFormId,
        std::uint32_t acceptorFormId,
        std::string_view newStatus,
        bool forceSkipTrigger) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const bool suppressed = IsActionNarrationSuppressed();
        const auto plan = Projection::PlanMaritalStatusChange(
            State::ActorId{ initiatorFormId },
            State::ActorId{ acceptorFormId },
            newStatus,
            forceSkipTrigger,
            suppressed,
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string initName = ResolveActorName(initiatorFormId, *worldSnap, *spatialSnap);
        const std::string accName = ResolveActorName(acceptorFormId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string_view effectiveStatus =
            !plan.canonicalOutcome.empty() ? std::string_view(plan.canonicalOutcome) : newStatus;
        const std::string payload =
            BuildMaritalStatusChangePayload(initName, accName, effectiveStatus, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);

        if (plan.postSuppressNarrationSeconds > 0.0f) {
            SuppressNextActionNarration(plan.postSuppressNarrationSeconds);
        }
    }

    void SkyrimNetEventService::EmitOpenRelationshipChange(
        std::uint32_t initiatorFormId,
        std::uint32_t partnerFormId,
        bool isOpen,
        bool forceSkipTrigger) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const bool suppressed = IsActionNarrationSuppressed();
        const auto plan = Projection::PlanOpenRelationshipChange(
            State::ActorId{ initiatorFormId },
            State::ActorId{ partnerFormId },
            forceSkipTrigger,
            suppressed,
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string initName = ResolveActorName(initiatorFormId, *worldSnap, *spatialSnap);
        const std::string partnerName = ResolveActorName(partnerFormId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload =
            BuildOpenRelationshipChangePayload(initName, partnerName, isOpen, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);

        if (plan.postSuppressNarrationSeconds > 0.0f) {
            SuppressNextActionNarration(plan.postSuppressNarrationSeconds);
        }
    }

    void SkyrimNetEventService::EmitSpouseHierarchyChange(std::uint32_t spouseFormId, bool isDemoted) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const auto plan = Projection::PlanSpouseHierarchyChange(
            State::ActorId::Player(),
            State::ActorId{ spouseFormId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string playerName = ResolveActorName(Foundation::kPlayerRefRawId, *worldSnap, *spatialSnap);
        const std::string spouseName = ResolveActorName(spouseFormId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload =
            BuildSpouseHierarchyChangePayload(playerName, spouseName, isDemoted, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitSpouseAffectionChange(
        std::uint32_t spouseFormId,
        std::string_view affectionLevel,
        std::string_view direction) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const auto plan = Projection::PlanSpouseAffectionChange(
            State::ActorId{ spouseFormId },
            State::ActorId::Player(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string spouseName = ResolveActorName(spouseFormId, *worldSnap, *spatialSnap);
        const std::string playerName = ResolveActorName(Foundation::kPlayerRefRawId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildSpouseAffectionChangePayload(
            spouseName, playerName, affectionLevel, direction, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitProposalDecision(
        std::uint32_t initiatorFormId,
        std::uint32_t acceptorFormId,
        std::string_view status,
        std::string_view decision,
        bool forceSkipTrigger) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const bool suppressed = IsActionNarrationSuppressed();
        const auto plan = Projection::PlanProposalDecision(
            State::ActorId{ initiatorFormId },
            State::ActorId{ acceptorFormId },
            forceSkipTrigger,
            suppressed,
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string initName = ResolveActorName(initiatorFormId, *worldSnap, *spatialSnap);
        const std::string accName = ResolveActorName(acceptorFormId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload =
            BuildProposalDecisionPayload(initName, accName, status, decision, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitActionDecline(
        std::uint32_t speakerFormId,
        std::uint32_t targetFormId,
        std::uint32_t initiatorFormId,
        std::uint32_t acceptorFormId,
        std::string_view declinedAction,
        std::string_view msg,
        bool skipTrigger) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanActionDecline(
            State::ActorId{ speakerFormId },
            State::ActorId{ targetFormId },
            skipTrigger,
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string speakerName = ResolveActorName(speakerFormId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildActionDeclinePayload(
            speakerName, initiatorFormId, acceptorFormId, declinedAction, msg, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitChildAdopted(
        std::uint32_t childActorId,
        std::uint32_t parentAId,
        std::uint32_t parentBId) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const auto plan = Projection::PlanChildGuardianEvent(
            Projection::SkyrimNetEventKind::ChildAdopted,
            State::ActorId{ childActorId },
            State::ActorId{ parentAId },
            State::ActorId{ parentBId },
            "",
            IsActionNarrationSuppressed(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string childName = ResolveActorName(childActorId, *worldSnap, *spatialSnap);
        const std::string pAName = ResolveActorName(parentAId, *worldSnap, *spatialSnap);
        const bool hasPB = Foundation::HasPhysicalActorRawId(parentBId);
        const std::string pBName = hasPB ? ResolveActorName(parentBId, *worldSnap, *spatialSnap) : "";

        const auto& leg = plan.legs.front();
        const std::string payload = BuildChildAdoptedPayload(
            childName, pAName, pBName, hasPB, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitChildInherited(
        std::uint32_t childActorId,
        std::uint32_t deceasedParentId,
        std::uint32_t newParentId,
        std::uint32_t coParentId) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const auto plan = Projection::PlanChildGuardianEvent(
            Projection::SkyrimNetEventKind::ChildInherited,
            State::ActorId{ childActorId },
            State::ActorId{ newParentId },
            State::ActorId{ coParentId },
            "spouse_death",
            IsActionNarrationSuppressed(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string childName = ResolveActorName(childActorId, *worldSnap, *spatialSnap);
        const std::string deceasedName = ResolveActorName(deceasedParentId, *worldSnap, *spatialSnap);
        const std::string newParentName = ResolveActorName(newParentId, *worldSnap, *spatialSnap);
        const std::string coParentName = ResolveActorName(coParentId, *worldSnap, *spatialSnap);
        const bool hasDistinctCoParent =
            Foundation::HasPhysicalActorRawId(newParentId) &&
            Foundation::HasPhysicalActorRawId(coParentId) &&
            newParentId != coParentId;

        const auto& leg = plan.legs.front();
        const std::string payload = BuildChildInheritedPayload(
            childName, deceasedName, newParentName, coParentName, hasDistinctCoParent, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitChildParentsChanged(
        std::uint32_t childActorId,
        std::uint32_t parentAId,
        std::uint32_t parentBId,
        std::string_view reason) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const auto plan = Projection::PlanChildGuardianEvent(
            Projection::SkyrimNetEventKind::ChildParentsChanged,
            State::ActorId{ childActorId },
            State::ActorId{ parentAId },
            State::ActorId{ parentBId },
            reason,
            IsActionNarrationSuppressed(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string childName = ResolveActorName(childActorId, *worldSnap, *spatialSnap);
        const std::string pAName = ResolveActorName(parentAId, *worldSnap, *spatialSnap);
        const bool hasPB = Foundation::HasPhysicalActorRawId(parentBId);
        const std::string pBName = hasPB ? ResolveActorName(parentBId, *worldSnap, *spatialSnap) : "";

        const auto& leg = plan.legs.front();
        const std::string payload = BuildChildParentsChangedPayload(
            childName, pAName, pBName, hasPB, reason, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitPregnancyAnnounced(
        std::uint32_t motherId,
        std::uint32_t partnerId,
        std::uint32_t fallbackBioId,
        std::string_view fallbackBioName,
        std::int32_t childCount) {
        if (!IsSkyrimNetAvailable() || State::IsLegacyMigrationActive()) {
            return;
        }

        const auto plan = Projection::PlanPregnancyAnnounced(
            State::ActorId{ motherId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto& leg = plan.legs.front();
        const std::string payload = BuildPregnancyAnnouncedPayload(
            worldSnap,
            spatialSnap,
            motherId,
            partnerId,
            fallbackBioId,
            fallbackBioName,
            childCount,
            leg.skipTrigger);
        if (payload.empty()) {
            return;
        }

        SkyrimNetMemoryService::GetSingleton().RecordPregnancyDiscovered(
            motherId,
            partnerId,
            fallbackBioId,
            std::string(fallbackBioName),
            childCount);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitPregnancyLost(
        std::uint32_t motherId,
        std::uint32_t partnerId,
        std::string_view reason) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanPregnancyLost(
            State::ActorId{ motherId },
            State::ActorId{ partnerId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string motherName = ResolveActorName(motherId, *worldSnap, *spatialSnap);
        const std::string partnerName = ResolveActorName(partnerId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildPregnancyLostPayload(
            motherName, partnerName, reason, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitLaborStarted(
        std::uint32_t motherId,
        std::uint32_t partnerId,
        std::int32_t childCount) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanLaborStarted(
            State::ActorId{ motherId },
            State::ActorId{ partnerId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string motherName = ResolveActorName(motherId, *worldSnap, *spatialSnap);
        const std::string partnerName = ResolveActorName(partnerId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildLaborStartedPayload(
            motherName, partnerName, childCount, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);

        if (plan.postSuppressNarrationSeconds > 0.0f) {
            SuppressNextActionNarration(plan.postSuppressNarrationSeconds);
        }
    }

    void SkyrimNetEventService::EmitLaborProgress(
        std::uint32_t motherId,
        std::int32_t bornCount,
        std::int32_t totalCount) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanLaborProgress(State::ActorId{ motherId });
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string motherName = ResolveActorName(motherId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildLaborProgressPayload(motherName, bornCount, totalCount);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitChildBirthBatch(
        std::uint32_t motherId,
        const std::vector<std::uint32_t>& childIds) {
        if (!IsSkyrimNetAvailable() ||
            State::IsLegacyMigrationActive() ||
            !Foundation::HasPhysicalActorRawId(motherId)) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();

        std::vector<State::ChildId> typedIds;
        typedIds.reserve(childIds.size());
        for (const std::uint32_t cid : childIds) {
            typedIds.emplace_back(cid);
        }

        const auto plan = Projection::PlanChildBirth(
            worldSnap,
            spatialSnap,
            State::ActorId{ motherId },
            typedIds,
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto& unmaskedLeg = plan.legs.front();
        const auto unmaskedResult = BuildChildBirthBatchPayload(
            worldSnap,
            spatialSnap,
            motherId,
            childIds,
            /*maskSecrets=*/false,
            unmaskedLeg.skipTrigger);
        if (unmaskedResult.json.empty()) {
            return;
        }

        SkyrimNetMemoryService::GetSingleton().RecordChildBirth(motherId, childIds);
        for (const auto& leg : plan.legs) {
            if (leg.maskMode == Projection::DispatchMaskMode::MaskForPartner) {
                const auto maskedResult = BuildChildBirthBatchPayload(
                    worldSnap,
                    spatialSnap,
                    motherId,
                    childIds,
                    /*maskSecrets=*/true,
                    leg.skipTrigger);
                (void)DispatchTtmLeg(leg, maskedResult.json);
            } else {
                (void)DispatchTtmLeg(leg, unmaskedResult.json);
            }
        }
    }

    void SkyrimNetEventService::EmitChildBirth(
        std::uint32_t childId,
        std::string_view childName) {
        if (!IsSkyrimNetAvailable() || childId == 0) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childId };
        const auto sec = Projection::ResolveChildSecrecy(worldSnap, spatialSnap, childKey);
        if (!sec.found) {
            return;
        }

        if (sec.mother.HasPhysicalActor()) {
            EmitChildBirthBatch(sec.mother.raw(), { childId });
            return;
        }

        const auto plan = Projection::PlanChildBirth(
            worldSnap,
            spatialSnap,
            State::ActorId{},
            { childKey },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto omniscient = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto perceivedOpt = omniscient.GetPerceivedChild(childKey);
        if (!perceivedOpt.has_value()) {
            return;
        }
        const auto& perceived = *perceivedOpt;

        std::string resolvedName = ResolveChildDisplayName(
            childKey, childName, *worldSnap, *spatialSnap, omniscient);
        const bool hasGivenName =
            (!resolvedName.empty() &&
             !Foundation::StringUtils::CaseInsensitiveEquals(resolvedName, "Newborn"));
        if (!hasGivenName) {
            resolvedName = "child";
        }

        const std::string motherName = ResolveActorName(sec.mother.raw(), *worldSnap, *spatialSnap);
        const std::string presumedFatherName =
            ResolveActorName(sec.presumedFather.raw(), *worldSnap, *spatialSnap);
        std::string bioFatherName = ResolveActorName(sec.bioFather.raw(), *worldSnap, *spatialSnap);
        if (bioFatherName.empty()) {
            bioFatherName = perceived.perceivedBioFatherFallbackName;
        }

        for (const auto& leg : plan.legs) {
            const std::string payload = BuildChildBirthSingleMotherlessPayload(
                sec,
                resolvedName,
                perceived.gender,
                hasGivenName,
                motherName,
                presumedFatherName,
                bioFatherName,
                leg.maskMode,
                leg.skipTrigger);
            (void)DispatchTtmLeg(leg, payload);
        }
    }

    void SkyrimNetEventService::EmitChildGrewUp(
        std::uint32_t childId,
        std::string_view childName) {
        if (!IsSkyrimNetAvailable() || childId == 0) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childId };
        const auto sec = Projection::ResolveChildSecrecy(worldSnap, spatialSnap, childKey);
        if (!sec.found) {
            return;
        }

        const auto plan = Projection::PlanChildGrewUp(
            worldSnap, spatialSnap, childKey, MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto omniscient = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto perceivedOpt = omniscient.GetPerceivedChild(childKey);
        if (!perceivedOpt.has_value()) {
            return;
        }
        const auto& perceived = *perceivedOpt;

        const std::string resolvedName = ResolveChildDisplayName(
            childKey, childName, *worldSnap, *spatialSnap, omniscient);
        const std::string motherName = ResolveActorName(sec.mother.raw(), *worldSnap, *spatialSnap);
        const std::string presumedFatherName =
            ResolveActorName(sec.presumedFather.raw(), *worldSnap, *spatialSnap);
        std::string bioFatherName = ResolveActorName(sec.bioFather.raw(), *worldSnap, *spatialSnap);
        if (bioFatherName.empty()) {
            bioFatherName = perceived.perceivedBioFatherFallbackName;
        }

        for (const auto& leg : plan.legs) {
            const std::string payload = BuildChildGrewUpPayload(
                sec,
                resolvedName,
                perceived.gender,
                motherName,
                presumedFatherName,
                bioFatherName,
                leg.maskMode,
                leg.skipTrigger);
            (void)DispatchTtmLeg(leg, payload);
        }
    }

    void SkyrimNetEventService::EmitChildSentToTraining(
        std::uint32_t childId,
        std::string_view childName,
        std::string_view trainingLocation) {
        if (!IsSkyrimNetAvailable() || childId == 0) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childId };
        const auto sec = Projection::ResolveChildSecrecy(worldSnap, spatialSnap, childKey);
        if (!sec.found) {
            return;
        }

        const auto plan = Projection::PlanChildSentToTraining(
            worldSnap, spatialSnap, childKey, MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto omniscient = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto perceivedOpt = omniscient.GetPerceivedChild(childKey);
        if (!perceivedOpt.has_value()) {
            return;
        }
        const auto& perceived = *perceivedOpt;

        const std::string resolvedName = ResolveChildDisplayName(
            childKey, childName, *worldSnap, *spatialSnap, omniscient);
        const std::string_view resolvedLocation =
            !trainingLocation.empty() ? trainingLocation : std::string_view(perceived.trainingLocation);
        const std::string motherName = ResolveActorName(sec.mother.raw(), *worldSnap, *spatialSnap);
        const std::string presumedFatherName =
            ResolveActorName(sec.presumedFather.raw(), *worldSnap, *spatialSnap);
        std::string bioFatherName = ResolveActorName(sec.bioFather.raw(), *worldSnap, *spatialSnap);
        if (bioFatherName.empty()) {
            bioFatherName = perceived.perceivedBioFatherFallbackName;
        }

        for (const auto& leg : plan.legs) {
            const std::string payload = BuildChildSentToTrainingPayload(
                sec,
                resolvedName,
                perceived.gender,
                resolvedLocation,
                motherName,
                presumedFatherName,
                bioFatherName,
                leg.maskMode,
                leg.skipTrigger);
            (void)DispatchTtmLeg(leg, payload);
        }
    }

    void SkyrimNetEventService::EmitChildBecameAdult(
        std::uint32_t childId,
        std::string_view childName) {
        if (!IsSkyrimNetAvailable() || childId == 0) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childId };
        const auto sec = Projection::ResolveChildSecrecy(worldSnap, spatialSnap, childKey);
        if (!sec.found) {
            return;
        }

        const auto plan = Projection::PlanChildBecameAdult(
            worldSnap, spatialSnap, childKey, MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto omniscient = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto perceivedOpt = omniscient.GetPerceivedChild(childKey);
        if (!perceivedOpt.has_value()) {
            return;
        }
        const auto& perceived = *perceivedOpt;

        const std::string resolvedName = ResolveChildDisplayName(
            childKey, childName, *worldSnap, *spatialSnap, omniscient);
        const std::string motherName = ResolveActorName(sec.mother.raw(), *worldSnap, *spatialSnap);
        const std::string presumedFatherName =
            ResolveActorName(sec.presumedFather.raw(), *worldSnap, *spatialSnap);
        std::string bioFatherName = ResolveActorName(sec.bioFather.raw(), *worldSnap, *spatialSnap);
        if (bioFatherName.empty()) {
            bioFatherName = perceived.perceivedBioFatherFallbackName;
        }

        for (const auto& leg : plan.legs) {
            const std::string payload = BuildChildBecameAdultPayload(
                sec,
                resolvedName,
                perceived.gender,
                motherName,
                presumedFatherName,
                bioFatherName,
                leg.maskMode,
                leg.skipTrigger);
            (void)DispatchTtmLeg(leg, payload);
        }
    }

    void SkyrimNetEventService::EmitPaternitySecretResolved(
        std::uint32_t childId,
        std::string_view rawCaseType) {
        if (!IsSkyrimNetAvailable() || childId == 0) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childId };
        const auto sec = Projection::ResolveChildSecrecy(worldSnap, spatialSnap, childKey);
        if (!sec.found) {
            return;
        }

        const auto plan = Projection::PlanPaternitySecretResolved(
            worldSnap,
            spatialSnap,
            childKey,
            rawCaseType,
            IsActionNarrationSuppressed(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto omniscient = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto perceivedOpt = omniscient.GetPerceivedChild(childKey);
        if (!perceivedOpt.has_value()) {
            return;
        }
        const auto& perceived = *perceivedOpt;

        const std::string childName = ResolveChildDisplayName(
            childKey, "", *worldSnap, *spatialSnap, omniscient);
        const std::string motherName = ResolveActorName(sec.mother.raw(), *worldSnap, *spatialSnap);
        const std::string presumedFatherName =
            ResolveActorName(sec.presumedFather.raw(), *worldSnap, *spatialSnap);
        std::string bioFatherName = ResolveActorName(sec.bioFather.raw(), *worldSnap, *spatialSnap);
        if (bioFatherName.empty()) {
            bioFatherName = perceived.perceivedBioFatherFallbackName;
        }

        for (const auto& leg : plan.legs) {
            const std::string payload = BuildPaternitySecretResolvedPayload(
                sec,
                plan.canonicalOutcome,
                plan.wasPregnancyUnannounced,
                childName,
                motherName,
                presumedFatherName,
                bioFatherName,
                leg.maskMode,
                leg.skipTrigger);
            (void)DispatchTtmLeg(leg, payload);
        }

        if (plan.postSuppressNarrationSeconds > 0.0f) {
            SuppressNextActionNarration(plan.postSuppressNarrationSeconds);
        }
        MARAS_LOG_INFO(
            "[SkyrimNetEventService] PaternitySecretResolved emitted: child={:08X}, outcome='{}', legs={}",
            childId, plan.canonicalOutcome, plan.legs.size());
    }

    void SkyrimNetEventService::EmitAffairConfessed(
        std::uint32_t cheaterId,
        std::uint32_t victimId,
        std::uint32_t affairPartnerId) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanAffairConfessed(
            State::ActorId{ cheaterId },
            State::ActorId{ victimId },
            IsActionNarrationSuppressed(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string cheaterName = ResolveActorName(cheaterId, *worldSnap, *spatialSnap);
        const std::string victimName = ResolveActorName(victimId, *worldSnap, *spatialSnap);
        const std::string partnerName = ResolveActorName(affairPartnerId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload =
            BuildAffairConfessedPayload(cheaterName, victimName, partnerName, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);

        if (plan.postSuppressNarrationSeconds > 0.0f) {
            SuppressNextActionNarration(plan.postSuppressNarrationSeconds);
        }
    }

    void SkyrimNetEventService::EmitAffairExposed(
        std::uint32_t affairPartnerId,
        std::uint32_t victimId,
        std::uint32_t cheaterId) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanAffairExposed(
            State::ActorId{ affairPartnerId },
            State::ActorId{ victimId },
            IsActionNarrationSuppressed(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string partnerName = ResolveActorName(affairPartnerId, *worldSnap, *spatialSnap);
        const std::string victimName = ResolveActorName(victimId, *worldSnap, *spatialSnap);
        const std::string cheaterName = ResolveActorName(cheaterId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload =
            BuildAffairExposedPayload(partnerName, victimName, cheaterName, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);

        if (plan.postSuppressNarrationSeconds > 0.0f) {
            SuppressNextActionNarration(plan.postSuppressNarrationSeconds);
        }
    }

    void SkyrimNetEventService::EmitAffairResolved(
        std::uint32_t initiatorId,
        std::uint32_t otherPartyId,
        std::uint32_t affairPartnerId,
        std::string_view resolution) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanAffairResolved(
            State::ActorId{ initiatorId },
            State::ActorId{ otherPartyId },
            IsActionNarrationSuppressed(),
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string initName = ResolveActorName(initiatorId, *worldSnap, *spatialSnap);
        const std::string otherName = ResolveActorName(otherPartyId, *worldSnap, *spatialSnap);
        const std::string partnerName = ResolveActorName(affairPartnerId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildAffairResolvedPayload(
            initName, otherName, partnerName, resolution, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);

        if (plan.postSuppressNarrationSeconds > 0.0f) {
            SuppressNextActionNarration(plan.postSuppressNarrationSeconds);
        }
    }

    void SkyrimNetEventService::EmitCheatingCaught(
        std::uint32_t victimId,
        std::uint32_t cheaterId,
        std::uint32_t affairPartnerId,
        bool isWalkIn) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanCheatingCaught(
            State::ActorId{ victimId },
            State::ActorId{ cheaterId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string victimName = ResolveActorName(victimId, *worldSnap, *spatialSnap);
        const std::string cheaterName = ResolveActorName(cheaterId, *worldSnap, *spatialSnap);
        const std::string partnerName = ResolveActorName(affairPartnerId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildCheatingCaughtPayload(
            victimName, cheaterName, partnerName, isWalkIn, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

    void SkyrimNetEventService::EmitOpenRelationshipWalkIn(
        std::uint32_t witnessId,
        std::uint32_t partnerId,
        std::uint32_t sexPartnerId,
        bool isWalkIn) {
        if (!IsSkyrimNetAvailable()) {
            return;
        }

        const auto plan = Projection::PlanOpenRelationshipWalkIn(
            State::ActorId{ witnessId },
            State::ActorId{ partnerId },
            MakeProximityPredicate());
        if (!plan.shouldEmit || plan.legs.empty()) {
            return;
        }

        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const std::string witnessName = ResolveActorName(witnessId, *worldSnap, *spatialSnap);
        const std::string partnerName = ResolveActorName(partnerId, *worldSnap, *spatialSnap);
        const std::string sexPartnerName = ResolveActorName(sexPartnerId, *worldSnap, *spatialSnap);

        const auto& leg = plan.legs.front();
        const std::string payload = BuildOpenRelationshipWalkInPayload(
            witnessName, partnerName, sexPartnerName, isWalkIn, leg.skipTrigger);
        (void)DispatchTtmLeg(leg, payload);
    }

}  // namespace MARAS::Bridge::SkyrimNet
