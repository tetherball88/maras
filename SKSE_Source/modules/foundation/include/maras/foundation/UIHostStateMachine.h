#pragma once

#include <cstdint>

namespace MARAS::Foundation {

    struct UIModeSyncPlan {
        bool releaseInstead{ false };
        bool showView{ false };
        bool raiseView{ false };
        bool setEscapeCapture{ false };
        bool waitForExitToResync{ false };
        bool cancelPendingRequestFirst{ false };
        bool requestUIMode{ false };
        bool requestPause{ false };
        bool requestReclaim{ false };
    };

    struct UIModeReleasePlan {
        bool cancelPendingRequest{ false };
        bool hideView{ false };
        bool releaseUIMode{ false };
        bool waitForEnterToRelease{ false };
        bool drainAfterFocusReleased{ false };
    };

    enum class UIModeEnteredAction : std::uint8_t {
        SyncNow = 0,
        ReleaseNow = 1
    };

    struct UIModeExitPlan {
        bool scheduleModalReacquire{ false };
        bool resyncDashboard{ false };
        bool closeDashboard{ false };
        bool hideView{ false };
        bool drainAfterFocusReleased{ false };
    };

    struct UIModeRefusedPlan {
        bool scheduleModalRetry{ false };
        std::uint32_t retryAttempt{ 0 };
        bool closeDashboard{ false };
        bool hideView{ false };
        bool drainAfterFocusReleased{ false };
    };

    /// Pure, engine-free UI-mode focus, pause, reclaim, and modal-recovery state machine
    /// driving `UIManager`'s Magelight UI lifecycle transitions.
    class UIHostStateMachine {
    public:
        static constexpr std::uint32_t kMaxModalRefuseRetries = 3u;

        [[nodiscard]] UIModeSyncPlan PlanSync(
            bool wantActive,
            bool wantPause,
            bool cancelSupported) noexcept;

        /// Commit result of `CancelUIModeRequest` invoked when `PlanSync().cancelPendingRequestFirst` was true.
        /// Returns true if the caller should proceed to call `RequestUIMode`.
        [[nodiscard]] bool OnCancelBeforeRequestResult(bool cancelOk) noexcept;

        /// Commit result of `RequestUIMode` invoked when `PlanSync().requestUIMode` was true.
        void OnRequestUIModeResult(bool ok, bool appliedPause, bool clearedReclaim) noexcept;

        [[nodiscard]] UIModeReleasePlan PlanRelease(
            bool viewInitialized,
            bool cancelSupported) noexcept;

        /// Commit result of `CancelUIModeRequest` when `PlanRelease().cancelPendingRequest` was true.
        /// Returns true if the caller should immediately drain `afterFocusReleased` tasks.
        [[nodiscard]] bool OnCancelBeforeReleaseResult(bool cancelOk) noexcept;

        /// Commit result of `ReleaseUIMode` when `PlanRelease().releaseUIMode` was true.
        /// Returns true if `ReleaseUIMode` failed and the caller should immediately drain `afterFocusReleased` tasks.
        [[nodiscard]] bool OnReleaseUIModeResult(bool ok) noexcept;

        [[nodiscard]] UIModeEnteredAction OnUIModeEntered(bool shouldKeep) noexcept;

        [[nodiscard]] UIModeExitPlan OnUIModeExited(
            bool modalActive,
            bool dashboardVisible) noexcept;

        [[nodiscard]] UIModeRefusedPlan OnUIModeRefused(
            bool modalActive,
            bool dashboardVisible) noexcept;

        void ResetOnViewDestroyedOrRenderDead() noexcept;

        void ResetModalRefuseRetries() noexcept { modalRefuseRetryCount_ = 0; }

        [[nodiscard]] bool CanDrainAfterFocusReleased() const noexcept {
            return !focusOwned_ && !uiModeRequested_ && !releasePending_;
        }

        [[nodiscard]] bool IsViewShown() const noexcept { return isViewShown_; }
        [[nodiscard]] bool IsFocusOwned() const noexcept { return focusOwned_; }
        [[nodiscard]] bool IsUIModeRequested() const noexcept { return uiModeRequested_; }
        [[nodiscard]] bool IsReleasePending() const noexcept { return releasePending_; }
        [[nodiscard]] bool AppliedPause() const noexcept { return appliedPause_; }
        [[nodiscard]] std::uint32_t ModalRefuseRetryCount() const noexcept { return modalRefuseRetryCount_; }

    private:
        bool isViewShown_{ false };
        bool focusOwned_{ false };
        bool uiModeRequested_{ false };
        bool releasePending_{ false };
        bool appliedPause_{ false };
        std::uint32_t modalRefuseRetryCount_{ 0 };
    };

}  // namespace MARAS::Foundation
