#pragma once

#include "RE/Skyrim.h"

#include <atomic>
#include <cstdint>

namespace MARAS {

    /**
     * Intercepts UIExtensions (`CustomMenu` / `UIListMenu`) and `UILIB_1` child-naming menus
     * opened by Fertility Mode Reloaded (`_JSW_BB_Utility.GenerateName("Name your ...")`) without
     * modifying FMR or UIExtensions scripts.
     *
     * Hides `CustomMenu` immediately on open, displays a standalone M.A.R.A.S. child-naming modal
     * via `Engine::IEngineEventHooks::OnFertilityChildNamingRequested`, and writes the player's
     * chosen name back into FMR's suspended VM frame before unblocking `UIListMenu` / `UILIB_1`.
     */
    class FertilityNamingMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        [[nodiscard]] static FertilityNamingMenuSink* GetSingleton() noexcept;

        void Revert() noexcept;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

    private:
        FertilityNamingMenuSink() = default;
        ~FertilityNamingMenuSink() override = default;

        FertilityNamingMenuSink(const FertilityNamingMenuSink&) = delete;
        FertilityNamingMenuSink& operator=(const FertilityNamingMenuSink&) = delete;
        FertilityNamingMenuSink(FertilityNamingMenuSink&&) = delete;
        FertilityNamingMenuSink& operator=(FertilityNamingMenuSink&&) = delete;

        std::atomic<std::uint32_t> activeStackId_{ 0 };
    };

}  // namespace MARAS
