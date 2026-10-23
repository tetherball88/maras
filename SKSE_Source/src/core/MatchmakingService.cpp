#include "core/MatchmakingService.h"
#include "core/AffectionService.h"
#include "core/DeceasedRegistry.h"
#include "core/FormCache.h"
#include "core/HomeCellService.h"
#include "core/PlayerHouseService.h"
#include "core/SpouseHierarchyManager.h"
#include "core/SkyrimNetIntegration.h"
#include "core/LoggingService.h"
#include "services/ActorService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/Settings.h"
#include "utils/ActorUtils.h"
#include "utils/EnumUtils.h"
#include "utils/JsonService.h"
#include "utils/StringUtils.h"

namespace MARAS {

    static std::string SafeJsonDump(const nlohmann::json& j) {
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    static std::string GetJsonString(const nlohmann::json& obj, std::string_view key, std::string_view defaultVal = "") {
        if (obj.is_object() && obj.contains(key) && obj[key].is_string()) {
            return obj[key].get<std::string>();
        }
        return std::string(defaultVal);
    }

    static bool GetJsonBool(const nlohmann::json& obj, std::string_view key, bool defaultVal = false) {
        if (obj.is_object() && obj.contains(key) && obj[key].is_boolean()) {
            return obj[key].get<bool>();
        }
        return defaultVal;
    }

    static float GetJsonFloat(const nlohmann::json& obj, std::string_view key, float defaultVal = 0.0f) {
        if (obj.is_object() && obj.contains(key) && obj[key].is_number()) {
            return obj[key].get<float>();
        }
        return defaultVal;
    }

    float MatchmakingService::GetCurrentGameDay() const {
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            return calendar->GetDaysPassed();
        }
        return 0.0f;
    }

    struct ActorProfileSnapshot {
        std::string name;
        std::string race;
        std::string sex;
        int sexInt = 0;
        uint64_t uuid = 0;
    };

    static ActorProfileSnapshot GetActorProfileSnapshot(RE::FormID actorID) {
        auto entry = NPCIndex::GetSingleton().FindByFormID(actorID);
        ActorProfileSnapshot p;
        p.name = entry && !entry->nameDisplay.empty() ? entry->nameDisplay : ActorSvc::GetName(actorID);
        p.sexInt = entry ? entry->sex : ActorSvc::GetSex(actorID);
        p.sex = (p.sexInt == 1) ? "female" : "male";
        p.race = (entry && !entry->raceName.empty()) ? entry->raceName : ActorSvc::GetRaceName(actorID);
        if (p.race.empty()) {
            p.race = "Nord";
        }
        p.uuid = GetActorUUID(actorID);
        return p;
    }

    static RE::FormID GetCommittedPrimaryPartner(RE::FormID actorID) {
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto married = mgr.GetPartners(actorID, RelationshipStatus::Married);
        if (!married.empty()) return married.front();
        const auto engaged = mgr.GetPartners(actorID, RelationshipStatus::Engaged);
        if (!engaged.empty()) return engaged.front();
        const auto dating = mgr.GetPartners(actorID, RelationshipStatus::Dating);
        if (!dating.empty()) return dating.front();
        return 0;
    }

    static bool IsActorInExclusiveMainRelationship(RE::FormID actorID) {
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        bool hasMain = !mgr.GetPartners(actorID, RelationshipStatus::Dating).empty() ||
                       !mgr.GetPartners(actorID, RelationshipStatus::Engaged).empty() ||
                       !mgr.GetPartners(actorID, RelationshipStatus::Married).empty();
        return hasMain && !mgr.HasAnyOpenMainPair(actorID);
    }

    static bool IsPairCheating(RE::FormID actorA, RE::FormID actorB, const std::string& targetStatus) {
        if (targetStatus != "lover" || actorA == 0 || actorB == 0) return false;
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        if (mgr.IsPlayerActiveMainPartner(actorA) && mgr.IsPlayerActiveMainPartner(actorB)) {
            return false;
        }
        return IsActorInExclusiveMainRelationship(actorA) || IsActorInExclusiveMainRelationship(actorB);
    }

    static std::pair<std::string, RelationshipStatus> GetBreakupTarget(RelationshipStatus currentStatus) {
        switch (currentStatus) {
            case RelationshipStatus::Married:
                return {"divorced", RelationshipStatus::Divorced};
            case RelationshipStatus::Engaged:
                return {"jilted", RelationshipStatus::Jilted};
            case RelationshipStatus::Dating:
                return {"exdating", RelationshipStatus::ExDating};
            case RelationshipStatus::Lover:
                return {"exlover", RelationshipStatus::ExLover};
            default:
                return {"", RelationshipStatus::Unknown};
        }
    }

    struct BatchFailureSummary {
        std::vector<size_t> indices;
        std::vector<std::string> descriptions;
        bool hasRequestFailure = false;
        bool hasInvalidJson = false;
    };

    template <typename TaskType>
    static BatchFailureSummary CollectBatchFailures(const std::vector<TaskType>& tasks) {
        BatchFailureSummary summary;
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].failed) {
                summary.indices.push_back(i);
                summary.descriptions.push_back(std::format("• {} & {}: {}",
                    tasks[i].result.actorAName, tasks[i].result.actorBName, tasks[i].failureReason));
                if (tasks[i].isInvalidJson) {
                    summary.hasInvalidJson = true;
                } else {
                    summary.hasRequestFailure = true;
                }
            }
        }
        return summary;
    }

    static std::vector<size_t> MakeBatchIndices(size_t count) {
        std::vector<size_t> indices(count);
        std::iota(indices.begin(), indices.end(), 0);
        return indices;
    }

    template <typename Container>
    static std::string JoinStrings(const Container& items, std::string_view delim) {
        std::string result;
        bool first = true;
        for (const auto& item : items) {
            if (!first) result += delim;
            first = false;
            result += item;
        }
        return result;
    }

    static inline bool IsActiveRelationship(RelationshipStatus status) {
        return status == RelationshipStatus::Dating ||
               status == RelationshipStatus::Engaged ||
               status == RelationshipStatus::Married ||
               status == RelationshipStatus::Lover;
    }

    static const nlohmann::json* FindJsonArray(const nlohmann::json& resp, std::string_view key) {
        if (resp.is_discarded()) return nullptr;
        if (resp.is_array()) return &resp;
        if (resp.is_object() && resp.contains(key) && resp[key].is_array()) return &resp[key];
        return nullptr;
    }

    template <typename BatchType, typename RetryFn, typename CancelFn, typename FinishFn>
    static void HandleBatchCompletion(std::shared_ptr<BatchType> batch,
                                      const std::string& modalTitle,
                                      std::string_view llmVariant,
                                      MatchmakingService* service,
                                      RetryFn retryFn,
                                      CancelFn cancelFn,
                                      FinishFn finishFn) {
        if (!batch) return;
        if (batch->pendingCount > 0) {
            batch->pendingCount--;
        }
        if (batch->pendingCount == 0) {
            auto summary = CollectBatchFailures(batch->tasks);
            if (!summary.indices.empty()) {
                MARAS_LOG_WARN("[Matchmaking] Batch finished with {} failed task(s). Showing in-game dialog.", summary.indices.size());
                auto failedIndices = summary.indices;
                service->ShowInGameFailedLLMModal(
                    modalTitle,
                    summary.descriptions,
                    [batch, failedIndices, retryFn]() {
                        batch->pendingCount = failedIndices.size();
                        retryFn(batch, failedIndices);
                    },
                    [batch, failedIndices, cancelFn, finishFn]() {
                        MARAS_LOG_INFO("[Matchmaking] User cancelled failed task(s). Relationship changes will NOT be applied for failed pairs.");
                        cancelFn(batch, failedIndices);
                        finishFn(batch);
                    },
                    summary.hasRequestFailure,
                    summary.hasInvalidJson,
                    llmVariant
                );
            } else {
                finishFn(batch);
            }
        }
    }

    static std::string GetEligibilityString(bool canFormMainTier, bool hasOpenMainPartner) {
        if (canFormMainTier) {
            return "can form main";
        }
        if (hasOpenMainPartner) {
            return "secondary only (allowed)";
        }
        return "secondary only (cheating)";
    }

    static std::string GetOrientationRuleText(Settings::MatchmakingOrientation orientation) {
        switch (orientation) {
        case Settings::MatchmakingOrientation::Heterosexual:
            return "Heterosexual only: ONLY pair different-sex NPCs (male + female). Do NOT pair same-sex NPCs.";
        case Settings::MatchmakingOrientation::Homosexual:
            return "Homosexual only: ONLY pair same-sex NPCs (male + male OR female + female). Do NOT pair different-sex NPCs.";
        case Settings::MatchmakingOrientation::Gay:
            return "Gay (male-male) only: ONLY pair male NPCs with other male NPCs. Do NOT pair female NPCs.";
        case Settings::MatchmakingOrientation::Lesbian:
            return "Lesbian (female-female) only: ONLY pair female NPCs with other female NPCs. Do NOT pair male NPCs.";
        case Settings::MatchmakingOrientation::HeteroAndGay:
            return "Heterosexual and Gay: ONLY pair different-sex NPCs (male + female) OR male same-sex NPCs (male + male). Do NOT pair female same-sex NPCs.";
        case Settings::MatchmakingOrientation::HeteroAndLesbian:
            return "Heterosexual and Lesbian: ONLY pair different-sex NPCs (male + female) OR female same-sex NPCs (female + female). Do NOT pair male same-sex NPCs.";
        case Settings::MatchmakingOrientation::Any:
        default:
            return "Any orientation: Pairing any combination of sexes (heterosexual or homosexual) is allowed.";
        }
    }

    static bool IsPairOrientationAllowed(int sexA, int sexB, Settings::MatchmakingOrientation orientation) {
        switch (orientation) {
        case Settings::MatchmakingOrientation::Heterosexual:
            return sexA != sexB;
        case Settings::MatchmakingOrientation::Homosexual:
            return sexA == sexB;
        case Settings::MatchmakingOrientation::Gay:
            return sexA == 0 && sexB == 0;
        case Settings::MatchmakingOrientation::Lesbian:
            return sexA == 1 && sexB == 1;
        case Settings::MatchmakingOrientation::HeteroAndGay:
            return (sexA != sexB) || (sexA == 0 && sexB == 0);
        case Settings::MatchmakingOrientation::HeteroAndLesbian:
            return (sexA != sexB) || (sexA == 1 && sexB == 1);
        case Settings::MatchmakingOrientation::Any:
        default:
            return true;
        }
    }

    static bool IsCandidateEligibleForTarget(int targetSex, int candidateSex, Settings::MatchmakingOrientation orientation) {
        switch (orientation) {
        case Settings::MatchmakingOrientation::Heterosexual:
            return candidateSex != targetSex;
        case Settings::MatchmakingOrientation::Homosexual:
            return candidateSex == targetSex;
        case Settings::MatchmakingOrientation::Gay:
            return targetSex == 0 && candidateSex == 0;
        case Settings::MatchmakingOrientation::Lesbian:
            return targetSex == 1 && candidateSex == 1;
        case Settings::MatchmakingOrientation::HeteroAndGay:
            return (candidateSex != targetSex) || (targetSex == 0 && candidateSex == 0);
        case Settings::MatchmakingOrientation::HeteroAndLesbian:
            return (candidateSex != targetSex) || (targetSex == 1 && candidateSex == 1);
        case Settings::MatchmakingOrientation::Any:
        default:
            return true;
        }
    }

    static std::string FormatNpcLine(const NPCEntry& npc) {
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto fid = npc.refFormID;

        const auto dating = mgr.GetPartners(fid, RelationshipStatus::Dating);
        const auto engaged = mgr.GetPartners(fid, RelationshipStatus::Engaged);
        const auto married = mgr.GetPartners(fid, RelationshipStatus::Married);
        bool canFormMain = dating.empty() && engaged.empty() && married.empty();
        bool hasOpenMain = mgr.HasAnyOpenMainPair(fid);

        std::string name = npc.nameDisplay.empty() ? npc.nameLower : npc.nameDisplay;
        std::string sex = (npc.sex == 1) ? "female" : "male";
        std::string race = npc.raceName.empty() ? "Nord" : npc.raceName;
        std::string elig = GetEligibilityString(canFormMain, hasOpenMain);

        return std::format("{} ({}, {}) - [{}]", name, sex, race, elig);
    }

    std::string MatchmakingService::BuildExistingPairsString() {
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto& allPairs = mgr.GetAllRelationshipPairs();

        std::ostringstream ss;
        bool first = true;
        for (const auto& [key, data] : allPairs) {
            if (key.actorA == kPlayerFormID || key.actorB == kPlayerFormID ||
                mgr.IsPlayerActiveMainPartner(key.actorA) || mgr.IsPlayerActiveMainPartner(key.actorB)) {
                continue;
            }
            if (!IsActiveRelationship(data.status)) {
                continue;
            }

            std::string nameA = ActorSvc::GetName(key.actorA);
            std::string nameB = ActorSvc::GetName(key.actorB);
            if (nameA.empty() || nameA == "None" || nameB.empty() || nameB == "None") {
                continue;
            }

            auto children = mgr.GetChildren(key.actorA, key.actorB);
            size_t childCount = children.size();
            std::string statusStr = StringUtils::ToLowerStd(Utils::RelationshipStatusToString(data.status));
            std::string openStr = data.isOpen ? "open" : "exclusive";

            if (!first) ss << "\n";
            first = false;

            if (data.status == RelationshipStatus::Lover) {
                ss << std::format("- {} & {}: lover ({} {})", nameA, nameB, childCount, childCount == 1 ? "child" : "children");
            } else {
                ss << std::format("- {} & {}: {} ({}, {} {})", nameA, nameB, statusStr, openStr, childCount, childCount == 1 ? "child" : "children");
            }
        }
        return ss.str().empty() ? "None" : ss.str();
    }

    std::string MatchmakingService::BuildTargetRelationshipsString(RE::FormID targetNpcFormID) {
        return BuildActorDetailedRelationshipsString(targetNpcFormID);
    }

    static std::string BuildAllowedOperationsText(bool allowBreakups, bool allowOpenClose) {
        std::ostringstream ss;
        ss << "- **Create new relationships** (dating, lover — mark whether lover is allowed or considered cheating)\n";
        ss << "- **Advance existing relationships** (dating->engaged, engaged->married, lover->dating, lover->engaged)";
        if (allowBreakups) {
            ss << "\n- **Break up existing relationships** (to exdating, exlover, divorced, jilted)";
        }
        if (allowOpenClose) {
            ss << "\n- **Open or close relationships** (switch between open and exclusive)";
        }
        return ss.str();
    }

    struct TargetAllowedOperations {
        std::string operationsText;
        std::string actions;
        std::string statuses;
    };

    static TargetAllowedOperations BuildTargetAllowedOperations(RE::FormID targetNpcFormID, bool allowBreakups, bool allowOpenClose, bool allowCheating) {
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto datingPartners = mgr.GetPartners(targetNpcFormID, RelationshipStatus::Dating);
        const auto engagedPartners = mgr.GetPartners(targetNpcFormID, RelationshipStatus::Engaged);
        const auto marriedPartners = mgr.GetPartners(targetNpcFormID, RelationshipStatus::Married);
        const auto loverPartners = mgr.GetPartners(targetNpcFormID, RelationshipStatus::Lover);

        bool canFormMain = datingPartners.empty() && engagedPartners.empty() && marriedPartners.empty();
        bool hasOpenMain = mgr.HasAnyOpenMainPair(targetNpcFormID);

        std::vector<std::string> opLines;
        std::vector<std::string> actions;
        std::vector<std::string> statuses;

        auto addStatus = [&](const std::string& st) {
            if (std::find(statuses.begin(), statuses.end(), st) == statuses.end()) {
                statuses.push_back(st);
            }
        };

        // 1. Create new relationships
        if (canFormMain) {
            opLines.push_back("- **Create new relationships** (dating, lover - allowed) with an eligible candidate");
            actions.push_back("new");
            addStatus("dating");
            addStatus("lover");
        } else if (hasOpenMain) {
            opLines.push_back("- **Create new relationships** (lover - allowed by open relationship) with an eligible candidate");
            actions.push_back("new");
            addStatus("lover");
        } else if (allowCheating) {
            opLines.push_back("- **Create new relationships** (lover - considered cheating/infidelity) with an eligible candidate");
            actions.push_back("new");
            addStatus("lover");
        }

        // 2. Advance existing relationships
        std::vector<std::string> advanceDetails;
        auto addAdvance = [&](const std::vector<RE::FormID>& partners, const char* from, const char* to) {
            for (auto pid : partners) {
                std::string name = ActorSvc::GetName(pid);
                advanceDetails.push_back(std::format("{}->{} with {}", from, to, name.empty() ? "partner" : name));
                addStatus(to);
            }
        };
        addAdvance(datingPartners, "dating", "engaged");
        addAdvance(engagedPartners, "engaged", "married");

        if (canFormMain) {
            for (auto partnerId : loverPartners) {
                bool partnerCanFormMain = mgr.GetPartners(partnerId, RelationshipStatus::Dating).empty() &&
                                          mgr.GetPartners(partnerId, RelationshipStatus::Engaged).empty() &&
                                          mgr.GetPartners(partnerId, RelationshipStatus::Married).empty();
                if (partnerCanFormMain) {
                    std::string partnerName = ActorSvc::GetName(partnerId);
                    advanceDetails.push_back("lover->dating or lover->engaged with " + (partnerName.empty() ? "lover" : partnerName));
                    addStatus("dating");
                    addStatus("engaged");
                }
            }
        }
        if (!advanceDetails.empty()) {
            actions.push_back("advance");
            opLines.push_back("- **Advance existing relationship** (" + JoinStrings(advanceDetails, ", ") + ")");
        }

        // 3. Break up existing relationships
        if (allowBreakups) {
            std::vector<std::string> breakupDetails;
            auto addBreakups = [&](const std::vector<RE::FormID>& partners, const char* from, const char* to) {
                for (auto pid : partners) {
                    std::string name = ActorSvc::GetName(pid);
                    breakupDetails.push_back(std::format("{} to {} with {}", from, to, name.empty() ? from : name));
                    addStatus(to);
                }
            };
            addBreakups(datingPartners, "dating", "exdating");
            addBreakups(engagedPartners, "engaged", "jilted");
            addBreakups(marriedPartners, "married", "divorced");
            addBreakups(loverPartners, "lover", "exlover");

            if (!breakupDetails.empty()) {
                actions.push_back("breakup");
                opLines.push_back("- **Break up existing relationship** (" + JoinStrings(breakupDetails, ", ") + ")");
            }
        }

        // 4. Open or close relationships
        if (allowOpenClose) {
            std::vector<std::string> openCloseDetails;
            auto allMainPartners = datingPartners;
            allMainPartners.insert(allMainPartners.end(), engagedPartners.begin(), engagedPartners.end());
            allMainPartners.insert(allMainPartners.end(), marriedPartners.begin(), marriedPartners.end());

            bool hasOpen = false;
            bool hasClose = false;
            for (auto partnerId : allMainPartners) {
                std::string partnerName = ActorSvc::GetName(partnerId);
                if (partnerName.empty()) partnerName = "partner";
                if (mgr.CanOpenRelationship(targetNpcFormID, partnerId)) {
                    openCloseDetails.push_back("open relationship with " + partnerName + " (switch to open)");
                    hasOpen = true;
                } else if (mgr.CanCloseRelationship(targetNpcFormID, partnerId)) {
                    openCloseDetails.push_back("close relationship with " + partnerName + " (switch to exclusive)");
                    hasClose = true;
                }
            }

            if (hasOpen) actions.push_back("open");
            if (hasClose) actions.push_back("close");

            if (!openCloseDetails.empty()) {
                opLines.push_back("- **Open or close relationships** (" + JoinStrings(openCloseDetails, ", ") + ")");
            }
        }

        TargetAllowedOperations res;
        res.operationsText = opLines.empty()
            ? "No relationship modifications are currently available for this character based on their relationship status and mod settings."
            : JoinStrings(opLines, "\n");
        res.actions = actions.empty() ? "none" : JoinStrings(actions, "|");
        res.statuses = statuses.empty() ? "none" : JoinStrings(statuses, "|");
        return res;
    }

    static std::string BuildBreakupRuleText(bool allowBreakups) {
        if (!allowBreakups) {
            return "Breakups are DISABLED by mod settings. Do NOT suggest breaking up any relationships and do NOT use action 'breakup'.";
        }
        return "Breakups are ALLOWED. You may suggest breaking up existing relationships (action 'breakup') when appropriate.";
    }

    static std::string BuildOpenCloseRuleText(bool allowOpenClose) {
        if (!allowOpenClose) {
            return "Open/close pair modifications are DISABLED by mod settings. Do NOT output actions 'open' or 'close'.";
        }
        return "Open/close modifications are ALLOWED. You may suggest opening (action 'open') or closing (action 'close') existing pairs.";
    }

    static std::string BuildCheatingRuleText(bool allowCheating) {
        if (!allowCheating) {
            return "Cheating/infidelity is DISABLED by player settings. NPCs marked '[secondary only (cheating)]' (who have exclusive/closed main partners) MUST NOT be paired with any secondary partners (lover).";
        }
        return "Cheating/infidelity is ALLOWED. You may suggest secondary lover relationships for NPCs marked '[secondary only (cheating)]' if dramatically interesting (this creates infidelity drama).";
    }

    static std::string BuildAllowedActionsString(bool allowBreakups, bool allowOpenClose) {
        std::vector<std::string> actions = { "new", "advance" };
        if (allowBreakups) actions.push_back("breakup");
        if (allowOpenClose) {
            actions.push_back("open");
            actions.push_back("close");
        }
        return JoinStrings(actions, "|");
    }

    nlohmann::json MatchmakingService::BuildScheduledContext(float currentGameDay) {
        const auto& settings = Settings::GetSingleton();
        const auto allEntries = NPCIndex::GetSingleton().GetAllEntries(NPCFilter::AdultOnly);

        std::ostringstream npcsStream;
        int count = 0;
        for (const auto& entry : allEntries) {
            if (entry.refFormID == kPlayerFormID || entry.baseFormID == kPlayerFormID) {
                continue;
            }
            if (entry.nameDisplay.empty() || entry.nameDisplay == "None") {
                continue;
            }
            RE::FormID actorFormID = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
            const auto& mgr = NPCRelationshipManager::GetSingleton();
            bool isPlayerPartner = mgr.IsPlayerActiveMainPartner(actorFormID) ||
                                   (entry.baseFormID != 0 && mgr.IsPlayerActiveMainPartner(entry.baseFormID));
            if (isPlayerPartner) {
                [[maybe_unused]] bool isOpenWithPlayer = mgr.GetPairIsOpen(actorFormID, kPlayerFormID) ||
                                        (entry.baseFormID != 0 && mgr.GetPairIsOpen(entry.baseFormID, kPlayerFormID));
                // Closed player partners are handled exclusively by TryScheduledPlayerPartnerInfidelity;
                // open-relationship player partners remain in generic matchmaking for consensual lovers unless matchmakingExcludePlayerActivePartners is set.
                // All active main partners of the Player (both closed and open) are governed exclusively by Tier 2 Partner Agency.
                if (true) {
                    continue;
                }
            }
            if (IsNpcOnCooldown(actorFormID, currentGameDay)) {
                continue;
            }
            if (count > 0) npcsStream << "\n";
            npcsStream << FormatNpcLine(entry);
            count++;
        }

        nlohmann::json ctx;
        ctx["maxSuggestions"] = settings.matchmakingMaxSuggestions;
        ctx["gameDay"] = static_cast<int>(currentGameDay);
        ctx["orientationRule"] = GetOrientationRuleText(settings.matchmakingOrientation);
        ctx["cheatingRule"] = BuildCheatingRuleText(settings.matchmakingAllowCheating);
        ctx["allowedOperations"] = BuildAllowedOperationsText(settings.matchmakingAllowBreakups, settings.matchmakingAllowOpenClose);
        ctx["breakupRule"] = BuildBreakupRuleText(settings.matchmakingAllowBreakups);
        ctx["openCloseRule"] = BuildOpenCloseRuleText(settings.matchmakingAllowOpenClose);
        ctx["allowedActions"] = BuildAllowedActionsString(settings.matchmakingAllowBreakups, settings.matchmakingAllowOpenClose);
        ctx["npcsCount"] = count;
        ctx["npcsList"] = npcsStream.str();
        ctx["existingPairsList"] = BuildExistingPairsString();
        return ctx;
    }

    nlohmann::json MatchmakingService::BuildManualContext(RE::FormID targetNpcFormID, float currentGameDay) {
        const auto& settings = Settings::GetSingleton();
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto allEntries = NPCIndex::GetSingleton().GetAllEntries(NPCFilter::AdultOnly);

        auto target = GetActorProfileSnapshot(targetNpcFormID);

        const auto dating = mgr.GetPartners(targetNpcFormID, RelationshipStatus::Dating);
        const auto engaged = mgr.GetPartners(targetNpcFormID, RelationshipStatus::Engaged);
        const auto married = mgr.GetPartners(targetNpcFormID, RelationshipStatus::Married);
        bool canFormMain = dating.empty() && engaged.empty() && married.empty();
        bool hasOpenMain = mgr.HasAnyOpenMainPair(targetNpcFormID);
        std::string targetEligibility = GetEligibilityString(canFormMain, hasOpenMain);

        std::ostringstream candStream;
        int candCount = 0;
        for (const auto& entry : allEntries) {
            if (entry.refFormID == kPlayerFormID || entry.baseFormID == kPlayerFormID) {
                continue;
            }
            if (entry.refFormID == targetNpcFormID || entry.baseFormID == targetNpcFormID) {
                continue;
            }
            if (entry.nameDisplay.empty() || entry.nameDisplay == "None") {
                continue;
            }
            // Filter candidate by matchmaking orientation setting
            if (!IsCandidateEligibleForTarget(target.sexInt, entry.sex, settings.matchmakingOrientation)) {
                continue;
            }

            if (candCount > 0) candStream << "\n";
            candStream << FormatNpcLine(entry);
            candCount++;
        }

        nlohmann::json ctx;
        ctx["maxSuggestions"] = settings.matchmakingMaxSuggestions;
        ctx["gameDay"] = static_cast<int>(currentGameDay);
        ctx["targetName"] = target.name;
        ctx["targetSex"] = target.sex;
        ctx["targetRace"] = target.race;
        ctx["targetMatchUUID"] = target.uuid;
        ctx["targetEligibility"] = targetEligibility;
        ctx["targetRelationshipsList"] = BuildTargetRelationshipsString(targetNpcFormID);
        ctx["orientationRule"] = GetOrientationRuleText(settings.matchmakingOrientation);
        ctx["cheatingRule"] = BuildCheatingRuleText(settings.matchmakingAllowCheating);
        auto targetOps = BuildTargetAllowedOperations(targetNpcFormID, settings.matchmakingAllowBreakups, settings.matchmakingAllowOpenClose, settings.matchmakingAllowCheating);
        ctx["allowedOperations"] = targetOps.operationsText;
        ctx["breakupRule"] = BuildBreakupRuleText(settings.matchmakingAllowBreakups);
        ctx["openCloseRule"] = BuildOpenCloseRuleText(settings.matchmakingAllowOpenClose);
        ctx["allowedActions"] = targetOps.actions;
        ctx["allowedStatuses"] = targetOps.statuses;
        ctx["candidatesCount"] = candCount;
        ctx["candidatesList"] = candStream.str();
        ctx["existingPairsList"] = BuildExistingPairsString();
        return ctx;
    }

    void MatchmakingService::TryScheduledMatchmaking(float currentGameDay) {
        const auto& settings = Settings::GetSingleton();
        if (!settings.matchmakingEnabled) {
            return;
        }

        if (lastMatchmakingDay_ >= 0.0f && (currentGameDay - lastMatchmakingDay_) < static_cast<float>(settings.matchmakingIntervalDays)) {
            return;
        }

        bool useLLM = settings.matchmakingUseLLM && IsSkyrimNetAvailable();
        if (useLLM) {
            RunLLMScheduledMatchmaking(currentGameDay);
        } else {
            RunDiceRollMatchmaking(currentGameDay);
        }
    }

    namespace {

        struct ActorLocationContext {
            std::unordered_set<RE::FormID> activeCells;
            RE::FormID activeCellID = 0;
            std::string activeCellName;
            RE::FormID subLocationID = 0;
            std::string subLocationName;
            RE::FormID settlementLocationID = 0;
            std::string settlementName;
            RE::FormID holdLocationID = 0;
            std::string holdName;
            RE::FormID playerHouseID = 0;
            RE::FormID crimeFactionID = 0;
            std::string locationName;
        };

        std::string GetLocationDisplayName(RE::BGSLocation* loc) {
            if (!loc) return "";
            if (const char* name = loc->GetName(); name && name[0] != '\0') {
                return name;
            }
            if (const char* edid = loc->GetFormEditorID(); edid && edid[0] != '\0') {
                return edid;
            }
            return "";
        }

        std::string GetCellDisplayName(RE::TESObjectCELL* cell) {
            if (!cell) return "";
            if (const char* name = cell->GetName(); name && name[0] != '\0') {
                return name;
            }
            if (const char* edid = cell->GetFormEditorID(); edid && edid[0] != '\0') {
                return edid;
            }
            return "";
        }

        bool HasWhitelistedHabitatKeyword(RE::BGSLocation* loc) {
            if (!loc) return false;
            auto& fc = FormCache::GetSingleton();
            static RE::BGSKeyword* const locTypeDwelling = RE::TESForm::LookupByID<RE::BGSKeyword>(0x000130DC);
            static RE::BGSKeyword* const locTypeCastle = RE::TESForm::LookupByID<RE::BGSKeyword>(0x0001CD57);
            RE::BGSKeyword* const keywords[] = {
                fc.GetLocTypeTempleKeyword(),
                fc.GetLocTypeStoreKeyword(),
                fc.GetLocTypeStewardsDwellingKeyword(),
                fc.GetLocTypeInnKeyword(),
                fc.GetLocTypeHouseKeyword(),
                fc.GetLocTypeBarracksKeyword(),
                fc.GetLocTypeGuildKeyword(),
                fc.GetLocTypePlayerHouseKeyword(),
                fc.GetBYOHLocTypeHomesteadKeyword(),
                fc.GetLocTypeTownKeyword(),
                fc.GetLocTypeSettlementKeyword(),
                fc.GetLocTypeOrcStrongholdKeyword(),
                fc.GetLocTypeCityKeyword(),
                fc.GetLocTypeLumberMillKeyword(),
                fc.GetLocTypeFarmKeyword(),
                locTypeDwelling,
                locTypeCastle
            };
            for (auto* kw : keywords) {
                if (kw && loc->HasKeyword(kw)) {
                    return true;
                }
            }
            return false;
        }

        bool IsSettlementLevelKeyword(RE::BGSLocation* loc) {
            if (!loc) return false;
            auto& fc = FormCache::GetSingleton();
            RE::BGSKeyword* const settlementKws[] = {
                fc.GetLocTypeCityKeyword(),
                fc.GetLocTypeTownKeyword(),
                fc.GetLocTypeSettlementKeyword(),
                fc.GetLocTypeOrcStrongholdKeyword(),
                fc.GetLocTypeLumberMillKeyword(),
                fc.GetLocTypeFarmKeyword(),
                fc.GetLocTypePlayerHouseKeyword(),
                fc.GetBYOHLocTypeHomesteadKeyword()
            };
            for (auto* kw : settlementKws) {
                if (kw && loc->HasKeyword(kw)) {
                    return true;
                }
            }
            return false;
        }

        RE::BGSLocation* FindWhitelistedHabitatInChain(RE::BGSLocation* startLoc) {
            int depth = 0;
            for (auto* cur = startLoc; cur != nullptr && depth < 16; cur = cur->parentLoc, ++depth) {
                if (HasWhitelistedHabitatKeyword(cur)) {
                    return cur;
                }
            }
            return nullptr;
        }

        RE::BGSLocation* FindSettlementRootInChain(RE::BGSLocation* habitatLoc) {
            if (!habitatLoc) return nullptr;
            RE::BGSLocation* bestSettlement = nullptr;
            int depth = 0;
            for (auto* cur = habitatLoc; cur != nullptr && depth < 16; cur = cur->parentLoc, ++depth) {
                if (IsSettlementLevelKeyword(cur)) {
                    bestSettlement = cur;
                    break;
                }
            }
            return bestSettlement ? bestSettlement : habitatLoc;
        }

        RE::BGSLocation* FindHoldRootInChain(RE::BGSLocation* startLoc) {
            if (!startLoc) return nullptr;
            static RE::BGSKeyword* const locTypeHold = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00016771);
            RE::BGSLocation* topMost = startLoc;
            int depth = 0;
            for (auto* cur = startLoc; cur != nullptr && depth < 16; cur = cur->parentLoc, ++depth) {
                if (locTypeHold && cur->HasKeyword(locTypeHold)) {
                    return cur;
                }
                topMost = cur;
            }
            return topMost;
        }

        struct TavernRendezvousTarget {
            RE::FormID innLocationID{0};
            RE::FormID centerMarkerID{0};
            std::string displayName;
        };

        TavernRendezvousTarget FindNearbyTavernForRendezvous(
            RE::BGSLocation* homeLoc,
            const ActorLocationContext& loverLocCtx) {
            TavernRendezvousTarget bestResult;
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            auto* kwInn = FormCache::GetSingleton().GetLocTypeInnKeyword();
            if (!dataHandler || !kwInn) {
                MARAS_LOG_WARN("[Matchmaking:HomeArrival:TavernSearch] Cannot search for tavern: dataHandler={} kwInn={}",
                               static_cast<const void*>(dataHandler), static_cast<const void*>(kwInn));
                return bestResult;
            }

            static RE::BGSLocationRefType* const kLocationCenterLCRT =
                RE::TESForm::LookupByID<RE::BGSLocationRefType>(0x0001BDF1);

            RE::BGSLocation* homeSettlement = FindSettlementRootInChain(homeLoc);
            RE::BGSLocation* homeHold = FindHoldRootInChain(homeLoc);
            RE::FormID homeSettlementID = homeSettlement ? homeSettlement->GetFormID() : 0;
            RE::FormID homeHoldID = homeHold ? homeHold->GetFormID() : 0;

            RE::FormID loverSettlementID = loverLocCtx.settlementLocationID;
            RE::FormID loverHoldID = loverLocCtx.holdLocationID;

            int bestScore = -1;
            size_t candidateInnsEvaluated = 0;

            for (auto* cell : dataHandler->interiorCells) {
                if (!cell) continue;
                RE::BGSLocation* cellLoc = cell->GetLocation();
                if (!cellLoc || !cellLoc->HasKeyword(kwInn)) continue;

                const char* rawInnName = cellLoc->GetFullName();
                if (!rawInnName || rawInnName[0] == '\0') continue;

                RE::FormID exactCenterMarker = 0;
                RE::FormID fallbackMarker = 0;
                cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                    if (!ref || ref->IsDisabled() || ref->IsDeleted()) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    if (kLocationCenterLCRT) {
                        if (auto* lrt = ref->extraList.GetByType<RE::ExtraLocationRefType>()) {
                            if (lrt->locRefType == kLocationCenterLCRT) {
                                exactCenterMarker = ref->GetFormID();
                                return RE::BSContainer::ForEachResult::kStop;
                            }
                        }
                    }
                    if (fallbackMarker == 0 && ref->IsPersistent()) {
                        fallbackMarker = ref->GetFormID();
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });

                RE::FormID chosenMarker = (exactCenterMarker != 0) ? exactCenterMarker : fallbackMarker;
                if (chosenMarker == 0) continue;

                candidateInnsEvaluated++;
                RE::BGSLocation* innSettlement = FindSettlementRootInChain(cellLoc);
                RE::BGSLocation* innHold = FindHoldRootInChain(cellLoc);
                RE::FormID innSettlementID = innSettlement ? innSettlement->GetFormID() : 0;
                RE::FormID innHoldID = innHold ? innHold->GetFormID() : 0;

                int score = (exactCenterMarker != 0) ? 10 : 1;
                if (homeSettlementID != 0 && innSettlementID == homeSettlementID) {
                    score += 1000;
                } else if (loverSettlementID != 0 && innSettlementID == loverSettlementID) {
                    score += 800;
                } else if (homeHoldID != 0 && innHoldID == homeHoldID) {
                    score += 500;
                } else if (loverHoldID != 0 && innHoldID == loverHoldID) {
                    score += 300;
                } else {
                    score += 50;
                }

                if (score > bestScore) {
                    bestScore = score;
                    bestResult.innLocationID = cellLoc->GetFormID();
                    bestResult.centerMarkerID = chosenMarker;
                    std::string innName(rawInnName);
                    std::string settlementName = (innSettlement && innSettlement != cellLoc && innSettlement->GetFullName())
                                                     ? innSettlement->GetFullName()
                                                     : "";
                    if (!settlementName.empty() && innName.find(settlementName) == std::string::npos) {
                        bestResult.displayName = std::format("{} in {}", innName, settlementName);
                    } else {
                        bestResult.displayName = innName;
                    }
                }
            }

            MARAS_LOG_INFO("[Matchmaking:HomeArrival:TavernSearch] Evaluated {} interior inn cell(s) (homeSettlement=0x{:08X}, homeHold=0x{:08X}, loverSettlement=0x{:08X}, loverHold=0x{:08X}) -> selected '{}' (innLoc=0x{:08X}, marker=0x{:08X}, score={})",
                           candidateInnsEvaluated, homeSettlementID, homeHoldID, loverSettlementID, loverHoldID,
                           bestResult.displayName.empty() ? "None" : bestResult.displayName,
                           bestResult.innLocationID, bestResult.centerMarkerID, bestScore);

            return bestResult;
        }

        ActorLocationContext ResolveActorLocationContext(RE::FormID actorFormID) {
            ActorLocationContext ctx;
            if (actorFormID == 0 || actorFormID == kPlayerFormID) return ctx;

            auto& idx = NPCIndex::GetSingleton();
            auto& relMgr = NPCRelationshipManager::GetSingleton();
            auto& houseSvc = PlayerHouseService::GetSingleton();
            auto entryOpt = idx.FindByFormID(actorFormID);

            if (entryOpt.has_value() && entryOpt->crimeFactionID != 0) {
                ctx.crimeFactionID = entryOpt->crimeFactionID;
            } else if (auto* base = ActorSvc::GetActorBase(actorFormID)) {
                if (base->crimeFaction) {
                    ctx.crimeFactionID = base->crimeFaction->GetFormID();
                }
            }

            // Always record household & interior home cells into activeCells
            RE::FormID tenantHouseID = houseSvc.GetTenantHouseFormID(actorFormID);
            if (tenantHouseID == 0) {
                if (const auto* prof = relMgr.GetNPCProfile(actorFormID)) {
                    if (prof->currentHome.has_value() && prof->currentHome.value() != 0) {
                        tenantHouseID = prof->currentHome.value();
                    } else if (prof->originalHome.has_value() && prof->originalHome.value() != 0) {
                        tenantHouseID = prof->originalHome.value();
                    }
                }
            }
            if (tenantHouseID != 0) {
                ctx.playerHouseID = tenantHouseID;
                ctx.activeCells.insert(tenantHouseID);
            }
            RE::FormID homeCellID = HomeCellService::GetSingleton().GetNpcHome(actorFormID);
            if (homeCellID != 0) {
                ctx.activeCells.insert(homeCellID);
            }

            auto applyHabitat = [&](RE::BGSLocation* rawLoc, RE::FormID cellID) -> bool {
                auto* habitat = FindWhitelistedHabitatInChain(rawLoc);
                if (!habitat) return false;
                auto* settlement = FindSettlementRootInChain(habitat);
                auto* holdLoc = FindHoldRootInChain(settlement ? settlement : habitat);
                if (cellID != 0) {
                    ctx.activeCellID = cellID;
                    ctx.activeCells.insert(cellID);
                    if (auto* c = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID)) {
                        ctx.activeCellName = GetCellDisplayName(c);
                    }
                }
                ctx.subLocationID = habitat->GetFormID();
                ctx.subLocationName = GetLocationDisplayName(habitat);
                ctx.settlementLocationID = settlement ? settlement->GetFormID() : habitat->GetFormID();
                ctx.settlementName = GetLocationDisplayName(settlement ? settlement : habitat);
                if (holdLoc) {
                    ctx.holdLocationID = holdLoc->GetFormID();
                    ctx.holdName = GetLocationDisplayName(holdLoc);
                }
                ctx.locationName = !ctx.settlementName.empty() ? ctx.settlementName : ctx.subLocationName;
                return true;
            };

            // 1. Live Runtime State (RE::Actor* GetParentCell & GetCurrentLocation)
            RE::Actor* actor = idx.GetActorFromFormId(actorFormID);
            if (actor && !actor->IsDeleted() && !actor->IsDisabled()) {
                auto* parentCell = actor->GetParentCell();
                RE::FormID cellID = parentCell ? parentCell->GetFormID() : 0;
                RE::BGSLocation* liveLoc = actor->GetCurrentLocation();
                if (!liveLoc && parentCell) {
                    liveLoc = parentCell->GetLocation();
                }
                if (applyHabitat(liveLoc, cellID)) {
                    return ctx;
                }
            }

            // 2. Household Assignment (PlayerHouseService & HomeCellService)
            if (tenantHouseID != 0) {
                if (auto* loc = RE::TESForm::LookupByID<RE::BGSLocation>(tenantHouseID)) {
                    if (!applyHabitat(loc, homeCellID)) {
                        ctx.subLocationID = tenantHouseID;
                        ctx.subLocationName = PlayerHouseService::GetHouseName(tenantHouseID);
                        ctx.settlementLocationID = tenantHouseID;
                        ctx.settlementName = ctx.subLocationName;
                        ctx.locationName = ctx.subLocationName;
                    }
                    return ctx;
                }
                if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(tenantHouseID)) {
                    ctx.activeCellID = tenantHouseID;
                    ctx.activeCellName = GetCellDisplayName(cell);
                    if (!applyHabitat(cell->GetLocation(), tenantHouseID)) {
                        ctx.subLocationID = tenantHouseID;
                        ctx.subLocationName = PlayerHouseService::GetHouseName(tenantHouseID);
                        ctx.settlementLocationID = tenantHouseID;
                        ctx.settlementName = ctx.subLocationName;
                        ctx.locationName = ctx.subLocationName;
                    }
                    return ctx;
                }
            }

            if (homeCellID != 0) {
                if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(homeCellID)) {
                    if (applyHabitat(cell->GetLocation(), homeCellID)) {
                        return ctx;
                    }
                }
            }

            // 3. Static Fallback (NPCEntry editorLocationID + placementCellID)
            if (entryOpt.has_value()) {
                if (entryOpt->editorLocationID != 0) {
                    if (auto* loc = RE::TESForm::LookupByID<RE::BGSLocation>(entryOpt->editorLocationID)) {
                        if (applyHabitat(loc, entryOpt->placementCellID)) {
                            return ctx;
                        }
                    }
                }
                if (entryOpt->placementCellID != 0) {
                    if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(entryOpt->placementCellID)) {
                        if (applyHabitat(cell->GetLocation(), entryOpt->placementCellID)) {
                            return ctx;
                        }
                    }
                }
            }

            return ctx;
        }

        std::string GetAffectionTierLabel(int affection) {
            if (affection >= 75) return "Happy";
            if (affection >= 50) return "Content";
            if (affection >= 25) return "Troubled";
            return "Estranged";
        }

        bool HasPastMarasRelationship(const NPCRelationshipManager& mgr, RE::FormID actorA, RE::FormID actorB) {
            const auto* data = mgr.GetNPCData(actorA, actorB);
            if (!data) return false;
            if (data->status == RelationshipStatus::ExLover || data->status == RelationshipStatus::ExDating ||
                data->status == RelationshipStatus::Jilted || data->status == RelationshipStatus::Divorced) {
                return true;
            }
            if (NPCRelationshipManager::IsActivePartnerStatus(data->originalStatus) ||
                (NPCRelationshipManager::IsBreakupStatus(data->originalStatus) &&
                 data->originalStatus != RelationshipStatus::Candidate)) {
                return true;
            }
            for (const auto& hist : data->statusHistory) {
                if ((hist.fromStatus != RelationshipStatus::Unknown && hist.fromStatus != RelationshipStatus::Candidate) ||
                    (hist.toStatus != RelationshipStatus::Unknown && hist.toStatus != RelationshipStatus::Candidate)) {
                    return true;
                }
            }
            return false;
        }

        std::string GetSharedWorkOrGuildFactionName(RE::FormID actorA, RE::FormID actorB, RE::FormID crimeFacA, RE::FormID crimeFacB) {
            auto* baseA = ActorSvc::GetActorBase(actorA);
            auto* baseB = ActorSvc::GetActorBase(actorB);
            if (!baseA || !baseB) return "";

            for (const auto& fA : baseA->factions) {
                if (!fA.faction || fA.rank < 0) continue;
                RE::FormID fid = fA.faction->GetFormID();
                if (fid == 0 || fid == crimeFacA || fid == crimeFacB) continue;
                if ((fid >> 24) == (FormCache::GetSingleton().GetTrackedFaction() ? (FormCache::GetSingleton().GetTrackedFaction()->GetFormID() >> 24) : 0xFE)) {
                    continue;
                }
                const char* edid = fA.faction->GetFormEditorID();
                if (edid && edid[0] != '\0') {
                    std::string edidLower = StringUtils::ToLowerStd(edid);
                    if (edidLower.find("crime") != std::string::npos ||
                        edidLower.find("town") != std::string::npos ||
                        edidLower.find("favor") != std::string::npos ||
                        edidLower.find("marriage") != std::string::npos ||
                        edidLower.find("potential") != std::string::npos ||
                        edidLower.find("services") != std::string::npos) {
                        continue;
                    }
                }
                for (const auto& fB : baseB->factions) {
                    if (fB.faction == fA.faction && fB.rank >= 0) {
                        if (const char* fName = fA.faction->GetName(); fName && fName[0] != '\0') {
                            return fName;
                        }
                        if (edid && edid[0] != '\0') {
                            return edid;
                        }
                        return "Guild/Faction";
                    }
                }
            }
            return "";
        }

        struct ScoredAffairCandidate {
            RE::FormID formID = 0;
            uint64_t uuid = 0;
            std::string name;
            std::string race;
            std::string sex;
            std::string socialClass;
            std::string temperament;
            std::string eligibility;
            std::string locationName;
            std::string connectionTag;
            int tier = 5;           // 1..5
            int score = 0;
            int jitteredScore = 0;
            std::string reasonsSummary;
        };

        struct TemptedPlayerPartner {
            RE::FormID partnerID = 0;
            std::string partnerName;
            RelationshipStatus statusWithPlayer = RelationshipStatus::Married;
            int affection = 100;
            std::string affectionTier;
            float daysSinceInteraction = 0.0f;
            int hierarchyRank = -1;
            size_t sharedChildrenCount = 0;
            bool playerCheatedOnPartner = false;
            bool isOpenRelationshipLover = false;
            std::string playerAffairPartnerName;
            float finalChance = 0.0f;
            float rolledValue = 0.0f;
            std::string driversSummary;
            ActorLocationContext locCtx;
            std::vector<ScoredAffairCandidate> shortlist;
            std::string evalMode = "candidate_select";
            RE::FormID sideLoverID = 0;
            std::string sideLoverName;
            AffairStage sideAffairStage = AffairStage::Physical;
            float sideLoverAgeDays = 0.0f;
            bool canConfess = false;
        };

        struct PartnerCheatingLLMTask : BatchTaskBase {
            TemptedPlayerPartner partner;
            bool willCheat = false;
            bool willBreakup = false;
            bool willConfess = false;
            bool willSuggest = false;
            RE::FormID chosenCandidateID = 0;
            std::string chosenCandidateName;
            std::string reasoning;
        };

        using PartnerCheatingLLMBatch = BatchBase<PartnerCheatingLLMTask>;

    }  // namespace

    void MatchmakingService::TryScheduledPlayerPartnerInfidelity(float currentGameDay) {
        const auto& settings = Settings::GetSingleton();
        if (!settings.playerPartnerCheatingAgencyEnabled && !settings.playerPartnerOpenRelAgencyEnabled) {
            MARAS_LOG_DEBUG("[Matchmaking:PartnerAgency] Skipping scheduled scan at day {:.2f}: playerPartnerCheatingAgencyEnabled={}, playerPartnerOpenRelAgencyEnabled={}",
                            currentGameDay, settings.playerPartnerCheatingAgencyEnabled, settings.playerPartnerOpenRelAgencyEnabled);
            return;
        }

        float intervalDays = settings.GetPartnerAgencyScanIntervalDays();
        if (lastPlayerPartnerCheatingDay_ >= 0.0f &&
            (currentGameDay - lastPlayerPartnerCheatingDay_) < static_cast<float>(intervalDays)) {
            MARAS_LOG_DEBUG("[Matchmaking:PartnerAgency] Skipping scheduled scan at day {:.2f}: {:.2f}d elapsed since last scan at day {:.2f} (required interval: {:.2f}d)",
                            currentGameDay, currentGameDay - lastPlayerPartnerCheatingDay_, lastPlayerPartnerCheatingDay_, intervalDays);
            return;
        }

        auto& mgr = NPCRelationshipManager::GetSingleton();
        auto& affSvc = AffectionService::GetSingleton();
        auto& hierMgr = SpouseHierarchyManager::GetSingleton();
        auto& deceasedReg = DeceasedRegistry::GetSingleton();
        auto& idx = NPCIndex::GetSingleton();

        std::vector<RE::FormID> playerMainPartners;
        std::unordered_set<RE::FormID> seenPartners;
        auto collectPartners = [&](const std::vector<RE::FormID>& list) {
            for (RE::FormID id : list) {
                if (id != 0 && id != kPlayerFormID && seenPartners.insert(id).second) {
                    playerMainPartners.push_back(id);
                }
            }
        };
        collectPartners(mgr.GetAllMarried(kPlayerFormID));
        collectPartners(mgr.GetAllEngaged(kPlayerFormID));
        collectPartners(mgr.GetAllDating(kPlayerFormID));

        lastPlayerPartnerCheatingDay_ = currentGameDay;

        if (playerMainPartners.empty()) {
            MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Scheduled scan triggered at day {:.2f} (interval={:.2f}d), but player has 0 active main partners.",
                           currentGameDay, intervalDays);
            return;
        }

        const size_t totalPlayerMainPartners = playerMainPartners.size();
        const size_t totalPlayerMarriedSpouses = mgr.GetAllMarried(kPlayerFormID).size();
        auto* playerActor = RE::PlayerCharacter::GetSingleton();
        const bool useLLM = settings.playerPartnerCheatingUseLLM && IsSkyrimNetAvailable();

        MARAS_LOG_INFO("================================================================================");
        MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Starting Stage 1 Partner Agency Scan at day {:.2f} ({} active player main partner(s), {} married) | interval={:.1f}d (L={}d) | useLLM={} | cheatingAgency={} | openRelAgency={} | maxSecretLovers={} | cheatMult={:.2f}x | openMult={:.2f}x | breakupMult={:.2f}x | forceGreets={} | homeArrival={}",
                       currentGameDay, totalPlayerMainPartners, totalPlayerMarriedSpouses,
                       intervalDays, settings.lonelinessThresholdDays, useLLM,
                       settings.playerPartnerCheatingAgencyEnabled, settings.playerPartnerOpenRelAgencyEnabled, settings.playerPartnerCheatingMaxSecretLovers,
                       settings.playerPartnerCheatingChanceMultiplier, settings.playerPartnerAgencyOpenLoverChanceMultiplier,
                       settings.playerPartnerAgencyBreakupChanceMultiplier, settings.playerPartnerAgencyEnableForceGreets,
                       settings.playerPartnerAgencyEnableHomeArrivalEvents);

        thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_real_distribution<float> pctDist(0.0f, 100.0f);
        std::uniform_int_distribution<int> jitterDist(0, 8);

        std::vector<TemptedPlayerPartner> temptedPartners;
        auto getLatestRelStartDay = [](const NPCRelationshipData* relData) -> float {
            if (!relData) return -1.0f;
            for (auto it = relData->statusHistory.rbegin(); it != relData->statusHistory.rend(); ++it) {
                if (!NPCRelationshipManager::IsBreakupStatus(it->toStatus) && it->timestamp >= 0.0f) {
                    return it->timestamp;
                }
            }
            return -1.0f;
        };
        std::vector<TemptedPlayerPartner> agencyLLMTasks;
        std::vector<MatchmakingResult> agencyBreakupMatches;
        auto finalizeInfidelityMatches = [this](std::vector<MatchmakingResult> matches) {
            if (matches.empty()) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] finalizeInfidelityMatches: 0 matches to apply.");
                return;
            }
            if (Settings::GetSingleton().playerPartnerCheatingConfirmResults) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] playerPartnerCheatingConfirmResults is enabled. Presenting review modal for {} partner agency change(s).",
                               matches.size());
                ShowMatchmakingReviewModal(
                    std::move(matches),
                    true,
                    [this](std::vector<MatchmakingResult> approved) {
                        ExecuteMemoryBatchOrApply(std::move(approved), true);
                    });
            } else {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Applying {} partner agency match(es) via ExecuteMemoryBatchOrApply.", matches.size());
                ExecuteMemoryBatchOrApply(std::move(matches), true);
            }
        };

        for (RE::FormID partnerID : playerMainPartners) {
            std::string partnerName = ActorSvc::GetName(partnerID);

            // Hard Immunity 1: Deceased / Deleted / Disabled
            if (deceasedReg.IsDeceased(partnerID)) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Skipping '{}' (0x{:08X}): registered as deceased", partnerName, partnerID);
                continue;
            }
            RE::Actor* livePartner = idx.GetActorFromFormId(partnerID);
            if (livePartner && (livePartner->IsDead() || livePartner->IsDeleted() || livePartner->IsDisabled())) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Skipping '{}' (0x{:08X}): actor is dead/deleted/disabled", partnerName, partnerID);
                continue;
            }

            const bool isOpenWithPlayer = mgr.GetPairIsOpen(partnerID, kPlayerFormID);
            bool forceCheatingFromExpiredSuggestOpen = false;

            // Step 1: Process Expired PendingPartnerIntent (TTL >= 2.0x L days)
            PendingPartnerIntent pendingIntent = mgr.GetPartnerPendingIntent(partnerID);
            if (pendingIntent.type != PartnerIntentType::None) {
                float intentAge = currentGameDay - pendingIntent.createdGameDay;
                float intentTTL = settings.GetPartnerIntentTTLDays();
                if (intentAge >= intentTTL) {
                    if (pendingIntent.type == PartnerIntentType::ConfessTalking) {
                        mgr.ClearPartnerIntent(partnerID);
                        if (pendingIntent.relatedLoverID != 0) {
                            mgr.SetCheatingAffairStage(partnerID, kPlayerFormID, pendingIntent.relatedLoverID, AffairStage::Physical);
                        }
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step1] Expired ConfessTalking intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> affair with 0x{:08X} escalated to Physical",
                                       intentAge, intentTTL, partnerName, partnerID, pendingIntent.relatedLoverID);
                    } else if (pendingIntent.type == PartnerIntentType::ConfessPhysical) {
                        mgr.ClearPartnerIntent(partnerID);
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step1] Expired ConfessPhysical intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> guilt hardened, affair stays secret",
                                       intentAge, intentTTL, partnerName, partnerID);
                    } else if (pendingIntent.type == PartnerIntentType::SuggestOpen) {
                        mgr.ClearPartnerIntent(partnerID);
                        if (!isOpenWithPlayer && settings.playerPartnerCheatingAgencyEnabled) {
                            forceCheatingFromExpiredSuggestOpen = true;
                            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step1] Expired SuggestOpen intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> forcing Stage 2 Talking affair selection",
                                           intentAge, intentTTL, partnerName, partnerID);
                        } else {
                            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step1] Expired SuggestOpen intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) cleared (isOpen={}, cheatingAgencyEnabled={})",
                                           intentAge, intentTTL, partnerName, partnerID, isOpenWithPlayer, settings.playerPartnerCheatingAgencyEnabled);
                        }
                    } else if (pendingIntent.type == PartnerIntentType::SuggestClose) {
                        mgr.ClearPartnerIntent(partnerID);
                        affSvc.SetPermanentAffection(partnerID, affSvc.GetPermanentAffection(partnerID) - 12, true);
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step1] Expired SuggestClose intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> applied -12 permanent affection",
                                       intentAge, intentTTL, partnerName, partnerID);
                    }
                } else {
                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step1] Partner '{}' (0x{:08X}) has active pending intent '{}' (age={:.1f}d < TTL={:.1f}d, lover=0x{:08X})",
                                   partnerName, partnerID, Utils::PartnerIntentTypeToString(pendingIntent.type),
                                   intentAge, intentTTL, pendingIntent.relatedLoverID);
                }
            }

            // Step 2: Deterministic Talking -> Physical Escalation (>= 2.0x L days)
            if (!isOpenWithPlayer && settings.playerPartnerCheatingAgencyEnabled) {
                for (const auto& rec : mgr.GetCheatingRecords(partnerID, kPlayerFormID)) {
                    if (rec.cheaterID == partnerID && rec.victimID == kPlayerFormID &&
                        rec.affairStatus == AffairStatus::Ongoing && rec.affairStage == AffairStage::Talking) {
                        float stageAge = (rec.stageStartDay > 0.0f) ? (currentGameDay - rec.stageStartDay) : 0.0f;
                        float escThreshold = settings.GetAffairTalkingEscalationDays();
                        if (stageAge >= escThreshold) {
                            mgr.SetCheatingAffairStage(partnerID, kPlayerFormID, rec.affairPartnerID, AffairStage::Physical);
                            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step2] Talking affair for '{}' (0x{:08X}) with '{}' (0x{:08X}) reached {:.1f}d (>= {:.1f}d) -> deterministically escalated to Physical",
                                           partnerName, partnerID, ActorSvc::GetName(rec.affairPartnerID), rec.affairPartnerID, stageAge, escThreshold);
                        } else {
                            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Step2] Talking affair for '{}' (0x{:08X}) with '{}' (0x{:08X}) is at {:.1f}d (< {:.1f}d required for Physical escalation)",
                                           partnerName, partnerID, ActorSvc::GetName(rec.affairPartnerID), rec.affairPartnerID, stageAge, escThreshold);
                        }
                    }
                }
            }

            // Hard Immunity 3: Active Follower or Physically Loaded Near the Player
            if (livePartner) {
                if (Utils::IsPlayerFollower(livePartner)) {
                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Skipping '{}' (0x{:08X}): currently an active player follower", partnerName, partnerID);
                    continue;
                }
                if (playerActor && livePartner->Is3DLoaded() &&
                    (livePartner->GetParentCell() == playerActor->GetParentCell() ||
                     livePartner->GetPosition().GetDistance(playerActor->GetPosition()) <= 3000.0f)) {
                    float dist = livePartner->GetPosition().GetDistance(playerActor->GetPosition());
                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Skipping '{}' (0x{:08X}): physically loaded near the player (sameCell={}, dist={:.1f} <= 3000.0)",
                                   partnerName, partnerID, livePartner->GetParentCell() == playerActor->GetParentCell(), dist);
                    continue;
                }
            }

            int preAffection = std::clamp(affSvc.GetPermanentAffection(partnerID), 0, 100);
            float preDaysSince = std::max(0.0f, affSvc.GetDaysSinceLastAffection(partnerID));
            RelationshipStatus preStatus = mgr.GetRelationshipStatus(partnerID, kPlayerFormID);
            Temperament preTemp = mgr.GetTemperament(partnerID);
            size_t preSharedChildren = mgr.GetChildren(kPlayerFormID, partnerID).size();
            int preRank = (preStatus == RelationshipStatus::Married) ? hierMgr.GetRank(partnerID) : -1;

            bool preRevenge = false;
            if (settings.playerPartnerCheatingEnableRevenge) {
                for (const auto& rec : mgr.GetCheatingRecords(partnerID, kPlayerFormID)) {
                    if (rec.cheaterID == kPlayerFormID && rec.victimID == partnerID &&
                        rec.affairStatus == AffairStatus::Ongoing && !rec.isSecret) {
                        preRevenge = true;
                        break;
                    }
                }
            }

            MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Evaluating partner '{}' (0x{:08X}) | Status={} | Open={} | Affection={}/100 ({}) | DaysSinceInteraction={:.1f}d | Temp={} | Rank={} | SharedChildren={} | Revenge={} | PendingIntent={}",
                           partnerName, partnerID, Utils::RelationshipStatusToString(preStatus), isOpenWithPlayer,
                           preAffection, GetAffectionTierLabel(preAffection), preDaysSince,
                           Utils::TemperamentToString(preTemp), preRank, preSharedChildren, preRevenge,
                           Utils::PartnerIntentTypeToString(mgr.GetPartnerPendingIntent(partnerID).type));

            // Action A (Closed Affair Breakup Roll + 20% Confess Sub-Roll) & Action B (Open-Lover Breakup Roll)
            float multBreakupSetting = std::clamp(settings.playerPartnerAgencyBreakupChanceMultiplier, 0.0f, 3.0f);
            if (!isOpenWithPlayer) {
                auto partnerCheatRecs = mgr.GetCheatingRecords(partnerID, kPlayerFormID);
                if (!settings.playerPartnerCheatingAgencyEnabled) partnerCheatRecs.clear();
                size_t ongoingAffairCount = 0;
                for (const auto& rec : partnerCheatRecs) {
                    if (rec.cheaterID != partnerID || rec.victimID != kPlayerFormID ||
                        rec.affairStatus != AffairStatus::Ongoing || rec.affairPartnerID == 0) {
                        continue;
                    }
                    ongoingAffairCount++;
                    std::string loverName = ActorSvc::GetName(rec.affairPartnerID);
                    const auto* loverPair = mgr.GetNPCData(partnerID, rec.affairPartnerID);
                    float affairStartDay = (getLatestRelStartDay(loverPair) >= 0.0f)
                                               ? getLatestRelStartDay(loverPair)
                                               : (rec.stageStartDay > 0.0f ? rec.stageStartDay : static_cast<float>(rec.lastTimestamp));
                    float affairAgeDays = std::max(0.0f, currentGameDay - affairStartDay);
                    float minAgeForBreakup = settings.GetMinSideLoverAgeForBreakupDays();
                    bool brokeUpThisAffair = false;

                    if (multBreakupSetting > 0.0f && affairAgeDays >= minAgeForBreakup) {
                        float baseGuilt = (preAffection >= 75) ? 25.0f : (preAffection >= 50 ? 15.0f : (preAffection >= 25 ? 5.0f : 0.0f));
                        float modAttention = (preDaysSince <= settings.GetRecentEffortMaxDays())
                                                 ? 12.0f
                                                 : (preDaysSince > settings.GetProlongedAbsencePenaltyDays() ? -10.0f : 0.0f);
                        float bonusFatigue = std::min(15.0f, std::max(0.0f, (affairAgeDays - settings.GetAffairFatigueStartDays()) * 0.75f));
                        float bonusChildrenA = (preSharedChildren > 0) ? 10.0f : 0.0f;
                        float penaltyRevenge = preRevenge ? 20.0f : 0.0f;

                        float multTempA = 1.0f;
                        switch (preTemp) {
                            case Temperament::Humble: multTempA = 1.35f; break;
                            case Temperament::Romantic:
                                multTempA = (preDaysSince <= settings.GetRomanticAttendedBreakupMaxDays()) ? 1.25f : 0.75f;
                                break;
                            case Temperament::Independent: multTempA = 1.10f; break;
                            case Temperament::Proud: multTempA = 0.90f; break;
                            case Temperament::Jealous: multTempA = preRevenge ? 0.50f : 1.00f; break;
                        }
                        float multStageA = (rec.affairStage == AffairStage::Talking) ? 1.40f : 0.85f;
                        float pBreakup = std::clamp(
                            (baseGuilt + modAttention + bonusFatigue + bonusChildrenA - penaltyRevenge) *
                                multTempA * multStageA * multBreakupSetting,
                            0.0f, 80.0f);
                        float bRoll = pctDist(gen);
                        bool breakupPassed = (pBreakup > 0.0f && bRoll < pBreakup);

                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionA] '{}' (0x{:08X}) affair breakup roll with '{}' (0x{:08X}, stage={}, age={:.1f}d) | BaseGuilt={:.1f} Attention={:+.1f} Fatigue={:+.1f} ChildBonus={:+.1f} RevengePen=-{:.1f} | TempMult={:.2f}x StageMult={:.2f}x SettingMult={:.2f}x -> Chance={:.1f}% (Roll={:.1f}) => {}",
                                       partnerName, partnerID, loverName, rec.affairPartnerID,
                                       Utils::AffairStageToString(rec.affairStage), affairAgeDays,
                                       baseGuilt, modAttention, bonusFatigue, bonusChildrenA, penaltyRevenge,
                                       multTempA, multStageA, multBreakupSetting, pBreakup, bRoll,
                                       breakupPassed ? (useLLM ? "BREAKUP CANDIDATE (Sending to LLM)" : "BREAKUP") : "KEEP AFFAIR");

                        if (breakupPassed) {
                            MatchmakingResult br;
                            br.actorAFormID = partnerID;
                            br.actorBFormID = rec.affairPartnerID;
                            br.actorAName = partnerName;
                            br.actorBName = loverName;
                            br.action = "breakup";
                            br.targetStatus = "exlover";
                            br.isCheating = false;
                            bool canConfessNow = settings.playerPartnerAgencyEnableForceGreets &&
                                                 mgr.GetPartnerPendingIntent(partnerID).type == PartnerIntentType::None;
                            if (!useLLM) {
                                if (canConfessNow &&
                                    (preTemp == Temperament::Humble || preTemp == Temperament::Romantic) &&
                                    preAffection >= 60) {
                                    float confRoll = pctDist(gen);
                                    if (confRoll < 20.0f) {
                                        br.postBreakupIntent = (rec.affairStage == AffairStage::Talking)
                                                                   ? PartnerIntentType::ConfessTalking
                                                                   : PartnerIntentType::ConfessPhysical;
                                    }
                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionA] '{}' (0x{:08X}) post-breakup confession sub-roll: Chance=20.0% (Roll={:.1f}) => {}",
                                                   partnerName, partnerID, confRoll,
                                                   br.postBreakupIntent != PartnerIntentType::None ? Utils::PartnerIntentTypeToString(br.postBreakupIntent) : "NO CONFESSION");
                                } else {
                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionA] '{}' (0x{:08X}) ineligible for post-breakup confession (canConfessNow={}, temp={}, affection={}/60)",
                                                   partnerName, partnerID, canConfessNow, Utils::TemperamentToString(preTemp), preAffection);
                                }
                            } else {
                                TemptedPlayerPartner tp;
                                tp.evalMode = "breakup_affair";
                                tp.partnerID = partnerID;
                                tp.partnerName = partnerName;
                                tp.statusWithPlayer = preStatus;
                                tp.affection = preAffection;
                                tp.affectionTier = GetAffectionTierLabel(preAffection);
                                tp.daysSinceInteraction = preDaysSince;
                                tp.hierarchyRank = preRank;
                                tp.sharedChildrenCount = preSharedChildren;
                                tp.playerCheatedOnPartner = preRevenge;
                                tp.isOpenRelationshipLover = false;
                                tp.sideLoverID = rec.affairPartnerID;
                                tp.sideLoverName = loverName;
                                tp.sideAffairStage = rec.affairStage;
                                tp.sideLoverAgeDays = affairAgeDays;
                                tp.canConfess = canConfessNow;
                                tp.finalChance = pBreakup;
                                tp.rolledValue = bRoll;
                                tp.driversSummary = "guilt, renewed attention from the player, or affair fatigue";
                                tp.locCtx = ResolveActorLocationContext(partnerID);
                                agencyLLMTasks.push_back(std::move(tp));
                            }
                            if (!useLLM) br.memory = std::format("{} ended their secret {} affair with {} out of guilt and renewed focus on their relationship with the player.",
                                                    partnerName, Utils::AffairStageToString(rec.affairStage), loverName);
                            if (!useLLM) agencyBreakupMatches.push_back(std::move(br));
                            brokeUpThisAffair = true;
                        }
                    } else {
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionA] '{}' (0x{:08X}) ongoing {} affair with '{}' (0x{:08X}, age={:.1f}d): breakup roll skipped (breakupMult={:.2f}x, minAgeRequired={:.1f}d)",
                                       partnerName, partnerID, Utils::AffairStageToString(rec.affairStage),
                                       loverName, rec.affairPartnerID, affairAgeDays, multBreakupSetting, minAgeForBreakup);
                    }

                    if (!brokeUpThisAffair) {
                        bool canStandaloneConfess = settings.playerPartnerAgencyEnableForceGreets &&
                                                    mgr.GetPartnerPendingIntent(partnerID).type == PartnerIntentType::None &&
                                                    (preTemp == Temperament::Humble || preTemp == Temperament::Romantic) &&
                                                    preAffection >= 60 && preDaysSince <= settings.GetRecentEffortMaxDays();
                        if (canStandaloneConfess) {
                            float confRoll = pctDist(gen);
                            bool confPassed = (confRoll < 20.0f);
                            PartnerIntentType confType = (rec.affairStage == AffairStage::Talking)
                                                             ? PartnerIntentType::ConfessTalking
                                                             : PartnerIntentType::ConfessPhysical;
                            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionA] '{}' (0x{:08X}) standalone guilt confession roll for affair with '{}' (0x{:08X}): Chance=20.0% (Roll={:.1f}) => {}",
                                           partnerName, partnerID, loverName, rec.affairPartnerID, confRoll,
                                           confPassed ? (useLLM ? "CONFESS CANDIDATE (Sending to LLM)" : Utils::PartnerIntentTypeToString(confType)) : "NO CONFESSION");
                            if (confPassed) {
                                if (!useLLM) {
                                    mgr.SetPartnerPendingIntent(partnerID, confType, rec.affairPartnerID);
                                } else {
                                    TemptedPlayerPartner tp;
                                    tp.evalMode = "breakup_affair";
                                    tp.partnerID = partnerID;
                                    tp.partnerName = partnerName;
                                    tp.statusWithPlayer = preStatus;
                                    tp.affection = preAffection;
                                    tp.affectionTier = GetAffectionTierLabel(preAffection);
                                    tp.daysSinceInteraction = preDaysSince;
                                    tp.hierarchyRank = preRank;
                                    tp.sharedChildrenCount = preSharedChildren;
                                    tp.playerCheatedOnPartner = preRevenge;
                                    tp.isOpenRelationshipLover = false;
                                    tp.sideLoverID = rec.affairPartnerID;
                                    tp.sideLoverName = loverName;
                                    tp.sideAffairStage = rec.affairStage;
                                    tp.sideLoverAgeDays = affairAgeDays;
                                    tp.canConfess = true;
                                    tp.finalChance = 20.0f;
                                    tp.rolledValue = confRoll;
                                    tp.driversSummary = "remorse and high affection after recent time with the player";
                                    tp.locCtx = ResolveActorLocationContext(partnerID);
                                    agencyLLMTasks.push_back(std::move(tp));
                                }
                            }
                        } else {
                            MARAS_LOG_DEBUG("[Matchmaking:PartnerAgency:ActionA] '{}' (0x{:08X}) ineligible for standalone guilt confession (forceGreets={}, hasIntent={}, temp={}, affection={}/60, daysSince={:.1f}d/{:.1f}d)",
                                            partnerName, partnerID, settings.playerPartnerAgencyEnableForceGreets,
                                            mgr.GetPartnerPendingIntent(partnerID).type != PartnerIntentType::None,
                                            Utils::TemperamentToString(preTemp), preAffection, preDaysSince, settings.GetRecentEffortMaxDays());
                        }
                    }
                }
            } else {
                auto openLovers = mgr.GetPartners(partnerID, RelationshipStatus::Lover);
                if (!settings.playerPartnerOpenRelAgencyEnabled) openLovers.clear();
                size_t activeOpenCount = 0;
                for (RE::FormID lid : openLovers) {
                    if (lid != 0 && lid != kPlayerFormID) activeOpenCount++;
                }
                for (RE::FormID lid : openLovers) {
                    if (lid == 0 || lid == kPlayerFormID) continue;
                    std::string loverName = ActorSvc::GetName(lid);
                    const auto* loverPair = mgr.GetNPCData(partnerID, lid);
                    float flingAgeDays = (getLatestRelStartDay(loverPair) >= 0.0f)
                                             ? std::max(0.0f, currentGameDay - getLatestRelStartDay(loverPair))
                                             : 0.0f;
                    float minAgeForBreakup = settings.GetMinSideLoverAgeForBreakupDays();
                    if (multBreakupSetting > 0.0f && flingAgeDays >= minAgeForBreakup) {
                        float baseFlingAge = (flingAgeDays <= settings.GetOpenFlingNoveltyDays())
                                                 ? 8.0f
                                                 : std::min(28.0f, 8.0f + (flingAgeDays - settings.GetOpenFlingNoveltyDays()) * 1.0f);
                        float bonusMultiLovers = (activeOpenCount >= 2) ? static_cast<float>(activeOpenCount - 1) * 8.0f : 0.0f;
                        float bonusPlayerDevotion = (preAffection >= 80 && preDaysSince <= settings.GetRecentEffortMaxDays()) ? 10.0f : 0.0f;
                        float multTempB = 1.0f;
                        switch (preTemp) {
                            case Temperament::Independent: multTempB = 1.30f; break;
                            case Temperament::Proud: multTempB = 1.15f; break;
                            case Temperament::Romantic: multTempB = 0.75f; break;
                            default: multTempB = 1.00f; break;
                        }
                        float pOpenBreakup = std::clamp(
                            (baseFlingAge + bonusMultiLovers + bonusPlayerDevotion) * multTempB * multBreakupSetting,
                            0.0f, 75.0f);
                        float bRoll = pctDist(gen);
                        bool breakupPassed = (pOpenBreakup > 0.0f && bRoll < pOpenBreakup);

                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionB] '{}' (0x{:08X}) open-lover breakup roll with '{}' (0x{:08X}, age={:.1f}d) | BaseAge={:.1f} MultiLovers={:+.1f} Devotion={:+.1f} | TempMult={:.2f}x SettingMult={:.2f}x -> Chance={:.1f}% (Roll={:.1f}) => {}",
                                       partnerName, partnerID, loverName, lid, flingAgeDays,
                                       baseFlingAge, bonusMultiLovers, bonusPlayerDevotion, multTempB, multBreakupSetting,
                                       pOpenBreakup, bRoll,
                                       breakupPassed ? (useLLM ? "BREAKUP CANDIDATE (Sending to LLM)" : "BREAKUP") : "CONTINUE FLING");

                        if (breakupPassed) {
                            MatchmakingResult br;
                            br.actorAFormID = partnerID;
                            br.actorBFormID = lid;
                            br.actorAName = partnerName;
                            br.actorBName = loverName;
                            br.action = "breakup";
                            br.targetStatus = "exlover";
                            br.isCheating = false;
                            if (useLLM) {
                                TemptedPlayerPartner tp;
                                tp.evalMode = "breakup_open_lover";
                                tp.partnerID = partnerID;
                                tp.partnerName = partnerName;
                                tp.statusWithPlayer = preStatus;
                                tp.affection = preAffection;
                                tp.affectionTier = GetAffectionTierLabel(preAffection);
                                tp.daysSinceInteraction = preDaysSince;
                                tp.hierarchyRank = preRank;
                                tp.sharedChildrenCount = preSharedChildren;
                                tp.playerCheatedOnPartner = false;
                                tp.isOpenRelationshipLover = true;
                                tp.sideLoverID = lid;
                                tp.sideLoverName = loverName;
                                tp.sideLoverAgeDays = flingAgeDays;
                                tp.finalChance = pOpenBreakup;
                                tp.rolledValue = bRoll;
                                tp.driversSummary = "natural cooling of casual open-relationship fling";
                                tp.locCtx = ResolveActorLocationContext(partnerID);
                                agencyLLMTasks.push_back(std::move(tp));
                            }
                            if (!useLLM) br.memory = std::format("{} amicably ended their casual open-relationship fling with {} after {:.0f} days.",
                                                    partnerName, loverName, flingAgeDays);
                            if (!useLLM) agencyBreakupMatches.push_back(std::move(br));
                            if (activeOpenCount > 0) activeOpenCount--;
                        }
                    } else {
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionB] '{}' (0x{:08X}) open-lover fling with '{}' (0x{:08X}, age={:.1f}d): breakup roll skipped (breakupMult={:.2f}x, minAgeRequired={:.1f}d)",
                                       partnerName, partnerID, loverName, lid, flingAgeDays, multBreakupSetting, minAgeForBreakup);
                    }
                }
            }

            if (!forceCheatingFromExpiredSuggestOpen && IsNpcOnCooldown(partnerID, currentGameDay)) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Skipping new bond / intent rolls for '{}' (0x{:08X}): on matchmaking cooldown ({:.1f}d remaining)",
                               partnerName, partnerID, GetNpcCooldownRemaining(partnerID, currentGameDay));
                continue;
            }

            // Hard Immunity 5: Max Secret Lovers Reached (1-3)
            int secretLoversCount = 0;
            for (const auto& rec : mgr.GetCheatingRecordsForActor(partnerID)) {
                if (rec.cheaterID == partnerID && rec.victimID == kPlayerFormID && rec.affairStatus == AffairStatus::Ongoing) {
                    secretLoversCount++;
                }
            }
            if (secretLoversCount == 0) {
                for (RE::FormID lovID : mgr.GetPartners(partnerID, RelationshipStatus::Lover)) {
                    if (lovID != 0 && lovID != kPlayerFormID) {
                        secretLoversCount++;
                    }
                }
            }
            int32_t maxSecretLovers = std::clamp(settings.playerPartnerCheatingMaxSecretLovers, 1, 3);
            if (secretLoversCount >= maxSecretLovers) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Skipping new bond / intent rolls for '{}' (0x{:08X}): already at max side lovers ({}/{})",
                               partnerName, partnerID, secretLoversCount, maxSecretLovers);
                continue;
            }

            // Action E: SuggestOpen / SuggestClose Force-Greet Intents
            if (settings.playerPartnerOpenRelAgencyEnabled && settings.playerPartnerAgencyEnableForceGreets &&
                mgr.GetPartnerPendingIntent(partnerID).type == PartnerIntentType::None &&
                !forceCheatingFromExpiredSuggestOpen) {
                if (!isOpenWithPlayer) {
                    if (secretLoversCount == 0 && preAffection >= 35) {
                        float baseTempE1 = 0.0f;
                        if (preTemp == Temperament::Independent) baseTempE1 = 12.0f;
                        else if (preTemp == Temperament::Proud) baseTempE1 = (preRank >= 1) ? 8.0f : 3.0f;
                        else if (preTemp == Temperament::Romantic) baseTempE1 = (preDaysSince > settings.GetNeglectContentStartDays()) ? 6.0f : 0.0f;

                        if (baseTempE1 > 0.0f) {
                            float bonusPolyE1 = (totalPlayerMainPartners > 1) ? 8.0f : 0.0f;
                            float bonusNeglectE1 = (preDaysSince > settings.GetSuggestOpenNeglectMinDays())
                                                       ? std::min(10.0f, (preDaysSince - settings.GetSuggestOpenNeglectMinDays()) * 1.0f)
                                                       : 0.0f;
                            float pSuggestOpen = std::clamp(baseTempE1 + bonusPolyE1 + bonusNeglectE1, 0.0f, 25.0f);
                            float eRoll = pctDist(gen);
                            bool suggestPassed = (eRoll < pSuggestOpen);
                            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionE1] '{}' (0x{:08X}) SuggestOpen roll | BaseTemp={:.1f} PolyBonus={:+.1f} NeglectBonus={:+.1f} -> Chance={:.1f}% (Roll={:.1f}) => {}",
                                           partnerName, partnerID, baseTempE1, bonusPolyE1, bonusNeglectE1, pSuggestOpen, eRoll,
                                           suggestPassed ? (useLLM ? "SUGGEST_OPEN CANDIDATE (Sending to LLM)" : "QUEUED SuggestOpen") : "NO INTENT");
                            if (suggestPassed) {
                                if (!useLLM) {
                                    mgr.SetPartnerPendingIntent(partnerID, PartnerIntentType::SuggestOpen, 0);
                                } else {
                                    TemptedPlayerPartner tp;
                                    tp.evalMode = "suggest_open";
                                    tp.partnerID = partnerID;
                                    tp.partnerName = partnerName;
                                    tp.statusWithPlayer = preStatus;
                                    tp.affection = preAffection;
                                    tp.affectionTier = GetAffectionTierLabel(preAffection);
                                    tp.daysSinceInteraction = preDaysSince;
                                    tp.hierarchyRank = preRank;
                                    tp.sharedChildrenCount = preSharedChildren;
                                    tp.playerCheatedOnPartner = preRevenge;
                                    tp.isOpenRelationshipLover = false;
                                    tp.finalChance = pSuggestOpen;
                                    tp.rolledValue = eRoll;
                                    tp.driversSummary = "desire for open relationship freedom or polygamy parity";
                                    tp.locCtx = ResolveActorLocationContext(partnerID);
                                    agencyLLMTasks.push_back(std::move(tp));
                                }
                                continue;
                            }
                        } else {
                            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionE1] '{}' (0x{:08X}) SuggestOpen skipped: baseTemp=0.0 for temperament={} (daysSince={:.1f}d vs contentNeglect={:.1f}d)",
                                           partnerName, partnerID, Utils::TemperamentToString(preTemp), preDaysSince, settings.GetNeglectContentStartDays());
                        }
                    } else {
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionE1] '{}' (0x{:08X}) SuggestOpen skipped: secretLoversCount={} (must be 0), affection={}/100 (must be >= 35)",
                                       partnerName, partnerID, secretLoversCount, preAffection);
                    }
                } else {
                    float baseTempE2 = 0.0f;
                    if (preTemp == Temperament::Jealous) baseTempE2 = 18.0f;
                    else if (preTemp == Temperament::Romantic) baseTempE2 = (preAffection >= 75) ? 12.0f : 4.0f;
                    else if (preTemp == Temperament::Humble) baseTempE2 = (preAffection >= 75) ? 8.0f : 2.0f;
                    else if (preTemp == Temperament::Proud) baseTempE2 = 4.0f;

                    if (baseTempE2 > 0.0f) {
                        float bonusNoSideE2 = (secretLoversCount == 0) ? 8.0f : 0.0f;
                        float bonusChildrenE2 = (preSharedChildren > 0) ? 6.0f : 0.0f;
                        float pSuggestClose = std::clamp(baseTempE2 + bonusNoSideE2 + bonusChildrenE2, 0.0f, 30.0f);
                        float eRoll = pctDist(gen);
                        bool suggestPassed = (eRoll < pSuggestClose);
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionE2] '{}' (0x{:08X}) SuggestClose roll | BaseTemp={:.1f} NoSideBonus={:+.1f} ChildBonus={:+.1f} -> Chance={:.1f}% (Roll={:.1f}) => {}",
                                       partnerName, partnerID, baseTempE2, bonusNoSideE2, bonusChildrenE2, pSuggestClose, eRoll,
                                       suggestPassed ? (useLLM ? "SUGGEST_CLOSE CANDIDATE (Sending to LLM)" : "QUEUED SuggestClose") : "NO INTENT");
                        if (suggestPassed) {
                            if (!useLLM) {
                                mgr.SetPartnerPendingIntent(partnerID, PartnerIntentType::SuggestClose, 0);
                            } else {
                                TemptedPlayerPartner tp;
                                tp.evalMode = "suggest_close";
                                tp.partnerID = partnerID;
                                tp.partnerName = partnerName;
                                tp.statusWithPlayer = preStatus;
                                tp.affection = preAffection;
                                tp.affectionTier = GetAffectionTierLabel(preAffection);
                                tp.daysSinceInteraction = preDaysSince;
                                tp.hierarchyRank = preRank;
                                tp.sharedChildrenCount = preSharedChildren;
                                tp.playerCheatedOnPartner = false;
                                tp.isOpenRelationshipLover = true;
                                tp.finalChance = pSuggestClose;
                                tp.rolledValue = eRoll;
                                tp.driversSummary = "desire for exclusive commitment with the player";
                                tp.locCtx = ResolveActorLocationContext(partnerID);
                                agencyLLMTasks.push_back(std::move(tp));
                            }
                            continue;
                        }
                    } else {
                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionE2] '{}' (0x{:08X}) SuggestClose skipped: baseTemp=0.0 for temperament={}",
                                       partnerName, partnerID, Utils::TemperamentToString(preTemp));
                    }
                }
            }

            float multExistingLovers = (secretLoversCount == 0) ? 1.00f
                                       : (secretLoversCount == 1 ? 0.75f
                                       : (secretLoversCount == 2 ? 0.50f : 0.30f));

            // Hard Immunity 6: Pregnant with Player's Child
            if (settings.playerPartnerCheatingEnableChildrenAnchor && mgr.IsNPCPregnant(partnerID)) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Skipping '{}' (0x{:08X}): currently pregnant (hard immunity)",
                               partnerName, partnerID);
                continue;
            }

            // Action D: New Open-Relationship Lover Roll (when isOpenWithPlayer == true)
            if (isOpenWithPlayer && !settings.playerPartnerOpenRelAgencyEnabled) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionD] Skipping open partner '{}' (0x{:08X}): playerPartnerOpenRelAgencyEnabled is disabled in settings.", partnerName, partnerID);
                continue;
            }
            if (isOpenWithPlayer) {
                float multOpenSetting = std::clamp(settings.playerPartnerAgencyOpenLoverChanceMultiplier, 0.0f, 3.0f);
                if (multOpenSetting <= 0.0f) {
                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionD] Skipping open partner '{}' (0x{:08X}): playerPartnerAgencyOpenLoverChanceMultiplier is {:.2f}",
                                   partnerName, partnerID, multOpenSetting);
                    continue;
                }
                float baseTempD = 8.0f;
                switch (preTemp) {
                    case Temperament::Independent: baseTempD = 22.0f; break;
                    case Temperament::Romantic: baseTempD = 16.0f; break;
                    case Temperament::Proud: baseTempD = 14.0f; break;
                    case Temperament::Humble: baseTempD = 8.0f; break;
                    case Temperament::Jealous: baseTempD = 5.0f; break;
                }
                float bonusLonelinessD = (preDaysSince > settings.GetLonelinessDecayStartDays())
                                             ? std::min(18.0f, (preDaysSince - settings.GetLonelinessDecayStartDays()) * 1.5f)
                                             : 0.0f;
                float bonusPolyParityD = (totalPlayerMainPartners > 1) ? 8.0f : 0.0f;
                float mitChildrenD = (settings.playerPartnerCheatingEnableChildrenAnchor && preSharedChildren > 0) ? 8.0f : 0.0f;
                float mitRecentVisitD = (preDaysSince <= settings.GetOpenRecentVisitMitigationMaxDays()) ? 8.0f : 0.0f;

                float additiveOpen = baseTempD + bonusLonelinessD + bonusPolyParityD - mitChildrenD - mitRecentVisitD;
                float finalOpenChance = std::clamp(additiveOpen * multExistingLovers * multOpenSetting, 0.0f, 65.0f);
                float openRoll = pctDist(gen);
                bool openPassed = (finalOpenChance > 0.0f && openRoll < finalOpenChance);

                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionD] Open Partner '{}' (0x{:08X}) | Affection={} | DaysSince={:.1f} | BaseTemp={:.1f} Lonely={:+.1f} Poly={:+.1f} ChildMit=-{:.1f} RecentMit=-{:.1f} | ExistingLoversMult={:.2f}x OpenMult={:.2f}x -> Final={:.1f}% (Roll={:.1f}) => {}",
                               partnerName, partnerID, preAffection, preDaysSince, baseTempD, bonusLonelinessD, bonusPolyParityD,
                               mitChildrenD, mitRecentVisitD, multExistingLovers, multOpenSetting, finalOpenChance, openRoll,
                               openPassed ? "SEEKING OPEN LOVER (Proceeding to Stage 2)" : "NO NEW LOVER");

                if (openPassed) {
                    TemptedPlayerPartner tp;
                    tp.partnerID = partnerID;
                    tp.partnerName = partnerName;
                    tp.statusWithPlayer = preStatus;
                    tp.affection = preAffection;
                    tp.affectionTier = GetAffectionTierLabel(preAffection);
                    tp.daysSinceInteraction = preDaysSince;
                    tp.hierarchyRank = preRank;
                    tp.sharedChildrenCount = preSharedChildren;
                    tp.playerCheatedOnPartner = false;
                    tp.isOpenRelationshipLover = true;
                    tp.finalChance = finalOpenChance;
                    tp.rolledValue = openRoll;
                    tp.driversSummary = "open relationship freedom and companionship";
                    tp.locCtx = ResolveActorLocationContext(partnerID);
                    temptedPartners.push_back(std::move(tp));
                }
                continue;
            }

            if (!settings.playerPartnerCheatingAgencyEnabled) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionC] Skipping closed partner '{}' (0x{:08X}): playerPartnerCheatingAgencyEnabled is disabled in settings.", partnerName, partnerID);
                continue;
            }

            if (forceCheatingFromExpiredSuggestOpen) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionC] Partner '{}' (0x{:08X}) bypassing Stage 1 roll -> 100% forced Talking affair selection due to expired SuggestOpen intent.",
                               partnerName, partnerID);
                TemptedPlayerPartner tp;
                tp.partnerID = partnerID;
                tp.partnerName = partnerName;
                tp.statusWithPlayer = preStatus;
                tp.affection = preAffection;
                tp.affectionTier = GetAffectionTierLabel(preAffection);
                tp.daysSinceInteraction = preDaysSince;
                tp.hierarchyRank = preRank;
                tp.sharedChildrenCount = preSharedChildren;
                tp.playerCheatedOnPartner = preRevenge;
                tp.isOpenRelationshipLover = false;
                tp.finalChance = 100.0f;
                tp.rolledValue = 0.0f;
                tp.driversSummary = "unaddressed desire to open the relationship after being ignored by the player";
                tp.locCtx = ResolveActorLocationContext(partnerID);
                temptedPartners.push_back(std::move(tp));
                continue;
            }

            // Check if Player has an ongoing, exposed (!isSecret) affair against this partner (Revenge trigger)
            bool revengeCheating = false;
            std::string playerAffairPartnerName;
            if (settings.playerPartnerCheatingEnableRevenge) {
                for (const auto& rec : mgr.GetCheatingRecords(partnerID, kPlayerFormID)) {
                    if (rec.cheaterID == kPlayerFormID && rec.victimID == partnerID &&
                        rec.affairStatus == AffairStatus::Ongoing && !rec.isSecret) {
                        revengeCheating = true;
                        if (rec.affairPartnerID != 0) {
                            playerAffairPartnerName = ActorSvc::GetName(rec.affairPartnerID);
                        }
                        break;
                    }
                }
            }

            int affection = std::clamp(affSvc.GetPermanentAffection(partnerID), 0, 100);
            float daysSince = std::max(0.0f, affSvc.GetDaysSinceLastAffection(partnerID));

            // Hard Immunity 7: Happy Tier (Affection >= 75) when !playerPartnerCheatingAllowWhenHappy and no Revenge
            if (affection >= 75 && !settings.playerPartnerCheatingAllowWhenHappy && !revengeCheating) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionC] '{}' (0x{:08X}) is Happy (affection={}/100, allowWhenHappy=false) and no revenge active -> 0% temptation (loyal)",
                               partnerName, partnerID, affection);
                continue;
            }

            // Hard Immunity 8: Content (50-74) & Not Neglected (daysSince <= contentNeglectStart) and no Revenge
            if (affection >= 50 && affection < 75 && (!settings.playerPartnerCheatingEnableNeglect || daysSince <= settings.GetNeglectContentStartDays()) && !revengeCheating) {
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionC] '{}' (0x{:08X}) is Content (affection={}/100, daysSince={:.1f}d <= neglectThreshold={:.1f}d) and no revenge -> 0% temptation (loyal)",
                               partnerName, partnerID, affection, daysSince, settings.GetNeglectContentStartDays());
                continue;
            }

            RelationshipStatus status = mgr.GetRelationshipStatus(partnerID, kPlayerFormID);
            Temperament temp = mgr.GetTemperament(partnerID);
            std::vector<std::string> drivers;

            // 1. Base_Affection
            float baseAff = 0.0f;
            if (affection >= 75) {
                baseAff = settings.playerPartnerCheatingAllowWhenHappy ? 2.0f : 0.0f;
                if (baseAff > 0.0f) {
                    drivers.push_back(std::format("Happy affection baseline ({}/100, +2%)", affection));
                }
            } else if (affection >= 50) {
                baseAff = 0.0f; // Content has 0% base; driven by Neglect or Revenge
            } else if (affection >= 25) {
                // Troubled (25-49): Linearly scales from 5% (at 49) to 18% (at 25)
                baseAff = 5.0f + (49.0f - static_cast<float>(affection)) * (13.0f / 24.0f);
                drivers.push_back(std::format("Troubled affection ({}/100, +{:.1f}%)", affection, baseAff));
            } else {
                // Estranged (0-24): Linearly scales from 20% (at 24) to 40% (at 0)
                baseAff = 20.0f + (24.0f - static_cast<float>(affection)) * (20.0f / 24.0f);
                drivers.push_back(std::format("Estranged affection ({}/100, +{:.1f}%)", affection, baseAff));
            }

            // 2. Bonus_Neglect & Mitigation_RecentEffort
            float bonusNeglect = 0.0f;
            float mitigationRecentEffort = 0.0f;
            if (affection < 50 && daysSince <= settings.GetRecentEffortMaxDays()) {
                mitigationRecentEffort = 5.0f;
                drivers.push_back("recent player effort mitigation (-5%)");
            } else if (settings.playerPartnerCheatingEnableNeglect) {
                if (affection < 50 && daysSince > settings.GetNeglectTroubledStartDays()) {
                    bonusNeglect = std::min(20.0f, (daysSince - settings.GetNeglectTroubledStartDays()) * 1.5f);
                    drivers.push_back(std::format("neglected for {:.0f} days (+{:.1f}%)", daysSince, bonusNeglect));
                } else if (affection >= 50 && affection < 75 && daysSince > settings.GetNeglectContentStartDays()) {
                    bonusNeglect = std::min(15.0f, (daysSince - settings.GetNeglectContentStartDays()) * 1.5f);
                    drivers.push_back(std::format("Content partner neglected for {:.0f} days (+{:.1f}%)", daysSince, bonusNeglect));
                }
            }

            // 3. Bonus_Revenge
            float bonusRevenge = 0.0f;
            if (revengeCheating) {
                bonusRevenge = 20.0f;
                drivers.push_back("revenge for player's exposed ongoing affair (+20%)");
            }

            // 4. Bonus_Hierarchy
            float bonusHierarchy = 0.0f;
            int rank = (status == RelationshipStatus::Married) ? hierMgr.GetRank(partnerID) : -1;
            if (settings.playerPartnerCheatingEnableHierarchy && settings.GetEnablePolygamy() &&
                status == RelationshipStatus::Married && totalPlayerMarriedSpouses > 1) {
                if (rank == 0) {
                    bonusHierarchy = 0.0f;
                } else if (rank == 1) {
                    bonusHierarchy = 3.0f;
                    drivers.push_back("Rank 2 spouse in polygamy hierarchy (+3%)");
                } else if (rank == 2) {
                    bonusHierarchy = 5.0f;
                    drivers.push_back("Rank 3 spouse in polygamy hierarchy (+5%)");
                } else if (rank >= 3) {
                    bonusHierarchy = 7.0f;
                    drivers.push_back("Rank 4+ spouse in polygamy hierarchy (+7%)");
                }
            }

            // 5. Mitigation_Children
            float mitigationChildren = 0.0f;
            size_t sharedChildrenCount = mgr.GetChildren(kPlayerFormID, partnerID).size();
            if (settings.playerPartnerCheatingEnableChildrenAnchor && sharedChildrenCount > 0) {
                mitigationChildren = 12.0f;
                drivers.push_back(std::format("{} shared child(ren) with player (-12%)", sharedChildrenCount));
            }

            // 6. Mult_Temperament
            float multTemperament = 1.0f;
            switch (temp) {
                case Temperament::Humble:
                    multTemperament = 0.50f;
                    drivers.push_back("Humble temperament (0.50x)");
                    break;
                case Temperament::Romantic:
                    if (daysSince > settings.GetNeglectContentStartDays()) {
                        multTemperament = 1.30f;
                        drivers.push_back("Romantic temperament neglected (1.30x)");
                    } else {
                        multTemperament = 0.75f;
                        drivers.push_back("Romantic temperament attended (0.75x)");
                    }
                    break;
                case Temperament::Independent:
                    multTemperament = 1.20f;
                    drivers.push_back("Independent temperament (1.20x)");
                    break;
                case Temperament::Proud:
                    if (rank >= 1 || revengeCheating) {
                        multTemperament = 1.35f;
                        drivers.push_back("Proud temperament subordinated/betrayed (1.35x)");
                    } else {
                        multTemperament = 1.00f;
                    }
                    break;
                case Temperament::Jealous:
                    if (revengeCheating || totalPlayerMainPartners > 1) {
                        multTemperament = 1.75f;
                        drivers.push_back("Jealous temperament provoked by rivals/affair (1.75x)");
                    } else {
                        multTemperament = 0.70f;
                        drivers.push_back("Jealous temperament in exclusive pair (0.70x)");
                    }
                    break;
            }

            // 7. Mult_Stage
            float multStage = 1.0f;
            if (status == RelationshipStatus::Dating) {
                multStage = 1.25f;
            } else if (status == RelationshipStatus::Engaged) {
                multStage = 1.00f;
            } else if (status == RelationshipStatus::Married) {
                multStage = 0.80f;
            }

            // 8. Mult_Setting & Final Clamp
            float multSetting = std::clamp(settings.playerPartnerCheatingChanceMultiplier, 0.1f, 3.0f);
            float maxCap = static_cast<float>(std::clamp(settings.playerPartnerCheatingMaxRollCap, 5, 100));
            float additiveCore = baseAff + bonusNeglect + bonusRevenge + bonusHierarchy - mitigationChildren - mitigationRecentEffort;
            float finalChance = std::clamp(additiveCore * multTemperament * multStage * multExistingLovers * multSetting, 0.0f, maxCap);
            float roll = pctDist(gen);
            bool passed = (finalChance > 0.0f && roll < finalChance);

            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:ActionC] Partner '{}' (0x{:08X}) | Affection={} ({}) | DaysSince={:.1f} | Base={:.1f} Neglect={:+.1f} Revenge={:+.1f} Hier={:+.1f} ChildMit=-{:.1f} RecentMit=-{:.1f} | TempMult={:.2f}x StageMult={:.2f}x ExistingMult={:.2f}x SettingMult={:.2f}x -> Final={:.1f}% (Roll={:.1f}) => {}",
                           partnerName, partnerID, affection, GetAffectionTierLabel(affection), daysSince,
                           baseAff, bonusNeglect, bonusRevenge, bonusHierarchy, mitigationChildren, mitigationRecentEffort,
                           multTemperament, multStage, multExistingLovers, multSetting, finalChance, roll,
                           passed ? "TEMPTED (Proceeding to Stage 2)" : "LOYAL");

            if (passed) {
                TemptedPlayerPartner tp;
                tp.partnerID = partnerID;
                tp.partnerName = partnerName;
                tp.statusWithPlayer = status;
                tp.affection = affection;
                tp.affectionTier = GetAffectionTierLabel(affection);
                tp.daysSinceInteraction = daysSince;
                tp.hierarchyRank = rank;
                tp.sharedChildrenCount = sharedChildrenCount;
                tp.playerCheatedOnPartner = revengeCheating;
                tp.playerAffairPartnerName = playerAffairPartnerName;
                tp.finalChance = finalChance;
                tp.rolledValue = roll;
                tp.driversSummary = drivers.empty() ? "emotional distance from player" : JoinStrings(drivers, "; ");
                tp.locCtx = ResolveActorLocationContext(partnerID);
                temptedPartners.push_back(std::move(tp));
            }
        }

        if (temptedPartners.empty() && agencyLLMTasks.empty()) {
            MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Stage 1 complete: no new side-lover searches or LLM agency tasks queued ({} direct breakup(s) queued).",
                           agencyBreakupMatches.size());
            finalizeInfidelityMatches(std::move(agencyBreakupMatches));
            MARAS_LOG_INFO("================================================================================");
            return;
        }

        // â”€â”€ STAGE 2: 5-Tier Candidate Pool Scoring & Quota-Balanced Top-8 Shortlist â”€â”€
        const auto allEntries = idx.GetAllEntries(NPCFilter::AdultOnly);
        std::vector<TemptedPlayerPartner> readyForResolution;

        for (auto& tp : temptedPartners) {
            int partnerSex = ActorSvc::GetSex(tp.partnerID);
            SocialClass partnerSC = mgr.GetSocialClass(tp.partnerID);
            Temperament partnerTemp = mgr.GetTemperament(tp.partnerID);

            std::vector<ScoredAffairCandidate> tierBuckets[5]; // Index 0..4 for Tiers 1..5
            std::vector<ScoredAffairCandidate> fallbackPool;

            for (const auto& entry : allEntries) {
                RE::FormID candID = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
                if (candID == 0 || candID == kPlayerFormID || candID == tp.partnerID) continue;
                if (entry.nameDisplay.empty() || entry.nameDisplay == "None") continue;
                if (entry.isChild) continue;
                if (deceasedReg.IsDeceased(candID)) continue;

                RE::Actor* candActor = idx.GetActorFromFormId(candID);
                if (candActor && (candActor->IsDead() || candActor->IsDeleted() || candActor->IsDisabled())) continue;
                if (candActor && Utils::IsPlayerFollower(candActor)) continue;
                if (IsNpcOnCooldown(candID, currentGameDay)) continue;
                if (mgr.AreFamilyMembers(tp.partnerID, candID)) continue;
                if (!IsCandidateEligibleForTarget(partnerSex, entry.sex, settings.matchmakingOrientation)) continue;

                // Gatekeeper #8: Co-spouses (other active main partners of the Player) never count as cheating with each other
                if (!tp.isOpenRelationshipLover && mgr.IsPlayerActiveMainPartner(candID)) {
                    continue;
                }

                std::string canFormReason;
                if (!mgr.CanFormRelationship(tp.partnerID, candID, RelationshipStatus::Lover, &canFormReason)) {
                    continue;
                }

                ActorLocationContext candLoc = ResolveActorLocationContext(candID);
                // Anti-Yo-Yo Rules:
                // 1) AffairStatus::EndedConfronted -> permanently barred (hard exclude)
                // 2) Past side-fling breakups during (Partner, Player) relationship -> never Tier 1 (100) + -40 score penalty per breakup
                bool endedConfronted = false;
                int pastSideBreakups = 0;
                for (const auto& rec : mgr.GetCheatingRecordsForActor(tp.partnerID)) {
                    if ((rec.cheaterID == tp.partnerID && rec.affairPartnerID == candID) ||
                        (rec.cheaterID == candID && rec.affairPartnerID == tp.partnerID)) {
                        if (rec.affairStatus == AffairStatus::EndedConfronted) {
                            endedConfronted = true;
                            break;
                        }
                        if (rec.affairStatus == AffairStatus::EndedBreakup) {
                            pastSideBreakups++;
                        }
                    }
                }
                if (endedConfronted) {
                    continue;
                }

                const auto* playerRelData = mgr.GetNPCData(tp.partnerID, kPlayerFormID);
                float playerRelStartDay = (getLatestRelStartDay(playerRelData) >= 0.0f)
                                              ? getLatestRelStartDay(playerRelData)
                                              : 0.0f;
                const auto* candRelData = mgr.GetNPCData(tp.partnerID, candID);
                int histSideBreakups = 0;
                if (candRelData) {
                    for (const auto& hist : candRelData->statusHistory) {
                        if (NPCRelationshipManager::IsBreakupStatus(hist.toStatus) && hist.timestamp >= playerRelStartDay) {
                            histSideBreakups++;
                        }
                    }
                }
                pastSideBreakups = std::max(pastSideBreakups, histSideBreakups);

                bool isOldFlame = HasPastMarasRelationship(mgr, tp.partnerID, candID) && (pastSideBreakups == 0);
                std::string sharedFaction = GetSharedWorkOrGuildFactionName(
                    tp.partnerID, candID, tp.locCtx.crimeFactionID, candLoc.crimeFactionID);

                bool sharesActiveCell = false;
                for (RE::FormID cCell : candLoc.activeCells) {
                    if (cCell != 0 && tp.locCtx.activeCells.contains(cCell)) {
                        sharesActiveCell = true;
                        break;
                    }
                }
                bool samePlayerHouse = (tp.locCtx.playerHouseID != 0 && tp.locCtx.playerHouseID == candLoc.playerHouseID);
                bool sameSubLocation = (tp.locCtx.subLocationID != 0 && tp.locCtx.subLocationID == candLoc.subLocationID);
                bool sameSettlement = (tp.locCtx.settlementLocationID != 0 && tp.locCtx.settlementLocationID == candLoc.settlementLocationID);
                bool sameHold = (tp.locCtx.holdLocationID != 0 && tp.locCtx.holdLocationID == candLoc.holdLocationID) ||
                                (tp.locCtx.crimeFactionID != 0 && tp.locCtx.crimeFactionID == candLoc.crimeFactionID);

                int tier = 0;
                int baseScore = 0;
                std::string connectionTag;

                // Tier 1 (100): Old Flames (MARAS past relationship history â€” no location/hold requirement)
                if (isOldFlame) {
                    tier = 1;
                    baseScore = 100;
                    connectionTag = "[Ex-Partner / Former Flame]";
                }
                // Tier 2 (85): Under the Same Roof / Same Hangout Cell
                else if (samePlayerHouse || sharesActiveCell ||
                         (sameSubLocation && tp.locCtx.subLocationID != tp.locCtx.settlementLocationID)) {
                    tier = 2;
                    baseScore = 85;
                    std::string roofName = !tp.locCtx.subLocationName.empty()
                                               ? tp.locCtx.subLocationName
                                               : (!tp.locCtx.activeCellName.empty() ? tp.locCtx.activeCellName : tp.locCtx.locationName);
                    connectionTag = std::format("[Currently in the same building/household ({})]",
                                                roofName.empty() ? "Shared Household" : roofName);
                }
                // Tier 3 (70): Guildmates & Same Hangout Hub
                else if (sameSubLocation || (!sharedFaction.empty() && (sameSettlement || sameHold))) {
                    tier = 3;
                    baseScore = 70;
                    std::string hubOrFac = !sharedFaction.empty()
                                               ? sharedFaction
                                               : (!tp.locCtx.subLocationName.empty() ? tp.locCtx.subLocationName : tp.locCtx.locationName);
                    connectionTag = std::format("[Close Friend / Fellow {} member]",
                                                hubOrFac.empty() ? "Circle" : hubOrFac);
                }
                // Tier 4 (50): Same City / Town / Settlement
                else if (sameSettlement) {
                    tier = 4;
                    baseScore = 50;
                    connectionTag = std::format("[Neighbor in {}]",
                                                !tp.locCtx.settlementName.empty() ? tp.locCtx.settlementName : "the same town");
                }
                // Tier 5 (25): Same Hold (Homestead / Regional Fallback)
                else if (sameHold) {
                    tier = 5;
                    baseScore = 25;
                    std::string regionName = !tp.locCtx.holdName.empty()
                                                 ? tp.locCtx.holdName
                                                 : (!tp.locCtx.locationName.empty() ? tp.locCtx.locationName : "the same Hold");
                    connectionTag = std::format("[In the same region ({})]", regionName);
                }

                // Secondary Tie-Breaking Modifiers (-10 to +20 points within each Tier)
                int tieBreaker = -40 * pastSideBreakups;
                bool candCanFormMain = mgr.GetPartners(candID, RelationshipStatus::Dating).empty() &&
                                       mgr.GetPartners(candID, RelationshipStatus::Engaged).empty() &&
                                       mgr.GetPartners(candID, RelationshipStatus::Married).empty();
                bool candHasOpenMain = mgr.HasAnyOpenMainPair(candID);
                if (candCanFormMain || candHasOpenMain) {
                    tieBreaker += 10;
                } else {
                    tieBreaker -= 10;
                }

                SocialClass candSC = mgr.GetSocialClass(candID);
                Temperament candTemp = mgr.GetTemperament(candID);
                int scDiff = std::abs(static_cast<int>(partnerSC) - static_cast<int>(candSC));
                if (scDiff <= 1 ||
                    (partnerTemp == Temperament::Proud &&
                     (candSC == SocialClass::Wealthy || candSC == SocialClass::Nobles || candSC == SocialClass::Rulers))) {
                    tieBreaker += 5;
                }

                int jitter = jitterDist(gen);

                ScoredAffairCandidate sc;
                sc.formID = candID;
                sc.uuid = GetActorUUID(candID);
                sc.name = entry.nameDisplay;
                sc.race = entry.raceName.empty() ? "Nord" : entry.raceName;
                sc.sex = (entry.sex == 1) ? "female" : "male";
                sc.socialClass = Utils::SocialClassToString(candSC);
                sc.temperament = Utils::TemperamentToString(candTemp);
                sc.eligibility = GetEligibilityString(candCanFormMain, candHasOpenMain);
                sc.locationName = !candLoc.locationName.empty() ? candLoc.locationName : "Skyrim";
                sc.tier = (tier > 0) ? tier : 5;
                sc.score = std::max(1, baseScore + tieBreaker);
                sc.jitteredScore = sc.score + jitter;
                sc.connectionTag = !connectionTag.empty() ? connectionTag : std::format("[Resident of {}]", sc.locationName);
                sc.reasonsSummary = sc.connectionTag;

                if (tier >= 1 && tier <= 5) {
                    tierBuckets[tier - 1].push_back(std::move(sc));
                } else if (!candLoc.locationName.empty()) {
                    fallbackPool.push_back(std::move(sc));
                }
            }

            auto sortBucket = [](std::vector<ScoredAffairCandidate>& b) {
                std::sort(b.begin(), b.end(), [](const ScoredAffairCandidate& a, const ScoredAffairCandidate& c) {
                    if (a.jitteredScore != c.jitteredScore) return a.jitteredScore > c.jitteredScore;
                    return a.formID < c.formID;
                });
            };
            for (auto& b : tierBuckets) {
                sortBucket(b);
            }
            sortBucket(fallbackPool);

            // Quota-Balanced Shortlist Assembly (Top 8):
            // Up to 2 from Tier 1, up to 3 from Tier 2, up to 2 from Tier 3, then fill up to 8 from Tier 4, then Tier 5
            std::vector<ScoredAffairCandidate> overflow;
            auto takeFromTier = [&](std::vector<ScoredAffairCandidate>& src, size_t cap) {
                for (size_t i = 0; i < src.size(); ++i) {
                    if (i < cap && tp.shortlist.size() < 8) {
                        tp.shortlist.push_back(src[i]);
                    } else {
                        overflow.push_back(src[i]);
                    }
                }
            };
            takeFromTier(tierBuckets[0], 2); // Tier 1: max 2
            takeFromTier(tierBuckets[1], 3); // Tier 2: max 3
            takeFromTier(tierBuckets[2], 2); // Tier 3: max 2
            takeFromTier(tierBuckets[3], 8); // Tier 4: fill up to 8
            takeFromTier(tierBuckets[4], 8); // Tier 5: fill up to 8

            if (tp.shortlist.size() < 8 && !overflow.empty()) {
                sortBucket(overflow);
                for (const auto& extra : overflow) {
                    if (tp.shortlist.size() >= 8) break;
                    tp.shortlist.push_back(extra);
                }
            }
            // Fallback if total connected candidates across all tiers < 4
            if (tp.shortlist.size() < 4 && !fallbackPool.empty()) {
                for (const auto& fb : fallbackPool) {
                    if (tp.shortlist.size() >= 6) break;
                    tp.shortlist.push_back(fb);
                }
            }

            MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Stage2] Candidate Pool for '{}' (0x{:08X}, habitat='{}', subLoc='{}', settlement='{}', hold='{}'): {} shortlisted (T1={}, T2={}, T3={}, T4={}, T5={}, fallback={})",
                           tp.partnerName, tp.partnerID,
                           tp.locCtx.locationName.empty() ? "Unresolved" : tp.locCtx.locationName,
                           tp.locCtx.subLocationName, tp.locCtx.settlementName, tp.locCtx.holdName,
                           tp.shortlist.size(),
                           tierBuckets[0].size(), tierBuckets[1].size(), tierBuckets[2].size(), tierBuckets[3].size(), tierBuckets[4].size(),
                           fallbackPool.size());
            for (size_t si = 0; si < tp.shortlist.size(); ++si) {
                const auto& sc = tp.shortlist[si];
                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:Stage2]   Shortlist #{}: '{}' (0x{:08X}) | Tier={} | Score={} (Jittered={}) | {} | Loc='{}'",
                               si + 1, sc.name, sc.formID, sc.tier, sc.score, sc.jitteredScore, sc.connectionTag, sc.locationName);
            }

            if (!tp.shortlist.empty()) {
                readyForResolution.push_back(std::move(tp));
                if (readyForResolution.size() >= 3) {
                    break;
                }
            }
        }

        if (readyForResolution.empty() && agencyLLMTasks.empty()) {
            MARAS_LOG_INFO("[Matchmaking:PartnerAgency] Stage 2 complete: no valid candidates found in habitat/social reach and no LLM tasks queued.");
            finalizeInfidelityMatches(std::move(agencyBreakupMatches));
            MARAS_LOG_INFO("================================================================================");
            return;
        }

        if (!useLLM) {
            // Dice-Roll weighted selection from Top-8 shortlist proportional to composite score
            std::unordered_set<RE::FormID> usedCandidates;
            std::vector<MatchmakingResult> diceMatches = std::move(agencyBreakupMatches);
            for (const auto& tp : readyForResolution) {
                std::vector<ScoredAffairCandidate> avail;
                int totalWeight = 0;
                for (const auto& c : tp.shortlist) {
                    if (!usedCandidates.contains(c.formID)) {
                        avail.push_back(c);
                        totalWeight += std::max(1, c.score);
                    }
                }
                if (avail.empty() || totalWeight <= 0) {
                    MARAS_LOG_WARN("[Matchmaking:PartnerAgency:DiceRoll] No available unused candidates in shortlist for '{}' (0x{:08X})",
                                   tp.partnerName, tp.partnerID);
                    continue;
                }

                std::uniform_int_distribution<int> wDist(0, totalWeight - 1);
                int wRoll = wDist(gen);
                int accum = 0;
                const ScoredAffairCandidate* chosen = &avail.front();
                for (const auto& c : avail) {
                    accum += std::max(1, c.score);
                    if (wRoll < accum) {
                        chosen = &c;
                        break;
                    }
                }

                MatchmakingResult r;
                r.actorAFormID = tp.partnerID;
                r.actorBFormID = chosen->formID;
                r.actorAName = tp.partnerName;
                r.actorBName = chosen->name;
                r.action = "new";
                r.targetStatus = "lover";
                r.isCheating = !tp.isOpenRelationshipLover;
                r.affairStage = tp.isOpenRelationshipLover ? AffairStage::Physical : AffairStage::Talking;
                r.memory = tp.isOpenRelationshipLover
                               ? std::format("{} formed an open-relationship lover bond with {} ({}) driven by {}.",
                                             tp.partnerName, chosen->name, chosen->connectionTag, tp.driversSummary)
                               : std::format("{} began a secret talking-stage affair with {} ({}) driven by {}.",
                                             tp.partnerName, chosen->name, chosen->connectionTag, tp.driversSummary);

                if (ValidateCandidate(r)) {
                    usedCandidates.insert(chosen->formID);
                    diceMatches.push_back(std::move(r));
                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:DiceRoll] Selected {} '{}' (0x{:08X}, tier={}, score={}, roll={}/{}) for '{}' (0x{:08X})",
                                   tp.isOpenRelationshipLover ? "open-relationship lover" : "secret talking-stage lover",
                                   chosen->name, chosen->formID, chosen->tier, chosen->score, wRoll, totalWeight, tp.partnerName, tp.partnerID);
                } else {
                    MARAS_LOG_WARN("[Matchmaking:PartnerAgency:DiceRoll] ValidateCandidate rejected '{}' (0x{:08X}) <-> '{}' (0x{:08X}): {}",
                                   tp.partnerName, tp.partnerID, chosen->name, chosen->formID, r.skipReason);
                }
            }
            MARAS_LOG_INFO("================================================================================");
            finalizeInfidelityMatches(std::move(diceMatches));
            return;
        }

        // LLM Stage 2 Evaluation (maras_evaluations/maras_player_partner_cheating)
        if (!agencyBreakupMatches.empty()) {
            finalizeInfidelityMatches(std::move(agencyBreakupMatches));
        }
        auto batch = std::make_shared<PartnerCheatingLLMBatch>();
        batch->isScheduledScan = true;
        for (auto& tp : agencyLLMTasks) {
            PartnerCheatingLLMTask t;
            t.partner = std::move(tp);
            t.result.actorAFormID = t.partner.partnerID;
            t.result.actorAName = t.partner.partnerName;
            t.result.actorBFormID = t.partner.sideLoverID;
            t.result.actorBName = !t.partner.sideLoverName.empty() ? t.partner.sideLoverName : t.partner.evalMode;
            batch->tasks.push_back(std::move(t));
        }
        for (auto& tp : readyForResolution) {
            PartnerCheatingLLMTask t;
            t.partner = std::move(tp);
            t.result.actorAFormID = t.partner.partnerID;
            t.result.actorAName = t.partner.partnerName;
            t.result.actorBName = "Candidate Pool";
            batch->tasks.push_back(std::move(t));
        }
        batch->pendingCount = batch->tasks.size();
        auto allIndices = MakeBatchIndices(batch->tasks.size());

        auto runPartnerCheatingTasks = std::make_shared<std::function<void(std::shared_ptr<PartnerCheatingLLMBatch>, const std::vector<size_t>&)>>();
        *runPartnerCheatingTasks = [this, runPartnerCheatingTasks, finalizeInfidelityMatches](
                                       std::shared_ptr<PartnerCheatingLLMBatch> b,
                                       const std::vector<size_t>& indices) {
            if (!b || indices.empty()) return;
            auto& relMgr = NPCRelationshipManager::GetSingleton();
            float gameDay = GetCurrentGameDay();
            std::string playerName = ActorSvc::GetName(kPlayerFormID);

            for (size_t idx : indices) {
                if (idx >= b->tasks.size()) continue;
                auto& task = b->tasks[idx];
                task.completed = false;
                task.failed = false;
                task.isInvalidJson = false;
                task.failureReason.clear();
                task.willCheat = false;
                task.willBreakup = false;
                task.willConfess = false;
                task.willSuggest = false;
                task.chosenCandidateID = 0;
                task.chosenCandidateName.clear();
                task.reasoning.clear();

                const auto& tp = task.partner;
                auto pSnap = GetActorProfileSnapshot(tp.partnerID);

                std::ostringstream candStream;
                nlohmann::json candidatesArr = nlohmann::json::array();
                for (size_t ci = 0; ci < tp.shortlist.size(); ++ci) {
                    const auto& c = tp.shortlist[ci];
                    if (ci > 0) candStream << "\n";
                    candStream << std::format("{}. {} ({} {}) - {} | Location: {} | Status: [{}]",
                                              ci + 1, c.name, c.race, c.sex,
                                              c.connectionTag, c.locationName, c.eligibility);

                    nlohmann::json cObj;
                    cObj["name"] = c.name;
                    cObj["uuid"] = c.uuid;
                    cObj["race"] = c.race;
                    cObj["sex"] = c.sex;
                    cObj["socialClass"] = c.socialClass;
                    cObj["temperament"] = c.temperament;
                    cObj["location"] = c.locationName;
                    cObj["connectionTag"] = c.connectionTag;
                    cObj["eligibility"] = c.eligibility;
                    cObj["score"] = c.score;
                    candidatesArr.push_back(std::move(cObj));
                }

                std::string hierDesc = (tp.hierarchyRank >= 0)
                                           ? std::format("Rank {} Spouse", tp.hierarchyRank + 1)
                                           : "Unranked / Non-Polygamous";
                std::string relStatusLower = StringUtils::ToLowerStd(Utils::RelationshipStatusToString(tp.statusWithPlayer));
                std::string locStr = tp.locCtx.locationName.empty() ? "Skyrim" : tp.locCtx.locationName;
                int daysSinceInt = static_cast<int>(std::round(tp.daysSinceInteraction));

                nlohmann::json ctx;
                ctx["gameDay"] = static_cast<int>(gameDay);
                ctx["playerName"] = playerName;
                ctx["partnerUUID"] = pSnap.uuid;
                ctx["partnerName"] = tp.partnerName;
                ctx["partnerRace"] = pSnap.race;
                ctx["partnerSex"] = pSnap.sex;
                ctx["partnerStatusWithPlayer"] = relStatusLower;
                ctx["playerRelationshipStatus"] = relStatusLower;
                ctx["partnerTemperament"] = Utils::TemperamentToString(relMgr.GetTemperament(tp.partnerID));
                ctx["partnerSocialClass"] = Utils::SocialClassToString(relMgr.GetSocialClass(tp.partnerID));
                ctx["partnerLocation"] = locStr;
                ctx["partnerCurrentLocation"] = locStr;
                ctx["partnerAffection"] = tp.affection;
                ctx["affectionScore"] = tp.affection;
                ctx["partnerAffectionTier"] = tp.affectionTier;
                ctx["affectionTier"] = tp.affectionTier;
                ctx["daysSincePlayerInteraction"] = daysSinceInt;
                ctx["daysSinceLastVisit"] = daysSinceInt;
                const float baseL = std::max(1.0f, Settings::GetSingleton().GetLonelinessDecayStartDays());
                const float absenceRatio = tp.daysSinceInteraction / baseL;
                std::string playerPresenceHint;
                if (absenceRatio <= 0.25f) {
                    playerPresenceHint = std::format("{} spent time with {} just now; they were together very recently.", playerName, tp.partnerName);
                } else if (absenceRatio <= 0.85f) {
                    playerPresenceHint = std::format("{} spent time with {} recently and has been attentive.", playerName, tp.partnerName);
                } else if (absenceRatio <= 1.50f) {
                    playerPresenceHint = std::format("{} left recently and has been away for a little while.", playerName);
                } else if (absenceRatio <= 2.50f) {
                    playerPresenceHint = std::format("{} has been gone for a while and hasn't come back to spend time with {}.", playerName, tp.partnerName);
                } else if (absenceRatio <= 4.00f) {
                    playerPresenceHint = std::format("{} has not returned or spoken to {} for a long time.", playerName, tp.partnerName);
                } else if (absenceRatio <= 6.50f) {
                    playerPresenceHint = std::format("{} left a very long time ago and has barely been present in {}'s life.", playerName, tp.partnerName);
                } else {
                    playerPresenceHint = std::format("It feels like forever since {} last came back or spoke to {}.", playerName, tp.partnerName);
                }
                ctx["playerPresenceHint"] = playerPresenceHint;
                const float flingRatio = tp.sideLoverAgeDays / baseL;
                ctx["sideLoverDurationHint"] = (flingRatio <= 2.0f)
                                                   ? "started recently"
                                                   : ((flingRatio <= 4.5f) ? "has been going on for some time" : "has been going on for a long time");
                ctx["hierarchyRank"] = hierDesc;
                ctx["hierarchyRankDescription"] = hierDesc;
                ctx["sharedChildrenCount"] = tp.sharedChildrenCount;
                ctx["playerCheatedOnPartner"] = tp.playerCheatedOnPartner;
                ctx["playerAffairPartnerName"] = tp.playerAffairPartnerName;
                ctx["temptationChance"] = static_cast<int>(std::round(tp.finalChance));
                ctx["temptationDrivers"] = tp.driversSummary;
                ctx["isOpenRelationship"] = tp.isOpenRelationshipLover;
                ctx["evalMode"] = tp.evalMode;
                ctx["canConfess"] = tp.canConfess;
                if (tp.sideLoverID != 0) {
                    auto sSnap = GetActorProfileSnapshot(tp.sideLoverID);
                    auto sLoc = ResolveActorLocationContext(tp.sideLoverID);
                    ctx["sideLoverUUID"] = sSnap.uuid;
                    ctx["sideLoverName"] = tp.sideLoverName;
                    ctx["sideLoverRace"] = sSnap.race;
                    ctx["sideLoverSex"] = sSnap.sex;
                    ctx["sideLoverSocialClass"] = Utils::SocialClassToString(relMgr.GetSocialClass(tp.sideLoverID));
                    ctx["sideLoverTemperament"] = Utils::TemperamentToString(relMgr.GetTemperament(tp.sideLoverID));
                    ctx["sideLoverLocation"] = sLoc.locationName.empty() ? "Skyrim" : sLoc.locationName;
                    ctx["sideLoverAgeDays"] = static_cast<int>(std::round(tp.sideLoverAgeDays));
                    ctx["affairStage"] = (tp.sideAffairStage == AffairStage::Talking) ? "talking" : "physical";
                } else {
                    ctx["affairStage"] = tp.isOpenRelationshipLover ? "open_lover" : "talking";
                }
                ctx["partnerRelationships"] = BuildActorDetailedRelationshipsString(tp.partnerID);
                ctx["candidatesCount"] = tp.shortlist.size();
                ctx["candidatesList"] = candStream.str();
                ctx["candidates"] = std::move(candidatesArr);

                std::string contextJson = SafeJsonDump(ctx);
                std::string promptName = "maras_evaluations/maras_player_partner_cheating";

                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] Requesting Stage 2 evaluation for '{}' (0x{:08X}) | evalMode='{}' | isOpen={} | candidates={} | sideLover='{}' (0x{:08X})",
                               tp.partnerName, tp.partnerID, tp.evalMode, tp.isOpenRelationshipLover,
                               tp.shortlist.size(), tp.sideLoverName, tp.sideLoverID);

                SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson,
                    [this, b, idx, runPartnerCheatingTasks, finalizeInfidelityMatches](const char* response, int success) {
                        std::string respStr = (response && response[0] != '\0') ? std::string(response) : "";
                        SKSE::GetTaskInterface()->AddTask([this, b, idx, success, respStr, runPartnerCheatingTasks, finalizeInfidelityMatches]() {
                            try {
                                if (idx >= b->tasks.size()) return;
                                auto& t = b->tasks[idx];
                                if (!success || respStr.empty()) {
                                    t.completed = true;
                                    t.failed = true;
                                    t.isInvalidJson = false;
                                    t.failureReason = "LLM request failed or returned empty response";
                                    MARAS_LOG_WARN("[Matchmaking:PartnerAgency:LLM] Request failed or returned empty response for '{}' (0x{:08X}, evalMode='{}')",
                                                   t.partner.partnerName, t.partner.partnerID, t.partner.evalMode);
                                } else {
                                    nlohmann::json resp = JsonService::ParseLLMJson(respStr);
                                    if (resp.is_discarded() || !resp.is_object()) {
                                        t.completed = true;
                                        t.failed = true;
                                        t.isInvalidJson = true;
                                        t.failureReason = "Malformed / unrepairable JSON response";
                                        MARAS_LOG_WARN("[Matchmaking:PartnerAgency:LLM] Malformed JSON response for '{}' (0x{:08X}, evalMode='{}'). Raw: {}",
                                                       t.partner.partnerName, t.partner.partnerID, t.partner.evalMode, respStr);
                                    } else {
                                        t.completed = true;
                                        t.failed = false;
                                        t.willCheat = GetJsonBool(resp, "willCheat", GetJsonBool(resp, "will_cheat", false));
                                        t.willBreakup = GetJsonBool(resp, "willBreakup", GetJsonBool(resp, "will_breakup", false));
                                        t.willConfess = GetJsonBool(resp, "willConfess", GetJsonBool(resp, "will_confess", false));
                                        t.willSuggest = GetJsonBool(resp, "willSuggest", GetJsonBool(resp, "will_suggest", false));
                                        t.chosenCandidateName = GetJsonString(resp, "chosenCandidate", GetJsonString(resp, "chosen_candidate", ""));
                                        t.reasoning = GetJsonString(resp, "reason", GetJsonString(resp, "reasoning", ""));

                                        if (t.willCheat && !t.chosenCandidateName.empty()) {
                                            for (const auto& c : t.partner.shortlist) {
                                                if (StringUtils::CaseInsensitiveEquals(c.name, t.chosenCandidateName) ||
                                                    StringUtils::Contains(c.name, t.chosenCandidateName)) {
                                                    t.chosenCandidateID = c.formID;
                                                    t.chosenCandidateName = c.name;
                                                    break;
                                                }
                                            }
                                            if (t.chosenCandidateID == 0) {
                                                if (auto entry = NPCIndex::GetSingleton().FindByName(t.chosenCandidateName, NPCFilter::AdultOnly)) {
                                                    t.chosenCandidateID = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
                                                    t.chosenCandidateName = entry->nameDisplay;
                                                }
                                            }
                                        }
                                        MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] Result for '{}' (0x{:08X}, evalMode='{}'): willCheat={}, willBreakup={}, willConfess={}, willSuggest={}, chosenCandidate='{}' (0x{:08X}), reason='{}'",
                                                       t.partner.partnerName, t.partner.partnerID, t.partner.evalMode,
                                                       t.willCheat, t.willBreakup, t.willConfess, t.willSuggest,
                                                       t.chosenCandidateName, t.chosenCandidateID, t.reasoning);
                                    }
                                }

                                HandleBatchCompletion(
                                    b,
                                    "MARAS: Partner Infidelity Evaluation Failed",
                                    "gamemaster_evaluation",
                                    this,
                                    [runPartnerCheatingTasks](auto batchPtr, const auto& retryIdxs) {
                                        (*runPartnerCheatingTasks)(batchPtr, retryIdxs);
                                    },
                                    [](auto batchPtr, const auto& cancelIdxs) {
                                        for (size_t ci : cancelIdxs) {
                                            batchPtr->tasks[ci].willCheat = false;
                                            batchPtr->tasks[ci].willBreakup = false;
                                            batchPtr->tasks[ci].willConfess = false;
                                            batchPtr->tasks[ci].willSuggest = false;
                                        }
                                    },
                                    [this, finalizeInfidelityMatches](auto batchPtr) {
                                        std::unordered_set<RE::FormID> usedCandidates;
                                        std::vector<MatchmakingResult> llmMatches;
                                        for (const auto& taskItem : batchPtr->tasks) {
                                            if (!taskItem.completed || taskItem.failed) {
                                                MARAS_LOG_WARN("[Matchmaking:PartnerAgency:LLM] Skipping failed/incomplete task for '{}' (0x{:08X}, evalMode='{}')",
                                                               taskItem.partner.partnerName, taskItem.partner.partnerID, taskItem.partner.evalMode);
                                                continue;
                                            }
                                            auto& mgrRef = NPCRelationshipManager::GetSingleton();
                                            const auto& mode = taskItem.partner.evalMode;
                                            if (mode == "suggest_open" || mode == "suggest_close") {
                                                if (taskItem.willSuggest &&
                                                    Settings::GetSingleton().playerPartnerAgencyEnableForceGreets &&
                                                    mgrRef.GetPartnerPendingIntent(taskItem.partner.partnerID).type == PartnerIntentType::None) {
                                                    PartnerIntentType sType = (mode == "suggest_open")
                                                                                  ? PartnerIntentType::SuggestOpen
                                                                                  : PartnerIntentType::SuggestClose;
                                                    mgrRef.SetPartnerPendingIntent(taskItem.partner.partnerID, sType, 0);
                                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) approved intent '{}' (reason: '{}')",
                                                                   taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                                   Utils::PartnerIntentTypeToString(sType), taskItem.reasoning);
                                                } else {
                                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) did NOT queue intent for mode='{}' (willSuggest={}, reason='{}')",
                                                                   taskItem.partner.partnerName, taskItem.partner.partnerID, mode, taskItem.willSuggest, taskItem.reasoning);
                                                }
                                                continue;
                                            }
                                            if (mode == "breakup_affair") {
                                                PartnerIntentType confType = (taskItem.partner.sideAffairStage == AffairStage::Talking)
                                                                                 ? PartnerIntentType::ConfessTalking
                                                                                 : PartnerIntentType::ConfessPhysical;
                                                if (taskItem.willBreakup && taskItem.partner.sideLoverID != 0) {
                                                    MatchmakingResult br;
                                                    br.actorAFormID = taskItem.partner.partnerID;
                                                    br.actorBFormID = taskItem.partner.sideLoverID;
                                                    br.actorAName = taskItem.partner.partnerName;
                                                    br.actorBName = taskItem.partner.sideLoverName;
                                                    br.action = "breakup";
                                                    br.targetStatus = "exlover";
                                                    br.isCheating = false;
                                                    br.memory = !taskItem.reasoning.empty()
                                                                    ? taskItem.reasoning
                                                                    : std::format("{} ended their secret {} affair with {}.",
                                                                                  taskItem.partner.partnerName,
                                                                                  Utils::AffairStageToString(taskItem.partner.sideAffairStage),
                                                                                  taskItem.partner.sideLoverName);
                                                    if (taskItem.willConfess && taskItem.partner.canConfess) {
                                                        br.postBreakupIntent = confType;
                                                    }
                                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) approved affair breakup with '{}' (0x{:08X}, willConfess={})",
                                                                   taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                                   taskItem.partner.sideLoverName, taskItem.partner.sideLoverID, taskItem.willConfess);
                                                    llmMatches.push_back(std::move(br));
                                                } else if (taskItem.willConfess && taskItem.partner.canConfess &&
                                                           Settings::GetSingleton().playerPartnerAgencyEnableForceGreets &&
                                                           mgrRef.GetPartnerPendingIntent(taskItem.partner.partnerID).type == PartnerIntentType::None) {
                                                    mgrRef.SetPartnerPendingIntent(taskItem.partner.partnerID, confType, taskItem.partner.sideLoverID);
                                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) approved guilt confession intent '{}' for 0x{:08X} without breakup",
                                                                   taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                                   Utils::PartnerIntentTypeToString(confType), taskItem.partner.sideLoverID);
                                                } else {
                                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) declined affair breakup/confession for 0x{:08X} (willBreakup={}, willConfess={}, reason='{}')",
                                                                   taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                                   taskItem.partner.sideLoverID, taskItem.willBreakup, taskItem.willConfess, taskItem.reasoning);
                                                }
                                                continue;
                                            }
                                            if (mode == "breakup_open_lover") {
                                                if (taskItem.willBreakup && taskItem.partner.sideLoverID != 0) {
                                                    MatchmakingResult br;
                                                    br.actorAFormID = taskItem.partner.partnerID;
                                                    br.actorBFormID = taskItem.partner.sideLoverID;
                                                    br.actorAName = taskItem.partner.partnerName;
                                                    br.actorBName = taskItem.partner.sideLoverName;
                                                    br.action = "breakup";
                                                    br.targetStatus = "exlover";
                                                    br.isCheating = false;
                                                    br.memory = !taskItem.reasoning.empty()
                                                                    ? taskItem.reasoning
                                                                    : std::format("{} amicably ended their open-relationship fling with {}.",
                                                                                  taskItem.partner.partnerName, taskItem.partner.sideLoverName);
                                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) approved open-lover breakup with '{}' (0x{:08X})",
                                                                   taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                                   taskItem.partner.sideLoverName, taskItem.partner.sideLoverID);
                                                    llmMatches.push_back(std::move(br));
                                                } else {
                                                    MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) declined open-lover breakup with 0x{:08X} (reason='{}')",
                                                                   taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                                   taskItem.partner.sideLoverID, taskItem.reasoning);
                                                }
                                                continue;
                                            }
                                            if (!taskItem.willCheat || taskItem.chosenCandidateID == 0) {
                                                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] '{}' (0x{:08X}) did NOT form new side bond (willCheat={}, chosenCandidate='{}' [0x{:08X}], reason='{}')",
                                                               taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                               taskItem.willCheat, taskItem.chosenCandidateName, taskItem.chosenCandidateID, taskItem.reasoning);
                                                continue;
                                            }
                                            if (usedCandidates.contains(taskItem.chosenCandidateID)) {
                                                MARAS_LOG_WARN("[Matchmaking:PartnerAgency:LLM] Skipping '{}' (0x{:08X}) <-> '{}' (0x{:08X}): candidate already matched in this batch",
                                                               taskItem.partner.partnerName, taskItem.partner.partnerID,
                                                               taskItem.chosenCandidateName, taskItem.chosenCandidateID);
                                                continue;
                                            }
                                            MatchmakingResult r;
                                            r.actorAFormID = taskItem.partner.partnerID;
                                            r.actorBFormID = taskItem.chosenCandidateID;
                                            r.actorAName = taskItem.partner.partnerName;
                                            r.actorBName = taskItem.chosenCandidateName;
                                            r.action = "new";
                                            r.targetStatus = "lover";
                                            r.isCheating = !taskItem.partner.isOpenRelationshipLover;
                                            r.affairStage = taskItem.partner.isOpenRelationshipLover ? AffairStage::Physical : AffairStage::Talking;
                                            r.memory = taskItem.reasoning;
                                            if (ValidateCandidate(r)) {
                                                usedCandidates.insert(taskItem.chosenCandidateID);
                                                llmMatches.push_back(std::move(r));
                                                MARAS_LOG_INFO("[Matchmaking:PartnerAgency:LLM] Queued new {} bond: '{}' (0x{:08X}) <-> '{}' (0x{:08X}, stage={})",
                                                               r.isCheating ? "secret affair" : "open lover",
                                                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID,
                                                               Utils::AffairStageToString(r.affairStage));
                                            } else {
                                                MARAS_LOG_WARN("[Matchmaking:PartnerAgency:LLM] ValidateCandidate rejected LLM match '{}' (0x{:08X}) <-> '{}' (0x{:08X}): {}",
                                                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID, r.skipReason);
                                            }
                                        }
                                        finalizeInfidelityMatches(std::move(llmMatches));
                                    });
                            } catch (const std::exception& e) {
                                MARAS_LOG_ERROR("[Matchmaking:PartnerAgency:LLM] Exception in callback: {}", e.what());
                            }
                        });
                    });
            }
        };

        (*runPartnerCheatingTasks)(batch, allIndices);
    }

    void MatchmakingService::RunLLMScheduledMatchmaking(float currentGameDay) {
        const auto& settings = Settings::GetSingleton();
        MARAS_LOG_INFO("[Matchmaking:RunLLMScheduledMatchmaking] Triggering scheduled AI matchmaking at game day {:.2f} (last run: {:.2f}, interval: {} days)",
                       currentGameDay, lastMatchmakingDay_, settings.matchmakingIntervalDays);

        lastMatchmakingDay_ = currentGameDay;

        nlohmann::json ctx = BuildScheduledContext(currentGameDay);
        std::string contextJson = SafeJsonDump(ctx);
        std::string promptName = "maras_evaluations/maras_matchmaking_scheduled";

        SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, [this, currentGameDay](const char* response, int success) {
            if (!success || !response || response[0] == '\0') {
                MARAS_LOG_WARN("[Matchmaking:RunLLMScheduledMatchmaking] Scheduled matchmaking request failed or returned empty response");
                ShowInGameFailedLLMModal(
                    "MARAS: Matchmaking Scan Failed",
                    { "The scheduled matchmaking scan request failed or returned an empty response." },
                    [this, currentGameDay]() {
                        lastMatchmakingDay_ = -1.0f;
                        TryScheduledMatchmaking(currentGameDay);
                    },
                    []() {
                        MARAS_LOG_INFO("[Matchmaking:RunLLMScheduledMatchmaking] Scheduled matchmaking retry cancelled by player.");
                    }
                );
                return;
            }
            SKSE::GetTaskInterface()->AddTask([this, respStr = std::string(response), currentGameDay]() {
                try {
                    nlohmann::json resp = JsonService::ParseLLMJson(respStr);
                    if (resp.is_discarded()) {
                        MARAS_LOG_WARN("[Matchmaking:RunLLMScheduledMatchmaking] Scheduled matchmaking response returned malformed/unrepairable JSON");
                        ShowInGameFailedLLMModal(
                            "MARAS: Matchmaking Scan Failed",
                            { "The scheduled matchmaking scan returned malformed or unreadable JSON data." },
                            [this, currentGameDay]() {
                                lastMatchmakingDay_ = -1.0f;
                                TryScheduledMatchmaking(currentGameDay);
                            },
                            []() {
                                MARAS_LOG_INFO("[Matchmaking:RunLLMScheduledMatchmaking] Scheduled matchmaking retry cancelled by player.");
                            },
                            false,
                            true,
                            "gamemaster_evaluation"
                        );
                        return;
                    }
                    ProcessScheduledResults(respStr, true);
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("[Matchmaking:RunLLMScheduledMatchmaking] Exception processing scheduled results: {}", e.what());
                }
            });
        });
    }

    std::vector<DiceRollActionSlot> MatchmakingService::RollActionSlots(int maxSlots) {
        std::vector<DiceRollActionSlot> slots;
        if (maxSlots <= 0) return slots;

        const auto& settings = Settings::GetSingleton();

        struct ActionOption {
            std::string action;
            std::string targetStatus;
            bool isCheating;
            std::string category;
            int32_t weight;
        };

        int countNew = 0;
        int countAdvance = 0;
        int countBreakup = 0;
        int countOpenClose = 0;

        thread_local std::mt19937 gen(std::random_device{}());

        for (int slotIdx = 0; slotIdx < maxSlots; ++slotIdx) {
            std::vector<ActionOption> available;

            // Category: New (max 3)
            if (countNew < 3) {
                if (settings.diceRollChanceNewDating > 0) {
                    available.push_back({"new", "dating", false, "New", settings.diceRollChanceNewDating});
                }
                if (settings.diceRollChanceNewEngaged > 0) {
                    available.push_back({"new", "engaged", false, "New", settings.diceRollChanceNewEngaged});
                }
                if (settings.diceRollChanceNewLoverAllowed > 0) {
                    available.push_back({"new", "lover", false, "New", settings.diceRollChanceNewLoverAllowed});
                }
                if (settings.matchmakingAllowCheating && settings.diceRollChanceNewLoverCheating > 0) {
                    available.push_back({"new", "lover", true, "New", settings.diceRollChanceNewLoverCheating});
                }
            }

            // Category: Advance (max 3)
            if (countAdvance < 3 && settings.diceRollChanceAdvance > 0) {
                available.push_back({"advance", "", false, "Advance", settings.diceRollChanceAdvance});
            }

            // Category: Breakup (max 3)
            if (countBreakup < 3 && settings.matchmakingAllowBreakups && settings.diceRollChanceBreakup > 0) {
                available.push_back({"breakup", "", false, "Breakup", settings.diceRollChanceBreakup});
            }

            // Category: OpenClose (max 3)
            if (countOpenClose < 3 && settings.matchmakingAllowOpenClose && settings.diceRollChanceOpenClose > 0) {
                available.push_back({"open_or_close", "", false, "OpenClose", settings.diceRollChanceOpenClose});
            }

            if (available.empty()) {
                MARAS_LOG_INFO("[Matchmaking:DiceRoll] Slot #{}/{}: No available action options (category caps reached or weights zero)",
                               slotIdx + 1, maxSlots);
                break;
            }

            int32_t totalWeight = 0;
            for (const auto& opt : available) {
                totalWeight += opt.weight;
            }
            if (totalWeight <= 0) {
                MARAS_LOG_INFO("[Matchmaking:DiceRoll] Slot #{}/{}: Total weight is 0 across remaining options",
                               slotIdx + 1, maxSlots);
                break;
            }

            std::uniform_int_distribution<int32_t> dist(0, totalWeight - 1);
            int32_t roll = dist(gen);
            int32_t accum = 0;

            for (const auto& opt : available) {
                accum += opt.weight;
                if (roll < accum) {
                    DiceRollActionSlot chosenSlot;
                    chosenSlot.action = opt.action;
                    chosenSlot.targetStatus = opt.targetStatus;
                    chosenSlot.isCheating = opt.isCheating;
                    chosenSlot.category = opt.category;

                    if (opt.category == "New") countNew++;
                    else if (opt.category == "Advance") countAdvance++;
                    else if (opt.category == "Breakup") countBreakup++;
                    else if (opt.category == "OpenClose") countOpenClose++;

                    MARAS_LOG_INFO("[Matchmaking:DiceRoll] Slot #{}/{}: Rolled Category='{}' | Action='{}', Target='{}' (Cheating={}, Roll={}/{})",
                                   slotIdx + 1, maxSlots, opt.category, opt.action,
                                   opt.targetStatus.empty() ? "(dynamic)" : opt.targetStatus,
                                   opt.isCheating, roll, totalWeight);

                    slots.push_back(std::move(chosenSlot));
                    break;
                }
            }
        }

        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Slot rolling finished: {} slot(s) planned (Categories -> New: {}/3, Advance: {}/3, Breakup: {}/3, OpenClose: {}/3)",
                       slots.size(), countNew, countAdvance, countBreakup, countOpenClose);

        return slots;
    }

    std::optional<MatchmakingResult> MatchmakingService::FindEligiblePairForSlot(
        const DiceRollActionSlot& slot,
        const std::vector<NPCEntry>& eligibleNPCs,
        std::unordered_set<RE::FormID>& usedActors,
        float currentGameDay)
    {
        const auto& settings = Settings::GetSingleton();
        auto& mgr = NPCRelationshipManager::GetSingleton();

        thread_local std::mt19937 gen(std::random_device{}());

        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Resolving pair for Category='{}', Action='{}', Target='{}' (Cheating: {})...",
                       slot.category, slot.action, slot.targetStatus.empty() ? "(dynamic)" : slot.targetStatus, slot.isCheating);

        if (slot.action == "new") {
            RelationshipStatus targetSt = Utils::StringToRelationshipStatus(slot.targetStatus);
            if (targetSt == RelationshipStatus::Candidate || targetSt == RelationshipStatus::Unknown) {
                targetSt = RelationshipStatus::Dating;
            }

            std::vector<size_t> indices;
            indices.reserve(eligibleNPCs.size());
            for (size_t i = 0; i < eligibleNPCs.size(); ++i) {
                RE::FormID id = eligibleNPCs[i].refFormID != 0 ? eligibleNPCs[i].refFormID : eligibleNPCs[i].baseFormID;
                if (id != 0 && usedActors.find(id) == usedActors.end()) {
                    indices.push_back(i);
                }
            }

            std::shuffle(indices.begin(), indices.end(), gen);

            for (size_t i = 0; i < indices.size(); ++i) {
                const auto& entryA = eligibleNPCs[indices[i]];
                RE::FormID actorA = entryA.refFormID != 0 ? entryA.refFormID : entryA.baseFormID;
                if (usedActors.find(actorA) != usedActors.end()) continue;

                for (size_t j = i + 1; j < indices.size(); ++j) {
                    const auto& entryB = eligibleNPCs[indices[j]];
                    RE::FormID actorB = entryB.refFormID != 0 ? entryB.refFormID : entryB.baseFormID;
                    if (usedActors.find(actorB) != usedActors.end()) continue;
                    if (actorA == actorB) continue;
                    if (mgr.AreFamilyMembers(actorA, actorB)) continue;

                    if (!IsPairOrientationAllowed(entryA.sex, entryB.sex, settings.matchmakingOrientation)) {
                        continue;
                    }

                    bool isCheating = IsPairCheating(actorA, actorB, slot.targetStatus);
                    if (slot.isCheating != isCheating) {
                        continue;
                    }
                    if (isCheating && !settings.matchmakingAllowCheating) {
                        continue;
                    }

                    std::string reason;
                    if (!mgr.CanFormRelationship(actorA, actorB, targetSt, &reason)) {
                        continue;
                    }

                    MatchmakingResult r;
                    r.actorAFormID = actorA;
                    r.actorBFormID = actorB;
                    r.actorAName = entryA.nameDisplay.empty() ? ActorSvc::GetName(actorA) : entryA.nameDisplay;
                    r.actorBName = entryB.nameDisplay.empty() ? ActorSvc::GetName(actorB) : entryB.nameDisplay;
                    r.action = "new";
                    r.targetStatus = slot.targetStatus;
                    r.isCheating = isCheating;

                    if (!ValidateCandidate(r)) {
                        continue;
                    }

                    usedActors.insert(actorA);
                    usedActors.insert(actorB);

                    MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> MATCH FOUND: {} (0x{:08X}) <-> {} (0x{:08X}) | Action: 'new' -> '{}' | Cheating: {}",
                                   r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID, r.targetStatus, r.isCheating);
                    return r;
                }
            }
            MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> NO MATCH: No eligible pair found for 'new' action '{}' (Cheating: {})",
                           slot.targetStatus, slot.isCheating);
            return std::nullopt;
        }
        else if (slot.action == "advance") {
            const auto& allPairs = mgr.GetAllRelationshipPairs();
            std::vector<std::pair<RelationshipPairKey, NPCRelationshipData>> pairsList(allPairs.begin(), allPairs.end());
            std::shuffle(pairsList.begin(), pairsList.end(), gen);

            for (const auto& [key, relData] : pairsList) {
                RE::FormID actorA = key.actorA;
                RE::FormID actorB = key.actorB;
                if (actorA == 0 || actorB == 0 || actorA == actorB) continue;
                if (actorA == kPlayerFormID || actorB == kPlayerFormID) continue;
                if (usedActors.find(actorA) != usedActors.end() || usedActors.find(actorB) != usedActors.end()) continue;
                if (IsNpcOnCooldown(actorA, currentGameDay) || IsNpcOnCooldown(actorB, currentGameDay)) continue;

                if (true) {
                    if (mgr.IsPlayerActiveMainPartner(actorA) || mgr.IsPlayerActiveMainPartner(actorB)) continue;
                }

                int sexA = ActorSvc::GetSex(actorA);
                int sexB = ActorSvc::GetSex(actorB);
                if (!IsPairOrientationAllowed(sexA, sexB, settings.matchmakingOrientation)) continue;

                std::string targetStatusStr;
                RelationshipStatus targetSt = RelationshipStatus::Unknown;

                if (relData.status == RelationshipStatus::Dating) {
                    targetStatusStr = "engaged";
                    targetSt = RelationshipStatus::Engaged;
                } else if (relData.status == RelationshipStatus::Engaged) {
                    targetStatusStr = "married";
                    targetSt = RelationshipStatus::Married;
                } else if (relData.status == RelationshipStatus::Lover) {
                    targetStatusStr = "dating";
                    targetSt = RelationshipStatus::Dating;
                } else {
                    continue;
                }

                std::string reason;
                if (!mgr.CanFormRelationship(actorA, actorB, targetSt, &reason)) {
                    continue;
                }

                MatchmakingResult r;
                r.actorAFormID = actorA;
                r.actorBFormID = actorB;
                r.actorAName = ActorSvc::GetName(actorA);
                r.actorBName = ActorSvc::GetName(actorB);
                r.action = "advance";
                r.targetStatus = targetStatusStr;
                r.isCheating = IsPairCheating(actorA, actorB, targetStatusStr);

                if (!ValidateCandidate(r)) {
                    continue;
                }

                usedActors.insert(actorA);
                usedActors.insert(actorB);

                MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> MATCH FOUND: {} (0x{:08X}) <-> {} (0x{:08X}) | Action: 'advance' -> '{}' | Cheating: {}",
                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID, r.targetStatus, r.isCheating);
                return r;
            }
            MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> NO MATCH: No existing relationship pair eligible for advance in current world state");
            return std::nullopt;
        }
        else if (slot.action == "breakup") {
            if (!settings.matchmakingAllowBreakups) {
                MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> SKIPPED: Breakups are disabled in mod settings");
                return std::nullopt;
            }

            const auto& allPairs = mgr.GetAllRelationshipPairs();
            std::vector<std::pair<RelationshipPairKey, NPCRelationshipData>> pairsList(allPairs.begin(), allPairs.end());
            std::shuffle(pairsList.begin(), pairsList.end(), gen);

            for (const auto& [key, relData] : pairsList) {
                RE::FormID actorA = key.actorA;
                RE::FormID actorB = key.actorB;
                if (actorA == 0 || actorB == 0 || actorA == actorB) continue;
                if (actorA == kPlayerFormID || actorB == kPlayerFormID) continue;
                if (usedActors.find(actorA) != usedActors.end() || usedActors.find(actorB) != usedActors.end()) continue;
                if (IsNpcOnCooldown(actorA, currentGameDay) || IsNpcOnCooldown(actorB, currentGameDay)) continue;

                if (true) {
                    if (mgr.IsPlayerActiveMainPartner(actorA) || mgr.IsPlayerActiveMainPartner(actorB)) continue;
                }

                auto [targetStr, targetEnum] = GetBreakupTarget(relData.status);
                if (targetEnum == RelationshipStatus::Unknown) continue;

                MatchmakingResult r;
                r.actorAFormID = actorA;
                r.actorBFormID = actorB;
                r.actorAName = ActorSvc::GetName(actorA);
                r.actorBName = ActorSvc::GetName(actorB);
                r.action = "breakup";
                r.targetStatus = targetStr;
                r.isCheating = false;

                if (!ValidateCandidate(r)) {
                    continue;
                }

                usedActors.insert(actorA);
                usedActors.insert(actorB);

                MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> MATCH FOUND: {} (0x{:08X}) <-> {} (0x{:08X}) | Action: 'breakup' -> '{}'",
                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID, r.targetStatus);
                return r;
            }
            MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> NO MATCH: No existing relationship pair eligible for breakup in current world state");
            return std::nullopt;
        }
        else if (slot.action == "open_or_close" || slot.action == "open" || slot.action == "close") {
            if (!settings.matchmakingAllowOpenClose) {
                MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> SKIPPED: Open/Close modifications are disabled in mod settings");
                return std::nullopt;
            }

            const auto& allPairs = mgr.GetAllRelationshipPairs();
            std::vector<std::pair<RelationshipPairKey, NPCRelationshipData>> pairsList(allPairs.begin(), allPairs.end());
            std::shuffle(pairsList.begin(), pairsList.end(), gen);

            for (const auto& [key, relData] : pairsList) {
                RE::FormID actorA = key.actorA;
                RE::FormID actorB = key.actorB;
                if (actorA == 0 || actorB == 0 || actorA == actorB) continue;
                if (actorA == kPlayerFormID || actorB == kPlayerFormID) continue;
                if (usedActors.find(actorA) != usedActors.end() || usedActors.find(actorB) != usedActors.end()) continue;
                if (IsNpcOnCooldown(actorA, currentGameDay) || IsNpcOnCooldown(actorB, currentGameDay)) continue;

                if (true) {
                    if (mgr.IsPlayerActiveMainPartner(actorA) || mgr.IsPlayerActiveMainPartner(actorB)) continue;
                }

                bool canOpen = mgr.CanOpenRelationship(actorA, actorB);
                bool canClose = mgr.CanCloseRelationship(actorA, actorB);

                std::string chosenAction;
                if (slot.action == "open" && canOpen) {
                    chosenAction = "open";
                } else if (slot.action == "close" && canClose) {
                    chosenAction = "close";
                } else if (slot.action == "open_or_close") {
                    if (canOpen && canClose) {
                        std::uniform_int_distribution<int> coin(0, 1);
                        chosenAction = (coin(gen) == 0) ? "open" : "close";
                    } else if (canOpen) {
                        chosenAction = "open";
                    } else if (canClose) {
                        chosenAction = "close";
                    }
                }

                if (chosenAction.empty()) continue;

                MatchmakingResult r;
                r.actorAFormID = actorA;
                r.actorBFormID = actorB;
                r.actorAName = ActorSvc::GetName(actorA);
                r.actorBName = ActorSvc::GetName(actorB);
                r.action = chosenAction;
                r.targetStatus = (chosenAction == "open") ? "open" : "closed";
                r.isCheating = false;

                if (!ValidateCandidate(r)) {
                    continue;
                }

                usedActors.insert(actorA);
                usedActors.insert(actorB);

                MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> MATCH FOUND: {} (0x{:08X}) <-> {} (0x{:08X}) | Action: '{}' -> '{}'",
                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID, r.action, r.targetStatus);
                return r;
            }
            MARAS_LOG_INFO("[Matchmaking:DiceRoll]   -> NO MATCH: No existing relationship pair eligible for open/close modification in current world state");
            return std::nullopt;
        }

        return std::nullopt;
    }

    void MatchmakingService::RunDiceRollMatchmaking(float currentGameDay) {
        const auto& settings = Settings::GetSingleton();
        if (!settings.matchmakingEnabled) {
            return;
        }

        MARAS_LOG_INFO("================================================================================");
        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Starting scheduled Dice Roll matchmaking scan");
        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Game Day: {:.2f} (Last Run: {:.2f}, Interval: {} days, Max Slots: {})",
                       currentGameDay, lastMatchmakingDay_, settings.matchmakingIntervalDays, settings.matchmakingMaxSuggestions);
        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Chances: Dating={}%, Engaged={}%, LoverAllowed={}%, LoverCheating={}%, Advance={}%, Breakup={}%, OpenClose={}%",
                       settings.diceRollChanceNewDating, settings.diceRollChanceNewEngaged, settings.diceRollChanceNewLoverAllowed,
                       settings.diceRollChanceNewLoverCheating, settings.diceRollChanceAdvance, settings.diceRollChanceBreakup, settings.diceRollChanceOpenClose);
        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Rules: Orientation={}, AllowCheating={}, AllowBreakups={}, AllowOpenClose={}, ExcludePlayerPartners={}",
                       static_cast<int>(settings.matchmakingOrientation), settings.matchmakingAllowCheating,
                       settings.matchmakingAllowBreakups, settings.matchmakingAllowOpenClose, settings.matchmakingExcludePlayerActivePartners);

        lastMatchmakingDay_ = currentGameDay;

        const auto allEntries = NPCIndex::GetSingleton().GetAllEntries(NPCFilter::AdultOnly);
        std::vector<NPCEntry> eligibleNPCs;
        eligibleNPCs.reserve(allEntries.size());
        auto& mgr = NPCRelationshipManager::GetSingleton();

        for (const auto& entry : allEntries) {
            if (entry.refFormID == kPlayerFormID || entry.baseFormID == kPlayerFormID) continue;
            if (entry.nameDisplay.empty() || entry.nameDisplay == "None") continue;
            RE::FormID actorID = entry.refFormID != 0 ? entry.refFormID : entry.baseFormID;
            if (actorID == 0) continue;
            bool isPlayerPartner = mgr.IsPlayerActiveMainPartner(actorID) ||
                                   (entry.baseFormID != 0 && mgr.IsPlayerActiveMainPartner(entry.baseFormID));
            if (isPlayerPartner) {
                [[maybe_unused]] bool isOpenWithPlayer = mgr.GetPairIsOpen(actorID, kPlayerFormID) ||
                                        (entry.baseFormID != 0 && mgr.GetPairIsOpen(entry.baseFormID, kPlayerFormID));
                // All active main partners of the Player (both closed and open) are governed exclusively by Tier 2 Partner Agency.
                if (true) {
                    continue;
                }
            }
            if (IsNpcOnCooldown(actorID, currentGameDay)) {
                continue;
            }
            eligibleNPCs.push_back(entry);
        }

        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Candidate pool: {} eligible adult NPC(s) in world index", eligibleNPCs.size());

        auto slots = RollActionSlots(settings.matchmakingMaxSuggestions);

        std::unordered_set<RE::FormID> usedActors;
        std::vector<MatchmakingResult> matches;

        for (size_t i = 0; i < slots.size(); ++i) {
            const auto& slot = slots[i];
            auto pairOpt = FindEligiblePairForSlot(slot, eligibleNPCs, usedActors, currentGameDay);
            if (pairOpt.has_value()) {
                matches.push_back(std::move(*pairOpt));
            }
        }

        MARAS_LOG_INFO("[Matchmaking:DiceRoll] -------------------- Final Results --------------------");
        MARAS_LOG_INFO("[Matchmaking:DiceRoll] Matched {} of {} planned slot(s):", matches.size(), slots.size());
        for (size_t i = 0; i < matches.size(); ++i) {
            const auto& m = matches[i];
            MARAS_LOG_INFO("[Matchmaking:DiceRoll]   Match #{}: {} (0x{:08X}) <-> {} (0x{:08X}) | Action: '{}' -> '{}' | Cheating: {}",
                           i + 1, m.actorAName, m.actorAFormID, m.actorBName, m.actorBFormID, m.action, m.targetStatus, m.isCheating);
        }

        if (matches.empty()) {
            MARAS_LOG_INFO("[Matchmaking:DiceRoll] No matches generated this cycle.");
            MARAS_LOG_INFO("================================================================================");
            return;
        }

        if (IsSkyrimNetAvailable() && settings.matchmakingEvaluationStep) {
            MARAS_LOG_INFO("[Matchmaking:DiceRoll] SkyrimNet is LOADED -> Dispatching {} match(es) to LLM verification & dual-memory generation pipeline",
                           matches.size());
            MARAS_LOG_INFO("================================================================================");
            StartVerificationBatch(std::move(matches), true);
        } else {
            MARAS_LOG_INFO("[Matchmaking:DiceRoll] Proceeding with {} match(es) to review/memory/apply pipeline (evaluationStep: {}, generateMemories: {})",
                           matches.size(), settings.matchmakingEvaluationStep, settings.matchmakingGenerateMemories);
            MARAS_LOG_INFO("================================================================================");
            StartMemoryBatch(std::move(matches), true);
        }
    }

    void MatchmakingService::RunManualMatchmaking(RE::FormID targetNpcFormID) {
        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[Matchmaking:RunManualMatchmaking] SkyrimNet is not available for manual matchmaking");
            PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, false);
            return;
        }
        if (targetNpcFormID == 0 || targetNpcFormID == kPlayerFormID) {
            MARAS_LOG_WARN("[Matchmaking:RunManualMatchmaking] Invalid target NPC FormID 0x{:08X} for manual matchmaking", targetNpcFormID);
            PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, false);
            return;
        }

        float currentGameDay = GetCurrentGameDay();

        MARAS_LOG_INFO("[Matchmaking:RunManualMatchmaking] Triggering manual matchmaking for target NPC 0x{:08X} ({})",
                       targetNpcFormID, ActorSvc::GetName(targetNpcFormID));

        nlohmann::json ctx = BuildManualContext(targetNpcFormID, currentGameDay);
        std::string contextJson = SafeJsonDump(ctx);
        std::string promptName = "maras_evaluations/maras_matchmaking_manual";

        SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, [this, targetNpcFormID](const char* response, int success) {
            if (!success || !response || response[0] == '\0') {
                MARAS_LOG_WARN("[Matchmaking:RunManualMatchmaking] Manual matchmaking request failed or returned empty response (success: {})", success);
                std::string targetName = ActorSvc::GetName(targetNpcFormID);
                ShowInGameFailedLLMModal(
                    "MARAS: Manual Matchmaking Failed",
                    { std::format("• Target NPC: {} — The matchmaking request failed or returned an empty response.", targetName) },
                    [this, targetNpcFormID]() {
                        RunManualMatchmaking(targetNpcFormID);
                    },
                    []() {
                        PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, false);
                    }
                );
                return;
            }
            SKSE::GetTaskInterface()->AddTask([this, respStr = std::string(response), targetNpcFormID]() {
                try {
                    nlohmann::json resp = JsonService::ParseLLMJson(respStr);
                    if (resp.is_discarded()) {
                        MARAS_LOG_WARN("[Matchmaking:RunManualMatchmaking] Manual matchmaking returned malformed/unrepairable JSON");
                        std::string targetName = ActorSvc::GetName(targetNpcFormID);
                        ShowInGameFailedLLMModal(
                            "MARAS: Manual Matchmaking Failed",
                            { std::format("• Target NPC: {} — The matchmaking scan returned malformed or unreadable JSON data.", targetName) },
                            [this, targetNpcFormID]() {
                                RunManualMatchmaking(targetNpcFormID);
                            },
                            []() {
                                PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, false);
                            },
                            false,
                            true,
                            "gamemaster_evaluation"
                        );
                        return;
                    }
                    ProcessManualResults(respStr, targetNpcFormID);
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("[Matchmaking:RunManualMatchmaking] Exception processing manual results: {}", e.what());
                    PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, false);
                }
            });
        });
    }

    void MatchmakingService::RunGlobalMatchmaking(bool autoApply) {
        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[Matchmaking:RunGlobalMatchmaking] SkyrimNet is not available for global matchmaking");
            PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, true);
            return;
        }

        float currentGameDay = GetCurrentGameDay();

        MARAS_LOG_INFO("[Matchmaking:RunGlobalMatchmaking] Triggering manual global matchmaking scan across all NPCs at game day {:.2f} (autoApply: {})",
                       currentGameDay, autoApply);

        nlohmann::json ctx = BuildScheduledContext(currentGameDay);
        std::string contextJson = SafeJsonDump(ctx);
        std::string promptName = "maras_evaluations/maras_matchmaking_scheduled";

        SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, [this, autoApply](const char* response, int success) {
            if (!success || !response || response[0] == '\0') {
                MARAS_LOG_WARN("[Matchmaking:RunGlobalMatchmaking] Global matchmaking request failed or returned empty response (success: {})", success);
                ShowInGameFailedLLMModal(
                    "MARAS: Global Matchmaking Scan Failed",
                    { "The global matchmaking scan request failed or returned an empty response." },
                    [this, autoApply]() {
                        RunGlobalMatchmaking(autoApply);
                    },
                    [autoApply]() {
                        if (!autoApply) {
                            PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, true);
                        }
                    }
                );
                return;
            }
            SKSE::GetTaskInterface()->AddTask([this, respStr = std::string(response), autoApply]() {
                try {
                    nlohmann::json resp = JsonService::ParseLLMJson(respStr);
                    if (resp.is_discarded()) {
                        MARAS_LOG_WARN("[Matchmaking:RunGlobalMatchmaking] Global matchmaking returned malformed/unrepairable JSON");
                        ShowInGameFailedLLMModal(
                            "MARAS: Global Matchmaking Scan Failed",
                            { "The global matchmaking scan returned malformed or unreadable JSON data." },
                            [this, autoApply]() {
                                RunGlobalMatchmaking(autoApply);
                            },
                            [autoApply]() {
                                if (!autoApply) {
                                    PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, true);
                                }
                            },
                            false,
                            true,
                            "gamemaster_evaluation"
                        );
                        return;
                    }
                    if (autoApply) {
                        ProcessScheduledResults(respStr, false);
                    } else {
                        ProcessGlobalManualResults(respStr);
                    }
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("[Matchmaking:RunGlobalMatchmaking] Exception processing global results: {}", e.what());
                    if (!autoApply) {
                        PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, true);
                    }
                }
            });
        });
    }

    std::string MatchmakingService::BuildActorDetailedRelationshipsString(RE::FormID actorFormID) {
        const auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto partnerIDs = mgr.GetAllPartners(actorFormID);

        bool hasOpenMainPartner = false;
        for (auto partnerId : partnerIDs) {
            auto st = mgr.GetRelationshipStatus(actorFormID, partnerId);
            if (st == RelationshipStatus::Dating || st == RelationshipStatus::Engaged || st == RelationshipStatus::Married) {
                const auto* pairData = mgr.GetNPCData(actorFormID, partnerId);
                if (pairData && pairData->isOpen) {
                    hasOpenMainPartner = true;
                    break;
                }
            }
        }

        std::ostringstream ss;
        bool first = true;
        for (auto partnerId : partnerIDs) {
            auto status = mgr.GetRelationshipStatus(actorFormID, partnerId);
            if (!IsActiveRelationship(status)) {
                continue;
            }

            const auto* pairData = mgr.GetNPCData(actorFormID, partnerId);
            std::string partnerName = ActorSvc::GetName(partnerId);
            if (partnerName.empty() || partnerName == "None") continue;

            auto children = mgr.GetChildren(actorFormID, partnerId);
            size_t childCount = children.size();
            std::string statusStr = StringUtils::ToLowerStd(Utils::RelationshipStatusToString(status));
            bool isOpen = pairData ? pairData->isOpen : false;

            if (!first) ss << "\n";
            first = false;

            if (status == RelationshipStatus::Lover) {
                std::string loverPerm = hasOpenMainPartner ? "allowed by open main relationship" : "cheating / secret infidelity";
                ss << std::format("- Lover with {} ({}, {} {})", partnerName, loverPerm, childCount, childCount == 1 ? "child" : "children");
            } else {
                std::string perm = isOpen ? "open primary partner" : "exclusive / closed primary partner";
                ss << std::format("- {} with {} ({}, {} {})", statusStr, partnerName, perm, childCount, childCount == 1 ? "child" : "children");
            }
        }
        return ss.str().empty() ? "None (single / no active relationships)" : ss.str();
    }

    nlohmann::json MatchmakingService::BuildMemoriesContext(RE::FormID actorA, RE::FormID actorB, const std::string& targetStatus, const std::string& action) {
        float currentGameDay = GetCurrentGameDay();

        auto pA = GetActorProfileSnapshot(actorA);
        auto pB = GetActorProfileSnapshot(actorB);

        MARAS_LOG_INFO("[Matchmaking:BuildMemoriesContext] actorA: {:08X} ({}, UUID: {}), actorB: {:08X} ({}, UUID: {}), targetStatus: {}, action: {}",
            actorA, pA.name, pA.uuid, actorB, pB.name, pB.uuid, targetStatus, action);

        nlohmann::json ctx;
        ctx["actorAUUID"] = pA.uuid;
        ctx["actorBUUID"] = pB.uuid;
        ctx["actorAName"] = pA.name;
        ctx["actorARace"] = pA.race;
        ctx["actorASex"] = pA.sex;
        ctx["actorARelationships"] = BuildActorDetailedRelationshipsString(actorA);
        ctx["actorBName"] = pB.name;
        ctx["actorBRace"] = pB.race;
        ctx["actorBSex"] = pB.sex;
        ctx["actorBRelationships"] = BuildActorDetailedRelationshipsString(actorB);
        ctx["action"] = action.empty() ? "update relationship" : action;
        ctx["targetStatus"] = targetStatus.empty() ? "dating" : targetStatus;
        ctx["gameDay"] = static_cast<int>(currentGameDay);
        return ctx;
    }

    nlohmann::json MatchmakingService::BuildVerifyContext(const MatchmakingResult& result) {
        float currentGameDay = GetCurrentGameDay();

        auto pA = GetActorProfileSnapshot(result.actorAFormID);
        auto pB = GetActorProfileSnapshot(result.actorBFormID);

        // Resolve existing primary committed partners for Actor A and Actor B (Married, Engaged, Dating)
        uint64_t partnerAUUID = 0;
        std::string partnerAName = "";
        RE::FormID partnerAFID = GetCommittedPrimaryPartner(result.actorAFormID);
        if (partnerAFID != 0 && partnerAFID != result.actorBFormID) {
            partnerAUUID = GetActorUUID(partnerAFID);
            partnerAName = ActorSvc::GetName(partnerAFID);
        }

        uint64_t partnerBUUID = 0;
        std::string partnerBName = "";
        RE::FormID partnerBFID = GetCommittedPrimaryPartner(result.actorBFormID);
        if (partnerBFID != 0 && partnerBFID != result.actorAFormID) {
            partnerBUUID = GetActorUUID(partnerBFID);
            partnerBName = ActorSvc::GetName(partnerBFID);
        }

        nlohmann::json ctx;
        ctx["actorAUUID"] = pA.uuid;
        ctx["actorBUUID"] = pB.uuid;
        ctx["actorAName"] = pA.name;
        ctx["actorARace"] = pA.race;
        ctx["actorASex"] = pA.sex;
        ctx["actorARelationships"] = BuildActorDetailedRelationshipsString(result.actorAFormID);
        ctx["actorBName"] = pB.name;
        ctx["actorBRace"] = pB.race;
        ctx["actorBSex"] = pB.sex;
        ctx["actorBRelationships"] = BuildActorDetailedRelationshipsString(result.actorBFormID);
        ctx["action"] = result.action.empty() ? "update relationship" : result.action;
        ctx["targetStatus"] = result.targetStatus.empty() ? "dating" : result.targetStatus;
        ctx["isCheating"] = result.isCheating;
        ctx["committedPartnerAUUID"] = partnerAUUID;
        ctx["committedPartnerAName"] = partnerAName;
        ctx["committedPartnerBUUID"] = partnerBUUID;
        ctx["committedPartnerBName"] = partnerBName;
        ctx["gameDay"] = static_cast<int>(currentGameDay);

        MARAS_LOG_INFO("[Matchmaking:BuildVerifyContext] Built verification context for {} <-> {} (Action: {}, Status: {}, Cheating: {})",
                       pA.name, pB.name, ctx["action"].get<std::string>(), ctx["targetStatus"].get<std::string>(), result.isCheating);

        return ctx;
    }

    void MatchmakingService::RequestMemoriesAsync(RE::FormID actorA, RE::FormID actorB,
                                                  const std::string& targetStatus, const std::string& action,
                                                  std::function<void(const std::string&)> onSuccess,
                                                  std::function<void()> onFailure) {
        auto dispatchFailure = [](std::function<void()>&& cb) {
            if (!cb) return;
            if (auto* taskIF = SKSE::GetTaskInterface()) {
                taskIF->AddTask([cb = std::move(cb)]() {
                    try {
                        cb();
                    } catch (const std::exception& e) {
                        MARAS_LOG_ERROR("[Matchmaking:RequestMemoriesAsync] Exception in onFailure callback: {}", e.what());
                    }
                });
            }
        };

        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[Matchmaking:RequestMemoriesAsync] SkyrimNet is not available for memory generation");
            dispatchFailure(std::move(onFailure));
            return;
        }
        if (actorA == 0 || actorB == 0) {
            MARAS_LOG_WARN("[Matchmaking:RequestMemoriesAsync] Invalid actor FormIDs for memory generation (0x{:08X}, 0x{:08X})", actorA, actorB);
            dispatchFailure(std::move(onFailure));
            return;
        }

        uint64_t uuidA = GetActorUUID(actorA);
        uint64_t uuidB = GetActorUUID(actorB);

        MARAS_LOG_INFO("[Matchmaking:RequestMemoriesAsync] Requesting memories for {:08X} ({}, UUID: {}) and {:08X} ({}, UUID: {}) with status '{}' action '{}'",
                       actorA, ActorSvc::GetName(actorA), uuidA, actorB, ActorSvc::GetName(actorB), uuidB, targetStatus, action);

        nlohmann::json ctx = BuildMemoriesContext(actorA, actorB, targetStatus, action);
        std::string contextJson = SafeJsonDump(ctx);
        std::string promptName = "maras_evaluations/maras_matchmaking_memories";

        SendCustomPromptToLLM(promptName, "memory", contextJson,
            [onSuccess = std::move(onSuccess), onFailure = std::move(onFailure)](const char* response, int success) mutable {
                if (!success || !response || response[0] == '\0') {
                    if (onFailure) {
                        if (auto* taskIF = SKSE::GetTaskInterface()) {
                            taskIF->AddTask([onFailure = std::move(onFailure)]() {
                                try {
                                    onFailure();
                                } catch (const std::exception& e) {
                                    MARAS_LOG_ERROR("[Matchmaking:RequestMemoriesAsync] Exception in onFailure callback: {}", e.what());
                                }
                            });
                        }
                    }
                    return;
                }
                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    taskIF->AddTask([respStr = std::string(response), onSuccess = std::move(onSuccess)]() {
                        try {
                            if (onSuccess) onSuccess(respStr);
                        } catch (const std::exception& e) {
                            MARAS_LOG_ERROR("[Matchmaking:RequestMemoriesAsync] Exception in onSuccess callback: {}", e.what());
                        }
                    });
                }
            });
    }

    void MatchmakingService::RunMemoryGeneration(RE::FormID actorA, RE::FormID actorB, const std::string& targetStatus, const std::string& action) {
        RequestMemoriesAsync(actorA, actorB, targetStatus, action,
            [this, actorA, actorB, targetStatus, action](const std::string& respStr) {
                auto entries = ParseGeneratedMemories(respStr, actorA, actorB);
                if (entries.empty()) {
                    MARAS_LOG_WARN("[Matchmaking:RunMemoryGeneration] Failed to parse generated memories for {:08X} & {:08X}", actorA, actorB);
                    ShowInGameFailedLLMModal(
                        "MARAS: Memory Generation Failed",
                        { std::format("• {} & {}: Failed to parse memories array from LLM response (malformed or invalid JSON).", ActorSvc::GetName(actorA), ActorSvc::GetName(actorB)) },
                        [this, actorA, actorB, targetStatus, action]() {
                            RunMemoryGeneration(actorA, actorB, targetStatus, action);
                        },
                        [actorB]() {
                            PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, nlohmann::json::array());
                        },
                        false,
                        true,
                        "memory"
                    );
                    return;
                }
                ProcessGeneratedMemories(respStr, actorA, actorB);
            },
            [this, actorA, actorB, targetStatus, action]() {
                ShowInGameFailedLLMModal(
                    "MARAS: Memory Generation Failed",
                    { std::format("• {} & {}: LLM memory generation request failed or returned an empty response.", ActorSvc::GetName(actorA), ActorSvc::GetName(actorB)) },
                    [this, actorA, actorB, targetStatus, action]() {
                        RunMemoryGeneration(actorA, actorB, targetStatus, action);
                    },
                    [actorB]() {
                        PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, nlohmann::json::array());
                    },
                    true,
                    false,
                    "memory"
                );
            });
    }

    std::vector<GeneratedMemoryEntry> MatchmakingService::ParseGeneratedMemories(const std::string& response, RE::FormID actorA, RE::FormID actorB) {
        std::vector<GeneratedMemoryEntry> entries;
        nlohmann::json resp = JsonService::ParseLLMJson(response);

        const auto* memsArray = FindJsonArray(resp, "memories");
        if (!memsArray) {
            MARAS_LOG_WARN("[Matchmaking] Failed to parse memories array from LLM response (even after sanitization and repair): {}", response);
            return entries;
        }

        std::string nameA = ActorSvc::GetName(actorA);
        std::string nameB = ActorSvc::GetName(actorB);
        std::string nameALower = StringUtils::ToLowerStd(nameA);
        std::string nameBLower = StringUtils::ToLowerStd(nameB);

        size_t idx = 0;
        for (const auto& item : *memsArray) {
            if (!item.is_object()) continue;

            std::string actorName = GetJsonString(item, "actor");
            std::string actorNameLower = StringUtils::ToLowerStd(actorName);
            RE::FormID memActorId = 0;

            if (!nameBLower.empty() && (actorNameLower == nameBLower || actorNameLower.find(nameBLower) != std::string::npos)) {
                memActorId = actorB;
            } else if (!nameALower.empty() && (actorNameLower == nameALower || actorNameLower.find(nameALower) != std::string::npos)) {
                memActorId = actorA;
            } else {
                // Fallback by position: first is actorA, second is actorB
                memActorId = (idx == 0) ? actorA : actorB;
            }

            // Ensure reference FormID resolution if possible
            if (auto* refr = ActorSvc::GetReference(memActorId)) {
                memActorId = refr->GetFormID();
            } else {
                RE::FormID ref = NPCIndex::GetSingleton().GetRefID(memActorId);
                if (ref != 0) memActorId = ref;
            }

            GeneratedMemoryEntry entry;
            entry.actorName = actorName.empty() ? (memActorId == actorA ? nameA : nameB) : actorName;
            entry.actorFormID = memActorId;
            entry.content = GetJsonString(item, "content");
            entry.location = GetJsonString(item, "location", "Skyrim");
            entry.emotion = GetJsonString(item, "emotion", "content");
            entry.importanceScore = GetJsonFloat(item, "importance_score", 0.7f);
            entry.type = GetJsonString(item, "type", "RELATIONSHIP");

            if (item.contains("tags") && item["tags"].is_array()) {
                for (const auto& tag : item["tags"]) {
                    if (tag.is_string()) {
                        entry.tags.push_back(tag.get<std::string>());
                    }
                }
            }

            if (!entry.content.empty()) {
                entries.push_back(std::move(entry));
            }
            idx++;
        }

        return entries;
    }

    void MatchmakingService::ProcessGeneratedMemories(const std::string& response, RE::FormID actorA, RE::FormID actorB) {
        auto entries = ParseGeneratedMemories(response, actorA, actorB);
        if (entries.empty()) {
            MARAS_LOG_WARN("[Matchmaking:ProcessGeneratedMemories] No valid memories parsed from LLM response");
            PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, nlohmann::json::array());
            return;
        }

        nlohmann::json outputArray = nlohmann::json::array();
        for (const auto& entry : entries) {
            nlohmann::json m;
            m["actor"] = entry.actorName;
            m["actorFormId"] = PrismaUIDataBridge::FormIDToString(entry.actorFormID);
            m["content"] = entry.content;
            m["location"] = entry.location;
            m["emotion"] = entry.emotion;
            m["importance_score"] = entry.importanceScore;
            m["type"] = entry.type;
            m["tags"] = entry.tags;
            outputArray.push_back(m);
        }

        MARAS_LOG_INFO("[Matchmaking:ProcessGeneratedMemories] Generated {} memories for partner 0x{:08X}", outputArray.size(), actorB);
        PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, outputArray);
    }

    void MatchmakingService::ShowInGameFailedLLMModal(
        const std::string& title,
        const std::vector<std::string>& failedDescriptions,
        std::function<void()> onRetry,
        std::function<void()> onCancel,
        bool hasRequestFailure,
        bool hasInvalidJson,
        std::string_view llmVariant)
    {
        if (failedDescriptions.empty()) {
            return;
        }

        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("[Matchmaking:ShowInGameFailedLLMModal] No task interface, falling back to cancel");
            if (onCancel) onCancel();
            return;
        }

        std::ostringstream ss;
        for (size_t i = 0; i < failedDescriptions.size(); ++i) {
            ss << failedDescriptions[i];
            if (i + 1 < failedDescriptions.size()) {
                ss << "\n";
            }
        }

        ModalRequest req;
        req.title = !title.empty() ? title : "Matchmaking LLM Request Failed";
        req.subtitle = "SkyrimNet Matchmaking Pipeline";
        req.message = "Choose Retry to re-run failed requests, or Cancel to abort them.";
        req.tone = ModalTone::Warning;
        req.size = ModalSize::Md;
        req.defaultCancelButtonIndex = 1;

        req.controls.push_back(ModalControl::MakeInfoBox(
            "failed_tasks",
            failedDescriptions.size() > 1 ? "Failed Requests" : "Failure Details",
            ss.str(),
            ModalTone::Warning));

        std::string guidanceTitle = (!hasRequestFailure && hasInvalidJson)
            ? GetLLMFailureGuidanceTitle(LLMFailureType::InvalidJson)
            : (hasRequestFailure && !hasInvalidJson)
                ? GetLLMFailureGuidanceTitle(LLMFailureType::RequestFailed)
                : "What Does This Mean?";

        req.controls.push_back(ModalControl::MakeInfoBox(
            "error_guidance",
            guidanceTitle,
            GetLLMBatchFailureGuidance(hasRequestFailure, hasInvalidJson, llmVariant),
            ModalTone::Default));

        req.buttons.push_back(ModalButton::Make("retry", "Retry Failed", ModalButtonVariant::Primary));
        req.buttons.push_back(ModalButton::Make("cancel", "Cancel", ModalButtonVariant::Secondary));

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [onRetry = std::move(onRetry), onCancel = std::move(onCancel)](const ModalResponse& resp) mutable {
                if (!resp.aborted && (resp.buttonIndex == 0 || resp.buttonId == "retry")) {
                    if (onRetry) onRetry();
                } else {
                    if (onCancel) onCancel();
                }
            });
    }

    namespace {
        std::string FormatStatusDisplayLabel(RelationshipStatus status) {
            switch (status) {
                case RelationshipStatus::Dating:
                    return "Dating";
                case RelationshipStatus::Lover:
                    return "Lover";
                case RelationshipStatus::Engaged:
                    return "Engaged";
                case RelationshipStatus::Married:
                    return "Married";
                case RelationshipStatus::ExDating:
                    return "Ex-Dating";
                case RelationshipStatus::ExLover:
                    return "Ex-Lover";
                case RelationshipStatus::Jilted:
                    return "Jilted";
                case RelationshipStatus::Divorced:
                    return "Divorced";
                case RelationshipStatus::Candidate:
                case RelationshipStatus::Unknown:
                default:
                    return "Single / Unrelated";
            }
        }

        std::string FormatTargetStatusDisplayLabel(std::string_view targetStatus) {
            std::string lower = StringUtils::ToLowerStd(targetStatus);
            if (lower == "dating") return "Dating";
            if (lower == "lover") return "Lover";
            if (lower == "engaged") return "Engaged";
            if (lower == "married") return "Married";
            if (lower == "exdating") return "Ex-Dating";
            if (lower == "exlover") return "Ex-Lover";
            if (lower == "jilted") return "Jilted";
            if (lower == "divorced") return "Divorced";
            if (lower == "open") return "Open Relationship";
            if (lower == "closed") return "Exclusive / Closed";
            return std::string(targetStatus);
        }
    }  // namespace

    void MatchmakingService::ShowMatchmakingReviewModal(
        std::vector<MatchmakingResult> candidates,
        bool isScheduledScan,
        std::function<void(std::vector<MatchmakingResult>)> onApproved)
    {
        if (candidates.empty()) {
            if (onApproved) onApproved({});
            return;
        }

        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("[Matchmaking:ShowMatchmakingReviewModal] No task interface, applying candidates without modal");
            if (onApproved) onApproved(std::move(candidates));
            return;
        }

        auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto& settings = Settings::GetSingleton();

        std::vector<ModalChecklistItem> items;
        items.reserve(candidates.size());

        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto& cand = candidates[i];
            ModalChecklistItem item;
            item.id = std::format("match_{}", i);

            std::string nameA = !cand.actorAName.empty() ? cand.actorAName : ActorSvc::GetName(cand.actorAFormID);
            std::string nameB = !cand.actorBName.empty() ? cand.actorBName : ActorSvc::GetName(cand.actorBFormID);
            item.label = std::format("{} & {}", nameA, nameB);

            RelationshipStatus currentSt = mgr.GetRelationshipStatus(cand.actorAFormID, cand.actorBFormID);
            const auto* pairData = mgr.GetNPCData(cand.actorAFormID, cand.actorBFormID);
            std::string currentLabel = FormatStatusDisplayLabel(currentSt);
            if (IsActiveRelationship(currentSt) && pairData) {
                currentLabel += pairData->isOpen ? " (Open)" : " (Exclusive)";
            }

            item.subtitle = std::format("{} -> {}", currentLabel, FormatTargetStatusDisplayLabel(cand.targetStatus));
            item.checked = true;

            if (cand.action == "new" || cand.action == "advance") {
                if (cand.isCheating) {
                    item.badgeText = "Cheating";
                    item.badgeTone = "warning";
                } else if (cand.action == "advance") {
                    item.badgeText = "Advance";
                    item.badgeTone = "success";
                } else {
                    item.badgeText = "New";
                    item.badgeTone = "accent";
                }

                item.selectKey = "targetStatus";
                item.selectLabel = "Status";
                item.selectValue = StringUtils::ToLowerStd(cand.targetStatus);
                if (item.selectValue.empty()) {
                    item.selectValue = "dating";
                }

                struct StatusOptionSpec {
                    const char* value;
                    const char* label;
                    RelationshipStatus statusEnum;
                };
                static constexpr StatusOptionSpec kPositiveOptions[] = {
                    {"dating", "Dating", RelationshipStatus::Dating},
                    {"lover", "Lover", RelationshipStatus::Lover},
                    {"engaged", "Engaged", RelationshipStatus::Engaged},
                    {"married", "Married", RelationshipStatus::Married}
                };

                for (const auto& spec : kPositiveOptions) {
                    ModalSelectOption opt;
                    opt.value = spec.value;
                    opt.label = spec.label;

                    std::string reason;
                    bool canForm = mgr.CanFormRelationship(cand.actorAFormID, cand.actorBFormID, spec.statusEnum, &reason);
                    if (canForm && spec.statusEnum == RelationshipStatus::Lover && !settings.matchmakingAllowCheating) {
                        if (IsPairCheating(cand.actorAFormID, cand.actorBFormID, "lover")) {
                            canForm = false;
                            reason = "Cheating is disabled in matchmaking settings";
                        }
                    }

                    if (!canForm && item.selectValue != spec.value) {
                        opt.disabled = true;
                        opt.tooltip = reason;
                    }
                    item.selectOptions.push_back(std::move(opt));
                }
            } else if (cand.action == "breakup") {
                item.badgeText = "Breakup";
                item.badgeTone = "danger";

                item.selectKey = "targetStatus";
                item.selectLabel = "Status";
                item.selectValue = StringUtils::ToLowerStd(cand.targetStatus);
                if (item.selectValue.empty()) {
                    item.selectValue = "exdating";
                }

                item.selectOptions = {
                    {"exdating", "Ex-Dating", false, ""},
                    {"exlover", "Ex-Lover", false, ""},
                    {"jilted", "Jilted", false, ""},
                    {"divorced", "Divorced", false, ""}
                };
            } else if (cand.action == "open") {
                item.badgeText = "Open";
                item.badgeTone = "accent";
            } else if (cand.action == "close") {
                item.badgeText = "Close";
                item.badgeTone = "default";
            }

            if (!cand.memory.empty()) {
                item.description = cand.memory;
            } else {
                item.description = std::format("Proposed '{}' transition to {}.",
                                               cand.action,
                                               FormatTargetStatusDisplayLabel(cand.targetStatus));
            }

            items.push_back(std::move(item));
        }

        ModalRequest req;
        req.title = "Matchmaking Suggestions";
        req.subtitle = std::format("{} Proposed NPC Relationship {} ({})",
                                   candidates.size(),
                                   candidates.size() == 1 ? "Change" : "Changes",
                                   isScheduledScan ? "Scheduled Scan" : "World Scan");
        req.message = "Review the proposed NPC relationship changes below. Uncheck any pairs you wish to skip, or adjust their target status before applying.";
        req.tone = ModalTone::Ai;
        req.size = candidates.size() > 4 ? ModalSize::Xl : ModalSize::Lg;
        req.defaultCancelButtonIndex = 1;

        req.controls.push_back(ModalControl::MakeChecklist(
            "matches",
            "Proposed Pairings",
            std::move(items),
            true));

        ModalButton applyBtn = ModalButton::Make("apply", "Apply Selected", ModalButtonVariant::Primary);
        applyBtn.requireChecklistId = "matches";
        applyBtn.minCheckedCount = 1;
        req.buttons.push_back(std::move(applyBtn));
        req.buttons.push_back(ModalButton::Make("discard", "Discard All", ModalButtonVariant::Secondary));

        PrismaModalManager::GetSingleton().ShowModal(
            std::move(req),
            [candidates = std::move(candidates), onApproved = std::move(onApproved)](const ModalResponse& resp) mutable {
                if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "apply")) {
                    MARAS_LOG_INFO("[Matchmaking:ShowMatchmakingReviewModal] User discarded all {} matchmaking suggestion(s).",
                                   candidates.size());
                    if (onApproved) onApproved({});
                    return;
                }

                std::vector<MatchmakingResult> approved;
                approved.reserve(candidates.size());

                for (size_t i = 0; i < candidates.size(); ++i) {
                    std::string itemId = std::format("match_{}", i);
                    if (!resp.IsItemChecked("matches", itemId, false)) {
                        MARAS_LOG_INFO("[Matchmaking:ShowMatchmakingReviewModal] User unchecked match: {} <-> {}",
                                       candidates[i].actorAName, candidates[i].actorBName);
                        continue;
                    }

                    MatchmakingResult match = std::move(candidates[i]);
                    if (match.action == "new" || match.action == "advance" || match.action == "breakup") {
                        std::string chosenStatus = StringUtils::ToLowerStd(
                            resp.GetItemSelectValue("matches", itemId, match.targetStatus));
                        if (!chosenStatus.empty() && chosenStatus != match.targetStatus) {
                            MARAS_LOG_INFO("[Matchmaking:ShowMatchmakingReviewModal] User overrode target status for {} <-> {}: '{}' -> '{}'",
                                           match.actorAName, match.actorBName, match.targetStatus, chosenStatus);
                            match.targetStatus = chosenStatus;
                            match.isCheating = IsPairCheating(match.actorAFormID, match.actorBFormID, match.targetStatus);
                        }
                    }

                    approved.push_back(std::move(match));
                }

                MARAS_LOG_INFO("[Matchmaking:ShowMatchmakingReviewModal] User approved {} of {} matchmaking suggestion(s).",
                               approved.size(), candidates.size());
                if (onApproved) onApproved(std::move(approved));
            });
    }

    void MatchmakingService::StartVerificationBatch(std::vector<MatchmakingResult> candidates, bool isScheduledScan) {
        if (candidates.empty()) {
            return;
        }

        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[Matchmaking:StartVerificationBatch] SkyrimNet is not available, forwarding candidates directly without verification");
            StartMemoryBatch(std::move(candidates), isScheduledScan);
            return;
        }

        auto batch = std::make_shared<VerificationBatch>();
        batch->isScheduledScan = isScheduledScan;
        batch->tasks.reserve(candidates.size());
        for (auto& cand : candidates) {
            VerificationTask task;
            task.result = std::move(cand);
            batch->tasks.push_back(std::move(task));
        }

        batch->pendingCount = batch->tasks.size();
        auto allIndices = MakeBatchIndices(batch->tasks.size());

        MARAS_LOG_INFO("[Matchmaking:StartVerificationBatch] Starting verification batch for {} candidates (isScheduled: {})",
                       batch->tasks.size(), isScheduledScan);

        RetryVerificationTasks(batch, allIndices);
    }

    void MatchmakingService::RetryVerificationTasks(std::shared_ptr<VerificationBatch> batch, const std::vector<size_t>& indicesToRetry) {
        if (!batch || indicesToRetry.empty()) {
            return;
        }

        for (size_t idx : indicesToRetry) {
            if (idx >= batch->tasks.size()) continue;

            auto& task = batch->tasks[idx];
            task.completed = false;
            task.failed = false;
            task.isInvalidJson = false;
            task.failureReason.clear();
            task.suitable = false;
            task.verificationReason.clear();

            nlohmann::json ctx = BuildVerifyContext(task.result);
            std::string contextJson = SafeJsonDump(ctx);
            std::string promptName = "maras_evaluations/maras_matchmaking_verify";

            MARAS_LOG_INFO("[Matchmaking:RetryVerificationTasks] Requesting narrative verification for [{}/{}] {} (0x{:08X}) <-> {} (0x{:08X}) (Action: {} -> {})",
                           idx + 1, batch->tasks.size(), task.result.actorAName, task.result.actorAFormID, task.result.actorBName, task.result.actorBFormID,
                           task.result.action, task.result.targetStatus);

            SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, [this, batch, idx](const char* response, int success) {
                std::string respStr = (response && response[0] != '\0') ? std::string(response) : "";
                SKSE::GetTaskInterface()->AddTask([this, batch, idx, success, respStr]() {
                    try {
                        if (idx >= batch->tasks.size()) return;
                        auto& t = batch->tasks[idx];

                        if (!success || respStr.empty()) {
                            t.completed = true;
                            t.failed = true;
                            t.isInvalidJson = false;
                            t.failureReason = "LLM request failed or returned empty response";
                            MARAS_LOG_WARN("[Matchmaking:RetryVerificationTasks] Verification request failed for {} <-> {}",
                                           t.result.actorAName, t.result.actorBName);
                        } else {
                            nlohmann::json resp = JsonService::ParseLLMJson(respStr);
                            if (resp.is_discarded()) {
                                t.completed = true;
                                t.failed = true;
                                t.isInvalidJson = true;
                                t.failureReason = "Malformed / unrepairable JSON response";
                                MARAS_LOG_WARN("[Matchmaking:RetryVerificationTasks] Failed to parse verification JSON for {} <-> {}. Raw: {}",
                                               t.result.actorAName, t.result.actorBName, respStr);
                            } else {
                                t.completed = true;
                                t.failed = false;
                                t.isInvalidJson = false;
                                t.suitable = GetJsonBool(resp, "suitable", GetJsonBool(resp, "allow", false));
                                t.verificationReason = GetJsonString(resp, "reason", "No reason provided");
                                MARAS_LOG_INFO("[Matchmaking:RetryVerificationTasks] Verification for {} <-> {}: suitable={}, reason='{}'",
                                               t.result.actorAName, t.result.actorBName, t.suitable, t.verificationReason);
                            }
                        }

                        HandleBatchCompletion(batch, "MARAS: Verification Request Failed", "gamemaster_evaluation", this,
                            [this](auto b, const auto& idxs) { RetryVerificationTasks(b, idxs); },
                            [](auto b, const auto& idxs) {
                                for (size_t fi : idxs) b->tasks[fi].suitable = false;
                            },
                            [this](auto b) { FinishVerificationBatch(b); }
                        );
                    } catch (const std::exception& e) {
                        MARAS_LOG_ERROR("[Matchmaking:RetryVerificationTasks] Exception in verification task callback: {}", e.what());
                    }
                });
            });
        }
    }

    void MatchmakingService::FinishVerificationBatch(std::shared_ptr<VerificationBatch> batch) {
        if (!batch) return;

        std::vector<MatchmakingResult> verifiedMatches;
        for (const auto& task : batch->tasks) {
            if (task.completed && !task.failed && task.suitable) {
                MARAS_LOG_INFO("[Matchmaking:FinishVerificationBatch] Match VERIFIED by LLM for {} (0x{:08X}) <-> {} (0x{:08X}) | Action: {} -> {} | Reason: {}",
                               task.result.actorAName, task.result.actorAFormID, task.result.actorBName, task.result.actorBFormID,
                               task.result.action, task.result.targetStatus, task.verificationReason);
                MatchmakingResult verified = task.result;
                if (verified.memory.empty() && !task.verificationReason.empty() && task.verificationReason != "No reason provided") {
                    verified.memory = task.verificationReason;
                }
                verifiedMatches.push_back(std::move(verified));
            } else {
                MARAS_LOG_INFO("[Matchmaking:FinishVerificationBatch] Match REJECTED or CANCELLED for {} (0x{:08X}) <-> {} (0x{:08X}) | Action: {} -> {} | Suitable: {}, Failed: {}",
                               task.result.actorAName, task.result.actorAFormID, task.result.actorBName, task.result.actorBFormID,
                               task.result.action, task.result.targetStatus, task.suitable, task.failed);
            }
        }

        if (verifiedMatches.empty()) {
            MARAS_LOG_INFO("[Matchmaking:FinishVerificationBatch] No matches were verified. Batch completed.");
            return;
        }

        MARAS_LOG_INFO("[Matchmaking:FinishVerificationBatch] Advancing {} verified match(es) to review/memory pipeline", verifiedMatches.size());
        StartMemoryBatch(std::move(verifiedMatches), batch->isScheduledScan);
    }

    void MatchmakingService::StartMemoryBatch(std::vector<MatchmakingResult> verifiedMatches, bool isScheduledScan) {
        if (verifiedMatches.empty()) {
            return;
        }

        const auto& settings = Settings::GetSingleton();
        if (settings.matchmakingConfirmResults) {
            MARAS_LOG_INFO("[Matchmaking:StartMemoryBatch] matchmakingConfirmResults is enabled. Presenting review modal for {} match(es) (isScheduled: {})",
                           verifiedMatches.size(), isScheduledScan);
            ShowMatchmakingReviewModal(
                std::move(verifiedMatches),
                isScheduledScan,
                [this, isScheduledScan](std::vector<MatchmakingResult> approvedMatches) {
                    ExecuteMemoryBatchOrApply(std::move(approvedMatches), isScheduledScan);
                });
            return;
        }

        ExecuteMemoryBatchOrApply(std::move(verifiedMatches), isScheduledScan);
    }

    void MatchmakingService::ExecuteMemoryBatchOrApply(std::vector<MatchmakingResult> approvedMatches, bool isScheduledScan) {
        if (approvedMatches.empty()) {
            return;
        }

        const auto& settings = Settings::GetSingleton();
        if (!settings.matchmakingGenerateMemories || !IsSkyrimNetAvailable()) {
            MARAS_LOG_INFO("[Matchmaking:ExecuteMemoryBatchOrApply] {} Applying {} match(es) directly without memory ingestion.",
                           !IsSkyrimNetAvailable() ? "SkyrimNet is not available." : "Memory generation is disabled.",
                           approvedMatches.size());
            bool anyApplied = false;
            for (auto& match : approvedMatches) {
                if (ApplyMatch(match)) {
                    anyApplied = true;
                }
            }
            if (anyApplied) {
                PrismaUIDataBridge::GetSingleton().SendOverviewData();
            }
            return;
        }

        auto batch = std::make_shared<MemoryBatch>();
        batch->isScheduledScan = isScheduledScan;
        batch->tasks.reserve(approvedMatches.size());
        for (auto& match : approvedMatches) {
            MemoryTask task;
            task.result = std::move(match);
            batch->tasks.push_back(std::move(task));
        }

        batch->pendingCount = batch->tasks.size();
        auto allIndices = MakeBatchIndices(batch->tasks.size());

        MARAS_LOG_INFO("[Matchmaking:ExecuteMemoryBatchOrApply] Starting memory generation batch for {} approved match(es) (isScheduled: {})",
                       batch->tasks.size(), isScheduledScan);

        RetryMemoryTasks(batch, allIndices);
    }

    void MatchmakingService::RetryMemoryTasks(std::shared_ptr<MemoryBatch> batch, const std::vector<size_t>& indicesToRetry) {
        if (!batch || indicesToRetry.empty()) {
            return;
        }

        for (size_t idx : indicesToRetry) {
            if (idx >= batch->tasks.size()) continue;

            auto& task = batch->tasks[idx];
            task.completed = false;
            task.failed = false;
            task.isInvalidJson = false;
            task.failureReason.clear();
            task.generatedMemories.clear();

            nlohmann::json ctx = BuildMemoriesContext(task.result.actorAFormID, task.result.actorBFormID, task.result.targetStatus, task.result.action);
            std::string contextJson = SafeJsonDump(ctx);
            std::string promptName = "maras_evaluations/maras_matchmaking_memories";

            MARAS_LOG_INFO("[Matchmaking:RetryMemoryTasks] Requesting memory generation for [{}/{}] {} (0x{:08X}) <-> {} (0x{:08X})",
                           idx + 1, batch->tasks.size(), task.result.actorAName, task.result.actorAFormID, task.result.actorBName, task.result.actorBFormID);

            SendCustomPromptToLLM(promptName, "memory", contextJson, [this, batch, idx](const char* response, int success) {
                std::string respStr = (response && response[0] != '\0') ? std::string(response) : "";
                SKSE::GetTaskInterface()->AddTask([this, batch, idx, success, respStr]() {
                    try {
                        if (idx >= batch->tasks.size()) return;
                        auto& t = batch->tasks[idx];

                        if (!success || respStr.empty()) {
                            t.completed = true;
                            t.failed = true;
                            t.isInvalidJson = false;
                            t.failureReason = "LLM request failed or returned empty response";
                            MARAS_LOG_WARN("[Matchmaking:RetryMemoryTasks] Memory generation failed for {} <-> {}",
                                           t.result.actorAName, t.result.actorBName);
                        } else {
                            auto memories = ParseGeneratedMemories(respStr, t.result.actorAFormID, t.result.actorBFormID);
                            if (memories.empty()) {
                                t.completed = true;
                                t.failed = true;
                                t.isInvalidJson = true;
                                t.failureReason = "Failed to parse memories array from LLM response (malformed or invalid JSON)";
                                MARAS_LOG_WARN("[Matchmaking:RetryMemoryTasks] Failed to parse memories for {} <-> {}. Raw: {}",
                                               t.result.actorAName, t.result.actorBName, respStr);
                            } else {
                                t.completed = true;
                                t.failed = false;
                                t.isInvalidJson = false;
                                t.generatedMemories = std::move(memories);
                                MARAS_LOG_INFO("[Matchmaking:RetryMemoryTasks] Parsed {} memories for {} <-> {}",
                                               t.generatedMemories.size(), t.result.actorAName, t.result.actorBName);
                            }
                        }

                        HandleBatchCompletion(batch, "MARAS: Memory Generation Failed", "memory", this,
                            [this](auto b, const auto& idxs) { RetryMemoryTasks(b, idxs); },
                            [](auto, const auto&) {},
                            [this](auto b) { FinishMemoryBatch(b); }
                        );
                    } catch (const std::exception& e) {
                        MARAS_LOG_ERROR("[Matchmaking:RetryMemoryTasks] Exception in memory task callback: {}", e.what());
                    }
                });
            });
        }
    }

    void MatchmakingService::FinishMemoryBatch(std::shared_ptr<MemoryBatch> batch) {
        if (!batch) return;

        bool anyApplied = false;
        for (auto& task : batch->tasks) {
            if (task.completed && !task.failed) {
                if (ApplyMatch(task.result)) {
                    anyApplied = true;
                    MARAS_LOG_INFO("[Matchmaking:FinishMemoryBatch] Successfully applied relationship change for {} (0x{:08X}) <-> {} (0x{:08X}) | Action: {} -> {}",
                                   task.result.actorAName, task.result.actorAFormID, task.result.actorBName, task.result.actorBFormID,
                                   task.result.action, task.result.targetStatus);
                    IngestMemories(task.generatedMemories, task.result.actorAFormID, task.result.actorBFormID);
                } else {
                    MARAS_LOG_WARN("[Matchmaking:FinishMemoryBatch] ApplyMatch failed for {} <-> {}",
                                   task.result.actorAName, task.result.actorBName);
                }
            } else {
                MARAS_LOG_WARN("[Matchmaking:FinishMemoryBatch] Skipping relationship change for {} <-> {} (Chain cancelled or failed). Status unchanged.",
                               task.result.actorAName, task.result.actorBName);
            }
        }
        if (anyApplied) {
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
        }
        MARAS_LOG_INFO("[Matchmaking:FinishMemoryBatch] Memory batch complete.");
    }

    void MatchmakingService::IngestMemories(const std::vector<GeneratedMemoryEntry>& memories, RE::FormID actorA, RE::FormID actorB) {
        for (const auto& mem : memories) {
            RE::FormID targetActor = mem.actorFormID != 0 ? mem.actorFormID : actorA;
            RE::FormID relatedActor = (targetActor == actorA) ? actorB : actorA;

            AddActorMemory(
                targetActor,
                mem.content,
                mem.importanceScore,
                mem.type,
                mem.emotion,
                mem.location,
                mem.tags,
                relatedActor
            );
        }
    }

    std::vector<MatchmakingResult> MatchmakingService::ParseLLMResponse(const std::string& response) {
        std::vector<MatchmakingResult> results;
        if (response.empty()) return results;

        nlohmann::json resp = JsonService::ParseLLMJson(response);
        const auto* matchesArray = FindJsonArray(resp, "matches");
        if (!matchesArray) {
            MARAS_LOG_WARN("[Matchmaking] Failed to parse LLM matches array from response (even after sanitization and repair): {}", response);
            return results;
        }

        for (const auto& item : *matchesArray) {
            if (!item.is_object()) continue;

            MatchmakingResult r;
            r.actorAName = GetJsonString(item, "actorA");
            if (r.actorAName.empty()) r.actorAName = GetJsonString(item, "actor_a");
            if (r.actorAName.empty()) r.actorAName = GetJsonString(item, "target");

            r.actorBName = GetJsonString(item, "actorB");
            if (r.actorBName.empty()) r.actorBName = GetJsonString(item, "actor_b");
            if (r.actorBName.empty()) r.actorBName = GetJsonString(item, "partner");
            if (r.actorBName.empty()) r.actorBName = GetJsonString(item, "candidate");

            std::string act = StringUtils::ToLowerStd(GetJsonString(item, "action", "new"));
            if (act.find("create") != std::string::npos || act.find("new") != std::string::npos) {
                act = "new";
            } else if (act.find("advance") != std::string::npos) {
                act = "advance";
            } else if (act.find("breakup") != std::string::npos || act.find("break") != std::string::npos) {
                act = "breakup";
            } else if (act.find("open") != std::string::npos) {
                act = "open";
            } else if (act.find("close") != std::string::npos) {
                act = "close";
            }

            r.action = act;
            r.targetStatus = StringUtils::ToLowerStd(GetJsonString(item, "targetStatus", "dating"));
            r.memory = GetJsonString(item, "memory");
            r.isCheating = GetJsonBool(item, "cheating", GetJsonBool(item, "isCheating", false));

            if (!r.actorAName.empty() && !r.actorBName.empty()) {
                results.push_back(r);
            }
        }

        return results;
    }

    void MatchmakingService::ResolveFormIDs(MatchmakingResult& result) {
        auto& index = NPCIndex::GetSingleton();
        if (result.actorAFormID == 0 && !result.actorAName.empty()) {
            if (auto entry = index.FindByName(result.actorAName, NPCFilter::AdultOnly)) {
                result.actorAFormID = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
            }
        }
        if (result.actorBFormID == 0 && !result.actorBName.empty()) {
            if (auto entry = index.FindByName(result.actorBName, NPCFilter::AdultOnly)) {
                result.actorBFormID = entry->refFormID != 0 ? entry->refFormID : entry->baseFormID;
            }
        }
    }

    bool MatchmakingService::ValidateCandidate(MatchmakingResult& result) {
        ResolveFormIDs(result);

        if (result.actorAFormID == 0 || result.actorBFormID == 0) {
            result.skipped = true;
            result.skipReason = "Could not resolve FormIDs for names: " + result.actorAName + " / " + result.actorBName;
            return false;
        }

        if (result.actorAFormID == result.actorBFormID) {
            result.skipped = true;
            result.skipReason = "Actor cannot be paired with themselves";
            return false;
        }

        if (result.actorAFormID == kPlayerFormID || result.actorBFormID == kPlayerFormID) {
            result.skipped = true;
            result.skipReason = "Player character is excluded from automated matchmaking";
            return false;
        }

        auto& mgr = NPCRelationshipManager::GetSingleton();
        const auto& settings = Settings::GetSingleton();

        if (result.action == "new" || result.action == "advance") {
            std::string familyReason;
            if (mgr.AreFamilyMembers(result.actorAFormID, result.actorBFormID, &familyReason)) {
                result.skipped = true;
                result.skipReason = familyReason.empty() ? "Actors are family members" : familyReason;
                return false;
            }

            int sexA = ActorSvc::GetSex(result.actorAFormID);
            int sexB = ActorSvc::GetSex(result.actorBFormID);
            if (!IsPairOrientationAllowed(sexA, sexB, settings.matchmakingOrientation)) {
                result.skipped = true;
                result.skipReason = "Pair does not match the configured matchmaking orientation setting";
                return false;
            }

            RelationshipStatus targetSt = Utils::StringToRelationshipStatus(result.targetStatus);
            if (targetSt == RelationshipStatus::Candidate || targetSt == RelationshipStatus::Unknown) {
                targetSt = RelationshipStatus::Dating;
            }

            if (!settings.matchmakingAllowCheating && targetSt == RelationshipStatus::Lover) {
                if (IsPairCheating(result.actorAFormID, result.actorBFormID, "lover")) {
                    result.skipped = true;
                    result.skipReason = "Cheating is disabled in matchmaking settings";
                    return false;
                }
            }

            std::string reason;
            if (!mgr.CanFormRelationship(result.actorAFormID, result.actorBFormID, targetSt, &reason)) {
                result.skipped = true;
                result.skipReason = reason;
                return false;
            }

            return true;
        }
        else if (result.action == "breakup") {
            if (!settings.matchmakingAllowBreakups) {
                result.skipped = true;
                result.skipReason = "Breakups are disabled in matchmaking settings";
                return false;
            }

            auto currentStatus = mgr.GetRelationshipStatus(result.actorAFormID, result.actorBFormID);
            auto [targetStr, targetEnum] = GetBreakupTarget(currentStatus);
            if (targetEnum == RelationshipStatus::Unknown) {
                result.skipped = true;
                result.skipReason = "No active relationship to break up";
                return false;
            }
            result.targetStatus = targetStr;
            return true;
        }
        else if (result.action == "open") {
            if (!settings.matchmakingAllowOpenClose) {
                result.skipped = true;
                result.skipReason = "Open/close modifications are disabled in matchmaking settings";
                return false;
            }

            if (!mgr.CanOpenRelationship(result.actorAFormID, result.actorBFormID)) {
                result.skipped = true;
                result.skipReason = "Pair is not eligible to be opened";
                return false;
            }

            result.targetStatus = "open";
            return true;
        }
        else if (result.action == "close") {
            if (!settings.matchmakingAllowOpenClose) {
                result.skipped = true;
                result.skipReason = "Open/close modifications are disabled in matchmaking settings";
                return false;
            }

            if (!mgr.CanCloseRelationship(result.actorAFormID, result.actorBFormID)) {
                result.skipped = true;
                result.skipReason = "Pair is not eligible to be closed";
                return false;
            }

            result.targetStatus = "closed";
            return true;
        }

        result.skipped = true;
        result.skipReason = "Unknown action: " + result.action;
        return false;
    }

    bool MatchmakingService::ApplyMatch(MatchmakingResult& result) {
        auto& mgr = NPCRelationshipManager::GetSingleton();
        bool success = false;

        if (result.action == "new" || result.action == "advance") {
            RelationshipStatus targetSt = Utils::StringToRelationshipStatus(result.targetStatus);
            if (targetSt == RelationshipStatus::Candidate || targetSt == RelationshipStatus::Unknown) {
                targetSt = RelationshipStatus::Dating;
            }
            success = mgr.PromoteToStatusWith(result.actorAFormID, result.actorBFormID, targetSt);
            if (success && result.action == "new" && targetSt == RelationshipStatus::Lover &&
                result.isCheating && result.affairStage == AffairStage::Talking) {
                if (mgr.IsPlayerActiveMainPartner(result.actorAFormID) && !mgr.GetPairIsOpen(result.actorAFormID, kPlayerFormID)) {
                    mgr.SetCheatingAffairStage(result.actorAFormID, kPlayerFormID, result.actorBFormID, AffairStage::Talking);
                }
                if (mgr.IsPlayerActiveMainPartner(result.actorBFormID) && !mgr.GetPairIsOpen(result.actorBFormID, kPlayerFormID)) {
                    mgr.SetCheatingAffairStage(result.actorBFormID, kPlayerFormID, result.actorAFormID, AffairStage::Talking);
                }
            }
        }
        else if (result.action == "breakup") {
            auto currentStatus = mgr.GetRelationshipStatus(result.actorAFormID, result.actorBFormID);
            auto [defaultStr, defaultEnum] = GetBreakupTarget(currentStatus);
            RelationshipStatus targetEnum = Utils::StringToRelationshipStatus(result.targetStatus);
            std::string targetStr = StringUtils::ToLowerStd(result.targetStatus);
            if (targetEnum != RelationshipStatus::Divorced &&
                targetEnum != RelationshipStatus::Jilted &&
                targetEnum != RelationshipStatus::ExDating &&
                targetEnum != RelationshipStatus::ExLover) {
                targetEnum = defaultEnum;
                targetStr = defaultStr;
            }
            switch (targetEnum) {
                case RelationshipStatus::Divorced:
                    success = mgr.PromoteToDivorced(result.actorAFormID, result.actorBFormID);
                    break;
                case RelationshipStatus::Jilted:
                    success = mgr.PromoteToJilted(result.actorAFormID, result.actorBFormID);
                    break;
                case RelationshipStatus::ExDating:
                    success = mgr.PromoteToExDating(result.actorAFormID, result.actorBFormID);
                    break;
                case RelationshipStatus::ExLover:
                    success = mgr.PromoteToExLover(result.actorAFormID, result.actorBFormID);
                    if (success && result.postBreakupIntent != PartnerIntentType::None &&
                        Settings::GetSingleton().playerPartnerAgencyEnableForceGreets &&
                        mgr.GetPartnerPendingIntent(result.actorAFormID).type == PartnerIntentType::None) {
                        mgr.SetPartnerPendingIntent(result.actorAFormID, result.postBreakupIntent, result.actorBFormID);
                        MARAS_LOG_INFO("[Matchmaking:ApplyMatch] Queued post-breakup confession intent '{}' for '{}' (0x{:08X}) regarding ex-lover 0x{:08X}",
                                       Utils::PartnerIntentTypeToString(result.postBreakupIntent),
                                       result.actorAName, result.actorAFormID, result.actorBFormID);
                    }
                    break;
                default:
                    success = false;
                    break;
            }
            if (success) {
                result.targetStatus = targetStr;
            }
        }
        else if (result.action == "open") {
            mgr.SetPairIsOpen(result.actorAFormID, result.actorBFormID, true);
            result.targetStatus = "open";
            success = true;
        }
        else if (result.action == "close") {
            mgr.SetPairIsOpen(result.actorAFormID, result.actorBFormID, false);
            result.targetStatus = "closed";
            success = true;
        }

        result.applied = success;
        if (success) {
            float today = GetCurrentGameDay();
            RecordNpcCooldown(result.actorAFormID, today);
            RecordNpcCooldown(result.actorBFormID, today);
        }

        return success;
    }

    void MatchmakingService::ProcessScheduledResults(const std::string& response, bool isScheduledScan) {
        auto results = ParseLLMResponse(response);
        MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Processing {} candidate matchmaking results from broad LLM scan (isScheduled: {})",
                       results.size(), isScheduledScan);

        std::set<RE::FormID> seenActorsInBatch;
        std::vector<MatchmakingResult> candidatesToVerify;

        for (auto& r : results) {
            ResolveFormIDs(r);

            // Skip if either actor already received an operation in this batch
            if (r.actorAFormID != 0 && seenActorsInBatch.count(r.actorAFormID) > 0) {
                MARAS_LOG_WARN("[Matchmaking:ProcessScheduledResults] Skipping match because actor 0x{:08X} already received a change in this batch", r.actorAFormID);
                continue;
            }
            if (r.actorBFormID != 0 && seenActorsInBatch.count(r.actorBFormID) > 0) {
                MARAS_LOG_WARN("[Matchmaking:ProcessScheduledResults] Skipping match because actor 0x{:08X} already received a change in this batch", r.actorBFormID);
                continue;
            }

            float today = GetCurrentGameDay();
            if (IsNpcOnCooldown(r.actorAFormID, today)) {
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Skipping world scan match: {} (0x{:08X}) is on relationship change cooldown ({:.1f} days remaining)",
                               r.actorAName, r.actorAFormID, GetNpcCooldownRemaining(r.actorAFormID, today));
                continue;
            }
            if (IsNpcOnCooldown(r.actorBFormID, today)) {
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Skipping world scan match: {} (0x{:08X}) is on relationship change cooldown ({:.1f} days remaining)",
                               r.actorBName, r.actorBFormID, GetNpcCooldownRemaining(r.actorBFormID, today));
                continue;
            }
            [[maybe_unused]] const auto& settings = Settings::GetSingleton();
            const auto& mgr = NPCRelationshipManager::GetSingleton();
            auto isExcludedPlayerPartner = [&](RE::FormID id) {
                if (!mgr.IsPlayerActiveMainPartner(id)) return false;
                return true;

            };
            if (isExcludedPlayerPartner(r.actorAFormID)) {
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Skipping match: {} (0x{:08X}) is player's active partner (closed or excluded by settings)",
                               r.actorAName, r.actorAFormID);
                continue;
            }
            if (isExcludedPlayerPartner(r.actorBFormID)) {
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Skipping match: {} (0x{:08X}) is player's active partner (closed or excluded by settings)",
                               r.actorBName, r.actorBFormID);
                continue;
            }

            r.isCheating = IsPairCheating(r.actorAFormID, r.actorBFormID, r.targetStatus);

            if (ValidateCandidate(r)) {
                if (r.actorAFormID != 0) seenActorsInBatch.insert(r.actorAFormID);
                if (r.actorBFormID != 0) seenActorsInBatch.insert(r.actorBFormID);
                candidatesToVerify.push_back(r);
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Candidate passed mechanical checks: {} (0x{:08X}) <-> {} (0x{:08X}) | Action: {} -> {} (Cheating: {})",
                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID, r.action, r.targetStatus, r.isCheating);
            } else {
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Skipped scheduled match: {} <-> {} | Reason: {}",
                               r.actorAName, r.actorBName, r.skipReason);
            }
        }

        // Trigger batched Tier 2 narrative verification for mechanically valid candidates
        if (!candidatesToVerify.empty()) {
            [[maybe_unused]] const auto& settings = Settings::GetSingleton();
            if (!settings.matchmakingEvaluationStep) {
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Evaluation step disabled. Advancing {} candidate pair(s) directly to memory/apply pipeline",
                               candidatesToVerify.size());
                StartMemoryBatch(std::move(candidatesToVerify), isScheduledScan);
            } else {
                MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Dispatching batched Tier 2 narrative verification for {} candidate pair(s) (isScheduled: {})",
                               candidatesToVerify.size(), isScheduledScan);
                StartVerificationBatch(std::move(candidatesToVerify), isScheduledScan);
            }
        }
    }

    void MatchmakingService::ProcessManualResults(const std::string& response, RE::FormID targetNpcFormID) {
        auto results = ParseLLMResponse(response);
        MARAS_LOG_INFO("[Matchmaking:ProcessManualResults] Processing {} manual matchmaking results for target NPC 0x{:08X}",
                       results.size(), targetNpcFormID);

        std::vector<MatchmakingResult> dedupedResults;
        std::set<RE::FormID> seenCandidateFormIDs;
        const std::string targetName = ActorSvc::GetName(targetNpcFormID);

        for (auto& r : results) {
            ResolveFormIDs(r);

            bool isA = (r.actorAFormID == targetNpcFormID) ||
                       (r.actorAFormID == 0 && !targetName.empty() && StringUtils::CaseInsensitiveEquals(r.actorAName, targetName));
            bool isB = (r.actorBFormID == targetNpcFormID) ||
                       (r.actorBFormID == 0 && !targetName.empty() && StringUtils::CaseInsensitiveEquals(r.actorBName, targetName));

            if (isB && !isA) {
                std::swap(r.actorAFormID, r.actorBFormID);
                std::swap(r.actorAName, r.actorBName);
                isA = true;
            }

            if (!isA) {
                MARAS_LOG_WARN("[Matchmaking:ProcessManualResults] Discarding suggestion '{}' (0x{:08X}) <-> '{}' (0x{:08X}) that does not involve target NPC 0x{:08X} ({})",
                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID, targetNpcFormID, targetName);
                continue;
            }

            r.actorAFormID = targetNpcFormID;
            if (!targetName.empty() && targetName != "None") {
                r.actorAName = targetName;
            }

            if (r.actorBFormID == 0 || r.actorBFormID == targetNpcFormID) {
                MARAS_LOG_WARN("[Matchmaking:ProcessManualResults] Discarding suggestion for target 0x{:08X} with unresolved or self partner '{}' (0x{:08X})",
                               targetNpcFormID, r.actorBName, r.actorBFormID);
                continue;
            }

            if (seenCandidateFormIDs.count(r.actorBFormID) > 0) {
                MARAS_LOG_WARN("[Matchmaking:ProcessManualResults] Discarding duplicate/contradictory suggestion for candidate FormID 0x{:08X} ({})",
                               r.actorBFormID, r.actorBName);
                continue;
            }

            if (!ValidateCandidate(r)) {
                MARAS_LOG_INFO("[Matchmaking:ProcessManualResults] Skipped invalid suggestion {} <-> {}: {}",
                               r.actorAName, r.actorBName, r.skipReason);
                continue;
            }

            seenCandidateFormIDs.insert(r.actorBFormID);
            r.isCheating = IsPairCheating(r.actorAFormID, r.actorBFormID, r.targetStatus);

            dedupedResults.push_back(r);
        }

        PrismaUIDataBridge::GetSingleton().SendMatchmakingResults(dedupedResults, false);
    }

    void MatchmakingService::ProcessGlobalManualResults(const std::string& response) {
        auto results = ParseLLMResponse(response);
        MARAS_LOG_INFO("[Matchmaking:ProcessGlobalManualResults] Processing {} global matchmaking results for UI display",
                       results.size());

        std::vector<MatchmakingResult> dedupedResults;
        std::set<RE::FormID> seenActorsInBatch;

        for (auto& r : results) {
            ResolveFormIDs(r);

            if (r.actorAFormID == 0 || r.actorBFormID == 0) {
                MARAS_LOG_WARN("[Matchmaking:ProcessGlobalManualResults] Could not resolve FormIDs for '{}' (0x{:08X}) / '{}' (0x{:08X})",
                               r.actorAName, r.actorAFormID, r.actorBName, r.actorBFormID);
                continue;
            }

            // Deduplicate: enforce at most 1 operation per actor across the entire batch
            if (seenActorsInBatch.count(r.actorAFormID) > 0 || seenActorsInBatch.count(r.actorBFormID) > 0) {
                MARAS_LOG_WARN("[Matchmaking:ProcessGlobalManualResults] Discarding duplicate/contradictory suggestion for actors 0x{:08X} / 0x{:08X}",
                               r.actorAFormID, r.actorBFormID);
                continue;
            }

            if (!ValidateCandidate(r)) {
                MARAS_LOG_INFO("[Matchmaking:ProcessGlobalManualResults] Skipped invalid global suggestion {} <-> {}: {}",
                               r.actorAName, r.actorBName, r.skipReason);
                continue;
            }

            r.isCheating = IsPairCheating(r.actorAFormID, r.actorBFormID, r.targetStatus);

            seenActorsInBatch.insert(r.actorAFormID);
            seenActorsInBatch.insert(r.actorBFormID);
            dedupedResults.push_back(r);
        }

        PrismaUIDataBridge::GetSingleton().SendMatchmakingResults(dedupedResults, true);
    }

    void MatchmakingService::RecordNpcCooldown(RE::FormID actorID, float currentDay) {
        if (actorID == 0 || actorID == kPlayerFormID) return;
        RE::FormID ref = NPCIndex::GetSingleton().GetRefID(actorID);
        RE::FormID canonicalId = (ref != 0) ? ref : actorID;
        npcLastMatchmakingChangeDay_[canonicalId] = currentDay;
        MARAS_LOG_INFO("[Matchmaking:RecordNpcCooldown] NPC 0x{:08X} placed on relationship change cooldown at game day {:.2f}",
                       canonicalId, currentDay);
    }

    bool MatchmakingService::IsNpcOnCooldown(RE::FormID actorID, float currentDay) const {
        if (actorID == 0 || actorID == kPlayerFormID) return false;
        const auto& settings = Settings::GetSingleton();
        if (settings.matchmakingNpcCooldownDays <= 0) return false;

        RE::FormID ref = NPCIndex::GetSingleton().GetRefID(actorID);
        RE::FormID canonicalId = (ref != 0) ? ref : actorID;

        auto it = npcLastMatchmakingChangeDay_.find(canonicalId);
        if (it == npcLastMatchmakingChangeDay_.end()) {
            if (canonicalId != actorID) {
                it = npcLastMatchmakingChangeDay_.find(actorID);
            }
            if (it == npcLastMatchmakingChangeDay_.end()) return false;
        }

        float elapsed = currentDay - it->second;
        return elapsed < static_cast<float>(settings.matchmakingNpcCooldownDays);
    }

    float MatchmakingService::GetNpcCooldownRemaining(RE::FormID actorID, float currentDay) const {
        if (actorID == 0 || actorID == kPlayerFormID) return 0.0f;
        const auto& settings = Settings::GetSingleton();
        if (settings.matchmakingNpcCooldownDays <= 0) return 0.0f;

        RE::FormID ref = NPCIndex::GetSingleton().GetRefID(actorID);
        RE::FormID canonicalId = (ref != 0) ? ref : actorID;

        auto it = npcLastMatchmakingChangeDay_.find(canonicalId);
        if (it == npcLastMatchmakingChangeDay_.end()) {
            if (canonicalId != actorID) {
                it = npcLastMatchmakingChangeDay_.find(actorID);
            }
            if (it == npcLastMatchmakingChangeDay_.end()) return 0.0f;
        }

        float elapsed = currentDay - it->second;
        float remaining = static_cast<float>(settings.matchmakingNpcCooldownDays) - elapsed;
        return remaining > 0.0f ? remaining : 0.0f;
    }

    bool MatchmakingService::EvaluatePartnerHomeArrival(RE::BGSLocation* newLocation) {
        const auto& settings = Settings::GetSingleton();
        auto* playerActor = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* currentCell = playerActor ? playerActor->GetParentCell() : nullptr;
        RE::BGSLocation* cellLoc = newLocation ? newLocation : (currentCell ? currentCell->GetLocation() : nullptr);
        RE::FormID cellID = currentCell ? currentCell->GetFormID() : 0;
        RE::FormID cellLocID = cellLoc ? cellLoc->GetFormID() : 0;
        RE::FormID locationKey = (cellLocID != 0) ? cellLocID : cellID;
        std::string currentCellName = (currentCell && currentCell->GetName() && currentCell->GetName()[0] != '\0')
                                          ? currentCell->GetName()
                                          : "UnnamedCell";
        std::string currentLocName = (cellLoc && cellLoc->GetName() && cellLoc->GetName()[0] != '\0')
                                         ? cellLoc->GetName()
                                         : (currentCellName != "UnnamedCell" ? currentCellName : "Home");

        if (!settings.playerPartnerCheatingAgencyEnabled && !settings.playerPartnerOpenRelAgencyEnabled) {
            MARAS_LOG_INFO("[Matchmaking:HomeArrival] Skipped at '{}' (loc=0x{:08X}, cell=0x{:08X}): both playerPartnerCheatingAgencyEnabled and playerPartnerOpenRelAgencyEnabled are disabled.", currentLocName, cellLocID, cellID);
            return false;
        }
        if (!settings.playerPartnerAgencyEnableHomeArrivalEvents) {
            MARAS_LOG_INFO("[Matchmaking:HomeArrival] Skipped at '{}' (loc=0x{:08X}, cell=0x{:08X}): playerPartnerAgencyEnableHomeArrivalEvents is disabled.",
                           currentLocName, cellLocID, cellID);
            return false;
        }

        if (!playerActor || playerActor->IsInCombat() || playerActor->IsDead()) {
            MARAS_LOG_INFO("[Matchmaking:HomeArrival] Skipped at '{}' (loc=0x{:08X}, cell=0x{:08X}): player is null, in combat ({}), or dead ({}).",
                           currentLocName, cellLocID, cellID,
                           playerActor ? playerActor->IsInCombat() : false,
                           playerActor ? playerActor->IsDead() : false);
            return false;
        }

        if (!currentCell && !cellLoc) {
            MARAS_LOG_INFO("[Matchmaking:HomeArrival] Skipped: both currentCell and cellLoc are null.");
            return false;
        }

        auto& mgr = NPCRelationshipManager::GetSingleton();
        auto& affSvc = AffectionService::GetSingleton();
        auto& deceasedReg = DeceasedRegistry::GetSingleton();
        auto& idx = NPCIndex::GetSingleton();
        auto& houseSvc = PlayerHouseService::GetSingleton();
        auto& homeCellSvc = HomeCellService::GetSingleton();

        std::vector<RE::FormID> playerMainPartners;
        std::unordered_set<RE::FormID> seenPartners;
        auto collectPartners = [&](const std::vector<RE::FormID>& list) {
            for (RE::FormID id : list) {
                if (id != 0 && id != kPlayerFormID && seenPartners.insert(id).second) {
                    playerMainPartners.push_back(id);
                }
            }
        };
        collectPartners(mgr.GetAllMarried(kPlayerFormID));
        collectPartners(mgr.GetAllEngaged(kPlayerFormID));
        collectPartners(mgr.GetAllDating(kPlayerFormID));
        if (playerMainPartners.empty()) {
            MARAS_LOG_DEBUG("[Matchmaking:HomeArrival] Skipped at '{}' (loc=0x{:08X}, cell=0x{:08X}): player has 0 active main partners.",
                            currentLocName, cellLocID, cellID);
            return false;
        }

        MARAS_LOG_INFO("[Matchmaking:HomeArrival] Evaluating arrival at '{}' (locID=0x{:08X}, cell='{}' [0x{:08X}], locationKey=0x{:08X}, interior={}) across {} active player main partner(s)",
                       currentLocName, cellLocID, currentCellName, cellID, locationKey,
                       currentCell ? currentCell->IsInteriorCell() : false, playerMainPartners.size());

        auto isLocationOrParentMatch = [](RE::BGSLocation* loc, RE::FormID targetLocID) -> bool {
            if (!loc || targetLocID == 0) return false;
            int depth = 0;
            for (auto* cur = loc; cur != nullptr && depth < 8; cur = cur->parentLoc, ++depth) {
                if (cur->GetFormID() == targetLocID) return true;
            }
            return false;
        };

        std::vector<RE::FormID> residentPartners;
        for (RE::FormID partnerID : playerMainPartners) {
            std::string partnerName = ActorSvc::GetName(partnerID);
            if (deceasedReg.IsDeceased(partnerID)) {
                MARAS_LOG_INFO("[Matchmaking:HomeArrival] Partner '{}' (0x{:08X}): skipped (deceased)", partnerName, partnerID);
                continue;
            }
            RE::Actor* livePartner = idx.GetActorFromFormId(partnerID);
            if (livePartner) {
                if (livePartner->IsDead() || livePartner->IsDeleted() || livePartner->IsDisabled()) {
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival] Partner '{}' (0x{:08X}): skipped (dead/deleted/disabled)", partnerName, partnerID);
                    continue;
                }
                if (Utils::IsPlayerFollower(livePartner)) {
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival] Partner '{}' (0x{:08X}): skipped (active player follower)", partnerName, partnerID);
                    continue;
                }
                if (livePartner->IsInCombat()) {
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival] Partner '{}' (0x{:08X}): skipped (in combat)", partnerName, partnerID);
                    continue;
                }
            }

            bool residesHere = false;
            std::string matchSource;
            RE::FormID tenantHouseID = houseSvc.GetTenantHouseFormID(partnerID);
            if (tenantHouseID != 0) {
                if (tenantHouseID == cellID || isLocationOrParentMatch(cellLoc, tenantHouseID)) {
                    residesHere = true;
                    matchSource = std::format("assigned player house 0x{:08X}", tenantHouseID);
                } else {
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival] Partner '{}' (0x{:08X}) assigned to player house 0x{:08X} -> does NOT match current loc=0x{:08X} / cell=0x{:08X}",
                                   partnerName, partnerID, tenantHouseID, cellLocID, cellID);
                }
            } else {
                auto matchesOriginalHomeForm = [&](RE::FormID homeFormID) -> bool {
                    if (homeFormID == 0) return false;
                    if (homeFormID == cellID || isLocationOrParentMatch(cellLoc, homeFormID)) {
                        return true;
                    }
                    if (auto* hCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(homeFormID)) {
                        if (auto* hLoc = hCell->GetLocation(); hLoc && !IsSettlementLevelKeyword(hLoc)) {
                            if (isLocationOrParentMatch(cellLoc, hLoc->GetFormID())) {
                                return true;
                            }
                        }
                    }
                    return false;
                };

                // 1. HomeCellService original interior home cell / location
                RE::FormID homeCellID = homeCellSvc.GetNpcHome(partnerID);
                if (matchesOriginalHomeForm(homeCellID)) {
                    residesHere = true;
                    matchSource = std::format("HomeCellService original home 0x{:08X}", homeCellID);
                }

                // 2. NPCProfile currentHome / originalHome / homeMarker
                const auto* prof = mgr.GetNPCProfile(partnerID);
                if (!residesHere && prof) {
                    if (prof->currentHome.has_value() && matchesOriginalHomeForm(prof->currentHome.value())) {
                        residesHere = true;
                        matchSource = std::format("NPCProfile.currentHome 0x{:08X}", prof->currentHome.value());
                    } else if (prof->originalHome.has_value() && matchesOriginalHomeForm(prof->originalHome.value())) {
                        residesHere = true;
                        matchSource = std::format("NPCProfile.originalHome 0x{:08X}", prof->originalHome.value());
                    } else if (prof->homeMarker.has_value() && prof->homeMarker.value() != 0) {
                        if (auto* markerRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(prof->homeMarker.value())) {
                            if (auto* mCell = markerRef->GetParentCell(); mCell && matchesOriginalHomeForm(mCell->GetFormID())) {
                                residesHere = true;
                                matchSource = std::format("NPCProfile.homeMarker 0x{:08X} (cell 0x{:08X})", prof->homeMarker.value(), mCell->GetFormID());
                            } else if (auto* mLoc = markerRef->GetCurrentLocation(); mLoc && !IsSettlementLevelKeyword(mLoc) &&
                                       isLocationOrParentMatch(cellLoc, mLoc->GetFormID())) {
                                residesHere = true;
                                matchSource = std::format("NPCProfile.homeMarker 0x{:08X} (loc 0x{:08X})", prof->homeMarker.value(), mLoc->GetFormID());
                            }
                        }
                    }
                }

                // 3. Static NPCIndex original editorLocationID & placementCellID
                auto entryOpt = idx.FindByFormID(partnerID);
                if (!residesHere && entryOpt.has_value()) {
                    if (entryOpt->editorLocationID != 0) {
                        if (auto* edLoc = RE::TESForm::LookupByID<RE::BGSLocation>(entryOpt->editorLocationID);
                            edLoc && !IsSettlementLevelKeyword(edLoc) && isLocationOrParentMatch(cellLoc, entryOpt->editorLocationID)) {
                            residesHere = true;
                            matchSource = std::format("NPCIndex.editorLocationID 0x{:08X}", entryOpt->editorLocationID);
                        }
                    }
                    if (!residesHere && entryOpt->placementCellID != 0) {
                        if (auto* pCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(entryOpt->placementCellID);
                            pCell && pCell->IsInteriorCell() && matchesOriginalHomeForm(entryOpt->placementCellID)) {
                            residesHere = true;
                            matchSource = std::format("NPCIndex.placementCellID 0x{:08X}", entryOpt->placementCellID);
                        }
                    }
                }

                if (!residesHere) {
                    RE::FormID profCur = (prof && prof->currentHome.has_value()) ? prof->currentHome.value() : 0;
                    RE::FormID profOrig = (prof && prof->originalHome.has_value()) ? prof->originalHome.value() : 0;
                    RE::FormID profMark = (prof && prof->homeMarker.has_value()) ? prof->homeMarker.value() : 0;
                    RE::FormID idxEdLoc = entryOpt.has_value() ? entryOpt->editorLocationID : 0;
                    RE::FormID idxPlaceCell = entryOpt.has_value() ? entryOpt->placementCellID : 0;
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival] Partner '{}' (0x{:08X}, unassigned to player house) does NOT reside at loc=0x{:08X}/cell=0x{:08X} (homeCellSvc=0x{:08X}, profCurrent=0x{:08X}, profOrig=0x{:08X}, profMarker=0x{:08X}, idxEdLoc=0x{:08X}, idxPlaceCell=0x{:08X})",
                                   partnerName, partnerID, cellLocID, cellID, homeCellID, profCur, profOrig, profMark, idxEdLoc, idxPlaceCell);
                }
            }

            if (residesHere) {
                MARAS_LOG_INFO("[Matchmaking:HomeArrival] Partner '{}' (0x{:08X}) RESIDES at '{}' via {}",
                               partnerName, partnerID, currentLocName, matchSource);
                residentPartners.push_back(partnerID);
            }
        }

        if (residentPartners.empty()) {
            MARAS_LOG_INFO("[Matchmaking:HomeArrival] Result: 0 active player partners reside at '{}' (locationKey=0x{:08X}).",
                           currentLocName, locationKey);
            return false;
        }

        float currentGameDay = GetCurrentGameDay();

        // Step 0: Lazy TTL Check for Resident Partners' Pending Intents (>= 2.0x L days)
        for (RE::FormID partnerID : residentPartners) {
            std::string partnerName = ActorSvc::GetName(partnerID);
            PendingPartnerIntent intent = mgr.GetPartnerPendingIntent(partnerID);
            if (intent.type != PartnerIntentType::None) {
                float intentAge = currentGameDay - intent.createdGameDay;
                float intentTTL = settings.GetPartnerIntentTTLDays();
                if (intentAge >= intentTTL) {
                    if (intent.type == PartnerIntentType::ConfessTalking) {
                        mgr.ClearPartnerIntent(partnerID);
                        if (intent.relatedLoverID != 0) {
                            mgr.SetCheatingAffairStage(partnerID, kPlayerFormID, intent.relatedLoverID, AffairStage::Physical);
                        }
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Step0] Expired ConfessTalking intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> escalated affair with 0x{:08X} to Physical",
                                       intentAge, intentTTL, partnerName, partnerID, intent.relatedLoverID);
                    } else if (intent.type == PartnerIntentType::ConfessPhysical) {
                        mgr.ClearPartnerIntent(partnerID);
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Step0] Expired ConfessPhysical intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> cleared",
                                       intentAge, intentTTL, partnerName, partnerID);
                    } else if (intent.type == PartnerIntentType::SuggestOpen) {
                        mgr.ClearPartnerIntent(partnerID);
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Step0] Expired SuggestOpen intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> cleared",
                                       intentAge, intentTTL, partnerName, partnerID);
                    } else if (intent.type == PartnerIntentType::SuggestClose) {
                        mgr.ClearPartnerIntent(partnerID);
                        affSvc.SetPermanentAffection(partnerID, affSvc.GetPermanentAffection(partnerID) - 12, true);
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Step0] Expired SuggestClose intent (age={:.1f}d >= TTL={:.1f}d) for '{}' (0x{:08X}) -> cleared and applied -12 permanent affection",
                                       intentAge, intentTTL, partnerName, partnerID);
                    }
                } else {
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival:Step0] Resident partner '{}' (0x{:08X}) has non-expired pending intent '{}' (age={:.1f}d < TTL={:.1f}d)",
                                   partnerName, partnerID, Utils::PartnerIntentTypeToString(intent.type), intentAge, intentTTL);
                }
            }
        }

        // Priority #1: Non-Expired PendingPartnerIntent (100% Trigger - No Dice Roll, Blocks Side-Lover Scenarios)
        if (settings.playerPartnerAgencyEnableForceGreets) {
            for (RE::FormID partnerID : residentPartners) {
                PendingPartnerIntent intent = mgr.GetPartnerPendingIntent(partnerID);
                if (intent.type == PartnerIntentType::None) continue;

                std::string scenario;
                std::string stageStr = "none";
                switch (intent.type) {
                    case PartnerIntentType::SuggestOpen:
                        if (!settings.playerPartnerOpenRelAgencyEnabled) continue;
                        scenario = "intent_suggest_open";
                        break;
                    case PartnerIntentType::SuggestClose:
                        if (!settings.playerPartnerOpenRelAgencyEnabled) continue;
                        scenario = "intent_suggest_close";
                        break;
                    case PartnerIntentType::ConfessTalking:
                        if (!settings.playerPartnerCheatingAgencyEnabled) continue;
                        scenario = "intent_confess_talking";
                        stageStr = "talking";
                        break;
                    case PartnerIntentType::ConfessPhysical:
                        if (!settings.playerPartnerCheatingAgencyEnabled) continue;
                        scenario = "intent_confess_physical";
                        stageStr = "physical";
                        break;
                    default:
                        break;
                }
                if (scenario.empty()) continue;

                std::string partnerName = ActorSvc::GetName(partnerID);
                RE::Actor* partnerActor = idx.GetActorFromFormId(partnerID);
                bool partnerInCell = (partnerActor && partnerActor->GetParentCell() == currentCell);
                if (!partnerInCell) {
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority1] Resident partner '{}' (0x{:08X}) has pending intent '{}', but is not currently in the arrived cell (0x{:08X}); deferring force-greet.",
                                   partnerName, partnerID, Utils::PartnerIntentTypeToString(intent.type), cellID);
                    continue;
                }

                std::string loverName = (intent.relatedLoverID != 0) ? ActorSvc::GetName(intent.relatedLoverID) : "";
                bool isOpen = mgr.GetPairIsOpen(partnerID, kPlayerFormID);

                nlohmann::json payload;
                payload["scenario"] = scenario;
                payload["partnerFormId"] = PrismaUIDataBridge::FormIDToString(partnerID);
                payload["partnerName"] = partnerName;
                payload["loverFormId"] = PrismaUIDataBridge::FormIDToString(intent.relatedLoverID);
                payload["loverName"] = loverName;
                payload["affairStage"] = stageStr;
                payload["isOpenRelationship"] = isOpen;
                payload["intentType"] = std::string(Utils::PartnerIntentTypeToString(intent.type));
                payload["targetLocationFormId"] = PrismaUIDataBridge::FormIDToString(locationKey);
                payload["targetLocationName"] = currentLocName;

                if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                    RE::TESForm* senderForm = partnerActor ? static_cast<RE::TESForm*>(partnerActor) : nullptr;
                    std::string payloadStr = SafeJsonDump(payload);
                    SKSE::ModCallbackEvent modEvent("maras_partner_home_arrival_event", payloadStr, 1.0f, senderForm);
                    eventSource->SendEvent(&modEvent);
                }

                MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority1] Fired 100% pending intent scenario '{}' for '{}' (0x{:08X}, lover='{}' [0x{:08X}]) at '{}'; blocking side-lover scenarios for this visit",
                               scenario, partnerName, partnerID, loverName, intent.relatedLoverID, currentLocName);
                return true;
            }
            MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority1] None of the {} resident partner(s) at '{}' have a pending force-greet intent; proceeding to Priority #2 side-lover scenarios.",
                           residentPartners.size(), currentLocName);
        } else {
            MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority1] Force-greets disabled by settings (playerPartnerAgencyEnableForceGreets=false); skipping pending intent check.");
        }

        // Priority #2: Side-Lover Home-Arrival Scenarios (6 Scenarios)
        auto itCool = lastHomeArrivalEventDayByLoc_.find(locationKey);
        if (itCool != lastHomeArrivalEventDayByLoc_.end() &&
            (currentGameDay - itCool->second) < settings.GetHomeArrivalCooldownDays()) {
            MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Location '{}' (key=0x{:08X}) is on home-arrival cooldown ({:.2f}d elapsed since day {:.2f} < {:.2f}d required); skipping side-lover scenarios.",
                           currentLocName, locationKey, currentGameDay - itCool->second, itCool->second, settings.GetHomeArrivalCooldownDays());
            return false;
        }

        thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_real_distribution<float> pctDist(0.0f, 100.0f);
        std::uniform_int_distribution<int> coinDist(0, 1);

        for (RE::FormID partnerID : residentPartners) {
            std::string partnerName = ActorSvc::GetName(partnerID);
            if (RE::Actor* partnerActor = idx.GetActorFromFormId(partnerID)) {
                if (partnerActor->GetCurrentScene() != nullptr) {
                    MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Resident partner '{}' (0x{:08X}) skipped for side-lover scenario: currently in an active scene.",
                                   partnerName, partnerID);
                    continue;
                }
            }
            bool isOpen = mgr.GetPairIsOpen(partnerID, kPlayerFormID);
            RE::FormID activeLoverID = 0;
            AffairStage activeStage = AffairStage::Physical;

            if (isOpen && !settings.playerPartnerOpenRelAgencyEnabled) {
                MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Resident open partner '{}' (0x{:08X}) skipped: playerPartnerOpenRelAgencyEnabled is disabled.", partnerName, partnerID);
                continue;
            }
            if (!isOpen && !settings.playerPartnerCheatingAgencyEnabled) {
                MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Resident closed partner '{}' (0x{:08X}) skipped: playerPartnerCheatingAgencyEnabled is disabled.", partnerName, partnerID);
                continue;
            }
            auto isEligibleSideLover = [&](RE::FormID lid) -> bool {
                if (lid == 0 || lid == kPlayerFormID || deceasedReg.IsDeceased(lid)) {
                    return false;
                }
                if (RE::Actor* loverActor = idx.GetActorFromFormId(lid)) {
                    if (loverActor->IsDead() || loverActor->IsDeleted() || loverActor->IsDisabled()) {
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Side lover '{}' (0x{:08X}) for '{}' (0x{:08X}) skipped: dead/deleted/disabled.",
                                       ActorSvc::GetName(lid), lid, partnerName, partnerID);
                        return false;
                    }
                    if (Utils::IsPlayerFollower(loverActor)) {
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Side lover '{}' (0x{:08X}) for '{}' (0x{:08X}) skipped: active player follower.",
                                       ActorSvc::GetName(lid), lid, partnerName, partnerID);
                        return false;
                    }
                    if (loverActor->IsInCombat()) {
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Side lover '{}' (0x{:08X}) for '{}' (0x{:08X}) skipped: in combat.",
                                       ActorSvc::GetName(lid), lid, partnerName, partnerID);
                        return false;
                    }
                    if (loverActor->GetCurrentScene() != nullptr) {
                        MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Side lover '{}' (0x{:08X}) for '{}' (0x{:08X}) skipped: currently in an active scene.",
                                       ActorSvc::GetName(lid), lid, partnerName, partnerID);
                        return false;
                    }
                }
                return true;
            };
            if (isOpen) {
                for (RE::FormID lid : mgr.GetPartners(partnerID, RelationshipStatus::Lover)) {
                    if (isEligibleSideLover(lid)) {
                        activeLoverID = lid;
                        break;
                    }
                }
            } else {
                for (const auto& rec : mgr.GetCheatingRecords(partnerID, kPlayerFormID)) {
                    if (rec.cheaterID == partnerID && rec.victimID == kPlayerFormID &&
                        rec.affairStatus == AffairStatus::Ongoing && isEligibleSideLover(rec.affairPartnerID)) {
                        activeLoverID = rec.affairPartnerID;
                        activeStage = rec.affairStage;
                        break;
                    }
                }
            }

            if (activeLoverID == 0) {
                MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Resident partner '{}' (0x{:08X}, isOpen={}) has no active side lover or ongoing secret affair; skipping.",
                               partnerName, partnerID, isOpen);
                continue;
            }

            std::string loverName = ActorSvc::GetName(activeLoverID);
            float baseChance = 25.0f;
            float daysSince = std::max(0.0f, affSvc.GetDaysSinceLastAffection(partnerID));
            float lonelyBonus = (daysSince > settings.GetLonelinessDecayStartDays()) ? 15.0f : 0.0f;
            float gameHour = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetHour() : 12.0f;
            float eveningBonus = (gameHour >= 17.0f && gameHour < 22.0f) ? 10.0f : 0.0f;
            float nightBonus = (gameHour >= 22.0f || gameHour < 5.0f) ? 20.0f : 0.0f;
            float triggerChance = baseChance + lonelyBonus + eveningBonus + nightBonus;

            float roll = pctDist(gen);
            bool triggered = (roll < triggerChance);
            MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Resident partner '{}' (0x{:08X}, isOpen={}) with side lover '{}' (0x{:08X}, stage={}) | Base={:.1f}% LonelyBonus={:+.1f}% (daysSince={:.1f}d vs {:.1f}d) EveningBonus={:+.1f}% NightBonus={:+.1f}% (hour={:.1f}) -> TriggerChance={:.1f}% (Roll={:.1f}) => {}",
                           partnerName, partnerID, isOpen, loverName, activeLoverID,
                           Utils::AffairStageToString(activeStage), baseChance, lonelyBonus, daysSince,
                           settings.GetLonelinessDecayStartDays(), eveningBonus, nightBonus, gameHour, triggerChance, roll,
                           triggered ? "TRIGGERED" : "DID NOT ROLL");

            if (!triggered) {
                continue;
            }

            RE::Actor* partnerActor = idx.GetActorFromFormId(partnerID);
            bool partnerInCell = (partnerActor && partnerActor->GetParentCell() == currentCell);
            bool isLetterScenario = partnerInCell ? (coinDist(gen) == 0) : true;
            std::string scenario;
            std::string stageStr = "none";
            if (isOpen) {
                scenario = isLetterScenario ? "open_date_away" : "open_in_house";
            } else if (activeStage == AffairStage::Talking) {
                scenario = isLetterScenario ? "cheating_talking_letter" : "cheating_talking_in_house";
                stageStr = "talking";
            } else {
                scenario = isLetterScenario ? "cheating_physical_letter" : "cheating_physical_in_house";
                stageStr = "physical";
            }

            if (partnerInCell) {
                MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Partner '{}' (0x{:08X}) is in cell (0x{:08X}); coin-flip isLetterScenario={} -> initial scenario='{}'",
                               partnerName, partnerID, cellID, isLetterScenario, scenario);
            } else {
                MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Partner '{}' (0x{:08X}) is NOT in arrived cell (0x{:08X}) -> forced letter/away scenario='{}'",
                               partnerName, partnerID, cellID, scenario);
            }

            std::string playerName = ActorSvc::GetName(kPlayerFormID);
            auto loverLocCtx = ResolveActorLocationContext(activeLoverID);
            std::string targetLocName = !loverLocCtx.subLocationName.empty()
                                            ? loverLocCtx.subLocationName
                                            : (!loverLocCtx.locationName.empty() ? loverLocCtx.locationName : currentLocName);
            RE::FormID targetLocID = (loverLocCtx.subLocationID != 0)
                                         ? loverLocCtx.subLocationID
                                         : ((loverLocCtx.settlementLocationID != 0) ? loverLocCtx.settlementLocationID : locationKey);
            RE::FormID targetMarkerID = 0;

            if (isLetterScenario) {
                TavernRendezvousTarget tavernTarget = FindNearbyTavernForRendezvous(cellLoc, loverLocCtx);
                if (tavernTarget.innLocationID == 0 || tavernTarget.centerMarkerID == 0) {
                    if (!partnerInCell) {
                        MARAS_LOG_WARN("[Matchmaking:HomeArrival:Priority2] No valid tavern marker found for forced letter scenario '{}' and partner '{}' (0x{:08X}) is not in the house cell; skipping.",
                                       scenario, partnerName, partnerID);
                        continue;
                    }
                    MARAS_LOG_WARN("[Matchmaking:HomeArrival:Priority2] No valid tavern marker found for letter scenario '{}'; falling back to in-house scenario", scenario);
                    isLetterScenario = false;
                    if (isOpen) {
                        scenario = "open_in_house";
                    } else if (activeStage == AffairStage::Talking) {
                        scenario = "cheating_talking_in_house";
                    } else {
                        scenario = "cheating_physical_in_house";
                    }
                } else {
                    targetLocID = tavernTarget.innLocationID;
                    targetMarkerID = tavernTarget.centerMarkerID;
                    targetLocName = tavernTarget.displayName;
                }
            }

            lastHomeArrivalEventDayByLoc_[locationKey] = currentGameDay;

            if (isLetterScenario) {
                Temperament temp = mgr.GetTemperament(partnerID);
                std::string salutation;
                std::string premise;
                std::string clue;
                std::string signOff;
                std::string modalTitle;
                std::string modalSubtitle;
                ModalTone modalTone = ModalTone::Default;

                if (scenario == "open_date_away") {
                    modalTitle = std::format("Note from {}", partnerName);
                    modalSubtitle = "Open Relationship â€” Evening Out";
                    modalTone = ModalTone::Default;
                    switch (temp) {
                        case Temperament::Romantic:
                            salutation = std::format("My dearest {},", playerName);
                            premise = std::format("I was hoping to catch you before I stepped out, but my heart called me away for the evening to share a warm drink and conversation with {}.", loverName);
                            break;
                        case Temperament::Humble:
                            salutation = std::format("Dear {},", playerName);
                            premise = std::format("I wanted to make sure you knew where I went so you wouldn't worry when you came homeâ€”{} invited me out for a quiet evening together.", loverName);
                            break;
                        case Temperament::Independent:
                            salutation = std::format("{},", playerName);
                            premise = std::format("Since you were out on the road, I decided to spend the evening with {}.", loverName);
                            break;
                        case Temperament::Proud:
                            salutation = std::format("To {},", playerName);
                            premise = std::format("I have stepped out to enjoy a well-deserved evening in the company of {}.", loverName);
                            break;
                        case Temperament::Jealous:
                            salutation = std::format("My love {},", playerName);
                            premise = std::format("You have been away for so long that I finally accepted {}'s invitation to spend the evening together.", loverName);
                            break;
                    }
                    clue = std::format("We'll be spending time around {}. Feel free to join us if your travels bring you nearby, or I'll see you at home later tonight.", targetLocName);
                    signOff = std::format("â€” Yours, {}", partnerName);
                } else if (scenario == "cheating_talking_letter") {
                    modalTitle = "Half-Burnt Note Near the Hearth";
                    modalSubtitle = std::format("Addressed to {} (Sender Signature Burned Away)", partnerName);
                    modalTone = ModalTone::Warning;
                    switch (temp) {
                        case Temperament::Romantic:
                            salutation = std::format("\"Dearest {}, I cannot stop thinking about our last quiet talk...\"", partnerName);
                            break;
                        case Temperament::Humble:
                            salutation = std::format("\"Dear {}, I know how hesitant you have been, and I promise no one will learn of our meetings...\"", partnerName);
                            break;
                        case Temperament::Independent:
                            salutation = std::format("\"{}, slip away from the house whenever you have a spare hour...\"", partnerName);
                            break;
                        case Temperament::Proud:
                            salutation = std::format("\"To the incomparable {}â€”someone of your grace deserves far more devotion than an empty hearth...\"", partnerName);
                            break;
                        case Temperament::Jealous:
                            salutation = std::format("\"{}, why wait at home alone while your partner wanders Skyrim? Let us talk again tonight...\"", partnerName);
                            break;
                    }
                    premise = "\"Every stolen conversation we've shared lately has only left me wishing we didn't have to hide in the shadows.\"";
                    clue = std::format("\"Meet me near {} when you can slip away cleanly.\"", targetLocName);
                    signOff = "(The bottom corner of the parchment and the sender's signature have been hastily burned away in the hearth embers.)";
                } else {
                    modalTitle = "Charred Letter Found in the Embers";
                    modalSubtitle = std::format("Addressed to {} (Sender Signature Destroyed)", partnerName);
                    modalTone = ModalTone::Danger;
                    switch (temp) {
                        case Temperament::Romantic:
                            salutation = std::format("\"My forbidden love {}, last night in my arms was still not enough...\"", partnerName);
                            break;
                        case Temperament::Humble:
                            salutation = std::format("\"Dearest {}, please don't let guilt keep you from our bed tonight...\"", partnerName);
                            break;
                        case Temperament::Independent:
                            salutation = std::format("\"{}, come to me again tonightâ€”our secret stays between the two of us...\"", partnerName);
                            break;
                        case Temperament::Proud:
                            salutation = std::format("\"My magnificent {}, no one else could ever match the passion we shared...\"", partnerName);
                            break;
                        case Temperament::Jealous:
                            salutation = std::format("\"{}, forget your absent partner and come back to my bed tonight...\"", partnerName);
                            break;
                    }
                    premise = "\"I still feel your touch on my skin, and I've arranged a private place where we won't be interrupted.\"";
                    clue = std::format("\"Slip out toward {} as soon as the house is quiet.\"", targetLocName);
                    signOff = "(The bottom half of the letter has been charred black in the fire, destroying the writer's name.)";
                }

                std::string fullLetter = std::format("{}\n\n{}\n\n{}\n\n{}", salutation, premise, clue, signOff);
                ModalRequest req;
                req.title = modalTitle;
                req.subtitle = modalSubtitle;
                req.message = "You notice a piece of parchment left behind as you arrive home.";
                req.tone = modalTone;
                req.size = ModalSize::Md;
                req.defaultCancelButtonIndex = 0;
                req.controls.push_back(ModalControl::MakeInfoBox("partner_note", "Parchment Contents", fullLetter, modalTone));
                req.buttons.push_back(ModalButton::Make("close", "Put Note Away", ModalButtonVariant::Primary));
                PrismaModalManager::GetSingleton().ShowModal(std::move(req), [](const ModalResponse&) {});
                MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Displayed parchment modal '{}' for scenario '{}' (rendezvous target='{}', loc=0x{:08X}, marker=0x{:08X})",
                               modalTitle, scenario, targetLocName, targetLocID, targetMarkerID);
            }

            nlohmann::json payload;
            payload["scenario"] = scenario;
            payload["partnerFormId"] = PrismaUIDataBridge::FormIDToString(partnerID);
            payload["partnerName"] = partnerName;
            payload["loverFormId"] = PrismaUIDataBridge::FormIDToString(activeLoverID);
            payload["loverName"] = loverName;
            payload["affairStage"] = stageStr;
            payload["isOpenRelationship"] = isOpen;
            payload["intentType"] = "none";
            payload["targetLocationFormId"] = PrismaUIDataBridge::FormIDToString(targetLocID);
            payload["targetMarkerFormId"] = PrismaUIDataBridge::FormIDToString(targetMarkerID);
            payload["targetLocationName"] = targetLocName;

            if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                RE::TESForm* senderForm = partnerActor ? static_cast<RE::TESForm*>(partnerActor) : nullptr;
                std::string payloadStr = SafeJsonDump(payload);
                SKSE::ModCallbackEvent modEvent("maras_partner_home_arrival_event", payloadStr, 0.0f, senderForm);
                eventSource->SendEvent(&modEvent);
            }

            MARAS_LOG_INFO("[Matchmaking:HomeArrival:Priority2] Fired scenario '{}' for '{}' (0x{:08X}) with lover '{}' (0x{:08X}) at '{}' (targetLoc='{}' [0x{:08X}], marker=0x{:08X})",
                           scenario, partnerName, partnerID, loverName, activeLoverID, currentLocName,
                           targetLocName, targetLocID, targetMarkerID);
            return true;
        }
        return false;
    }

    bool MatchmakingService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;
        if (!serialization->WriteRecordData(lastMatchmakingDay_)) {
            MARAS_LOG_ERROR("MatchmakingService::Save - failed to write lastMatchmakingDay_");
            return false;
        }

        uint32_t count = static_cast<uint32_t>(npcLastMatchmakingChangeDay_.size());
        if (!serialization->WriteRecordData(count)) {
            MARAS_LOG_ERROR("MatchmakingService::Save - failed to write cooldown count");
            return false;
        }

        for (const auto& [actorID, day] : npcLastMatchmakingChangeDay_) {
            if (!serialization->WriteRecordData(actorID) || !serialization->WriteRecordData(day)) {
                MARAS_LOG_ERROR("MatchmakingService::Save - failed to write cooldown entry for 0x{:08X}", actorID);
                return false;
            }
        }
        if (!serialization->WriteRecordData(lastPlayerPartnerCheatingDay_)) {
            if (true) {
                MARAS_LOG_ERROR("MatchmakingService::Save - failed to write lastPlayerPartnerCheatingDay_");
                return false;
            }
        }
        uint32_t locCoolCount = static_cast<uint32_t>(lastHomeArrivalEventDayByLoc_.size());
        if (!serialization->WriteRecordData(locCoolCount)) {
            return false;
        }
        for (const auto& [locID, day] : lastHomeArrivalEventDayByLoc_) {
            if (!serialization->WriteRecordData(locID) || !serialization->WriteRecordData(day)) {
                return false;
            }
        }
        return true;
    }

    bool MatchmakingService::Load(SKSE::SerializationInterface* serialization, uint32_t /*version*/) {
        if (!serialization) return false;
        Revert();
        if (!serialization->ReadRecordData(lastMatchmakingDay_)) {
            MARAS_LOG_WARN("MatchmakingService::Load - failed to read lastMatchmakingDay_, resetting");
            lastMatchmakingDay_ = -1.0f;
            return false;
        }

        constexpr uint32_t kMaxCooldownEntries = 100000;
        uint32_t count = 0;
        if (serialization->ReadRecordData(count)) {
            if (count > kMaxCooldownEntries) {
                MARAS_LOG_WARN("MatchmakingService::Load - clamping unreasonable cooldown count {} to {}", count, kMaxCooldownEntries);
                count = kMaxCooldownEntries;
            }
            for (uint32_t i = 0; i < count; ++i) {
                RE::FormID savedID = 0;
                float day = 0.0f;
                if (!serialization->ReadRecordData(savedID) || !serialization->ReadRecordData(day)) {
                    MARAS_LOG_WARN("MatchmakingService::Load - failed to read cooldown entry {}", i);
                    break;
                }
                RE::FormID resolvedID = 0;
                if (savedID != 0 && !IsSyntheticChildID(savedID) && serialization->ResolveFormID(savedID, resolvedID)) {
                    npcLastMatchmakingChangeDay_[resolvedID] = day;
                }
            }
            float savedPartnerCheatDay = -1.0f;
            if (serialization->ReadRecordData(savedPartnerCheatDay)) {
                if (true) {
                    lastPlayerPartnerCheatingDay_ = savedPartnerCheatDay;
                    uint32_t locCoolCount = 0;
                    if (serialization->ReadRecordData(locCoolCount)) {
                        if (locCoolCount > kMaxCooldownEntries) {
                            locCoolCount = kMaxCooldownEntries;
                        }
                        for (uint32_t i = 0; i < locCoolCount; ++i) {
                            RE::FormID savedLocID = 0;
                            float locDay = 0.0f;
                            if (!serialization->ReadRecordData(savedLocID) || !serialization->ReadRecordData(locDay)) {
                                break;
                            }
                            RE::FormID resolvedLocID = 0;
                            if (savedLocID != 0 && serialization->ResolveFormID(savedLocID, resolvedLocID)) {
                                lastHomeArrivalEventDayByLoc_[resolvedLocID] = locDay;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }

    void MatchmakingService::Revert() {
        lastMatchmakingDay_ = -1.0f;
        npcLastMatchmakingChangeDay_.clear();
        lastPlayerPartnerCheatingDay_ = -1.0f;
        lastHomeArrivalEventDayByLoc_.clear();
    }

    nlohmann::json MatchmakingService::DumpJson() const {
        nlohmann::json j;
        j["lastMatchmakingDay"] = lastMatchmakingDay_;
        j["lastPlayerPartnerCheatingDay"] = lastPlayerPartnerCheatingDay_;
        nlohmann::json cooldowns = nlohmann::json::array();
        float currentDay = GetCurrentGameDay();
        for (const auto& [actorID, lastDay] : npcLastMatchmakingChangeDay_) {
            nlohmann::json c;
            c["formId"] = std::format("{:08X}", actorID);
            c["name"] = ActorSvc::GetName(actorID);
            c["lastMatchmakingChangeDay"] = lastDay;
            c["cooldownRemainingDays"] = GetNpcCooldownRemaining(actorID, currentDay);
            cooldowns.push_back(c);
        }
        j["npcCooldowns"] = cooldowns;
        return j;
    }

}  // namespace MARAS
