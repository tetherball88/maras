#pragma once

#include "maras/app/AppContext.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MARAS::App {

    struct LlmBatchFailureSummary {
        bool hasRequestFailure{ false };
        bool hasInvalidJson{ false };
        std::size_t failedCount{ 0 };
        std::string description;
    };

    struct FailedLlmModalRequest {
        std::string batchTypeName;
        std::string description;
        bool hasRequestFailure{ false };
        bool hasInvalidJson{ false };
        std::string llmVariant{ "gamemaster_evaluation" };
        std::function<void()> onRetry;
        std::function<void()> onIgnore;
    };

    /// Displays the in-game Skyrim/UI retry modal for a partially or completely failed LLM batch.
    /// Guards both retry and ignore callbacks with `expectedEpoch` and `GetPapyrusMutex()`.
    void ShowInGameFailedLLMModal(FailedLlmModalRequest request,
                                  SessionEpoch expectedEpoch = GetSessionEpoch());

    /// Legacy-compatible convenience overload for batch failure retry prompts.
    void ShowInGameFailedLLMModal(std::string batchTypeName,
                                  std::string description,
                                  bool hasRequestFailure,
                                  bool hasInvalidJson,
                                  std::function<void()> onRetry,
                                  std::function<void()> onIgnore,
                                  std::string llmVariant = "gamemaster_evaluation",
                                  SessionEpoch expectedEpoch = GetSessionEpoch());

    /// Internal non-template completion dispatcher used by `RunLlmBatch<PayloadT>`.
    /// Checks `expectedEpoch`, acquires `GetPapyrusMutex()`, and either opens the retry modal or calls `onProceed`.
    void DispatchLlmBatchCompletion(SessionEpoch expectedEpoch,
                                    std::string batchTypeName,
                                    std::string llmVariant,
                                    LlmBatchFailureSummary summary,
                                    std::function<void()> onRetry,
                                    std::function<void()> onProceed);

    template <typename PayloadT>
    struct LlmBatchTaskItem {
        PayloadT payload{};
        bool completed{ false };
        bool success{ false };
        bool isInvalidJson{ false };
        std::string failureReason{};
    };

    /// Pure data batch container with ZERO callback members to prevent std::shared_ptr cycles.
    template <typename PayloadT>
    struct LlmBatchState {
        SessionEpoch epoch{ 0 };
        std::string batchTypeName;
        std::string llmVariant{ "gamemaster_evaluation" };
        std::vector<LlmBatchTaskItem<PayloadT>> tasks;
        std::atomic<std::size_t> remaining{ 0 };
        std::mutex mutex;
    };

    /// Stateless batch hooks kept separate from LlmBatchState so `state` and `callbacks` form a strict DAG.
    template <typename PayloadT>
    struct LlmBatchCallbacks {
        /// Called per pending task index. Must invoke `reportDone(success, isInvalidJson, failureReason, mutator)`
        /// when the underlying LLM call finishes, or return false if the request could not be dispatched.
        std::function<bool(PayloadT& payload,
                           std::function<void(bool success,
                                              bool isInvalidJson,
                                              std::string failureReason,
                                              std::function<void(PayloadT&)> mutator)> reportDone)>
            submitTask;
        std::function<std::string(const PayloadT& payload, const std::string& failureReason)> formatFailureLine;
        std::function<void(const std::shared_ptr<LlmBatchState<PayloadT>>& state)> onAllResolved;
    };

    namespace detail {

        template <typename PayloadT>
        [[nodiscard]] LlmBatchFailureSummary SummarizeBatchFailures(const LlmBatchState<PayloadT>& state,
                                                                    const LlmBatchCallbacks<PayloadT>& callbacks) {
            LlmBatchFailureSummary summary{};
            for (const auto& item : state.tasks) {
                if (item.completed && item.success) {
                    continue;
                }
                ++summary.failedCount;
                if (item.isInvalidJson) {
                    summary.hasInvalidJson = true;
                } else {
                    summary.hasRequestFailure = true;
                }

                std::string line;
                if (callbacks.formatFailureLine) {
                    line = callbacks.formatFailureLine(item.payload, item.failureReason);
                } else {
                    line = item.failureReason;
                }
                if (!line.empty()) {
                    if (!summary.description.empty()) {
                        summary.description += '\n';
                    }
                    summary.description += line;
                }
            }
            return summary;
        }

        template <typename PayloadT>
        void FinishPendingBatch(const std::shared_ptr<LlmBatchState<PayloadT>>& state,
                                const std::shared_ptr<const LlmBatchCallbacks<PayloadT>>& callbacks);

        template <typename PayloadT>
        void SubmitPendingBatchItems(const std::shared_ptr<LlmBatchState<PayloadT>>& state,
                                     const std::shared_ptr<const LlmBatchCallbacks<PayloadT>>& callbacks) {
            if (!state || !callbacks || !IsSessionEpochCurrent(state->epoch)) {
                return;
            }

            std::vector<std::size_t> pendingIndices;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                pendingIndices.reserve(state->tasks.size());
                for (std::size_t i = 0; i < state->tasks.size(); ++i) {
                    if (!state->tasks[i].completed || !state->tasks[i].success) {
                        state->tasks[i].completed = false;
                        state->tasks[i].success = false;
                        state->tasks[i].isInvalidJson = false;
                        state->tasks[i].failureReason.clear();
                        pendingIndices.push_back(i);
                    }
                }
            }

            if (pendingIndices.empty()) {
                if (callbacks->onAllResolved && IsSessionEpochCurrent(state->epoch)) {
                    callbacks->onAllResolved(state);
                }
                return;
            }

            // Sentinel +1 ensures callbacks completing synchronously mid-loop cannot prematurely finalize.
            state->remaining.store(pendingIndices.size() + 1u, std::memory_order_release);

            for (const std::size_t idx : pendingIndices) {
                if (!IsSessionEpochCurrent(state->epoch)) {
                    state->remaining.fetch_sub(1u, std::memory_order_acq_rel);
                    continue;
                }

                auto completedOnce = std::make_shared<std::atomic<bool>>(false);
                auto reportDone = [state, callbacks, idx, completedOnce](bool success,
                                                                         bool isInvalidJson,
                                                                         std::string failureReason,
                                                                         std::function<void(PayloadT&)> mutator) {
                    if (completedOnce->exchange(true, std::memory_order_acq_rel)) {
                        return;
                    }
                    if (!IsSessionEpochCurrent(state->epoch)) {
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (idx < state->tasks.size()) {
                            auto& item = state->tasks[idx];
                            if (mutator) {
                                mutator(item.payload);
                            }
                            item.completed = true;
                            item.success = success;
                            item.isInvalidJson = isInvalidJson;
                            item.failureReason = std::move(failureReason);
                        }
                    }
                    if (state->remaining.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
                        FinishPendingBatch(state, callbacks);
                    }
                };

                PayloadT payloadCopy;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    payloadCopy = state->tasks[idx].payload;
                }

                bool dispatched = false;
                if (callbacks->submitTask) {
                    dispatched = callbacks->submitTask(payloadCopy, reportDone);
                }
                if (!dispatched) {
                    reportDone(false, false, "Failed to dispatch LLM evaluation request", nullptr);
                }
            }

            if (state->remaining.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
                FinishPendingBatch(state, callbacks);
            }
        }

        template <typename PayloadT>
        void FinishPendingBatch(const std::shared_ptr<LlmBatchState<PayloadT>>& state,
                                const std::shared_ptr<const LlmBatchCallbacks<PayloadT>>& callbacks) {
            if (!state || !callbacks || !IsSessionEpochCurrent(state->epoch)) {
                return;
            }

            LlmBatchFailureSummary summary;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                summary = SummarizeBatchFailures(*state, *callbacks);
            }

            auto onRetry = [state, callbacks]() {
                SubmitPendingBatchItems(state, callbacks);
            };
            auto onProceed = [state, callbacks]() {
                if (callbacks->onAllResolved && IsSessionEpochCurrent(state->epoch)) {
                    callbacks->onAllResolved(state);
                }
            };

            DispatchLlmBatchCompletion(
                state->epoch,
                state->batchTypeName,
                state->llmVariant,
                std::move(summary),
                std::move(onRetry),
                std::move(onProceed));
        }

    }  // namespace detail

    /// Starts an epoch-guarded, retryable async LLM batch without self-capturing shared_ptr cycles.
    template <typename PayloadT>
    void RunLlmBatch(std::string batchTypeName,
                     std::string llmVariant,
                     std::vector<PayloadT> payloads,
                     LlmBatchCallbacks<PayloadT> callbacks,
                     SessionEpoch expectedEpoch = GetSessionEpoch()) {
        if (!IsSessionEpochCurrent(expectedEpoch)) {
            MARAS_LOG_DEBUG("[AsyncLlmBatchRunner] Dropping '{}' batch for stale epoch {}",
                            batchTypeName, expectedEpoch);
            return;
        }

        auto state = std::make_shared<LlmBatchState<PayloadT>>();
        state->epoch = expectedEpoch;
        state->batchTypeName = std::move(batchTypeName);
        state->llmVariant = llmVariant.empty() ? std::string("gamemaster_evaluation") : std::move(llmVariant);
        state->tasks.reserve(payloads.size());
        for (auto& payload : payloads) {
            LlmBatchTaskItem<PayloadT> item;
            item.payload = std::move(payload);
            state->tasks.push_back(std::move(item));
        }

        auto cbPtr = std::make_shared<const LlmBatchCallbacks<PayloadT>>(std::move(callbacks));
        detail::SubmitPendingBatchItems(state, cbPtr);
    }

}  // namespace MARAS::App
