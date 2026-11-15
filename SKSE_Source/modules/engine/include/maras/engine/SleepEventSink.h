#pragma once

#include "maras/engine/EngineLogging.h"

namespace MARAS {

    /**
     * Native TESSleepStopEvent listener replacing TTM_PlayerScript.ApplySleepRestedSpell().
     * Pays same-cell partner sleep affection via IEngineEventHooks::OnPlayerSlept,
     * queues vanilla PlayerSleepQuestScript rested removal, and reconciles MARAS/BYOH
     * rested abilities via BuffReconciler::ApplySleepRestedPlan strictly afterward.
     */
    class SleepEventSink final : public RE::BSTEventSink<RE::TESSleepStopEvent> {
    public:
        static SleepEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESSleepStopEvent* a_event,
            RE::BSTEventSource<RE::TESSleepStopEvent>* a_eventSource) override;

    private:
        SleepEventSink() = default;
        ~SleepEventSink() override = default;
        SleepEventSink(const SleepEventSink&) = delete;
        SleepEventSink(SleepEventSink&&) = delete;
        SleepEventSink& operator=(const SleepEventSink&) = delete;
        SleepEventSink& operator=(SleepEventSink&&) = delete;
    };

}  // namespace MARAS
