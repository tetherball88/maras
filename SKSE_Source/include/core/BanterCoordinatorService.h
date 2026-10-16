#pragma once

#include "core/NPCRelationshipManager.h"
#include "core/Serialization.h"
#include "utils/Common.h"

namespace SKSE {
    class SerializationInterface;
}

namespace MARAS {

    /**
     * BanterCoordinatorService - Coordinates NPC-NPC relationship candidate pairs
     * for Skyrim CK banter scenes.
     *
     * Evaluates nearby NPCs around the player ref using a strict radius:
     * - Tier 1: If both partners in an active relationship are present in the radius,
     *   returns a randomly chosen pair.
     * - Tier 2: If no complete couples are present, searches singular local NPCs whose
     *   active partners exist elsewhere in the game world (alive, placed, and not disabled),
     *   returning a randomly chosen pair [localNPC, absentPartner].
     *
     * Enforces built-in repetition cooldown per couple, serialized to the SKSE co-save.
     */
    class BanterCoordinatorService {
    public:
        static BanterCoordinatorService& GetSingleton() {
            static BanterCoordinatorService singleton;
            return singleton;
        }

        // ── Candidate Queries ──────────────────────────────────────────────

        /**
         * Scans around the player within scanRadius for relationship banter candidates.
         * Returns a vector of Actors:
         * - If Tier 1: [ActorA, ActorB] (both present locally)
         * - If Tier 2: [LocalActor, AbsentPartner] (local actor and world partner)
         * - If none available (or all on cooldown): empty vector
         */
        std::vector<RE::Actor*> FindBanterCoupleCandidates(float scanRadius = 3000.0f);

        /**
         * Checks if a partner actor is physically loaded, 3D loaded, and within scanRadius of the player.
         * Returns true if partner is present (Tier 1), false if absent (Tier 2).
         */
        bool IsBanterPartnerPresent(RE::Actor* partner, float scanRadius = 3000.0f) const;

        /**
         * Authoritative validation predicate for banter candidate actors.
         * Enforces: placed ref, alive, not disabled, not deleted, not player, not follower/teammate,
         * not in combat, not in scene, not ghost, not child, and not in DeceasedRegistry.
         */
        bool IsValidBanterActor(RE::Actor* actor, bool checkDistanceAndLoaded = true, float scanRadius = 3000.0f) const;

        /**
         * Checks if a location is eligible for relationship banter scenes based on its
         * location keywords and the user's scene location type settings.
         */
        bool IsLocationAllowedForScenes(RE::BGSLocation* location) const;

        // ── Cooldown Management ────────────────────────────────────────────

        float GetCooldownHours() const;
        void SetCooldownHours(float hours);

        bool IsSceneOnCooldown(float currentDay) const;
        void RecordScenePlayed(RE::Actor* actorA = nullptr, RE::Actor* actorB = nullptr);
        bool IsOnCooldown(RE::FormID actorA, RE::FormID actorB, float currentDay) const;
        void SetCooldown(RE::FormID actorA, RE::FormID actorB, float currentDay);
        void ResetCooldown(RE::FormID actorA, RE::FormID actorB);
        void ClearAllCooldowns();

        // ── SKSE Co-Save Serialization ─────────────────────────────────────

        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization, std::uint32_t version);
        void Revert();

    private:
        BanterCoordinatorService() = default;
        ~BanterCoordinatorService() = default;
        BanterCoordinatorService(const BanterCoordinatorService&) = delete;
        BanterCoordinatorService& operator=(const BanterCoordinatorService&) = delete;

        // Collects all valid, living, peaceful, non-follower actors within scanRadius of the player
        std::vector<RE::Actor*> GetNearbyLivingActors(float scanRadius) const;

        // Returns last-played timestamp in game days for a pair (-1.0f if never played)
        float GetLastPlayedDay(RE::FormID actorA, RE::FormID actorB) const;

        // Selects a couple prioritizing never-played couples first, then least-recently-played couples
        std::pair<RE::Actor*, RE::Actor*> SelectPrioritizedCouple(
            const std::vector<std::pair<RE::Actor*, RE::Actor*>>& candidates) const;

        // Per-couple repetition cooldown multiplier relative to global scene cooldown (2x)
        static constexpr float kCoupleCooldownMultiplier = 2.0f;

        mutable std::shared_mutex m_mutex;

        // Default cooldown duration in game hours (24 in-game hours = 1 game day)
        float m_cooldownHours{ 24.0f };

        // Last scene execution timestamp (in game days passed, -1.0f means no scene played yet)
        float m_lastSceneDay{ -1.0f };

        // Last-picked timestamp per couple key (in game days passed)
        std::unordered_map<RelationshipPairKey, float, RelationshipPairKeyHash> m_coupleCooldowns;
    };

}  // namespace MARAS
