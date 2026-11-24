#include "maras/state/ModalQueueStateMachine.h"

#include <utility>

namespace MARAS::State {

    ModalEnqueueOutcome ModalQueueStateMachine::Enqueue(ModalQueueItem item) {
        ModalEnqueueOutcome out{};
        if (!active_.has_value()) {
            active_ = std::move(item);
            lastSentModalId_.clear();
            out.activatedImmediately = true;
            out.pendingQueueSize = queue_.size();
        } else {
            queue_.push_back(std::move(item));
            out.activatedImmediately = false;
            out.pendingQueueSize = queue_.size();
        }
        return out;
    }

    std::optional<ModalSendPayload> ModalQueueStateMachine::TakePayloadToSend(bool forceResend) {
        if (!active_.has_value()) {
            return std::nullopt;
        }
        if (!forceResend && lastSentModalId_ == active_->modalId) {
            return std::nullopt;
        }
        lastSentModalId_ = active_->modalId;
        return ModalSendPayload{
            .modalId = active_->modalId,
            .specJson = active_->specJson
        };
    }

    ModalPromoteOutcome ModalQueueStateMachine::PruneAndPromote(SessionEpoch currentEpoch) {
        ModalPromoteOutcome out{};
        if (active_.has_value() && active_->expectedEpoch != currentEpoch) {
            out.droppedStaleTokens.push_back(active_->token);
            active_.reset();
            lastSentModalId_.clear();
        }
        while (!queue_.empty() && queue_.front().expectedEpoch != currentEpoch) {
            out.droppedStaleTokens.push_back(queue_.front().token);
            queue_.pop_front();
        }
        if (!active_.has_value() && !queue_.empty()) {
            active_ = std::move(queue_.front());
            queue_.pop_front();
            lastSentModalId_.clear();
            out.promotedModalId = active_->modalId;
        }
        out.hasActive = active_.has_value();
        return out;
    }

    ModalResolveOutcome ModalQueueStateMachine::ResolveActive(
        std::string_view incomingModalId,
        std::string_view buttonId,
        std::int32_t buttonIndex,
        bool aborted,
        SessionEpoch currentEpoch) {
        ModalResolveOutcome out{};
        if (!active_.has_value() || active_->modalId != incomingModalId) {
            return out;
        }

        out.matched = true;
        out.resolved = std::move(*active_);
        active_.reset();
        lastSentModalId_.clear();

        while (!queue_.empty() && queue_.front().expectedEpoch != currentEpoch) {
            out.droppedStaleTokens.push_back(queue_.front().token);
            queue_.pop_front();
        }
        if (!queue_.empty()) {
            active_ = std::move(queue_.front());
            queue_.pop_front();
            out.promotedModalId = active_->modalId;
            out.hasMoreModals = true;
        }

        out.callbackEpochValid = (out.resolved.expectedEpoch == currentEpoch);
        std::int32_t finalIndex = buttonIndex;
        if (finalIndex < 0 && !buttonId.empty()) {
            for (std::size_t i = 0; i < out.resolved.buttonIds.size(); ++i) {
                if (out.resolved.buttonIds[i] == buttonId) {
                    finalIndex = static_cast<std::int32_t>(i);
                    break;
                }
            }
        }
        if (aborted && finalIndex < 0) {
            finalIndex = out.resolved.defaultCancelButtonIndex;
        }
        out.resolvedButtonIndex = finalIndex;
        return out;
    }

    std::vector<ModalQueueItem> ModalQueueStateMachine::AbortAll() {
        std::vector<ModalQueueItem> cancelled;
        if (active_.has_value()) {
            cancelled.push_back(std::move(*active_));
            active_.reset();
        }
        while (!queue_.empty()) {
            cancelled.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        lastSentModalId_.clear();
        return cancelled;
    }

}  // namespace MARAS::State
