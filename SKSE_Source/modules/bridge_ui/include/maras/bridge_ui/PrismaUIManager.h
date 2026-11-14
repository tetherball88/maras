#pragma once

#include "maras/bridge_ui/PrismaUI_API.h"

#include <RE/Skyrim.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace MARAS::Bridge::Ui {

    /// Manages the lifecycle, input hotkeys, focus state, and view visibility of the MARAS Prisma UI dashboard.
    class PrismaUIManager final : public RE::BSTEventSink<RE::InputEvent*>,
                                  public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static PrismaUIManager& GetSingleton();

        PrismaUIManager(const PrismaUIManager&) = delete;
        PrismaUIManager& operator=(const PrismaUIManager&) = delete;
        PrismaUIManager(PrismaUIManager&&) = delete;
        PrismaUIManager& operator=(PrismaUIManager&&) = delete;

        /// Acquires PRISMA_UI_API::IVPrismaUI1 and creates the view (no-op if PrismaUI.dll is absent).
        void Initialize();

        /// Starts listening for the toggle UI hotkey and menu open/close events.
        void StartListeningInput();

        /// Configures whether the initial relationship scan waits for the Loading Menu to close on new game.
        void OnNewGameStarted(bool deferInitialScan);

        /// Clears new-game scan deferral flags (called on normal save load).
        void ResetNewGameDeferral();

        /// Destroys the active Prisma view and resets transient focus/visibility state.
        void Destroy();

        /// Shows the MARAS dashboard view (or queues show until DOM ready).
        void Show();

        /// Hides the MARAS dashboard view and releases focus unless an active modal requires it.
        void Hide();

        /// Toggles dashboard visibility.
        void Toggle();

        [[nodiscard]] bool IsVisible() const noexcept { return isVisible_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsInitialized() const noexcept { return prismaAPI_ != nullptr && view_ != 0; }
        [[nodiscard]] bool IsDomReady() const noexcept { return isDomReady_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsDashboardPaused() const noexcept { return isPaused_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsDashboardOpen() const noexcept { return IsInitialized() && IsVisible(); }
        [[nodiscard]] bool IsFocusOwned() const noexcept { return focusOwned_.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsModalDisplayBlocked() const;
        [[nodiscard]] PrismaView GetView() const noexcept { return view_; }

        /// Registers a JavaScript listener on the active Prisma view.
        bool RegisterJSListener(const char* eventName, PRISMA_UI_API::JSListenerCallback callback);

        /// Called when React App reports `uiReady` on the game thread.
        void OnUIReady();

        /// Reconciles Prisma view visibility and focus with dashboard state, active modals, and blocking menus.
        void SyncViewVisibilityAndFocus();

        /// Queues a callback to run on the main thread once Prisma view focus has cleanly settled as released.
        void RunAfterFocusReleased(std::function<void()> task);

        /// Runs the deferred new-game relationship scan if pending and safe.
        void TryRunPendingNewGameScan();

        /// Invokes a named JS function (`window.<functionName>(<jsonData>)`) on the Prisma view if ready.
        void SendToUI(const std::string& functionName, const std::string& jsonData);

        /// Updates game-pause state while the dashboard is open.
        void SetDashboardPaused(bool pause);

        /// Reverts transient UI state on game revert / load transition.
        void Revert();

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

    private:
        PrismaUIManager() = default;
        ~PrismaUIManager() override = default;

        static void OnDomReady(PrismaView view);
        static void OnCloseUI(const char* argument);
        static void OnLogFromUI(const char* argument);
        static void OnRefocusView(const char* argument);

        void HandleToggleHotkey();
        void DrainAfterFocusReleasedTasks();
        void FlushPendingAfterDomReady();

        PRISMA_UI_API::IVPrismaUI1* prismaAPI_{ nullptr };
        PrismaView view_{ 0 };

        std::atomic<bool> isVisible_{ false };
        std::atomic<bool> focusOwned_{ false };
        std::atomic<bool> isDomReady_{ false };
        std::atomic<bool> isDestroying_{ false };
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
