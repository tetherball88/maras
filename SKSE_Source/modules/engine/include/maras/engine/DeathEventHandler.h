#pragma once

#include "maras/engine/EngineLogging.h"

namespace MARAS {

    /**
     * DeathEventHandler - Listens for engine-level TESDeathEvent via ScriptEventSourceHolder.
     *
     * Catches 100% of actor deaths across the engine (combat, environmental, console, scripts)
     * and dispatches confirmed deaths on the main thread to IEngineEventHooks::OnActorDeath.
     */
    class DeathEventHandler : public RE::BSTEventSink<RE::TESDeathEvent> {
    public:
        static DeathEventHandler* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event,
                                              RE::BSTEventSource<RE::TESDeathEvent>*) override;

    private:
        DeathEventHandler() = default;
        ~DeathEventHandler() = default;
        DeathEventHandler(const DeathEventHandler&) = delete;
        DeathEventHandler(DeathEventHandler&&) = delete;
        DeathEventHandler& operator=(const DeathEventHandler&) = delete;
        DeathEventHandler& operator=(DeathEventHandler&&) = delete;

        void HandleActorDeathOnMainThread(RE::FormID victimID, RE::FormID killerID, bool isPlayerKiller);
    };

}  // namespace MARAS
