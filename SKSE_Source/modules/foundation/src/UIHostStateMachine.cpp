#include "maras/foundation/UIHostStateMachine.h"

namespace MARAS::Foundation {

    UIModeSyncPlan UIHostStateMachine::PlanSync(
        bool wantActive,
        bool wantPause,
        bool cancelSupported) noexcept {
        UIModeSyncPlan plan{};
        if (!wantActive) {
            plan.releaseInstead = true;
            return plan;
        }

        if (!isViewShown_) {
            isViewShown_ = true;
            plan.showView = true;
        }
        plan.raiseView = true;
        plan.setEscapeCapture = true;

        if (focusOwned_) {
            const bool needReclaim = releasePending_;
            if (needReclaim && !cancelSupported) {
                // Pre-1.1.0 host: in-flight ReleaseUIMode cannot be withdrawn; wait for UIModeExited to re-sync.
                plan.waitForExitToResync = true;
                return plan;
            }
            const bool needPauseUpdate = (appliedPause_ != wantPause);
            if (needReclaim || needPauseUpdate) {
                plan.requestUIMode = true;
                plan.requestPause = wantPause;
                plan.requestReclaim = needReclaim && cancelSupported;
            }
            return plan;
        }

        if (uiModeRequested_) {
            if (appliedPause_ != wantPause && cancelSupported) {
                plan.cancelPendingRequestFirst = true;
                plan.requestUIMode = true;
                plan.requestPause = wantPause;
                plan.requestReclaim = releasePending_ && cancelSupported;
            }
            return plan;
        }

        if (releasePending_ && !cancelSupported) {
            // Pre-1.1.0 host: wait for UIModeExited to finish the in-flight release and re-sync.
            plan.waitForExitToResync = true;
            return plan;
        }

        uiModeRequested_ = true;
        plan.requestUIMode = true;
        plan.requestPause = wantPause;
        plan.requestReclaim = releasePending_ && cancelSupported;
        return plan;
    }

    bool UIHostStateMachine::OnCancelBeforeRequestResult(bool cancelOk) noexcept {
        if (!cancelOk) {
            return false;
        }
        // Replacing in-flight request with a fresh RequestUIMode call.
        uiModeRequested_ = true;
        return true;
    }

    void UIHostStateMachine::OnRequestUIModeResult(
        bool ok,
        bool appliedPause,
        bool clearedReclaim) noexcept {
        if (ok) {
            appliedPause_ = appliedPause;
            if (clearedReclaim) {
                releasePending_ = false;
            }
        } else if (!focusOwned_) {
            uiModeRequested_ = false;
        }
    }

    UIModeReleasePlan UIHostStateMachine::PlanRelease(
        bool viewInitialized,
        bool cancelSupported) noexcept {
        UIModeReleasePlan plan{};
        if (!viewInitialized) {
            focusOwned_ = false;
            uiModeRequested_ = false;
            releasePending_ = false;
            appliedPause_ = false;
            isViewShown_ = false;
            plan.drainAfterFocusReleased = true;
            return plan;
        }

        if (isViewShown_) {
            isViewShown_ = false;
            plan.hideView = true;
        }

        if (uiModeRequested_ && cancelSupported) {
            plan.cancelPendingRequest = true;
            // Caller resolves uiModeRequested_ / releasePending_ via OnCancelBeforeReleaseResult.
            return plan;
        }

        if (focusOwned_) {
            if (!releasePending_) {
                releasePending_ = true;
                plan.releaseUIMode = true;
            }
        } else if (uiModeRequested_) {
            // Host < 1.1.0: RequestUIMode is still in flight; mark releasePending_ so UIModeEntered releases cleanly.
            releasePending_ = true;
            plan.waitForEnterToRelease = true;
        } else if (!releasePending_) {
            appliedPause_ = false;
            plan.drainAfterFocusReleased = true;
        }
        return plan;
    }

    bool UIHostStateMachine::OnCancelBeforeReleaseResult(bool cancelOk) noexcept {
        if (cancelOk) {
            uiModeRequested_ = false;
            if (!focusOwned_ && !releasePending_) {
                appliedPause_ = false;
                return true;
            }
            return false;
        }
        // CancelUIModeRequest returned Denied: UIModeEntered or UIModeRefused is already en route.
        // Keep uiModeRequested_ = true and mark releasePending_ = true; never drain tasks prematurely.
        releasePending_ = true;
        return false;
    }

    bool UIHostStateMachine::OnReleaseUIModeResult(bool ok) noexcept {
        if (ok) {
            return false;
        }
        releasePending_ = false;
        focusOwned_ = false;
        uiModeRequested_ = false;
        appliedPause_ = false;
        return true;
    }

    UIModeEnteredAction UIHostStateMachine::OnUIModeEntered(bool shouldKeep) noexcept {
        uiModeRequested_ = false;
        modalRefuseRetryCount_ = 0;
        focusOwned_ = true;

        const bool wasReleasePending = releasePending_;
        releasePending_ = false;
        if (wasReleasePending && !shouldKeep) {
            return UIModeEnteredAction::ReleaseNow;
        }
        return UIModeEnteredAction::SyncNow;
    }

    UIModeExitPlan UIHostStateMachine::OnUIModeExited(
        bool modalActive,
        bool dashboardVisible) noexcept {
        UIModeExitPlan plan{};
        const bool wasReleasePending = releasePending_;
        releasePending_ = false;
        focusOwned_ = false;
        uiModeRequested_ = false;
        appliedPause_ = false;

        if (modalActive) {
            plan.scheduleModalReacquire = true;
            return plan;
        }

        if (wasReleasePending && dashboardVisible) {
            plan.resyncDashboard = true;
            return plan;
        }

        plan.closeDashboard = dashboardVisible;
        if (isViewShown_) {
            isViewShown_ = false;
            plan.hideView = true;
        }
        plan.drainAfterFocusReleased = true;
        return plan;
    }

    UIModeRefusedPlan UIHostStateMachine::OnUIModeRefused(
        bool modalActive,
        bool dashboardVisible) noexcept {
        UIModeRefusedPlan plan{};
        uiModeRequested_ = false;
        releasePending_ = false;
        focusOwned_ = false;
        appliedPause_ = false;

        if (modalActive) {
            ++modalRefuseRetryCount_;
            plan.retryAttempt = modalRefuseRetryCount_;
            plan.scheduleModalRetry = (modalRefuseRetryCount_ <= kMaxModalRefuseRetries);
            return plan;
        }

        plan.closeDashboard = dashboardVisible;
        if (isViewShown_) {
            isViewShown_ = false;
            plan.hideView = true;
        }
        plan.drainAfterFocusReleased = true;
        return plan;
    }

    void UIHostStateMachine::ResetOnViewDestroyedOrRenderDead() noexcept {
        isViewShown_ = false;
        focusOwned_ = false;
        uiModeRequested_ = false;
        releasePending_ = false;
        appliedPause_ = false;
        modalRefuseRetryCount_ = 0;
    }

}  // namespace MARAS::Foundation
