#include "core/SkyrimNetDecisions.h"
#include "utils/Common.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"
#include "utils/StringUtils.h"

#include <Windows.h>

namespace MARAS::SkyrimNetDecisions {

    namespace {

        std::string GetJsonStr(const nlohmann::json& j, std::string_view key, std::string_view defaultVal = "") {
            if (!j.is_object()) return std::string(defaultVal);
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return std::string(defaultVal);
            if (it->is_string()) return it->get<std::string>();
            if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
            if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
            if (it->is_number_unsigned()) return std::to_string(it->get<uint64_t>());
            return std::string(defaultVal);
        }

        bool GetJsonBoolVal(const nlohmann::json& j, std::string_view key, bool defaultVal = false) {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_boolean()) return it->get<bool>();
            if (it->is_number()) return it->get<double>() != 0.0;
            if (it->is_string()) {
                std::string lower = StringUtils::ToLowerStd(it->get<std::string>());
                if (lower == "true" || lower == "1" || lower == "yes") return true;
                if (lower == "false" || lower == "0" || lower == "no") return false;
            }
            return defaultVal;
        }

    }  // namespace

    bool IsDecisionsAvailable() {
        if (!IsSkyrimNetAvailable()) {
            return false;
        }
        HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            return false;
        }
        return GetProcAddress(hMod, "PublicSendCustomDecisionToLLM") != nullptr;
    }

    bool ShouldUseDecisions() {
        return Settings::GetSingleton().skyrimnetUseDecisions && IsDecisionsAvailable();
    }

    bool ShouldFallbackToChatOnFailure(std::string_view resultJson, int success) {
        if (success != 0 || resultJson.empty()) {
            return false;
        }
        auto parsed = nlohmann::json::parse(resultJson, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return false;
        }
        auto it = parsed.find("error");
        return it != parsed.end() && it->is_string() && it->get<std::string>() == "no_decisions_route";
    }

    std::string SanitizeSingleLine(std::string_view text) {
        std::string out;
        out.reserve(text.size());
        bool lastWasSpace = true;
        for (char ch : text) {
            if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
                if (!lastWasSpace) {
                    out.push_back(' ');
                    lastWasSpace = true;
                }
            } else {
                out.push_back(ch);
                lastWasSpace = false;
            }
        }
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
        return out;
    }

    std::string SanitizeKey(std::string_view rawKey) {
        std::string out;
        out.reserve(rawKey.size());
        for (char ch : rawKey) {
            const auto u = static_cast<unsigned char>(ch);
            if (std::isalnum(u) != 0 || ch == '_' || ch == '-') {
                out.push_back(ch);
            }
        }
        return out;
    }

    std::optional<double> ParsedDecisionResponse::GetNoulProbability(std::string_view questionId) const {
        if (!answers.is_object()) {
            return std::nullopt;
        }
        auto qIt = answers.find(questionId);
        if (qIt == answers.end() || !qIt->is_object()) {
            return std::nullopt;
        }
        auto nIt = qIt->find("noul");
        if (nIt != qIt->end() && nIt->is_number()) {
            return std::clamp(nIt->get<double>(), 0.0, 1.0);
        }
        return std::nullopt;
    }

    bool ParsedDecisionResponse::GetNoulBool(std::string_view questionId, bool defaultVal, double threshold) const {
        if (auto p = GetNoulProbability(questionId); p.has_value()) {
            return *p >= threshold;
        }
        return defaultVal;
    }

    std::string ParsedDecisionResponse::GetChoice(std::string_view questionId, std::string_view defaultVal) const {
        if (!answers.is_object()) {
            return std::string(defaultVal);
        }
        auto qIt = answers.find(questionId);
        if (qIt == answers.end() || !qIt->is_object()) {
            return std::string(defaultVal);
        }
        auto cIt = qIt->find("choice");
        if (cIt != qIt->end() && cIt->is_string()) {
            return cIt->get<std::string>();
        }
        return std::string(defaultVal);
    }

    double ParsedDecisionResponse::GetChoiceProbability(std::string_view questionId, std::string_view choiceKey) const {
        if (!answers.is_object() || choiceKey.empty()) {
            return 0.0;
        }
        auto qIt = answers.find(questionId);
        if (qIt == answers.end() || !qIt->is_object()) {
            return 0.0;
        }
        auto pIt = qIt->find("probabilities");
        if (pIt == qIt->end() || !pIt->is_object()) {
            return 0.0;
        }
        auto kIt = pIt->find(choiceKey);
        if (kIt != pIt->end() && kIt->is_number()) {
            return std::clamp(kIt->get<double>(), 0.0, 1.0);
        }
        return 0.0;
    }

    ParsedDecisionResponse ParseDecisionResult(const char* answer, const char* resultJson, int success) {
        ParsedDecisionResponse out;
        out.primaryAnswer = (answer && answer[0] != '\0') ? std::string(answer) : "";
        std::string rawJson = (resultJson && resultJson[0] != '\0') ? std::string(resultJson) : "";

        if (success == 0) {
            out.valid = false;
            out.failureType = LLMFailureType::RequestFailed;
            if (!rawJson.empty()) {
                auto errObj = nlohmann::json::parse(rawJson, nullptr, false);
                if (!errObj.is_discarded() && errObj.is_object()) {
                    std::string errCode = GetJsonStr(errObj, "error");
                    std::string errMsg = GetJsonStr(errObj, "message");
                    if (errCode == "no_decisions_route") {
                        out.noDecisionsRoute = true;
                    }
                    if (!errMsg.empty()) {
                        out.errorMessage = errCode.empty() ? errMsg : std::format("{}: {}", errCode, errMsg);
                    } else if (!errCode.empty()) {
                        out.errorMessage = errCode;
                    }
                }
            }
            if (out.errorMessage.empty()) {
                out.errorMessage = "SkyrimNet Decisions request failed or returned an empty response.";
            }
            return out;
        }

        if (rawJson.empty()) {
            out.valid = false;
            out.failureType = LLMFailureType::RequestFailed;
            out.errorMessage = "SkyrimNet Decisions returned an empty result payload.";
            return out;
        }

        auto parsed = nlohmann::json::parse(rawJson, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            out.valid = false;
            out.failureType = LLMFailureType::InvalidJson;
            out.errorMessage = "SkyrimNet Decisions returned malformed JSON.";
            return out;
        }

        auto answersIt = parsed.find("answers");
        if (answersIt == parsed.end() || !answersIt->is_object()) {
            out.valid = false;
            out.failureType = LLMFailureType::InvalidJson;
            out.errorMessage = "SkyrimNet Decisions payload missing 'answers' object.";
            return out;
        }

        out.valid = true;
        out.answers = *answersIt;
        return out;
    }

    // =========================================================================
    // 1. Bio-Father Step-In Evaluation (maras_step_in_evaluation)
    // =========================================================================
    namespace {

        bool RunStepInChatFallback(const std::string& contextJson,
                                   std::function<void(const StepInEvalResult&)> callback) {
            const std::string promptName = "maras_evaluations/maras_step_in_evaluation";
            return SendCustomPromptToLLM(
                promptName,
                "gamemaster_evaluation",
                contextJson,
                [cb = std::move(callback)](const char* response, int success) {
                    StepInEvalResult res;
                    res.usedDecisions = false;
                    if (success && response && response[0] != '\0') {
                        nlohmann::json resp = JsonService::ParseLLMJson(response);
                        if (!resp.is_discarded() && resp.is_object()) {
                            res.success = true;
                            res.steppedIn = GetJsonBoolVal(resp, "bio_father_steps_in", false);
                            res.probability = res.steppedIn ? 1.0 : 0.0;
                            res.reason = GetJsonStr(resp, "reason");
                        } else {
                            res.success = false;
                            res.failureType = LLMFailureType::InvalidJson;
                            res.failureDetail = "AI model returned broken or unreadable JSON.";
                        }
                    } else {
                        res.success = false;
                        res.failureType = LLMFailureType::RequestFailed;
                        res.failureDetail = "HTTP / connection request failed or returned an empty response.";
                    }
                    if (cb) cb(res);
                });
        }

    }  // namespace

    bool EvaluateStepIn(const std::string& contextJson,
                        std::function<void(const StepInEvalResult&)> callback) {
        if (!ShouldUseDecisions()) {
            return RunStepInChatFallback(contextJson, std::move(callback));
        }

        bool queued = SendCustomDecisionToLLM(
            "maras_step_in_evaluation",
            contextJson,
            4,
            [contextJson, cb = callback](const char* answer, const char* resultJson, int success) mutable {
                if (ShouldFallbackToChatOnFailure(resultJson ? resultJson : "", success)) {
                    MARAS_LOG_WARN("[SkyrimNetDecisions] No 'decisions' route configured in SkyrimNet for maras_step_in_evaluation; falling back to Chat LLM prompt.");
                    if (!RunStepInChatFallback(contextJson, cb)) {
                        StepInEvalResult failRes;
                        failRes.usedDecisions = false;
                        failRes.success = false;
                        failRes.failureType = LLMFailureType::RequestFailed;
                        failRes.failureDetail = "Fallback Chat LLM request failed to queue.";
                        if (cb) cb(failRes);
                    }
                    return;
                }

                ParsedDecisionResponse parsed = ParseDecisionResult(answer, resultJson, success);
                StepInEvalResult res;
                res.usedDecisions = true;

                if (!parsed.valid) {
                    res.success = false;
                    res.failureType = parsed.failureType;
                    res.failureDetail = parsed.errorMessage;
                    if (cb) cb(res);
                    return;
                }

                auto probOpt = parsed.GetNoulProbability("bio_father_steps_in");
                if (!probOpt.has_value()) {
                    res.success = false;
                    res.failureType = LLMFailureType::InvalidJson;
                    res.failureDetail = "SkyrimNet Decisions response missing 'bio_father_steps_in' probability.";
                    if (cb) cb(res);
                    return;
                }

                res.success = true;
                res.probability = *probOpt;
                res.steppedIn = (*probOpt >= 0.5);
                res.reason = std::format("Jev decision confidence: {:.1f}% ({})",
                                         *probOpt * 100.0,
                                         res.steppedIn ? "steps in" : "declines");
                if (cb) cb(res);
            });

        if (!queued) {
            MARAS_LOG_WARN("[SkyrimNetDecisions] SendCustomDecisionToLLM failed to queue maras_step_in_evaluation; falling back to Chat LLM.");
            return RunStepInChatFallback(contextJson, std::move(callback));
        }
        return true;
    }

    // =========================================================================
    // 2. Paternity Secret Evaluation (maras_paternity_evaluation)
    // =========================================================================
    namespace {

        std::string NormalizePaternityOutcome(std::string outcome, bool bioInformedFallback = false) {
            if (outcome == "confession") {
                return "mother_confessed_presumed";
            }
            if (outcome == "discovery") {
                return "presumed_father_learnt";
            }
            if (outcome == "remains_secret" || outcome == "none" || outcome.empty()) {
                return bioInformedFallback ? "bio_father_learnt" : "none";
            }
            return outcome;
        }

        bool RunPaternityChatFallback(const std::string& contextJson,
                                      std::function<void(const PaternityEvalResult&)> callback) {
            const std::string promptName = "maras_evaluations/maras_paternity_evaluation";
            return SendCustomPromptToLLM(
                promptName,
                "gamemaster_evaluation",
                contextJson,
                [cb = std::move(callback)](const char* response, int success) {
                    PaternityEvalResult res;
                    res.usedDecisions = false;
                    if (success && response && response[0] != '\0') {
                        nlohmann::json resp = JsonService::ParseLLMJson(response);
                        if (!resp.is_discarded() && resp.is_object()) {
                            res.success = true;
                            std::string rawOutcome = GetJsonStr(resp, "outcome");
                            if (rawOutcome.empty()) {
                                rawOutcome = GetJsonStr(resp, "event", "remains_secret");
                            }
                            bool bioInformed = GetJsonBoolVal(resp, "bio_father_informed", false) ||
                                               GetJsonBoolVal(resp, "bio_father_aware", false);
                            res.outcome = NormalizePaternityOutcome(rawOutcome, bioInformed);
                            res.partnerReaction = GetJsonStr(resp, "partner_reaction", "none");
                            res.summary = GetJsonStr(resp, "summary");
                        } else {
                            res.success = false;
                            res.failureType = LLMFailureType::InvalidJson;
                            res.failureDetail = "AI model returned broken or unreadable JSON.";
                        }
                    } else {
                        res.success = false;
                        res.failureType = LLMFailureType::RequestFailed;
                        res.failureDetail = "HTTP / connection request failed or returned an empty response.";
                    }
                    if (cb) cb(res);
                });
        }

    }  // namespace

    bool EvaluatePaternity(const std::string& contextJson,
                           bool isSecretForPresumedFather,
                           std::function<void(const PaternityEvalResult&)> callback) {
        if (!ShouldUseDecisions()) {
            return RunPaternityChatFallback(contextJson, std::move(callback));
        }

        bool queued = SendCustomDecisionToLLM(
            "maras_paternity_evaluation",
            contextJson,
            6,
            [contextJson, isSecretForPresumedFather, cb = callback](const char* answer, const char* resultJson, int success) mutable {
                if (ShouldFallbackToChatOnFailure(resultJson ? resultJson : "", success)) {
                    MARAS_LOG_WARN("[SkyrimNetDecisions] No 'decisions' route configured in SkyrimNet for maras_paternity_evaluation; falling back to Chat LLM prompt.");
                    if (!RunPaternityChatFallback(contextJson, cb)) {
                        PaternityEvalResult failRes;
                        failRes.usedDecisions = false;
                        failRes.success = false;
                        failRes.failureType = LLMFailureType::RequestFailed;
                        failRes.failureDetail = "Fallback Chat LLM request failed to queue.";
                        if (cb) cb(failRes);
                    }
                    return;
                }

                ParsedDecisionResponse parsed = ParseDecisionResult(answer, resultJson, success);
                PaternityEvalResult res;
                res.usedDecisions = true;

                if (!parsed.valid) {
                    res.success = false;
                    res.failureType = parsed.failureType;
                    res.failureDetail = parsed.errorMessage;
                    if (cb) cb(res);
                    return;
                }

                std::string rawOutcome = parsed.GetChoice("outcome");
                if (rawOutcome.empty()) {
                    res.success = false;
                    res.failureType = LLMFailureType::InvalidJson;
                    res.failureDetail = "SkyrimNet Decisions response missing 'outcome' choice.";
                    if (cb) cb(res);
                    return;
                }

                res.success = true;
                res.outcomeProbability = parsed.GetChoiceProbability("outcome", rawOutcome);
                res.outcome = NormalizePaternityOutcome(rawOutcome, false);

                if (isSecretForPresumedFather &&
                    (res.outcome == "mother_confessed_presumed" ||
                     res.outcome == "presumed_father_learnt" ||
                     res.outcome == "mother_confessed_both")) {
                    res.partnerReaction = parsed.GetChoice("partner_reaction", "none");
                    res.reactionProbability = parsed.GetChoiceProbability("partner_reaction", res.partnerReaction);
                } else {
                    res.partnerReaction = "none";
                }

                res.summary = std::format("Jev paternity decision: outcome='{}' ({:.1f}%), reaction='{}' ({:.1f}%)",
                                          res.outcome, res.outcomeProbability * 100.0,
                                          res.partnerReaction, res.reactionProbability * 100.0);
                if (cb) cb(res);
            });

        if (!queued) {
            MARAS_LOG_WARN("[SkyrimNetDecisions] SendCustomDecisionToLLM failed to queue maras_paternity_evaluation; falling back to Chat LLM.");
            return RunPaternityChatFallback(contextJson, std::move(callback));
        }
        return true;
    }

    // =========================================================================
    // 3. Player-Partner Agency / Infidelity Evaluation (maras_player_partner_cheating)
    // =========================================================================
    namespace {

        bool RunPartnerAgencyChatFallback(const std::string& contextJson,
                                          std::function<void(const PartnerAgencyEvalResult&)> callback) {
            const std::string promptName = "maras_evaluations/maras_player_partner_cheating";
            return SendCustomPromptToLLM(
                promptName,
                "gamemaster_evaluation",
                contextJson,
                [cb = std::move(callback)](const char* response, int success) {
                    PartnerAgencyEvalResult res;
                    res.usedDecisions = false;
                    if (success && response && response[0] != '\0') {
                        nlohmann::json resp = JsonService::ParseLLMJson(response);
                        if (!resp.is_discarded() && resp.is_object()) {
                            res.success = true;
                            res.willCheat = GetJsonBoolVal(resp, "willCheat", GetJsonBoolVal(resp, "will_cheat", false));
                            res.willBreakup = GetJsonBoolVal(resp, "willBreakup", GetJsonBoolVal(resp, "will_breakup", false));
                            res.willConfess = GetJsonBoolVal(resp, "willConfess", GetJsonBoolVal(resp, "will_confess", false));
                            res.willSuggest = GetJsonBoolVal(resp, "willSuggest", GetJsonBoolVal(resp, "will_suggest", false));
                            res.chosenCandidateName = GetJsonStr(resp, "chosenCandidate", GetJsonStr(resp, "chosen_candidate"));
                            res.reasoning = GetJsonStr(resp, "reason", GetJsonStr(resp, "reasoning"));
                        } else {
                            res.success = false;
                            res.failureType = LLMFailureType::InvalidJson;
                            res.failureDetail = "Malformed / unrepairable JSON response";
                        }
                    } else {
                        res.success = false;
                        res.failureType = LLMFailureType::RequestFailed;
                        res.failureDetail = "LLM request failed or returned empty response";
                    }
                    if (cb) cb(res);
                });
        }

    }  // namespace

    bool EvaluatePartnerAgency(const std::string& contextJson,
                               const std::string& evalMode,
                               bool canConfess,
                               const std::string& partnerName,
                               const std::string& sideLoverName,
                               const std::string& affairStage,
                               std::function<void(const PartnerAgencyEvalResult&)> callback) {
        if (!ShouldUseDecisions()) {
            return RunPartnerAgencyChatFallback(contextJson, std::move(callback));
        }

        bool queued = SendCustomDecisionToLLM(
            "maras_player_partner_cheating",
            contextJson,
            10,
            [contextJson, evalMode, canConfess, partnerName, sideLoverName, affairStage, cb = callback](
                const char* answer, const char* resultJson, int success) mutable {
                if (ShouldFallbackToChatOnFailure(resultJson ? resultJson : "", success)) {
                    MARAS_LOG_WARN("[SkyrimNetDecisions] No 'decisions' route configured in SkyrimNet for maras_player_partner_cheating; falling back to Chat LLM prompt.");
                    if (!RunPartnerAgencyChatFallback(contextJson, cb)) {
                        PartnerAgencyEvalResult failRes;
                        failRes.usedDecisions = false;
                        failRes.success = false;
                        failRes.failureType = LLMFailureType::RequestFailed;
                        failRes.failureDetail = "Fallback Chat LLM request failed to queue.";
                        if (cb) cb(failRes);
                    }
                    return;
                }

                ParsedDecisionResponse parsed = ParseDecisionResult(answer, resultJson, success);
                PartnerAgencyEvalResult res;
                res.usedDecisions = true;

                if (!parsed.valid) {
                    res.success = false;
                    res.failureType = parsed.failureType;
                    res.failureDetail = parsed.errorMessage;
                    if (cb) cb(res);
                    return;
                }

                if (evalMode == "breakup_affair") {
                    auto probBreakup = parsed.GetNoulProbability("will_breakup");
                    if (!probBreakup.has_value()) {
                        res.success = false;
                        res.failureType = LLMFailureType::InvalidJson;
                        res.failureDetail = "Jev decision response missing 'will_breakup'.";
                        if (cb) cb(res);
                        return;
                    }
                    res.success = true;
                    res.willBreakup = (*probBreakup >= 0.5);
                    double probConfessVal = 0.0;
                    if (canConfess) {
                        if (auto pConf = parsed.GetNoulProbability("will_confess"); pConf.has_value()) {
                            probConfessVal = *pConf;
                            res.willConfess = (*pConf >= 0.5);
                        }
                    }
                    res.reasoning = res.willBreakup
                                        ? std::format("{} ended their secret {} affair with {} (AI confidence: {:.0f}%{}).",
                                                      partnerName, affairStage, sideLoverName, *probBreakup * 100.0,
                                                      res.willConfess ? std::format(", confessing: {:.0f}%", probConfessVal * 100.0) : "")
                                        : std::format("{} continued their secret {} affair with {} (breakup probability: {:.0f}%).",
                                                      partnerName, affairStage, sideLoverName, *probBreakup * 100.0);
                    if (cb) cb(res);
                    return;
                }

                if (evalMode == "breakup_open_lover") {
                    auto probBreakup = parsed.GetNoulProbability("will_breakup");
                    if (!probBreakup.has_value()) {
                        res.success = false;
                        res.failureType = LLMFailureType::InvalidJson;
                        res.failureDetail = "Jev decision response missing 'will_breakup'.";
                        if (cb) cb(res);
                        return;
                    }
                    res.success = true;
                    res.willBreakup = (*probBreakup >= 0.5);
                    res.reasoning = res.willBreakup
                                        ? std::format("{} amicably ended their open-relationship fling with {} (AI confidence: {:.0f}%).",
                                                      partnerName, sideLoverName, *probBreakup * 100.0)
                                        : std::format("{} continued their open-relationship fling with {} (breakup probability: {:.0f}%).",
                                                      partnerName, sideLoverName, *probBreakup * 100.0);
                    if (cb) cb(res);
                    return;
                }

                if (evalMode == "suggest_open" || evalMode == "suggest_close") {
                    auto probSuggest = parsed.GetNoulProbability("will_suggest");
                    if (!probSuggest.has_value()) {
                        res.success = false;
                        res.failureType = LLMFailureType::InvalidJson;
                        res.failureDetail = "Jev decision response missing 'will_suggest'.";
                        if (cb) cb(res);
                        return;
                    }
                    res.success = true;
                    res.willSuggest = (*probSuggest >= 0.5);
                    res.reasoning = std::format("Jev decision {}: willSuggest={} ({:.0f}%).",
                                                evalMode, res.willSuggest, *probSuggest * 100.0);
                    if (cb) cb(res);
                    return;
                }

                // Default: candidate_select
                std::string choice = parsed.GetChoice("decision");
                if (choice.empty()) {
                    res.success = false;
                    res.failureType = LLMFailureType::InvalidJson;
                    res.failureDetail = "Jev decision response missing 'decision' choice.";
                    if (cb) cb(res);
                    return;
                }

                double prob = parsed.GetChoiceProbability("decision", choice);
                res.success = true;
                if (choice == "none") {
                    res.willCheat = false;
                    res.reasoning = std::format("{} chose not to pursue any candidate (confidence: {:.0f}%).",
                                                partnerName, prob * 100.0);
                } else if (choice.starts_with("c_")) {
                    std::string_view idxView(choice.data() + 2, choice.size() - 2);
                    int parsedIdx = 0;
                    const auto [ptr, ec] = std::from_chars(idxView.data(), idxView.data() + idxView.size(), parsedIdx);
                    if (ec == std::errc{} && parsedIdx >= 1) {
                        res.willCheat = true;
                        res.chosenCandidateIndex = parsedIdx - 1;
                        res.reasoning = std::format("AI confidence: {:.0f}%", prob * 100.0);
                    } else {
                        res.willCheat = false;
                        res.reasoning = std::format("Unrecognized candidate key '{}'.", choice);
                    }
                } else {
                    res.willCheat = false;
                    res.reasoning = std::format("Choice '{}' did not select a candidate.", choice);
                }

                if (cb) cb(res);
            });

        if (!queued) {
            MARAS_LOG_WARN("[SkyrimNetDecisions] SendCustomDecisionToLLM failed to queue maras_player_partner_cheating; falling back to Chat LLM.");
            return RunPartnerAgencyChatFallback(contextJson, std::move(callback));
        }
        return true;
    }

    // =========================================================================
    // 4. Matchmaking Verification Evaluation (maras_matchmaking_verify)
    // =========================================================================
    namespace {

        bool RunMatchmakingVerifyChatFallback(const std::string& contextJson,
                                              std::function<void(const MatchmakingVerifyEvalResult&)> callback) {
            const std::string promptName = "maras_evaluations/maras_matchmaking_verify";
            return SendCustomPromptToLLM(
                promptName,
                "gamemaster_evaluation",
                contextJson,
                [cb = std::move(callback)](const char* response, int success) {
                    MatchmakingVerifyEvalResult res;
                    res.usedDecisions = false;
                    if (success && response && response[0] != '\0') {
                        nlohmann::json resp = JsonService::ParseLLMJson(response);
                        if (!resp.is_discarded() && resp.is_object()) {
                            res.success = true;
                            res.suitable = GetJsonBoolVal(resp, "suitable", GetJsonBoolVal(resp, "allow", false));
                            res.probA = res.suitable ? 1.0 : 0.0;
                            res.probB = res.suitable ? 1.0 : 0.0;
                            res.reason = GetJsonStr(resp, "reason", "No reason provided");
                        } else {
                            res.success = false;
                            res.failureType = LLMFailureType::InvalidJson;
                            res.failureDetail = "Malformed / unrepairable JSON response";
                        }
                    } else {
                        res.success = false;
                        res.failureType = LLMFailureType::RequestFailed;
                        res.failureDetail = "LLM request failed or returned empty response";
                    }
                    if (cb) cb(res);
                });
        }

    }  // namespace

    bool EvaluateMatchmakingVerify(const std::string& contextJson,
                                   const std::string& action,
                                   const std::string& targetStatus,
                                   const std::string& actorAName,
                                   const std::string& actorBName,
                                   std::function<void(const MatchmakingVerifyEvalResult&)> callback) {
        if (!ShouldUseDecisions()) {
            return RunMatchmakingVerifyChatFallback(contextJson, std::move(callback));
        }

        bool queued = SendCustomDecisionToLLM(
            "maras_matchmaking_verify",
            contextJson,
            4,
            [contextJson, action, targetStatus, actorAName, actorBName, cb = callback](
                const char* answer, const char* resultJson, int success) mutable {
                if (ShouldFallbackToChatOnFailure(resultJson ? resultJson : "", success)) {
                    MARAS_LOG_WARN("[SkyrimNetDecisions] No 'decisions' route configured in SkyrimNet for maras_matchmaking_verify; falling back to Chat LLM prompt.");
                    if (!RunMatchmakingVerifyChatFallback(contextJson, cb)) {
                        MatchmakingVerifyEvalResult failRes;
                        failRes.usedDecisions = false;
                        failRes.success = false;
                        failRes.failureType = LLMFailureType::RequestFailed;
                        failRes.failureDetail = "Fallback Chat LLM request failed to queue.";
                        if (cb) cb(failRes);
                    }
                    return;
                }

                ParsedDecisionResponse parsed = ParseDecisionResult(answer, resultJson, success);
                MatchmakingVerifyEvalResult res;
                res.usedDecisions = true;

                if (!parsed.valid) {
                    res.success = false;
                    res.failureType = parsed.failureType;
                    res.failureDetail = parsed.errorMessage;
                    if (cb) cb(res);
                    return;
                }

                if (action == "breakup") {
                    auto pA = parsed.GetNoulProbability("wants_breakup_a");
                    auto pB = parsed.GetNoulProbability("wants_breakup_b");
                    if (!pA.has_value() && !pB.has_value()) {
                        res.success = false;
                        res.failureType = LLMFailureType::InvalidJson;
                        res.failureDetail = "Jev decision response missing 'wants_breakup_a' and 'wants_breakup_b'.";
                        if (cb) cb(res);
                        return;
                    }
                    res.success = true;
                    res.probA = pA.value_or(0.0);
                    res.probB = pB.value_or(0.0);
                    const bool wantsBreakupA = (res.probA >= 0.5);
                    const bool wantsBreakupB = (res.probB >= 0.5);
                    res.suitable = (wantsBreakupA || wantsBreakupB);

                    if (wantsBreakupA && wantsBreakupB) {
                        res.reason = std::format("Both {} ({:.0f}%) and {} ({:.0f}%) want to end their relationship and transition to {}.",
                                                 actorAName, res.probA * 100.0, actorBName, res.probB * 100.0, targetStatus);
                    } else if (wantsBreakupA) {
                        res.reason = std::format("{} ({:.0f}% confidence) wants to end their relationship with {} ({:.0f}%) and transition to {}.",
                                                 actorAName, res.probA * 100.0, actorBName, res.probB * 100.0, targetStatus);
                    } else if (wantsBreakupB) {
                        res.reason = std::format("{} ({:.0f}% confidence) wants to end their relationship with {} ({:.0f}%) and transition to {}.",
                                                 actorBName, res.probB * 100.0, actorAName, res.probA * 100.0, targetStatus);
                    } else {
                        res.reason = std::format("Neither {} ({:.0f}%) nor {} ({:.0f}%) wants to end their relationship.",
                                                 actorAName, res.probA * 100.0, actorBName, res.probB * 100.0);
                    }
                    if (cb) cb(res);
                    return;
                }

                // Bilateral actions: new, advance, open, close
                auto pA = parsed.GetNoulProbability("willing_a");
                auto pB = parsed.GetNoulProbability("willing_b");
                if (!pA.has_value() || !pB.has_value()) {
                    res.success = false;
                    res.failureType = LLMFailureType::InvalidJson;
                    res.failureDetail = "Jev decision response missing 'willing_a' or 'willing_b'.";
                    if (cb) cb(res);
                    return;
                }

                res.success = true;
                res.probA = *pA;
                res.probB = *pB;
                const bool willingA = (res.probA >= 0.5);
                const bool willingB = (res.probB >= 0.5);
                res.suitable = (willingA && willingB);

                if (res.suitable) {
                    res.reason = std::format("Both {} ({:.0f}%) and {} ({:.0f}%) are willing to transition their relationship ({} -> {}).",
                                             actorAName, res.probA * 100.0, actorBName, res.probB * 100.0, action, targetStatus);
                } else if (!willingA && !willingB) {
                    res.reason = std::format("Neither {} ({:.0f}%) nor {} ({:.0f}%) is willing to transition their relationship ({} -> {}).",
                                             actorAName, res.probA * 100.0, actorBName, res.probB * 100.0, action, targetStatus);
                } else if (!willingA) {
                    res.reason = std::format("{} ({:.0f}%) is unwilling to transition the relationship with {} ({:.0f}%) ({} -> {}).",
                                             actorAName, res.probA * 100.0, actorBName, res.probB * 100.0, action, targetStatus);
                } else {
                    res.reason = std::format("{} ({:.0f}%) is unwilling to transition the relationship with {} ({:.0f}%) ({} -> {}).",
                                             actorBName, res.probB * 100.0, actorAName, res.probA * 100.0, action, targetStatus);
                }
                if (cb) cb(res);
            });

        if (!queued) {
            MARAS_LOG_WARN("[SkyrimNetDecisions] SendCustomDecisionToLLM failed to queue maras_matchmaking_verify; falling back to Chat LLM.");
            return RunMatchmakingVerifyChatFallback(contextJson, std::move(callback));
        }
        return true;
    }

}  // namespace MARAS::SkyrimNetDecisions

