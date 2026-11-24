#pragma once

#include "maras/state/SessionEpoch.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::State {

    struct ModalQueueItem {
        std::uint64_t token{ 0 };
        std::string modalId{};
        std::string specJson{};
        std::vector<std::string> buttonIds{};
        std::int32_t defaultCancelButtonIndex{ 1 };
        SessionEpoch expectedEpoch{ 0 };
    };

    struct ModalEnqueueOutcome {
        bool activatedImmediately{ false };
        std::size_t pendingQueueSize{ 0 };
    };

    struct ModalSendPayload {
        std::string modalId{};
        std::string specJson{};
    };

    struct ModalPromoteOutcome {
        bool hasActive{ false };
        std::string promotedModalId{};
        std::vector<std::uint64_t> droppedStaleTokens{};
    };

    struct ModalResolveOutcome {
        bool matched{ false };
        ModalQueueItem resolved{};
        std::int32_t resolvedButtonIndex{ -1 };
        bool callbackEpochValid{ false };
        bool hasMoreModals{ false };
        std::string promotedModalId{};
        std::vector<std::uint64_t> droppedStaleTokens{};
    };

    /// Pure, engine-free FIFO modal queue & SessionEpoch-guarded lifecycle state machine
    /// backing `ModalManager`.
    class ModalQueueStateMachine {
    public:
        [[nodiscard]] ModalEnqueueOutcome Enqueue(ModalQueueItem item);

        [[nodiscard]] bool HasActiveOrQueued() const noexcept {
            return active_.has_value() || !queue_.empty();
        }

        [[nodiscard]] std::string GetActiveModalId() const {
            return active_ ? active_->modalId : std::string{};
        }

        [[nodiscard]] std::size_t QueuedCount() const noexcept { return queue_.size(); }

        [[nodiscard]] std::optional<ModalSendPayload> TakePayloadToSend(bool forceResend);

        [[nodiscard]] ModalPromoteOutcome PruneAndPromote(SessionEpoch currentEpoch);

        [[nodiscard]] ModalResolveOutcome ResolveActive(
            std::string_view incomingModalId,
            std::string_view buttonId,
            std::int32_t buttonIndex,
            bool aborted,
            SessionEpoch currentEpoch);

        /// Drains active + all queued modals in FIFO order (for UI unavailable / ViewDestroyed / RenderDead / Revert).
        [[nodiscard]] std::vector<ModalQueueItem> AbortAll();

    private:
        std::optional<ModalQueueItem> active_;
        std::deque<ModalQueueItem> queue_;
        std::string lastSentModalId_;
    };

}  // namespace MARAS::State
