#include "maras/app/SkyrimNetActionCoordinator.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/AppContext.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/PaternityWorkflowCoordinator.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/lineage/PaternityActionRules.h"
#include "maras/domain/relationships/DialogueAndActionRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"

#include <algorithm>
#include <unordered_set>

namespace MARAS::App {

    namespace {

        namespace StringUtils = Foundation::StringUtils;
        using Domain::Relationships::ActorNameAliasKind;
        using Domain::Relationships::ClassifyActorNameAlias;

        [[nodiscard]] bool IsModalAccepted(std::int32_t modalResult) noexcept {
            return modalResult == 0 || modalResult == 4 || modalResult == 5;
        }

        [[nodiscard]] RE::Actor* LookupActorSafe(RE::FormID actorId) noexcept {
            if (actorId == 0) {
                return nullptr;
            }
            if (actorId == Foundation::kPlayerRefRawId) {
                return RE::PlayerCharacter::GetSingleton();
            }
            if (auto* actor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(actorId)) {
                return actor;
            }
            return RE::TESForm::LookupByID<RE::Actor>(actorId);
        }

        void SuppressSkyrimNetActionNarrationIfAvailable() {
            auto& skyrimNet = GetSkyrimNetGateway();
            if (skyrimNet.IsAvailable()) {
                skyrimNet.SuppressNextActionNarration();
            }
        }

    }  // namespace

    SkyrimNetActionCoordinator& SkyrimNetActionCoordinator::GetSingleton() {
        static SkyrimNetActionCoordinator instance;
        return instance;
    }

    void SkyrimNetActionCoordinator::Revert() noexcept {
        std::lock_guard lock(pendingMutex_);
        pendingNarrations_.clear();
    }

    void SkyrimNetActionCoordinator::TickPendingNarrations(float deltaSeconds) {
        std::vector<PendingProposalNarration> ready;
        {
            std::lock_guard lock(pendingMutex_);
            if (pendingNarrations_.empty()) {
                return;
            }

            const float clampedDelta = (std::max)(0.0f, deltaSeconds);
            std::vector<PendingProposalNarration> remaining;
            remaining.reserve(pendingNarrations_.size());
            for (auto& entry : pendingNarrations_) {
                if (!MARAS::IsSessionEpochCurrent(entry.epoch)) {
                    continue;
                }
                entry.remainingSeconds -= clampedDelta;
                if (entry.remainingSeconds <= 0.0f) {
                    ready.push_back(std::move(entry));
                } else {
                    remaining.push_back(std::move(entry));
                }
            }
            pendingNarrations_ = std::move(remaining);
        }

        if (ready.empty() || !GetSkyrimNetGateway().IsAvailable()) {
            return;
        }

        auto& skyrimNet = GetSkyrimNetGateway();
        for (const auto& entry : ready) {
            if (!MARAS::IsSessionEpochCurrent(entry.epoch)) {
                continue;
            }
            auto* partnerActor = LookupActorSafe(entry.partnerId);
            auto* proposerActor = LookupActorSafe(entry.proposerId);
            if (!partnerActor || !proposerActor ||
                partnerActor->IsDeleted() || proposerActor->IsDeleted() ||
                partnerActor->IsDead() || proposerActor->IsDead()) {
                continue;
            }
            if (!skyrimNet.ShouldTriggerNarration(partnerActor, proposerActor)) {
                continue;
            }
            skyrimNet.EmitDirectNarration(entry.message, entry.partnerId, entry.proposerId);
        }
    }

    RE::Actor* SkyrimNetActionCoordinator::ResolveActorByName(std::string_view name) {
        const std::string playerName = ActorSvc::GetName(Foundation::kPlayerRefRawId);
        switch (ClassifyActorNameAlias(name, playerName)) {
            case ActorNameAliasKind::NoneSentinel:
                return nullptr;
            case ActorNameAliasKind::Player:
                return RE::PlayerCharacter::GetSingleton();
            case ActorNameAliasKind::NamedNpc:
                break;
        }

        const std::string query(StringUtils::TrimView(name));
        const auto entry = SpatialWorldIndex::GetSingleton().FindByName(query);
        if (!entry) {
            return nullptr;
        }
        if (!entry->isUnique) {
            MARAS_LOG_INFO("[SpatialWorldIndex] FindActorByName: '{}' is non-unique NPC (guard/soldier/generic); returning none",
                           query);
            return nullptr;
        }

        const RE::FormID refId = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
        return SpatialWorldIndex::GetSingleton().GetActorFromFormId(refId);
    }

    void SkyrimNetActionCoordinator::ProposeRelationshipChange(
        RE::Actor* akActor,
        RE::Actor* partner,
        std::string suggestedStatus,
        std::function<void(bool)> onComplete) {
        if (!akActor || !partner) {
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        const RE::FormID akActorId = akActor->GetFormID();
        const RE::FormID partnerId = partner->GetFormID();
        const auto parsedTarget = Foundation::StringToRelationshipStatus(suggestedStatus);
        auto& relSvc = RelationshipUseCases::GetSingleton();
        const auto currentStatus = relSvc.GetRelationshipStatus(akActorId, partnerId);

        if (parsedTarget == Foundation::RelationshipStatus::Unknown || currentStatus == parsedTarget) {
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        if (!relSvc.IsAvailableForStatus(akActorId, partnerId, suggestedStatus)) {
            MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ProposeRelationshipChange({}): {} ({:08X}) and {} ({:08X}) not eligible",
                            suggestedStatus, ActorSvc::GetName(akActorId), akActorId,
                            ActorSvc::GetName(partnerId), partnerId);
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        if (partnerId == Foundation::kPlayerRefRawId) {
            const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();
            GetModalGateway().ShowConfirmationModalWithCallback(
                suggestedStatus.c_str(),
                { akActor },
                { partner },
                "",
                [expectedEpoch,
                 akActorId,
                 partnerId,
                 suggestedStatus,
                 currentStatus,
                 parsedTarget,
                 onComplete](std::int32_t modalResult) {
                    if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                        return;
                    }
                    std::lock_guard lock(MARAS::GetPapyrusMutex());
                    if (!IsModalAccepted(modalResult)) {
                        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ProposeRelationshipChange: '{}' declined/blocked (modalResult={})",
                                        suggestedStatus, modalResult);
                        if (onComplete) {
                            onComplete(false);
                        }
                        return;
                    }

                    auto& innerRelSvc = RelationshipUseCases::GetSingleton();
                    if (innerRelSvc.GetRelationshipStatus(akActorId, partnerId) != currentStatus ||
                        !innerRelSvc.IsAvailableForStatus(akActorId, partnerId, suggestedStatus)) {
                        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ProposeRelationshipChange({}): state changed while pending; ignoring",
                                        suggestedStatus);
                        if (onComplete) {
                            onComplete(false);
                        }
                        return;
                    }

                    SuppressSkyrimNetActionNarrationIfAvailable();
                    const bool ok = innerRelSvc.PromoteToStatusWith(partnerId, akActorId, parsedTarget, true);
                    if (!ok) {
                        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ProposeRelationshipChange: '{}' blocked by relationship rules",
                                        suggestedStatus);
                    }
                    if (onComplete) {
                        onComplete(ok);
                    }
                });
            return;
        }

        auto& skyrimNet = GetSkyrimNetGateway();
        if (!skyrimNet.IsAvailable() || !skyrimNet.ShouldTriggerNarration(partner, akActor)) {
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        std::string message = Domain::Relationships::BuildNpcProposalNarration(
            ActorSvc::GetName(akActorId),
            ActorSvc::GetName(partnerId),
            suggestedStatus);
        {
            std::lock_guard lock(pendingMutex_);
            pendingNarrations_.push_back(PendingProposalNarration{
                akActorId,
                partnerId,
                std::move(message),
                5.0f,
                MARAS::GetSessionEpoch() });
        }

        if (onComplete) {
            onComplete(true);
        }
    }

    void SkyrimNetActionCoordinator::ConsiderRelationshipChange(
        RE::Actor* akActor,
        RE::Actor* partner,
        std::string decision,
        std::string status,
        std::string reasoning,
        std::function<void(bool)> onComplete) {
        const RE::FormID akActorId = akActor ? akActor->GetFormID() : 0;
        const RE::FormID partnerId = partner ? partner->GetFormID() : 0;
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConsiderRelationshipChange({}): {} ({:08X}) and {} ({:08X}) decision='{}' reasoning='{}'",
                        status,
                        akActor ? ActorSvc::GetName(akActorId) : "None",
                        akActorId,
                        partner ? ActorSvc::GetName(partnerId) : "None",
                        partnerId,
                        decision,
                        reasoning);

        if (!akActor || !partner) {
            MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConsiderRelationshipChange({}): invalid actor(s)", status);
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        const auto parsedTarget = Foundation::StringToRelationshipStatus(status);
        auto& relSvc = RelationshipUseCases::GetSingleton();
        const auto currentStatus = relSvc.GetRelationshipStatus(akActorId, partnerId);

        if (parsedTarget == Foundation::RelationshipStatus::Unknown || currentStatus == parsedTarget) {
            MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConsiderRelationshipChange({}): already '{}' or invalid status",
                            status, status);
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        const std::string_view trimmedDecision = StringUtils::TrimView(decision);
        if (StringUtils::CaseInsensitiveEquals(trimmedDecision, "accept")) {
            if (!relSvc.IsAvailableForStatus(partnerId, akActorId, status)) {
                MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConsiderRelationshipChange({}): {} ({:08X}) and {} ({:08X}) not eligible",
                                status, ActorSvc::GetName(partnerId), partnerId,
                                ActorSvc::GetName(akActorId), akActorId);
                if (onComplete) {
                    onComplete(false);
                }
                return;
            }

            const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();
            GetModalGateway().ShowConfirmationModalWithCallback(
                status.c_str(),
                { akActor },
                { partner },
                "",
                [expectedEpoch,
                 akActorId,
                 partnerId,
                 status,
                 currentStatus,
                 parsedTarget,
                 onComplete](std::int32_t modalResult) {
                    if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                        return;
                    }
                    std::lock_guard lock(MARAS::GetPapyrusMutex());
                    if (!IsModalAccepted(modalResult)) {
                        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConsiderRelationshipChange: '{}' declined/blocked (modalResult={})",
                                        status, modalResult);
                        if (onComplete) {
                            onComplete(false);
                        }
                        return;
                    }

                    auto& innerRelSvc = RelationshipUseCases::GetSingleton();
                    if (innerRelSvc.GetRelationshipStatus(akActorId, partnerId) != currentStatus ||
                        !innerRelSvc.IsAvailableForStatus(partnerId, akActorId, status)) {
                        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConsiderRelationshipChange({}): state changed while pending; ignoring",
                                        status);
                        if (onComplete) {
                            onComplete(false);
                        }
                        return;
                    }

                    SuppressSkyrimNetActionNarrationIfAvailable();
                    const bool ok = innerRelSvc.PromoteToStatusWith(partnerId, akActorId, parsedTarget, true);
                    if (!ok) {
                        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConsiderRelationshipChange: '{}' blocked by relationship rules",
                                        status);
                    }
                    if (onComplete) {
                        onComplete(ok);
                    }
                });
            return;
        }

        if (StringUtils::CaseInsensitiveEquals(trimmedDecision, "reject")) {
            if (partnerId == Foundation::kPlayerRefRawId && akActorId != 0) {
                AffectionEventCoordinator::GetSingleton().AdjustPlayerPartnerAffection(
                    akActorId, -15.0f, "proposal_rejected_by_partner");
            }
            auto& skyrimNet = GetSkyrimNetGateway();
            if (skyrimNet.IsAvailable()) {
                skyrimNet.SuppressNextActionNarration();
                skyrimNet.EmitProposalDecision(partnerId, akActorId, status, decision, true);
            }
            if (onComplete) {
                onComplete(true);
            }
            return;
        }

        if (onComplete) {
            onComplete(false);
        }
    }

    void SkyrimNetActionCoordinator::BreakupRelationship(
        RE::Actor* akActor,
        std::string partnerName,
        std::string status,
        std::string reasoning,
        std::function<void(bool)> onComplete) {
        const RE::FormID akActorId = akActor ? akActor->GetFormID() : 0;
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] BreakupRelationship({}): {} ({:08X}) with '{}' reasoning='{}'",
                        status,
                        akActor ? ActorSvc::GetName(akActorId) : "None",
                        akActorId,
                        partnerName,
                        reasoning);

        const std::string_view trimmedPartner = StringUtils::TrimView(partnerName);
        if (!akActor || trimmedPartner.empty()) {
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        const auto requiredCurrent = Domain::Relationships::MapBreakupTargetToRequiredCurrentStatus(status);
        const auto parsedTarget = Foundation::StringToRelationshipStatus(status);
        if (!requiredCurrent || parsedTarget == Foundation::RelationshipStatus::Unknown) {
            MARAS_LOG_WARN("[SkyrimNetActionCoordinator] BreakupRelationship: unknown breakup status '{}'", status);
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        auto& relSvc = RelationshipUseCases::GetSingleton();
        const auto partners = relSvc.GetPartners(akActorId, *requiredCurrent);
        RE::FormID partnerId = 0;
        RE::FormID caseInsensitiveMatch = 0;
        for (const RE::FormID candId : partners) {
            const std::string candName = ActorSvc::GetName(candId);
            if (candName == partnerName) {
                partnerId = candId;
                break;
            }
            if (caseInsensitiveMatch == 0 && StringUtils::CaseInsensitiveEquals(candName, trimmedPartner)) {
                caseInsensitiveMatch = candId;
            }
        }
        if (partnerId == 0) {
            partnerId = caseInsensitiveMatch;
        }

        RE::Actor* partnerActor = LookupActorSafe(partnerId);
        if (partnerId == 0 || !partnerActor) {
            MARAS_LOG_WARN("[SkyrimNetActionCoordinator] BreakupRelationship({}): no partner matching '{}' found for {} ({:08X})",
                           status, partnerName, ActorSvc::GetName(akActorId), akActorId);
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        if (!relSvc.IsAvailableForStatus(partnerId, akActorId, status)) {
            MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] BreakupRelationship({}): {} ({:08X}) and {} ({:08X}) not eligible",
                            status, ActorSvc::GetName(partnerId), partnerId,
                            ActorSvc::GetName(akActorId), akActorId);
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();
        const auto reqStatus = *requiredCurrent;
        GetModalGateway().ShowConfirmationModalWithCallback(
            status.c_str(),
            { akActor },
            { partnerActor },
            "",
            [expectedEpoch,
             akActorId,
             partnerId,
             status,
             reqStatus,
             parsedTarget,
             onComplete](std::int32_t modalResult) {
                if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                std::lock_guard lock(MARAS::GetPapyrusMutex());
                if (!IsModalAccepted(modalResult)) {
                    MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] BreakupRelationship: '{}' declined/blocked (modalResult={})",
                                    status, modalResult);
                    if (onComplete) {
                        onComplete(false);
                    }
                    return;
                }

                auto& innerRelSvc = RelationshipUseCases::GetSingleton();
                if (innerRelSvc.GetRelationshipStatus(akActorId, partnerId) != reqStatus ||
                    !innerRelSvc.IsAvailableForStatus(partnerId, akActorId, status)) {
                    MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] BreakupRelationship({}): state changed while pending; ignoring",
                                    status);
                    if (onComplete) {
                        onComplete(false);
                    }
                    return;
                }

                SuppressSkyrimNetActionNarrationIfAvailable();
                const bool ok = innerRelSvc.PromoteToStatusWith(partnerId, akActorId, parsedTarget, true);
                if (!ok) {
                    MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] BreakupRelationship: '{}' blocked by relationship rules",
                                    status);
                }
                if (onComplete) {
                    onComplete(ok);
                }
            });
    }

    void SkyrimNetActionCoordinator::SetOpenRelationshipChange(
        RE::Actor* akActor,
        RE::Actor* partner,
        bool isOpen,
        std::string reasoning,
        std::function<void(bool)> onComplete) {
        const RE::FormID akActorId = akActor ? akActor->GetFormID() : 0;
        const RE::FormID partnerId = partner ? partner->GetFormID() : 0;
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] SetOpenRelationshipChange: {} ({:08X}) and {} ({:08X}) open={} reasoning='{}'",
                        akActor ? ActorSvc::GetName(akActorId) : "None",
                        akActorId,
                        partner ? ActorSvc::GetName(partnerId) : "None",
                        partnerId,
                        isOpen,
                        reasoning);

        if (!akActor || !partner) {
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        auto& relSvc = RelationshipUseCases::GetSingleton();
        const auto currentStatus = relSvc.GetRelationshipStatus(akActorId, partnerId);
        const bool isEligible = isOpen ? relSvc.CanOpenRelationship(akActorId, partnerId)
                                       : relSvc.CanCloseRelationship(akActorId, partnerId);
        if (relSvc.GetPairIsOpen(akActorId, partnerId) == isOpen || !isEligible) {
            MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] SetOpenRelationshipChange({}): {} ({:08X}) and {} ({:08X}) not eligible",
                            isOpen ? "open" : "close",
                            ActorSvc::GetName(akActorId), akActorId,
                            ActorSvc::GetName(partnerId), partnerId);
            if (onComplete) {
                onComplete(false);
            }
            return;
        }

        const char* actionType = isOpen ? "open_relationship" : "close_relationship";
        const std::string actionTypeStr(actionType);
        const MARAS::SessionEpoch expectedEpoch = MARAS::GetSessionEpoch();

        GetModalGateway().ShowConfirmationModalWithCallback(
            actionType,
            { akActor },
            { partner },
            "",
            [expectedEpoch,
             akActorId,
             partnerId,
             isOpen,
             currentStatus,
             actionTypeStr,
             onComplete](std::int32_t modalResult) {
                if (!MARAS::IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                std::lock_guard lock(MARAS::GetPapyrusMutex());
                if (!IsModalAccepted(modalResult)) {
                    MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] SetOpenRelationshipChange: '{}' declined (modalResult={})",
                                    actionTypeStr, modalResult);
                    if (onComplete) {
                        onComplete(false);
                    }
                    return;
                }

                auto& innerRelSvc = RelationshipUseCases::GetSingleton();
                if (innerRelSvc.GetRelationshipStatus(akActorId, partnerId) != currentStatus ||
                    innerRelSvc.GetPairIsOpen(akActorId, partnerId) == isOpen) {
                    MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] SetOpenRelationshipChange({}): state changed while pending; ignoring",
                                    actionTypeStr);
                    if (onComplete) {
                        onComplete(false);
                    }
                    return;
                }

                SuppressSkyrimNetActionNarrationIfAvailable();
                const bool ok = innerRelSvc.SetPairIsOpen(akActorId, partnerId, isOpen, true);
                if (onComplete) {
                    onComplete(ok);
                }
            });
    }

    bool SkyrimNetActionCoordinator::RevealChildSecret(
        RE::Actor* akActor,
        std::string_view targetFather,
        std::string_view childName,
        std::string_view revealMode) {
        if (!akActor) {
            return false;
        }

        const RE::FormID akActorId = akActor->GetFormID();
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] RevealChildSecret: {} ({:08X}) targetFather='{}' childName='{}' mode='{}'",
                        ActorSvc::GetName(akActorId), akActorId, targetFather, childName, revealMode);

        const auto directive = Domain::Lineage::ParsePaternityRevealDirective(targetFather, childName, revealMode);
        RE::Actor* targetFatherActor = !directive.fatherLookupQuery.empty()
                                           ? ResolveActorByName(directive.fatherLookupQuery)
                                           : nullptr;
        const RE::FormID targetFatherId = targetFatherActor ? targetFatherActor->GetFormID() : 0;

        auto& paternitySvc = PaternityWorkflowCoordinator::GetSingleton();
        auto& lineageSvc = LineageUseCases::GetSingleton();

        RE::FormID filterFatherId = targetFatherId;
        RE::FormID childID = paternitySvc.FindChildPaternitySecret(akActorId, filterFatherId, std::string(childName));
        if (childID == 0 && filterFatherId != 0) {
            filterFatherId = 0;
            childID = paternitySvc.FindChildPaternitySecret(akActorId, 0, std::string(childName));
        }
        if (childID == 0) {
            MARAS_LOG_WARN("[SkyrimNetActionCoordinator] RevealChildSecret: no child with active paternity secret for mother {} ({:08X}) (childName='{}')",
                           ActorSvc::GetName(akActorId), akActorId, childName);
            return false;
        }

        if (directive.isPregnancyOnly) {
            if (!paternitySvc.IsPregnancySecretForPartner(childID)) {
                MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] RevealChildSecret: ignoring pregnancy_only reveal for {:08X}; partner already aware",
                                childID);
                return false;
            }
            SuppressSkyrimNetActionNarrationIfAvailable();
            const bool ok = paternitySvc.RevealPregnancyToPartner(childID, /*byVisibleBelly=*/false, /*sendEvent=*/true);
            if (directive.isBoth) {
                (void)paternitySvc.ResolvePaternitySecretForBioFather(childID, /*discoveredByBioFather=*/false, /*sendEvent=*/true);
            }
            return ok;
        }

        auto resolveOne = [&](RE::FormID id) -> bool {
            const RE::FormID bioFatherId = lineageSvc.GetBioFather(id);
            const RE::FormID presumedFatherId = lineageSvc.GetMotherPartner(id);
            const std::string bioFatherName = bioFatherId != 0 ? ActorSvc::GetName(bioFatherId) : std::string{};
            const std::string presumedFatherName = presumedFatherId != 0 ? ActorSvc::GetName(presumedFatherId) : std::string{};

            switch (Domain::Lineage::ClassifyPaternityRevealTarget(
                directive,
                Foundation::ActorId{ targetFatherId },
                targetFather,
                Foundation::ActorId{ bioFatherId },
                bioFatherName,
                Foundation::ActorId{ presumedFatherId },
                presumedFatherName)) {
                case Domain::Lineage::PaternityRevealTarget::BioFather:
                    return paternitySvc.ResolvePaternitySecretForBioFather(id, /*discoveredByBioFather=*/false, /*sendEvent=*/true);
                case Domain::Lineage::PaternityRevealTarget::PresumedPartner:
                    return paternitySvc.ResolvePaternitySecretForPartner(id, /*discoveredByPartner=*/false, /*sendEvent=*/true);
                case Domain::Lineage::PaternityRevealTarget::Both:
                default:
                    return paternitySvc.ResolvePaternitySecret(id, /*discoveredByPartner=*/false, /*sendEvent=*/true);
            }
        };

        SuppressSkyrimNetActionNarrationIfAvailable();
        const bool ok = resolveOne(childID);

        if (directive.resolveAllMatches) {
            std::unordered_set<RE::FormID> visited{ childID };
            const auto candidates = paternitySvc.GetChildrenWithActiveSecretsForActor(akActorId);
            for (const RE::FormID candId : candidates) {
                if (candId == 0 || !visited.insert(candId).second) {
                    continue;
                }
                if (filterFatherId != 0 &&
                    lineageSvc.GetBioFather(candId) != filterFatherId &&
                    lineageSvc.GetMotherPartner(candId) != filterFatherId) {
                    continue;
                }
                (void)resolveOne(candId);
            }
        }

        return ok;
    }

    bool SkyrimNetActionCoordinator::DiscoverChildPaternity(
        RE::Actor* akActor,
        std::string_view childName,
        std::string_view motherName) {
        if (!akActor) {
            return false;
        }

        const RE::FormID akActorId = akActor->GetFormID();
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] DiscoverChildPaternity: {} ({:08X}) child='{}' mother='{}'",
                        ActorSvc::GetName(akActorId), akActorId, childName, motherName);

        RE::Actor* motherActor = !motherName.empty() ? ResolveActorByName(motherName) : nullptr;
        RE::FormID motherId = motherActor ? motherActor->GetFormID() : 0;

        auto& paternitySvc = PaternityWorkflowCoordinator::GetSingleton();
        auto& lineageSvc = LineageUseCases::GetSingleton();

        RE::FormID childID = paternitySvc.FindChildPaternitySecret(akActorId, motherId, std::string(childName));
        if (childID == 0 && motherId != 0) {
            childID = paternitySvc.FindChildPaternitySecret(akActorId, 0, std::string(childName));
            if (childID != 0) {
                motherId = lineageSvc.GetBioMother(childID);
            }
        }
        if (childID == 0) {
            MARAS_LOG_WARN("[SkyrimNetActionCoordinator] DiscoverChildPaternity: no child with active paternity secret for father {} ({:08X}) (childName='{}')",
                           ActorSvc::GetName(akActorId), akActorId, childName);
            return false;
        }

        if (motherId == 0) {
            motherId = lineageSvc.GetBioMother(childID);
        }

        auto discoverOne = [&](RE::FormID id) -> bool {
            const RE::FormID presumedId = lineageSvc.GetMotherPartner(id);
            const RE::FormID bioId = lineageSvc.GetBioFather(id);
            const bool isSecPartner = paternitySvc.IsSecretForPartner(id);

            switch (Domain::Lineage::ClassifyPaternityDiscoveryTarget(
                Foundation::ActorId{ akActorId },
                Foundation::ActorId{ presumedId },
                Foundation::ActorId{ bioId },
                isSecPartner)) {
                case Domain::Lineage::PaternityDiscoveryTarget::PresumedPartner:
                    return paternitySvc.ResolvePaternitySecretForPartner(id, /*discoveredByPartner=*/true, /*sendEvent=*/true);
                case Domain::Lineage::PaternityDiscoveryTarget::BioFather:
                default:
                    return paternitySvc.ResolvePaternitySecretForBioFather(id, /*discoveredByBioFather=*/true, /*sendEvent=*/true);
            }
        };

        SuppressSkyrimNetActionNarrationIfAvailable();
        const bool ok = discoverOne(childID);

        if (StringUtils::CaseInsensitiveEquals(StringUtils::TrimView(childName), "all")) {
            std::unordered_set<RE::FormID> visited{ childID };
            const auto candidates = paternitySvc.GetChildrenWithActiveSecretsForActor(akActorId);
            for (const RE::FormID candId : candidates) {
                if (candId == 0 || !visited.insert(candId).second) {
                    continue;
                }
                if (motherId != 0 && lineageSvc.GetBioMother(candId) != motherId) {
                    continue;
                }
                (void)discoverOne(candId);
            }
        }

        return ok;
    }

    bool SkyrimNetActionCoordinator::ConfessAffair(
        RE::Actor* akActor,
        std::string_view victimName,
        std::string_view affairPartnerName,
        std::string_view reasoning) {
        if (!akActor) {
            return false;
        }

        const RE::FormID akActorId = akActor->GetFormID();
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ConfessAffair: {} ({:08X}) confessing '{}' to '{}' reasoning='{}'",
                        ActorSvc::GetName(akActorId), akActorId, affairPartnerName, victimName, reasoning);

        RE::Actor* victim = ResolveActorByName(victimName);
        RE::Actor* affairPartner = ResolveActorByName(affairPartnerName);
        const RE::FormID victimId = victim ? victim->GetFormID() : 0;
        const RE::FormID affairPartnerId = affairPartner ? affairPartner->GetFormID() : 0;

        SuppressSkyrimNetActionNarrationIfAvailable();
        const bool success = RelationshipUseCases::GetSingleton().SetCheatingDiscovered(
            akActorId, affairPartnerId, "confessed", victimId);
        if (!success) {
            MARAS_LOG_WARN("[SkyrimNetActionCoordinator] ConfessAffair: SetCheatingDiscovered failed (cheater={:08X}, partner={:08X}, victim={:08X})",
                           akActorId, affairPartnerId, victimId);
        }
        return success;
    }

    bool SkyrimNetActionCoordinator::ExposeAffair(
        RE::Actor* akActor,
        std::string_view victimName,
        std::string_view cheaterName,
        std::string_view reasoning) {
        if (!akActor) {
            return false;
        }

        const RE::FormID akActorId = akActor->GetFormID();
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ExposeAffair: {} ({:08X}) exposing '{}' to '{}' reasoning='{}'",
                        ActorSvc::GetName(akActorId), akActorId, cheaterName, victimName, reasoning);

        RE::Actor* victim = ResolveActorByName(victimName);
        RE::Actor* cheater = ResolveActorByName(cheaterName);
        const RE::FormID victimId = victim ? victim->GetFormID() : 0;
        const RE::FormID cheaterId = cheater ? cheater->GetFormID() : 0;

        SuppressSkyrimNetActionNarrationIfAvailable();
        const bool success = RelationshipUseCases::GetSingleton().SetCheatingDiscovered(
            cheaterId, akActorId, "exposed", victimId);
        if (!success) {
            MARAS_LOG_WARN("[SkyrimNetActionCoordinator] ExposeAffair: SetCheatingDiscovered failed (cheater={:08X}, partner={:08X}, victim={:08X})",
                           cheaterId, akActorId, victimId);
        }
        return success;
    }

    bool SkyrimNetActionCoordinator::ResolveAffair(
        RE::Actor* akActor,
        std::string_view partnerName,
        std::string_view affairPartnerName,
        std::string_view resolution,
        std::string_view reasoning) {
        if (!akActor) {
            return false;
        }

        const RE::FormID akActorId = akActor->GetFormID();
        MARAS_LOG_DEBUG("[SkyrimNetActionCoordinator] ResolveAffair: {} ({:08X}) partner='{}' affairPartner='{}' res='{}' reasoning='{}'",
                        ActorSvc::GetName(akActorId), akActorId, partnerName, affairPartnerName, resolution, reasoning);

        RE::Actor* otherParty = ResolveActorByName(partnerName);
        RE::Actor* affairPartner = ResolveActorByName(affairPartnerName);
        const RE::FormID otherPartyId = otherParty ? otherParty->GetFormID() : 0;
        const RE::FormID affairPartnerId = affairPartner ? affairPartner->GetFormID() : 0;

        RE::FormID cheaterId = akActorId;
        RE::FormID victimId = otherPartyId;
        auto& relSvc = RelationshipUseCases::GetSingleton();
        if (otherPartyId != 0 && relSvc.HasCheatedOn(otherPartyId, akActorId)) {
            cheaterId = otherPartyId;
            victimId = akActorId;
        }

        SuppressSkyrimNetActionNarrationIfAvailable();
        const bool success = relSvc.SetCheatingDiscovered(cheaterId, affairPartnerId, resolution, victimId);
        if (!success) {
            MARAS_LOG_WARN("[SkyrimNetActionCoordinator] ResolveAffair: SetCheatingDiscovered failed (cheater={:08X}, partner={:08X}, victim={:08X}, res='{}')",
                           cheaterId, affairPartnerId, victimId, resolution);
        }
        return success;
    }

}  // namespace MARAS::App
