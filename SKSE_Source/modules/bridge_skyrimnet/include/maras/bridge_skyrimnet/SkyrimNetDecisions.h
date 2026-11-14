#pragma once

#include "RE/Skyrim.h"
#include "maras/app/ModalTypes.h"
#include "maras/app/SkyrimNetGateway.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace MARAS::Bridge::SkyrimNet::Decisions {

    // Returns true if SkyrimNet is available and exports PublicSendCustomDecisionToLLM (SkyrimNet API v12+).
    [[nodiscard]] bool IsDecisionsAvailable();

    // Returns true if skyrimnetUseDecisions is enabled in WorldStore settings and IsDecisionsAvailable() is true.
    [[nodiscard]] bool ShouldUseDecisions();

    // Returns true if a failed decision callback indicates that SkyrimNet has no 'decisions' model route configured
    // ("error": "no_decisions_route"), meaning the caller should fall back to the Chat LLM prompt.
    [[nodiscard]] bool ShouldFallbackToChatOnFailure(std::string_view resultJson, int success);

    // Collapses CR/LF/tabs and repeated whitespace into single spaces and trims ends so dynamic
    // strings inside [ criteria ] blocks never introduce embedded newlines.
    [[nodiscard]] std::string SanitizeSingleLine(std::string_view text);

    // Sanitizes a dynamic criterion key so it contains only [A-Za-z0-9_-].
    [[nodiscard]] std::string SanitizeKey(std::string_view rawKey);

    struct ParsedDecisionResponse {
        bool valid{ false };
        bool noDecisionsRoute{ false };
        MARAS::App::LLMFailureType failureType{ MARAS::App::LLMFailureType::RequestFailed };
        std::string errorMessage;
        std::string primaryAnswer;
        nlohmann::json answers;

        [[nodiscard]] std::optional<double> GetNoulProbability(std::string_view questionId) const;
        [[nodiscard]] bool GetNoulBool(std::string_view questionId, bool defaultVal = false, double threshold = 0.5) const;
        [[nodiscard]] std::string GetChoice(std::string_view questionId, std::string_view defaultVal = "") const;
        [[nodiscard]] double GetChoiceProbability(std::string_view questionId, std::string_view choiceKey) const;
    };

    [[nodiscard]] ParsedDecisionResponse ParseDecisionResult(const char* answer, const char* resultJson, int success);

    bool EvaluateStepIn(const std::string& contextJson,
                        std::function<void(const MARAS::App::StepInEvalResult&)> callback);

    bool EvaluatePaternity(const std::string& contextJson,
                           bool isSecretForPresumedFather,
                           std::function<void(const MARAS::App::PaternityEvalResult&)> callback);

    bool EvaluatePartnerAgency(const std::string& contextJson,
                               const std::string& evalMode,
                               bool canConfess,
                               const std::string& partnerName,
                               const std::string& sideLoverName,
                               const std::string& affairStage,
                               std::function<void(const MARAS::App::PartnerAgencyEvalResult&)> callback);

    bool EvaluateMatchmakingVerify(const std::string& contextJson,
                                   const std::string& action,
                                   const std::string& targetStatus,
                                   const std::string& actorAName,
                                   const std::string& actorBName,
                                   std::function<void(const MARAS::App::MatchmakingVerifyEvalResult&)> callback);

}  // namespace MARAS::Bridge::SkyrimNet::Decisions
