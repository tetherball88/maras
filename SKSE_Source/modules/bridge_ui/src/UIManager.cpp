#include "maras/bridge_ui/UIManager.h"

#include "maras/app/RelationshipUseCases.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/bridge_ui/ModalManager.h"
#include "maras/bridge_ui/UIDataBridge.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/WorldStore.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <utility>

namespace MARAS::Bridge::Ui {

    namespace {

        constexpr std::size_t kMaxPendingJsCalls = 64u;

        [[nodiscard]] bool IsBlockingNativeMenuOpen() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                return true;
            }
            return ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
                   ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
                   ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
        }

        [[nodiscard]] std::string GetDashboardHtmlPath() {
            wchar_t buf[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
            const auto path = std::filesystem::path(buf).parent_path() /
                              L"Data" / L"Magelight" / L"MARAS" / L"views" / L"MARAS" / L"index.html";
            const auto u8 = path.u8string();
            return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        }

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

        [[nodiscard]] bool CapabilitySupported(
            const MAGELIGHT_API::MagelightApi4* api,
            std::uint32_t minVersion,
            const char* capabilityName) noexcept {
            if (!api || api->hostVersionNumber < minVersion) {
                return false;
            }
            if (!capabilityName || !*capabilityName || !api->QueryCapability) {
                return true;
            }
            return api->QueryCapability(capabilityName) == 1;
        }

    }  // namespace

    UIManager& UIManager::GetSingleton() {
        static UIManager instance;
        return instance;
    }

    void UIManager::Initialize() {
        if (renderDead_.load(std::memory_order_acquire)) {
            MARAS_LOG_WARN("[UIManager] Skipping Initialize after Magelight RenderDead");
            return;
        }
        if (IsInitialized()) {
            return;
        }

        if (!magelight_) {
            magelight_ = MAGELIGHT_API::RequestApi4();
        }
        if (!magelight_) {
            MARAS_LOG_INFO("[UIManager] MagelightUI.dll v4 API unavailable; dashboard UI disabled");
            return;
        }

        if (modHandle_ == 0) {
            MAGELIGHT_API::ModDesc modDesc{};
            modDesc.size = sizeof(MAGELIGHT_API::ModDesc);
            modDesc.modId = "MARAS";
            modDesc.displayName = "M.A.R.A.S.";
            modDesc.modVersion = MAGELIGHT_API::PackVersion(2, 1, 0);
            modDesc.minHostVersion = MAGELIGHT_API::PackVersion(0, 28, 0);
            modDesc.callbackThread = MAGELIGHT_API::CallbackThread::GameThread;
            modDesc.onEvent = &UIManager::OnMagelightEvent;
            modDesc.onLog = nullptr;
            modDesc.user = this;
            modDesc.sessionName = nullptr;

            const auto modRes = magelight_->RegisterMod(&modDesc, &modHandle_);
            if (modRes != MAGELIGHT_API::Result::Ok || modHandle_ == 0) {
                modHandle_ = 0;
                MARAS_LOG_ERROR("[UIManager] RegisterMod failed (result={})", static_cast<int>(modRes));
                return;
            }

            if (magelight_->hostVersionNumber >= 3000 && magelight_->SetNetworkPolicy) {
                const auto netRes = magelight_->SetNetworkPolicy(modHandle_, MAGELIGHT_API::NetworkPolicy::FileOnly);
                if (netRes != MAGELIGHT_API::Result::Ok) {
                    MARAS_LOG_WARN("[UIManager] SetNetworkPolicy(FileOnly) failed (result={})", static_cast<int>(netRes));
                }
            }
        }

        isDomReady_.store(false, std::memory_order_release);
        domReadyBootstrapped_.store(false, std::memory_order_release);
        modeState_.ResetOnViewDestroyedOrRenderDead();
        SyncModeAtomicMirrors();

        const std::string pagePath = GetDashboardHtmlPath();

        MAGELIGHT_API::ViewDesc viewDesc{};
        viewDesc.size = sizeof(MAGELIGHT_API::ViewDesc);
        viewDesc.name = "main";
        viewDesc.htmlPath = pagePath.c_str();
        viewDesc.anchor = MAGELIGHT_API::Anchor::TopLeft;
        viewDesc.x = 0;
        viewDesc.y = 0;
        viewDesc.w = 0;
        viewDesc.h = 0;
        viewDesc.fullscreen = true;
        viewDesc.clickThrough = false;
        viewDesc.startVisible = false;
        viewDesc.layer = MAGELIGHT_API::Layer::Popup;
        viewDesc.uiScale = 1.0f;
        viewDesc.onDomReady = nullptr;

        MAGELIGHT_API::ViewId createdView = 0;
        const auto viewRes = magelight_->CreateViewEx(modHandle_, &viewDesc, &createdView);
        if (viewRes != MAGELIGHT_API::Result::Ok || createdView == 0) {
            const char* errMsg = magelight_->GetLastErrorMessage ? magelight_->GetLastErrorMessage(modHandle_) : "";
            MARAS_LOG_ERROR(
                "[UIManager] CreateViewEx failed for '{}' (result={}, error='{}')",
                pagePath,
                static_cast<int>(viewRes),
                errMsg ? errMsg : "");
            return;
        }
        view_ = createdView;

        if (CapabilitySupported(magelight_, 2800, "escapecapture") && magelight_->SetEscapeCapture) {
            magelight_->SetEscapeCapture(view_, true);
        }
        if (CapabilitySupported(magelight_, 10600, "gamepad") && magelight_->SetViewGamepad) {
            MAGELIGHT_API::GamepadConfig gp{};
            gp.size = sizeof(MAGELIGHT_API::GamepadConfig);
            gp.enabled = true;
            gp.style = MAGELIGHT_API::kGamepadStyleKeys;
            gp.allowCursor = true;
            gp.startInCursorMode = true;
            magelight_->SetViewGamepad(view_, &gp);
        }

        RegisterJSListener("closeMARAS", &UIManager::OnCloseUI);
        RegisterJSListener("log", &UIManager::OnLogFromUI);
        UIDataBridge::GetSingleton().RegisterListeners();

        MARAS_LOG_INFO(
            "[UIManager] Created Magelight view {} for '{}' (host={})",
            view_,
            pagePath,
            magelight_->hostVersion ? magelight_->hostVersion : "unknown");
    }

    void UIManager::StartListeningInput() {
        if (!inputSinkRegistered_.exchange(true, std::memory_order_acq_rel)) {
            if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
                dm->RemoveEventSink(this);
                dm->AddEventSink(this);
                MARAS_LOG_INFO("[UIManager] Registered input event sink");
            } else {
                inputSinkRegistered_.store(false, std::memory_order_release);
                MARAS_LOG_WARN("[UIManager] BSInputDeviceManager unavailable for hotkey sink");
            }
        }

        if (!menuSinkRegistered_.exchange(true, std::memory_order_acq_rel)) {
            if (auto* ui = RE::UI::GetSingleton()) {
                ui->RemoveEventSink<RE::MenuOpenCloseEvent>(this);
                ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
                MARAS_LOG_INFO("[UIManager] Registered MenuOpenCloseEvent sink");
            } else {
                menuSinkRegistered_.store(false, std::memory_order_release);
                MARAS_LOG_WARN("[UIManager] UI singleton unavailable for MenuOpenCloseEvent sink");
            }
        }
    }

    void UIManager::OnNewGameStarted(bool deferInitialScan) {
        deferModalsUntilRaceMenuClosed_.store(true, std::memory_order_release);
        pendingNewGameInitialScan_.store(deferInitialScan, std::memory_order_release);
        MARAS_LOG_INFO(
            "[UIManager] OnNewGameStarted: deferring modals and initial scan (deferInitialScan={}) until RaceSexMenu closes",
            deferInitialScan);
    }

    void UIManager::ResetNewGameDeferral() {
        deferModalsUntilRaceMenuClosed_.store(false, std::memory_order_release);
        pendingNewGameInitialScan_.store(false, std::memory_order_release);
    }

    void UIManager::TryRunPendingNewGameScan() {
        if (!pendingNewGameInitialScan_.load(std::memory_order_acquire) || IsModalDisplayBlocked()) {
            return;
        }

        std::lock_guard lock(MARAS::GetPapyrusMutex());
        if (!pendingNewGameInitialScan_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        const auto settingsSnap = App::SettingsCoordinator::GetSingleton().Snapshot();
        if (settingsSnap && settingsSnap->initialRelationshipScanDone) {
            return;
        }

        MARAS_LOG_INFO("[UIManager] Running deferred initial new-game relationship scan");
        auto& relUseCases = App::RelationshipUseCases::GetSingleton();
        relUseCases.ScanExistingRelationships(/*isNewGame=*/true);
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.settings.Write().initialRelationshipScanDone = true;
        });
        relUseCases.LogStatistics();
    }

    void UIManager::SyncModeAtomicMirrors() noexcept {
        focusOwned_.store(modeState_.IsFocusOwned(), std::memory_order_release);
        uiModeRequested_.store(modeState_.IsUIModeRequested(), std::memory_order_release);
    }

    void UIManager::Revert() {
        isVisible_.store(false, std::memory_order_release);
        pendingShowAfterCreate_.store(false, std::memory_order_release);
        openDashboardAfterJournalClose_.store(false, std::memory_order_release);
        modeState_.ResetModalRefuseRetries();
        ResetNewGameDeferral();

        {
            std::lock_guard lock(stateMutex_);
            afterFocusReleasedTasks_.clear();
            pendingJsCalls_.clear();
        }

        if (isDomReady_.load(std::memory_order_acquire)) {
            // Pass "true" so React discards pending debounced Save A edits and resets session state.
            SendToUI("closeMARASView", "true");
        }

        SyncViewVisibilityAndFocus();
    }

    void UIManager::Show() {
        if (!DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            AddCurrentEpochTask("UIManager::Show", [this]() {
                std::lock_guard lock(MARAS::GetPapyrusMutex());
                ShowOnMainThread();
            });
            return;
        }
        ShowOnMainThread();
    }

    void UIManager::ShowOnMainThread() {
        if (renderDead_.load(std::memory_order_acquire)) {
            MARAS_LOG_WARN("[UIManager] Show ignored after Magelight RenderDead");
            return;
        }

        if (IsBlockingNativeMenuOpen()) {
            MARAS_LOG_INFO("[UIManager] Show ignored while blocking native menu is open");
            return;
        }

        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)) {
            MARAS_LOG_INFO("[UIManager] Show closing JournalMenu before opening Dashboard");
            openDashboardAfterJournalClose_.store(true, std::memory_order_release);
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                if (ui->IsMenuOpen(RE::TweenMenu::MENU_NAME)) {
                    queue->AddMessage(RE::TweenMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                }
            }
            return;
        }

        if (deferModalsUntilRaceMenuClosed_.exchange(false, std::memory_order_acq_rel)) {
            TryRunPendingNewGameScan();
        }

        if (!IsInitialized()) {
            pendingShowAfterCreate_.store(true, std::memory_order_release);
            Initialize();
            if (!IsInitialized()) {
                pendingShowAfterCreate_.store(false, std::memory_order_release);
            }
            return;
        }

        if (ModalManager::GetSingleton().HasActiveModal() && !IsModalDisplayBlocked()) {
            MARAS_LOG_INFO("[UIManager] Deferring dashboard Show while modal dialog is active");
            SyncViewVisibilityAndFocus();
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
            UIDataBridge::GetSingleton().RefreshDashboardOnOpen();
        } else {
            pendingShowAfterCreate_.store(true, std::memory_order_release);
        }
    }

    void UIManager::Hide() {
        if (!DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            AddCurrentEpochTask("UIManager::Hide", [this]() {
                std::lock_guard lock(MARAS::GetPapyrusMutex());
                HideOnMainThread();
            });
            return;
        }
        HideOnMainThread();
    }

    void UIManager::HideOnMainThread() {
        isVisible_.store(false, std::memory_order_release);
        pendingShowAfterCreate_.store(false, std::memory_order_release);
        openDashboardAfterJournalClose_.store(false, std::memory_order_release);

        if (isDomReady_.load(std::memory_order_acquire)) {
            SendToUI("closeMARASView", "");
        }

        SyncViewVisibilityAndFocus();
    }

    void UIManager::Toggle() {
        if (ModalManager::GetSingleton().HasActiveModal()) {
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

    bool UIManager::IsModalDisplayBlocked() const {
        if (deferModalsUntilRaceMenuClosed_.load(std::memory_order_acquire)) {
            return true;
        }
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

    bool UIManager::RegisterJSListener(const char* eventName, JSListenerCallback callback) {
        if (!IsInitialized() || !eventName || !*eventName || !callback) {
            return false;
        }
        magelight_->RegisterJSListener(view_, eventName, callback);
        return true;
    }

    void UIManager::PostToGameThread(std::function<void()> task) {
        if (!task) {
            return;
        }

        auto* fn = new std::function<void()>(std::move(task));
        if (magelight_ && magelight_->hostVersionNumber >= 3001 && magelight_->PostGameTask) {
            if (magelight_->PostGameTask(&UIManager::GameTaskTrampoline, fn) == MAGELIGHT_API::Result::Ok) {
                return;
            }
        }

        if (auto* taskIF = SKSE::GetTaskInterface()) {
            taskIF->AddTask([fn]() { UIManager::GameTaskTrampoline(fn); });
        } else {
            delete fn;
        }
    }

    void UIManager::GameTaskTrampoline(void* userData) {
        std::unique_ptr<std::function<void()>> guard(static_cast<std::function<void()>*>(userData));
        if (!guard || !*guard) {
            return;
        }
        MARAS::ScopedMainThreadTask mainScope;
        (*guard)();
    }

    void UIManager::BootstrapDomReadyState() {
        auto& bridge = UIDataBridge::GetSingleton();
        bridge.SendLocaleDataOnMainThreadSafe();
        bridge.SendSettingsDataOnMainThreadSafe();
        FlushPendingAfterDomReady();

        if (pendingShowAfterCreate_.exchange(false, std::memory_order_acq_rel)) {
            ShowOnMainThread();
        } else if (isVisible_.load(std::memory_order_acquire)) {
            SendToUI("openMARAS", "");
            SendToUI("onGamePausedChanged", BuildPauseChangedJson(IsDashboardPaused()));
            bridge.RefreshDashboardOnOpen();
        } else {
            SendToUI("closeMARASView", "");
        }

        if (ModalManager::GetSingleton().HasActiveModal() && !IsModalDisplayBlocked()) {
            ModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/true);
        }
        SyncViewVisibilityAndFocus();
    }

    void UIManager::OnUIReady() {
        if (!IsInitialized()) {
            return;
        }

        isDomReady_.store(true, std::memory_order_release);

        nlohmann::json statusData;
        statusData["status"] = "M.A.R.A.S Core Initialized";
        SendToUI(
            "receiveMARASStatus",
            statusData.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));

        if (!domReadyBootstrapped_.exchange(true, std::memory_order_acq_rel)) {
            BootstrapDomReadyState();
            return;
        }

        // ViewDomReady already performed initial locale/settings/modal bootstrap; only flush
        // any newly queued calls or late pendingShowAfterCreate_ without double-pushing.
        FlushPendingAfterDomReady();
        if (pendingShowAfterCreate_.exchange(false, std::memory_order_acq_rel)) {
            ShowOnMainThread();
        } else {
            SyncViewVisibilityAndFocus();
        }
    }

    void UIManager::SyncViewVisibilityAndFocus() {
        if (!IsInitialized()) {
            return;
        }

        const bool hasModal = ModalManager::GetSingleton().HasActiveModal();
        const bool modalBlocked = IsModalDisplayBlocked();
        const bool modalActive = hasModal && !modalBlocked;
        const bool dashboardVisible = isVisible_.load(std::memory_order_acquire);
        const bool wantActive = modalActive || dashboardVisible;
        const bool wantPause = modalActive ? true : isPaused_.load(std::memory_order_acquire);
        const bool cancelSupported =
            CapabilitySupported(magelight_, 10100, "uimodecancel") && magelight_->CancelUIModeRequest;

        const auto plan = modeState_.PlanSync(wantActive, wantPause, cancelSupported);
        SyncModeAtomicMirrors();

        if (plan.releaseInstead) {
            ReleaseFocusAndUIMode();
            return;
        }
        if (plan.showView) {
            magelight_->ShowView(view_, true);
        }
        if (plan.raiseView) {
            magelight_->RaiseView(view_);
        }
        if (plan.setEscapeCapture && CapabilitySupported(magelight_, 2800, "escapecapture") &&
            magelight_->SetEscapeCapture) {
            magelight_->SetEscapeCapture(view_, true);
        }
        if (plan.waitForExitToResync || !plan.requestUIMode) {
            return;
        }

        if (plan.cancelPendingRequestFirst) {
            const bool cancelOk =
                (magelight_->CancelUIModeRequest(modHandle_) == MAGELIGHT_API::Result::Ok);
            const bool proceed = modeState_.OnCancelBeforeRequestResult(cancelOk);
            SyncModeAtomicMirrors();
            if (!proceed) {
                return;
            }
        }

        std::uint32_t flags =
            plan.requestPause ? MAGELIGHT_API::kUIModeFlagPause : MAGELIGHT_API::kUIModeFlagNone;
        if (plan.requestReclaim) {
            flags |= MAGELIGHT_API::kUIModeFlagReclaim;
        }
        const auto res = magelight_->RequestUIMode(view_, flags);
        const bool ok = (res == MAGELIGHT_API::Result::Ok);
        modeState_.OnRequestUIModeResult(ok, plan.requestPause, plan.requestReclaim);
        SyncModeAtomicMirrors();
        if (!ok) {
            MARAS_LOG_WARN("[UIManager] RequestUIMode failed (result={})", static_cast<int>(res));
        }
    }

    void UIManager::ReleaseFocusAndUIMode() {
        const bool initialized = IsInitialized();
        const bool cancelSupported =
            initialized &&
            CapabilitySupported(magelight_, 10100, "uimodecancel") &&
            magelight_->CancelUIModeRequest;

        const auto plan = modeState_.PlanRelease(initialized, cancelSupported);
        SyncModeAtomicMirrors();

        if (plan.hideView && initialized) {
            magelight_->ShowView(view_, false);
        }
        if (plan.cancelPendingRequest) {
            const bool cancelOk =
                (magelight_->CancelUIModeRequest(modHandle_) == MAGELIGHT_API::Result::Ok);
            const bool shouldDrain = modeState_.OnCancelBeforeReleaseResult(cancelOk);
            SyncModeAtomicMirrors();
            if (shouldDrain) {
                DrainAfterFocusReleasedTasks();
            }
            return;
        }
        if (plan.releaseUIMode) {
            const auto relRes = magelight_->ReleaseUIMode(modHandle_);
            const bool shouldDrain = modeState_.OnReleaseUIModeResult(relRes == MAGELIGHT_API::Result::Ok);
            SyncModeAtomicMirrors();
            if (shouldDrain) {
                DrainAfterFocusReleasedTasks();
            }
            return;
        }
        if (plan.drainAfterFocusReleased) {
            DrainAfterFocusReleasedTasks();
        }
    }

    void UIManager::RunAfterFocusReleased(std::function<void()> task) {
        if (!task) {
            return;
        }

        {
            std::lock_guard lock(stateMutex_);
            afterFocusReleasedTasks_.push_back(std::move(task));
        }

        AddCurrentEpochTask("UIManager::RunAfterFocusReleased", [this]() {
            SyncViewVisibilityAndFocus();
        });
    }

    void UIManager::DrainAfterFocusReleasedTasks() {
        if (!modeState_.CanDrainAfterFocusReleased()) {
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

    void UIManager::SendToUI(const std::string& functionName, const std::string& jsonData) {
        if (renderDead_.load(std::memory_order_acquire) || !magelight_ || view_ == 0) {
            return;
        }

        if (!isDomReady_.load(std::memory_order_acquire)) {
            std::lock_guard lock(stateMutex_);
            const bool isCoalescable =
                functionName == "receiveMARASOverview" ||
                functionName == "receiveMARASLocale" ||
                functionName == "receiveNPCList" ||
                functionName == "receiveMARASStatus" ||
                functionName == "showMARASModal" ||
                functionName == "clearMARASModal" ||
                functionName == "closeMARASView" ||
                functionName == "openMARAS" ||
                functionName == "onGamePausedChanged";
            if (isCoalescable) {
                for (auto& [fn, payload] : pendingJsCalls_) {
                    if (fn == functionName) {
                        payload = jsonData;
                        return;
                    }
                }
            }
            if (pendingJsCalls_.size() >= kMaxPendingJsCalls) {
                pendingJsCalls_.erase(pendingJsCalls_.begin());
            }
            pendingJsCalls_.emplace_back(functionName, jsonData);
            return;
        }

        if (!DeferredActorResolver::GetSingleton().IsOnMainThread()) {
            AddCurrentEpochTask("UIManager::SendToUI", [this, functionName, jsonData]() {
                SendToUI(functionName, jsonData);
            });
            return;
        }

        if (!magelight_->IsViewValid || !magelight_->IsViewValid(view_)) {
            return;
        }

        magelight_->InteropCall(view_, functionName.c_str(), jsonData.c_str());
    }

    void UIManager::SetDashboardPaused(bool pause) {
        isPaused_.store(pause, std::memory_order_release);
        if (IsVisible()) {
            SyncViewVisibilityAndFocus();
        }
        SendToUI("onGamePausedChanged", BuildPauseChangedJson(pause));
    }

    void UIManager::OnMagelightEvent(const MAGELIGHT_API::EventData* ev, void* user) {
        if (!ev) {
            return;
        }

        MARAS::ScopedMainThreadTask mainScope;
        auto* self = user ? static_cast<UIManager*>(user) : &UIManager::GetSingleton();

        switch (ev->type) {
            case MAGELIGHT_API::Event::ViewDomReady: {
                if (!self->IsInitialized() || ev->view != self->view_) {
                    return;
                }
                self->RegisterJSListener("closeMARAS", &UIManager::OnCloseUI);
                self->RegisterJSListener("log", &UIManager::OnLogFromUI);
                UIDataBridge::GetSingleton().RegisterListeners();

                self->isDomReady_.store(true, std::memory_order_release);
                MARAS_LOG_INFO("[UIManager] DOM ready on Magelight view {}; listeners registered", ev->view);

                if (!self->domReadyBootstrapped_.exchange(true, std::memory_order_acq_rel)) {
                    self->BootstrapDomReadyState();
                } else {
                    self->FlushPendingAfterDomReady();
                    self->SyncViewVisibilityAndFocus();
                }
                break;
            }

            case MAGELIGHT_API::Event::ViewReloaded:
                if (ev->view == self->view_) {
                    self->isDomReady_.store(true, std::memory_order_release);
                    self->domReadyBootstrapped_.store(true, std::memory_order_release);
                    MARAS_LOG_INFO("[UIManager] Magelight view {} reloaded (DOM ready)", ev->view);
                    self->BootstrapDomReadyState();
                }
                break;

            case MAGELIGHT_API::Event::ViewLoadFailed:
                MARAS_LOG_ERROR(
                    "[UIManager] View {} load failed: {}",
                    ev->view,
                    ev->detail ? ev->detail : "");
                if (ev->view == self->view_) {
                    self->isDomReady_.store(false, std::memory_order_release);
                    self->domReadyBootstrapped_.store(false, std::memory_order_release);
                    self->isVisible_.store(false, std::memory_order_release);
                    self->pendingShowAfterCreate_.store(false, std::memory_order_release);
                    {
                        std::lock_guard lock(self->stateMutex_);
                        self->pendingJsCalls_.clear();
                    }
                    ModalManager::GetSingleton().AbortAllModals("ViewLoadFailed");
                    self->ReleaseFocusAndUIMode();
                }
                break;

            case MAGELIGHT_API::Event::ViewDestroyed:
                if (ev->view == self->view_) {
                    MARAS_LOG_INFO("[UIManager] Magelight view {} destroyed", ev->view);
                    self->view_ = 0;
                    self->isDomReady_.store(false, std::memory_order_release);
                    self->domReadyBootstrapped_.store(false, std::memory_order_release);
                    self->isVisible_.store(false, std::memory_order_release);
                    self->pendingShowAfterCreate_.store(false, std::memory_order_release);
                    self->openDashboardAfterJournalClose_.store(false, std::memory_order_release);
                    self->modeState_.ResetOnViewDestroyedOrRenderDead();
                    self->SyncModeAtomicMirrors();
                    {
                        std::lock_guard lock(self->stateMutex_);
                        self->pendingJsCalls_.clear();
                    }
                    ModalManager::GetSingleton().AbortAllModals("ViewDestroyed");
                    self->DrainAfterFocusReleasedTasks();
                }
                break;

            case MAGELIGHT_API::Event::UIModeEntered:
                if (ev->view == self->view_) {
                    const bool modalActive =
                        ModalManager::GetSingleton().HasActiveModal() && !self->IsModalDisplayBlocked();
                    const bool shouldKeep = modalActive || self->isVisible_.load(std::memory_order_acquire);
                    const auto action = self->modeState_.OnUIModeEntered(shouldKeep);
                    self->SyncModeAtomicMirrors();
                    if (action == Foundation::UIModeEnteredAction::ReleaseNow) {
                        self->ReleaseFocusAndUIMode();
                    } else {
                        self->SyncViewVisibilityAndFocus();
                    }
                }
                break;

            case MAGELIGHT_API::Event::UIModeExited: {
                const bool modalActive =
                    ModalManager::GetSingleton().HasActiveModal() && !self->IsModalDisplayBlocked();
                const bool dashboardVisible = self->isVisible_.load(std::memory_order_acquire);
                const auto plan = self->modeState_.OnUIModeExited(modalActive, dashboardVisible);
                self->SyncModeAtomicMirrors();

                if (plan.scheduleModalReacquire) {
                    MARAS_LOG_INFO("[UIManager] UIModeExited while modal active; scheduling focus re-acquisition");
                    AddCurrentEpochTask("UIManager::ReacquireModalFocus", [self]() {
                        std::lock_guard lock(MARAS::GetPapyrusMutex());
                        if (ModalManager::GetSingleton().HasActiveModal() &&
                            !self->IsModalDisplayBlocked() &&
                            !self->IsFocusOwned()) {
                            self->SyncViewVisibilityAndFocus();
                        }
                    });
                    break;
                }
                if (plan.resyncDashboard) {
                    MARAS_LOG_DEBUG("[UIManager] UIModeExited finished prior release while dashboard open; re-syncing");
                    self->SyncViewVisibilityAndFocus();
                    break;
                }
                if (plan.closeDashboard && self->isVisible_.exchange(false, std::memory_order_acq_rel) &&
                    self->isDomReady_.load(std::memory_order_acquire)) {
                    self->SendToUI("closeMARASView", "");
                }
                if (plan.hideView && self->IsInitialized()) {
                    self->magelight_->ShowView(self->view_, false);
                }
                if (plan.drainAfterFocusReleased) {
                    self->DrainAfterFocusReleasedTasks();
                }
                break;
            }

            case MAGELIGHT_API::Event::UIModeRefused: {
                MARAS_LOG_WARN(
                    "[UIManager] UI mode refused for view {}: {}",
                    ev->view,
                    ev->detail ? ev->detail : "");
                const bool modalActive =
                    ModalManager::GetSingleton().HasActiveModal() && !self->IsModalDisplayBlocked();
                const bool dashboardVisible = self->isVisible_.load(std::memory_order_acquire);
                const auto plan = self->modeState_.OnUIModeRefused(modalActive, dashboardVisible);
                self->SyncModeAtomicMirrors();

                if (modalActive) {
                    if (plan.scheduleModalRetry) {
                        AddCurrentEpochTask("UIManager::RetryModalFocusAfterRefused", [self]() {
                            std::lock_guard lock(MARAS::GetPapyrusMutex());
                            if (ModalManager::GetSingleton().HasActiveModal() &&
                                !self->IsModalDisplayBlocked() &&
                                !self->IsFocusOwned()) {
                                self->SyncViewVisibilityAndFocus();
                            }
                        });
                    }
                    break;
                }
                if (plan.closeDashboard && self->isVisible_.exchange(false, std::memory_order_acq_rel) &&
                    self->isDomReady_.load(std::memory_order_acquire)) {
                    self->SendToUI("closeMARASView", "");
                }
                if (plan.hideView && self->IsInitialized()) {
                    self->magelight_->ShowView(self->view_, false);
                }
                if (plan.drainAfterFocusReleased) {
                    self->DrainAfterFocusReleasedTasks();
                }
                break;
            }

            case MAGELIGHT_API::Event::FocusDenied:
                MARAS_LOG_DEBUG(
                    "[UIManager] FocusDenied while holding UI mode (requester={})",
                    ev->detail ? ev->detail : "");
                break;

            case MAGELIGHT_API::Event::RenderDead:
                MARAS_LOG_ERROR("[UIManager] Magelight RenderDead event received; resetting UI state and aborting modals");
                self->renderDead_.store(true, std::memory_order_release);
                self->view_ = 0;
                self->isVisible_.store(false, std::memory_order_release);
                self->isDomReady_.store(false, std::memory_order_release);
                self->domReadyBootstrapped_.store(false, std::memory_order_release);
                self->pendingShowAfterCreate_.store(false, std::memory_order_release);
                self->openDashboardAfterJournalClose_.store(false, std::memory_order_release);
                self->modeState_.ResetOnViewDestroyedOrRenderDead();
                self->SyncModeAtomicMirrors();
                {
                    std::lock_guard lock(self->stateMutex_);
                    self->pendingJsCalls_.clear();
                }
                ModalManager::GetSingleton().AbortAllModals("RenderDead");
                self->DrainAfterFocusReleasedTasks();
                break;

            case MAGELIGHT_API::Event::ConsoleMessage:
                if (ev->detail && *ev->detail) {
                    if (ev->x >= 2) {
                        MARAS_LOG_ERROR("[UI Console] {}", ev->detail);
                    } else if (ev->x == 1) {
                        MARAS_LOG_WARN("[UI Console] {}", ev->detail);
                    } else {
                        MARAS_LOG_TRACE("[UI Console] {}", ev->detail);
                    }
                }
                break;

            default:
                break;
        }
    }

    void UIManager::FlushPendingAfterDomReady() {
        std::vector<std::pair<std::string, std::string>> queued;
        {
            std::lock_guard lock(stateMutex_);
            queued.swap(pendingJsCalls_);
        }
        for (const auto& [fn, payload] : queued) {
            SendToUI(fn, payload);
        }
    }

    void UIManager::OnCloseUI(const char* /*argument*/) {
        const auto expectedEpoch = MARAS::State::GetSessionEpoch();
        UIManager::GetSingleton().PostToGameThread([expectedEpoch]() {
            if (!MARAS::State::IsSessionEpochCurrent(expectedEpoch)) {
                return;
            }
            std::lock_guard lock(MARAS::GetPapyrusMutex());
            UIManager::GetSingleton().HideOnMainThread();
        });
    }

    void UIManager::OnLogFromUI(const char* argument) {
        if (argument && *argument) {
            MARAS_LOG_DEBUG("[UI] {}", argument);
        }
    }

    void UIManager::HandleToggleHotkey() {
        if (IsDashboardToggleBlockedByGameState()) {
            return;
        }
        AddCurrentEpochTask("UIManager::HandleToggleHotkey", [this]() {
            std::lock_guard lock(MARAS::GetPapyrusMutex());
            if (!IsDashboardToggleBlockedByGameState()) {
                Toggle();
            }
        });
    }

    RE::BSEventNotifyControl UIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event || !*a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // While a modal is active, block the dashboard toggle hotkey and recover focus if lost.
        if (ModalManager::GetSingleton().HasActiveModal()) {
            if (!focusOwned_.load(std::memory_order_acquire) &&
                !uiModeRequested_.load(std::memory_order_acquire) &&
                !IsModalDisplayBlocked() &&
                IsInitialized() &&
                isDomReady_.load(std::memory_order_acquire)) {
                for (RE::InputEvent* event = *a_event; event != nullptr; event = event->next) {
                    if (const auto* button = event->AsButtonEvent(); button && button->IsDown()) {
                        AddCurrentEpochTask("UIManager::RestoreModalFocus", [this]() {
                            std::lock_guard lock(MARAS::GetPapyrusMutex());
                            if (ModalManager::GetSingleton().HasActiveModal() &&
                                !IsModalDisplayBlocked() &&
                                !IsFocusOwned()) {
                                SyncViewVisibilityAndFocus();
                            }
                        });
                        break;
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // While the Magelight UI holds focus, React handles Escape and toggleUIKey directly.
        if (focusOwned_.load(std::memory_order_acquire)) {
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

            const bool ctrlDown = IsModifierHeld(VK_CONTROL);
            const bool shiftDown = IsModifierHeld(VK_SHIFT);
            const bool altDown = IsModifierHeld(VK_MENU);
            if (ctrlDown != settingsSnap->toggleUIModCtrl ||
                shiftDown != settingsSnap->toggleUIModShift ||
                altDown != settingsSnap->toggleUIModAlt) {
                continue;
            }

            HandleToggleHotkey();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl UIManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!a_event->opening && a_event->menuName == RE::JournalMenu::MENU_NAME) {
            AddCurrentEpochTask("UIManager::JournalMenuClosed", [this]() {
                std::lock_guard lock(MARAS::GetPapyrusMutex());
                if (openDashboardAfterJournalClose_.exchange(false, std::memory_order_acq_rel)) {
                    ShowOnMainThread();
                }
            });
        }

        const bool isRaceSexClosed = (!a_event->opening && a_event->menuName == RE::RaceSexMenu::MENU_NAME);
        if (isRaceSexClosed || (!a_event->opening && a_event->menuName == RE::LoadingMenu::MENU_NAME)) {
            AddCurrentEpochTask("UIManager::NewGameMenuClosed", [this, isRaceSexClosed]() {
                std::lock_guard lock(MARAS::GetPapyrusMutex());
                if (isRaceSexClosed && deferModalsUntilRaceMenuClosed_.exchange(false, std::memory_order_acq_rel)) {
                    MARAS_LOG_INFO("[UIManager] RaceSexMenu closed — lifting new-game modal deferral");
                }
                TryRunPendingNewGameScan();
                if (ModalManager::GetSingleton().HasActiveModal() && !IsModalDisplayBlocked()) {
                    ModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/false);
                }
                SyncViewVisibilityAndFocus();
            });
            return RE::BSEventNotifyControl::kContinue;
        }

        if (ModalManager::GetSingleton().HasActiveModal() ||
            IsVisible() ||
            a_event->menuName == RE::Console::MENU_NAME ||
            a_event->menuName == RE::JournalMenu::MENU_NAME ||
            a_event->menuName == RE::RaceSexMenu::MENU_NAME ||
            a_event->menuName == "Pause Menu") {
            AddCurrentEpochTask("UIManager::MenuSync", [this]() {
                std::lock_guard lock(MARAS::GetPapyrusMutex());
                if (ModalManager::GetSingleton().HasActiveModal() && !IsModalDisplayBlocked()) {
                    ModalManager::GetSingleton().SendActiveModalToUI(/*forceResend=*/false);
                }
                SyncViewVisibilityAndFocus();
            });
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MARAS::Bridge::Ui
