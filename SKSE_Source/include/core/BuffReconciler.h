#pragma once

#include "PCH.h"

#include <atomic>
#include <string>
#include <vector>

namespace MARAS {

    /**
     * BuffReconciler
     * --------------
     * Native C++ main-thread reconciler for Player spouse/follower perk and spell buffs
     * defined in `Data/SKSE/Plugins/MARAS/bonuses.json`.
     *
     * Eliminates Papyrus `TTM_ServiceBuff.Maintenance()` dispatch latency and floating-point
     * churn:
     * - Groups BonusEntry definitions by resolved `RE::BGSPerk*`.
     * - Computes permanent social-class and active-teammate skill-type multipliers from
     *   `WorldStore::Snapshot()` + `PollingService::GetCurrentTeammates()`.
     * - Applies `State::ComputeBonusTargetValue` (preserving negative base values without `std::abs`).
     * - Skips form mutation completely when all entries for a perk are inactive, removing the perk
     *   only if currently present on `PlayerCharacter`.
     * - Performs at most one `RemovePerk` / `AddPerk` cycle per perk when magnitudes/entry values change.
     */
    class BuffReconciler {
    public:
        static BuffReconciler& GetSingleton();

        // Converges Player perks & spell/entry-point magnitudes on the main game thread
        // (coalesces via SessionEpoch-fenced task if invoked off-thread).
        void ReconcilePlayerBuffs();

        // Clears cached resolved perk groups so the next reconcile re-reads BonusesService.
        void InvalidateCache();

        // Clears cached forms and pending coalesce state across save/revert boundaries.
        void Revert();

    private:
        BuffReconciler() = default;
        ~BuffReconciler() = default;
        BuffReconciler(const BuffReconciler&) = delete;
        BuffReconciler& operator=(const BuffReconciler&) = delete;

        struct CachedEntryRef {
            bool isPermanent{ true };
            std::size_t categoryIndex{ 0 };
            int effectIndex{ 0 };
            std::string type;
            float baseValue{ 0.0f };
        };

        struct PerkGroup {
            RE::BGSPerk* perk{ nullptr };
            std::vector<CachedEntryRef> entries;
        };

        void RebuildCacheIfNeeded();

        bool cacheValid_{ false };
        std::vector<PerkGroup> perkGroups_;
        std::atomic<bool> pendingReconcile_{ false };
    };

}  // namespace MARAS
