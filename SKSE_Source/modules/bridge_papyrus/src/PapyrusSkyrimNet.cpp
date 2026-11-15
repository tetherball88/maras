#include "maras/bridge_papyrus/PapyrusInterface.h"

#include "maras/app/AppContext.h"
#include "maras/app/BanterSceneCoordinator.h"
#include "maras/app/SkyrimNetActionCoordinator.h"
#include "maras/app/SkyrimNetGateway.h"
#include "maras/engine/TaskDispatcher.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MARAS::PapyrusInterface {

    namespace {

        void SuppressNextActionNarration(RE::StaticFunctionTag*, float durationSeconds) {
            App::GetSkyrimNetGateway().SuppressNextActionNarration(durationSeconds);
        }

        void DirectNarration(RE::StaticFunctionTag*,
                             std::string content,
                             RE::Actor* originatorActor,
                             RE::Actor* targetActor,
                             bool exactAudience,
                             std::vector<RE::Actor*> audience) {
            auto& gateway = App::GetSkyrimNetGateway();
            if (!gateway.IsAvailable()) {
                return;
            }
            std::vector<std::uint32_t> audienceIds;
            audienceIds.reserve(audience.size());
            for (RE::Actor* actor : audience) {
                if (actor) {
                    audienceIds.push_back(actor->GetFormID());
                }
            }
            gateway.EmitDirectNarration(
                content,
                originatorActor ? originatorActor->GetFormID() : 0,
                targetActor ? targetActor->GetFormID() : 0,
                exactAudience,
                audienceIds);
        }

        void RegisterPolygamyBlessing(RE::StaticFunctionTag*, RE::Actor* spouse) {
            auto& gateway = App::GetSkyrimNetGateway();
            if (spouse && gateway.IsAvailable()) {
                gateway.EmitPolygamyBlessing(spouse->GetFormID());
            }
        }

        void RegisterProposalDecision(RE::StaticFunctionTag*,
                                      RE::Actor* initiator,
                                      RE::Actor* acceptor,
                                      std::string status,
                                      std::string decision,
                                      bool skipTrigger) {
            auto& gateway = App::GetSkyrimNetGateway();
            if (initiator && acceptor && gateway.IsAvailable()) {
                gateway.EmitProposalDecision(
                    initiator->GetFormID(), acceptor->GetFormID(), status, decision, skipTrigger);
            }
        }

    }  // namespace

    RE::BSScript::LatentStatus ExecuteProposeRelationshipAction(
        RE::BSScript::Internal::VirtualMachine* vm,
        RE::VMStackID stackID,
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        RE::Actor* partner,
        std::string suggestedStatus) {
        if (!vm || !SKSE::GetTaskInterface()) {
            return RE::BSScript::LatentStatus::kFailed;
        }
        const auto expectedEpoch = MARAS::GetSessionEpoch();
        App::SkyrimNetActionCoordinator::GetSingleton().ProposeRelationshipChange(
            akActor,
            partner,
            std::move(suggestedStatus),
            [expectedEpoch, vm, stackID](bool ok) {
                MARAS::DispatchLatentResult<bool>(
                    expectedEpoch, vm, stackID, ok, "ExecuteProposeRelationshipAction");
            });
        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ExecuteConsiderRelationshipAction(
        RE::BSScript::Internal::VirtualMachine* vm,
        RE::VMStackID stackID,
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        RE::Actor* partner,
        std::string decision,
        std::string status,
        std::string reasoning) {
        if (!vm || !SKSE::GetTaskInterface()) {
            return RE::BSScript::LatentStatus::kFailed;
        }
        const auto expectedEpoch = MARAS::GetSessionEpoch();
        App::SkyrimNetActionCoordinator::GetSingleton().ConsiderRelationshipChange(
            akActor,
            partner,
            std::move(decision),
            std::move(status),
            std::move(reasoning),
            [expectedEpoch, vm, stackID](bool ok) {
                MARAS::DispatchLatentResult<bool>(
                    expectedEpoch, vm, stackID, ok, "ExecuteConsiderRelationshipAction");
            });
        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ExecuteBreakupRelationshipAction(
        RE::BSScript::Internal::VirtualMachine* vm,
        RE::VMStackID stackID,
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        std::string partnerName,
        std::string status,
        std::string reasoning) {
        if (!vm || !SKSE::GetTaskInterface()) {
            return RE::BSScript::LatentStatus::kFailed;
        }
        const auto expectedEpoch = MARAS::GetSessionEpoch();
        App::SkyrimNetActionCoordinator::GetSingleton().BreakupRelationship(
            akActor,
            std::move(partnerName),
            std::move(status),
            std::move(reasoning),
            [expectedEpoch, vm, stackID](bool ok) {
                MARAS::DispatchLatentResult<bool>(
                    expectedEpoch, vm, stackID, ok, "ExecuteBreakupRelationshipAction");
            });
        return RE::BSScript::LatentStatus::kStarted;
    }

    RE::BSScript::LatentStatus ExecuteSetOpenRelationshipAction(
        RE::BSScript::Internal::VirtualMachine* vm,
        RE::VMStackID stackID,
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        RE::Actor* partner,
        bool isOpen,
        std::string reasoning) {
        if (!vm || !SKSE::GetTaskInterface()) {
            return RE::BSScript::LatentStatus::kFailed;
        }
        const auto expectedEpoch = MARAS::GetSessionEpoch();
        App::SkyrimNetActionCoordinator::GetSingleton().SetOpenRelationshipChange(
            akActor,
            partner,
            isOpen,
            std::move(reasoning),
            [expectedEpoch, vm, stackID](bool ok) {
                MARAS::DispatchLatentResult<bool>(
                    expectedEpoch, vm, stackID, ok, "ExecuteSetOpenRelationshipAction");
            });
        return RE::BSScript::LatentStatus::kStarted;
    }

    bool ExecuteRevealChildSecretAction(
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        std::string targetFather,
        std::string childName,
        std::string revealMode) {
        return App::SkyrimNetActionCoordinator::GetSingleton().RevealChildSecret(
            akActor, targetFather, childName, revealMode);
    }

    bool ExecuteDiscoverChildPaternityAction(
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        std::string childName,
        std::string motherName) {
        return App::SkyrimNetActionCoordinator::GetSingleton().DiscoverChildPaternity(
            akActor, childName, motherName);
    }

    bool ExecuteConfessAffairAction(
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        std::string victimName,
        std::string affairPartnerName,
        std::string reasoning) {
        return App::SkyrimNetActionCoordinator::GetSingleton().ConfessAffair(
            akActor, victimName, affairPartnerName, reasoning);
    }

    bool ExecuteExposeAffairAction(
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        std::string victimName,
        std::string cheaterName,
        std::string reasoning) {
        return App::SkyrimNetActionCoordinator::GetSingleton().ExposeAffair(
            akActor, victimName, cheaterName, reasoning);
    }

    bool ExecuteResolveAffairAction(
        RE::StaticFunctionTag*,
        RE::Actor* akActor,
        std::string partnerName,
        std::string affairPartnerName,
        std::string resolution,
        std::string reasoning) {
        return App::SkyrimNetActionCoordinator::GetSingleton().ResolveAffair(
            akActor, partnerName, affairPartnerName, resolution, reasoning);
    }

    std::string PrepareBanterScene(
        RE::StaticFunctionTag*,
        RE::Actor* akNpc1,
        RE::Actor* akNpc2,
        std::string asForcedScenario,
        bool abHasSexFramework,
        bool abHasBedsNearby) {
        return App::BanterSceneCoordinator::GetSingleton().PrepareBanterScene(
            akNpc1, akNpc2, asForcedScenario, abHasSexFramework, abHasBedsNearby);
    }

    std::string PlanBanterCheatingFallback(
        RE::StaticFunctionTag*,
        RE::Actor* akNpc1,
        RE::Actor* akNpc2,
        bool abHasSexFramework,
        bool abHasBedsNearby) {
        return App::BanterSceneCoordinator::GetSingleton().PlanBanterCheatingFallback(
            akNpc1, akNpc2, abHasSexFramework, abHasBedsNearby);
    }

    int32_t PlanCaughtCheatingSceneIndex(
        RE::StaticFunctionTag*,
        RE::Actor* akCheater,
        RE::Actor* akAffairPartner) {
        return App::BanterSceneCoordinator::GetSingleton().PlanCaughtCheatingSceneIndex(
            akCheater, akAffairPartner);
    }

    void MuteBanterSceneParticipants(
        RE::StaticFunctionTag*,
        RE::Actor* akNpc1,
        RE::Actor* akNpc2,
        RE::Actor* akPartner,
        bool abShouldMutePartner) {
        App::BanterSceneCoordinator::GetSingleton().MuteBanterSceneParticipants(
            akNpc1, akNpc2, akPartner, abShouldMutePartner);
    }

    bool UnMuteBanterSceneParticipants(RE::StaticFunctionTag*) {
        return App::BanterSceneCoordinator::GetSingleton().UnMuteBanterSceneParticipants();
    }

    void RestoreMovedBanterSceneNpcs(RE::StaticFunctionTag*) {
        App::BanterSceneCoordinator::GetSingleton().RestoreMovedBanterSceneNpcs();
    }

    void RegisterPapyrusSkyrimNetFunctions(RE::BSScript::IVirtualMachine* vm) {
        if (!vm) {
            return;
        }
        RegisterSynced<SuppressNextActionNarration>(vm, "SuppressNextActionNarration");
        RegisterSynced<DirectNarration>(vm, "DirectNarration");
        RegisterSynced<RegisterPolygamyBlessing>(vm, "RegisterPolygamyBlessing");
        RegisterSynced<RegisterProposalDecision>(vm, "RegisterProposalDecision");
        RegisterLatentLocked<bool, ExecuteProposeRelationshipAction>(vm, "ExecuteProposeRelationshipAction");
        RegisterLatentLocked<bool, ExecuteConsiderRelationshipAction>(vm, "ExecuteConsiderRelationshipAction");
        RegisterLatentLocked<bool, ExecuteBreakupRelationshipAction>(vm, "ExecuteBreakupRelationshipAction");
        RegisterLatentLocked<bool, ExecuteSetOpenRelationshipAction>(vm, "ExecuteSetOpenRelationshipAction");
        RegisterSynced<ExecuteRevealChildSecretAction>(vm, "ExecuteRevealChildSecretAction");
        RegisterSynced<ExecuteDiscoverChildPaternityAction>(vm, "ExecuteDiscoverChildPaternityAction");
        RegisterSynced<ExecuteConfessAffairAction>(vm, "ExecuteConfessAffairAction");
        RegisterSynced<ExecuteExposeAffairAction>(vm, "ExecuteExposeAffairAction");
        RegisterSynced<ExecuteResolveAffairAction>(vm, "ExecuteResolveAffairAction");
        RegisterSynced<PrepareBanterScene>(vm, "PrepareBanterScene");
        RegisterSynced<PlanBanterCheatingFallback>(vm, "PlanBanterCheatingFallback");
        RegisterSynced<PlanCaughtCheatingSceneIndex>(vm, "PlanCaughtCheatingSceneIndex");
        RegisterSynced<MuteBanterSceneParticipants>(vm, "MuteBanterSceneParticipants");
        RegisterSynced<UnMuteBanterSceneParticipants>(vm, "UnMuteBanterSceneParticipants");
        RegisterSynced<RestoreMovedBanterSceneNpcs>(vm, "RestoreMovedBanterSceneNpcs");
    }

}  // namespace MARAS::PapyrusInterface
