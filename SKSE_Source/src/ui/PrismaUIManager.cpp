#include "ui/PrismaUIManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "utils/Settings.h"
#include <Windows.h>

namespace MARAS {

    void PrismaUIManager::StartListeningInput() {
        if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
            dm->RemoveEventSink(this);
            dm->AddEventSink(this);
            isListeningInput = true;
            MARAS_LOG_INFO("PrismaUIManager: started listening for input events");
        }
    }

    void PrismaUIManager::StopListeningInput() {
        if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
            dm->RemoveEventSink(this);
        }
        isListeningInput = false;
        MARAS_LOG_INFO("PrismaUIManager: stopped listening for input events");
    }

    RE::BSEventNotifyControl PrismaUIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_source*/)
    {
        if (!a_event || !*a_event) return RE::BSEventNotifyControl::kContinue;

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
            return;
        }

        prismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(api);
        initialized = true;
        MARAS_LOG_INFO("PrismaUI initialized successfully for M.A.R.A.S");

        StartListeningInput();
    }

    void PrismaUIManager::OnDomReady(PrismaView view) {
        MARAS_LOG_INFO("PrismaUIManager: DOM ready callback received for view {}", view);
        SKSE::GetTaskInterface()->AddTask([view]() {
            auto& mgr = PrismaUIManager::GetSingleton();
            if (mgr.view != view || !mgr.IsViewValid()) return;

            if (!mgr.prismaUI->HasFocus(mgr.view)) {
                MARAS_LOG_INFO("PrismaUIManager: OnDomReady Focusing view {}", mgr.view);
                mgr.prismaUI->Focus(mgr.view);
            }

            nlohmann::json statusData;
            statusData["status"] = "M.A.R.A.S Core Initialized";
            statusData["version"] = PROJECT_VERSION;

            std::string statusJson = statusData.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            mgr.InteropCall("receiveMARASStatus", statusJson.c_str());
            mgr.InteropCall("openMARAS", "");

            PrismaUIDataBridge::GetSingleton().SendOverviewData();
        });
    }

    void PrismaUIManager::Show() {
        if (!prismaUI) {
            MARAS_LOG_WARN("PrismaUIManager::Show called but PrismaUI API is not available");
            return;
        }

        MARAS_LOG_INFO("PrismaUIManager::Show() requested");

        if (view && prismaUI->IsValid(view)) {
            MARAS_LOG_INFO("PrismaUIManager: Showing existing valid view {}", view);
            prismaUI->Show(view);
            if (!prismaUI->HasFocus(view)) {
                prismaUI->Focus(view);
            }
            InteropCall("openMARAS", "");
            PrismaUIDataBridge::GetSingleton().SendOverviewData();
            return;
        }

        MARAS_LOG_INFO("PrismaUIManager: Creating view 'MARAS/index.html'...");
        auto newView = prismaUI->CreateView("MARAS/index.html", OnDomReady);
        if (!newView) {
            MARAS_LOG_ERROR("Failed to create PrismaUI view for M.A.R.A.S");
            return;
        }

        view = newView;
        prismaUI->RegisterJSListener(view, "closeMARAS", OnCloseMARAS);
        prismaUI->RegisterJSListener(view, "log", OnLog);
        prismaUI->RegisterJSListener(view, "refocusView", OnRefocusView);
        prismaUI->RegisterJSListener(view, "marasAction", OnMarasAction);
        
        prismaUI->Show(view);
        prismaUI->Focus(view);
        
        MARAS_LOG_INFO("PrismaUI view created successfully for M.A.R.A.S: handle {}", newView);
    }

    void PrismaUIManager::Hide() {
        if (!prismaUI || !view) {
            return;
        }

        MARAS_LOG_INFO("PrismaUIManager::Hide() called for view {}", view);

        if (prismaUI->IsValid(view)) {
            prismaUI->Hide(view);
            prismaUI->Unfocus(view);
            MARAS_LOG_INFO("PrismaUIManager: Hidden and unfocused view {}", view);
        }
    }

    void PrismaUIManager::Toggle() {
        if (!prismaUI) {
            MARAS_LOG_WARN("PrismaUIManager::Toggle() called but PrismaUI is not available");
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
                mgr.prismaUI->Focus(mgr.view);
            }
        });
    }

    void PrismaUIManager::OnMarasAction(const char* argument) {
        if (argument) {
            PrismaUIDataBridge::GetSingleton().HandleAction(argument);
        }
    }

}
