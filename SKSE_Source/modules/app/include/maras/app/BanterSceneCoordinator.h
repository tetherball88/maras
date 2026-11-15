#pragma once

#include "RE/Skyrim.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace MARAS::App {

    /**
     * App-layer coordinator for NPC-NPC banter scene preparation, relocation/restoration,
     * caught/fallback planning, and SkyrimNet mute lifecycle with co-save recovery.
     */
    class BanterSceneCoordinator {
    public:
        [[nodiscard]] static BanterSceneCoordinator& GetSingleton();

        BanterSceneCoordinator(const BanterSceneCoordinator&) = delete;
        BanterSceneCoordinator& operator=(const BanterSceneCoordinator&) = delete;
        BanterSceneCoordinator(BanterSceneCoordinator&&) = delete;
        BanterSceneCoordinator& operator=(BanterSceneCoordinator&&) = delete;

        /**
         * Plans a banter scene, relocates off-cell/scene participants via persistent XMarkers,
         * exposes cheating affairs when appropriate, and returns JSON consumed by TTM_NpcsScenes.psc.
         */
        [[nodiscard]] std::string PrepareBanterScene(
            RE::Actor* npc1In,
            RE::Actor* npc2In,
            std::string_view forcedScenario,
            bool hasSexFramework,
            bool hasBedsNearby);

        /**
         * Plans a non-caught cheating banter fallback when intimate setup fails.
         * Returns JSON {"sceneKind": <int>, "sceneIdx": <int>}.
         */
        [[nodiscard]] std::string PlanBanterCheatingFallback(
            RE::Actor* npc1,
            RE::Actor* npc2,
            bool hasSexFramework,
            bool hasBedsNearby);

        /**
         * Plans a variant index for a caught-cheating scene, or -1 if invalid / cheater is player.
         */
        [[nodiscard]] std::int32_t PlanCaughtCheatingSceneIndex(
            RE::Actor* cheater,
            RE::Actor* affairPartner);

        /**
         * Applies SkyrimNet filter faction mutes to active banter participants and records them in 'BNT1'.
         */
        void MuteBanterSceneParticipants(
            RE::Actor* npc1,
            RE::Actor* npc2,
            RE::Actor* npc1Partner,
            bool shouldMutePartner);

        /**
         * Removes all recorded banter SkyrimNet filter faction mutes and clears 'BNT1' muted list.
         */
        void UnMuteBanterSceneParticipants();

        /**
         * Removes stale banter mutes loaded from 'BNT1' when SkyrimNet is unavailable or the actor
         * is no longer in an active scene.
         */
        void RecoverStaleMutesOnLoad();

        /**
         * Returns banter-relocated NPCs not currently in the player's cell back to their stored
         * XMarkers, disables/deletes the markers, and erases them from 'BNT1'.
         */
        void RestoreMovedBanterSceneNpcs();

    private:
        BanterSceneCoordinator() = default;
        ~BanterSceneCoordinator() = default;
    };

}  // namespace MARAS::App
