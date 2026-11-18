#pragma once

#include "RE/Skyrim.h"

namespace MARAS {

    /**
     * Intercepts SkyUI MCM option clicks (`_global.skse.SendModEvent("SKICP_optionSelected", ...)`)
     * inside `Journal Menu` for third-party fertility mods (`_JSW_BB_ConfigQuestScript` / `FWSystemConfig`)
     * when the player attempts to spawn young/adult children, send children to training, or remove
     * children through those MCMs instead of the M.A.R.A.S. Dashboard.
     *
     * Blocks the click before it reaches Papyrus, unlocks SkyUI's MCM panel cleanly, closes
     * `Journal Menu`, and notifies `Engine::IEngineEventHooks::OnBlockedFertilityMcmClick` so
     * Layer 4 displays a standalone M.A.R.A.S. modal guiding the player to the Dashboard.
     */
    class FertilityMcmClickSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        [[nodiscard]] static FertilityMcmClickSink* GetSingleton() noexcept;

        static bool RegisterScaleformCallback(RE::GFxMovieView* a_view, RE::GFxValue* a_root);

        static void InstallOnJournalMovie(RE::GFxMovieView* movie);

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

    private:
        FertilityMcmClickSink() = default;
        ~FertilityMcmClickSink() override = default;

        FertilityMcmClickSink(const FertilityMcmClickSink&) = delete;
        FertilityMcmClickSink& operator=(const FertilityMcmClickSink&) = delete;
        FertilityMcmClickSink(FertilityMcmClickSink&&) = delete;
        FertilityMcmClickSink& operator=(FertilityMcmClickSink&&) = delete;
    };

}  // namespace MARAS
