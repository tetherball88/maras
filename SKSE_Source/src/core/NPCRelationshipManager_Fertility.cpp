#include "core/NPCRelationshipManager.h"

#include "core/AdultChildProgression.h"
#include "core/DeceasedRegistry.h"
#include "core/DeferredActorResolver.h"
#include "core/FactionReconciler.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/PackageOverrideService.h"
#include "core/PackageReconciler.h"
#include "core/PerspectiveEngineAdapters.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/SessionEpoch.h"
#include "core/SkyrimNetDecisions.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/SkyrimNetProfileService.h"
#include "core/WorldStateLegacyAdapters.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "papyrus/PapyrusInterface.h"
#include "services/ActorService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "ui/PrismaUIManager.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

namespace MARAS {

    namespace {
        RE::FormID AllocateSyntheticChildID() {
            RE::FormID allocated = 0;
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                allocated = ws.lineage.Write().AllocateSyntheticChildId().raw();
            });
            if (allocated == 0) {
                MARAS_LOG_ERROR("[Fertility] Synthetic child FormID pool (0xFF700001..0xFF7FFFFF) exhausted!");
                return kSyntheticChildIDMax;
            }
            return allocated;
        }
    }  // namespace

    bool NPCRelationshipManager::ApplyBioFatherStepIn(RE::FormID motherID, RE::FormID bioFatherID) {
        if (motherID == 0 || bioFatherID == 0) return false;

        auto isActorDeadOrInvalid = [](RE::FormID actorID) -> bool {
            if (DeceasedRegistry::GetSingleton().IsDeceased(actorID)) {
                return true;
            }
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID)) {
                return actor->IsDead() || actor->IsDeleted() || actor->IsDisabled();
            }
            return false;
        };
        if (isActorDeadOrInvalid(motherID) || isActorDeadOrInvalid(bioFatherID)) {
            MARAS_LOG_INFO("[Fertility] Skipping bio-father step-in: mother {:08X} or bio father {:08X} is deceased/dead/disabled",
                           motherID, bioFatherID);
            return false;
        }

        // Verify a live unborn or child record still links this mother and bioFather
        bool hasMatchingChild = false;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [childKey, rec] : snap->lineage->AllChildren()) {
                (void)childKey;
                if (rec.bioMother.raw() == motherID && rec.bioFather.raw() == bioFatherID) {
                    hasMatchingChild = true;
                    break;
                }
            }
        }
        if (!hasMatchingChild) {
            MARAS_LOG_INFO("[Fertility] Skipping bio-father step-in: no child record still links mother {:08X} and bio father {:08X}",
                           motherID, bioFatherID);
            return false;
        }

        // Ensure relationship can be formed / already married
        bool alreadyMarried = (GetRelationshipStatus(motherID, bioFatherID) == RelationshipStatus::Married);
        if (!alreadyMarried) {
            if (GetPrimaryPartner(motherID) != 0 || GetPrimaryPartner(bioFatherID) != 0) {
                MARAS_LOG_INFO("[Fertility] Mother {:08X} or bio father {:08X} already has a primary partner, skipping step-in.",
                               motherID, bioFatherID);
                return false;
            }
            if (IsMostRecentRelationshipDivorced(motherID, bioFatherID)) {
                MARAS_LOG_INFO("[Fertility] Bio father {:08X} has divorced relationship with mother {:08X}, skipping step-in.", bioFatherID, motherID);
                return false;
            }
            if (!CanFormRelationship(motherID, bioFatherID, RelationshipStatus::Married)) {
                MARAS_LOG_INFO("[Fertility] Bio father {:08X} cannot marry mother {:08X}, skipping step-in.", bioFatherID, motherID);
                return false;
            }
            if (!PromoteToStatusWith(motherID, bioFatherID, RelationshipStatus::Married)) {
                MARAS_LOG_WARN("[Fertility] Failed to marry bio father {:08X} with mother {:08X} during step-in.", bioFatherID, motherID);
                return false;
            }
        }

        MARAS_LOG_INFO("[Fertility] Bio father {:08X} stepped in and married mother {:08X}", bioFatherID, motherID);
        return true;
    }

    namespace {

        struct StepInCandidate {
            RE::FormID bioFatherID{ 0 };
            bool isPregnancy{ false };
            std::string childName;
            float childAgeDays{ 0.0f };
        };

        struct StepInEvalQueue {
            RE::FormID motherID{ 0 };
            RE::FormID formerSpouseID{ 0 };
            std::vector<StepInCandidate> candidates;
            SessionEpoch epoch{ 0 };
        };

        void RunStepInCandidateAtIndex(std::shared_ptr<const StepInEvalQueue> queue, size_t index) {
            if (!queue || !IsSessionEpochCurrent(queue->epoch) || index >= queue->candidates.size()) {
                return;
            }

            auto& mgr = NPCRelationshipManager::GetSingleton();
            const RE::FormID motherID = queue->motherID;
            const RE::FormID formerSpouseID = queue->formerSpouseID;
            const auto& cand = queue->candidates[index];

            if (mgr.IsMostRecentRelationshipDivorced(motherID, cand.bioFatherID) ||
                mgr.GetPrimaryPartner(cand.bioFatherID) != 0 ||
                !mgr.CanFormRelationship(motherID, cand.bioFatherID, RelationshipStatus::Married)) {
                RunStepInCandidateAtIndex(std::move(queue), index + 1);
                return;
            }

            std::string motherName = ActorSvc::GetName(motherID);
            std::string bioFatherName = ActorSvc::GetName(cand.bioFatherID);

            nlohmann::json ctx;
            ctx["motherName"] = motherName;
            ctx["motherRace"] = ActorSvc::GetRaceName(motherID);
            ctx["motherUUID"] = GetActorUUID(motherID);

            ctx["bioFatherName"] = bioFatherName;
            ctx["bioFatherRace"] = ActorSvc::GetRaceName(cand.bioFatherID);
            ctx["bioFatherUUID"] = GetActorUUID(cand.bioFatherID);

            if (formerSpouseID != 0) {
                ctx["formerSpouseName"] = ActorSvc::GetName(formerSpouseID);
                ctx["formerSpouseRace"] = ActorSvc::GetRaceName(formerSpouseID);
                ctx["formerSpouseUUID"] = GetActorUUID(formerSpouseID);
            } else {
                ctx["formerSpouseName"] = "";
                ctx["formerSpouseRace"] = "";
                ctx["formerSpouseUUID"] = 0;
            }

            ctx["isPregnancy"] = cand.isPregnancy;
            ctx["childName"] = cand.childName;
            ctx["childAgeDays"] = static_cast<int>(std::round(cand.childAgeDays));

            nlohmann::json recentEventsParticipants = nlohmann::json::array();
            uint64_t motherUUID = ctx["motherUUID"].get<uint64_t>();
            uint64_t bioFatherUUID = ctx["bioFatherUUID"].get<uint64_t>();
            uint64_t formerSpouseUUID = ctx["formerSpouseUUID"].get<uint64_t>();
            if (motherUUID != 0) recentEventsParticipants.push_back(motherUUID);
            if (bioFatherUUID != 0 && bioFatherUUID != motherUUID) recentEventsParticipants.push_back(bioFatherUUID);
            if (formerSpouseUUID != 0 && formerSpouseUUID != motherUUID && formerSpouseUUID != bioFatherUUID) recentEventsParticipants.push_back(formerSpouseUUID);
            ctx["recent_events_participants"] = std::move(recentEventsParticipants);

            std::string contextJson = ctx.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

            const bool queued = SkyrimNetDecisions::EvaluateStepIn(
                contextJson,
                [queue, index, motherID, motherName, bioFatherName, cand](const SkyrimNetDecisions::StepInEvalResult& res) {
                    if (!IsSessionEpochCurrent(queue->epoch)) {
                        return;
                    }
                    AddEpochTask(queue->epoch, "EvaluateBioFatherStepIn::OnResult",
                        [queue, index, motherID, motherName, bioFatherName, cand, res]() {
                            std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                            auto& relMgr = NPCRelationshipManager::GetSingleton();
                            if (res.success) {
                                MARAS_LOG_INFO("[SkyrimNet] Step-in evaluation for bio father {:08X} with mother {:08X} (usedDecisions={}): steppedIn={}, reason='{}'",
                                               cand.bioFatherID, motherID, res.usedDecisions, res.steppedIn, res.reason);
                                if (res.steppedIn) {
                                    relMgr.ApplyBioFatherStepIn(motherID, cand.bioFatherID);
                                } else {
                                    RunStepInCandidateAtIndex(queue, index + 1);
                                }
                                return;
                            }

                            MARAS_LOG_WARN("[SkyrimNet] Step-in evaluation failed for {:08X} & {:08X} (usedDecisions={}, detail='{}')",
                                           cand.bioFatherID, motherID, res.usedDecisions, res.failureDetail);

                            ModalRequest req;
                            req.title = "Bio-Father Step-In Evaluation Failed";
                            req.subtitle = "SkyrimNet LLM Request Failed";
                            req.message = "The SkyrimNet LLM evaluation for whether the biological father steps in to marry the mother failed. Choose how to proceed:";
                            req.tone = ModalTone::Warning;
                            req.size = ModalSize::Md;
                            req.defaultCancelButtonIndex = 2;

                            std::string childDesc = cand.isPregnancy ? "Unborn Child" : (cand.childName.empty() ? "Child" : cand.childName);
                            req.controls.push_back(ModalControl::MakeInfoBox(
                                "error_details",
                                "Failure Details",
                                std::format("{} & {} ({})\n{}", bioFatherName, motherName, childDesc, res.failureDetail),
                                ModalTone::Warning));
                            req.controls.push_back(ModalControl::MakeInfoBox(
                                "error_guidance",
                                GetLLMFailureGuidanceTitle(res.failureType),
                                GetLLMFailureGuidance(res.failureType, res.usedDecisions ? "decisions" : "gamemaster_evaluation"),
                                ModalTone::Default));

                            req.buttons.push_back(ModalButton::Make("retry", "Retry LLM Request", ModalButtonVariant::Primary));
                            req.buttons.push_back(ModalButton::Make("dice_roll", "Use Dice Roll Fallback", ModalButtonVariant::Secondary));
                            req.buttons.push_back(ModalButton::Make("skip", "Skip Candidate", ModalButtonVariant::Ghost));

                            PrismaModalManager::GetSingleton().ShowModal(
                                std::move(req),
                                [queue, index, motherID, cand](const ModalResponse& modalResp) {
                                    std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                                    if (!IsSessionEpochCurrent(queue->epoch) || modalResp.aborted) {
                                        return;
                                    }
                                    if (modalResp.buttonIndex == 0 || modalResp.buttonId == "retry") {
                                        MARAS_LOG_INFO("[SkyrimNet] Player chose to retry bio-father step-in LLM evaluation for {:08X} & {:08X}",
                                                       cand.bioFatherID, motherID);
                                        RunStepInCandidateAtIndex(queue, index);
                                        return;
                                    }
                                    if (modalResp.buttonIndex == 1 || modalResp.buttonId == "dice_roll") {
                                        float roll = Utils::RandomFloat(0.0f, 100.0f);
                                        float chance = static_cast<float>(Settings::GetSingleton().Snapshot()->paternityBioStepUpChance);
                                        bool fallbackStepIn = (roll < chance);
                                        MARAS_LOG_INFO("[SkyrimNet] Player chose dice-roll fallback for bio-father step-in ({:08X} & {:08X}): {:.1f}% < {:.1f}% -> {}",
                                                       cand.bioFatherID, motherID, roll, chance, fallbackStepIn);
                                        if (fallbackStepIn) {
                                            NPCRelationshipManager::GetSingleton().ApplyBioFatherStepIn(motherID, cand.bioFatherID);
                                        } else {
                                            RunStepInCandidateAtIndex(queue, index + 1);
                                        }
                                        return;
                                    }
                                    MARAS_LOG_INFO("[SkyrimNet] Player skipped bio-father step-in evaluation for {:08X} & {:08X}",
                                                   cand.bioFatherID, motherID);
                                    RunStepInCandidateAtIndex(queue, index + 1);
                                });
                        });
                });

            if (!queued) {
                MARAS_LOG_WARN("[SkyrimNet] Failed to dispatch bio-father step-in evaluation for {:08X} & {:08X}; advancing queue",
                               cand.bioFatherID, motherID);
                AddEpochTask(queue->epoch, "EvaluateBioFatherStepIn::AdvanceOnDispatchFailure", [queue, index]() {
                    std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                    RunStepInCandidateAtIndex(queue, index + 1);
                });
            }
        }

    }  // namespace

    void NPCRelationshipManager::EvaluateBioFatherStepIn(RE::FormID motherID, RE::FormID formerSpouseID) {
        if (motherID == 0 || (formerSpouseID != 0 && motherID == formerSpouseID)) return;

        std::vector<StepInCandidate> candidates;
        std::unordered_set<RE::FormID> seenFathers;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto sSnap = Settings::GetSingleton().Snapshot();

        // 1. Check unborn children for mother
        for (RE::FormID uID : GetUnbornChildren(motherID)) {
            const auto* rec = snap->lineage->FindChild(State::ChildId{ uID });
            if (!rec) continue;

            const RE::FormID bioFatherID = rec->bioFather.raw();
            if (bioFatherID == 0 || bioFatherID == kUnknownFatherFormID || (formerSpouseID != 0 && MatchesActor(bioFatherID, formerSpouseID))) continue;
            if (motherID == kPlayerFormID || bioFatherID == kPlayerFormID) continue;
            if (seenFathers.contains(bioFatherID)) continue;
            if (GetPrimaryPartner(bioFatherID) != 0) continue;
            if (IsMostRecentRelationshipDivorced(motherID, bioFatherID)) {
                MARAS_LOG_INFO("[Fertility] Bio father {:08X} skipped from step-in candidates: most recent relationship with mother {:08X} is divorced.",
                               bioFatherID, motherID);
                continue;
            }
            if (!CanFormRelationship(motherID, bioFatherID, RelationshipStatus::Married)) continue;

            candidates.push_back({ bioFatherID, true, rec->name, 0.0f });
            seenFathers.insert(bioFatherID);
        }

        // 2. Children left with mother in her custody
        auto calendar = RE::Calendar::GetSingleton();
        float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            const RE::FormID cID = cIdKey.raw();
            if (rec.stage == State::ChildStage::Adult) continue;

            const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
            if (!entry.HasGuardian(motherID)) continue;

            RE::FormID bioFatherID = GetBioFather(cID);
            if (bioFatherID == 0 && entry.bioFatherID != 0 && (formerSpouseID == 0 || !MatchesActor(entry.bioFatherID, formerSpouseID)) && !MatchesActor(entry.bioFatherID, motherID)) {
                bioFatherID = entry.bioFatherID;
            }

            if (bioFatherID == 0 || bioFatherID == kUnknownFatherFormID || (formerSpouseID != 0 && MatchesActor(bioFatherID, formerSpouseID))) continue;
            if (motherID == kPlayerFormID || bioFatherID == kPlayerFormID) continue;
            if (seenFathers.contains(bioFatherID)) continue;
            if (GetPrimaryPartner(bioFatherID) != 0) continue;
            if (IsMostRecentRelationshipDivorced(motherID, bioFatherID)) {
                MARAS_LOG_INFO("[Fertility] Bio father {:08X} skipped from step-in candidates: most recent relationship with mother {:08X} is divorced.",
                               bioFatherID, motherID);
                continue;
            }
            if (!CanFormRelationship(motherID, bioFatherID, RelationshipStatus::Married)) continue;

            float ageDays = (entry.stage == ChildStage::Unborn) ? 0.0f : std::max(0.0f, currentDay - entry.birthTimestamp);
            bool isPreg = (entry.stage == ChildStage::Unborn);
            std::string candChildName = (entry.name.empty() || StringUtils::CaseInsensitiveEquals(entry.name, "Newborn")) ? "child" : entry.name;
            candidates.push_back({ bioFatherID, isPreg, candChildName, ageDays });
            seenFathers.insert(bioFatherID);
        }

        if (candidates.empty()) return;

        bool useLLM = IsSkyrimNetAvailable() && !sSnap->paternityForceDiceRolls;
        if (!useLLM) {
            // Dice roll evaluation
            float stepUpChance = static_cast<float>(sSnap->paternityBioStepUpChance);
            for (const auto& cand : candidates) {
                if (IsMostRecentRelationshipDivorced(motherID, cand.bioFatherID)) continue;
                if (GetPrimaryPartner(cand.bioFatherID) != 0) continue;
                if (!CanFormRelationship(motherID, cand.bioFatherID, RelationshipStatus::Married)) continue;
                float stepUpRoll = Utils::RandomFloat(0.0f, 100.0f);
                MARAS_LOG_INFO("[Fertility] Step-in dice roll for bio father {:08X} with mother {:08X}: {:.1f}% (chance: {:.1f}%)",
                               cand.bioFatherID, motherID, stepUpRoll, stepUpChance);
                if (stepUpRoll < stepUpChance) {
                    if (ApplyBioFatherStepIn(motherID, cand.bioFatherID)) {
                        break; // First candidate agreed and married; terminate queue
                    }
                }
            }
            return;
        }

        // SkyrimNet LLM evaluation queue (cycle-free data state + SessionEpoch guard)
        auto queue = std::make_shared<StepInEvalQueue>();
        queue->motherID = motherID;
        queue->formerSpouseID = formerSpouseID;
        queue->candidates = std::move(candidates);
        queue->epoch = GetSessionEpoch();
        RunStepInCandidateAtIndex(std::move(queue), 0);
    }

    // ---------------------------------------------------------------------------
    // Fertility Mode Reloaded Integration & Phenotype Matching
    // ---------------------------------------------------------------------------

    static PhenotypeTier ClassifyPhenotypeTierFromEditorID(std::string_view rawId) {
        std::string s = StringUtils::ToLowerStd(rawId);
        if (s.empty()) return PhenotypeTier::Unknown;

        // Argonian (Reptilian / Saxhleel)
        if (s.find("argonianrace") != std::string::npos) {
            return PhenotypeTier::Argonian;
        }

        // Khajiit (Feline)
        if (s.find("khajiitrace") != std::string::npos) {
            return PhenotypeTier::Khajiit;
        }

        // Orc
        if (s.find("orcrace") != std::string::npos) {
            return PhenotypeTier::Orc;
        }

        // Mer / Elven races (High Elf, Wood Elf, Dark Elf, Snow Elf, Falmer)
        if (s.find("highelfrace") != std::string::npos ||
            s.find("woodelfrace") != std::string::npos ||
            s.find("darkelfrace") != std::string::npos ||
            s.find("snowelfrace") != std::string::npos ||
            s.find("falmerrace") != std::string::npos) {
            return PhenotypeTier::Mer;
        }

        // Redguard
        if (s.find("redguardrace") != std::string::npos) {
            return PhenotypeTier::Redguard;
        }

        // Pale Humans (Nord, Breton, Imperial, Elder)
        if (s.find("nordrace") != std::string::npos ||
            s.find("bretonrace") != std::string::npos ||
            s.find("imperialrace") != std::string::npos ||
            s.find("elderrace") != std::string::npos) {
            return PhenotypeTier::PaleHuman;
        }

        return PhenotypeTier::Unknown;
    }

    PhenotypeTier NPCRelationshipManager::GetActorPhenotypeTier(RE::Actor* actor) const {
        if (!actor) return PhenotypeTier::Unknown;
        auto* race = actor->GetRace();
        if (!race) return PhenotypeTier::Unknown;

        if (const char* formEditorID = race->GetFormEditorID(); formEditorID && *formEditorID) {
            PhenotypeTier tier = ClassifyPhenotypeTierFromEditorID(formEditorID);
            if (tier != PhenotypeTier::Unknown) return tier;
        }
        if (const char* rName = race->GetFullName(); rName && *rName) {
            return ClassifyPhenotypeTierFromEditorID(rName);
        }
        return PhenotypeTier::Unknown;
    }

    PhenotypeTier NPCRelationshipManager::GetFormIDPhenotypeTier(RE::FormID formID) const {
        if (formID == 0) return PhenotypeTier::Unknown;
        if (auto* refr = ActorSvc::GetReference(formID)) {
            if (auto* actor = refr->As<RE::Actor>()) {
                return GetActorPhenotypeTier(actor);
            }
        }
        std::string raceEdid = ActorSvc::GetRaceEditorID(formID);
        if (!raceEdid.empty()) {
            PhenotypeTier tier = ClassifyPhenotypeTierFromEditorID(raceEdid);
            if (tier != PhenotypeTier::Unknown) return tier;
        }
        std::string raceName = ActorSvc::GetRaceName(formID);
        return ClassifyPhenotypeTierFromEditorID(raceName);
    }

    bool NPCRelationshipManager::HasPhenotypeMismatch(RE::FormID father1ID, RE::FormID father2ID) const {
        if (father1ID == 0 || father2ID == 0) return false;
        PhenotypeTier t1 = GetFormIDPhenotypeTier(father1ID);
        PhenotypeTier t2 = GetFormIDPhenotypeTier(father2ID);
        if (t1 == PhenotypeTier::Unknown || t2 == PhenotypeTier::Unknown) return false;
        return t1 != t2;
    }

    std::string NPCRelationshipManager::GetVisualDiscrepancyDescription(RE::FormID motherID, RE::FormID presumedFatherID, RE::FormID bioFatherID) const {
        PhenotypeTier bioTier = GetFormIDPhenotypeTier(bioFatherID);
        PhenotypeTier presumedTier = GetFormIDPhenotypeTier(presumedFatherID);
        std::string bioRace = ActorSvc::GetRaceName(bioFatherID);
        std::string presumedRace = ActorSvc::GetRaceName(presumedFatherID);
        std::string motherRace = ActorSvc::GetRaceName(motherID);

        if (bioTier == PhenotypeTier::Argonian && presumedTier == PhenotypeTier::Khajiit) {
            return fmt::format("The newborn displays unmistakable reptilian scales, horns, and Argonian traits, completely contradicting the feline Khajiit heritage of the presumed father ({}).", presumedRace);
        }
        if (bioTier == PhenotypeTier::Khajiit && presumedTier == PhenotypeTier::Argonian) {
            return fmt::format("The newborn exhibits feline fur, ears, and Khajiit traits, completely contradicting the reptilian Argonian heritage of the presumed father ({}).", presumedRace);
        }
        if (bioTier == PhenotypeTier::Argonian) {
            return fmt::format("The child displays unmistakable reptilian scales, horns, and traits of an Argonian (Saxhleel lineage), completely contradicting the presumed father's ({}) heritage.", presumedRace);
        }
        if (bioTier == PhenotypeTier::Khajiit) {
            return fmt::format("The child displays unmistakable feline fur, ears, and traits of a Khajiit, completely contradicting the presumed father's ({}) heritage.", presumedRace);
        }
        if (presumedTier == PhenotypeTier::Argonian || presumedTier == PhenotypeTier::Khajiit) {
            return fmt::format("The child displays humanoid/{} features lacking any scales, fur, or beast traits, impossible for the offspring of {} ({}).", bioRace, ActorSvc::GetName(presumedFatherID), presumedRace);
        }
        if (bioTier == PhenotypeTier::Orc && presumedTier == PhenotypeTier::Mer) {
            return fmt::format("The child exhibits distinctive Orcish features—green-tinted complexion, prominent lower tusks, and a heavy brow—contrasting sharply with the refined elven heritage of {} ({}).", ActorSvc::GetName(presumedFatherID), presumedRace);
        }
        if (bioTier == PhenotypeTier::Mer && presumedTier == PhenotypeTier::Orc) {
            return fmt::format("The child displays delicate, classic elven traits lacking the green skin and tusks of an Orc, impossible for the offspring of {} ({}).", ActorSvc::GetName(presumedFatherID), presumedRace);
        }
        if (bioTier == PhenotypeTier::Orc) {
            return fmt::format("The child displays unmistakable Orcish traits—greenish skin, prominent lower tusks, and a rugged brow—completely contradicting the {} heritage of presumed father {}.", presumedRace, ActorSvc::GetName(presumedFatherID));
        }
        if (presumedTier == PhenotypeTier::Orc) {
            return fmt::format("The child completely lacks the green complexion and tusks of an Orc, bearing traits of a {} instead, which clashes noticeably with presumed father {}.", bioRace, ActorSvc::GetName(presumedFatherID));
        }
        if (bioTier == PhenotypeTier::Mer && (presumedTier == PhenotypeTier::PaleHuman || presumedTier == PhenotypeTier::Redguard)) {
            return fmt::format("The child exhibits distinct elven features and pointed ears characteristic of a {}, clashing noticeably with the human lineage of the presumed father ({}).", bioRace, presumedRace);
        }
        if (bioTier == PhenotypeTier::Redguard && presumedTier == PhenotypeTier::PaleHuman) {
            return fmt::format("The child has a deep, warm Redguard complexion, creating an immediate and undeniable visual contrast against the pale skin of {} (mother) and {} (presumed father).", motherRace, presumedRace);
        }
        if (bioTier == PhenotypeTier::PaleHuman && presumedTier == PhenotypeTier::Redguard) {
            return fmt::format("The child has strikingly pale northern skin, contrasting sharply with the deep complexion of the presumed father ({}).", presumedRace);
        }
        return fmt::format("The child's physical appearance unmistakably reflects the traits of a {}, which is distinct from the presumed father's {} lineage.", bioRace, presumedRace);
    }

    RE::FormID NPCRelationshipManager::GetPrimaryPartner(RE::FormID actorID) const {
        if (actorID == 0) return 0;
        auto married = GetPartners(actorID, RelationshipStatus::Married);
        if (!married.empty()) return married[0];
        auto engaged = GetPartners(actorID, RelationshipStatus::Engaged);
        if (!engaged.empty()) return engaged[0];
        auto dating = GetPartners(actorID, RelationshipStatus::Dating);
        if (!dating.empty()) return dating[0];
        return 0;
    }

    RE::FormID NPCRelationshipManager::FindChildPaternitySecret(RE::FormID parent1ID, RE::FormID parent2ID, const std::string& childName) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();

        auto matchesParent = [this](const ChildRegistryEntry& entry, RE::FormID pID) -> bool {
            if (pID == 0) return true;
            return entry.HasGuardian(pID) ||
                   MatchesActor(entry.bioMotherID, pID) || MatchesActor(entry.bioFatherID, pID) ||
                   MatchesActor(entry.presumedFatherID, pID) || MatchesActor(GetPrimaryPartner(entry.bioMotherID), pID);
        };

        bool hasName = !childName.empty() && !StringUtils::CaseInsensitiveEquals(childName, "all");

        auto matchesChildName = [](const ChildRegistryEntry& entry, const std::string& nameLower) -> bool {
            std::string entryNameLower = StringUtils::ToLowerStd(entry.name);
            if (!entryNameLower.empty() &&
                (entryNameLower == nameLower || entryNameLower.find(nameLower) != std::string::npos || nameLower.find(entryNameLower) != std::string::npos)) {
                return true;
            }
            if (entry.stage == ChildStage::Unborn) {
                if (nameLower.find("unborn") != std::string::npos || nameLower.find("pregnancy") != std::string::npos ||
                    nameLower.find("expecting") != std::string::npos || nameLower.find("baby") != std::string::npos ||
                    nameLower.find("child") != std::string::npos) {
                    return true;
                }
                std::string bioName = !entry.paternitySecret.bioFatherName.empty() ? entry.paternitySecret.bioFatherName : ActorSvc::GetName(entry.bioFatherID);
                if (!bioName.empty()) {
                    std::string bioNameLower = StringUtils::ToLowerStd(bioName);
                    if (nameLower.find(bioNameLower) != std::string::npos || bioNameLower.find(nameLower) != std::string::npos) {
                        return true;
                    }
                }
            }
            return false;
        };

        const auto& allChildren = snap->lineage->AllChildren();

        // Pass 1: Match childName and both parents (if provided)
        if (hasName) {
            std::string nameLower = StringUtils::ToLowerStd(childName);
            for (const auto& [cIdKey, rec] : allChildren) {
                const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (!matchesParent(entry, parent1ID) || !matchesParent(entry, parent2ID)) continue;
                if (matchesChildName(entry, nameLower)) {
                    return cIdKey.raw();
                }
            }
        }

        // Pass 2: If both parents provided, return first matching child with active secret
        if (parent1ID != 0 && parent2ID != 0) {
            for (const auto& [cIdKey, rec] : allChildren) {
                const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (matchesParent(entry, parent1ID) && matchesParent(entry, parent2ID)) {
                    return cIdKey.raw();
                }
            }
        }

        // Pass 3: If parent1 provided, return first matching child with active secret
        if (parent1ID != 0) {
            for (const auto& [cIdKey, rec] : allChildren) {
                const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (matchesParent(entry, parent1ID)) {
                    return cIdKey.raw();
                }
            }
        }

        // Pass 4: Fallback by name alone across any child with active secret
        if (hasName) {
            std::string nameLower = StringUtils::ToLowerStd(childName);
            for (const auto& [cIdKey, rec] : allChildren) {
                const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (matchesChildName(entry, nameLower)) {
                    return cIdKey.raw();
                }
            }
        }

        return 0;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildrenWithActiveSecretsForActor(RE::FormID actorID) const {
        std::vector<RE::FormID> result;
        if (actorID == 0) return result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            const auto entry = LegacyAdapters::ToLegacyChildEntry(rec);
            if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
            const RE::FormID cID = cIdKey.raw();
            if (entry.HasGuardian(actorID) || MatchesActor(entry.bioMotherID, actorID) ||
                MatchesActor(entry.bioFatherID, actorID) ||
                MatchesActor(entry.presumedFatherID, actorID) || MatchesActor(GetMotherPartner(cID), actorID)) {
                result.push_back(cID);
            }
        }
        return result;
    }

    void NPCRelationshipManager::SetNPCPregnant(RE::FormID motherID, bool pregnant, RE::FormID bioFatherID, RE::FormID /*presumedFatherID*/, float conceptionTime) {
        if (motherID == 0) return;
        if (pregnant) {
            if (FindUnbornChild(motherID) == 0) {
                RegisterUnbornChild(motherID, bioFatherID, conceptionTime);
            }
        } else {
            RemoveUnbornChild(motherID);
        }
    }

    bool NPCRelationshipManager::IsNPCPregnant(RE::FormID motherID) const {
        if (motherID == 0) return false;
        if (ActorSvc::GetSex(motherID) == 0) return false; // Male actors are never pregnant
        return FindUnbornChild(motherID) != 0;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetUnbornChildren(RE::FormID motherID) const {
        std::vector<RE::FormID> result;
        if (motherID == 0 || ActorSvc::GetSex(motherID) == 0) return result;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            if (rec.stage == State::ChildStage::Unborn) {
                if (MatchesActor(rec.bioMother.raw(), motherID)) {
                    result.push_back(cIdKey.raw());
                }
            }
        }
        return result;
    }

    RE::FormID NPCRelationshipManager::FindUnbornChild(RE::FormID motherID) const {
        auto unborn = GetUnbornChildren(motherID);
        return unborn.empty() ? 0 : unborn.front();
    }

    RE::FormID NPCRelationshipManager::RegisterUnbornChild(RE::FormID motherID, RE::FormID bioFatherID, float conceptionTime, bool evaluateStepIn) {
        if (motherID == 0) return 0;

        auto existingUnborn = GetUnbornChildren(motherID);
        size_t existingCount = existingUnborn.size();

        RE::FormID presumedFatherID = GetPrimaryPartner(motherID);
        if (bioFatherID == 0) {
            bioFatherID = (presumedFatherID != 0) ? presumedFatherID : kUnknownFatherFormID;
        }

        RE::FormID childID = AllocateSyntheticChildID();

        std::string motherName = ActorSvc::GetName(motherID);
        std::string childName = (motherName.empty() ? "Mother" : motherName) + "'s Unborn Child #" + std::to_string(existingCount + 1);

        float cTime = (conceptionTime > 0.0f) ? conceptionTime : (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f);

        if (motherID != 0) EnsureRegistered(motherID);
        if (presumedFatherID != 0) EnsureRegistered(presumedFatherID);
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) EnsureRegistered(bioFatherID);

        RE::FormID gA = motherID;
        RE::FormID gB = (presumedFatherID != 0 && IsMarried(motherID, presumedFatherID)) ? presumedFatherID : 0;
        RE::FormID bioM = motherID;
        RE::FormID bioF = bioFatherID;

        PaternitySecret pSec;
        RE::FormID recordedPresumedFatherID = 0;
        if (presumedFatherID != 0 && bioFatherID != 0 && !MatchesActor(bioFatherID, presumedFatherID)) {
            recordedPresumedFatherID = presumedFatherID;
            pSec.isSecret = true;
            pSec.isSecretForPartner = true;
            pSec.isSecretForBioFather = true;
            pSec.isPregnancySecretForPartner = true;
            pSec.discovered = false;
            pSec.discoveredByPartner = false;
            pSec.discoveredByBioFather = false;
            pSec.lastCheckTimestamp = cTime;
            if (bioFatherID != kUnknownFatherFormID) {
                pSec.bioFatherName = ActorSvc::GetName(bioFatherID);
                if (auto* r = ActorSvc::GetRace(bioFatherID)) {
                    pSec.bioFatherRaceID = r->GetFormID();
                }
            }
            pSec.hasPhenotypeMismatch = HasPhenotypeMismatch(presumedFatherID, bioFatherID);
        }

        ChildRegistryEntry entry(childID, gA, gB, childName, 0, false, bioM, bioF, true, cTime, 0.0f, pSec, ChildStage::Unborn, "", 2, recordedPresumedFatherID);
        EvaluateChildBeastbloodInheritance(entry);
        UpsertChildEntry(entry);
        UpdateSpouseChildrenFaction(motherID);
        if (gB != 0 && gB != motherID) {
            UpdateSpouseChildrenFaction(gB);
        }

        MARAS_LOG_INFO("[Fertility] Registered Unborn child {:08X} ('{}') for mother {:08X}. Presumed: {:08X}, GuardianB: {:08X}, Bio: {:08X}, Secret: {}",
                       childID, childName, motherID, recordedPresumedFatherID, gB, bioF, pSec.isSecret);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);

        // Step-in evaluation when mother has no official partner and bio father also has no main relationship.
        // Callers registering several fetuses in a batch pass evaluateStepIn=false until the last one so every father is a candidate.
        if (evaluateStepIn &&
            presumedFatherID == 0 &&
            bioFatherID != 0 && bioFatherID != kUnknownFatherFormID &&
            motherID != kPlayerFormID && bioFatherID != kPlayerFormID &&
            GetPrimaryPartner(bioFatherID) == 0 &&
            !IsMostRecentRelationshipDivorced(motherID, bioFatherID) &&
            CanFormRelationship(motherID, bioFatherID, RelationshipStatus::Married)) {
            MARAS_LOG_INFO("[Fertility] Mother {:08X} and Bio Father {:08X} have no official partners. Triggering Step-In evaluation for pregnancy.",
                           motherID, bioFatherID);
            EvaluateBioFatherStepIn(motherID, 0);
        } else if (evaluateStepIn && presumedFatherID == 0 && GetUnbornChildren(motherID).size() > 1) {
            // Batch pregnancy: the last fetus triggers a single evaluation covering every registered father
            EvaluateBioFatherStepIn(motherID, 0);
        }

        return childID;
    }

    RE::FormID NPCRelationshipManager::PromoteUnbornToInfant(RE::FormID motherID, float birthTime, const std::string& name, RE::FormID bioFatherID) {
        if (motherID == 0) return 0;
        auto unbornIDs = GetUnbornChildren(motherID);
        if (unbornIDs.empty()) {
            MARAS_LOG_WARN("[Fertility] PromoteUnbornToInfant: No unborn child found for mother {:08X}", motherID);
            return 0;
        }

        // Pick the unborn child fathered by bioFatherID; fall back to the first unborn if no father matches
        RE::FormID uID = 0;
        if (bioFatherID != 0) {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (RE::FormID candidate : unbornIDs) {
                const auto* rec = snap->lineage->FindChild(State::ChildId{ candidate });
                if (rec && MatchesActor(rec->bioFather.raw(), bioFatherID)) {
                    uID = candidate;
                    break;
                }
            }
            if (uID == 0) {
                MARAS_LOG_WARN("[Fertility] PromoteUnbornToInfant: No unborn child of mother {:08X} fathered by {:08X}, promoting first unborn instead",
                               motherID, bioFatherID);
            }
        }
        if (uID == 0) uID = unbornIDs.front();

        auto entryOpt = GetChildEntry(uID);
        if (!entryOpt.has_value()) return 0;
        ChildRegistryEntry entry = std::move(*entryOpt);

        float bTime = (birthTime > 0.0f) ? birthTime : (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f);
        float birthChance = static_cast<float>(Settings::GetSingleton().Snapshot()->paternityPhenotypeBirthChance);

        entry.stage = ChildStage::Infant;
        entry.birthTimestamp = bTime;
        entry.timestamp = static_cast<int32_t>(bTime);
        entry.name = name.empty() ? "Newborn" : name;
        entry.paternitySecret.isPregnancySecretForPartner = false;
        RE::FormID prevCoGuardian = GetCoGuardian(uID, motherID);
        if (!entry.paternitySecret.isSecret && !entry.paternitySecret.discovered) {
            entry.presumedFatherID = 0;
        } else if (entry.presumedFatherID == 0 ||
                   MatchesActor(entry.presumedFatherID, motherID) ||
                   (entry.bioFatherID != 0 && MatchesActor(entry.presumedFatherID, entry.bioFatherID))) {
            RE::FormID partnerID = GetPrimaryPartner(motherID);
            if (partnerID != 0 && !MatchesActor(partnerID, motherID) &&
                (entry.bioFatherID == 0 || !MatchesActor(partnerID, entry.bioFatherID))) {
                entry.presumedFatherID = partnerID;
            } else {
                entry.presumedFatherID = 0;
            }
        }
        RE::FormID presumedFatherID = entry.presumedFatherID;
        if (presumedFatherID == 0 && entry.paternitySecret.isSecretForPartner) {
            entry.paternitySecret.isSecretForPartner = false;
            if (!entry.paternitySecret.isSecretForBioFather) {
                entry.paternitySecret.isSecret = false;
                entry.paternitySecret.discovered = true;
            }
        }
        UpsertChildEntry(entry);
        ApplyBirthCustodyRules(uID, entry, motherID, presumedFatherID, entry.bioFatherID);
        EvaluateChildBeastbloodInheritance(entry);
        AdultChildProgression::GetSingleton().EvaluateChildBioLineageAtBirth(*this, entry);
        UpsertChildEntry(entry);

        UpdateChildBothParentsFaction(uID);
        UpdateSpouseChildrenFaction(motherID);
        if (prevCoGuardian != 0) UpdateSpouseChildrenFaction(prevCoGuardian);
        if (entry.guardianAID != 0 && entry.guardianAID != motherID && entry.guardianAID != prevCoGuardian) {
            UpdateSpouseChildrenFaction(entry.guardianAID);
        }
        if (entry.guardianBID != 0 && entry.guardianBID != motherID && entry.guardianBID != prevCoGuardian &&
            entry.guardianBID != entry.guardianAID) {
            UpdateSpouseChildrenFaction(entry.guardianBID);
        }

        MARAS_LOG_INFO("[Fertility] Promoted Unborn child {:08X} to Infant ('{}') for mother {:08X}. Birth: {:.2f}, SecretPartner: {}, SecretBio: {}",
                       uID, entry.name, motherID, bTime,
                       entry.paternitySecret.isSecretForPartner,
                       entry.paternitySecret.isSecretForBioFather);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);

        // Check phenotype mismatch at birth: if parents have stark phenotype difference, roll configured birth discovery chance
        bool evaluateAtBirth = false;
        if (entry.paternitySecret.hasPhenotypeMismatch && entry.paternitySecret.isSecretForPartner) {
            float roll = Utils::RandomFloat(0.0f, 100.0f);
            if (roll < birthChance) {
                MARAS_LOG_INFO("[Fertility] Phenotype mismatch noticed at birth for child {:08X} (roll: {:.1f}%, chance: {:.1f}%). Triggering paternity secret evaluation.",
                               uID, roll, birthChance);
                evaluateAtBirth = true;
            }
        }

        if (evaluateAtBirth) {
            EvaluatePaternitySecret(uID);
        }

        return uID;
    }

    bool NPCRelationshipManager::RemoveUnbornChild(RE::FormID motherID, RE::FormID bioFatherID) {
        if (motherID == 0) return false;
        auto unbornIDs = GetUnbornChildren(motherID);
        if (unbornIDs.empty()) return false;

        if (bioFatherID != 0) {
            RE::FormID targetID = 0;
            std::unordered_set<RE::FormID> affectedGuardians;
            {
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                for (RE::FormID uID : unbornIDs) {
                    const auto* rec = snap->lineage->FindChild(State::ChildId{ uID });
                    if (rec && MatchesActor(rec->bioFather.raw(), bioFatherID)) {
                        targetID = uID;
                        if (!rec->guardians.slotA.IsNull()) affectedGuardians.insert(rec->guardians.slotA.raw());
                        if (!rec->guardians.slotB.IsNull()) affectedGuardians.insert(rec->guardians.slotB.raw());
                        if (!rec->bioMother.IsNull()) affectedGuardians.insert(rec->bioMother.raw());
                        break;
                    }
                }
            }
            if (targetID == 0) {
                MARAS_LOG_WARN("[Fertility] RemoveUnbornChild: no unborn child of mother {:08X} fathered by {:08X}", motherID, bioFatherID);
                return false;
            }
            bool erased = false;
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                erased = ws.lineage.Write().EraseChild(State::ChildId{ targetID });
            });
            if (erased) {
                for (RE::FormID gid : affectedGuardians) {
                    if (gid != 0) UpdateSpouseChildrenFaction(gid);
                }
                MARAS_LOG_INFO("[Fertility] Removed Unborn child {:08X} fathered by {:08X} for mother {:08X} (stillbirth)", targetID, bioFatherID, motherID);
                PrismaUIDataBridge::GetSingleton().OnChildRemoved(targetID);
                return true;
            }
            return false;
        }

        std::unordered_set<RE::FormID> affectedGuardians{ motherID };
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (RE::FormID uID : unbornIDs) {
                if (const auto* rec = snap->lineage->FindChild(State::ChildId{ uID })) {
                    if (!rec->guardians.slotA.IsNull()) affectedGuardians.insert(rec->guardians.slotA.raw());
                    if (!rec->guardians.slotB.IsNull()) affectedGuardians.insert(rec->guardians.slotB.raw());
                    if (!rec->bioMother.IsNull()) affectedGuardians.insert(rec->bioMother.raw());
                }
            }
        }
        std::vector<RE::FormID> removedIDs;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            for (RE::FormID uID : unbornIDs) {
                if (ws.lineage.Write().EraseChild(State::ChildId{ uID })) {
                    removedIDs.push_back(uID);
                }
            }
        });
        for (RE::FormID gid : affectedGuardians) {
            if (gid != 0) UpdateSpouseChildrenFaction(gid);
        }
        for (RE::FormID uID : removedIDs) {
            MARAS_LOG_INFO("[Fertility] Removed Unborn child {:08X} for mother {:08X} (pregnancy cleared/miscarried)", uID, motherID);
            PrismaUIDataBridge::GetSingleton().OnChildRemoved(uID);
        }
        return !removedIDs.empty();
    }

    namespace {
        struct PregnancyAnnouncedResult {
            std::string message;
            std::string bioFatherJson;
            std::string motherName;
            std::string partnerName;
        };

        PregnancyAnnouncedResult BuildPregnancyAnnouncedInternal(
            const NPCRelationshipManager& mgr,
            RE::FormID motherID,
            RE::FormID partnerID,
            RE::FormID fallbackBioID,
            const std::string& fallbackBioName,
            int32_t childCount)
        {
            PregnancyAnnouncedResult result;
            if (motherID == 0) return result;

            result.motherName = ActorSvc::GetName(motherID);
            if (result.motherName.empty()) result.motherName = "Mother";

            if (partnerID == 0) {
                partnerID = mgr.GetPrimaryPartner(motherID);
            }
            if (partnerID != 0) {
                result.partnerName = ActorSvc::GetName(partnerID);
            }

            std::string bioNameFallback = fallbackBioName;
            if (bioNameFallback.empty() && fallbackBioID != 0 && fallbackBioID != kUnknownFatherFormID) {
                bioNameFallback = ActorSvc::GetName(fallbackBioID);
            }

            auto unbornIDs = mgr.GetUnbornChildren(motherID);
            if (static_cast<int32_t>(unbornIDs.size()) > childCount) {
                childCount = static_cast<int32_t>(unbornIDs.size());
            }

            struct FatherGroup {
                RE::FormID bioFatherID{ 0 };
                std::string name;
                int count{ 0 };
                bool isPartner{ false };
                bool isSecret{ false };
            };
            std::vector<FatherGroup> groups;

            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto motherView = Projection::PerspectiveView::ForActor(
                worldSnap, spatialSnap, State::ActorId{ motherID }, &GetEngineWerewolfStateProvider());
            const auto partnerView =
                (partnerID != 0)
                    ? std::optional<Projection::PerspectiveView>(Projection::PerspectiveView::ForActor(
                          worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider()))
                    : std::nullopt;

            for (RE::FormID uID : unbornIDs) {
                const State::ChildId childKey{ uID };
                const auto perceivedMotherOpt = motherView.GetPerceivedChild(childKey);
                if (!perceivedMotherOpt.has_value()) continue;
                const auto& perceived = *perceivedMotherOpt;

                const RE::FormID bID = perceived.perceivedBioFather.raw();
                std::string fName;
                if (bID != 0 && bID != kUnknownFatherFormID) {
                    fName = ResolveActorNameSafe(bID, *worldSnap, *spatialSnap);
                }
                if (fName.empty()) {
                    fName = perceived.perceivedBioFatherFallbackName;
                }
                if (fName.empty()) {
                    fName = !bioNameFallback.empty() ? bioNameFallback : "an unknown father";
                }

                const bool isPart = (bID != 0 && partnerID != 0 && bID == partnerID) || (!result.partnerName.empty() && fName == result.partnerName);
                bool isSec = false;
                if (partnerView.has_value()) {
                    isSec = !partnerView->CanViewerKnowTrueBioFather(childKey) || !partnerView->CanViewerKnowPregnancy(childKey);
                }

                auto it = std::find_if(groups.begin(), groups.end(), [&](const FatherGroup& g) {
                    if (bID != 0 && bID != kUnknownFatherFormID && g.bioFatherID != 0) {
                        return g.bioFatherID == bID;
                    }
                    return g.name == fName;
                });

                if (it != groups.end()) {
                    it->count++;
                    if (isSec) it->isSecret = true;
                } else {
                    FatherGroup fg;
                    fg.bioFatherID = bID;
                    fg.name = fName;
                    fg.count = 1;
                    fg.isPartner = isPart;
                    fg.isSecret = isSec;
                    groups.push_back(fg);
                }
            }

            if (groups.empty()) {
                std::string fallbackName = !bioNameFallback.empty() ? bioNameFallback : result.partnerName;
                if (fallbackName.empty()) {
                    fallbackName = "an unknown father";
                }
                FatherGroup fg;
                fg.bioFatherID = fallbackBioID;
                fg.name = fallbackName;
                fg.count = childCount > 0 ? childCount : 1;
                fg.isPartner = (!result.partnerName.empty() && fallbackName == result.partnerName);
                fg.isSecret = (partnerID != 0 && !fg.isPartner);
                groups.push_back(fg);
            }

            int totalChildren = 0;
            for (const auto& g : groups) {
                totalChildren += g.count;
            }
            if (totalChildren > childCount) {
                childCount = totalChildren;
            } else if (childCount > totalChildren && !groups.empty()) {
                groups.front().count += (childCount - totalChildren);
            }

            if (groups.size() > 1) {
                result.message = result.motherName + " is surprised to discover that she is expecting " + std::to_string(childCount) + " children: ";

                bool hasPrevious = false;
                int partnerIdx = -1;

                for (size_t p = 0; p < groups.size(); ++p) {
                    if (groups[p].isPartner) {
                        partnerIdx = static_cast<int>(p);
                        int pNum = groups[p].count;
                        if (pNum == 1) {
                            result.message += "1 child is from " + result.partnerName + " (her current partner)";
                        } else {
                            result.message += std::to_string(pNum) + " children are from " + result.partnerName + " (her current partner)";
                        }
                        hasPrevious = true;
                        break;
                    }
                }

                int secretOthers = 0;
                for (size_t o = 0; o < groups.size(); ++o) {
                    if (static_cast<int>(o) != partnerIdx) {
                        if (hasPrevious) {
                            result.message += ", ";
                        }
                        int oNum = groups[o].count;
                        const std::string& oName = groups[o].name;
                        if (oNum == 1) {
                            result.message += "1 child from " + oName;
                        } else {
                            result.message += std::to_string(oNum) + " children from " + oName;
                        }
                        hasPrevious = true;

                        if (groups[o].isSecret) {
                            secretOthers++;
                        }

                        if (!result.bioFatherJson.empty()) {
                            result.bioFatherJson += ", ";
                        }
                        result.bioFatherJson += oName;
                    }
                }

                if (!result.partnerName.empty() && secretOthers > 0) {
                    result.message += ", while her current partner " + result.partnerName + " does not know she is pregnant yet and neither " + result.partnerName + " nor the biological father knows unless she chooses to tell them right now.";
                } else if (!result.partnerName.empty() && partnerIdx < 0) {
                    result.message += ", though " + result.partnerName + " is her current partner.";
                } else {
                    result.message += ".";
                }
            } else {
                if (childCount > 1) {
                    result.message = result.motherName + " is surprised to discover that she is expecting " + std::to_string(childCount) + " children";
                } else {
                    result.message = result.motherName + " is surprised to discover that she is expecting a child";
                }

                const auto& single = groups.front();
                const std::string& singleName = single.name;
                bool isPart = single.isPartner;
                bool isSec = single.isSecret;
                result.bioFatherJson = singleName;

                if (!result.partnerName.empty() && isPart) {
                    result.message += " with " + result.partnerName + ".";
                } else if (!result.partnerName.empty() && !singleName.empty() && singleName != "an unknown father" && singleName != "Unknown") {
                    if (isSec) {
                        result.message += " fathered by " + singleName + " (while " + result.partnerName + " is her current partner). Neither " + result.partnerName + " nor " + singleName + " knows that she is pregnant yet unless she chooses to tell them right now.";
                    } else {
                        result.message += " fathered by " + singleName + ", though " + result.partnerName + " is her partner.";
                    }
                } else if (!result.partnerName.empty()) {
                    if (isSec) {
                        result.message += " (while " + result.partnerName + " is her current partner). " + result.partnerName + " does not know that she is pregnant yet unless she chooses to tell him right now.";
                    } else {
                        result.message += " with " + result.partnerName + ".";
                    }
                } else if (!singleName.empty() && singleName != "an unknown father" && singleName != "Unknown") {
                    result.message += ", knowing the biological father is " + singleName + ".";
                } else {
                    result.message += ".";
                }
            }

            return result;
        }

        struct ChildBirthResult {
            std::string message;
            std::string motherName;
            std::string partnerName;
            struct ChildJsonInfo {
                std::string name;
                std::string gender;
                std::string relation;
                std::string bioFather;
                bool isSecret{ false };
            };
            std::vector<ChildJsonInfo> childrenJson;
        };

        struct BirthChildItem {
            RE::FormID childID{ 0 };
            std::string name;
            int gender{ 2 }; // 0=male, 1=female, 2=unknown
            std::string relation; // "son", "daughter", "child"
            bool isSecretForPartner{ false };
            bool isSecretForBio{ false };
        };

        struct BirthFatherGroup {
            RE::FormID bioFatherID{ 0 };
            std::string name;
            bool isPartner{ false };
            std::vector<BirthChildItem> children;
        };

        std::string FormatChildItem(const BirthChildItem& c, bool withArticle = true) {
            std::string rel = c.relation.empty() ? "child" : c.relation;
            if (!withArticle) {
                return c.name.empty() ? rel : (rel + ", " + c.name);
            }
            std::string base = (rel == "child" ? "a child" : (rel == "son" ? "a son" : "a daughter"));
            return c.name.empty() ? base : (base + ", " + c.name);
        }

        std::string FormatChildrenGroup(const std::vector<BirthChildItem>& children) {
            if (children.empty()) return "";
            if (children.size() == 1) {
                return FormatChildItem(children[0], true);
            }
            if (children.size() == 2) {
                bool bothGirls = (children[0].gender == 1 && children[1].gender == 1);
                bool bothBoys = (children[0].gender == 0 && children[1].gender == 0);
                if (bothGirls) {
                    return (!children[0].name.empty() && !children[1].name.empty()) ? ("twin daughters, " + children[0].name + " and " + children[1].name) : "twin daughters";
                }
                if (bothBoys) {
                    return (!children[0].name.empty() && !children[1].name.empty()) ? ("twin sons, " + children[0].name + " and " + children[1].name) : "twin sons";
                }
                return FormatChildItem(children[0], true) + ", and " + FormatChildItem(children[1], true);
            }
            if (children.size() == 3) {
                bool allGirls = (children[0].gender == 1 && children[1].gender == 1 && children[2].gender == 1);
                bool allBoys = (children[0].gender == 0 && children[1].gender == 0 && children[2].gender == 0);
                if (allGirls) {
                    return (!children[0].name.empty() && !children[1].name.empty() && !children[2].name.empty()) ? ("triplet daughters, " + children[0].name + ", " + children[1].name + ", and " + children[2].name) : "triplet daughters";
                }
                if (allBoys) {
                    return (!children[0].name.empty() && !children[1].name.empty() && !children[2].name.empty()) ? ("triplet sons, " + children[0].name + ", " + children[1].name + ", and " + children[2].name) : "triplet sons";
                }
            }

            std::string res;
            for (size_t i = 0; i < children.size(); ++i) {
                if (i > 0) {
                    res += (i == children.size() - 1 ? ", and " : ", ");
                }
                res += FormatChildItem(children[i], true);
            }
            return res;
        }

        ChildBirthResult BuildChildBirthInternal(
            const NPCRelationshipManager& mgr,
            RE::FormID motherID,
            const std::vector<RE::FormID>& childIDs,
            bool maskSecrets = false)
        {
            ChildBirthResult result;
            if (motherID == 0) return result;

            result.motherName = ActorSvc::GetName(motherID);
            if (result.motherName.empty()) result.motherName = "Mother";

            RE::FormID partnerID = mgr.GetPrimaryPartner(motherID);
            if (partnerID == 0) {
                for (RE::FormID cID : childIDs) {
                    RE::FormID mpID = mgr.GetMotherPartner(cID);
                    if (mpID != 0 && !MatchesActor(mpID, motherID)) {
                        partnerID = mpID;
                        break;
                    }
                }
            }
            if (partnerID != 0) {
                result.partnerName = ActorSvc::GetName(partnerID);
            }

            const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
            const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
            const auto view =
                maskSecrets
                    ? Projection::PerspectiveView::ForDialogueTarget(
                          worldSnap, spatialSnap, &GetEngineWerewolfStateProvider())
                    : Projection::PerspectiveView::ForDashboard(
                          worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());

            std::vector<RE::FormID> targetIDs = childIDs;
            if (targetIDs.empty()) {
                for (const auto& perceivedChild : view.GetVisibleChildren()) {
                    if (MatchesActor(perceivedChild.bioMother.raw(), motherID) ||
                        perceivedChild.guardians.HasGuardian(State::ActorId{ motherID })) {
                        if (perceivedChild.stage == State::ChildStage::Infant ||
                            perceivedChild.stage == State::ChildStage::Child) {
                            targetIDs.push_back(perceivedChild.childId.raw());
                        }
                    }
                }
            }
            if (targetIDs.empty()) return result;

            std::vector<BirthFatherGroup> groups;

            for (RE::FormID cID : targetIDs) {
                const State::ChildId childKey{ cID };
                const auto perceivedOpt = view.GetPerceivedChild(childKey);
                if (!perceivedOpt.has_value()) continue;
                const auto& perceived = *perceivedOpt;

                BirthChildItem item;
                item.childID = cID;
                item.name = (perceived.name.empty() || StringUtils::CaseInsensitiveEquals(perceived.name, "Newborn")) ? "" : perceived.name;
                item.gender = perceived.gender;
                if (item.gender == 0) {
                    item.relation = "son";
                } else if (item.gender == 1) {
                    item.relation = "daughter";
                } else {
                    item.relation = "child";
                }

                const RE::FormID bID = maskSecrets ? perceived.publicBioFather.raw() : perceived.perceivedBioFather.raw();
                std::string fName;
                if (bID != 0 && bID != kUnknownFatherFormID) {
                    fName = ResolveActorNameSafe(bID, *worldSnap, *spatialSnap);
                }
                if (fName.empty() && !maskSecrets) {
                    fName = perceived.perceivedBioFatherFallbackName;
                }
                if (fName.empty()) {
                    fName = "an unknown father";
                }

                const bool isPart = (bID != 0 && partnerID != 0 && bID == partnerID) || (!result.partnerName.empty() && fName == result.partnerName);
                bool isSecPartner = false;
                bool isSecBio = false;
                if (!maskSecrets) {
                    if (partnerID != 0 && !isPart) {
                        const auto partnerView = Projection::PerspectiveView::ForActor(
                            worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider());
                        isSecPartner = !partnerView.CanViewerKnowTrueBioFather(childKey);
                    }
                    if (bID != 0 && bID != kUnknownFatherFormID && !isPart) {
                        const auto bioView = Projection::PerspectiveView::ForActor(
                            worldSnap, spatialSnap, State::ActorId{ bID }, &GetEngineWerewolfStateProvider());
                        isSecBio = !bioView.CanViewerKnowTrueBioFather(childKey);
                    }
                }
                item.isSecretForPartner = isSecPartner;
                item.isSecretForBio = isSecBio;

                ChildBirthResult::ChildJsonInfo cJson;
                cJson.name = item.name.empty() ? "child" : item.name;
                cJson.gender = (item.gender == 0 ? "male" : (item.gender == 1 ? "female" : "unknown"));
                cJson.relation = item.relation;
                cJson.bioFather = fName;
                cJson.isSecret = isSecPartner;
                result.childrenJson.push_back(cJson);

                auto it = std::find_if(groups.begin(), groups.end(), [&](const BirthFatherGroup& g) {
                    if (bID != 0 && bID != kUnknownFatherFormID && g.bioFatherID != 0) {
                        return g.bioFatherID == bID;
                    }
                    return g.name == fName;
                });

                if (it != groups.end()) {
                    it->children.push_back(item);
                } else {
                    BirthFatherGroup fg;
                    fg.bioFatherID = bID;
                    fg.name = fName;
                    fg.isPartner = isPart;
                    fg.children.push_back(item);
                    groups.push_back(fg);
                }
            }

            if (groups.empty()) return result;

            int totalChildren = 0;
            for (const auto& g : groups) {
                totalChildren += static_cast<int>(g.children.size());
            }

            // Case A: Single Biological Father (Same Father for all children)
            if (groups.size() == 1) {
                const auto& fg = groups.front();
                std::string childPhrase = FormatChildrenGroup(fg.children);

                if (totalChildren == 1) {
                    const auto& c = fg.children.front();
                    if (fg.isPartner) {
                        result.message = result.motherName + " gave birth to a healthy " + c.relation + (c.name.empty() ? "" : ", " + c.name + ",") + " fathered by " + fg.name + ".";
                    } else if (!result.partnerName.empty()) {
                        result.message = result.motherName + " gave birth to " + (c.name.empty() ? ("a newborn " + c.relation) : c.name) + ". ";
                        if (c.isSecretForPartner) {
                            result.message += result.partnerName + " believes the newborn " + c.relation + " is his, unaware that the true biological father is " + fg.name + ".";
                        } else {
                            result.message += "The " + c.relation + " was fathered by " + fg.name + ", though " + result.partnerName + " is her partner.";
                        }
                        if (c.isSecretForBio) {
                            result.message += " Furthermore, " + fg.name + " has not been informed that he fathered the child.";
                        }
                    } else {
                        if (c.isSecretForBio) {
                            result.message = result.motherName + " gave birth to " + (c.name.empty() ? ("a " + c.relation) : (c.name + ", a " + c.relation)) + " fathered by " + fg.name + ", though " + fg.name + " is unaware of the child.";
                        } else {
                            result.message = result.motherName + " gave birth to a healthy " + c.relation + (c.name.empty() ? "" : ", " + c.name + ",") + " fathered by " + fg.name + ".";
                        }
                    }
                } else {
                    bool hasSecretForPartner = false;
                    bool hasSecretForBio = false;
                    for (const auto& c : fg.children) {
                        if (c.isSecretForPartner) hasSecretForPartner = true;
                        if (c.isSecretForBio) hasSecretForBio = true;
                    }

                    if (fg.isPartner) {
                        result.message = result.motherName + " gave birth to " + childPhrase + ", fathered by " + fg.name + ".";
                    } else if (!result.partnerName.empty()) {
                        result.message = result.motherName + " gave birth to " + childPhrase + ". ";
                        if (hasSecretForPartner) {
                            result.message += result.partnerName + " believes the newborn children are his, unaware that the true biological father is " + fg.name + ".";
                        } else {
                            result.message += "The children were fathered by " + fg.name + ", though " + result.partnerName + " is her partner.";
                        }
                        if (hasSecretForBio) {
                            result.message += " Furthermore, " + fg.name + " has not been informed that he fathered the children.";
                        }
                    } else {
                        if (hasSecretForBio) {
                            result.message = result.motherName + " gave birth to " + childPhrase + ", fathered by " + fg.name + ", though " + fg.name + " is unaware of the children.";
                        } else {
                            result.message = result.motherName + " gave birth to " + childPhrase + ", fathered by " + fg.name + ".";
                        }
                    }
                }
            } else {
                // Case B: Multiple Biological Fathers (Superfecundation)
                std::string countWord = (totalChildren == 2 ? "twins: " : (std::to_string(totalChildren) + " children: "));
                result.message = result.motherName + " gave birth to " + countWord;

                int partnerGroupIdx = -1;
                for (size_t i = 0; i < groups.size(); ++i) {
                    if (groups[i].isPartner) {
                        partnerGroupIdx = static_cast<int>(i);
                        break;
                    }
                }

                if (partnerGroupIdx >= 0) {
                    result.message += FormatChildrenGroup(groups[partnerGroupIdx].children) + ", fathered by " + groups[partnerGroupIdx].name;
                    if (!result.partnerName.empty()) {
                        result.message += " (her partner)";
                    }
                }

                bool firstOther = (partnerGroupIdx < 0);
                for (size_t i = 0; i < groups.size(); ++i) {
                    if (static_cast<int>(i) == partnerGroupIdx) continue;
                    if (!firstOther) {
                        result.message += "; and ";
                    }
                    result.message += FormatChildrenGroup(groups[i].children) + ", fathered by " + groups[i].name;
                    firstOther = false;
                }
                result.message += ".";

                if (partnerID != 0 && partnerGroupIdx >= 0) {
                    std::vector<std::string> secretOtherFathers;
                    std::string secretChildDesc;
                    bool anySecretForBio = false;

                    for (size_t i = 0; i < groups.size(); ++i) {
                        if (static_cast<int>(i) == partnerGroupIdx) continue;
                        bool sec = false;
                        for (const auto& c : groups[i].children) {
                            if (c.isSecretForPartner) sec = true;
                            if (c.isSecretForBio) anySecretForBio = true;
                        }
                        if (sec) {
                            secretOtherFathers.push_back(groups[i].name);
                            if (groups[i].children.size() == 1) {
                                secretChildDesc = "the newborn " + groups[i].children[0].relation;
                            } else {
                                secretChildDesc = "the other children";
                            }
                        }
                    }

                    if (!secretOtherFathers.empty()) {
                        if (secretOtherFathers.size() == 1) {
                            result.message += " " + result.partnerName + " believes " + secretChildDesc + " is his, unaware that the true biological father is " + secretOtherFathers[0] + ".";
                            if (anySecretForBio) {
                                result.message += " Furthermore, " + secretOtherFathers[0] + " has not been informed that he fathered the child.";
                            }
                        } else {
                            result.message += " " + result.partnerName + " is unaware of the true biological paternity of the other children.";
                        }
                    }
                } else if (partnerID != 0 && partnerGroupIdx < 0) {
                    bool anySecretForPartner = false;
                    for (const auto& g : groups) {
                        for (const auto& c : g.children) {
                            if (c.isSecretForPartner) anySecretForPartner = true;
                        }
                    }
                    if (anySecretForPartner) {
                        result.message += " " + result.partnerName + " (her partner) is completely unaware of the true biological paternity.";
                    } else {
                        result.message += " though " + result.partnerName + " is her partner.";
                    }
                }
            }

            return result;
        }
    }

    std::string NPCRelationshipManager::BuildPregnancyAnnouncedMessage(RE::FormID motherID, RE::FormID partnerID, RE::FormID fallbackBioID, const std::string& fallbackBioName, int32_t childCount) const {
        auto res = BuildPregnancyAnnouncedInternal(*this, motherID, partnerID, fallbackBioID, fallbackBioName, childCount);
        return res.message;
    }

    std::string NPCRelationshipManager::BuildPregnancyAnnouncedJson(RE::FormID motherID, RE::FormID partnerID, RE::FormID fallbackBioID, const std::string& fallbackBioName, int32_t childCount) const {
        auto res = BuildPregnancyAnnouncedInternal(*this, motherID, partnerID, fallbackBioID, fallbackBioName, childCount);
        if (res.message.empty()) return "";

        nlohmann::json j;
        RE::Actor* motherActor = (motherID == kPlayerFormID)
                                     ? RE::PlayerCharacter::GetSingleton()
                                     : NPCIndex::GetSingleton().GetActorFromFormId(motherID);
        bool skipTrigger = !ShouldTriggerSkyrimNetNarration(motherActor, nullptr);

        j["type"] = "ttm_pregnancy_announced";
        j["skipTrigger"] = skipTrigger;
        j["msg"] = res.message;
        // computed dynamically above via ShouldTriggerSkyrimNetNarration
        if (!res.motherName.empty()) j["mother"] = res.motherName;
        if (!res.partnerName.empty()) j["partner"] = res.partnerName;
        if (!res.bioFatherJson.empty()) j["bioFather"] = res.bioFatherJson;
        j["_marasMemory"] = {
            {"motherId", motherID},
            {"partnerId", partnerID},
            {"fallbackBioId", fallbackBioID},
            {"fallbackBioName", fallbackBioName},
            {"childCount", childCount}
        };

        MARAS_LOG_INFO("[PregnancyAnnounced] (skipTrigger={}) {}", skipTrigger, res.message);
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    }

    std::string NPCRelationshipManager::BuildChildBirthMessage(RE::FormID motherID, const std::vector<RE::FormID>& childIDs, bool maskSecrets) const {
        auto res = BuildChildBirthInternal(*this, motherID, childIDs, maskSecrets);
        return res.message;
    }

    std::string NPCRelationshipManager::BuildChildBirthJson(RE::FormID motherID, const std::vector<RE::FormID>& childIDs, bool maskSecrets) const {
        auto res = BuildChildBirthInternal(*this, motherID, childIDs, maskSecrets);
        if (res.message.empty()) return "";

        nlohmann::json j;
        RE::Actor* motherActor = (motherID == kPlayerFormID)
                                     ? RE::PlayerCharacter::GetSingleton()
                                     : NPCIndex::GetSingleton().GetActorFromFormId(motherID);
        bool skipTrigger = !ShouldTriggerSkyrimNetNarration(motherActor, nullptr);
        if (maskSecrets) {
            if (motherID != kPlayerFormID) {
                skipTrigger = true;
            } else {
                RE::FormID partnerID = GetPrimaryPartner(motherID);
                RE::Actor* partnerActor = (partnerID != 0) ? NPCIndex::GetSingleton().GetActorFromFormId(partnerID) : nullptr;
                skipTrigger = !ShouldTriggerSkyrimNetNarration(partnerActor, motherActor);
            }
        }

        j["type"] = "ttm_child_birth";
        j["skipTrigger"] = skipTrigger;
        j["msg"] = res.message;
        // computed dynamically above via ShouldTriggerSkyrimNetNarration
        if (!res.motherName.empty()) {
            j["mother"] = res.motherName;
        }
        if (!res.partnerName.empty()) {
            j["partner"] = res.partnerName;
        }
        nlohmann::json childrenArr = nlohmann::json::array();
        for (const auto& c : res.childrenJson) {
            nlohmann::json cJson;
            cJson["name"] = c.name;
            cJson["gender"] = c.gender;
            cJson["relation"] = c.relation;
            cJson["bioFather"] = c.bioFather;
            cJson["isSecret"] = c.isSecret;
            childrenArr.push_back(cJson);
        }
        j["children"] = childrenArr;
        if (!maskSecrets) {
            j["_marasMemory"] = {
                {"motherId", motherID},
                {"childIds", childIDs}
            };
        }

        MARAS_LOG_INFO("[ChildBirth] (maskSecrets={}, skipTrigger={}) {}", maskSecrets, skipTrigger, res.message);
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    }

    RE::FormID NPCRelationshipManager::RegisterFertilityChild(RE::FormID syntheticChildID, RE::FormID motherID, RE::FormID presumedFatherID, RE::FormID bioFatherID, float conceptionTime, float birthTime, const std::string& name) {
        if (motherID == 0) return 0;

        if (presumedFatherID == 0) {
            presumedFatherID = GetPrimaryPartner(motherID);
        }
        if (bioFatherID == 0) {
            bioFatherID = presumedFatherID;
        }

        std::string childName = name;
        if (childName.empty()) {
            childName = fmt::format("{}'s Child", ActorSvc::GetName(motherID));
        }

        float bTime = birthTime;
        if (bTime <= 0.0f) {
            auto calendar = RE::Calendar::GetSingleton();
            bTime = calendar ? calendar->GetDaysPassed() : 0.0f;
        }

        // Idempotency check: a duplicate birth event for the same infant (same mother, bio father, name and birth day) returns the existing record.
        // Siblings (twins/triplets) differ by name or father and are registered normally.
        if (syntheticChildID == 0) {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
                if (rec.stage != State::ChildStage::Infant) continue;
                if (!MatchesActor(rec.bioMother.raw(), motherID)) continue;
                if (!MatchesActor(rec.bioFather.raw(), bioFatherID)) continue;
                if (!StringUtils::CaseInsensitiveEquals(rec.name, childName)) continue;
                if (std::floor(rec.birthTimestamp) != std::floor(bTime)) continue;
                MARAS_LOG_WARN("[Fertility] Infant {:08X} ('{}') already registered for mother {:08X} on this birth day. Skipping duplicate birth registration.",
                               cIdKey.raw(), rec.name, motherID);
                return cIdKey.raw();
            }
        }

        RE::FormID childID = syntheticChildID;
        if (childID == 0) {
            childID = AllocateSyntheticChildID();
        }

        float cTime = conceptionTime;
        if (cTime <= 0.0f) {
            cTime = bTime;
        }

        if (motherID != 0) EnsureRegistered(motherID);
        if (presumedFatherID != 0) EnsureRegistered(presumedFatherID);
        if (bioFatherID != 0 && bioFatherID != kUnknownFatherFormID) EnsureRegistered(bioFatherID);

        RE::FormID gA = motherID;
        RE::FormID gB = (presumedFatherID != 0 && IsMarried(motherID, presumedFatherID)) ? presumedFatherID : 0;
        RE::FormID bioM = motherID;
        RE::FormID bioF = (bioFatherID != 0) ? bioFatherID : presumedFatherID;

        PaternitySecret pSec;
        RE::FormID recordedPresumedFatherID = 0;
        if (presumedFatherID != 0 && bioFatherID != 0 && !MatchesActor(bioFatherID, presumedFatherID)) {
            recordedPresumedFatherID = presumedFatherID;
            pSec.isSecret = true;
            pSec.isSecretForPartner = true;
            pSec.isSecretForBioFather = true;
            pSec.discovered = false;
            pSec.discoveredByPartner = false;
            pSec.discoveredByBioFather = false;
            pSec.lastCheckTimestamp = bTime;
            if (bioFatherID != kUnknownFatherFormID) {
                pSec.bioFatherName = ActorSvc::GetName(bioFatherID);
                if (auto* r = ActorSvc::GetRace(bioFatherID)) {
                    pSec.bioFatherRaceID = r->GetFormID();
                }
            }
            if (HasPhenotypeMismatch(presumedFatherID, bioFatherID)) {
                pSec.hasPhenotypeMismatch = true;
            }
        }

        ChildRegistryEntry entry(childID, gA, gB, childName, static_cast<int32_t>(bTime), false, bioM, bioF, true, cTime, bTime, pSec, ChildStage::Infant, "", 2, recordedPresumedFatherID);
        UpsertChildEntry(entry);
        ApplyBirthCustodyRules(childID, entry, motherID, presumedFatherID, bioF);
        EvaluateChildBeastbloodInheritance(entry);
        AdultChildProgression::GetSingleton().EvaluateChildBioLineageAtBirth(*this, entry);
        gA = entry.guardianAID;
        gB = entry.guardianBID;
        UpsertChildEntry(entry);

        MARAS_LOG_INFO("[Fertility] Registered fertility child {:08X} ('{}') to mother {:08X}, presumed father {:08X}, bio father {:08X} (isSecret: {}, secretPartner: {}, secretBio: {}, mismatch: {})",
                       childID, childName, motherID, presumedFatherID, bioFatherID, pSec.isSecret, pSec.isSecretForPartner, pSec.isSecretForBioFather, pSec.hasPhenotypeMismatch);

        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);

        UpdateChildBothParentsFaction(childID);
        UpdateSpouseChildrenFaction(gA);
        if (gB != 0) UpdateSpouseChildrenFaction(gB);

        // Birth evaluation if distinct phenotype mismatch (EvaluatePaternitySecret handles both SkyrimNet and non-SkyrimNet fallback)
        if (pSec.isSecret && pSec.hasPhenotypeMismatch) {
            float roll = Utils::RandomFloat(0.0f, 100.0f);
            float birthChance = static_cast<float>(Settings::GetSingleton().Snapshot()->paternityPhenotypeBirthChance);
            if (roll < birthChance) {
                MARAS_LOG_INFO("[Fertility] Phenotype mismatch detected between presumed father {:08X} and bio father {:08X}. Triggering birth evaluation for child {:08X} (roll: {:.1f}%, chance: {:.1f}%).",
                               presumedFatherID, bioFatherID, childID, roll, birthChance);
                EvaluatePaternitySecret(childID);
            } else {
                MARAS_LOG_INFO("[Fertility] Phenotype mismatch detected between presumed father {:08X} and bio father {:08X}, but birth evaluation roll failed for child {:08X} (roll: {:.1f}%, chance: {:.1f}%).",
                               presumedFatherID, bioFatherID, childID, roll, birthChance);
            }
        }

        return childID;
    }

    bool NPCRelationshipManager::ResolvePaternitySecret(RE::FormID childID, bool discoveredByPartner, bool sendEvent) {
        const RE::FormID motherID = GetBioMother(childID);
        const RE::FormID bioFatherID = GetBioFather(childID);

        bool found = false;
        bool wasSecretPartner = false;
        bool wasSecretBio = false;
        bool wasPregSecretPartner = false;
        std::vector<ChildRegistryEntry> updatedEntries;
        std::vector<RE::FormID> resolvedSiblingIDs;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec) {
                return;
            }
            found = true;
            wasSecretPartner = rec->paternitySecret.isSecretForPartner;
            wasSecretBio = rec->paternitySecret.isSecretForBioFather;
            wasPregSecretPartner = (rec->stage == State::ChildStage::Unborn) &&
                                   wasSecretPartner &&
                                   rec->paternitySecret.isPregnancySecretForPartner;

            rec->paternitySecret.discovered = true;
            rec->paternitySecret.discoveredByPartner = discoveredByPartner;
            rec->paternitySecret.isSecret = false;
            rec->paternitySecret.isSecretForPartner = false;
            rec->paternitySecret.isSecretForBioFather = false;
            rec->paternitySecret.isPregnancySecretForPartner = false;
            const bool isUnborn = (rec->stage == State::ChildStage::Unborn);
            updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*rec));

            // If this is an unborn child, also resolve for all unborn siblings from the same biological father
            if (isUnborn) {
                std::vector<State::ChildId> siblingIds;
                for (const auto& [sibKey, sibRec] : ws.lineage.Read().AllChildren()) {
                    if (sibKey.raw() != childID && sibRec.stage == State::ChildStage::Unborn &&
                        MatchesActor(sibRec.bioMother.raw(), motherID)) {
                        siblingIds.push_back(sibKey);
                    }
                }
                for (const auto sibKey : siblingIds) {
                    if (auto* sibRec = ws.lineage.Write().FindChildMutable(sibKey)) {
                        sibRec->paternitySecret.isPregnancySecretForPartner = false;
                        if (MatchesActor(sibRec->bioFather.raw(), bioFatherID)) {
                            sibRec->paternitySecret.discovered = true;
                            sibRec->paternitySecret.discoveredByPartner = discoveredByPartner;
                            sibRec->paternitySecret.isSecret = false;
                            sibRec->paternitySecret.isSecretForPartner = false;
                            sibRec->paternitySecret.isSecretForBioFather = false;
                            resolvedSiblingIDs.push_back(sibKey.raw());
                        }
                        updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*sibRec));
                    }
                }
            }
        });

        if (!found) {
            return false;
        }

        MARAS_LOG_INFO("[Fertility] Fully resolved paternity secret for child {:08X} (discoveredByPartner: {})", childID, discoveredByPartner);
        for (const RE::FormID sibID : resolvedSiblingIDs) {
            MARAS_LOG_INFO("[Fertility] Also fully resolved paternity secret on unborn sibling {:08X}", sibID);
        }

        if (PrismaUIManager::GetSingleton().IsDashboardOpen()) {
            for (const auto& updated : updatedEntries) {
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(updated);
            }
        }

        if (sendEvent && (wasSecretPartner || wasSecretBio)) {
            std::string baseOutcome;
            if (wasSecretPartner && wasSecretBio) {
                baseOutcome = "mother_confessed_both";
            } else if (wasSecretPartner) {
                baseOutcome = discoveredByPartner ? "presumed_father_learnt" : "mother_confessed_presumed";
            } else {
                baseOutcome = "mother_confessed_bio";
            }
            std::string eventOutcome = (wasSecretPartner && wasPregSecretPartner) ? (baseOutcome + "_unannounced") : baseOutcome;
            const float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", std::move(eventOutcome), childIdFloat, RE::FormID{ 0 });
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, baseOutcome, "none", wasPregSecretPartner);
        }
        return true;
    }

    bool NPCRelationshipManager::ResolvePaternitySecretForPartner(RE::FormID childID, bool discoveredByPartner, bool sendEvent) {
        const RE::FormID motherID = GetBioMother(childID);
        const RE::FormID bioFatherID = GetBioFather(childID);

        bool updated = false;
        bool wasPregSecretPartner = false;
        bool stillSecretForBio = false;
        std::vector<ChildRegistryEntry> updatedEntries;
        std::vector<RE::FormID> resolvedSiblingIDs;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec || !rec->paternitySecret.isSecretForPartner) {
                return;
            }
            updated = true;
            wasPregSecretPartner = (rec->stage == State::ChildStage::Unborn) &&
                                   rec->paternitySecret.isPregnancySecretForPartner;
            rec->paternitySecret.isSecretForPartner = false;
            rec->paternitySecret.isPregnancySecretForPartner = false;
            rec->paternitySecret.discoveredByPartner = discoveredByPartner;
            if (!rec->paternitySecret.isSecretForBioFather) {
                rec->paternitySecret.discovered = true;
                rec->paternitySecret.isSecret = false;
            }
            stillSecretForBio = rec->paternitySecret.isSecretForBioFather;
            const bool isUnborn = (rec->stage == State::ChildStage::Unborn);
            updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*rec));

            // If this is an unborn child, also resolve for all unborn siblings from the same biological father
            if (isUnborn) {
                std::vector<State::ChildId> siblingIds;
                for (const auto& [sibKey, sibRec] : ws.lineage.Read().AllChildren()) {
                    if (sibKey.raw() != childID && sibRec.stage == State::ChildStage::Unborn &&
                        MatchesActor(sibRec.bioMother.raw(), motherID)) {
                        siblingIds.push_back(sibKey);
                    }
                }
                for (const auto sibKey : siblingIds) {
                    if (auto* sibRec = ws.lineage.Write().FindChildMutable(sibKey)) {
                        sibRec->paternitySecret.isPregnancySecretForPartner = false;
                        if (MatchesActor(sibRec->bioFather.raw(), bioFatherID)) {
                            sibRec->paternitySecret.isSecretForPartner = false;
                            sibRec->paternitySecret.discoveredByPartner = discoveredByPartner;
                            if (!sibRec->paternitySecret.isSecretForBioFather) {
                                sibRec->paternitySecret.discovered = true;
                                sibRec->paternitySecret.isSecret = false;
                            }
                            resolvedSiblingIDs.push_back(sibKey.raw());
                        }
                        updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*sibRec));
                    }
                }
            }
        });

        if (!updated) {
            return false;
        }

        MARAS_LOG_INFO("[Fertility] Resolved paternity secret for partner on child {:08X} (discoveredByPartner: {}, stillSecretForBio: {})",
                       childID, discoveredByPartner, stillSecretForBio);
        for (const RE::FormID sibID : resolvedSiblingIDs) {
            MARAS_LOG_INFO("[Fertility] Also resolved paternity secret for partner on unborn sibling {:08X}", sibID);
        }

        if (PrismaUIManager::GetSingleton().IsDashboardOpen()) {
            for (const auto& entry : updatedEntries) {
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            }
        }

        if (sendEvent) {
            std::string baseOutcome = discoveredByPartner ? "presumed_father_learnt" : "mother_confessed_presumed";
            std::string eventOutcome = wasPregSecretPartner ? (baseOutcome + "_unannounced") : baseOutcome;
            const float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", std::move(eventOutcome), childIdFloat, RE::FormID{ 0 });
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, baseOutcome, "none", wasPregSecretPartner);
        }

        return true;
    }

    bool NPCRelationshipManager::RevealPregnancyToPartner(RE::FormID childID, bool byVisibleBelly, bool sendEvent) {
        const RE::FormID motherID = GetBioMother(childID);

        bool revealed = false;
        bool paternityStillSecretForPartner = false;
        std::vector<ChildRegistryEntry> updatedEntries;
        std::vector<RE::FormID> siblingIDsUpdated;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec || rec->stage != State::ChildStage::Unborn || !rec->paternitySecret.isPregnancySecretForPartner) {
                return;
            }
            revealed = true;
            rec->paternitySecret.isPregnancySecretForPartner = false;
            paternityStillSecretForPartner = rec->paternitySecret.isSecretForPartner;
            updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*rec));

            if (motherID != 0) {
                std::vector<State::ChildId> siblingIds;
                for (const auto& [sibKey, sibRec] : ws.lineage.Read().AllChildren()) {
                    if (sibKey.raw() != childID && sibRec.stage == State::ChildStage::Unborn &&
                        MatchesActor(sibRec.bioMother.raw(), motherID) &&
                        sibRec.paternitySecret.isPregnancySecretForPartner) {
                        siblingIds.push_back(sibKey);
                    }
                }
                for (const auto sibKey : siblingIds) {
                    if (auto* sibRec = ws.lineage.Write().FindChildMutable(sibKey)) {
                        sibRec->paternitySecret.isPregnancySecretForPartner = false;
                        siblingIDsUpdated.push_back(sibKey.raw());
                        updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*sibRec));
                    }
                }
            }
        });

        if (!revealed) {
            return false;
        }

        MARAS_LOG_INFO("[Fertility] Revealed pregnancy to partner on unborn child {:08X} (byVisibleBelly: {}, paternityStillSecretForPartner: {})",
                       childID, byVisibleBelly, paternityStillSecretForPartner);
        for (const RE::FormID sibID : siblingIDsUpdated) {
            MARAS_LOG_INFO("[Fertility] Also revealed pregnancy to partner on unborn sibling {:08X}", sibID);
        }
        for (const auto& entry : updatedEntries) {
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
        }

        if (sendEvent) {
            std::string outcome = byVisibleBelly ? "pregnancy_visible_presumed" : "mother_shared_pregnancy_presumed";
            const float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", outcome, childIdFloat, RE::FormID{ 0 });
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, outcome);
        }

        return true;
    }

    bool NPCRelationshipManager::ResolvePaternitySecretForBioFather(RE::FormID childID, bool discoveredByBioFather, bool sendEvent) {
        const RE::FormID motherID = GetBioMother(childID);
        const RE::FormID bioFatherID = GetBioFather(childID);

        bool updated = false;
        bool stillSecretForPartner = false;
        std::vector<ChildRegistryEntry> updatedEntries;
        std::vector<RE::FormID> resolvedSiblingIDs;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec || !rec->paternitySecret.isSecretForBioFather) {
                return;
            }
            updated = true;
            rec->paternitySecret.isSecretForBioFather = false;
            rec->paternitySecret.discoveredByBioFather = discoveredByBioFather;
            if (!rec->paternitySecret.isSecretForPartner) {
                rec->paternitySecret.discovered = true;
                rec->paternitySecret.isSecret = false;
            }
            stillSecretForPartner = rec->paternitySecret.isSecretForPartner;
            const bool isUnborn = (rec->stage == State::ChildStage::Unborn);
            updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*rec));

            // If this is an unborn child, also resolve for all unborn siblings from the same biological father
            if (isUnborn) {
                std::vector<State::ChildId> siblingIds;
                for (const auto& [sibKey, sibRec] : ws.lineage.Read().AllChildren()) {
                    if (sibKey.raw() != childID && sibRec.stage == State::ChildStage::Unborn &&
                        MatchesActor(sibRec.bioMother.raw(), motherID) &&
                        MatchesActor(sibRec.bioFather.raw(), bioFatherID)) {
                        siblingIds.push_back(sibKey);
                    }
                }
                for (const auto sibKey : siblingIds) {
                    if (auto* sibRec = ws.lineage.Write().FindChildMutable(sibKey)) {
                        sibRec->paternitySecret.isSecretForBioFather = false;
                        sibRec->paternitySecret.discoveredByBioFather = discoveredByBioFather;
                        if (!sibRec->paternitySecret.isSecretForPartner) {
                            sibRec->paternitySecret.discovered = true;
                            sibRec->paternitySecret.isSecret = false;
                        }
                        resolvedSiblingIDs.push_back(sibKey.raw());
                        updatedEntries.push_back(LegacyAdapters::ToLegacyChildEntry(*sibRec));
                    }
                }
            }
        });

        if (!updated) {
            return false;
        }

        MARAS_LOG_INFO("[Fertility] Resolved paternity secret for bio father on child {:08X} (discoveredByBio: {}, stillSecretForPartner: {})",
                       childID, discoveredByBioFather, stillSecretForPartner);
        for (const RE::FormID sibID : resolvedSiblingIDs) {
            MARAS_LOG_INFO("[Fertility] Also resolved paternity secret for bio father on unborn sibling {:08X}", sibID);
        }

        if (PrismaUIManager::GetSingleton().IsDashboardOpen()) {
            for (const auto& entry : updatedEntries) {
                PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            }
        }

        if (sendEvent) {
            std::string outcome = discoveredByBioFather ? "bio_father_learnt" : "mother_confessed_bio";
            const float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
            EmitModCallbackEvent("maras_paternity_secret_resolved", outcome, childIdFloat, RE::FormID{ 0 });
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, outcome);
        }

        return true;
    }

    bool NPCRelationshipManager::SetPaternitySecretEdges(RE::FormID childID, bool secretForBioFather, bool secretForPartner, std::optional<bool> pregnancySecretForPartner) {
        const RE::FormID motherPartner = GetMotherPartner(childID);

        bool found = false;
        bool bioLearned = false;
        bool partnerLearned = false;
        bool pregnancyLearnedOnly = false;
        bool wasPregnancySecretPartner = false;
        bool finalPregSecretPartner = false;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID });
            if (!rec) {
                return;
            }
            found = true;
            const bool wasSecretBio = rec->paternitySecret.isSecretForBioFather;
            const bool wasSecretPartner = rec->paternitySecret.isSecretForPartner;
            wasPregnancySecretPartner = (rec->stage == State::ChildStage::Unborn) && rec->paternitySecret.isPregnancySecretForPartner;

            rec->paternitySecret.isSecretForBioFather = secretForBioFather;
            rec->paternitySecret.isSecretForPartner = secretForPartner;
            if (!secretForPartner || rec->stage != State::ChildStage::Unborn) {
                rec->paternitySecret.isPregnancySecretForPartner = false;
            } else if (pregnancySecretForPartner.has_value()) {
                rec->paternitySecret.isPregnancySecretForPartner = *pregnancySecretForPartner;
            }
            const bool hasAnyPartnerContext = (!rec->presumedFather.IsNull() || motherPartner != 0);
            rec->paternitySecret.isSecret = secretForBioFather || secretForPartner;
            rec->paternitySecret.discovered = !rec->paternitySecret.isSecret;
            rec->paternitySecret.discoveredByPartner = !secretForPartner && hasAnyPartnerContext &&
                                                       (wasSecretPartner || rec->paternitySecret.discoveredByPartner);
            rec->paternitySecret.discoveredByBioFather = !secretForBioFather &&
                                                         (wasSecretBio || rec->paternitySecret.discoveredByBioFather);
            finalPregSecretPartner = rec->paternitySecret.isPregnancySecretForPartner;

            bioLearned = wasSecretBio && !secretForBioFather;
            partnerLearned = wasSecretPartner && !secretForPartner;
            pregnancyLearnedOnly = !partnerLearned && secretForPartner && wasPregnancySecretPartner && !finalPregSecretPartner;
        });

        if (!found) {
            return false;
        }

        MARAS_LOG_INFO("[Fertility] Set paternity secret edges on child {:08X} (secretBio: {}, secretPartner: {}, pregSecretPartner: {})",
                       childID, secretForBioFather, secretForPartner, finalPregSecretPartner);

        {
            const float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
            if (bioLearned && partnerLearned) {
                std::string eventOutcome = wasPregnancySecretPartner ? "mother_confessed_both_unannounced" : "mother_confessed_both";
                EmitModCallbackEvent("maras_paternity_secret_resolved", std::move(eventOutcome), childIdFloat, RE::FormID{ 0 });
            } else if (bioLearned) {
                EmitModCallbackEvent("maras_paternity_secret_resolved", "bio_father_learnt", childIdFloat, RE::FormID{ 0 });
            } else if (partnerLearned) {
                std::string eventOutcome = wasPregnancySecretPartner ? "presumed_father_learnt_unannounced" : "presumed_father_learnt";
                EmitModCallbackEvent("maras_paternity_secret_resolved", std::move(eventOutcome), childIdFloat, RE::FormID{ 0 });
            } else if (pregnancyLearnedOnly) {
                EmitModCallbackEvent("maras_paternity_secret_resolved", "mother_shared_pregnancy_presumed", childIdFloat, RE::FormID{ 0 });
            }
        }

        if (bioLearned && partnerLearned) {
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, "mother_confessed_both", "none", wasPregnancySecretPartner);
        } else if (bioLearned) {
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, "bio_father_learnt");
        } else if (partnerLearned) {
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, "presumed_father_learnt", "none", wasPregnancySecretPartner);
        } else if (pregnancyLearnedOnly) {
            SkyrimNetMemoryService::GetSingleton().RecordPaternityReveal(childID, "mother_shared_pregnancy_presumed");
        }

        return true;
    }

    bool NPCRelationshipManager::SetChildPaternityLastCheck(RE::FormID childID, float currentDay) {
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->paternitySecret.lastCheckTimestamp = currentDay;
                updated = true;
            }
        });
        return updated;
    }

    bool NPCRelationshipManager::EvaluatePaternitySecret(RE::FormID childID) {
        if (IsSkyrimNetAvailable() && !Settings::GetSingleton().Snapshot()->paternityForceDiceRolls) {
            return MARAS::EvaluatePaternitySecret(childID);
        }
        return EvaluatePaternitySecretFallback(childID);
    }

    bool NPCRelationshipManager::EvaluatePaternitySecretFallback(RE::FormID childID) {
        const auto entryOpt = GetChildEntry(childID);
        if (!entryOpt.has_value()) return false;

        const auto& entry = *entryOpt;
        RE::FormID motherID = GetBioMother(childID);
        RE::FormID partnerID = GetMotherPartner(childID);
        RE::FormID bioFatherID = GetBioFather(childID);

        if (motherID == 0 || partnerID == 0 || bioFatherID == 0) return false;

        auto calendar = RE::Calendar::GetSingleton();
        float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        const auto sSnap = Settings::GetSingleton().Snapshot();
        const auto& settings = *sSnap;

        // 1. Determine reveal: phenotype mismatch reveals immediately; otherwise roll against paternityNonSkyrimNetRevealChance
        bool shouldReveal = false;
        if (entry.paternitySecret.hasPhenotypeMismatch) {
            shouldReveal = true;
            MARAS_LOG_INFO("[PaternityFallback] Child {:08X} has phenotype mismatch -> reveals immediately.", childID);
        } else {
            float revealRoll = Utils::RandomFloat(0.0f, 100.0f);
            float revealChance = static_cast<float>(settings.paternityNonSkyrimNetRevealChance);
            shouldReveal = (revealRoll < revealChance);
            MARAS_LOG_INFO("[PaternityFallback] Child {:08X} reveal roll: {:.1f}%, chance: {:.1f}% -> {}",
                           childID, revealRoll, revealChance, shouldReveal ? "REVEALED" : "REMAINS SECRET");
        }

        if (!shouldReveal) {
            SetChildPaternityLastCheck(childID, currentDay);
            return true;
        }

        // 2. Both fathers learn when secret is revealed (sends "presumed_father_learnt" and "bio_father_learnt")
        ResolvePaternitySecretForPartner(childID, true);
        ResolvePaternitySecretForBioFather(childID, true);

        // 3. Partner reaction roll
        float breakupRoll = Utils::RandomFloat(0.0f, 100.0f);
        float breakupChance = static_cast<float>(settings.paternityBreakupChance);
        bool doesBreakup = (breakupRoll < breakupChance);

        std::string partnerReaction = "none";

        if (doesBreakup) {
            auto currentStatus = GetRelationshipStatus(motherID, partnerID);
            if (currentStatus == RelationshipStatus::Married) {
                partnerReaction = "divorce";
            } else {
                partnerReaction = "breakup";
            }
            Breakup(motherID, partnerID);
        }

        MARAS_LOG_INFO("[PaternityFallback] Child {:08X} secret resolved: reaction='{}'",
                       childID, partnerReaction);

        // 4. Refresh Prisma UI
        AddCurrentEpochTask("ResolvePaternitySecretFallback::RefreshUI", [childID]() {
            std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
        });

        return true;
    }

    bool NPCRelationshipManager::IsSecretForBioFather(RE::FormID childID) const {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childID };
        const auto* rec = worldSnap->lineage->FindChild(childKey);
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

    bool NPCRelationshipManager::IsSecretForPartner(RE::FormID childID) const {
        const RE::FormID partnerID = GetMotherPartner(childID);
        if (partnerID == 0) {
            return false;
        }
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childID };
        const auto* rec = worldSnap->lineage->FindChild(childKey);
        if (!rec || !rec->paternitySecret.isSecretForPartner) {
            return false;
        }
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto partnerView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider());
        return !partnerView.CanViewerKnowTrueBioFather(childKey);
    }

    bool NPCRelationshipManager::IsPregnancySecretForPartner(RE::FormID childID) const {
        const RE::FormID partnerID = GetMotherPartner(childID);
        if (partnerID == 0) {
            return false;
        }
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const State::ChildId childKey{ childID };
        const auto* rec = worldSnap->lineage->FindChild(childKey);
        if (!rec || rec->stage != State::ChildStage::Unborn || !rec->paternitySecret.isSecretForPartner) {
            return false;
        }
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto partnerView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider());
        return !partnerView.CanViewerKnowPregnancy(childKey);
    }

    bool NPCRelationshipManager::SetChildStage(RE::FormID childID, ChildStage stage) {
        std::optional<ChildRegistryEntry> updated;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->stage = static_cast<State::ChildStage>(stage);
                updated = LegacyAdapters::ToLegacyChildEntry(*rec);
            }
        });
        if (updated.has_value()) {
            MARAS_LOG_INFO("[Fertility] Set child {:08X} stage to {}", childID, static_cast<int>(stage));
            UpdateNonPlayerChildPackageOverride(childID);
            if (!IsSyntheticChildID(childID)) {
                FactionReconciler::GetSingleton().ReconcileActor(childID);
                PackageReconciler::GetSingleton().ReconcileActor(childID);
            }
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(*updated);
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            return true;
        }
        return false;
    }

    ChildStage NPCRelationshipManager::GetChildStage(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? static_cast<ChildStage>(rec->stage) : ChildStage::Unborn;
    }

    bool NPCRelationshipManager::SetChildTrainingLocation(RE::FormID childID, const std::string& location) {
        auto* calendar = RE::Calendar::GetSingleton();
        const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;
        const int32_t inferredClass = AdultChildProgression::FindTrainingClassByLocation(location);

        std::optional<ChildRegistryEntry> updated;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->trainingLocation = location;
                rec->stage = State::ChildStage::Training;
                if (rec->trainingStartGameDay <= 0.0f) {
                    rec->trainingStartGameDay = currentDay;
                }
                if (rec->trainingClassIndex < 0) {
                    rec->trainingClassIndex = inferredClass;
                }
                updated = LegacyAdapters::ToLegacyChildEntry(*rec);
            }
        });
        if (updated.has_value()) {
            MARAS_LOG_INFO("[Fertility] Set child {:08X} training location to '{}'", childID, location);
            UpdateNonPlayerChildPackageOverride(childID);
            if (!IsSyntheticChildID(childID)) {
                FactionReconciler::GetSingleton().ReconcileActor(childID);
                PackageReconciler::GetSingleton().ReconcileActor(childID);
            }
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(*updated);
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            return true;
        }
        return false;
    }

    std::string NPCRelationshipManager::GetChildTrainingLocation(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->trainingLocation : "";
    }

    std::string_view NPCRelationshipManager::GetTrainingClassName(int32_t classIndex) {
        return AdultChildProgression::GetTrainingClassName(classIndex);
    }

    std::string_view NPCRelationshipManager::GetTrainingLocationName(int32_t classIndex) {
        return AdultChildProgression::GetTrainingLocationName(classIndex);
    }

    bool NPCRelationshipManager::IsPlayerEligibleForTrainingClass(int32_t classIndex, std::string* outReason) {
        return AdultChildProgression::GetSingleton().IsPlayerEligibleForTrainingClass(classIndex, outReason);
    }

    uint8_t NPCRelationshipManager::ResolveActorFMRRaceIndex(RE::FormID actorID) {
        if (actorID == 0 || IsSyntheticChildID(actorID)) {
            return 255;
        }

        std::string combined;
        if (auto* race = ActorSvc::GetRace(actorID)) {
            if (const char* edid = race->GetFormEditorID(); edid && edid[0] != '\0') {
                combined += edid;
                combined += ' ';
            }
            if (const char* rName = race->GetName(); rName && rName[0] != '\0') {
                combined += rName;
            }
        } else {
            combined = ActorSvc::GetRaceName(actorID);
        }

        if (combined.empty()) {
            return 255;
        }

        const std::string lower = StringUtils::ToLowerStd(combined);
        // Must match Fertility Mode Reloaded (_JSW_BB_Storage.BirthMotherRace / GetRaceNameForStorage) 0..9 order:
        // 0=Argonian, 1=Breton, 2=Dark Elf, 3=High Elf, 4=Imperial, 5=Khajiit, 6=Nord, 7=Orc, 8=Redguard, 9=Wood Elf
        if (lower.find("argonian") != std::string::npos) return 0;
        if (lower.find("breton") != std::string::npos) return 1;
        if (lower.find("darkelf") != std::string::npos || lower.find("dark elf") != std::string::npos || lower.find("dunmer") != std::string::npos) return 2;
        if (lower.find("highelf") != std::string::npos || lower.find("high elf") != std::string::npos || lower.find("altmer") != std::string::npos) return 3;
        if (lower.find("imperial") != std::string::npos) return 4;
        if (lower.find("khajiit") != std::string::npos) return 5;
        if (lower.find("nord") != std::string::npos) return 6;
        if (lower.find("orc") != std::string::npos || lower.find("orsimer") != std::string::npos) return 7;
        if (lower.find("redguard") != std::string::npos) return 8;
        if (lower.find("woodelf") != std::string::npos || lower.find("wood elf") != std::string::npos || lower.find("bosmer") != std::string::npos) return 9;
        return 255;
    }

    bool NPCRelationshipManager::SendChildToTraining(RE::FormID childID, int32_t trainingClassIndex, std::string* outReason) {
        if (!AdultChildProgression::IsValidTrainingClass(trainingClassIndex)) {
            if (outReason) *outReason = "Invalid training class index";
            MARAS_LOG_WARN("[Training] SendChildToTraining failed for child {:08X}: invalid trainingClassIndex={}",
                           childID, trainingClassIndex);
            return false;
        }

        const auto entryOpt = GetChildEntry(childID);
        if (!entryOpt.has_value()) {
            if (outReason) *outReason = "Child record not found";
            MARAS_LOG_WARN("[Training] SendChildToTraining failed: child {:08X} not found in children registry", childID);
            return false;
        }
        if (entryOpt->stage != ChildStage::Child) {
            if (outReason) *outReason = "Only Stage 2 (Young Child) children can be sent to training";
            MARAS_LOG_WARN("[Training] SendChildToTraining failed for child {:08X} ('{}'): invalid stage={} (expected Stage 2 Child)",
                           childID, entryOpt->name, static_cast<int>(entryOpt->stage));
            return false;
        }

        ChildRegistryEntry entry = *entryOpt;
        EvaluateChildBeastbloodInheritance(entry);
        const bool isPlayerChild = entry.HasGuardian(kPlayerFormID);
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();
        const auto dashboardView = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, &GetEngineWerewolfStateProvider());
        const auto perceivedOpt = dashboardView.GetPerceivedChild(State::ChildId{ childID });
        bool allowBeastbloodTraining = entry.hasBeastblood;
        if (isPlayerChild) {
            const bool unpersistedFirstEvalVisible =
                !entryOpt->beastbloodEvaluated && entry.hasBeastblood &&
                (dashboardView.IsActorKnownWerewolf(State::ActorId{ entry.bioMotherID }) ||
                 (perceivedOpt.has_value() && dashboardView.IsActorKnownWerewolf(perceivedOpt->perceivedBioFather)));
            allowBeastbloodTraining =
                (perceivedOpt.has_value() && perceivedOpt->hasBeastblood) || unpersistedFirstEvalVisible;
        }
        if (AdultChildProgression::DoesTrainingRequireBeastblood(trainingClassIndex) && !allowBeastbloodTraining) {
            if (outReason) {
                *outReason = "Requires child to carry Hircine's Beastblood (inherited Lycanthropy)";
            }
            MARAS_LOG_WARN("[Training] SendChildToTraining blocked for child {:08X} ('{}'): classIdx={} ('{}') requires Beastblood",
                           childID, entry.name, trainingClassIndex, GetTrainingClassName(trainingClassIndex));
            return false;
        }

        int32_t paidTuitionGold = 0;
        int32_t appliedDiscountPercent = 0;
        std::string appliedDiscountReason;

        if (isPlayerChild) {
            std::string eligReason;
            if (!IsPlayerEligibleForTrainingClass(trainingClassIndex, &eligReason)) {
                if (outReason) *outReason = eligReason;
                MARAS_LOG_WARN("[Training] SendChildToTraining blocked for child {:08X} ('{}') at classIdx={} ('{}'): {}",
                               childID, entry.name, trainingClassIndex, GetTrainingClassName(trainingClassIndex), eligReason);
                return false;
            }

            const int32_t baseCost = std::max(0, Settings::GetSingleton().Snapshot()->childTrainingCost);
            const auto discountInfo = AdultChildProgression::GetSingleton().EvaluateChildTrainingDiscount(
                State::ChildId{ childID }, trainingClassIndex, dashboardView);
            const int32_t cost = AdultChildProgression::GetDiscountedTrainingCost(trainingClassIndex, baseCost, discountInfo.discountPercent);
            appliedDiscountPercent = discountInfo.discountPercent;
            appliedDiscountReason = discountInfo.reason;
            if (cost > 0) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* goldForm = FormCache::GetSingleton().GetGoldBaseObject();
                const int32_t playerGold = player ? player->GetGoldAmount() : 0;
                if (!player || !goldForm || playerGold < cost) {
                    const std::string goldMsg = std::format("Not enough gold ({} required, {} available)", cost, playerGold);
                    if (outReason) {
                        *outReason = goldMsg;
                    }
                    MARAS_LOG_WARN("[Training] SendChildToTraining blocked for child {:08X} ('{}'): {}",
                                   childID, entry.name, goldMsg);
                    return false;
                }
                player->RemoveItem(goldForm, cost, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                paidTuitionGold = cost;
            }
        }

        if (entry.gender > 1) {
            entry.gender = GetChildGender(childID);
        }
        if (entry.savedRaceIndex > 9) {
            uint8_t resolvedRace = ResolveActorFMRRaceIndex(childID);
            if (resolvedRace > 9 && entry.bioMotherID != 0) resolvedRace = ResolveActorFMRRaceIndex(entry.bioMotherID);
            if (resolvedRace > 9 && entry.bioFatherID != 0 && entry.bioFatherID != kUnknownFatherFormID) resolvedRace = ResolveActorFMRRaceIndex(entry.bioFatherID);
            if (resolvedRace <= 9) {
                entry.savedRaceIndex = resolvedRace;
            }
        }

        auto* calendar = RE::Calendar::GetSingleton();
        const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        entry.trainingClassIndex = trainingClassIndex;
        entry.trainingLocation = std::string(GetTrainingLocationName(trainingClassIndex));
        entry.trainingStartGameDay = currentDay;
        entry.stage = ChildStage::Training;

        RE::FormID targetChildID = childID;
        const bool wasPhysicalChild = !IsSyntheticChildID(childID);

        if (wasPhysicalChild) {
            if (IsSkyrimNetAvailable()) {
                std::string bioTpl = SkyrimNetProfileService::GetSingleton().GetActorBioTemplateName(childID);
                if (!bioTpl.empty()) {
                    entry.savedBioTemplateName = bioTpl;
                    MARAS_LOG_INFO("[Training] Captured childhood SkyrimNet bioTemplateName='{}' for child {:08X} ('{}')",
                                   bioTpl, childID, entry.name);
                }
            }

            auto& childSvc = PlayerChildrenService::GetSingleton();
            if (RE::FormID critterID = childSvc.GetCritter(childID); critterID != 0) {
                if (auto* critterActor = RE::TESForm::LookupByID<RE::Actor>(critterID)) {
                    critterActor->Disable();
                    critterActor->SetDelete(true);
                }
                childSvc.RemoveCritter(childID);
            }
            if (childSvc.GetPet(childID) != 0) {
                childSvc.RemovePet(childID);
            }
            childSvc.OnChildRemovedFromHousehold(childID);
            (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(childID);
            RemoveChildSandboxAroundParentLinkedRef(childID);
            PackageOverrideService::GetSingleton().UnregisterActor(childID);

            if (auto* childActor = RE::TESForm::LookupByID<RE::Actor>(childID)) {
                childActor->Disable();
                childActor->SetDelete(true);
            }

            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                RE::FormID allocated = ws.lineage.Write().AllocateSyntheticChildId().raw();
                if (allocated == 0) {
                    allocated = kSyntheticChildIDMax;
                }
                targetChildID = allocated;
                entry.childID = targetChildID;
                ws.lineage.Write().EraseChild(State::ChildId{ childID });
                ws.lineage.Write().UpsertChild(LegacyAdapters::ToChildRecord(entry));
            });

            if (entry.guardianAID != 0) UpdateSpouseChildrenFaction(entry.guardianAID);
            if (entry.guardianBID != 0) UpdateSpouseChildrenFaction(entry.guardianBID);

            PrismaUIDataBridge::GetSingleton().OnChildRemoved(childID);
            PrismaUIDataBridge::GetSingleton().OnChildPromoted(childID, targetChildID);
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(targetChildID);
        } else {
            UpsertChildEntry(entry);
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
        }

        const std::string className = std::string(GetTrainingClassName(trainingClassIndex));
        MARAS_LOG_INFO(
            "[Training] Sent child {:08X} -> {:08X} ('{}') to '{}' for '{}' training (classIdx={}, startDay={:.2f}, "
            "tuitionPaid={}g, discount={}% [{}], hasBeastblood={}, savedBio='{}', savedRace={})",
            childID, targetChildID, entry.name, entry.trainingLocation, className, trainingClassIndex,
            entry.trainingStartGameDay, paidTuitionGold, appliedDiscountPercent,
            appliedDiscountReason.empty() ? "None" : appliedDiscountReason, entry.hasBeastblood,
            entry.savedBioTemplateName, entry.savedRaceIndex);

        QueueHUDMessage(std::format("{} has been sent to {} to train as a {}.",
                                    entry.name.empty() ? "Child" : entry.name,
                                    entry.trainingLocation,
                                    className));

        SendChildEvent("sent_to_training", targetChildID, entry.guardianAID, entry.guardianBID, 0, entry.trainingLocation);
        return true;
    }

    bool NPCRelationshipManager::CompleteChildTraining(RE::FormID childID) {
        const auto entryOpt = GetChildEntry(childID);
        if (!entryOpt.has_value()) {
            MARAS_LOG_WARN("[Training] CompleteChildTraining failed: child {:08X} not found in children registry", childID);
            return false;
        }
        if (entryOpt->stage != ChildStage::Training) {
            MARAS_LOG_WARN("[Training] CompleteChildTraining failed for child {:08X} ('{}'): current stage={} (expected Stage 3 Training)",
                           childID, entryOpt->name, static_cast<int>(entryOpt->stage));
            return false;
        }

        ChildRegistryEntry entry = *entryOpt;
        entry.stage = ChildStage::Adult;
        EvaluateChildBeastbloodInheritance(entry);
        UpsertChildEntry(entry);

        const std::string className = std::string(GetTrainingClassName(entry.trainingClassIndex));
        MARAS_LOG_INFO("[Training] Child {:08X} ('{}') completed {} training at {} and graduated to Stage 4 (Adult)",
                       childID, entry.name, className, entry.trainingLocation);

        QueueHUDMessage(std::format("{} has completed {} training at {} and is ready to return as an adult!",
                                    entry.name.empty() ? "Your child" : entry.name,
                                    className.empty() ? "adolescent" : className,
                                    entry.trainingLocation.empty() ? "their academy" : entry.trainingLocation));

        SendChildEvent("became_adult", childID, entry.guardianAID, entry.guardianBID, 0, entry.trainingLocation);

        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);
        PrismaUIDataBridge::GetSingleton().SendOverviewData();
        PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
        return true;
    }

    void NPCRelationshipManager::CheckChildrenTrainingProgress(float currentGameDay) {
        if (currentGameDay <= 0.0f) return;

        const float requiredDays = static_cast<float>(std::max(1, Settings::GetSingleton().Snapshot()->childTrainingDurationDays));
        std::vector<RE::FormID> graduatedIDs;

        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
                if (rec.stage != State::ChildStage::Training) continue;
                if (rec.trainingStartGameDay <= 0.0f) continue;
                const float elapsedDays = currentGameDay - rec.trainingStartGameDay;
                if (elapsedDays >= requiredDays) {
                    MARAS_LOG_INFO("[Training] Child {:08X} ('{}') reached training duration ({:.2f} / {:.1f} days at '{}') — graduating to Adult",
                                   cIdKey.raw(), rec.name, elapsedDays, requiredDays, rec.trainingLocation);
                    graduatedIDs.push_back(cIdKey.raw());
                }
            }
        }

        for (RE::FormID cID : graduatedIDs) {
            CompleteChildTraining(cID);
        }
    }

    int32_t NPCRelationshipManager::GetChildTrainingClass(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        return rec ? rec->trainingClassIndex : -1;
    }

    int32_t NPCRelationshipManager::GetChildSavedRaceIndex(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (rec && rec->savedRaceIndex <= 9) {
            return static_cast<int32_t>(rec->savedRaceIndex);
        }
        return -1;
    }

    bool NPCRelationshipManager::SetChildGender(RE::FormID childID, uint8_t gender) {
        std::optional<ChildRegistryEntry> updatedEntry;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->gender = gender;
                updatedEntry = LegacyAdapters::ToLegacyChildEntry(*rec);
            }
        });
        if (updatedEntry.has_value()) {
            MARAS_LOG_INFO("[Fertility] Set child {:08X} gender to {}", childID, gender == 1 ? "female" : "male");
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(*updatedEntry);
            return true;
        }
        return false;
    }

    uint8_t NPCRelationshipManager::GetChildGender(RE::FormID childID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->lineage->FindChild(State::ChildId{ childID });
        if (rec) {
            if (rec->stage == State::ChildStage::Unborn) {
                return 2;
            }
            if (HasPhysicalActor(childID)) {
                auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(childID);
                if (actor && actor->GetActorBase()) {
                    return (actor->GetActorBase()->GetSex() == RE::SEX::kFemale) ? 1 : 0;
                }
            }
            return rec->gender;
        }
        if (HasPhysicalActor(childID)) {
            auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(childID);
            if (actor && actor->GetActorBase()) {
                return (actor->GetActorBase()->GetSex() == RE::SEX::kFemale) ? 1 : 0;
            }
        }
        return 2;
    }

    bool NPCRelationshipManager::UpdateChildName(RE::FormID childID, const std::string& newName) {
        std::optional<ChildRegistryEntry> updated;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ childID })) {
                rec->name = newName;
                updated = LegacyAdapters::ToLegacyChildEntry(*rec);
            }
        });
        if (updated.has_value()) {
            MARAS_LOG_INFO("[Fertility] Updated child {:08X} name to '{}'", childID, newName);
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(*updated);
            return true;
        }
        return false;
    }

    RE::FormID NPCRelationshipManager::FindSyntheticChild(RE::FormID motherID, const std::string& name) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& allChildren = snap->lineage->AllChildren();

        // First pass: match synthetic ID with exact mother and name
        for (const auto& [cIdKey, rec] : allChildren) {
            const RE::FormID cID = cIdKey.raw();
            if (!IsSyntheticChildID(cID)) continue;
            if (motherID != 0 && !rec.guardians.HasGuardian(State::ActorId{ motherID }) &&
                !MatchesActor(rec.bioMother.raw(), motherID) && !MatchesActor(GetBioMother(cID), motherID)) {
                continue;
            }
            if (!name.empty() && StringUtils::CaseInsensitiveEquals(rec.name, name)) {
                return cID;
            }
        }

        // Second pass: if motherID provided and name is empty, return first synthetic child belonging to this mother
        if (motherID != 0 && name.empty()) {
            for (const auto& [cIdKey, rec] : allChildren) {
                const RE::FormID cID = cIdKey.raw();
                if (!IsSyntheticChildID(cID)) continue;
                if (rec.guardians.HasGuardian(State::ActorId{ motherID }) ||
                    MatchesActor(rec.bioMother.raw(), motherID) || MatchesActor(GetBioMother(cID), motherID)) {
                    return cID;
                }
            }
        }

        // Third pass: if motherID is 0, match synthetic ID with name alone across all synthetic children
        if (motherID == 0 && !name.empty()) {
            for (const auto& [cIdKey, rec] : allChildren) {
                const RE::FormID cID = cIdKey.raw();
                if (!IsSyntheticChildID(cID)) continue;
                if (StringUtils::CaseInsensitiveEquals(rec.name, name)) {
                    return cID;
                }
            }
        }

        return 0;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildrenWithActivePaternitySecrets() const {
        std::vector<RE::FormID> result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
            if (rec.paternitySecret.HasActiveSecret() && !rec.paternitySecret.discovered) {
                result.push_back(cIdKey.raw());
            }
        }
        return result;
    }

    void NPCRelationshipManager::CheckPaternitySecretsDailyTick(float currentDay) {
        const auto sSnap = Settings::GetSingleton().Snapshot();
        const auto& settings = *sSnap;
        std::vector<RE::FormID> childrenToEvaluate;

        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [cIdKey, rec] : snap->lineage->AllChildren()) {
                if (rec.stage == State::ChildStage::Unborn || rec.birthTimestamp <= 0.0f) {
                    continue;
                }
                if (!rec.paternitySecret.HasActiveSecret() || rec.paternitySecret.discovered) {
                    continue;
                }

                const RE::FormID cID = cIdKey.raw();
                float childAgeDays = currentDay - rec.birthTimestamp;
                if (childAgeDays <= 0.0f) continue;

                float chance = 0.0f;
                if (rec.paternitySecret.hasPhenotypeMismatch) {
                    // Accelerated daily tick: visual differences become more glaring each day
                    float baseChance = static_cast<float>(settings.paternityPhenotypeBaseChance) / 100.0f;
                    float dailyGrowth = settings.paternityPhenotypeDailyGrowth / 100.0f;
                    float maxChance = static_cast<float>(settings.paternityPhenotypeMaxChance) / 100.0f;
                    chance = std::clamp(baseChance + (childAgeDays * dailyGrowth), 0.0f, maxChance);
                } else {
                    // Standard subtle tick: gradual physical trait resemblance
                    float baseChance = static_cast<float>(settings.paternityBaseChance) / 100.0f;
                    float dailyGrowth = settings.paternityDailyGrowth / 100.0f;
                    float maxChance = static_cast<float>(settings.paternityMaxChance) / 100.0f;
                    chance = std::clamp(baseChance + (childAgeDays * dailyGrowth), 0.0f, maxChance);
                }

                float roll = Utils::RandomFloat(0.0f, 1.0f);
                MARAS_LOG_DEBUG("[Paternity] Discovery roll for child {:08X} (age: {:.1f} days, chance: {:.1f}%)",
                               cID, childAgeDays, chance * 100.0f);
                if (roll < chance) {
                    MARAS_LOG_INFO("[Paternity] Discovery roll succeeded for child {:08X} (age: {:.1f} days, chance: {:.1f}%, roll: {:.1f}%)",
                                   cID, childAgeDays, chance * 100.0f, roll * 100.0f);
                    childrenToEvaluate.push_back(cID);
                }
            }
        }

        if (!childrenToEvaluate.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                for (RE::FormID evalID : childrenToEvaluate) {
                    if (auto* rec = ws.lineage.Write().FindChildMutable(State::ChildId{ evalID })) {
                        rec->paternitySecret.lastCheckTimestamp = currentDay;
                    }
                }
            });
        }

        for (RE::FormID evalID : childrenToEvaluate) {
            EvaluatePaternitySecret(evalID);
        }
    }

}  // namespace MARAS
