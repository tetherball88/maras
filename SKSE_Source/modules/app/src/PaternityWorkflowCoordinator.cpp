#include "maras/app/PaternityWorkflowCoordinator.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/AppContext.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/lineage/FertilityRules.h"
#include "maras/domain/lineage/LineageRules.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/ChildSpawnService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace MARAS::App {

    namespace {

        [[nodiscard]] float GetCurrentGameDaysOrZero() noexcept {
            if (auto* cal = RE::Calendar::GetSingleton()) {
                return cal->GetDaysPassed();
            }
            return 0.0f;
        }

        [[nodiscard]] bool IsMostRecentRelationshipDivorced(
            const State::WorldStateSnapshot& snap,
            RE::FormID actorA,
            RE::FormID actorB) noexcept {
            if (!snap.relationships || actorA == 0 || actorB == 0) {
                return false;
            }
            const auto* edge = snap.relationships->FindEdge(
                State::ActorId{ actorA }, State::ActorId{ actorB });
            if (!edge || edge->statusHistory.empty()) {
                return false;
            }
            return edge->statusHistory.back().toStatus == Foundation::RelationshipStatus::Divorced;
        }

        [[nodiscard]] bool IsChildActorForRelationshipCheck(
            const State::WorldStateSnapshot& snap,
            RE::FormID actorID) noexcept {
            if (actorID == 0 || Utils::MatchesActor(actorID, kPlayerFormID)) {
                return false;
            }
            if (IsSyntheticChildID(actorID)) {
                return true;
            }
            if (snap.lineage) {
                if (const auto* child = snap.lineage->FindChild(State::ChildId{ actorID });
                    child && child->stage != Foundation::ChildStage::Adult) {
                    return true;
                }
            }
            if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(actorID);
                entry.has_value() && entry->isChild) {
                return true;
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID)) {
                return actor->IsChild();
            }
            return false;
        }

        [[nodiscard]] bool CanFormStepInMarriage(
            const State::WorldStateSnapshot& snap,
            RE::FormID motherID,
            RE::FormID bioFatherID) {
            if (motherID == 0 || bioFatherID == 0 || bioFatherID == kUnknownFatherFormID) {
                return false;
            }
            if (Utils::MatchesActor(motherID, kPlayerFormID) ||
                Utils::MatchesActor(bioFatherID, kPlayerFormID)) {
                return false;
            }
            if (LineageUseCases::GetSingleton().IsDeceased(motherID) ||
                LineageUseCases::GetSingleton().IsDeceased(bioFatherID)) {
                return false;
            }
            if (RelationshipUseCases::GetSingleton().AreFamilyMembers(motherID, bioFatherID)) {
                return false;
            }

            const auto meta = LineageUseCases::CaptureWorldActorMetadata(
                std::make_shared<const State::WorldStateSnapshot>(snap), { motherID, bioFatherID });
            Domain::Relationships::RelationshipCheckContext ctx{};
            ctx.isChildA = IsChildActorForRelationshipCheck(snap, motherID);
            ctx.isChildB = IsChildActorForRelationshipCheck(snap, bioFatherID);
            return Domain::Relationships::CanFormRelationship(
                snap,
                *meta,
                State::ActorId{ motherID },
                State::ActorId{ bioFatherID },
                Foundation::RelationshipStatus::Married,
                ctx);
        }

        [[nodiscard]] bool IsActorDeadOrInvalidInEngine(RE::FormID actorID) {
            if (LineageUseCases::GetSingleton().IsDeceased(actorID)) {
                return true;
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID)) {
                return actor->IsDead() || actor->IsDeleted() || actor->IsDisabled();
            }
            return false;
        }

        struct StepInCandidate {
            RE::FormID bioFatherID{ 0 };
            bool isPregnancy{ false };
            std::string childName{};
            float childAgeDays{ 0.0f };
        };

        struct StepInEvalQueue {
            RE::FormID motherID{ 0 };
            RE::FormID formerSpouseID{ 0 };
            std::vector<StepInCandidate> candidates{};
            SessionEpoch epoch{ 0 };
        };

        void RunStepInCandidateAtIndex(std::shared_ptr<const StepInEvalQueue> queue, std::size_t index) {
            if (!queue || !IsSessionEpochCurrent(queue->epoch) || index >= queue->candidates.size()) {
                return;
            }

            const RE::FormID motherID = queue->motherID;
            const RE::FormID formerSpouseID = queue->formerSpouseID;
            const auto& cand = queue->candidates[index];

            {
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                if (!snap || !snap->relationships ||
                    IsMostRecentRelationshipDivorced(*snap, motherID, cand.bioFatherID) ||
                    !snap->relationships->GetPrimaryPartner(State::ActorId{ cand.bioFatherID }).IsNull() ||
                    !CanFormStepInMarriage(*snap, motherID, cand.bioFatherID)) {
                    RunStepInCandidateAtIndex(std::move(queue), index + 1);
                    return;
                }
            }

            if (!GetSkyrimNetGateway().IsAvailable()) {
                return;
            }

            const std::string motherName = ActorSvc::GetName(motherID);
            const std::string bioFatherName = ActorSvc::GetName(cand.bioFatherID);

            auto& skyrimNet = GetSkyrimNetGateway();
            const std::uint64_t motherUUID = skyrimNet.GetActorUUID(motherID);
            const std::uint64_t bioFatherUUID = skyrimNet.GetActorUUID(cand.bioFatherID);
            const std::uint64_t formerSpouseUUID = (formerSpouseID != 0) ? skyrimNet.GetActorUUID(formerSpouseID) : 0;

            nlohmann::json ctx;
            ctx["motherFormId"] = std::format("{:08X}", motherID);
            ctx["motherName"] = motherName;
            ctx["motherRace"] = ActorSvc::GetRaceName(motherID);
            ctx["motherUUID"] = motherUUID;

            ctx["bioFatherFormId"] = std::format("{:08X}", cand.bioFatherID);
            ctx["bioFatherName"] = bioFatherName;
            ctx["bioFatherRace"] = ActorSvc::GetRaceName(cand.bioFatherID);
            ctx["bioFatherUUID"] = bioFatherUUID;

            if (formerSpouseID != 0) {
                ctx["formerSpouseFormId"] = std::format("{:08X}", formerSpouseID);
                ctx["formerSpouseName"] = ActorSvc::GetName(formerSpouseID);
                ctx["formerSpouseRace"] = ActorSvc::GetRaceName(formerSpouseID);
                ctx["formerSpouseUUID"] = formerSpouseUUID;
            } else {
                ctx["formerSpouseFormId"] = "";
                ctx["formerSpouseName"] = "";
                ctx["formerSpouseRace"] = "";
                ctx["formerSpouseUUID"] = 0;
            }

            ctx["isPregnancy"] = cand.isPregnancy;
            ctx["childName"] = cand.childName;
            ctx["childAgeDays"] = static_cast<int>(std::round(cand.childAgeDays));
            nlohmann::json recentParticipants = nlohmann::json::array();
            if (motherUUID != 0) recentParticipants.push_back(motherUUID);
            if (bioFatherUUID != 0 && bioFatherUUID != motherUUID) recentParticipants.push_back(bioFatherUUID);
            if (formerSpouseUUID != 0 && formerSpouseUUID != motherUUID && formerSpouseUUID != bioFatherUUID) {
                recentParticipants.push_back(formerSpouseUUID);
            }
            ctx["recent_events_participants"] = std::move(recentParticipants);

            const std::string contextJson =
                ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

            const bool queued = GetSkyrimNetGateway().EvaluateStepIn(
                contextJson,
                [queue, index, motherID, motherName, bioFatherName, cand](const StepInEvalResult& res) {
                    if (!IsSessionEpochCurrent(queue->epoch)) {
                        return;
                    }
                    AddEpochTask(
                        queue->epoch,
                        "EvaluateBioFatherStepIn::OnResult",
                        [queue, index, motherID, motherName, bioFatherName, cand, res]() {
                            std::lock_guard lock(GetPapyrusMutex());
                            if (!IsSessionEpochCurrent(queue->epoch)) {
                                return;
                            }

                            auto& coordinator = PaternityWorkflowCoordinator::GetSingleton();
                            if (res.success) {
                                MARAS_LOG_INFO(
                                    "[SkyrimNet] Step-in evaluation for bio father {:08X} with mother {:08X} (usedDecisions={}): steppedIn={}, reason='{}'",
                                    cand.bioFatherID, motherID, res.usedDecisions, res.steppedIn, res.reason);
                                if (res.steppedIn) {
                                    coordinator.ApplyBioFatherStepIn(motherID, cand.bioFatherID);
                                } else {
                                    RunStepInCandidateAtIndex(queue, index + 1);
                                }
                                return;
                            }

                            MARAS_LOG_WARN(
                                "[SkyrimNet] Step-in evaluation failed for {:08X} & {:08X} (usedDecisions={}, detail='{}')",
                                cand.bioFatherID, motherID, res.usedDecisions, res.failureDetail);

                            ModalRequest req;
                            req.title = Tr("modal.paternity_step_in.title");
                            req.subtitle = Tr("modal.paternity_step_in.subtitle");
                            req.message = Tr("modal.paternity_step_in.message");
                            req.tone = ModalTone::Warning;
                            req.size = ModalSize::Md;
                            req.defaultCancelButtonIndex = 2;

                            const std::string childDesc = cand.isPregnancy
                                ? Tr("common.unborn_child_fallback")
                                : (cand.childName.empty() ? Tr("common.child_fallback") : cand.childName);
                            req.controls.push_back(ModalControl::MakeInfoBox(
                                "error_details",
                                Tr("modal.paternity_step_in.details_label"),
                                std::format("{} & {} ({})\n{}", bioFatherName, motherName, childDesc, res.failureDetail),
                                ModalTone::Warning));
                            auto& skyrimNet = GetSkyrimNetGateway();
                            req.controls.push_back(ModalControl::MakeInfoBox(
                                "error_guidance",
                                skyrimNet.GetLLMFailureGuidanceTitle(res.failureType),
                                skyrimNet.GetLLMFailureGuidance(
                                    res.failureType,
                                    res.usedDecisions ? "decisions" : "gamemaster_evaluation"),
                                ModalTone::Default));

                            req.buttons.push_back(ModalButton::Make("retry", Tr("modal.paternity_step_in.btn_retry"), ModalButtonVariant::Primary));
                            req.buttons.push_back(ModalButton::Make("dice_roll", Tr("modal.paternity_step_in.btn_dice_roll"), ModalButtonVariant::Secondary));
                            req.buttons.push_back(ModalButton::Make("skip", Tr("modal.paternity_step_in.btn_skip"), ModalButtonVariant::Ghost));

                            GetModalGateway().ShowModal(
                                std::move(req),
                                [queue, index, motherID, cand](const ModalResponse& modalResp) {
                                    std::lock_guard lock(GetPapyrusMutex());
                                    if (!IsSessionEpochCurrent(queue->epoch) || modalResp.aborted) {
                                        return;
                                    }
                                    if (modalResp.buttonIndex == 0 || modalResp.buttonId == "retry") {
                                        MARAS_LOG_INFO(
                                            "[SkyrimNet] Player chose to retry bio-father step-in LLM evaluation for {:08X} & {:08X}",
                                            cand.bioFatherID, motherID);
                                        RunStepInCandidateAtIndex(queue, index);
                                        return;
                                    }
                                    if (modalResp.buttonIndex == 1 || modalResp.buttonId == "dice_roll") {
                                        const float roll = Utils::RandomFloat(0.0f, 100.0f);
                                        const auto sSnap = State::WorldStore::GetSingleton().Snapshot();
                                        const float chance = sSnap && sSnap->settings
                                            ? static_cast<float>(sSnap->settings->paternityBioStepUpChance)
                                            : 50.0f;
                                        const bool fallbackStepIn = (roll < chance);
                                        MARAS_LOG_INFO(
                                            "[SkyrimNet] Player chose dice-roll fallback for bio-father step-in ({:08X} & {:08X}): {:.1f}% < {:.1f}% -> {}",
                                            cand.bioFatherID, motherID, roll, chance, fallbackStepIn);
                                        if (fallbackStepIn) {
                                            PaternityWorkflowCoordinator::GetSingleton().ApplyBioFatherStepIn(
                                                motherID, cand.bioFatherID);
                                        } else {
                                            RunStepInCandidateAtIndex(queue, index + 1);
                                        }
                                        return;
                                    }
                                    MARAS_LOG_INFO(
                                        "[SkyrimNet] Player skipped bio-father step-in evaluation for {:08X} & {:08X}",
                                        cand.bioFatherID, motherID);
                                    RunStepInCandidateAtIndex(queue, index + 1);
                                });
                        });
                });

            if (!queued) {
                MARAS_LOG_WARN(
                    "[SkyrimNet] Failed to dispatch bio-father step-in evaluation for {:08X} & {:08X}; advancing queue",
                    cand.bioFatherID, motherID);
                AddEpochTask(queue->epoch, "EvaluateBioFatherStepIn::AdvanceOnDispatchFailure", [queue, index]() {
                    std::lock_guard lock(GetPapyrusMutex());
                    RunStepInCandidateAtIndex(queue, index + 1);
                });
            }
        }

        void NotifyUpdatedChildrenUI(
            const std::vector<State::ChildId>& affectedChildren,
            bool requireDashboardOpen) {
            if (affectedChildren.empty()) {
                return;
            }
            auto& ui = GetUiBridgeGateway();
            if (requireDashboardOpen && !ui.IsDashboardOpen()) {
                return;
            }
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            if (!snap || !snap->lineage) {
                return;
            }
            for (const State::ChildId cid : affectedChildren) {
                if (const auto* rec = snap->lineage->FindChild(cid)) {
                    ui.OnChildUpdated(*rec);
                }
            }
        }

    }  // namespace

    PaternityWorkflowCoordinator& PaternityWorkflowCoordinator::GetSingleton() {
        static PaternityWorkflowCoordinator instance;
        return instance;
    }

    // =========================================================================
    // Lifecycle & Scheduled Polling
    // =========================================================================

    void PaternityWorkflowCoordinator::FinalizeLoad() {
        // Co-save unborn & secret tables live inside WorldStore; no external transient cache needed.
        MARAS_LOG_INFO("[Fertility] PaternityWorkflowCoordinator::FinalizeLoad completed");
    }

    void PaternityWorkflowCoordinator::FinalizeCoSaveLoad(bool /*loadedAny*/) {
        FinalizeLoad();
    }

    void PaternityWorkflowCoordinator::PollFertilityTick(float /*currentGameDay*/) {
        // Reserved per-poll hook for fertility checks; daily rolls occur in ProcessDailyTick.
    }

    void PaternityWorkflowCoordinator::ProcessDailyTick(float currentGameDay) {
        CheckPaternitySecretsDailyTick(currentGameDay);
    }

    void PaternityWorkflowCoordinator::ProcessDayChange(float currentGameDay) {
        ProcessDailyTick(currentGameDay);
    }

    void PaternityWorkflowCoordinator::Revert() {
        // Pending async callbacks check GetSessionEpoch() automatically and drop stale continuations.
        MARAS_LOG_INFO("[Fertility] PaternityWorkflowCoordinator reverted");
    }

    void PaternityWorkflowCoordinator::CheckPaternitySecretsDailyTick(float currentDay) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage || !snap->settings) {
            return;
        }

        const auto& settings = *snap->settings;
        auto rng = LineageUseCases::MakeEngineRng();
        std::vector<RE::FormID> childrenToEvaluate;

        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            if (rec.stage == Foundation::ChildStage::Unborn || rec.birthTimestamp <= 0.0f) {
                continue;
            }
            if (!rec.paternitySecret.HasActiveSecret() || rec.paternitySecret.discovered) {
                continue;
            }
            if (Domain::Lineage::EvaluatePaternitySecretDailyRoll(rec, settings, currentDay, rng)) {
                MARAS_LOG_INFO(
                    "[Paternity] Daily discovery roll succeeded for child {:08X} (age: {:.1f} days)",
                    cIdKey.raw(),
                    currentDay - rec.birthTimestamp);
                childrenToEvaluate.push_back(cIdKey.raw());
            }
        }

        if (!childrenToEvaluate.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                for (const RE::FormID evalID : childrenToEvaluate) {
                    if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ evalID })) {
                        rec->paternitySecret.lastCheckTimestamp = currentDay;
                    }
                }
            });
        }

        for (const RE::FormID evalID : childrenToEvaluate) {
            EvaluatePaternitySecret(evalID);
        }
    }

    // =========================================================================
    // Fertility Mode Reloaded (FMR) Unborn & Infant Lifecycle
    // =========================================================================

    void PaternityWorkflowCoordinator::SetNPCPregnant(
        RE::FormID motherID,
        bool pregnant,
        RE::FormID bioFatherID,
        RE::FormID /*presumedFatherID*/,
        float conceptionTime) {
        if (motherID == 0) {
            return;
        }
        if (pregnant) {
            if (FindUnbornChild(motherID) == 0) {
                RegisterUnbornChild(motherID, bioFatherID, conceptionTime);
            }
        } else {
            RemoveUnbornChild(motherID);
        }
    }

    bool PaternityWorkflowCoordinator::IsNPCPregnant(RE::FormID motherID) const {
        if (motherID == 0) {
            return false;
        }
        if (ActorSvc::GetSex(motherID) == 0) {
            return false;
        }
        return FindUnbornChild(motherID) != 0;
    }

    std::vector<RE::FormID> PaternityWorkflowCoordinator::GetUnbornChildren(RE::FormID motherID) const {
        std::vector<RE::FormID> result;
        if (motherID == 0 || ActorSvc::GetSex(motherID) == 0) {
            return result;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return result;
        }
        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            if (rec.stage == Foundation::ChildStage::Unborn &&
                Utils::MatchesActor(rec.bioMother.raw(), motherID)) {
                result.push_back(cIdKey.raw());
            }
        }
        return result;
    }

    RE::FormID PaternityWorkflowCoordinator::FindUnbornChild(RE::FormID motherID) const {
        const auto unborn = GetUnbornChildren(motherID);
        return unborn.empty() ? 0 : unborn.front();
    }

    RE::FormID PaternityWorkflowCoordinator::RegisterUnbornChild(
        RE::FormID motherID,
        RE::FormID bioFatherID,
        float conceptionTime,
        bool evaluateStepIn) {
        if (motherID == 0) {
            return 0;
        }

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        const RE::FormID presumedFatherID = preSnap && preSnap->relationships
            ? preSnap->relationships->GetPrimaryPartner(State::ActorId{ motherID }).raw()
            : 0;
        if (bioFatherID == 0) {
            bioFatherID = (presumedFatherID != 0) ? presumedFatherID : kUnknownFatherFormID;
        }

        auto& relUseCases = RelationshipUseCases::GetSingleton();
        relUseCases.EnsureRegistered(motherID);
        if (presumedFatherID != 0) {
            relUseCases.EnsureRegistered(presumedFatherID);
        }
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) {
            relUseCases.EnsureRegistered(bioFatherID);
        }

        const float cTime = (conceptionTime > 0.0f) ? conceptionTime : GetCurrentGameDaysOrZero();
        const std::string motherRaceEdid = ActorSvc::GetRaceEditorID(motherID);
        const std::string presumedRaceEdid =
            presumedFatherID != 0 ? ActorSvc::GetRaceEditorID(presumedFatherID) : std::string{};
        const std::string bioRaceEdid =
            (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID)
                ? ActorSvc::GetRaceEditorID(bioFatherID)
                : std::string{};

        Domain::Lineage::UnbornChildRegistrationInput input{};
        input.bioMother = State::ActorId{ motherID };
        input.bioFather = State::ActorId{ bioFatherID };
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) {
            input.bioFatherName = ActorSvc::GetName(bioFatherID);
            if (auto* race = ActorSvc::GetRace(bioFatherID)) {
                input.bioFatherRaceId = State::FormRawId{ race->GetFormID() };
            }
        }
        input.motherRaceEditorId = motherRaceEdid;
        input.presumedFatherRaceEditorId = presumedRaceEdid;
        input.bioFatherRaceEditorId = bioRaceEdid;
        input.motherIsWerewolf = ChildSpawnService::IsActorWerewolf(motherID);
        input.bioFatherIsWerewolf =
            (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID)
                ? ChildSpawnService::IsActorWerewolf(bioFatherID)
                : false;
        input.conceptionTimestamp = cTime;

        const auto metaSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto meta = LineageUseCases::CaptureWorldActorMetadata(
            metaSnap, { motherID, presumedFatherID, bioFatherID });

        Domain::Lineage::UnbornChildRegistrationOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyRegisterUnbornChild(ws, input, *meta);
        });

        const RE::FormID childID = outcome.childId.raw();
        if (childID == 0) {
            return 0;
        }

        auto& factions = FactionReconciler::GetSingleton();
        for (const State::ActorId dirty : outcome.dirtyActors) {
            if (!dirty.IsNull() && !dirty.IsPlayer()) {
                factions.ReconcileActor(dirty.raw());
            }
        }

        const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = postSnap && postSnap->lineage
                ? postSnap->lineage->FindChild(outcome.childId)
                : nullptr) {
            MARAS_LOG_INFO(
                "[Fertility] Registered Unborn child {:08X} ('{}') for mother {:08X}. Presumed: {:08X}, GuardianB: {:08X}, Bio: {:08X}, Secret: {}",
                childID,
                rec->name,
                motherID,
                rec->presumedFather.raw(),
                rec->guardians.slotB.raw(),
                rec->bioFather.raw(),
                rec->paternitySecret.isSecret);
            GetUiBridgeGateway().OnChildUpdated(*rec);
        }

        const RE::FormID finalPresumed = outcome.presumedFather.raw();
        if (evaluateStepIn && finalPresumed == 0 && postSnap && postSnap->relationships) {
            const bool bioHasPartner =
                (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) &&
                !postSnap->relationships->GetPrimaryPartner(State::ActorId{ bioFatherID }).IsNull();
            const bool singleEligibleFather =
                bioFatherID != 0 && bioFatherID != kUnknownFatherFormID &&
                !Utils::MatchesActor(motherID, kPlayerFormID) &&
                !Utils::MatchesActor(bioFatherID, kPlayerFormID) &&
                !bioHasPartner &&
                !IsMostRecentRelationshipDivorced(*postSnap, motherID, bioFatherID) &&
                CanFormStepInMarriage(*postSnap, motherID, bioFatherID);
            if (singleEligibleFather) {
                MARAS_LOG_INFO(
                    "[Fertility] Mother {:08X} and Bio Father {:08X} have no official partners. Triggering Step-In evaluation for pregnancy.",
                    motherID, bioFatherID);
                EvaluateBioFatherStepIn(motherID, 0);
            } else if (GetUnbornChildren(motherID).size() > 1) {
                EvaluateBioFatherStepIn(motherID, 0);
            }
        }

        return childID;
    }

    RE::FormID PaternityWorkflowCoordinator::PromoteUnbornToInfant(
        RE::FormID motherID,
        float birthTime,
        const std::string& name,
        RE::FormID bioFatherID) {
        if (motherID == 0) {
            return 0;
        }

        const auto unbornIDs = GetUnbornChildren(motherID);
        if (unbornIDs.empty()) {
            MARAS_LOG_WARN("[Fertility] PromoteUnbornToInfant: No unborn child found for mother {:08X}", motherID);
            return 0;
        }

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        RE::FormID resolvedFatherForLog = bioFatherID;
        if (bioFatherID != 0 && preSnap && preSnap->lineage) {
            bool matched = false;
            for (const RE::FormID candidate : unbornIDs) {
                const auto* rec = preSnap->lineage->FindChild(State::ChildId{ candidate });
                if (rec && Utils::MatchesActor(rec->bioFather.raw(), bioFatherID)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                MARAS_LOG_WARN(
                    "[Fertility] PromoteUnbornToInfant: No unborn child of mother {:08X} fathered by {:08X}, promoting first unborn instead",
                    motherID, bioFatherID);
            }
        }

        if (resolvedFatherForLog == 0 && preSnap && preSnap->lineage) {
            if (const auto* firstRec = preSnap->lineage->FindChild(State::ChildId{ unbornIDs.front() })) {
                resolvedFatherForLog = firstRec->bioFather.raw();
            }
        }

        const float bTime = (birthTime > 0.0f) ? birthTime : GetCurrentGameDaysOrZero();
        const bool motherWW = ChildSpawnService::IsActorWerewolf(motherID);
        const bool fatherWW =
            (resolvedFatherForLog != 0 && resolvedFatherForLog != kUnknownFatherFormID)
                ? ChildSpawnService::IsActorWerewolf(resolvedFatherForLog)
                : false;

        const auto meta = LineageUseCases::CaptureWorldActorMetadata(
            preSnap, { motherID, resolvedFatherForLog });
        auto rng = LineageUseCases::MakeEngineRng();

        Domain::Lineage::PromoteUnbornOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyPromoteUnbornToInfant(
                ws,
                State::ActorId{ motherID },
                bTime,
                name,
                State::ActorId{ bioFatherID },
                motherWW,
                fatherWW,
                *meta,
                rng);
        });

        if (!outcome.promoted || outcome.childId.IsNull()) {
            return 0;
        }

        const RE::FormID childID = outcome.childId.raw();
        LineageUseCases::GetSingleton().EvaluateChildBioLineageAtBirth(childID);

        auto& factions = FactionReconciler::GetSingleton();
        for (const State::ActorId dirty : outcome.dirtyActors) {
            if (!dirty.IsNull() && !dirty.IsPlayer()) {
                factions.ReconcileActor(dirty.raw());
            }
        }
        LineageUseCases::GetSingleton().UpdateChildBothParentsFaction(childID);

        const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* postRec = postSnap && postSnap->lineage
                ? postSnap->lineage->FindChild(outcome.childId)
                : nullptr) {
            MARAS_LOG_INFO(
                "[Fertility] Promoted Unborn child {:08X} to Infant ('{}') for mother {:08X}. Birth: {:.2f}, SecretPartner: {}, SecretBio: {}",
                childID,
                postRec->name,
                motherID,
                bTime,
                postRec->paternitySecret.isSecretForPartner,
                postRec->paternitySecret.isSecretForBioFather);
            GetUiBridgeGateway().OnChildUpdated(*postRec);
        }

        if (outcome.evaluateAtBirth) {
            MARAS_LOG_INFO(
                "[Fertility] Phenotype mismatch noticed at birth for child {:08X}. Triggering paternity secret evaluation.",
                childID);
            EvaluatePaternitySecret(childID);
        }

        return childID;
    }

    bool PaternityWorkflowCoordinator::RemoveUnbornChild(RE::FormID motherID, RE::FormID bioFatherID) {
        if (motherID == 0) {
            return false;
        }
        if (GetUnbornChildren(motherID).empty()) {
            return false;
        }

        std::vector<State::ActorId> affectedGuardians;
        std::vector<State::ChildId> removedIds;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            removedIds = Domain::Lineage::ApplyRemoveUnbornChild(
                ws,
                State::ActorId{ motherID },
                State::ActorId{ bioFatherID },
                &affectedGuardians);
            if (ws.lineage->GetUnbornOfMother(State::ActorId{ motherID }).empty()) {
                ws.lineage.Write().EraseActiveDeliveryBatch(State::ActorId{ motherID });
            }
        });

        if (removedIds.empty()) {
            if (bioFatherID != 0) {
                MARAS_LOG_WARN(
                    "[Fertility] RemoveUnbornChild: no unborn child of mother {:08X} fathered by {:08X}",
                    motherID, bioFatherID);
            }
            return false;
        }

        auto& lineage = LineageUseCases::GetSingleton();
        for (const State::ActorId gid : affectedGuardians) {
            if (!gid.IsNull()) {
                lineage.UpdateSpouseChildrenFaction(gid.raw());
            }
        }
        for (const State::ChildId cid : removedIds) {
            if (bioFatherID != 0) {
                MARAS_LOG_INFO(
                    "[Fertility] Removed Unborn child {:08X} fathered by {:08X} for mother {:08X} (stillbirth)",
                    cid.raw(), bioFatherID, motherID);
            } else {
                MARAS_LOG_INFO(
                    "[Fertility] Removed Unborn child {:08X} for mother {:08X} (pregnancy cleared/miscarried)",
                    cid.raw(), motherID);
            }
            GetUiBridgeGateway().OnChildRemoved(cid.raw());
        }

        return true;
    }

    RE::FormID PaternityWorkflowCoordinator::RegisterFertilityChild(
        RE::FormID syntheticChildID,
        RE::FormID motherID,
        RE::FormID presumedFatherID,
        RE::FormID bioFatherID,
        float conceptionTime,
        float birthTime,
        const std::string& name) {
        if (motherID == 0) {
            return 0;
        }

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        if (presumedFatherID == 0 && preSnap && preSnap->relationships) {
            presumedFatherID = preSnap->relationships->GetPrimaryPartner(State::ActorId{ motherID }).raw();
        }
        if (bioFatherID == 0) {
            bioFatherID = presumedFatherID;
        }

        std::string childName = name;
        if (childName.empty()) {
            childName = std::format("{}'s Child", ActorSvc::GetName(motherID));
        }

        const float bTime = (birthTime > 0.0f) ? birthTime : GetCurrentGameDaysOrZero();

        if (syntheticChildID == 0 && preSnap && preSnap->lineage) {
            for (const auto& [cIdKey, rec] : preSnap->lineage->AllChildren()) {
                if (rec.stage != Foundation::ChildStage::Infant) {
                    continue;
                }
                if (!Utils::MatchesActor(rec.bioMother.raw(), motherID) ||
                    !Utils::MatchesActor(rec.bioFather.raw(), bioFatherID)) {
                    continue;
                }
                if (!Foundation::StringUtils::CaseInsensitiveEquals(rec.name, childName)) {
                    continue;
                }
                if (std::floor(rec.birthTimestamp) != std::floor(bTime)) {
                    continue;
                }
                MARAS_LOG_WARN(
                    "[Fertility] Infant {:08X} ('{}') already registered for mother {:08X} on this birth day. Skipping duplicate birth registration.",
                    cIdKey.raw(), rec.name, motherID);
                return cIdKey.raw();
            }
        }

        auto& relUseCases = RelationshipUseCases::GetSingleton();
        relUseCases.EnsureRegistered(motherID);
        if (presumedFatherID != 0) {
            relUseCases.EnsureRegistered(presumedFatherID);
        }
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) {
            relUseCases.EnsureRegistered(bioFatherID);
        }

        const float cTime = (conceptionTime > 0.0f) ? conceptionTime : bTime;
        const bool motherWW = ChildSpawnService::IsActorWerewolf(motherID);
        const bool fatherWW =
            (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID)
                ? ChildSpawnService::IsActorWerewolf(bioFatherID)
                : false;
        const bool hasMismatch =
            (presumedFatherID != 0 && bioFatherID != 0 && !Utils::MatchesActor(bioFatherID, presumedFatherID))
                ? HasPhenotypeMismatch(presumedFatherID, bioFatherID)
                : false;

        std::string bioFatherName;
        State::FormRawId bioFatherRaceId{};
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) {
            bioFatherName = ActorSvc::GetName(bioFatherID);
            if (auto* r = ActorSvc::GetRace(bioFatherID)) {
                bioFatherRaceId = State::FormRawId{ r->GetFormID() };
            }
        }

        const auto meta = LineageUseCases::CaptureWorldActorMetadata(
            preSnap, { motherID, presumedFatherID, bioFatherID });
        auto rng = LineageUseCases::MakeEngineRng();

        RE::FormID assignedChildID = syntheticChildID;
        State::WorldMutationResult birthMut{};

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (assignedChildID == 0) {
                assignedChildID = ws.lineage.Write().AllocateSyntheticChildId().raw();
            }
            if (assignedChildID == 0) {
                return;
            }

            State::ChildRecord rec{};
            rec.childId = State::ChildId{ assignedChildID };
            rec.name = childName;
            rec.timestamp = static_cast<std::int32_t>(bTime);
            rec.isAdopted = false;
            rec.isBiologicalChild = true;
            rec.bioMother = State::ActorId{ motherID };
            rec.bioFather = State::ActorId{ (bioFatherID != 0) ? bioFatherID : presumedFatherID };
            rec.conceptionTimestamp = cTime;
            rec.birthTimestamp = bTime;
            rec.stage = Foundation::ChildStage::Infant;
            rec.gender = 2;

            const State::ActorId guardianB =
                (presumedFatherID != 0 && ws.IsMarried(State::ActorId{ motherID }, State::ActorId{ presumedFatherID }))
                    ? State::ActorId{ presumedFatherID }
                    : State::ActorId{};
            rec.guardians.Assign(State::ActorId{ motherID }, guardianB);

            if (presumedFatherID != 0 && bioFatherID != 0 && !Utils::MatchesActor(bioFatherID, presumedFatherID)) {
                rec.presumedFather = State::ActorId{ presumedFatherID };
                rec.paternitySecret.isSecret = true;
                rec.paternitySecret.isSecretForPartner = true;
                rec.paternitySecret.isSecretForBioFather = true;
                rec.paternitySecret.discovered = false;
                rec.paternitySecret.discoveredByPartner = false;
                rec.paternitySecret.discoveredByBioFather = false;
                rec.paternitySecret.lastCheckTimestamp = bTime;
                rec.paternitySecret.bioFatherName = std::move(bioFatherName);
                rec.paternitySecret.bioFatherRaceId = bioFatherRaceId;
                rec.paternitySecret.hasPhenotypeMismatch = hasMismatch;
            }

            Domain::Lineage::EvaluateChildBeastbloodInheritance(rec, motherWW, fatherWW);
            ws.lineage.Write().UpsertChild(rec);
            birthMut = ws.ApplyBirthCustodyRules(
                State::ChildId{ assignedChildID },
                State::ActorId{ motherID },
                rec.presumedFather,
                rec.bioFather,
                *meta,
                rng);
        });

        if (assignedChildID == 0) {
            MARAS_LOG_ERROR("[Fertility] Synthetic child FormID pool exhausted during RegisterFertilityChild!");
            return kSyntheticChildIDMax;
        }

        auto& lineage = LineageUseCases::GetSingleton();
        lineage.EvaluateChildBioLineageAtBirth(assignedChildID);

        lineage.UpdateChildBothParentsFaction(assignedChildID);
        for (const State::ActorId dirty : birthMut.dirtyActors) {
            if (!dirty.IsNull() && !dirty.IsPlayer()) {
                lineage.UpdateSpouseChildrenFaction(dirty.raw());
            }
        }

        bool triggerBirthEval = false;
        const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* postRec = postSnap && postSnap->lineage
                ? postSnap->lineage->FindChild(State::ChildId{ assignedChildID })
                : nullptr) {
            MARAS_LOG_INFO(
                "[Fertility] Registered fertility child {:08X} ('{}') to mother {:08X}, presumed father {:08X}, bio father {:08X} (isSecret: {}, secretPartner: {}, secretBio: {}, mismatch: {})",
                assignedChildID,
                postRec->name,
                motherID,
                presumedFatherID,
                bioFatherID,
                postRec->paternitySecret.isSecret,
                postRec->paternitySecret.isSecretForPartner,
                postRec->paternitySecret.isSecretForBioFather,
                postRec->paternitySecret.hasPhenotypeMismatch);
            GetUiBridgeGateway().OnChildUpdated(*postRec);
            triggerBirthEval = postRec->paternitySecret.isSecret && postRec->paternitySecret.hasPhenotypeMismatch;
        }

        if (triggerBirthEval) {
            const float roll = Utils::RandomFloat(0.0f, 100.0f);
            const float birthChance = postSnap && postSnap->settings
                ? static_cast<float>(postSnap->settings->paternityPhenotypeBirthChance)
                : 0.0f;
            if (roll < birthChance) {
                MARAS_LOG_INFO(
                    "[Fertility] Phenotype mismatch detected between presumed father {:08X} and bio father {:08X}. Triggering birth evaluation for child {:08X} (roll: {:.1f}%, chance: {:.1f}%).",
                    presumedFatherID, bioFatherID, assignedChildID, roll, birthChance);
                EvaluatePaternitySecret(assignedChildID);
            } else {
                MARAS_LOG_INFO(
                    "[Fertility] Phenotype mismatch detected between presumed father {:08X} and bio father {:08X}, but birth evaluation roll failed for child {:08X} (roll: {:.1f}%, chance: {:.1f}%).",
                    presumedFatherID, bioFatherID, assignedChildID, roll, birthChance);
            }
        }

        return assignedChildID;
    }

    // =========================================================================
    // Paternity Secrets, Evaluation & Bio-Father Step-In Workflows
    // =========================================================================

    bool PaternityWorkflowCoordinator::ResolvePaternitySecret(
        RE::FormID childID,
        bool discoveredByPartner,
        bool sendEvent) {
        Domain::Lineage::PaternityRevealOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyResolvePaternitySecret(
                ws,
                State::ChildId{ childID },
                discoveredByPartner ? "discovered" : "confess");
        });

        if (!outcome.updated) {
            return false;
        }

        MARAS_LOG_INFO(
            "[Fertility] Fully resolved paternity secret for child {:08X} (discoveredByPartner: {})",
            childID, discoveredByPartner);
        for (const State::ChildId sibId : outcome.affectedChildren) {
            if (sibId.raw() != childID) {
                MARAS_LOG_INFO("[Fertility] Also resolved paternity secret on unborn sibling {:08X}", sibId.raw());
            }
        }

        NotifyUpdatedChildrenUI(outcome.affectedChildren, /*requireDashboardOpen=*/true);

        if (sendEvent && !outcome.modEventOutcome.empty()) {
            AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(childID, outcome.modEventOutcome);
            const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", outcome.modEventOutcome, childIdFloat, RE::FormID{ 0 });
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitPaternitySecretResolved(childID, outcome.modEventOutcome);
                GetSkyrimNetGateway().RecordPaternityReveal(
                    childID,
                    outcome.memoryOutcome,
                    "none",
                    outcome.wasPregnancySecretForPartner);
            }
        }

        return true;
    }

    bool PaternityWorkflowCoordinator::ResolvePaternitySecretForPartner(
        RE::FormID childID,
        bool discoveredByPartner,
        bool sendEvent) {
        Domain::Lineage::PaternityRevealOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyResolvePaternitySecretForPartner(
                ws,
                State::ChildId{ childID },
                discoveredByPartner ? "discovered" : "confess");
        });

        if (!outcome.updated) {
            return false;
        }

        MARAS_LOG_INFO(
            "[Fertility] Resolved paternity secret for partner on child {:08X} (discoveredByPartner: {})",
            childID, discoveredByPartner);
        for (const State::ChildId sibId : outcome.affectedChildren) {
            if (sibId.raw() != childID) {
                MARAS_LOG_INFO("[Fertility] Also resolved paternity secret for partner on unborn sibling {:08X}", sibId.raw());
            }
        }

        NotifyUpdatedChildrenUI(outcome.affectedChildren, /*requireDashboardOpen=*/true);

        if (sendEvent && !outcome.modEventOutcome.empty()) {
            AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(childID, outcome.modEventOutcome);
            const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", outcome.modEventOutcome, childIdFloat, RE::FormID{ 0 });
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitPaternitySecretResolved(childID, outcome.modEventOutcome);
                GetSkyrimNetGateway().RecordPaternityReveal(
                    childID,
                    outcome.memoryOutcome,
                    "none",
                    outcome.wasPregnancySecretForPartner);
            }
        }

        return true;
    }

    bool PaternityWorkflowCoordinator::RevealPregnancyToPartner(
        RE::FormID childID,
        bool byVisibleBelly,
        bool sendEvent) {
        Domain::Lineage::PaternityRevealOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyRevealPregnancyToPartner(
                ws,
                State::ChildId{ childID },
                byVisibleBelly ? "visible" : "share");
        });

        if (!outcome.updated) {
            return false;
        }

        MARAS_LOG_INFO(
            "[Fertility] Revealed pregnancy to partner on unborn child {:08X} (byVisibleBelly: {})",
            childID, byVisibleBelly);
        for (const State::ChildId sibId : outcome.affectedChildren) {
            if (sibId.raw() != childID) {
                MARAS_LOG_INFO("[Fertility] Also revealed pregnancy to partner on unborn sibling {:08X}", sibId.raw());
            }
        }

        NotifyUpdatedChildrenUI(outcome.affectedChildren, /*requireDashboardOpen=*/false);

        if (sendEvent && !outcome.modEventOutcome.empty()) {
            AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(childID, outcome.modEventOutcome);
            const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", outcome.modEventOutcome, childIdFloat, RE::FormID{ 0 });
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitPaternitySecretResolved(childID, outcome.modEventOutcome);
                GetSkyrimNetGateway().RecordPaternityReveal(childID, outcome.modEventOutcome);
            }
        }

        return true;
    }

    bool PaternityWorkflowCoordinator::ResolvePaternitySecretForBioFather(
        RE::FormID childID,
        bool discoveredByBioFather,
        bool sendEvent) {
        Domain::Lineage::PaternityRevealOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplyResolvePaternitySecretForBioFather(
                ws,
                State::ChildId{ childID },
                discoveredByBioFather ? "discovered" : "confess");
        });

        if (!outcome.updated) {
            return false;
        }

        MARAS_LOG_INFO(
            "[Fertility] Resolved paternity secret for bio father on child {:08X} (discoveredByBio: {})",
            childID, discoveredByBioFather);
        for (const State::ChildId sibId : outcome.affectedChildren) {
            if (sibId.raw() != childID) {
                MARAS_LOG_INFO("[Fertility] Also resolved paternity secret for bio father on unborn sibling {:08X}", sibId.raw());
            }
        }

        NotifyUpdatedChildrenUI(outcome.affectedChildren, /*requireDashboardOpen=*/true);

        if (sendEvent && !outcome.modEventOutcome.empty()) {
            AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(childID, outcome.modEventOutcome);
            const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", outcome.modEventOutcome, childIdFloat, RE::FormID{ 0 });
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitPaternitySecretResolved(childID, outcome.modEventOutcome);
                GetSkyrimNetGateway().RecordPaternityReveal(childID, outcome.modEventOutcome);
            }
        }

        return true;
    }

    bool PaternityWorkflowCoordinator::SetPaternitySecretEdges(
        RE::FormID childID,
        bool secretForBioFather,
        bool secretForPartner,
        std::optional<bool> pregnancySecretForPartner) {
        Domain::Lineage::PaternityRevealOutcome outcome{};
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Lineage::ApplySetPaternitySecretEdges(
                ws,
                State::ChildId{ childID },
                secretForBioFather,
                secretForPartner,
                pregnancySecretForPartner,
                /*bypassCooldown=*/false);
        });

        if (!outcome.updated) {
            return false;
        }

        MARAS_LOG_INFO(
            "[Fertility] Set paternity secret edges on child {:08X} (secretBio: {}, secretPartner: {})",
            childID, secretForBioFather, secretForPartner);

        if (!outcome.modEventOutcome.empty()) {
            AffectionEventCoordinator::GetSingleton().OnPaternitySecretResolved(childID, outcome.modEventOutcome);
            const float childIdFloat = static_cast<float>(static_cast<std::int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", outcome.modEventOutcome, childIdFloat, RE::FormID{ 0 });
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitPaternitySecretResolved(childID, outcome.modEventOutcome);
            }
        }

        if (!outcome.memoryOutcome.empty() && GetSkyrimNetGateway().IsAvailable()) {
            GetSkyrimNetGateway().RecordPaternityReveal(
                childID,
                outcome.memoryOutcome,
                "none",
                outcome.wasPregnancySecretForPartner);
        }

        return true;
    }

    bool PaternityWorkflowCoordinator::SetChildPaternityLastCheck(RE::FormID childID, float currentDay) {
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->paternitySecret.lastCheckTimestamp = currentDay;
                updated = true;
            }
        });
        return updated;
    }

    bool PaternityWorkflowCoordinator::EvaluatePaternitySecret(RE::FormID childID) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const bool forceDice = snap && snap->settings && snap->settings->paternityForceDiceRolls;
        if (GetSkyrimNetGateway().IsAvailable() && !forceDice) {
            return GetSkyrimNetGateway().EvaluatePaternitySecret(childID);
        }
        return EvaluatePaternitySecretFallback(childID);
    }

    bool PaternityWorkflowCoordinator::EvaluatePaternitySecretFallback(RE::FormID childID) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage || !snap->settings || !snap->relationships) {
            return false;
        }
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (!rec) {
            return false;
        }

        const RE::FormID motherID = rec->bioMother.raw();
        const RE::FormID partnerID = LineageUseCases::GetSingleton().GetMotherPartner(childID);
        const RE::FormID bioFatherID = rec->bioFather.raw();
        if (motherID == 0 || partnerID == 0 || bioFatherID == 0) {
            return false;
        }

        const float currentDay = GetCurrentGameDaysOrZero();
        const auto& settings = *snap->settings;

        bool shouldReveal = false;
        if (rec->paternitySecret.hasPhenotypeMismatch) {
            shouldReveal = true;
            MARAS_LOG_INFO("[PaternityFallback] Child {:08X} has phenotype mismatch -> reveals immediately.", childID);
        } else {
            const float revealRoll = Utils::RandomFloat(0.0f, 100.0f);
            const float revealChance = static_cast<float>(settings.paternityNonSkyrimNetRevealChance);
            shouldReveal = (revealRoll < revealChance);
            MARAS_LOG_INFO(
                "[PaternityFallback] Child {:08X} reveal roll: {:.1f}%, chance: {:.1f}% -> {}",
                childID, revealRoll, revealChance, shouldReveal ? "REVEALED" : "REMAINS SECRET");
        }

        if (!shouldReveal) {
            SetChildPaternityLastCheck(childID, currentDay);
            return true;
        }

        ResolvePaternitySecret(childID, /*discoveredByPartner=*/true, /*sendEvent=*/true);

        const float breakupRoll = Utils::RandomFloat(0.0f, 100.0f);
        const float breakupChance = static_cast<float>(settings.paternityBreakupChance);
        const bool isOpen = snap->relationships->IsPairOpen(
            State::ActorId{ motherID }, State::ActorId{ partnerID });
        const bool doesBreakup = !isOpen && (breakupRoll < breakupChance);

        std::string partnerReaction = "none";
        if (doesBreakup) {
            const auto currentStatus = snap->relationships->GetStatus(
                State::ActorId{ motherID }, State::ActorId{ partnerID });
            Foundation::RelationshipStatus breakupTarget = Foundation::RelationshipStatus::Unknown;
            switch (currentStatus) {
                case Foundation::RelationshipStatus::Married:
                    partnerReaction = "divorce";
                    breakupTarget = Foundation::RelationshipStatus::Divorced;
                    break;
                case Foundation::RelationshipStatus::Engaged:
                    partnerReaction = "breakup";
                    breakupTarget = Foundation::RelationshipStatus::Jilted;
                    break;
                case Foundation::RelationshipStatus::Dating:
                    partnerReaction = "breakup";
                    breakupTarget = Foundation::RelationshipStatus::ExDating;
                    break;
                case Foundation::RelationshipStatus::Lover:
                    partnerReaction = "breakup";
                    breakupTarget = Foundation::RelationshipStatus::ExLover;
                    break;
                default:
                    break;
            }
            if (breakupTarget != Foundation::RelationshipStatus::Unknown) {
                RelationshipUseCases::GetSingleton().PromoteToStatus(
                    motherID,
                    partnerID,
                    breakupTarget,
                    partnerID,
                    /*bypassDifficulty=*/true,
                    /*suppressModal=*/true);
            }
        }

        MARAS_LOG_INFO("[PaternityFallback] Child {:08X} secret resolved: reaction='{}'", childID, partnerReaction);

        const SessionEpoch epoch = GetSessionEpoch();
        AddEpochTask(epoch, "EvaluatePaternitySecretFallback::RefreshUI", [epoch, childID]() {
            std::lock_guard lock(GetPapyrusMutex());
            if (!IsSessionEpochCurrent(epoch)) {
                return;
            }
            auto& ui = GetUiBridgeGateway();
            ui.SendOverviewData();
            ui.SendNPCDetail(childID);
        });

        return true;
    }

    bool PaternityWorkflowCoordinator::ApplyBioFatherStepIn(RE::FormID motherID, RE::FormID bioFatherID) {
        if (motherID == 0 || bioFatherID == 0) {
            return false;
        }

        if (IsActorDeadOrInvalidInEngine(motherID) || IsActorDeadOrInvalidInEngine(bioFatherID)) {
            MARAS_LOG_INFO(
                "[Fertility] Skipping bio-father step-in: mother {:08X} or bio father {:08X} is deceased/dead/disabled",
                motherID, bioFatherID);
            return false;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage || !snap->relationships) {
            return false;
        }

        bool hasMatchingChild = false;
        for (const auto& [childKey, rec] : snap->lineage->AllChildren()) {
            (void)childKey;
            if (rec.bioMother.raw() == motherID && rec.bioFather.raw() == bioFatherID) {
                hasMatchingChild = true;
                break;
            }
        }
        if (!hasMatchingChild) {
            MARAS_LOG_INFO(
                "[Fertility] Skipping bio-father step-in: no child record still links mother {:08X} and bio father {:08X}",
                motherID, bioFatherID);
            return false;
        }

        const bool alreadyMarried =
            snap->relationships->GetStatus(State::ActorId{ motherID }, State::ActorId{ bioFatherID }) ==
            Foundation::RelationshipStatus::Married;
        if (!alreadyMarried) {
            if (!snap->relationships->GetPrimaryPartner(State::ActorId{ motherID }).IsNull() ||
                !snap->relationships->GetPrimaryPartner(State::ActorId{ bioFatherID }).IsNull()) {
                MARAS_LOG_INFO(
                    "[Fertility] Mother {:08X} or bio father {:08X} already has a primary partner, skipping step-in.",
                    motherID, bioFatherID);
                return false;
            }
            if (IsMostRecentRelationshipDivorced(*snap, motherID, bioFatherID)) {
                MARAS_LOG_INFO(
                    "[Fertility] Bio father {:08X} has divorced relationship with mother {:08X}, skipping step-in.",
                    bioFatherID, motherID);
                return false;
            }
            if (!CanFormStepInMarriage(*snap, motherID, bioFatherID)) {
                MARAS_LOG_INFO(
                    "[Fertility] Bio father {:08X} cannot marry mother {:08X}, skipping step-in.",
                    bioFatherID, motherID);
                return false;
            }
            if (!RelationshipUseCases::GetSingleton().PromoteToStatus(
                    motherID,
                    bioFatherID,
                    Foundation::RelationshipStatus::Married,
                    bioFatherID,
                    /*bypassDifficulty=*/true,
                    /*suppressModal=*/true)) {
                MARAS_LOG_WARN(
                    "[Fertility] Failed to marry bio father {:08X} with mother {:08X} during step-in.",
                    bioFatherID, motherID);
                return false;
            }
        }

        MARAS_LOG_INFO("[Fertility] Bio father {:08X} stepped in and married mother {:08X}", bioFatherID, motherID);
        return true;
    }

    void PaternityWorkflowCoordinator::EvaluateBioFatherStepIn(RE::FormID motherID, RE::FormID formerSpouseID) {
        if (motherID == 0 || (formerSpouseID != 0 && motherID == formerSpouseID)) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage || !snap->relationships || !snap->settings) {
            return;
        }

        std::vector<StepInCandidate> candidates;
        std::unordered_set<RE::FormID> seenFathers;

        auto tryCollectCandidate = [&](RE::FormID bioFatherID, bool isPreg, const std::string& childName, float ageDays) {
            if (bioFatherID == 0 || bioFatherID == kUnknownFatherFormID ||
                (formerSpouseID != 0 && Utils::MatchesActor(bioFatherID, formerSpouseID))) {
                return;
            }
            if (Utils::MatchesActor(motherID, kPlayerFormID) ||
                Utils::MatchesActor(bioFatherID, kPlayerFormID)) {
                return;
            }
            if (seenFathers.contains(bioFatherID)) {
                return;
            }
            if (!snap->relationships->GetPrimaryPartner(State::ActorId{ bioFatherID }).IsNull()) {
                return;
            }
            if (IsMostRecentRelationshipDivorced(*snap, motherID, bioFatherID)) {
                MARAS_LOG_INFO(
                    "[Fertility] Bio father {:08X} skipped from step-in candidates: most recent relationship with mother {:08X} is divorced.",
                    bioFatherID, motherID);
                return;
            }
            if (!CanFormStepInMarriage(*snap, motherID, bioFatherID)) {
                return;
            }

            candidates.push_back(StepInCandidate{
                bioFatherID,
                isPreg,
                (childName.empty() || Foundation::StringUtils::CaseInsensitiveEquals(childName, "Newborn"))
                    ? "child"
                    : childName,
                ageDays});
            seenFathers.insert(bioFatherID);
        };

        for (const RE::FormID uID : GetUnbornChildren(motherID)) {
            if (const auto* rec = snap->lineage->FindChild(State::ChildId{ uID })) {
                tryCollectCandidate(rec->bioFather.raw(), true, rec->name, 0.0f);
            }
        }

        const float currentDay = GetCurrentGameDaysOrZero();
        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            if (rec.stage == Foundation::ChildStage::Unborn || rec.stage == Foundation::ChildStage::Adult) {
                continue;
            }
            if (!rec.guardians.HasGuardian(State::ActorId{ motherID })) {
                continue;
            }
            const RE::FormID cID = cIdKey.raw();
            RE::FormID bioFatherID = LineageUseCases::GetSingleton().GetBioFather(cID);
            if (bioFatherID == 0 && !rec.bioFather.IsNull() &&
                (formerSpouseID == 0 || !Utils::MatchesActor(rec.bioFather.raw(), formerSpouseID)) &&
                !Utils::MatchesActor(rec.bioFather.raw(), motherID)) {
                bioFatherID = rec.bioFather.raw();
            }
            const float ageDays = std::max(0.0f, currentDay - rec.birthTimestamp);
            tryCollectCandidate(bioFatherID, false, rec.name, ageDays);
        }

        if (candidates.empty()) {
            return;
        }

        const bool useLLM = GetSkyrimNetGateway().IsAvailable() && !snap->settings->paternityForceDiceRolls;
        if (!useLLM) {
            const float stepUpChance = static_cast<float>(snap->settings->paternityBioStepUpChance);
            for (const auto& cand : candidates) {
                if (IsMostRecentRelationshipDivorced(*snap, motherID, cand.bioFatherID) ||
                    !snap->relationships->GetPrimaryPartner(State::ActorId{ cand.bioFatherID }).IsNull() ||
                    !CanFormStepInMarriage(*snap, motherID, cand.bioFatherID)) {
                    continue;
                }
                const float stepUpRoll = Utils::RandomFloat(0.0f, 100.0f);
                MARAS_LOG_INFO(
                    "[Fertility] Step-in dice roll for bio father {:08X} with mother {:08X}: {:.1f}% (chance: {:.1f}%)",
                    cand.bioFatherID, motherID, stepUpRoll, stepUpChance);
                if (stepUpRoll < stepUpChance) {
                    if (ApplyBioFatherStepIn(motherID, cand.bioFatherID)) {
                        break;
                    }
                }
            }
            return;
        }

        auto queue = std::make_shared<StepInEvalQueue>();
        queue->motherID = motherID;
        queue->formerSpouseID = formerSpouseID;
        queue->candidates = std::move(candidates);
        queue->epoch = GetSessionEpoch();
        RunStepInCandidateAtIndex(std::move(queue), 0);
    }

    // =========================================================================
    // Strict Read-Only Secret, Phenotype & Narration Queries (CQS)
    // =========================================================================

    bool PaternityWorkflowCoordinator::IsSecretForBioFather(RE::FormID childID) const {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childID };
        const auto* rec = worldSnap && worldSnap->lineage ? worldSnap->lineage->FindChild(childKey) : nullptr;
        if (!rec || !rec->paternitySecret.isSecretForBioFather) {
            return false;
        }
        if (rec->bioFather.IsNull() || rec->bioFather.raw() == kUnknownFatherFormID) {
            return !rec->paternitySecret.discovered && !rec->paternitySecret.discoveredByBioFather;
        }
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto bioView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, rec->bioFather, &GetEngineWerewolfStateProvider());
        return !bioView.CanViewerKnowTrueBioFather(childKey);
    }

    bool PaternityWorkflowCoordinator::IsSecretForPartner(RE::FormID childID) const {
        const RE::FormID partnerID = LineageUseCases::GetSingleton().GetMotherPartner(childID);
        if (partnerID == 0) {
            return false;
        }
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childID };
        const auto* rec = worldSnap && worldSnap->lineage ? worldSnap->lineage->FindChild(childKey) : nullptr;
        if (!rec || !rec->paternitySecret.isSecretForPartner) {
            return false;
        }
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto partnerView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider());
        return !partnerView.CanViewerKnowTrueBioFather(childKey);
    }

    bool PaternityWorkflowCoordinator::IsPregnancySecretForPartner(RE::FormID childID) const {
        const RE::FormID partnerID = LineageUseCases::GetSingleton().GetMotherPartner(childID);
        if (partnerID == 0) {
            return false;
        }
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childID };
        const auto* rec = worldSnap && worldSnap->lineage ? worldSnap->lineage->FindChild(childKey) : nullptr;
        if (!rec || rec->stage != Foundation::ChildStage::Unborn || !rec->paternitySecret.isSecretForPartner) {
            return false;
        }
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto partnerView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider());
        return !partnerView.CanViewerKnowPregnancy(childKey);
    }

    std::vector<RE::FormID> PaternityWorkflowCoordinator::GetChildrenWithActivePaternitySecrets() const {
        std::vector<RE::FormID> result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return result;
        }
        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            if (rec.paternitySecret.HasActiveSecret() && !rec.paternitySecret.discovered) {
                result.push_back(cIdKey.raw());
            }
        }
        return result;
    }

    std::vector<RE::FormID> PaternityWorkflowCoordinator::GetChildrenWithActiveSecretsForActor(RE::FormID actorID) const {
        std::vector<RE::FormID> result;
        if (actorID == 0) {
            return result;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return result;
        }
        auto& lineage = LineageUseCases::GetSingleton();
        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            if (!rec.paternitySecret.HasActiveSecret() || rec.paternitySecret.discovered) {
                continue;
            }
            const RE::FormID cID = cIdKey.raw();
            if (rec.guardians.HasGuardian(State::ActorId{ actorID }) ||
                Utils::MatchesActor(rec.bioMother.raw(), actorID) ||
                Utils::MatchesActor(rec.bioFather.raw(), actorID) ||
                Utils::MatchesActor(rec.presumedFather.raw(), actorID) ||
                Utils::MatchesActor(lineage.GetMotherPartner(cID), actorID)) {
                result.push_back(cID);
            }
        }
        return result;
    }

    RE::FormID PaternityWorkflowCoordinator::FindChildPaternitySecret(
        RE::FormID parent1ID,
        RE::FormID parent2ID,
        const std::string& childName) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->lineage) {
            return 0;
        }

        auto& lineage = LineageUseCases::GetSingleton();
        auto matchesParent = [&lineage](const State::ChildRecord& rec, RE::FormID pID) -> bool {
            if (pID == 0) {
                return true;
            }
            return rec.guardians.HasGuardian(State::ActorId{ pID }) ||
                   Utils::MatchesActor(rec.bioMother.raw(), pID) ||
                   Utils::MatchesActor(rec.bioFather.raw(), pID) ||
                   Utils::MatchesActor(rec.presumedFather.raw(), pID) ||
                   Utils::MatchesActor(lineage.GetMotherPartner(rec.childId.raw()), pID);
        };

        const bool hasName =
            !childName.empty() && !Foundation::StringUtils::CaseInsensitiveEquals(childName, "all");

        auto matchesChildName = [](const State::ChildRecord& rec, const std::string& nameLower) -> bool {
            const std::string entryNameLower = Foundation::StringUtils::ToLowerStd(rec.name);
            if (!entryNameLower.empty() &&
                (entryNameLower == nameLower ||
                 entryNameLower.find(nameLower) != std::string::npos ||
                 nameLower.find(entryNameLower) != std::string::npos)) {
                return true;
            }
            if (rec.stage == Foundation::ChildStage::Unborn) {
                if (nameLower.find("unborn") != std::string::npos ||
                    nameLower.find("pregnancy") != std::string::npos ||
                    nameLower.find("expecting") != std::string::npos ||
                    nameLower.find("baby") != std::string::npos ||
                    nameLower.find("child") != std::string::npos) {
                    return true;
                }
                const std::string bioName = !rec.paternitySecret.bioFatherName.empty()
                    ? rec.paternitySecret.bioFatherName
                    : ActorSvc::GetName(rec.bioFather.raw());
                if (!bioName.empty()) {
                    const std::string bioNameLower = Foundation::StringUtils::ToLowerStd(bioName);
                    if (nameLower.find(bioNameLower) != std::string::npos ||
                        bioNameLower.find(nameLower) != std::string::npos) {
                        return true;
                    }
                }
            }
            return false;
        };

        const auto& allChildren = snap->lineage->AllChildren();

        if (hasName) {
            const std::string nameLower = Foundation::StringUtils::ToLowerStd(childName);
            for (const auto& [cIdKey, rec] : allChildren) {
                if (!rec.paternitySecret.HasActiveSecret() || rec.paternitySecret.discovered) continue;
                if (!matchesParent(rec, parent1ID) || !matchesParent(rec, parent2ID)) continue;
                if (matchesChildName(rec, nameLower)) {
                    return cIdKey.raw();
                }
            }
        }

        if (parent1ID != 0 && parent2ID != 0) {
            for (const auto& [cIdKey, rec] : allChildren) {
                if (!rec.paternitySecret.HasActiveSecret() || rec.paternitySecret.discovered) continue;
                if (matchesParent(rec, parent1ID) && matchesParent(rec, parent2ID)) {
                    return cIdKey.raw();
                }
            }
        }

        if (parent1ID != 0) {
            for (const auto& [cIdKey, rec] : allChildren) {
                if (!rec.paternitySecret.HasActiveSecret() || rec.paternitySecret.discovered) continue;
                if (matchesParent(rec, parent1ID)) {
                    return cIdKey.raw();
                }
            }
        }

        if (hasName) {
            const std::string nameLower = Foundation::StringUtils::ToLowerStd(childName);
            for (const auto& [cIdKey, rec] : allChildren) {
                if (!rec.paternitySecret.HasActiveSecret() || rec.paternitySecret.discovered) continue;
                if (matchesChildName(rec, nameLower)) {
                    return cIdKey.raw();
                }
            }
        }

        return 0;
    }

    Foundation::PhenotypeTier PaternityWorkflowCoordinator::GetActorPhenotypeTier(RE::Actor* actor) const {
        if (!actor) {
            return Foundation::PhenotypeTier::Unknown;
        }
        auto* race = actor->GetRace();
        if (!race) {
            return Foundation::PhenotypeTier::Unknown;
        }

        if (const char* formEditorID = race->GetFormEditorID(); formEditorID && *formEditorID) {
            const auto tier = Domain::Lineage::ClassifyPhenotypeTierFromEditorID(formEditorID);
            if (tier != Foundation::PhenotypeTier::Unknown) {
                return tier;
            }
        }
        if (const char* rName = race->GetFullName(); rName && *rName) {
            return Domain::Lineage::ClassifyPhenotypeTierFromEditorID(rName);
        }
        return Foundation::PhenotypeTier::Unknown;
    }

    Foundation::PhenotypeTier PaternityWorkflowCoordinator::GetFormIDPhenotypeTier(RE::FormID formID) const {
        if (formID == 0) {
            return Foundation::PhenotypeTier::Unknown;
        }
        if (auto* refr = ActorSvc::GetReference(formID)) {
            if (auto* actor = refr->As<RE::Actor>()) {
                return GetActorPhenotypeTier(actor);
            }
        }
        const std::string raceEdid = ActorSvc::GetRaceEditorID(formID);
        if (!raceEdid.empty()) {
            const auto tier = Domain::Lineage::ClassifyPhenotypeTierFromEditorID(raceEdid);
            if (tier != Foundation::PhenotypeTier::Unknown) {
                return tier;
            }
        }
        return Domain::Lineage::ClassifyPhenotypeTierFromEditorID(ActorSvc::GetRaceName(formID));
    }

    bool PaternityWorkflowCoordinator::HasPhenotypeMismatch(RE::FormID father1ID, RE::FormID father2ID) const {
        if (father1ID == 0 || father2ID == 0) {
            return false;
        }
        return Domain::Lineage::HasPhenotypeMismatch(
            GetFormIDPhenotypeTier(father1ID),
            GetFormIDPhenotypeTier(father2ID));
    }

    std::string PaternityWorkflowCoordinator::GetVisualDiscrepancyDescription(
        RE::FormID motherID,
        RE::FormID presumedFatherID,
        RE::FormID bioFatherID) const {
        const auto bioTier = GetFormIDPhenotypeTier(bioFatherID);
        const auto presumedTier = GetFormIDPhenotypeTier(presumedFatherID);
        const std::string bioRace = ActorSvc::GetRaceName(bioFatherID);
        const std::string presumedRace = ActorSvc::GetRaceName(presumedFatherID);
        const std::string motherRace = ActorSvc::GetRaceName(motherID);
        const std::string presumedName = ActorSvc::GetName(presumedFatherID);

        if (bioTier == Foundation::PhenotypeTier::Argonian && presumedTier == Foundation::PhenotypeTier::Khajiit) {
            return std::format(
                "The newborn displays unmistakable reptilian scales, horns, and Argonian traits, completely contradicting the feline Khajiit heritage of the presumed father ({}).",
                presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::Khajiit && presumedTier == Foundation::PhenotypeTier::Argonian) {
            return std::format(
                "The newborn exhibits feline fur, ears, and Khajiit traits, completely contradicting the reptilian Argonian heritage of the presumed father ({}).",
                presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::Argonian) {
            return std::format(
                "The child displays unmistakable reptilian scales, horns, and traits of an Argonian (Saxhleel lineage), completely contradicting the presumed father's ({}) heritage.",
                presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::Khajiit) {
            return std::format(
                "The child displays unmistakable feline fur, ears, and traits of a Khajiit, completely contradicting the presumed father's ({}) heritage.",
                presumedRace);
        }
        if (presumedTier == Foundation::PhenotypeTier::Argonian || presumedTier == Foundation::PhenotypeTier::Khajiit) {
            return std::format(
                "The child displays humanoid/{} features lacking any scales, fur, or beast traits, impossible for the offspring of {} ({}).",
                bioRace, presumedName, presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::Orc && presumedTier == Foundation::PhenotypeTier::Mer) {
            return std::format(
                "The child exhibits distinctive Orcish features—green-tinted complexion, prominent lower tusks, and a heavy brow—contrasting sharply with the refined elven heritage of {} ({}).",
                presumedName, presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::Mer && presumedTier == Foundation::PhenotypeTier::Orc) {
            return std::format(
                "The child displays delicate, classic elven traits lacking the green skin and tusks of an Orc, impossible for the offspring of {} ({}).",
                presumedName, presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::Orc) {
            return std::format(
                "The child displays unmistakable Orcish traits—greenish skin, prominent lower tusks, and a rugged brow—completely contradicting the {} heritage of presumed father {}.",
                presumedRace, presumedName);
        }
        if (presumedTier == Foundation::PhenotypeTier::Orc) {
            return std::format(
                "The child completely lacks the green complexion and tusks of an Orc, bearing traits of a {} instead, which clashes noticeably with presumed father {}.",
                bioRace, presumedName);
        }
        if (bioTier == Foundation::PhenotypeTier::Mer &&
            (presumedTier == Foundation::PhenotypeTier::PaleHuman ||
             presumedTier == Foundation::PhenotypeTier::Redguard)) {
            return std::format(
                "The child exhibits distinct elven features and pointed ears characteristic of a {}, clashing noticeably with the human lineage of the presumed father ({}).",
                bioRace, presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::Redguard && presumedTier == Foundation::PhenotypeTier::PaleHuman) {
            return std::format(
                "The child has a deep, warm Redguard complexion, creating an immediate and undeniable visual contrast against the pale skin of {} (mother) and {} (presumed father).",
                motherRace, presumedRace);
        }
        if (bioTier == Foundation::PhenotypeTier::PaleHuman && presumedTier == Foundation::PhenotypeTier::Redguard) {
            return std::format(
                "The child has strikingly pale northern skin, contrasting sharply with the deep complexion of the presumed father ({}).",
                presumedRace);
        }

        if (!bioRace.empty() && !presumedRace.empty()) {
            return std::format(
                "The child's physical appearance unmistakably reflects the traits of a {}, which is distinct from the presumed father's {} lineage.",
                bioRace, presumedRace);
        }
        return Domain::Lineage::GetVisualDiscrepancyDescription(bioTier, presumedTier, "the child");
    }

}  // namespace MARAS::App
