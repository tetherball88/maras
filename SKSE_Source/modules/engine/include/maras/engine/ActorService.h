#pragma once

#include "maras/engine/EngineLogging.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <optional>
#include <string>

namespace MARAS::ActorSvc {

    // Object Extraction
    RE::TESNPC* GetActorBase(RE::FormID id);
    RE::TESObjectREFR* GetReference(RE::FormID id);

    // Attributes & Metadata
    std::string GetName(RE::FormID id);
    std::string GetRaceName(RE::FormID id);
    std::string GetRaceEditorID(RE::FormID id);
    RE::TESRace* GetRace(RE::FormID id);
    std::string GetPluginName(RE::FormID id);
    std::uint8_t GetSex(RE::FormID id);
    bool IsValid(RE::FormID id);

}  // namespace MARAS::ActorSvc

namespace MARAS {

    /**
     * Thread-safe werewolf state adapter for PerspectiveView.
     * Prefers the 2 Hz SpatialActorSnapshot for loaded actors and falls back
     * to static TESNPC / FormCache / companion quest checks only when unloaded
     * or not yet present in the snapshot (never dereferences live RE::Actor* off-thread).
     */
    class EngineWerewolfStateProvider final : public Projection::IWerewolfStateProvider {
    public:
        [[nodiscard]] std::optional<bool> IsAdultWerewolf(State::ActorId actor) const noexcept override;
    };

    [[nodiscard]] const EngineWerewolfStateProvider& GetEngineWerewolfStateProvider() noexcept;

    /**
     * Thread-safe metadata helpers reading immutable WorldStateSnapshot + SpatialActorSnapshot
     * + indexed/static records without dereferencing live RE::Actor* off-main-thread.
     */
    [[nodiscard]] std::string ResolveActorNameSafe(
        RE::FormID id,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial);

    [[nodiscard]] State::ActorSex ResolveActorSexSafe(
        RE::FormID id,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial) noexcept;

    [[nodiscard]] std::string ResolveActorRaceSafe(
        RE::FormID id,
        const State::WorldStateSnapshot& world,
        const State::SpatialActorSnapshot& spatial);

    [[nodiscard]] RE::FormID ResolveBaseFormIDSafe(
        RE::FormID id,
        const State::SpatialActorSnapshot& spatial) noexcept;

}  // namespace MARAS
