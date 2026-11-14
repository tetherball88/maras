#include "maras/app/AsyncLlmBatchRunner.h"

#include "maras/app/AppContext.h"
#include "maras/app/ModalTypes.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"

#include <mutex>
#include <utility>

namespace MARAS::App {

    void ShowInGameFailedLLMModal(FailedLlmModalRequest request, SessionEpoch expectedEpoch) {
        if (!IsSessionEpochCurrent(expectedEpoch)) {
            MARAS_LOG_DEBUG("[AsyncLlmBatchRunner] Skipping failure modal '{}' from stale epoch {}",
                            request.batchTypeName, expectedEpoch);
            return;
        }

        if (!SKSE::GetTaskInterface()) {
            MARAS_LOG_ERROR("[AsyncLlmBatchRunner] No SKSE task interface for '{}' failure modal; falling back to ignore",
                            request.batchTypeName);
            std::lock_guard lock(GetPapyrusMutex());
            if (IsSessionEpochCurrent(expectedEpoch) && request.onIgnore) {
                request.onIgnore();
            }
            return;
        }

        auto& skyrimNet = GetSkyrimNetGateway();
        ModalRequest modalReq;
        modalReq.modalType = "generic";
        modalReq.title = "LLM Evaluation Failed";
        modalReq.subtitle = request.batchTypeName;
        modalReq.message =
            "Some LLM evaluations in this batch failed. Choose Retry to resend failed requests, or Ignore to skip them.";
        modalReq.tone = ModalTone::Warning;
        modalReq.size = ModalSize::Md;
        modalReq.defaultCancelButtonIndex = 1;

        if (!request.description.empty()) {
            modalReq.controls.push_back(ModalControl::MakeInfoBox(
                "error_details",
                "Failed Evaluations",
                request.description,
                ModalTone::Warning));
        }

        const LLMFailureType primaryFailureType =
            request.hasRequestFailure ? LLMFailureType::RequestFailed : LLMFailureType::InvalidJson;
        const std::string variant =
            request.llmVariant.empty() ? std::string("gamemaster_evaluation") : request.llmVariant;
        const std::string guidanceTitle = skyrimNet.GetLLMFailureGuidanceTitle(primaryFailureType);
        const std::string guidanceBody =
            skyrimNet.GetLLMBatchFailureGuidance(request.hasRequestFailure, request.hasInvalidJson, variant);

        if (!guidanceBody.empty()) {
            modalReq.controls.push_back(ModalControl::MakeInfoBox(
                "error_guidance",
                guidanceTitle.empty() ? std::string("Troubleshooting Guidance") : guidanceTitle,
                guidanceBody,
                ModalTone::Default));
        }

        modalReq.buttons.push_back(ModalButton::Make("retry", "Retry Failed", ModalButtonVariant::Primary));
        modalReq.buttons.push_back(ModalButton::Make("ignore", "Ignore", ModalButtonVariant::Secondary));

        GetModalGateway().ShowModal(
            std::move(modalReq),
            [expectedEpoch,
             batchName = request.batchTypeName,
             onRetry = std::move(request.onRetry),
             onIgnore = std::move(request.onIgnore)](const ModalResponse& resp) mutable {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    MARAS_LOG_DEBUG("[AsyncLlmBatchRunner] Discarding '{}' retry modal response for stale epoch {}",
                                    batchName, expectedEpoch);
                    return;
                }

                const bool isRetry = !resp.aborted && (resp.buttonIndex == 0 || resp.buttonId == "retry");
                std::lock_guard lock(GetPapyrusMutex());
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                if (isRetry) {
                    if (onRetry) {
                        onRetry();
                    }
                } else {
                    if (onIgnore) {
                        onIgnore();
                    }
                }
            });
    }

    void ShowInGameFailedLLMModal(std::string batchTypeName,
                                  std::string description,
                                  bool hasRequestFailure,
                                  bool hasInvalidJson,
                                  std::function<void()> onRetry,
                                  std::function<void()> onIgnore,
                                  std::string llmVariant,
                                  SessionEpoch expectedEpoch) {
        FailedLlmModalRequest req{};
        req.batchTypeName = std::move(batchTypeName);
        req.description = std::move(description);
        req.hasRequestFailure = hasRequestFailure;
        req.hasInvalidJson = hasInvalidJson;
        req.llmVariant = std::move(llmVariant);
        req.onRetry = std::move(onRetry);
        req.onIgnore = std::move(onIgnore);
        ShowInGameFailedLLMModal(std::move(req), expectedEpoch);
    }

    void DispatchLlmBatchCompletion(SessionEpoch expectedEpoch,
                                    std::string batchTypeName,
                                    std::string llmVariant,
                                    LlmBatchFailureSummary summary,
                                    std::function<void()> onRetry,
                                    std::function<void()> onProceed) {
        if (!IsSessionEpochCurrent(expectedEpoch)) {
            MARAS_LOG_DEBUG("[AsyncLlmBatchRunner] Ignoring completion of '{}' batch from stale epoch {}",
                            batchTypeName, expectedEpoch);
            return;
        }

        auto executeCompletion = [expectedEpoch,
                                  batchTypeName = std::move(batchTypeName),
                                  llmVariant = std::move(llmVariant),
                                  summary = std::move(summary),
                                  onRetry = std::move(onRetry),
                                  onProceed = std::move(onProceed)]() mutable {
            std::lock_guard lock(GetPapyrusMutex());
            if (!IsSessionEpochCurrent(expectedEpoch)) {
                MARAS_LOG_DEBUG("[AsyncLlmBatchRunner] Dropped '{}' completion on main thread due to epoch change",
                                batchTypeName);
                return;
            }

            if (summary.failedCount > 0) {
                MARAS_LOG_WARN("[AsyncLlmBatchRunner] '{}' finished with {} failed task(s) (reqFail={}, invalidJson={})",
                               batchTypeName, summary.failedCount, summary.hasRequestFailure, summary.hasInvalidJson);
                FailedLlmModalRequest modalReq{};
                modalReq.batchTypeName = std::move(batchTypeName);
                modalReq.description = std::move(summary.description);
                modalReq.hasRequestFailure = summary.hasRequestFailure;
                modalReq.hasInvalidJson = summary.hasInvalidJson;
                modalReq.llmVariant = std::move(llmVariant);
                modalReq.onRetry = std::move(onRetry);
                modalReq.onIgnore = std::move(onProceed);
                ShowInGameFailedLLMModal(std::move(modalReq), expectedEpoch);
                return;
            }

            if (onProceed) {
                onProceed();
            }
        };

        if (SKSE::GetTaskInterface()) {
            AddEpochTask(expectedEpoch, "AsyncLlmBatchRunner::Complete", std::move(executeCompletion));
        } else {
            executeCompletion();
        }
    }

}  // namespace MARAS::App
