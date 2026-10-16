#include "core/MatchmakingService.h"
#include "core/SkyrimNetIntegration.h"
#include "core/LoggingService.h"
#include "services/ActorService.h"
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

    template <typename TaskType>
    static std::pair<std::vector<size_t>, std::vector<std::string>> CollectBatchFailures(const std::vector<TaskType>& tasks) {
        std::vector<size_t> indices;
        std::vector<std::string> descriptions;
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].failed) {
                indices.push_back(i);
                descriptions.push_back(std::format("• {} & {}: {}",
                    tasks[i].result.actorAName, tasks[i].result.actorBName, tasks[i].failureReason));
            }
        }
        return {indices, descriptions};
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
                                      MatchmakingService* service,
                                      RetryFn retryFn,
                                      CancelFn cancelFn,
                                      FinishFn finishFn) {
        if (!batch) return;
        if (batch->pendingCount > 0) {
            batch->pendingCount--;
        }
        if (batch->pendingCount == 0) {
            auto [failedIndices, failedDescriptions] = CollectBatchFailures(batch->tasks);
            if (!failedIndices.empty() && batch->isScheduledScan) {
                MARAS_LOG_WARN("[Matchmaking] Batch finished with {} failed task(s). Showing in-game dialog.", failedIndices.size());
                service->ShowInGameFailedLLMModal(
                    modalTitle,
                    failedDescriptions,
                    [batch, failedIndices, retryFn]() {
                        batch->pendingCount = failedIndices.size();
                        retryFn(batch, failedIndices);
                    },
                    [batch, failedIndices, cancelFn, finishFn]() {
                        MARAS_LOG_INFO("[Matchmaking] User cancelled failed task(s). Relationship changes will NOT be applied for failed pairs.");
                        cancelFn(batch, failedIndices);
                        finishFn(batch);
                    }
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
            if (key.actorA == kPlayerFormID || key.actorB == kPlayerFormID) {
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
            if (settings.matchmakingExcludePlayerActivePartners &&
                (NPCRelationshipManager::GetSingleton().IsPlayerActiveMainPartner(actorFormID) ||
                 (entry.baseFormID != 0 && NPCRelationshipManager::GetSingleton().IsPlayerActiveMainPartner(entry.baseFormID)))) {
                continue;
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
                            { "The scheduled matchmaking scan returned malformed or unreadable data." },
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

                if (settings.matchmakingExcludePlayerActivePartners) {
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

                if (settings.matchmakingExcludePlayerActivePartners) {
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

                if (settings.matchmakingExcludePlayerActivePartners) {
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
            if (settings.matchmakingExcludePlayerActivePartners &&
                (mgr.IsPlayerActiveMainPartner(actorID) || (entry.baseFormID != 0 && mgr.IsPlayerActiveMainPartner(entry.baseFormID)))) {
                continue;
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

        if (IsSkyrimNetAvailable() && (settings.matchmakingEvaluationStep || settings.matchmakingGenerateMemories)) {
            if (settings.matchmakingEvaluationStep) {
                MARAS_LOG_INFO("[Matchmaking:DiceRoll] SkyrimNet is LOADED -> Dispatching {} match(es) to LLM verification & dual-memory generation pipeline",
                               matches.size());
                MARAS_LOG_INFO("================================================================================");
                StartVerificationBatch(std::move(matches), true);
            } else {
                MARAS_LOG_INFO("[Matchmaking:DiceRoll] SkyrimNet is LOADED (evaluation step disabled) -> Dispatching {} match(es) directly to memory generation pipeline",
                               matches.size());
                MARAS_LOG_INFO("================================================================================");
                StartMemoryBatch(std::move(matches), true);
            }
        } else {
            MARAS_LOG_INFO("[Matchmaking:DiceRoll] Directly applying {} match(es) in background (SkyrimNet unavailable or evaluation & memory steps disabled):",
                           matches.size());
            for (size_t i = 0; i < matches.size(); ++i) {
                auto& match = matches[i];
                bool applied = ApplyMatch(match);
                MARAS_LOG_INFO("[Matchmaking:DiceRoll]   Match #{}: {} <-> {} | Action: '{}' -> '{}' | Status: {}",
                               i + 1, match.actorAName, match.actorBName, match.action, match.targetStatus, applied ? "APPLIED" : "FAILED");
            }
            MARAS_LOG_INFO("================================================================================");
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

        SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, [targetNpcFormID](const char* response, int success) {
            if (!success || !response || response[0] == '\0') {
                MARAS_LOG_WARN("[Matchmaking:RunManualMatchmaking] Manual matchmaking request failed or returned empty response (success: {})", success);
                SKSE::GetTaskInterface()->AddTask([]() {
                    PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, false);
                });
                return;
            }
            SKSE::GetTaskInterface()->AddTask([respStr = std::string(response), targetNpcFormID]() {
                try {
                    MatchmakingService::GetSingleton().ProcessManualResults(respStr, targetNpcFormID);
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

        SendCustomPromptToLLM(promptName, "gamemaster_evaluation", contextJson, [autoApply](const char* response, int success) {
            if (!success || !response || response[0] == '\0') {
                MARAS_LOG_WARN("[Matchmaking:RunGlobalMatchmaking] Global matchmaking request failed or returned empty response (success: {})", success);
                SKSE::GetTaskInterface()->AddTask([]() {
                    PrismaUIDataBridge::GetSingleton().SendMatchmakingResults({}, true);
                });
                return;
            }
            SKSE::GetTaskInterface()->AddTask([respStr = std::string(response), autoApply]() {
                try {
                    if (autoApply) {
                        MatchmakingService::GetSingleton().ProcessScheduledResults(respStr, false);
                    } else {
                        MatchmakingService::GetSingleton().ProcessGlobalManualResults(respStr);
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
            [actorA, actorB](const std::string& respStr) {
                MatchmakingService::GetSingleton().ProcessGeneratedMemories(respStr, actorA, actorB);
            },
            [actorB]() {
                PrismaUIDataBridge::GetSingleton().SendGeneratedMemories(actorB, nlohmann::json::array());
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

    struct MatchmakingRetryCallback : RE::IMessageBoxCallback {
        std::function<void()> retry;
        std::function<void()> cancel;
        MatchmakingRetryCallback(std::function<void()> r, std::function<void()> c)
            : retry(std::move(r)), cancel(std::move(c)) {}
        void Run(std::uint8_t a_msg) override {
            if (static_cast<int>(a_msg) == 0) {
                if (retry) retry();
            } else {
                if (cancel) cancel();
            }
        }
    };

    void MatchmakingService::ShowInGameFailedLLMModal(
        const std::string& title,
        const std::vector<std::string>& failedDescriptions,
        std::function<void()> onRetry,
        std::function<void()> onCancel)
    {
        if (failedDescriptions.empty()) {
            return;
        }

        std::ostringstream ss;
        if (!title.empty()) {
            ss << title << "\n\n";
        }
        for (size_t i = 0; i < failedDescriptions.size(); ++i) {
            ss << failedDescriptions[i];
            if (i + 1 < failedDescriptions.size()) {
                ss << "\n";
            }
        }
        ss << "\n\nChoose Retry to re-run failed requests, or Cancel to abort them.";

        std::string message = ss.str();

        auto* taskIF = SKSE::GetTaskInterface();
        if (!taskIF) {
            MARAS_LOG_ERROR("[Matchmaking:ShowInGameFailedLLMModal] No task interface, falling back to cancel");
            if (onCancel) onCancel();
            return;
        }

        taskIF->AddTask([message, onRetry = std::move(onRetry), onCancel = std::move(onCancel)]() mutable {
            auto* factory = RE::MessageDataFactoryManager::GetSingleton();
            auto* ifStr   = RE::InterfaceStrings::GetSingleton();
            auto* creator = factory ? factory->GetCreator<RE::MessageBoxData>(ifStr->messageBoxData) : nullptr;
            if (!creator) {
                MARAS_LOG_ERROR("[Matchmaking:ShowInGameFailedLLMModal] Null creator, cancelling");
                if (onCancel) onCancel();
                return;
            }
            auto* msgData = creator->Create();
            if (!msgData) {
                MARAS_LOG_ERROR("[Matchmaking:ShowInGameFailedLLMModal] Null msgData, cancelling");
                if (onCancel) onCancel();
                return;
            }

            msgData->bodyText = message.c_str();
            msgData->buttonText.push_back(RE::BSString("Retry"));
            msgData->buttonText.push_back(RE::BSString("Cancel"));
            msgData->cancelButtonIndex = 1;
            msgData->callback = RE::make_smart<MatchmakingRetryCallback>(std::move(onRetry), std::move(onCancel));
            RE::MessageBoxMenu::QueueMessage(msgData);
        });
    }

    void MatchmakingService::StartVerificationBatch(std::vector<MatchmakingResult> candidates, bool isScheduledScan) {
        if (candidates.empty()) {
            return;
        }

        if (!IsSkyrimNetAvailable()) {
            MARAS_LOG_WARN("[Matchmaking:StartVerificationBatch] SkyrimNet is not available, applying candidates directly without verification");
            for (auto& cand : candidates) {
                ApplyMatch(cand);
            }
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
                            t.failureReason = "LLM request failed or returned empty response";
                            MARAS_LOG_WARN("[Matchmaking:RetryVerificationTasks] Verification request failed for {} <-> {}",
                                           t.result.actorAName, t.result.actorBName);
                        } else {
                            nlohmann::json resp = JsonService::ParseLLMJson(respStr);
                            if (resp.is_discarded()) {
                                t.completed = true;
                                t.failed = true;
                                t.failureReason = "Malformed / unrepairable JSON response";
                                MARAS_LOG_WARN("[Matchmaking:RetryVerificationTasks] Failed to parse verification JSON for {} <-> {}. Raw: {}",
                                               t.result.actorAName, t.result.actorBName, respStr);
                            } else {
                                t.completed = true;
                                t.failed = false;
                                t.suitable = GetJsonBool(resp, "suitable", GetJsonBool(resp, "allow", false));
                                t.verificationReason = GetJsonString(resp, "reason", "No reason provided");
                                MARAS_LOG_INFO("[Matchmaking:RetryVerificationTasks] Verification for {} <-> {}: suitable={}, reason='{}'",
                                               t.result.actorAName, t.result.actorBName, t.suitable, t.verificationReason);
                            }
                        }

                        HandleBatchCompletion(batch, "MARAS: Verification Request Failed", this,
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
                verifiedMatches.push_back(task.result);
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

        MARAS_LOG_INFO("[Matchmaking:FinishVerificationBatch] Advancing {} verified match(es) to memory generation batch", verifiedMatches.size());
        StartMemoryBatch(std::move(verifiedMatches), batch->isScheduledScan);
    }

    void MatchmakingService::StartMemoryBatch(std::vector<MatchmakingResult> verifiedMatches, bool isScheduledScan) {
        if (verifiedMatches.empty()) {
            return;
        }

        const auto& settings = Settings::GetSingleton();
        if (!settings.matchmakingGenerateMemories || !IsSkyrimNetAvailable()) {
            MARAS_LOG_INFO("[Matchmaking:StartMemoryBatch] {} Applying verified matches directly without memory ingestion.",
                           !IsSkyrimNetAvailable() ? "SkyrimNet is not available." : "Memory generation is disabled.");
            for (auto& match : verifiedMatches) {
                ApplyMatch(match);
            }
            return;
        }

        auto batch = std::make_shared<MemoryBatch>();
        batch->isScheduledScan = isScheduledScan;
        batch->tasks.reserve(verifiedMatches.size());
        for (auto& match : verifiedMatches) {
            MemoryTask task;
            task.result = std::move(match);
            batch->tasks.push_back(std::move(task));
        }

        batch->pendingCount = batch->tasks.size();
        auto allIndices = MakeBatchIndices(batch->tasks.size());

        MARAS_LOG_INFO("[Matchmaking:StartMemoryBatch] Starting memory generation batch for {} verified match(es) (isScheduled: {})",
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
                            t.failureReason = "LLM request failed or returned empty response";
                            MARAS_LOG_WARN("[Matchmaking:RetryMemoryTasks] Memory generation failed for {} <-> {}",
                                           t.result.actorAName, t.result.actorBName);
                        } else {
                            auto memories = ParseGeneratedMemories(respStr, t.result.actorAFormID, t.result.actorBFormID);
                            if (memories.empty()) {
                                t.completed = true;
                                t.failed = true;
                                t.failureReason = "Failed to parse memories array from LLM response";
                                MARAS_LOG_WARN("[Matchmaking:RetryMemoryTasks] Failed to parse memories for {} <-> {}. Raw: {}",
                                               t.result.actorAName, t.result.actorBName, respStr);
                            } else {
                                t.completed = true;
                                t.failed = false;
                                t.generatedMemories = std::move(memories);
                                MARAS_LOG_INFO("[Matchmaking:RetryMemoryTasks] Parsed {} memories for {} <-> {}",
                                               t.generatedMemories.size(), t.result.actorAName, t.result.actorBName);
                            }
                        }

                        HandleBatchCompletion(batch, "MARAS: Memory Generation Failed", this,
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

        for (auto& task : batch->tasks) {
            if (task.completed && !task.failed) {
                if (ApplyMatch(task.result)) {
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
        }
        else if (result.action == "breakup") {
            auto currentStatus = mgr.GetRelationshipStatus(result.actorAFormID, result.actorBFormID);
            auto [targetStr, targetEnum] = GetBreakupTarget(currentStatus);
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
            const auto& settings = Settings::GetSingleton();
            if (settings.matchmakingExcludePlayerActivePartners) {
                if (NPCRelationshipManager::GetSingleton().IsPlayerActiveMainPartner(r.actorAFormID)) {
                    MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Skipping match: {} (0x{:08X}) is player's active main partner and excluded by settings",
                                   r.actorAName, r.actorAFormID);
                    continue;
                }
                if (NPCRelationshipManager::GetSingleton().IsPlayerActiveMainPartner(r.actorBFormID)) {
                    MARAS_LOG_INFO("[Matchmaking:ProcessScheduledResults] Skipping match: {} (0x{:08X}) is player's active main partner and excluded by settings",
                                   r.actorBName, r.actorBFormID);
                    continue;
                }
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
            const auto& settings = Settings::GetSingleton();
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
        }
        return true;
    }

    void MatchmakingService::Revert() {
        lastMatchmakingDay_ = -1.0f;
        npcLastMatchmakingChangeDay_.clear();
    }

    nlohmann::json MatchmakingService::DumpJson() const {
        nlohmann::json j;
        j["lastMatchmakingDay"] = lastMatchmakingDay_;
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
