#pragma once

#include "maras/bridge_ui/MagelightUI_API.h"

#include <RE/Skyrim.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace MARAS::Bridge::Ui {

    /// Manages lifecycle, input hotkeys, UI-mode focus, pause state, and visibility of the MARAS UI view.
    class UIManager final : public RE::BSTEventSink<RE::InputEvent*>,
                            public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        using JSListenerCallback = MAGELIGHT_API::JsListenerFn;

        static UIManager& GetSingleton();

        UIManager(const UIManager&) = delete;
        UIManager& operator=(const UIManager&) = delete;
        UIManager(UIManager&&) = delete;
        UIManager& operator=(UIManager&&) = delete;

        /// Acquires MAGELIGHT_API::MagelightApi4 and creates the persistent hidden UI view (no-op if host absent).
        void Initialize();

        /// Starts listening for the toggle UI hotkey and menu open/close events.
        void StartListeningInput();

        /// Configures whether the initial relationship scan waits for the Loading Menu to close on new game.
        void OnNewGameStarted(bool deferInitialScan);

        /// Clears new-game scan deferral flags (called on normal save load).
        void ResetNewGameDeferral();

        /// Shows the MARAS dashboard view (or queues show until DOM ready).
        void Show();

        /// Hides the MARAS dashboard view and releases UI mode unless an active modal requires it.
        void Hide();

        /// Toggles dashboard visibility.
        void Toggle();

        [[nodiscard]] bool IsVisible() const noexcept { return isVisible_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsInitialized() const noexcept {
            return magelight_ != nullptr && modHandle_ != 0 && view_ != 0 && magelight_->IsViewValid &&
                   magelight_->IsViewValid(view_);
        }
        [[nodiscard]] bool IsDomReady() const noexcept { return isDomReady_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsDashboardPaused() const noexcept { return isPaused_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsDashboardOpen() const noexcept { return IsInitialized() && IsVisible(); }
        [[nodiscard]] bool IsFocusOwned() const noexcept { return focusOwned_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsModalDisplayBlocked() const;
        [[nodiscard]] MAGELIGHT_API::ViewId GetView() const noexcept { return view_; }

        /// Registers a JavaScript listener on the active UI view.
        bool RegisterJSListener(const char* eventName, JSListenerCallback callback);

        /// Marshals a task from a render-thread UI callback onto the Skyrim game thread safely.
        void PostToGameThread(std::function<void()> task);

        /// Called when React App reports `uiReady` on the game thread.
        void OnUIReady();

        /// Reconciles UI view visibility, UI-mode focus, pause state, and Escape capture.
        void SyncViewVisibilityAndFocus();

        /// Queues a callback to run on the main thread once UI-mode focus has cleanly settled as released.
        void RunAfterFocusReleased(std::function<void()> task);

        /// Runs the deferred new-game relationship scan if pending and safe.
        void TryRunPendingNewGameScan();

        /// Invokes a named JS function (`window.<functionName>(<jsonData>)`) on the UI view if ready.
        void SendToUI(const std::string& functionName, const std::string& jsonData);

        /// Updates game-pause state while the dashboard is open.
        void SetDashboardPaused(bool pause);

        /// Reverts transient UI visibility/focus state on game revert / load transition (preserves persistent view).
        void Revert();

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

    private:
        UIManager() = default;
        ~UIManager() override = default;

        static void OnMagelightEvent(const MAGELIGHT_API::EventData* ev, void* user);
        static void OnCloseUI(const char* argument);
        static void OnLogFromUI(const char* argument);
        static void GameTaskTrampoline(void* userData);

        void HandleToggleHotkey();
        void ReleaseFocusAndUIMode();
        void DrainAfterFocusReleasedTasks();
        void FlushPendingAfterDomReady();

        const MAGELIGHT_API::MagelightApi4* magelight_{ nullptr };
        MAGELIGHT_API::ModId modHandle_{ 0 };
        MAGELIGHT_API::ViewId view_{ 0 };

        std::atomic<bool> isVisible_{ false };
        std::atomic<bool> isViewShown_{ false };
        std::atomic<bool> focusOwned_{ false };
        std::atomic<bool> uiModeRequested_{ false };
        std::atomic<bool> releasePending_{ false };
        std::atomic<bool> appliedPause_{ false };
        std::atomic<bool> isDomReady_{ false };
        std::atomic<bool> isPaused_{ true };
        std::atomic<bool> inputSinkRegistered_{ false };
        std::atomic<bool> menuSinkRegistered_{ false };
        std::atomic<bool> pendingShowAfterCreate_{ false };
        std::atomic<bool> pendingNewGameInitialScan_{ false };
        std::atomic<bool> awaitingInitialNewGameLoadScreenClose_{ false };

        mutable std::mutex stateMutex_;
        std::vector<std::function<void()>> afterFocusReleasedTasks_;
        std::vector<std::pair<std::string, std::string>> pendingJsCalls_;
    };

}  // namespace MARAS::Bridge::Ui
