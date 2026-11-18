#pragma once

#include "maras/engine/EngineEventHooks.h"

#include <functional>
#include <string>

namespace MARAS::App {

    /**
     * Layer 4 coordinator presenting a standalone M.A.R.A.S. child-naming modal
     * (replacing Fertility Mode Reloaded's UIExtensions `UIListMenu` / `UILIB_1` popups),
     * enforcing `PerspectiveView::ForDashboard` Intrigue Mode secret masking on parent subtitles,
     * offering localized lore-friendly racial name suggestions via `NamePoolLoader`, and syncing
     * `OnChildGrewUp` when triggered from FMR's Debug MCM (`PLAYER_GROWTH` / `NPC_GROWTH`).
     */
    class FertilityChildNamingCoordinator {
    public:
        static FertilityChildNamingCoordinator& GetSingleton() noexcept;

        FertilityChildNamingCoordinator(const FertilityChildNamingCoordinator&) = delete;
        FertilityChildNamingCoordinator& operator=(const FertilityChildNamingCoordinator&) = delete;
        FertilityChildNamingCoordinator(FertilityChildNamingCoordinator&&) = delete;
        FertilityChildNamingCoordinator& operator=(FertilityChildNamingCoordinator&&) = delete;

        void ShowChildNamingModal(
            Engine::IEngineEventHooks::FertilityChildNamingRequest request,
            std::function<void(const std::string&)> onResolved);

        void PromptBeeingFemaleChildNaming(
            RE::FormID childId,
            RE::FormID motherId,
            bool isFemale,
            std::string initialName,
            std::function<void(const std::string&)> onNamed = {});

    private:
        FertilityChildNamingCoordinator() = default;
        ~FertilityChildNamingCoordinator() = default;
    };

}  // namespace MARAS::App
