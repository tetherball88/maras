#pragma once

#include "maras/app/ModalTypes.h"
#include "maras/state/SessionEpoch.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

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

    /// Thread-safe FIFO queue and dispatcher for standalone Prisma UI modal dialogs (`<PrismaModalHost />`).
    class PrismaModalManager {
    public:
        static PrismaModalManager& GetSingleton();
        static void RegisterAppGateway() noexcept;

        PrismaModalManager(const PrismaModalManager&) = delete;
        PrismaModalManager& operator=(const PrismaModalManager&) = delete;
        PrismaModalManager(PrismaModalManager&&) = delete;
        PrismaModalManager& operator=(PrismaModalManager&&) = delete;

        /// Enqueues a structured modal request. Safe from any thread; returns assigned `modalId`.
        std::string ShowModal(ModalRequest request, ModalCallback callback);

        /// Enqueues a pre-built modal spec JSON string. Safe from any thread; returns assigned `modalId`.
        std::string ShowRawJsonModal(const std::string& modalSpecJson, ModalCallback callback);

        /// Handles a `submitModalResponse` payload received from the Prisma frontend.
        bool HandleModalResponse(const nlohmann::json& payload);

        [[nodiscard]] bool HasActiveModal() const;
        [[nodiscard]] std::string GetActiveModalId() const;

        /// Pushes the active modal payload to the Prisma view if ready (no-op if none active).
        void SendActiveModalToUI(bool forceResend = false);

        /// Promotes the next queued modal on the main game thread and updates Prisma focus.
        void ProcessQueueOnGameThread();

        /// Cancels all active and queued modals cleanly (with `aborted = true`) on save load/revert.
        void Revert();

    private:
        PrismaModalManager() = default;
        ~PrismaModalManager() = default;

        struct ModalEntry {
            std::string modalId;
            std::string specJson;
            ModalCallback callback;
            MARAS::State::SessionEpoch expectedEpoch{ 0 };
        };

        std::string EnqueueEntry(std::string modalId, std::string specJson, ModalCallback callback);
        void ScheduleProcessQueue();

        mutable std::mutex mutex_;
        std::optional<ModalEntry> activeModal_;
        std::deque<ModalEntry> queue_;
        std::string lastSentModalId_;
        std::atomic<std::uint64_t> nextId_{ 1 };
    };

}  // namespace MARAS::Bridge::Ui
