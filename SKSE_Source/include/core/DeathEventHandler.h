#pragma once

#include "PCH.h"

namespace MARAS {

    /**
     * DeathEventHandler - Listens for engine-level TESDeathEvent via ScriptEventSourceHolder.
     *
     * Catches 100% of actor deaths across the engine (combat, environmental, console, scripts).
     * Distinguishes registered children, child pets, and registered adults, archiving them
     * in DeceasedRegistry before cleaning up their active state.
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
    };

}  // namespace MARAS
