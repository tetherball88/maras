#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "maras/state/EngineSyncPlan.h"

#include <atomic>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <span>
#include <vector>

namespace MARAS {

    /**
     * FactionReconciler
     * -----------------
     * Single owner of SKSE faction hooks, internal-mutation suppression scope,
     * listener notifications, and idempotent main-thread faction convergence.
     *
     * All mutations check current actor faction membership & rank (and native
     * TESActorBaseData::factions where applicable) before invoking engine writes,
     * and marshal off-thread calls through DeferredActorResolver.
     */
    class FactionReconciler {
    public:
        using FactionAddedCallback = std::function<void(RE::Actor*, RE::TESFaction*, std::int8_t)>;
        using FactionRemovedCallback = std::function<void(RE::Actor*, RE::TESFaction*)>;

        static FactionReconciler& GetSingleton();

        static void InstallHooks();

        // RAII scope marking internal MARAS faction writes so hooks skip external auto-promote/demote
        struct InternalFactionScope {
            InternalFactionScope();
            ~InternalFactionScope();
        };

        static bool IsInternalFactionUpdate();

        // Listener registration for external systems (e.g. child adoption tracking)
        void RegisterFactionAddedListener(FactionAddedCallback callback);
        void RegisterFactionRemovedListener(FactionRemovedCallback callback);

        // Internal dispatcher called from the engine hooks
        void NotifyFactionAdded(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_rank);
        void NotifyFactionRemoved(RE::Actor* a_actor, RE::TESFaction* a_faction);

        // Engine-level helpers & reconcilers
        [[nodiscard]] static bool IsNativeBaseFaction(const RE::Actor* actor, const RE::TESFaction* faction) noexcept;

        // Converges Player (0x14) factions (`PlayerMarriedFaction` & `CanFormNonCheatingMain`).
        void ReconcilePlayer();

        // Removes PlayerMarriedFaction from the player if polygamy is currently enabled.
        void SanitizePlayerMarriedFaction();

        // Converges a loaded actor's full MARAS + mirrored vanilla factions from WorldStore::Snapshot().
        void ReconcileLoadedActor(RE::Actor* actor);

        // Main-thread gated entry points for one or many actor FormIDs.
        void ReconcileActor(RE::FormID actorId);
        void ReconcileActors(std::span<const RE::FormID> actorIds);

        // Reconciles Player + all MARAS-relevant adults, children, and companions after co-save load.
        void ReconcileAllOnLoad();

        void Revert() noexcept;

        // Explicitly strips MARAS/Hearthfires household factions from a child leaving player custody
        // prior to restoring pre-adoption snapshotted factions.
        void ClearChildHouseholdFactions(RE::Actor* actor);

    private:
        FactionReconciler() = default;
        ~FactionReconciler() = default;
        FactionReconciler(const FactionReconciler&) = delete;
        FactionReconciler& operator=(const FactionReconciler&) = delete;

        void ReconcilePlayerOnMainThread();
        void ReconcileAllOnLoadOnMainThread();
        void ClearChildHouseholdFactionsOnMainThread(RE::Actor* actor);

        bool SyncActorFaction(
            RE::Actor* actor,
            RE::TESFaction* faction,
            std::optional<std::int8_t> desiredRank,
            bool preserveIfNativeOnBase = false);

        bool ApplyMembershipOp(
            RE::Actor* actor,
            RE::TESFaction* faction,
            State::FactionMembershipOp op,
            std::int8_t defaultRank = 0,
            bool preserveIfNativeOnBase = false);

        void ReconcileAdultFactions(
            RE::Actor* actor,
            const State::WorldStateSnapshot& snap,
            const State::AdultActorFactionPlan& plan);

        void ReconcileMerchantFactions(
            RE::Actor* actor,
            bool isHousedAdultTenant,
            bool stripNonNativeDefaultMerchants);

        void ReconcileChildFactions(
            RE::Actor* actor,
            const State::WorldStateSnapshot& snap,
            const State::ChildFactionPlan& plan);

        void ReconcileCompanionFactions(
            RE::Actor* actor,
            const State::CompanionFactionPlan& plan);

        void ClearUnknownActorMarasFactions(RE::Actor* actor);

        std::shared_mutex mutex_{};
        std::vector<FactionAddedCallback> addedListeners_{};
        std::vector<FactionRemovedCallback> removedListeners_{};
        std::atomic<bool> pendingAllOnLoad_{ false };
    };

}  // namespace MARAS
