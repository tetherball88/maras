#pragma once

#include "maras/app/ModalTypes.h"
#include "maras/state/ModalQueueStateMachine.h"
#include "maras/state/SessionEpoch.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MARAS::Bridge::Ui {

    using ModalTone = MARAS::App::ModalTone;
    using ModalSize = MARAS::App::ModalSize;
    using ModalButtonVariant = MARAS::App::ModalButtonVariant;
    using ModalControlType = MARAS::App::ModalControlType;
    using ModalSelectOption = MARAS::App::ModalSelectOption;
    using ModalChecklistItem = MARAS::App::ModalChecklistItem;
    using ModalControl = MARAS::App::ModalControl;
    using ModalButton = MARAS::App::ModalButton;
    using ModalRequest = MARAS::App::ModalRequest;
    using ModalResponse = MARAS::App::ModalResponse;
    using ModalCallback = MARAS::App::ModalCallback;

    /// Thread-safe FIFO queue and dispatcher for standalone UI modal dialogs (`<ModalHost />`).
    class ModalManager {
    public:
        static ModalManager& GetSingleton();
        static void RegisterAppGateway() noexcept;

        ModalManager(const ModalManager&) = delete;
        ModalManager& operator=(const ModalManager&) = delete;
        ModalManager(ModalManager&&) = delete;
        ModalManager& operator=(ModalManager&&) = delete;

        /// Enqueues a structured modal request. Safe from any thread; returns assigned `modalId`.
        std::string ShowModal(ModalRequest request, ModalCallback callback);

        /// Enqueues a pre-built modal spec JSON string. Safe from any thread; returns assigned `modalId`.
        std::string ShowRawJsonModal(const std::string& modalSpecJson, ModalCallback callback);

        /// Handles a `modalResponse` payload received from the UI frontend.
        bool HandleModalResponse(const nlohmann::json& payload);

        [[nodiscard]] bool HasActiveModal() const;
        [[nodiscard]] std::string GetActiveModalId() const;

        /// Pushes the active modal payload to the UI view if ready (no-op if none active).
        void SendActiveModalToUI(bool forceResend = false);

        /// Promotes the next queued modal on the main game thread and updates UI focus.
        void ProcessQueueOnGameThread();

        /// Cancels all active and queued modals cleanly (with `aborted = true`) on save load/revert.
        void Revert();

        /// Aborts all active and queued modals (with `aborted = true` and `defaultCancelButtonIndex`)
        /// when the UI host/view is unavailable, destroyed, or render-dead.
        void AbortAllModals(const char* reason = "UI unavailable");

    private:
        ModalManager() = default;
        ~ModalManager() = default;

        std::string EnqueueEntry(std::string modalId,
                                 std::string specJson,
                                 std::vector<std::string> buttonIds,
                                 std::int32_t defaultCancelButtonIndex,
                                 ModalCallback callback);
        void ScheduleProcessQueue();

        mutable std::mutex mutex_;
        MARAS::State::ModalQueueStateMachine queueState_;
        std::unordered_map<std::uint64_t, ModalCallback> callbacks_;
        std::atomic<std::uint64_t> nextId_{ 1 };
    };

}  // namespace MARAS::Bridge::Ui
