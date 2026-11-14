#pragma once

#include "RE/Skyrim.h"

#include <cstdint>
#include <vector>

namespace MARAS {

    /**
     * BanterSceneStarter
     * ------------------
     * Layer 3 engine validation and spatial candidate-scan helper for NPC-NPC banter scenes.
     *
     * Delegates pure relationship/cooldown planning to `Domain::Agency::BanterPlanner` while
     * performing live `RE::Actor*` / `RE::BGSLocation*` engine checks via `SpatialSnapshotStore`,
     * `SpatialWorldIndex`, `FormCache`, and `Utils::IsPlayerFollower`.
     */
    class BanterSceneStarter {
    public:
        static BanterSceneStarter& GetSingleton();

        /**
         * Scans around the player within `scanRadius` using `Domain::Agency::BanterPlanner` and
         * live engine validation. Returns `[actorA, actorB]` for the highest-priority valid couple
         * (Tier 1 local pair preferred over Tier 2 local + absent partner), or empty if none qualify.
         */
        [[nodiscard]] std::vector<RE::Actor*> FindBanterCoupleCandidates(float scanRadius = 3000.0f) const;

        /**
         * Returns all valid, living, peaceful, non-follower, non-main-partner actors within
         * `scanRadius` of the player using `SpatialSnapshotStore` + live ghost checks.
         */
        [[nodiscard]] std::vector<RE::Actor*> GetNearbyLivingActors(float scanRadius = 3000.0f) const;

        /**
         * Checks if a partner actor is physically loaded, 3D loaded, and within `scanRadius`
         * of the player.
         */
        [[nodiscard]] bool IsBanterPartnerPresent(RE::Actor* partner, float scanRadius = 3000.0f) const;

        /**
         * Authoritative live-engine validation predicate for banter candidate actors.
         */
        [[nodiscard]] bool IsValidBanterActor(
            RE::Actor* actor,
            bool checkDistanceAndLoaded = true,
            float scanRadius = 3000.0f) const;

        /**
         * Checks if a location is eligible for relationship banter scenes based on its
         * FormCache location type keywords and the active `SettingsData` toggles.
         */
        [[nodiscard]] bool IsLocationAllowedForScenes(RE::BGSLocation* location) const;

    private:
        BanterSceneStarter() = default;
        ~BanterSceneStarter() = default;
        BanterSceneStarter(const BanterSceneStarter&) = delete;
        BanterSceneStarter& operator=(const BanterSceneStarter&) = delete;
    };

}  // namespace MARAS
