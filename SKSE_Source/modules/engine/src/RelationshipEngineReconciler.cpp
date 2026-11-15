#include "maras/engine/RelationshipEngineReconciler.h"

#include "maras/engine/ActorService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/SessionEpoch.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace MARAS {

    namespace {
        void SetGlobalFloatIfChanged(RE::TESGlobal* global, float targetValue, const char* label) {
            if (!global) {
                MARAS_LOG_WARN("[RelationshipEngineReconciler] Missing TESGlobal '{}'", label);
                return;
            }
            if (std::fabs(global->value - targetValue) > 0.001f) {
                global->value = targetValue;
            }
        }

        void ApplyChanceGlobalsInline(const Domain::Relationships::NoAiDialogueChances& chances) {
            auto& fc = FormCache::GetSingleton();
            SetGlobalFloatIfChanged(fc.GetDatingChanceGlobal(), chances.datingChance, "DatingChance");
            SetGlobalFloatIfChanged(fc.GetLoverChanceGlobal(), chances.loverChance, "LoverChance");
            SetGlobalFloatIfChanged(fc.GetMarriageChanceGlobal(), chances.marriageChance, "MarriageChance");
        }

        void DispatchSetRelationshipRank(RE::FormID npcID, RE::FormID targetID, int rank) {
            if (npcID == 0 || targetID == 0) {
                return;
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* npcActor = RE::TESForm::LookupByID<RE::Actor>(npcID);
            auto* targetActor = RE::TESForm::LookupByID<RE::Actor>(targetID);
            if (!vm || !npcActor || npcActor->IsDeleted() || !targetActor) {
                return;
            }
            auto* policy = vm->GetObjectHandlePolicy1();
            if (!policy) {
                return;
            }
            const RE::VMHandle handle = policy->GetHandleForObject(RE::Actor::FORMTYPE, npcActor);
            if (handle == 0 || handle == policy->EmptyHandle()) {
                return;
            }
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> emptyCallback;
            auto* args = RE::MakeFunctionArguments(std::move(targetActor), static_cast<std::int32_t>(rank));
            if (!vm->DispatchMethodCall2(
                    handle,
                    RE::BSFixedString("Actor"),
                    RE::BSFixedString("SetRelationshipRank"),
                    args,
                    emptyCallback)) {
                MARAS_LOG_WARN("[RelationshipEngineReconciler] SetRelationshipRank dispatch failed ({:08X} -> {:08X}, rank={})",
                               npcID, targetID, rank);
            }
        }

        void ApplyPacifySpell(RE::Actor* npc, bool enable) {
            if (!npc || npc->IsDeleted()) {
                return;
            }
            auto* spell = FormCache::GetSingleton().GetPacifyFianceOrSpouseSpell();
            if (!spell) {
                return;
            }
            if (enable) {
                const float aggression = npc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);
                if (!npc->HasSpell(spell) && (aggression > 0.0f || aggression == -1.0f)) {
                    npc->AddSpell(spell);
                }
            } else {
                if (npc->HasSpell(spell)) {
                    npc->RemoveSpell(spell);
                }
            }
        }

        void QueueNotificationIfAllowed(bool sendNotifications, std::string msg) {
            if (!sendNotifications || State::IsLegacyMigrationActive() || msg.empty()) {
                return;
            }
            QueueHUDMessage(std::move(msg));
        }

        [[nodiscard]] std::string ResolveNotificationActorName(RE::FormID npcID) {
            std::string name = ActorSvc::GetName(npcID);
            return name;
        }
    }  // namespace

    RelationshipEngineReconciler& RelationshipEngineReconciler::GetSingleton() {
        static RelationshipEngineReconciler instance;
        return instance;
    }

    int RelationshipEngineReconciler::GetVanillaRelationshipRank(RE::FormID npcID, RE::FormID targetID) const {
        if (npcID == 0 || targetID == 0) {
            return 0;
        }
        auto* npcBase = ActorSvc::GetActorBase(npcID);
        auto* targetBase = ActorSvc::GetActorBase(targetID);
        if (!npcBase || !targetBase) {
            return 0;
        }
        auto* rel = RE::BGSRelationship::GetRelationship(npcBase, targetBase);
        if (!rel) {
            rel = RE::BGSRelationship::GetRelationship(targetBase, npcBase);
        }
        if (!rel) {
            return 0;
        }
        return 4 - static_cast<int>(rel->level.underlying());
    }

    void RelationshipEngineReconciler::SetVanillaRelationshipRank(
        RE::Actor* npc,
        RE::Actor* target,
        int rank) const {
        if (!npc || !target) {
            return;
        }
        const RE::FormID npcID = npc->GetFormID();
        const RE::FormID targetID = target->GetFormID();
        if (GetVanillaRelationshipRank(npcID, targetID) == rank) {
            return;
        }
        DispatchPostLock(
            GetSessionEpoch(),
            [npcID, targetID, rank]() {
                DispatchSetRelationshipRank(npcID, targetID, rank);
            },
            "RelationshipEngineReconciler::SetVanillaRelationshipRank");
    }

    void RelationshipEngineReconciler::PacifyFianceOrSpouse(RE::FormID npcID, bool enable) const {
        if (npcID == 0) {
            return;
        }
        DispatchPostLock(
            GetSessionEpoch(),
            [npcID, enable]() {
                if (auto* npc = RE::TESForm::LookupByID<RE::Actor>(npcID)) {
                    ApplyPacifySpell(npc, enable);
                }
            },
            "RelationshipEngineReconciler::PacifyFianceOrSpouse");
    }

    void RelationshipEngineReconciler::ReconcilePlayerStatusTransition(
        RE::FormID npcID,
        Foundation::RelationshipStatus newStatus,
        bool appliedBreakupCooldown,
        bool sendNotifications) const {
        if (npcID == 0 || npcID == Foundation::kPlayerRefRawId) {
            return;
        }

        const std::string actorName = ResolveNotificationActorName(npcID);

        switch (newStatus) {
            case Foundation::RelationshipStatus::Candidate:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_RegisterCandidate{" + actorName + "}");
                break;

            case Foundation::RelationshipStatus::Dating:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_Dating{" + actorName + "}");
                break;

            case Foundation::RelationshipStatus::Lover:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_Lover{" + actorName + "}");
                break;

            case Foundation::RelationshipStatus::Engaged:
                // Engagement notification is emitted via NotifyEngagementCeremonyStarted
                // when skip-wedding does not immediately advance Engaged -> Married.
                break;

            case Foundation::RelationshipStatus::Married:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_Married{" + actorName + "}");
                break;

            case Foundation::RelationshipStatus::Divorced:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_Divorce{" + actorName + "}");
                break;

            case Foundation::RelationshipStatus::Jilted:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_Jilted{" + actorName + "}");
                break;

            case Foundation::RelationshipStatus::ExDating:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_ExDating{" + actorName + "}");
                break;

            case Foundation::RelationshipStatus::ExLover:
                QueueNotificationIfAllowed(
                    sendNotifications, "$TTM_MSG_ExLover{" + actorName + "}");
                break;

            default:
                break;
        }

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        DispatchPostLock(
            expectedEpoch,
            [this, npcID, newStatus, appliedBreakupCooldown]() {
                auto* npc = RE::TESForm::LookupByID<RE::Actor>(npcID);
                if (!npc || npc->IsDeleted()) {
                    return;
                }
                auto& fc = FormCache::GetSingleton();
                constexpr RE::FormID playerID = Foundation::kPlayerRefRawId;

                switch (newStatus) {
                    case Foundation::RelationshipStatus::Dating:
                    case Foundation::RelationshipStatus::Lover:
                        if (GetVanillaRelationshipRank(npcID, playerID) < 3) {
                            DispatchSetRelationshipRank(npcID, playerID, 3);
                        }
                        break;

                    case Foundation::RelationshipStatus::Engaged:
                        DispatchSetRelationshipRank(npcID, playerID, 3);
                        if (auto* cooldownSpell = fc.GetBreakupCooldownSpell();
                            cooldownSpell && npc->HasSpell(cooldownSpell)) {
                            npc->RemoveSpell(cooldownSpell);
                        }
                        ApplyPacifySpell(npc, true);
                        break;

                    case Foundation::RelationshipStatus::Married:
                        DispatchSetRelationshipRank(npcID, playerID, 4);
                        if (auto* cooldownSpell = fc.GetBreakupCooldownSpell();
                            cooldownSpell && npc->HasSpell(cooldownSpell)) {
                            npc->RemoveSpell(cooldownSpell);
                        }
                        ApplyPacifySpell(npc, true);
                        break;

                    case Foundation::RelationshipStatus::Divorced:
                        DispatchSetRelationshipRank(npcID, playerID, -2);
                        if (appliedBreakupCooldown) {
                            if (auto* cooldownSpell = fc.GetBreakupCooldownSpell();
                                cooldownSpell && !npc->HasSpell(cooldownSpell)) {
                                npc->AddSpell(cooldownSpell);
                            }
                        }
                        ApplyPacifySpell(npc, false);
                        break;

                    case Foundation::RelationshipStatus::Jilted:
                    case Foundation::RelationshipStatus::ExDating:
                    case Foundation::RelationshipStatus::ExLover:
                        DispatchSetRelationshipRank(npcID, playerID, -1);
                        if (appliedBreakupCooldown) {
                            if (auto* cooldownSpell = fc.GetBreakupCooldownSpell();
                                cooldownSpell && !npc->HasSpell(cooldownSpell)) {
                                npc->AddSpell(cooldownSpell);
                            }
                        }
                        ApplyPacifySpell(npc, false);
                        break;

                    default:
                        break;
                }
            },
            "RelationshipEngineReconciler::ReconcilePlayerStatusTransition");
    }

    void RelationshipEngineReconciler::NotifyEngagementCeremonyStarted(RE::FormID npcID) const {
        if (npcID == 0 || State::IsLegacyMigrationActive()) {
            return;
        }
        const std::string actorName = ResolveNotificationActorName(npcID);
        QueueNotificationIfAllowed(true, "$TTM_MSG_Engagement{" + actorName + "}");
    }

    void RelationshipEngineReconciler::ReconcileHierarchyRankChange(RE::FormID spouseID, float rankDiff) const {
        if (spouseID == 0 || rankDiff == 0.0f || State::IsLegacyMigrationActive()) {
            return;
        }
        const std::string spouseName = ResolveNotificationActorName(spouseID);
        const char* prefix = (rankDiff > 0.0f) ? "$TTM_MSG_RankUp{" : "$TTM_MSG_RankDown{";
        QueueNotificationIfAllowed(true, std::string(prefix) + spouseName + "}");

        if (rankDiff < 0.0f) {
            DispatchPostLock(
                GetSessionEpoch(),
                [spouseID]() {
                    auto* spouse = RE::TESForm::LookupByID<RE::Actor>(spouseID);
                    auto* demotedSpell = FormCache::GetSingleton().GetDemotedCooldownSpell();
                    if (spouse && !spouse->IsDeleted() && demotedSpell && !spouse->HasSpell(demotedSpell)) {
                        spouse->AddSpell(demotedSpell);
                    }
                },
                "RelationshipEngineReconciler::AddDemotedCooldownSpell");
        }
    }

    void RelationshipEngineReconciler::NotifyAffectionThresholdChange(
        RE::FormID spouseID,
        std::string_view level,
        float affectionDiff) const {
        if (spouseID == 0 || affectionDiff == 0.0f || State::IsLegacyMigrationActive()) {
            return;
        }
        std::string_view keyPrefix;
        if (affectionDiff > 0.0f) {
            if (level == "happy") {
                keyPrefix = "$TTM_MSG_AffectionImproveHappy{";
            } else if (level == "content") {
                keyPrefix = "$TTM_MSG_AffectionImproveContent{";
            } else if (level == "troubled") {
                keyPrefix = "$TTM_MSG_AffectionImproveTroubled{";
            }
        } else {
            if (level == "content") {
                keyPrefix = "$TTM_MSG_AffectionDeclineContent{";
            } else if (level == "troubled") {
                keyPrefix = "$TTM_MSG_AffectionDeclineTroubled{";
            } else if (level == "estranged") {
                keyPrefix = "$TTM_MSG_AffectionDeclineEstranged{";
            }
        }
        if (keyPrefix.empty()) {
            return;
        }
        const std::string spouseName = ResolveNotificationActorName(spouseID);
        QueueNotificationIfAllowed(true, std::string(keyPrefix) + spouseName + "}");
    }

    void RelationshipEngineReconciler::ApplyNoAiDialogueChanceGlobals(
        const Domain::Relationships::NoAiDialogueChances& chances) const {
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddEpochTask(
                GetSessionEpoch(),
                "RelationshipEngineReconciler::ApplyNoAiDialogueChanceGlobals",
                [chances]() {
                    std::lock_guard lock(GetPapyrusMutex());
                    ApplyChanceGlobalsInline(chances);
                });
            return;
        }
        ApplyChanceGlobalsInline(chances);
    }

}  // namespace MARAS
