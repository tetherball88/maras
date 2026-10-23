#include "core/NPCRelationshipManager.h"

#include "core/NPCIndex.h"
#include "core/SkyrimNetIntegration.h"
#include "services/ActorService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

namespace MARAS {

    bool NPCRelationshipManager::ApplyBioFatherStepIn(RE::FormID motherID, RE::FormID bioFatherID) {
        if (motherID == 0 || bioFatherID == 0) return false;

        // Ensure relationship can be formed / already married
        bool alreadyMarried = (GetRelationshipStatus(motherID, bioFatherID) == RelationshipStatus::Married);
        if (!alreadyMarried) {
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

    void NPCRelationshipManager::EvaluateBioFatherStepIn(RE::FormID motherID, RE::FormID formerSpouseID) {
        if (motherID == 0 || (formerSpouseID != 0 && motherID == formerSpouseID)) return;

        struct StepInCandidate {
            RE::FormID bioFatherID{ 0 };
            bool isPregnancy{ false };
            std::string childName;
            float childAgeDays{ 0.0f };
        };

        std::vector<StepInCandidate> candidates;
        std::unordered_set<RE::FormID> seenFathers;

        // 1. Check unborn children for mother
        for (auto uID : GetUnbornChildren(motherID)) {
            auto it = childrenByID.find(uID);
            if (it == childrenByID.end()) continue;

            RE::FormID bioFatherID = it->second.bioFatherID;
            if (bioFatherID == 0 || bioFatherID == kUnknownFatherFormID || (formerSpouseID != 0 && MatchesActor(bioFatherID, formerSpouseID))) continue;
            if (motherID == kPlayerFormID || bioFatherID == kPlayerFormID) continue;
            if (seenFathers.find(bioFatherID) != seenFathers.end()) continue;
            if (GetPrimaryPartner(bioFatherID) != 0) continue;
            if (IsMostRecentRelationshipDivorced(motherID, bioFatherID)) {
                MARAS_LOG_INFO("[Fertility] Bio father {:08X} skipped from step-in candidates: most recent relationship with mother {:08X} is divorced.",
                               bioFatherID, motherID);
                continue;
            }
            if (!CanFormRelationship(motherID, bioFatherID, RelationshipStatus::Married)) continue;

            candidates.push_back({ bioFatherID, true, it->second.name, 0.0f });
            seenFathers.insert(bioFatherID);
        }

        // 2. Children left with mother in her custody (guardianAID == motherID || guardianBID == motherID)
        auto calendar = RE::Calendar::GetSingleton();
        float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        for (auto& [cID, entry] : childrenByID) {
            if (entry.stage == ChildStage::Adult) continue;

            bool withMother = MatchesActor(entry.guardianAID, motherID) || MatchesActor(entry.guardianBID, motherID);
            if (!withMother) continue;

            RE::FormID bioFatherID = GetBioFather(cID);
            if (bioFatherID == 0 && entry.bioFatherID != 0 && (formerSpouseID == 0 || !MatchesActor(entry.bioFatherID, formerSpouseID)) && !MatchesActor(entry.bioFatherID, motherID)) {
                bioFatherID = entry.bioFatherID;
            }

            if (bioFatherID == 0 || bioFatherID == kUnknownFatherFormID || (formerSpouseID != 0 && MatchesActor(bioFatherID, formerSpouseID))) continue;
            if (motherID == kPlayerFormID || bioFatherID == kPlayerFormID) continue;
            if (seenFathers.find(bioFatherID) != seenFathers.end()) continue;
            if (GetPrimaryPartner(bioFatherID) != 0) continue;
            if (IsMostRecentRelationshipDivorced(motherID, bioFatherID)) {
                MARAS_LOG_INFO("[Fertility] Bio father {:08X} skipped from step-in candidates: most recent relationship with mother {:08X} is divorced.",
                               bioFatherID, motherID);
                continue;
            }
            if (!CanFormRelationship(motherID, bioFatherID, RelationshipStatus::Married)) continue;

            float ageDays = (entry.stage == ChildStage::Unborn) ? 0.0f : std::max(0.0f, currentDay - entry.birthTimestamp);
            bool isPreg = (entry.stage == ChildStage::Unborn);
            candidates.push_back({ bioFatherID, isPreg, entry.name.empty() ? ActorSvc::GetName(cID) : entry.name, ageDays });
            seenFathers.insert(bioFatherID);
        }

        if (candidates.empty()) return;

        bool useLLM = IsSkyrimNetAvailable() && !Settings::GetSingleton().paternityForceDiceRolls;
        if (!useLLM) {
            // Dice roll evaluation
            float stepUpChance = static_cast<float>(Settings::GetSingleton().paternityBioStepUpChance);
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

        // SkyrimNet LLM evaluation queue
        auto processNext = std::make_shared<std::function<void(size_t)>>();
        *processNext = [this, motherID, formerSpouseID, candidates, processNext](size_t index) {
            if (index >= candidates.size()) {
                *processNext = nullptr;
                return;
            }
            const auto& cand = candidates[index];
            if (IsMostRecentRelationshipDivorced(motherID, cand.bioFatherID) ||
                GetPrimaryPartner(cand.bioFatherID) != 0 ||
                !CanFormRelationship(motherID, cand.bioFatherID, RelationshipStatus::Married)) {
                (*processNext)(index + 1);
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

            std::string promptName = "maras_evaluations/maras_step_in_evaluation";
            std::string contextJson = ctx.dump();

            SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson,
                [this, motherID, motherName, bioFatherName, cand, processNext, index](const char* response, int success) {
                    bool validJson = false;
                    bool steppedIn = false;
                    std::string failureDetail;
                    LLMFailureType failureType = LLMFailureType::RequestFailed;

                    if (success && response && response[0] != '\0') {
                        nlohmann::json resp = JsonService::ParseLLMJson(response);
                        if (!resp.is_discarded() && resp.is_object()) {
                            validJson = true;
                            steppedIn = resp.value("bio_father_steps_in", false);
                            std::string reason = resp.value("reason", "");
                            spdlog::info("[SkyrimNet] Step-in LLM evaluation for bio father {:08X} with mother {:08X}: steppedIn={}, reason='{}'",
                                         cand.bioFatherID, motherID, steppedIn, reason);
                        } else {
                            failureType = LLMFailureType::InvalidJson;
                            failureDetail = "AI model returned broken or unreadable JSON.";
                            spdlog::warn("[SkyrimNet] Step-in LLM evaluation returned malformed JSON for {:08X} & {:08X}: {}",
                                         cand.bioFatherID, motherID, response);
                        }
                    } else {
                        failureType = LLMFailureType::RequestFailed;
                        failureDetail = "HTTP / connection request failed or returned an empty response.";
                        spdlog::warn("[SkyrimNet] Step-in LLM call failed for {:08X} & {:08X}", cand.bioFatherID, motherID);
                    }

                    if (validJson) {
                        SKSE::GetTaskInterface()->AddTask([this, motherID, cand, steppedIn, processNext, index]() {
                            if (!*processNext) {
                                return;
                            }
                            if (steppedIn) {
                                ApplyBioFatherStepIn(motherID, cand.bioFatherID);
                                *processNext = nullptr;
                            } else {
                                (*processNext)(index + 1);
                            }
                        });
                        return;
                    }

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
                        std::format("{} & {} ({})\n{}", bioFatherName, motherName, childDesc, failureDetail),
                        ModalTone::Warning));
                    req.controls.push_back(ModalControl::MakeInfoBox(
                        "error_guidance",
                        GetLLMFailureGuidanceTitle(failureType),
                        GetLLMFailureGuidance(failureType, "gamemaster_evaluation"),
                        ModalTone::Default));

                    req.buttons.push_back(ModalButton::Make("retry", "Retry LLM Request", ModalButtonVariant::Primary));
                    req.buttons.push_back(ModalButton::Make("dice_roll", "Use Dice Roll Fallback", ModalButtonVariant::Secondary));
                    req.buttons.push_back(ModalButton::Make("skip", "Skip Candidate", ModalButtonVariant::Ghost));

                    PrismaModalManager::GetSingleton().ShowModal(
                        std::move(req),
                        [this, motherID, cand, processNext, index](const ModalResponse& modalResp) {
                            if (!*processNext) {
                                return;
                            }
                            if (!modalResp.aborted && (modalResp.buttonIndex == 0 || modalResp.buttonId == "retry")) {
                                spdlog::info("[SkyrimNet] Player chose to retry bio-father step-in LLM evaluation for {:08X} & {:08X}",
                                             cand.bioFatherID, motherID);
                                (*processNext)(index);
                                return;
                            }
                            if (!modalResp.aborted && (modalResp.buttonIndex == 1 || modalResp.buttonId == "dice_roll")) {
                                float roll = Utils::RandomFloat(0.0f, 100.0f);
                                float chance = static_cast<float>(Settings::GetSingleton().paternityBioStepUpChance);
                                bool fallbackStepIn = (roll < chance);
                                spdlog::info("[SkyrimNet] Player chose dice-roll fallback for bio-father step-in ({:08X} & {:08X}): {:.1f}% < {:.1f}% -> {}",
                                             cand.bioFatherID, motherID, roll, chance, fallbackStepIn);
                                if (fallbackStepIn) {
                                    ApplyBioFatherStepIn(motherID, cand.bioFatherID);
                                    *processNext = nullptr;
                                } else {
                                    (*processNext)(index + 1);
                                }
                                return;
                            }
                            spdlog::info("[SkyrimNet] Player skipped bio-father step-in evaluation for {:08X} & {:08X}",
                                         cand.bioFatherID, motherID);
                            (*processNext)(index + 1);
                        });
                });
        };

        (*processNext)(0);
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
        auto matchesParent = [this](const ChildRegistryEntry& entry, RE::FormID pID) -> bool {
            if (pID == 0) return true;
            return MatchesActor(entry.guardianAID, pID) || MatchesActor(entry.guardianBID, pID) ||
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

        // Pass 1: Match childName and both parents (if provided)
        if (hasName) {
            std::string nameLower = StringUtils::ToLowerStd(childName);
            for (const auto& [cID, entry] : childrenByID) {
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (!matchesParent(entry, parent1ID) || !matchesParent(entry, parent2ID)) continue;
                if (matchesChildName(entry, nameLower)) {
                    return cID;
                }
            }
        }

        // Pass 2: If both parents provided, return first matching child with active secret
        if (parent1ID != 0 && parent2ID != 0) {
            for (const auto& [cID, entry] : childrenByID) {
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (matchesParent(entry, parent1ID) && matchesParent(entry, parent2ID)) {
                    return cID;
                }
            }
        }

        // Pass 3: If parent1 provided, return first matching child with active secret
        if (parent1ID != 0) {
            for (const auto& [cID, entry] : childrenByID) {
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (matchesParent(entry, parent1ID)) {
                    return cID;
                }
            }
        }

        // Pass 4: Fallback by name alone across any child with active secret
        if (hasName) {
            std::string nameLower = StringUtils::ToLowerStd(childName);
            for (const auto& [cID, entry] : childrenByID) {
                if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
                if (matchesChildName(entry, nameLower)) {
                    return cID;
                }
            }
        }

        return 0;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildrenWithActiveSecretsForActor(RE::FormID actorID) const {
        std::vector<RE::FormID> result;
        if (actorID == 0) return result;
        for (const auto& [cID, entry] : childrenByID) {
            if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) continue;
            if (MatchesActor(entry.guardianAID, actorID) || MatchesActor(entry.bioMotherID, actorID) ||
                MatchesActor(entry.guardianBID, actorID) || MatchesActor(entry.bioFatherID, actorID) ||
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

        for (const auto& [cID, entry] : childrenByID) {
            if (entry.stage == ChildStage::Unborn) {
                if (MatchesActor(entry.bioMotherID, motherID)) {
                    result.push_back(cID);
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

        RE::FormID childID = nextSyntheticChildID_++;

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
        childrenByID[childID] = entry;

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
            for (RE::FormID candidate : unbornIDs) {
                auto cit = childrenByID.find(candidate);
                if (cit != childrenByID.end() && MatchesActor(cit->second.bioFatherID, bioFatherID)) {
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

        auto it = childrenByID.find(uID);
        if (it == childrenByID.end()) return 0;

        float bTime = (birthTime > 0.0f) ? birthTime : (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f);
        float birthChance = static_cast<float>(Settings::GetSingleton().paternityPhenotypeBirthChance);

        it->second.stage = ChildStage::Infant;
        it->second.birthTimestamp = bTime;
        it->second.timestamp = static_cast<int32_t>(bTime);
        it->second.name = name.empty() ? "Newborn" : name;
        RE::FormID prevGuardianB = it->second.guardianBID;
        RE::FormID presumedFatherID = (it->second.presumedFatherID != 0)
            ? it->second.presumedFatherID
            : ((prevGuardianB != 0) ? prevGuardianB : GetPrimaryPartner(motherID));
        if (it->second.presumedFatherID == 0 && presumedFatherID != 0 && it->second.bioFatherID != 0 && !MatchesActor(presumedFatherID, it->second.bioFatherID)) {
            it->second.presumedFatherID = presumedFatherID;
        }
        if (presumedFatherID == 0 && it->second.paternitySecret.isSecretForPartner) {
            it->second.paternitySecret.isSecretForPartner = false;
            if (!it->second.paternitySecret.isSecretForBioFather) {
                it->second.paternitySecret.isSecret = false;
                it->second.paternitySecret.discovered = true;
            }
        }
        ApplyBirthCustodyRules(uID, it->second, motherID, presumedFatherID, it->second.bioFatherID);
        UpdateChildBothParentsFaction(uID);
        UpdateSpouseChildrenFaction(motherID);
        if (prevGuardianB != 0) UpdateSpouseChildrenFaction(prevGuardianB);
        if (it->second.guardianAID != 0 && it->second.guardianAID != motherID && it->second.guardianAID != prevGuardianB) {
            UpdateSpouseChildrenFaction(it->second.guardianAID);
        }

        MARAS_LOG_INFO("[Fertility] Promoted Unborn child {:08X} to Infant ('{}') for mother {:08X}. Birth: {:.2f}, SecretPartner: {}, SecretBio: {}",
                       uID, it->second.name, motherID, bTime,
                       it->second.paternitySecret.isSecretForPartner,
                       it->second.paternitySecret.isSecretForBioFather);
        PrismaUIDataBridge::GetSingleton().OnChildUpdated(it->second);

        // Check phenotype mismatch at birth: if parents have stark phenotype difference, roll configured birth discovery chance
        bool evaluateAtBirth = false;
        if (it->second.paternitySecret.hasPhenotypeMismatch && it->second.paternitySecret.isSecretForPartner) {
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
            // Remove only one fetus fathered by bioFatherID (e.g. a single stillbirth in a multi-father pregnancy)
            for (RE::FormID uID : unbornIDs) {
                auto it = childrenByID.find(uID);
                if (it != childrenByID.end() && MatchesActor(it->second.bioFatherID, bioFatherID)) {
                    childrenByID.erase(it);
                    MARAS_LOG_INFO("[Fertility] Removed Unborn child {:08X} fathered by {:08X} for mother {:08X} (stillbirth)", uID, bioFatherID, motherID);
                    PrismaUIDataBridge::GetSingleton().OnChildRemoved(uID);
                    return true;
                }
            }
            MARAS_LOG_WARN("[Fertility] RemoveUnbornChild: no unborn child of mother {:08X} fathered by {:08X}", motherID, bioFatherID);
            return false;
        }

        for (RE::FormID uID : unbornIDs) {
            childrenByID.erase(uID);
            MARAS_LOG_INFO("[Fertility] Removed Unborn child {:08X} for mother {:08X} (pregnancy cleared/miscarried)", uID, motherID);
            PrismaUIDataBridge::GetSingleton().OnChildRemoved(uID);
        }
        return true;
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

            for (RE::FormID uID : unbornIDs) {
                auto childOpt = mgr.GetChildEntry(uID);
                if (!childOpt.has_value()) continue;

                RE::FormID bID = mgr.GetBioFather(uID);
                if (bID == 0) {
                    bID = childOpt->bioFatherID;
                }
                std::string fName;
                if (bID != 0 && bID != kUnknownFatherFormID) {
                    fName = ActorSvc::GetName(bID);
                }
                if (fName.empty()) {
                    fName = childOpt->paternitySecret.bioFatherName;
                }
                if (fName.empty()) {
                    fName = !bioNameFallback.empty() ? bioNameFallback : "an unknown father";
                }

                bool isPart = (bID != 0 && partnerID != 0 && bID == partnerID) || (!result.partnerName.empty() && fName == result.partnerName);
                bool isSec = childOpt->paternitySecret.isSecretForPartner;
                if (!isSec && partnerID != 0 && !isPart) {
                    isSec = true;
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
                    result.message += ", and she is keeping the true biological paternity a secret from " + result.partnerName + " (her current partner).";
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
                        result.message += " fathered by " + singleName + ", though " + result.partnerName + " is her current partner and she is keeping the true paternity a secret.";
                    } else {
                        result.message += " fathered by " + singleName + ", though " + result.partnerName + " is her partner.";
                    }
                } else if (!result.partnerName.empty()) {
                    if (isSec) {
                        result.message += ", though " + result.partnerName + " is her current partner and she is keeping the true paternity a secret.";
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
                return rel + ", " + c.name;
            }
            return (rel == "child" ? "a child, " : (rel == "son" ? "a son, " : "a daughter, ")) + c.name;
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
                    return "twin daughters, " + children[0].name + " and " + children[1].name;
                }
                if (bothBoys) {
                    return "twin sons, " + children[0].name + " and " + children[1].name;
                }
                return FormatChildItem(children[0], true) + ", and " + FormatChildItem(children[1], true);
            }
            if (children.size() == 3) {
                bool allGirls = (children[0].gender == 1 && children[1].gender == 1 && children[2].gender == 1);
                bool allBoys = (children[0].gender == 0 && children[1].gender == 0 && children[2].gender == 0);
                if (allGirls) {
                    return "triplet daughters, " + children[0].name + ", " + children[1].name + ", and " + children[2].name;
                }
                if (allBoys) {
                    return "triplet sons, " + children[0].name + ", " + children[1].name + ", and " + children[2].name;
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
            const std::vector<RE::FormID>& childIDs)
        {
            ChildBirthResult result;
            if (motherID == 0) return result;

            result.motherName = ActorSvc::GetName(motherID);
            if (result.motherName.empty()) result.motherName = "Mother";

            RE::FormID partnerID = mgr.GetPrimaryPartner(motherID);
            if (partnerID != 0) {
                result.partnerName = ActorSvc::GetName(partnerID);
            }

            std::vector<RE::FormID> targetIDs = childIDs;
            if (targetIDs.empty()) {
                for (const auto& [cID, entry] : mgr.GetAllChildren()) {
                    RE::FormID actualMotherID = mgr.GetBioMother(cID);
                    if (MatchesActor(actualMotherID, motherID) || MatchesActor(entry.guardianAID, motherID) ||
                        MatchesActor(entry.guardianBID, motherID)) {
                        if (entry.stage == ChildStage::Infant || entry.stage == ChildStage::Child) {
                            targetIDs.push_back(cID);
                        }
                    }
                }
            }
            if (targetIDs.empty()) return result;

            std::vector<BirthFatherGroup> groups;

            for (RE::FormID cID : targetIDs) {
                auto childOpt = mgr.GetChildEntry(cID);
                if (!childOpt.has_value()) continue;

                BirthChildItem item;
                item.childID = cID;
                item.name = childOpt->name.empty() ? "Newborn" : childOpt->name;
                item.gender = childOpt->gender;
                if (item.gender == 0) {
                    item.relation = "son";
                } else if (item.gender == 1) {
                    item.relation = "daughter";
                } else {
                    item.relation = "child";
                }

                RE::FormID bID = mgr.GetBioFather(cID);
                if (bID == 0) {
                    bID = childOpt->bioFatherID;
                }
                std::string fName;
                if (bID != 0 && bID != kUnknownFatherFormID) {
                    fName = ActorSvc::GetName(bID);
                }
                if (fName.empty()) {
                    fName = childOpt->paternitySecret.bioFatherName;
                }
                if (fName.empty()) {
                    fName = "an unknown father";
                }

                bool isPart = (bID != 0 && partnerID != 0 && bID == partnerID) || (!result.partnerName.empty() && fName == result.partnerName);
                bool isSecPartner = childOpt->paternitySecret.isSecretForPartner;
                if (!isSecPartner && partnerID != 0 && !isPart) {
                    isSecPartner = true;
                }
                item.isSecretForPartner = isSecPartner;
                item.isSecretForBio = childOpt->paternitySecret.isSecretForBioFather;

                ChildBirthResult::ChildJsonInfo cJson;
                cJson.name = item.name;
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
                        result.message = result.motherName + " gave birth to a healthy " + c.relation + ", " + c.name + ", fathered by " + fg.name + ".";
                    } else if (!result.partnerName.empty()) {
                        result.message = result.motherName + " gave birth to " + c.name + ". ";
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
                            result.message = result.motherName + " gave birth to " + c.name + ", a " + c.relation + " fathered by " + fg.name + ", though " + fg.name + " is unaware of the child.";
                        } else {
                            result.message = result.motherName + " gave birth to a healthy " + c.relation + ", " + c.name + ", fathered by " + fg.name + ".";
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
        j["type"] = "ttm_pregnancy_announced";
        j["msg"] = res.message;
        j["skipTrigger"] = false;
        if (!res.motherName.empty()) j["mother"] = res.motherName;
        if (!res.partnerName.empty()) j["partner"] = res.partnerName;
        if (!res.bioFatherJson.empty()) j["bioFather"] = res.bioFatherJson;

        MARAS_LOG_INFO("[PregnancyAnnounced] {}", res.message);
        return j.dump();
    }

    std::string NPCRelationshipManager::BuildChildBirthMessage(RE::FormID motherID, const std::vector<RE::FormID>& childIDs) const {
        auto res = BuildChildBirthInternal(*this, motherID, childIDs);
        return res.message;
    }

    std::string NPCRelationshipManager::BuildChildBirthJson(RE::FormID motherID, const std::vector<RE::FormID>& childIDs) const {
        auto res = BuildChildBirthInternal(*this, motherID, childIDs);
        if (res.message.empty()) return "";

        nlohmann::json j;
        j["type"] = "ttm_child_birth";
        j["msg"] = res.message;
        j["skipTrigger"] = false;
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

        MARAS_LOG_INFO("[ChildBirth] {}", res.message);
        return j.dump();
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
            for (const auto& [cID, entry] : childrenByID) {
                if (entry.stage != ChildStage::Infant) continue;
                if (!MatchesActor(entry.bioMotherID, motherID)) continue;
                if (!MatchesActor(entry.bioFatherID, bioFatherID)) continue;
                if (!StringUtils::CaseInsensitiveEquals(entry.name, childName)) continue;
                if (std::floor(entry.birthTimestamp) != std::floor(bTime)) continue;
                MARAS_LOG_WARN("[Fertility] Infant {:08X} ('{}') already registered for mother {:08X} on this birth day. Skipping duplicate birth registration.",
                               cID, entry.name, motherID);
                return cID;
            }
        }

        RE::FormID childID = syntheticChildID;
        if (childID == 0) {
            childID = nextSyntheticChildID_++;
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
        ApplyBirthCustodyRules(childID, entry, motherID, presumedFatherID, bioF);
        gA = entry.guardianAID;
        gB = entry.guardianBID;
        childrenByID[childID] = entry;

        MARAS_LOG_INFO("[Fertility] Registered fertility child {:08X} ('{}') to mother {:08X}, presumed father {:08X}, bio father {:08X} (isSecret: {}, secretPartner: {}, secretBio: {}, mismatch: {})",
                       childID, childName, motherID, presumedFatherID, bioFatherID, pSec.isSecret, pSec.isSecretForPartner, pSec.isSecretForBioFather, pSec.hasPhenotypeMismatch);

        PrismaUIDataBridge::GetSingleton().OnChildUpdated(entry);

        UpdateChildBothParentsFaction(childID);
        UpdateSpouseChildrenFaction(gA);
        if (gB != 0) UpdateSpouseChildrenFaction(gB);

        // Birth evaluation if distinct phenotype mismatch (EvaluatePaternitySecret handles both SkyrimNet and non-SkyrimNet fallback)
        if (pSec.isSecret && pSec.hasPhenotypeMismatch) {
            float roll = Utils::RandomFloat(0.0f, 100.0f);
            float birthChance = static_cast<float>(Settings::GetSingleton().paternityPhenotypeBirthChance);
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

    bool NPCRelationshipManager::ResolvePaternitySecret(RE::FormID childID, bool discoveredByPartner) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.paternitySecret.discovered = true;
            it->second.paternitySecret.discoveredByPartner = discoveredByPartner;
            it->second.paternitySecret.isSecret = false;
            it->second.paternitySecret.isSecretForPartner = false;
            it->second.paternitySecret.isSecretForBioFather = false;
            MARAS_LOG_INFO("[Fertility] Fully resolved paternity secret for child {:08X} (discoveredByPartner: {})", childID, discoveredByPartner);

            // If this is an unborn child, also resolve for all unborn siblings from the same biological father
            if (it->second.stage == ChildStage::Unborn) {
                RE::FormID motherID = GetBioMother(childID);
                RE::FormID bioFatherID = GetBioFather(childID);
                for (auto& [sibID, sibEntry] : childrenByID) {
                    if (sibID != childID && sibEntry.stage == ChildStage::Unborn &&
                        MatchesActor(GetBioMother(sibID), motherID) && MatchesActor(GetBioFather(sibID), bioFatherID)) {
                        sibEntry.paternitySecret.discovered = true;
                        sibEntry.paternitySecret.discoveredByPartner = discoveredByPartner;
                        sibEntry.paternitySecret.isSecret = false;
                        sibEntry.paternitySecret.isSecretForPartner = false;
                        sibEntry.paternitySecret.isSecretForBioFather = false;
                        MARAS_LOG_INFO("[Fertility] Also fully resolved paternity secret on unborn sibling {:08X}", sibID);
                    }
                }
            }
            return true;
        }
        return false;
    }

    bool NPCRelationshipManager::ResolvePaternitySecretForPartner(RE::FormID childID, bool discoveredByPartner, bool sendEvent) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.paternitySecret.isSecretForPartner = false;
            it->second.paternitySecret.discoveredByPartner = discoveredByPartner;
            if (!it->second.paternitySecret.isSecretForBioFather) {
                it->second.paternitySecret.discovered = true;
                it->second.paternitySecret.isSecret = false;
            }
            MARAS_LOG_INFO("[Fertility] Resolved paternity secret for partner on child {:08X} (discoveredByPartner: {}, stillSecretForBio: {})",
                           childID, discoveredByPartner, it->second.paternitySecret.isSecretForBioFather);

            // If this is an unborn child, also resolve for all unborn siblings from the same biological father
            if (it->second.stage == ChildStage::Unborn) {
                RE::FormID motherID = GetBioMother(childID);
                RE::FormID bioFatherID = GetBioFather(childID);
                for (auto& [sibID, sibEntry] : childrenByID) {
                    if (sibID != childID && sibEntry.stage == ChildStage::Unborn &&
                        MatchesActor(GetBioMother(sibID), motherID) && MatchesActor(GetBioFather(sibID), bioFatherID)) {
                        sibEntry.paternitySecret.isSecretForPartner = false;
                        sibEntry.paternitySecret.discoveredByPartner = discoveredByPartner;
                        if (!sibEntry.paternitySecret.isSecretForBioFather) {
                            sibEntry.paternitySecret.discovered = true;
                            sibEntry.paternitySecret.isSecret = false;
                        }
                        MARAS_LOG_INFO("[Fertility] Also resolved paternity secret for partner on unborn sibling {:08X}", sibID);
                    }
                }
            }

            if (sendEvent) {
                if (auto* source = SKSE::GetModCallbackEventSource()) {
                    std::string outcome = discoveredByPartner ? "presumed_father_learnt" : "mother_confessed_presumed";
                    float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
                    SKSE::ModCallbackEvent modEvent("maras_paternity_secret_resolved", outcome, childIdFloat, nullptr);
                    source->SendEvent(&modEvent);
                }
            }

            return true;
        }
        return false;
    }

    bool NPCRelationshipManager::ResolvePaternitySecretForBioFather(RE::FormID childID, bool discoveredByBioFather, bool sendEvent) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.paternitySecret.isSecretForBioFather = false;
            it->second.paternitySecret.discoveredByBioFather = discoveredByBioFather;
            if (!it->second.paternitySecret.isSecretForPartner) {
                it->second.paternitySecret.discovered = true;
                it->second.paternitySecret.isSecret = false;
            }
            MARAS_LOG_INFO("[Fertility] Resolved paternity secret for bio father on child {:08X} (discoveredByBio: {}, stillSecretForPartner: {})",
                           childID, discoveredByBioFather, it->second.paternitySecret.isSecretForPartner);

            // If this is an unborn child, also resolve for all unborn siblings from the same biological father
            if (it->second.stage == ChildStage::Unborn) {
                RE::FormID motherID = GetBioMother(childID);
                RE::FormID bioFatherID = GetBioFather(childID);
                for (auto& [sibID, sibEntry] : childrenByID) {
                    if (sibID != childID && sibEntry.stage == ChildStage::Unborn &&
                        MatchesActor(GetBioMother(sibID), motherID) && MatchesActor(GetBioFather(sibID), bioFatherID)) {
                        sibEntry.paternitySecret.isSecretForBioFather = false;
                        sibEntry.paternitySecret.discoveredByBioFather = discoveredByBioFather;
                        if (!sibEntry.paternitySecret.isSecretForPartner) {
                            sibEntry.paternitySecret.discovered = true;
                            sibEntry.paternitySecret.isSecret = false;
                        }
                        MARAS_LOG_INFO("[Fertility] Also resolved paternity secret for bio father on unborn sibling {:08X}", sibID);
                    }
                }
            }

            if (sendEvent) {
                if (auto* source = SKSE::GetModCallbackEventSource()) {
                    std::string outcome = discoveredByBioFather ? "bio_father_learnt" : "mother_confessed_bio";
                    float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
                    SKSE::ModCallbackEvent modEvent("maras_paternity_secret_resolved", outcome, childIdFloat, nullptr);
                    source->SendEvent(&modEvent);
                }
            }

            return true;
        }
        return false;
    }

    bool NPCRelationshipManager::SetPaternitySecretEdges(RE::FormID childID, bool secretForBioFather, bool secretForPartner) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            bool wasSecretBio = it->second.paternitySecret.isSecretForBioFather;
            bool wasSecretPartner = it->second.paternitySecret.isSecretForPartner;

            it->second.paternitySecret.isSecretForBioFather = secretForBioFather;
            it->second.paternitySecret.isSecretForPartner = secretForPartner;
            it->second.paternitySecret.isSecret = secretForBioFather || secretForPartner;
            it->second.paternitySecret.discovered = !it->second.paternitySecret.isSecret;
            it->second.paternitySecret.discoveredByPartner = !secretForPartner;
            it->second.paternitySecret.discoveredByBioFather = !secretForBioFather;
            MARAS_LOG_INFO("[Fertility] Set paternity secret edges on child {:08X} (secretBio: {}, secretPartner: {})",
                           childID, secretForBioFather, secretForPartner);

            // If a secret edge was lifted/learned, notify SkyrimNet via mod event
            bool bioLearned = wasSecretBio && !secretForBioFather;
            bool partnerLearned = wasSecretPartner && !secretForPartner;

            if (auto* source = SKSE::GetModCallbackEventSource()) {
                float childIdFloat = static_cast<float>(static_cast<int32_t>(childID));
                if (bioLearned) {
                    SKSE::ModCallbackEvent modEvent("maras_paternity_secret_resolved", "bio_father_learnt", childIdFloat, nullptr);
                    source->SendEvent(&modEvent);
                }
                if (partnerLearned) {
                    SKSE::ModCallbackEvent modEvent("maras_paternity_secret_resolved", "presumed_father_learnt", childIdFloat, nullptr);
                    source->SendEvent(&modEvent);
                }
            }

            return true;
        }
        return false;
    }

    bool NPCRelationshipManager::SetChildPaternityLastCheck(RE::FormID childID, float currentDay) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.paternitySecret.lastCheckTimestamp = currentDay;
            return true;
        }
        return false;
    }

    bool NPCRelationshipManager::EvaluatePaternitySecret(RE::FormID childID) {
        if (IsSkyrimNetAvailable() && !Settings::GetSingleton().paternityForceDiceRolls) {
            return MARAS::EvaluatePaternitySecret(childID);
        }
        return EvaluatePaternitySecretFallback(childID);
    }

    bool NPCRelationshipManager::EvaluatePaternitySecretFallback(RE::FormID childID) {
        auto it = childrenByID.find(childID);
        if (it == childrenByID.end()) return false;

        const auto& entry = it->second;
        RE::FormID motherID = GetBioMother(childID);
        RE::FormID partnerID = GetMotherPartner(childID);
        RE::FormID bioFatherID = GetBioFather(childID);

        if (motherID == 0 || partnerID == 0 || bioFatherID == 0) return false;

        auto calendar = RE::Calendar::GetSingleton();
        float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        const auto& settings = Settings::GetSingleton();

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
        SKSE::GetTaskInterface()->AddTask([childID]() {
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            PrismaUIDataBridge::GetSingleton().SendNPCDetail(childID);
        });

        return true;
    }

    bool NPCRelationshipManager::IsSecretForBioFather(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            return it->second.paternitySecret.isSecretForBioFather;
        }
        return false;
    }

    bool NPCRelationshipManager::IsSecretForPartner(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            if (GetMotherPartner(childID) == 0) return false;
            return it->second.paternitySecret.isSecretForPartner;
        }
        return false;
    }

    bool NPCRelationshipManager::SetChildStage(RE::FormID childID, ChildStage stage) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.stage = stage;
            MARAS_LOG_INFO("[Fertility] Set child {:08X} stage to {}", childID, static_cast<int>(stage));
            UpdateNonPlayerChildPackageOverride(childID);
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(it->second);
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            return true;
        }
        return false;
    }

    ChildStage NPCRelationshipManager::GetChildStage(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            return it->second.stage;
        }
        return ChildStage::Unborn;
    }

    bool NPCRelationshipManager::SetChildTrainingLocation(RE::FormID childID, const std::string& location) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.trainingLocation = location;
            it->second.stage = ChildStage::Training;
            MARAS_LOG_INFO("[Fertility] Set child {:08X} training location to '{}'", childID, location);
            UpdateNonPlayerChildPackageOverride(childID);
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(it->second);
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            return true;
        }
        return false;
    }

    std::string NPCRelationshipManager::GetChildTrainingLocation(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            return it->second.trainingLocation;
        }
        return "";
    }

    bool NPCRelationshipManager::SetChildGender(RE::FormID childID, uint8_t gender) {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.gender = gender;
            MARAS_LOG_INFO("[Fertility] Set child {:08X} gender to {}", childID, gender == 1 ? "female" : "male");
            return true;
        }
        return false;
    }

    uint8_t NPCRelationshipManager::GetChildGender(RE::FormID childID) const {
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            if (it->second.stage == ChildStage::Unborn) {
                return 2;
            }
            if (HasPhysicalActor(childID)) {
                auto* actor = NPCIndex::GetSingleton().GetActorFromFormId(childID);
                if (actor && actor->GetActorBase()) {
                    return (actor->GetActorBase()->GetSex() == RE::SEX::kFemale) ? 1 : 0;
                }
            }
            return it->second.gender;
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
        auto it = childrenByID.find(childID);
        if (it != childrenByID.end()) {
            it->second.name = newName;
            MARAS_LOG_INFO("[Fertility] Updated child {:08X} name to '{}'", childID, newName);
            PrismaUIDataBridge::GetSingleton().OnChildUpdated(it->second);
            return true;
        }
        return false;
    }

    RE::FormID NPCRelationshipManager::FindSyntheticChild(RE::FormID motherID, const std::string& name) const {
        // First pass: match synthetic ID with exact mother and name
        for (const auto& [cID, entry] : childrenByID) {
            if (!IsSyntheticChildID(cID)) continue;
            if (motherID != 0 && !MatchesActor(entry.guardianAID, motherID) && !MatchesActor(entry.guardianBID, motherID) &&
                !MatchesActor(entry.bioMotherID, motherID) && !MatchesActor(GetBioMother(cID), motherID)) {
                continue;
            }
            if (!name.empty() && StringUtils::CaseInsensitiveEquals(entry.name, name)) {
                return cID;
            }
        }

        // Second pass: if motherID provided and name is empty, return first synthetic child belonging to this mother
        if (motherID != 0 && name.empty()) {
            for (const auto& [cID, entry] : childrenByID) {
                if (!IsSyntheticChildID(cID)) continue;
                if (MatchesActor(entry.guardianAID, motherID) || MatchesActor(entry.guardianBID, motherID) ||
                    MatchesActor(entry.bioMotherID, motherID) || MatchesActor(GetBioMother(cID), motherID)) {
                    return cID;
                }
            }
        }

        // Third pass: if motherID is 0, match synthetic ID with name alone across all synthetic children
        if (motherID == 0 && !name.empty()) {
            for (const auto& [cID, entry] : childrenByID) {
                if (!IsSyntheticChildID(cID)) continue;
                if (StringUtils::CaseInsensitiveEquals(entry.name, name)) {
                    return cID;
                }
            }
        }

        return 0;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetChildrenWithActivePaternitySecrets() const {
        std::vector<RE::FormID> result;
        for (const auto& [cID, entry] : childrenByID) {
            if (entry.paternitySecret.HasActiveSecret() && !entry.paternitySecret.discovered) {
                result.push_back(cID);
            }
        }
        return result;
    }

    void NPCRelationshipManager::CheckPaternitySecretsDailyTick(float currentDay) {
        const auto& settings = Settings::GetSingleton();
        std::vector<RE::FormID> childrenToEvaluate;

        for (auto& [cID, entry] : childrenByID) {
            if (entry.stage == ChildStage::Unborn || entry.birthTimestamp <= 0.0f) {
                continue;
            }
            if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) {
                continue;
            }

            float childAgeDays = currentDay - entry.birthTimestamp;
            if (childAgeDays <= 0.0f) continue;

            float chance = 0.0f;
            if (entry.paternitySecret.hasPhenotypeMismatch) {
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
                entry.paternitySecret.lastCheckTimestamp = currentDay;
                childrenToEvaluate.push_back(cID);
            }
        }

        for (RE::FormID evalID : childrenToEvaluate) {
            EvaluatePaternitySecret(evalID);
        }
    }

}  // namespace MARAS

