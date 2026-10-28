#pragma once

#include "PCH.h"
#include "core/NPCRelationshipManager.h"

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace MARAS {

    /**
     * @brief Service responsible for generating and updating SkyrimNet character profiles
     *        for newly spawned placed child actors (Stage 2 Young Child and Stage 4 Adult Child).
     *
     * Rules:
     *  - Operates strictly on placed RE::Actor references in the game world (!IsSyntheticChildID).
     *  - Never runs on synthetic child records (Stage 0 Unborn, Stage 1 Infant, Stage 3 InTraining,
     *    or unplaced synthetic Stage 2 / Stage 4 records).
     *  - Never runs on household relocations or custody reassignments of an already-spawned actor.
     *  - Deduplicates per (actorFormID, ChildStage) within a save session so each placed child actor
     *    triggers profile generation at most once for Stage 2 (Young Child) and once for Stage 4 (Adult).
     */
    class SkyrimNetProfileService {
    public:
        static SkyrimNetProfileService& GetSingleton() {
            static SkyrimNetProfileService singleton;
            return singleton;
        }

        // RAII guard to suppress profile updates during save load or migration
        class SuppressScope {
        public:
            SuppressScope() { ++suppressDepth_; }
            ~SuppressScope() { --suppressDepth_; }
            SuppressScope(const SuppressScope&) = delete;
            SuppressScope& operator=(const SuppressScope&) = delete;
        };

        static bool IsSuppressed() { return suppressDepth_.load() > 0; }

        // Clear per-session tracking state on save revert / load
        void Revert();

        // Mark an existing placed child actor's stage as already having a profile (e.g. during co-save load)
        void MarkActorStageUpdated(RE::FormID childID, ChildStage stage);

        // Check whether a profile update has already been requested for this actor and stage
        bool HasUpdatedActorStage(RE::FormID childID, ChildStage stage) const;

        // Request an asynchronous SkyrimNet character profile generation/update for a newly spawned
        // placed child actor (ChildStage::Child or ChildStage::Adult only).
        void RequestChildProfileUpdate(RE::FormID childID, ChildStage stage);

        // Request an asynchronous SkyrimNet dynamic profile update for any placed actor with optional
        // additional LLM instructions and comma-separated updatable block names via PublicRequestProfileUpdate.
        bool RequestActorProfileUpdate(RE::FormID actorFormID,
                                       const std::string& additionalInstructions = "",
                                       const std::string& updatableBlocksCSV = "");

    private:
        SkyrimNetProfileService() = default;
        ~SkyrimNetProfileService() = default;
        SkyrimNetProfileService(const SkyrimNetProfileService&) = delete;
        SkyrimNetProfileService& operator=(const SkyrimNetProfileService&) = delete;

        static uint64_t MakeStageKey(RE::FormID childID, ChildStage stage) noexcept {
            return (static_cast<uint64_t>(childID) << 8) | static_cast<uint64_t>(static_cast<uint8_t>(stage));
        }

        static inline std::atomic<int> suppressDepth_{0};
        mutable std::mutex mutex_;
        std::unordered_set<uint64_t> updatedActorStages_;
    };

}  // namespace MARAS

