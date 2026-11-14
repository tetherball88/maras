#include "maras/bridge_ui/PrismaUIManager.h"

#include "maras/app/RelationshipUseCases.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/bridge_ui/PrismaModalManager.h"
#include "maras/bridge_ui/PrismaUIDataBridge.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/WorldStore.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <utility>

namespace MARAS::Bridge::Ui {

    namespace {

        constexpr const char* kDashboardHtmlPath = "MARAS/index.html";

        [[nodiscard]] bool IsModifierHeld(int vKey) noexcept {
            return (::GetAsyncKeyState(vKey) & 0x8000) != 0;
        }

        [[nodiscard]] bool IsDashboardToggleBlockedByGameState() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                return true;
            }
            if (ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
                ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
                ui->IsMenuOpen(RE::Console::MENU_NAME) ||
                ui->IsMenuOpen(RE::BookMenu::MENU_NAME) ||
                ui->IsMenuOpen("TextInputMenu")) {
                return true;
            }
            return false;
        }

        [[nodiscard]] std::string BuildPauseChangedJson(bool paused) {
            nlohmann::json j = { { "paused", paused } };
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

    }  // namespace

    PrismaUIManager& PrismaUIManager::GetSingleton() {
        static PrismaUIManager instance;
        return instance;
    }

    void PrismaUIManager::Initialize() {
        if (prismaAPI_ && view_ != 0 && prismaAPI_->IsValid(view_)) {
            return;
        }

        if (!prismaAPI_) {
            prismaAPI_ = static_cast<PRISMA_UI_API::IVPrismaUI1*>(
                PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
        }
        if (!prismaAPI_) {
            MARAS_LOG_INFO("[PrismaUIManager] PrismaUI.dll API unavailable; dashboard UI disabled");
            return;
        }

        if (view_ == 0 || !prismaAPI_->IsValid(view_)) {
            isDomReady_.store(false, std::memory_order_release);
            isDestroying_.store(false, std::memory_order_release);
            view_ = prismaAPI_->CreateView(kDashboardHtmlPath, &PrismaUIManager::OnDomReady);
            if (view_ == 0) {
                MARAS_LOG_ERROR("[PrismaUIManager] Failed to create Prisma view for '{}'", kDashboardHtmlPath);
                return;
            }
            MARAS_LOG_INFO("[PrismaUIManager] Created Prisma view {} for '{}'", view_, kDashboardHtmlPath);
        }
    }

    void PrismaUIManager::StartListeningInput() {
        if (!inputSinkRegistered_.exchange(true, std::memory_order_acq_rel)) {
            if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
                dm->RemoveEventSink(this);
                dm->AddEventSink(this);
                MARAS_LOG_INFO("[PrismaUIManager] Registered input event sink");
            } else {
                inputSinkRegistered_.store(false, std::memory_order_release);
                MARAS_LOG_WARN("[PrismaUIManager] BSInputDeviceManager unavailable for hotkey sink");
            }
        }

        if (!menuSinkRegistered_.exchange(true, std::memory_order_acq_rel)) {
            if (auto* ui = RE::UI::GetSingleton()) {
                ui->RemoveEventSink<RE::MenuOpenCloseEvent>(this);
                ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
                MARAS_LOG_INFO("[PrismaUIManager] Registered MenuOpenCloseEvent sink");
            } else {
                menuSinkRegistered_.store(false, std::memory_order_release);
                MARAS_LOG_WARN("[PrismaUIManager] UI singleton unavailable for MenuOpenCloseEvent sink");
            }
        }
    }

    void PrismaUIManager::OnNewGameStarted(bool deferInitialScan) {
        pendingNewGameInitialScan_.store(deferInitialScan, std::memory_order_release);
        awaitingInitialNewGameLoadScreenClose_.store(true, std::memory_order_release);
        MARAS_LOG_INFO("[PrismaUIManager] OnNewGameStarted (deferInitialScan={})", deferInitialScan);

        if (auto* ui = RE::UI::GetSingleton(); ui && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
            TryRunPendingNewGameScan();
        }
    }

    void PrismaUIManager::ResetNewGameDeferral() {
        pendingNewGameInitialScan_.store(false, std::memory_order_release);
        awaitingInitialNewGameLoadScreenClose_.store(false, std::memory_order_release);
    }

    void PrismaUIManager::TryRunPendingNewGameScan() {
        if (!awaitingInitialNewGameLoadScreenClose_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        if (!pendingNewGameInitialScan_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        MARAS_LOG_INFO("[PrismaUIManager] Running deferred initial new-game relationship scan");
        App::RelationshipUseCases::GetSingleton().ScanExistingRelationships(/*isNewGame=*/true);
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.settings.Write().initialRelationshipScanDone = true;
        });
    }

    void PrismaUIManager::Destroy() {
        isDestroying_.store(true, std::memory_order_release);
        isVisible_.store(false, std::memory_order_release);
        focusOwned_.store(false, std::memory_order_release);
        isDomReady_.store(false, std::memory_order_release);
        pendingShowAfterCreate_.store(false, std::memory_order_release);

        {
            std::lock_guard lock(stateMutex_);
            afterFocusReleasedTasks_.clear();
            pendingJsCalls_.clear();
        }

        if (prismaAPI_ && view_ != 0) {
            const auto oldView = view_;
            view_ = 0;
            if (prismaAPI_->IsValid(oldView)) {
                if (prismaAPI_->HasFocus(oldView)) {
                    prismaAPI_->Unfocus(oldView);
                }
                prismaAPI_->Destroy(oldView);
            }
            MARAS_LOG_INFO("[PrismaUIManager] Destroyed Prisma view {}", oldView);
        } else {
            view_ = 0;
        }

        isDestroying_.store(false, std::memory_order_release);
    }

    void PrismaUIManager::Revert() {
        isVisible_.store(false, std::memory_order_release);
        focusOwned_.store(false, std::memory_order_release);
        pendingShowAfterCreate_.store(false, std::memory_order_release);
        ResetNewGameDeferral();

        std::vector<std::function<void()>> staleTasks;
        {
            std::lock_guard lock(stateMutex_);
            staleTasks.swap(afterFocusReleasedTasks_);
            pendingJsCalls_.clear();
        }

        SyncViewVisibilityAndFocus();
    }

    void PrismaUIManager::Show() {
        if (isDestroying_.load(std::memory_order_acquire)) {
            return;
        }
        if (!IsInitialized()) {
            pendingShowAfterCreate_.store(true, std::memory_order_release);
            Initialize();
            return;
        }

        const auto settingsSnap = App::SettingsCoordinator::GetSingleton().Snapshot();
        const bool defaultPause = settingsSnap ? settingsSnap->autoPauseOnOpen : true;
        isPaused_.store(defaultPause, std::memory_order_release);
        isVisible_.store(true, std::memory_order_release);

        SyncViewVisibilityAndFocus();

        if (isDomReady_.load(std::memory_order_acquire)) {
            SendToUI("openMARAS", "");
            SendToUI("onGamePausedChanged", BuildPauseChangedJson(defaultPause));
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
        } else {
            pendingShowAfterCreate_.store(true, std::memory_order_release);
        }
    }

    void PrismaUIManager::Hide() {
        isVisible_.store(false, std::memory_order_release);
        pendingShowAfterCreate_.store(false, std::memory_order_release);

        if (isDomReady_.load(std::memory_order_acquire)) {
            SendToUI("closeMARASView", "");
        }

        SyncViewVisibilityAndFocus();
    }

    void PrismaUIManager::Toggle() {
        if (PrismaModalManager::GetSingleton().HasActiveModal()) {
            // Do not hide a blocking standalone confirmation/prompt modal with dashboard toggle key.
            SyncViewVisibilityAndFocus();
            return;
        }

        if (IsVisible()) {
            Hide();
        } else {
            Show();
        }
    }

    bool PrismaUIManager::IsModalDisplayBlocked() const {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return true;
        }
        return ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
               ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
               ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) ||
               ui->IsMenuOpen(RE::Console::MENU_NAME) ||
               ui->IsMenuOpen("Pause Menu") ||
               ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) ||
               ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) ||
               ui->IsMenuOpen("Sleep/Wait Menu") ||
               ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
    }

    bool PrismaUIManager::RegisterJSListener(
        const char* eventName,
        PRISMA_UI_API::JSListenerCallback callback) {
        if (!prismaAPI_ || view_ == 0 || !eventName || !*eventName || !callback ||
            !prismaAPI_->IsValid(view_)) {
            return false;
        }
        prismaAPI_->RegisterJSListener(view_, eventName, callback);
        return true;
    }

    void PrismaUIManager::OnUIReady() {
        if (!prismaAPI_ || view_ == 0 || !prismaAPI_->IsValid(view_) ||
            isDestroying_.load(std::memory_order_acquire)) {
            return;
        }

        isDomReady_.store(true, std::memory_order_release);
        SyncViewVisibilityAndFocus();

        nlohmann::json statusData;
        statusData["status"] = "M.A.R.A.S Core Initialized";
        SendToUI(
            "receiveMARASStatus",
            statusData.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));

        if (isVisible_.load(std::memory_order_acquire)) {
            SendToUI("openMARAS", "");
            SendToUI("onGamePausedChanged", BuildPauseChangedJson(IsDashboardPaused()));
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
        } else {
            SendToUI("closeMARASView", "");
        }

        if (PrismaModalManager::GetSingleton().HasActiveModal() && !IsModalDisplayBlocked()) {
            PrismaModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/true);
        }
    }

    void PrismaUIManager::SyncViewVisibilityAndFocus() {
        if (!prismaAPI_ || view_ == 0 || !prismaAPI_->IsValid(view_) ||
            isDestroying_.load(std::memory_order_acquire)) {
            return;
        }

        const bool hasModal = PrismaModalManager::GetSingleton().HasActiveModal();
        const bool modalBlocked = IsModalDisplayBlocked();
        const bool dashboardVisible = isVisible_.load(std::memory_order_acquire);

        if (hasModal && !modalBlocked) {
            if (prismaAPI_->IsHidden(view_)) {
                prismaAPI_->Show(view_);
            }
            // Standalone Prisma modal policy: pause game, allow modal controls only.
            if (!prismaAPI_->HasFocus(view_)) {
                prismaAPI_->Focus(view_, /*pauseGame=*/true, /*disableFocusMenu=*/false);
            }
            focusOwned_.store(true, std::memory_order_release);
            return;
        }

        if (dashboardVisible) {
            if (prismaAPI_->IsHidden(view_)) {
                prismaAPI_->Show(view_);
            }
            const bool pauseGame = isPaused_.load(std::memory_order_acquire);
            if (!prismaAPI_->HasFocus(view_)) {
                prismaAPI_->Focus(view_, pauseGame, /*disableFocusMenu=*/false);
            } else {
                // Re-apply focus when pause preference changes so PrismaUI updates pause state cleanly.
                prismaAPI_->Unfocus(view_);
                prismaAPI_->Focus(view_, pauseGame, /*disableFocusMenu=*/false);
            }
            focusOwned_.store(true, std::memory_order_release);
            return;
        }

        if (prismaAPI_->HasFocus(view_)) {
            prismaAPI_->Unfocus(view_);
        }
        if (!prismaAPI_->IsHidden(view_)) {
            prismaAPI_->Hide(view_);
        }
        focusOwned_.store(false, std::memory_order_release);
        DrainAfterFocusReleasedTasks();
    }

    void PrismaUIManager::RunAfterFocusReleased(std::function<void()> task) {
        if (!task) {
            return;
        }

        {
            std::lock_guard lock(stateMutex_);
            afterFocusReleasedTasks_.push_back(std::move(task));
        }

        AddCurrentEpochTask("PrismaUIManager::RunAfterFocusReleased", [this]() {
            SyncViewVisibilityAndFocus();
        });
    }

    void PrismaUIManager::DrainAfterFocusReleasedTasks() {
        if (focusOwned_.load(std::memory_order_acquire)) {
            return;
        }

        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard lock(stateMutex_);
            if (afterFocusReleasedTasks_.empty()) {
                return;
            }
            tasks.swap(afterFocusReleasedTasks_);
        }

        for (auto& task : tasks) {
            if (task) {
                task();
            }
        }
    }

    void PrismaUIManager::SendToUI(const std::string& functionName, const std::string& jsonData) {
        if (isDestroying_.load(std::memory_order_acquire)) {
            return;
        }
        if (!prismaAPI_ || view_ == 0 || !isDomReady_.load(std::memory_order_acquire)) {
            std::lock_guard lock(stateMutex_);
            pendingJsCalls_.emplace_back(functionName, jsonData);
            return;
        }

        if (!DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            AddCurrentEpochTask("PrismaUIManager::SendToUI", [this, functionName, jsonData]() {
                SendToUI(functionName, jsonData);
            });
            return;
        }

        if (!prismaAPI_->IsValid(view_)) {
            return;
        }

        prismaAPI_->InteropCall(view_, functionName.c_str(), jsonData.c_str());
    }

    void PrismaUIManager::SetDashboardPaused(bool pause) {
        isPaused_.store(pause, std::memory_order_release);
        SyncViewVisibilityAndFocus();
        SendToUI("onGamePausedChanged", BuildPauseChangedJson(pause));
    }

    void PrismaUIManager::OnDomReady(PrismaView view) {
        AddCurrentEpochTask("PrismaUIManager::OnDomReady", [view]() {
            auto& mgr = PrismaUIManager::GetSingleton();
            if (mgr.isDestroying_.load(std::memory_order_acquire) ||
                !mgr.prismaAPI_ ||
                mgr.view_ != view ||
                !mgr.prismaAPI_->IsValid(view)) {
                return;
            }

            mgr.prismaAPI_->RegisterJSListener(view, "closeMARAS", &PrismaUIManager::OnCloseUI);
            mgr.prismaAPI_->RegisterJSListener(view, "log", &PrismaUIManager::OnLogFromUI);
            mgr.prismaAPI_->RegisterJSListener(view, "refocusView", &PrismaUIManager::OnRefocusView);
            PrismaUIDataBridge::GetSingleton().RegisterListeners();

            mgr.isDomReady_.store(true, std::memory_order_release);
            MARAS_LOG_INFO("[PrismaUIManager] DOM ready on Prisma view {}; listeners registered", view);

            if (!mgr.isVisible_.load(std::memory_order_acquire) &&
                !PrismaModalManager::GetSingleton().HasActiveModal()) {
                mgr.prismaAPI_->Hide(view);
            }

            mgr.FlushPendingAfterDomReady();

            if (mgr.pendingShowAfterCreate_.exchange(false, std::memory_order_acq_rel)) {
                mgr.Show();
            }
            if (PrismaModalManager::GetSingleton().HasActiveModal() && !mgr.IsModalDisplayBlocked()) {
                PrismaModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/true);
            }
            mgr.SyncViewVisibilityAndFocus();
        });
    }

    void PrismaUIManager::FlushPendingAfterDomReady() {
        std::vector<std::pair<std::string, std::string>> queued;
        {
            std::lock_guard lock(stateMutex_);
            queued.swap(pendingJsCalls_);
        }
        for (const auto& [fn, payload] : queued) {
            SendToUI(fn, payload);
        }
    }

    void PrismaUIManager::OnCloseUI(const char* /*argument*/) {
        AddCurrentEpochTask("PrismaUIManager::OnCloseUI", []() {
            PrismaUIManager::GetSingleton().Hide();
        });
    }

    void PrismaUIManager::OnLogFromUI(const char* argument) {
        if (argument && *argument) {
            MARAS_LOG_DEBUG("[PrismaUI] {}", argument);
        }
    }

    void PrismaUIManager::OnRefocusView(const char* /*argument*/) {
        AddCurrentEpochTask("PrismaUIManager::OnRefocusView", []() {
            PrismaUIManager::GetSingleton().SyncViewVisibilityAndFocus();
        });
    }

    void PrismaUIManager::HandleToggleHotkey() {
        if (IsDashboardToggleBlockedByGameState()) {
            return;
        }
        AddCurrentEpochTask("PrismaUIManager::HandleToggleHotkey", [this]() {
            if (!IsDashboardToggleBlockedByGameState()) {
                Toggle();
            }
        });
    }

    RE::BSEventNotifyControl PrismaUIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto settingsSnap = App::SettingsCoordinator::GetSingleton().Snapshot();
        if (!settingsSnap || settingsSnap->toggleUIKey == 0) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (RE::InputEvent* event = *a_event; event != nullptr; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                continue;
            }
            const auto* button = event->AsButtonEvent();
            if (!button ||
                button->GetDevice() != RE::INPUT_DEVICE::kKeyboard ||
                !button->IsDown()) {
                continue;
            }
            if (button->GetIDCode() != settingsSnap->toggleUIKey) {
                continue;
            }

            if (settingsSnap->toggleUIModCtrl && !IsModifierHeld(VK_CONTROL)) {
                continue;
            }
            if (settingsSnap->toggleUIModShift && !IsModifierHeld(VK_SHIFT)) {
                continue;
            }
            if (settingsSnap->toggleUIModAlt && !IsModifierHeld(VK_MENU)) {
                continue;
            }

            HandleToggleHotkey();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl PrismaUIManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening) {
            AddCurrentEpochTask("PrismaUIManager::LoadingMenuClosed", [this]() {
                TryRunPendingNewGameScan();
            });
        }

        if (PrismaModalManager::GetSingleton().HasActiveModal() ||
            IsVisible() ||
            a_event->menuName == RE::Console::MENU_NAME ||
            a_event->menuName == RE::JournalMenu::MENU_NAME ||
            a_event->menuName == RE::RaceSexMenu::MENU_NAME ||
            a_event->menuName == "Pause Menu") {
            AddCurrentEpochTask("PrismaUIManager::MenuSync", [this]() {
                if (PrismaModalManager::GetSingleton().HasActiveModal() && !IsModalDisplayBlocked()) {
                    PrismaModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/false);
                }
                SyncViewVisibilityAndFocus();
            });
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS::Bridge::Ui
