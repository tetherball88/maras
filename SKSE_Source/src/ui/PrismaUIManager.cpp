#include "ui/PrismaUIManager.h"
#include "core/NPCRelationshipManager.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/Settings.h"
#include <Windows.h>

namespace MARAS {

    void PrismaUIManager::OnNewGameStarted(bool deferInitialScan) {
        m_deferModalsUntilRaceMenuClosed = true;
        m_pendingNewGameRelationshipScan = deferInitialScan;
        MARAS_LOG_INFO("PrismaUIManager::OnNewGameStarted: deferring modals and initial relationship scan (deferInitialScan={}) until RaceSex Menu closes",
                       deferInitialScan);
    }

    void PrismaUIManager::ResetNewGameDeferral() {
        m_deferModalsUntilRaceMenuClosed = false;
        m_pendingNewGameRelationshipScan = false;
    }

    bool PrismaUIManager::IsBlockingNativeMenuOpen() const {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }
        return ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) ||
               ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
               ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
    }

    bool PrismaUIManager::IsModalDisplayBlocked() const {
        return m_deferModalsUntilRaceMenuClosed || IsBlockingNativeMenuOpen();
    }

    void PrismaUIManager::TryRunPendingNewGameScan() {
        if (!m_pendingNewGameRelationshipScan || IsModalDisplayBlocked()) {
            return;
        }

        m_pendingNewGameRelationshipScan = false;
        if (!Settings::GetSingleton().initialRelationshipScanDone) {
            MARAS_LOG_INFO("PrismaUIManager::TryRunPendingNewGameScan: character creation finished — running deferred new-game relationship scan");
            NPCRelationshipManager::GetSingleton().ScanExistingRelationships(/*isNewGame=*/true);
            Settings::GetSingleton().initialRelationshipScanDone = true;
            NPCRelationshipManager::GetSingleton().LogStatistics();
        }
    }

    void PrismaUIManager::StartListeningInput() {
        if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
            dm->RemoveEventSink(this);
            dm->AddEventSink(this);
            isListeningInput = true;
            MARAS_LOG_INFO("PrismaUIManager: started listening for input events");
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->RemoveEventSink<RE::MenuOpenCloseEvent>(this);
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
        }
    }

    void PrismaUIManager::StopListeningInput() {
        if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
            dm->RemoveEventSink(this);
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->RemoveEventSink<RE::MenuOpenCloseEvent>(this);
        }
        isListeningInput = false;
        MARAS_LOG_INFO("PrismaUIManager: stopped listening for input events");
    }

    RE::BSEventNotifyControl PrismaUIManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_source*/)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!a_event->opening && a_event->menuName == "PrismaUI_FocusMenu") {
            SKSE::GetTaskInterface()->AddTask([]() {
                auto& mgr = PrismaUIManager::GetSingleton();
                mgr.TryFlushPostUnfocusCallbacks();
                mgr.SyncViewVisibilityAndFocus();
            });
        }

        const bool isBlockingMenu = (a_event->menuName == RE::RaceSexMenu::MENU_NAME ||
                                     a_event->menuName == RE::LoadingMenu::MENU_NAME ||
                                     a_event->menuName == RE::MainMenu::MENU_NAME);

        if (!a_event->opening && a_event->menuName == RE::RaceSexMenu::MENU_NAME) {
            if (m_deferModalsUntilRaceMenuClosed) {
                MARAS_LOG_INFO("PrismaUIManager: RaceSex Menu closed — lifting new-game modal deferral");
                m_deferModalsUntilRaceMenuClosed = false;
            }
        }

        if (isBlockingMenu) {
            if (auto* taskIF = SKSE::GetTaskInterface()) {
                taskIF->AddTask([]() {
                    auto& mgr = PrismaUIManager::GetSingleton();
                    mgr.TryRunPendingNewGameScan();
                    if (!mgr.IsModalDisplayBlocked() && PrismaModalManager::GetSingleton().HasActiveModal()) {
                        PrismaModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/false);
                    }
                    mgr.SyncViewVisibilityAndFocus();
                });
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void PrismaUIManager::RunAfterFocusReleased(std::function<void()> callback) {
        if (!callback) {
            return;
        }

        SyncViewVisibilityAndFocus();

        auto* ui = RE::UI::GetSingleton();
        const bool focusMenuOpen = ui && ui->IsMenuOpen("PrismaUI_FocusMenu");
        const bool viewFocused = IsViewValid() && prismaUI->HasFocus(view);

        if (!focusMenuOpen && !viewFocused) {
            if (auto* taskIF = SKSE::GetTaskInterface()) {
                taskIF->AddTask([cb = std::move(callback)]() {
                    try {
                        cb();
                    } catch (const std::exception& e) {
                        MARAS_LOG_ERROR("PrismaUIManager::RunAfterFocusReleased immediate callback exception: {}", e.what());
                    }
                });
            }
            return;
        }

        MARAS_LOG_INFO("PrismaUIManager::RunAfterFocusReleased: deferring callback until PrismaUI_FocusMenu closes");
        m_postUnfocusCallbacks.push_back(std::move(callback));
    }

    void PrismaUIManager::TryFlushPostUnfocusCallbacks() {
        if (m_postUnfocusCallbacks.empty()) {
            return;
        }

        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen("PrismaUI_FocusMenu")) {
            return;
        }

        std::vector<std::function<void()>> callbacks;
        callbacks.swap(m_postUnfocusCallbacks);

        MARAS_LOG_INFO("PrismaUIManager::TryFlushPostUnfocusCallbacks: FocusMenu and Cursor Menu closed, scheduling {} callback(s)",
                       callbacks.size());

        if (auto* taskIF = SKSE::GetTaskInterface()) {
            taskIF->AddTask([callbacks = std::move(callbacks)]() {
                for (const auto& cb : callbacks) {
                    if (!cb) continue;
                    try {
                        cb();
                    } catch (const std::exception& e) {
                        MARAS_LOG_ERROR("PrismaUIManager::TryFlushPostUnfocusCallbacks exception: {}", e.what());
                    }
                }
            });
        }
    }

    RE::BSEventNotifyControl PrismaUIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_source*/)
    {
        if (!a_event || !*a_event) return RE::BSEventNotifyControl::kContinue;

        // While a Prisma modal is active, block the dashboard toggle hotkey and restore focus if lost on button press
        if (PrismaModalManager::GetSingleton().HasActiveModal()) {
            if (!IsModalDisplayBlocked() && IsViewValid() && m_domReady && !prismaUI->HasFocus(view)) {
                for (auto* event = *a_event; event; event = event->next) {
                    if (auto* button = event->AsButtonEvent(); button && button->IsDown()) {
                        SKSE::GetTaskInterface()->AddTask([]() {
                            PrismaUIManager::GetSingleton().SyncViewVisibilityAndFocus();
                        });
                        break;
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto& settings = Settings::GetSingleton();
        const uint32_t hotkeyCode = settings.toggleUIKey;
        if (hotkeyCode == 0) return RE::BSEventNotifyControl::kContinue;

        for (auto* event = *a_event; event; event = event->next) {
            auto* button = event->AsButtonEvent();
            if (!button || !button->IsDown()) continue;
            if (button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;

            if (button->GetIDCode() == hotkeyCode) {
                // Verify modifier states using Win32 GetAsyncKeyState
                const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

                MARAS_LOG_TRACE("PrismaUIManager: Target key 0x{:02X} pressed. Modifiers present: Ctrl={}, Shift={}, Alt={}. Required: Ctrl={}, Shift={}, Alt={}",
                                hotkeyCode, ctrlDown, shiftDown, altDown,
                                settings.toggleUIModCtrl, settings.toggleUIModShift, settings.toggleUIModAlt);

                if (settings.toggleUIModCtrl != ctrlDown) continue;
                if (settings.toggleUIModShift != shiftDown) continue;
                if (settings.toggleUIModAlt != altDown) continue;

                MARAS_LOG_INFO("PrismaUIManager: Hotkey 0x{:02X} matched (Ctrl={}, Shift={}, Alt={}) -> Toggling UI",
                               hotkeyCode, ctrlDown, shiftDown, altDown);

                SKSE::GetTaskInterface()->AddTask([]() {
                    PrismaUIManager::GetSingleton().Toggle();
                });
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void PrismaUIManager::Initialize() {
        if (initialized) {
            return;
        }

        MARAS_LOG_INFO("PrismaUIManager: Requesting PrismaUI API...");
        auto api = PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1);
        if (!api) {
            MARAS_LOG_INFO("PrismaUI not available — M.A.R.A.S will run without Prisma UI");
            StartListeningInput();
            return;
        }

        prismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(api);
        initialized = true;
        MARAS_LOG_INFO("PrismaUI initialized successfully for M.A.R.A.S");

        StartListeningInput();
    }

    bool PrismaUIManager::EnsureViewCreated() {
        if (!prismaUI) {
            MARAS_LOG_WARN("PrismaUIManager::EnsureViewCreated called but PrismaUI API is not available");
            return false;
        }

        if (view && prismaUI->IsValid(view)) {
            return true;
        }

        MARAS_LOG_INFO("PrismaUIManager: Creating view 'MARAS/index.html'...");
        m_domReady = false;
        m_modalFocusLocked = false;

        auto newView = prismaUI->CreateView("MARAS/index.html", OnDomReady);
        if (!newView) {
            MARAS_LOG_ERROR("Failed to create PrismaUI view for M.A.R.A.S");
            return false;
        }

        view = newView;
        prismaUI->RegisterJSListener(view, "closeMARAS", OnCloseMARAS);
        prismaUI->RegisterJSListener(view, "log", OnLog);
        prismaUI->RegisterJSListener(view, "refocusView", OnRefocusView);
        prismaUI->RegisterJSListener(view, "marasAction", OnMarasAction);

        MARAS_LOG_INFO("PrismaUI view created successfully for M.A.R.A.S: handle {}", newView);
        return true;
    }

    void PrismaUIManager::SyncViewVisibilityAndFocus() {
        if (!IsViewValid()) {
            return;
        }

        const bool blocked = IsModalDisplayBlocked();
        const bool hasModal = !blocked && PrismaModalManager::GetSingleton().HasActiveModal();
        const bool shouldBeVisible = !blocked && (m_dashboardOpen || hasModal);

        if (!shouldBeVisible) {
            if (prismaUI->HasFocus(view)) {
                prismaUI->Unfocus(view);
            }
            if (!prismaUI->IsHidden(view)) {
                prismaUI->Hide(view);
            }
            m_modalFocusLocked = false;
            return;
        }

        // Wait until Ultralight has finished creating the view on the UI thread (OnDomReady -> OnUIReady)
        if (!m_domReady) {
            return;
        }

        if (prismaUI->IsHidden(view)) {
            prismaUI->Show(view);
        }

        const bool shouldPauseGame = hasModal || (m_dashboardOpen && m_dashboardPaused);

        if (shouldPauseGame != m_modalFocusLocked && prismaUI->HasFocus(view)) {
            m_modalFocusLocked = false;
            prismaUI->Unfocus(view);
            // PrismaUI->Unfocus closes "PrismaUI_FocusMenu" asynchronously via Skyrim's UIMessageQueue.
            // Wait for MenuOpenCloseEvent("PrismaUI_FocusMenu", opening=false) (or refocusView) to re-focus
            // so the queued hide message does not strip focus/cursor right after Focus() is called.
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen("PrismaUI_FocusMenu")) {
                return;
            }
        }

        // FocusMenu (disableFocusMenu=false) MUST remain enabled so PrismaUI's Scaleform cursor/input menu
        // owns the mouse cursor and routes mouse + keyboard events to the web view.
        if (!prismaUI->HasFocus(view)) {
            const bool focused = prismaUI->Focus(view, /*pauseGame=*/shouldPauseGame, /*disableFocusMenu=*/false);
            m_modalFocusLocked = shouldPauseGame && focused;
            MARAS_LOG_INFO("PrismaUIManager: Focused view {} (pauseGame={}, disableFocusMenu=false, success={})",
                           view, shouldPauseGame, focused);
        }
    }

    void PrismaUIManager::SetDashboardPaused(bool paused) {
        m_dashboardPaused = paused;
        MARAS_LOG_INFO("PrismaUIManager::SetDashboardPaused: paused={}", m_dashboardPaused);
        if (m_dashboardOpen) {
            SyncViewVisibilityAndFocus();
        }
        if (IsDomReady()) {
            nlohmann::json data;
            data["paused"] = m_dashboardPaused;
            std::string jsonStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            InteropCall("onGamePausedChanged", jsonStr.c_str());
        }
    }

    void PrismaUIManager::OnUIReady() {
        if (!IsViewValid()) return;

        m_domReady = true;
        SyncViewVisibilityAndFocus();

        nlohmann::json statusData;
        statusData["status"] = "M.A.R.A.S Core Initialized";
        statusData["version"] = PROJECT_VERSION;
        std::string statusJson = statusData.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        InteropCall("receiveMARASStatus", statusJson.c_str());

        if (m_dashboardOpen) {
            InteropCall("openMARAS", "");
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
        } else {
            InteropCall("closeMARASView", "");
        }

        if (PrismaModalManager::GetSingleton().HasActiveModal()) {
            PrismaModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/true);
        }
    }

    void PrismaUIManager::OnDomReady(PrismaView readyView) {
        MARAS_LOG_INFO("PrismaUIManager: DOM ready callback received for view {}", readyView);
        SKSE::GetTaskInterface()->AddTask([readyView]() {
            try {
                auto& mgr = PrismaUIManager::GetSingleton();
                if (mgr.view != readyView || !mgr.IsViewValid()) return;
                mgr.OnUIReady();
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("PrismaUIManager::OnDomReady exception: {}", e.what());
            }
        });
    }

    void PrismaUIManager::Show() {
        if (!prismaUI) {
            MARAS_LOG_WARN("PrismaUIManager::Show called but PrismaUI API is not available");
            return;
        }

        if (IsBlockingNativeMenuOpen()) {
            MARAS_LOG_INFO("PrismaUIManager::Show() ignored while blocking native menu is open");
            return;
        }

        if (m_deferModalsUntilRaceMenuClosed) {
            m_deferModalsUntilRaceMenuClosed = false;
            TryRunPendingNewGameScan();
        }

        MARAS_LOG_INFO("PrismaUIManager::Show() requested");
        const bool wasOpen = m_dashboardOpen;
        m_dashboardOpen = true;
        if (!wasOpen) {
            m_dashboardPaused = Settings::GetSingleton().autoPauseOnOpen;
        }

        const bool wasValid = IsViewValid();
        if (!EnsureViewCreated()) {
            m_dashboardOpen = false;
            m_dashboardPaused = false;
            return;
        }

        SyncViewVisibilityAndFocus();

        if (wasValid && m_domReady) {
            InteropCall("openMARAS", "");
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
        }
    }

    void PrismaUIManager::Hide() {
        if (!prismaUI || !view) {
            m_dashboardOpen = false;
            m_dashboardPaused = false;
            return;
        }

        MARAS_LOG_INFO("PrismaUIManager::Hide() called for view {}", view);
        m_dashboardOpen = false;
        m_dashboardPaused = false;

        if (prismaUI->IsValid(view)) {
            if (m_domReady) {
                InteropCall("closeMARASView", "");
            }
            SyncViewVisibilityAndFocus();
            MARAS_LOG_INFO("PrismaUIManager: Dashboard closed for view {} (modalActive={})",
                           view, PrismaModalManager::GetSingleton().HasActiveModal());
        }
    }

    void PrismaUIManager::Toggle() {
        if (!prismaUI) {
            MARAS_LOG_WARN("PrismaUIManager::Toggle() called but PrismaUI is not available");
            return;
        }

        if (PrismaModalManager::GetSingleton().HasActiveModal()) {
            MARAS_LOG_INFO("PrismaUIManager::Toggle() ignored while a modal is active");
            SyncViewVisibilityAndFocus();
            return;
        }

        bool currentlyVisible = IsVisible();
        MARAS_LOG_INFO("PrismaUIManager::Toggle() called — currently {}", currentlyVisible ? "visible" : "hidden");

        if (currentlyVisible) {
            Hide();
        } else {
            Show();
        }
    }

    void PrismaUIManager::Destroy() {
        m_dashboardOpen = false;
        m_dashboardPaused = false;
        m_domReady = false;
        m_modalFocusLocked = false;
        m_deferModalsUntilRaceMenuClosed = false;
        m_pendingNewGameRelationshipScan = false;
        m_postUnfocusCallbacks.clear();

        if (prismaUI && view) {
            MARAS_LOG_INFO("PrismaUIManager::Destroy() called for view {}", view);
            if (prismaUI->IsValid(view)) {
                prismaUI->Unfocus(view);
                prismaUI->Hide(view);
                prismaUI->Destroy(view);
                MARAS_LOG_INFO("PrismaUIManager: View {} destroyed", view);
            }
            view = 0;
        }
    }

    void PrismaUIManager::OnCloseMARAS(const char* /*argument*/) {
        MARAS_LOG_INFO("PrismaUIManager: JS 'closeMARAS' event received");
        SKSE::GetTaskInterface()->AddTask([]() {
            PrismaUIManager::GetSingleton().Hide();
        });
    }

    void PrismaUIManager::OnLog(const char* message) {
        if (message) {
            MARAS_LOG_INFO("[PrismaUI Web] {}", message);
        }
    }

    void PrismaUIManager::OnRefocusView(const char* /*argument*/) {
        MARAS_LOG_INFO("PrismaUIManager: JS 'refocusView' event received");
        SKSE::GetTaskInterface()->AddTask([]() {
            auto& mgr = PrismaUIManager::GetSingleton();
            if (mgr.IsViewValid()) {
                mgr.SyncViewVisibilityAndFocus();
            }
        });
    }

    void PrismaUIManager::OnMarasAction(const char* argument) {
        if (argument) {
            PrismaUIDataBridge::GetSingleton().HandleAction(argument);
        }
    }

}
