#pragma once

#include "PCH.h"
#include "core/SkyrimNetIntegration.h"

namespace MARAS::SkyrimNetDecisions {

    // Returns true if SkyrimNet is available and exports PublicSendCustomDecisionToLLM (SkyrimNet API v12+).
    bool IsDecisionsAvailable();

    // Returns true if skyrimnetUseDecisions is enabled in Settings and IsDecisionsAvailable() is true.
    bool ShouldUseDecisions();

    // Returns true if a failed decision callback indicates that SkyrimNet has no 'decisions' model route configured
    // ("error": "no_decisions_route"), meaning the caller should fall back to the Chat LLM prompt.
    bool ShouldFallbackToChatOnFailure(std::string_view resultJson, int success);

    // Collapses CR/LF/tabs and repeated whitespace into single spaces and trims ends so dynamic
    // strings inside [ criteria ] blocks never introduce embedded newlines.
    std::string SanitizeSingleLine(std::string_view text);

    // Sanitizes a dynamic criterion key so it contains only [A-Za-z0-9_-].
    std::string SanitizeKey(std::string_view rawKey);

    struct ParsedDecisionResponse {
        bool valid{ false };
        bool noDecisionsRoute{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string errorMessage;
        std::string primaryAnswer;
        nlohmann::json answers;

        std::optional<double> GetNoulProbability(std::string_view questionId) const;
        bool GetNoulBool(std::string_view questionId, bool defaultVal = false, double threshold = 0.5) const;
        std::string GetChoice(std::string_view questionId, std::string_view defaultVal = "") const;
        double GetChoiceProbability(std::string_view questionId, std::string_view choiceKey) const;
    };

    ParsedDecisionResponse ParseDecisionResult(const char* answer, const char* resultJson, int success);

    // -------------------------------------------------------------------------
    // 1. Bio-Father Step-In Evaluation (maras_step_in_evaluation)
    // -------------------------------------------------------------------------
    struct StepInEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        bool steppedIn{ false };
        double probability{ 0.0 };
        std::string reason;
    };

    bool EvaluateStepIn(const std::string& contextJson,
                        std::function<void(const StepInEvalResult&)> callback);

    // -------------------------------------------------------------------------
    // 2. Paternity Secret Evaluation (maras_paternity_evaluation)
    // -------------------------------------------------------------------------
    struct PaternityEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        std::string outcome;          // "none", "mother_confessed_presumed", "presumed_father_learnt", "mother_confessed_bio", "bio_father_learnt", "mother_confessed_both"
        std::string partnerReaction;  // "none", "divorce", "breakup"
        double outcomeProbability{ 0.0 };
        double reactionProbability{ 0.0 };
        std::string summary;
    };

    bool EvaluatePaternity(const std::string& contextJson,
                           bool isSecretForPresumedFather,
                           std::function<void(const PaternityEvalResult&)> callback);

    // -------------------------------------------------------------------------
    // 3. Player-Partner Agency / Infidelity Evaluation (maras_player_partner_cheating)
    // -------------------------------------------------------------------------
    struct PartnerAgencyEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        bool willCheat{ false };
        bool willBreakup{ false };
        bool willConfess{ false };
        bool willSuggest{ false };
        int chosenCandidateIndex{ -1 };  // 0-based index into shortlist when selected via Jev c_1..c_N
        std::string chosenCandidateName;
        std::string reasoning;
    };

    bool EvaluatePartnerAgency(const std::string& contextJson,
                               const std::string& evalMode,
                               bool canConfess,
                               const std::string& partnerName,
                               const std::string& sideLoverName,
                               const std::string& affairStage,
                               std::function<void(const PartnerAgencyEvalResult&)> callback);

    // -------------------------------------------------------------------------
    // 4. Matchmaking Verification Evaluation (maras_matchmaking_verify)
    //    - action == "breakup": unilateral (wants_breakup_a || wants_breakup_b)
    //    - action != "breakup": bilateral (willing_a && willing_b)
    // -------------------------------------------------------------------------
    struct MatchmakingVerifyEvalResult {
        bool success{ false };
        bool usedDecisions{ false };
        LLMFailureType failureType{ LLMFailureType::RequestFailed };
        std::string failureDetail;
        bool suitable{ false };
        double probA{ 0.0 };
        double probB{ 0.0 };
        std::string reason;
    };

    bool EvaluateMatchmakingVerify(const std::string& contextJson,
                                   const std::string& action,
                                   const std::string& targetStatus,
                                   const std::string& actorAName,
                                   const std::string& actorBName,
                                   std::function<void(const MatchmakingVerifyEvalResult&)> callback);

}  // namespace MARAS::SkyrimNetDecisions

