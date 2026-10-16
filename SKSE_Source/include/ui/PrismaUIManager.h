#pragma once

#include "PCH.h"
#include "ui/PrismaUI_API.h"
#include "utils/Common.h"

namespace MARAS {

    class PrismaUIManager : public RE::BSTEventSink<RE::InputEvent*> {
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

        bool IsAvailable() const { return prismaUI != nullptr; }
        bool IsViewValid() const { return prismaUI && prismaUI->IsValid(view); }
        bool IsVisible() const { return IsViewValid() && !prismaUI->IsHidden(view); }

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

    private:
        PRISMA_UI_API::IVPrismaUI1* prismaUI = nullptr;
        PrismaView view = 0;
        bool initialized = false;
        bool isListeningInput = false;

        static void OnDomReady(PrismaView view);

        PrismaUIManager() = default;
        ~PrismaUIManager() = default;
        PrismaUIManager(const PrismaUIManager&) = delete;
        PrismaUIManager& operator=(const PrismaUIManager&) = delete;
    };

}
