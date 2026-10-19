#pragma once

#include "PCH.h"
#include "ui/PrismaUI_API.h"
#include "utils/Common.h"
#include <functional>
#include <vector>

namespace MARAS {

    class PrismaUIManager :
        public RE::BSTEventSink<RE::InputEvent*>,
        public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static PrismaUIManager& GetSingleton() {
            static PrismaUIManager singleton;
            return singleton;
        }

        void Initialize();
        void Show();
        void Hide();
        void Toggle();
        void Destroy();
        void StartListeningInput();
        void StopListeningInput();

        bool EnsureViewCreated();
        void SyncViewVisibilityAndFocus();
        void OnUIReady();

        // New-game character creation (RaceSex Menu) and blocking native menu guards
        void OnNewGameStarted(bool deferInitialScan);
        void ResetNewGameDeferral();
        bool IsBlockingNativeMenuOpen() const;
        bool IsModalDisplayBlocked() const;
        void TryRunPendingNewGameScan();

        // Schedules a callback on the SKSE main thread after PrismaUI_FocusMenu (and Cursor Menu)
        // have finished closing on the UI thread. Used when transitioning from a MARAS modal
        // to another UI overlay such as SkyrimNet's text input dialog.
        void RunAfterFocusReleased(std::function<void()> callback);

        bool IsAvailable() const { return prismaUI != nullptr; }
        bool IsViewValid() const { return prismaUI && prismaUI->IsValid(view); }
        bool IsDomReady() const { return IsViewValid() && m_domReady; }
        bool IsDashboardOpen() const { return m_dashboardOpen; }
        bool IsVisible() const { return IsViewValid() && !prismaUI->IsHidden(view) && m_dashboardOpen; }

        void InteropCall(const char* functionName, const char* argument = "") {
            if (IsViewValid()) {
                prismaUI->InteropCall(view, functionName, argument ? argument : "");
            }
        }

        // JS Callback Listeners
        static void OnCloseMARAS(const char* argument);
        static void OnLog(const char* message);
        static void OnRefocusView(const char* argument);
        static void OnMarasAction(const char* argument);

        // RE::BSTEventSink<RE::InputEvent*>
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                              RE::BSTEventSource<RE::InputEvent*>* a_source) override;

        // RE::BSTEventSink<RE::MenuOpenCloseEvent>
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

    private:
        void TryFlushPostUnfocusCallbacks();

        PRISMA_UI_API::IVPrismaUI1* prismaUI = nullptr;
        PrismaView view = 0;
        bool initialized = false;
        bool isListeningInput = false;
        bool m_domReady = false;
        bool m_dashboardOpen = false;
        bool m_modalFocusLocked = false;
        bool m_deferModalsUntilRaceMenuClosed = false;
        bool m_pendingNewGameRelationshipScan = false;
        std::vector<std::function<void()>> m_postUnfocusCallbacks;

        static void OnDomReady(PrismaView view);

        PrismaUIManager() = default;
        ~PrismaUIManager() = default;
        PrismaUIManager(const PrismaUIManager&) = delete;
        PrismaUIManager& operator=(const PrismaUIManager&) = delete;
    };

}
