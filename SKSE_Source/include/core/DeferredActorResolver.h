#pragma once

#include "utils/Common.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace MARAS {

    /**
     * DeferredActorResolver
     * ---------------------
     * Main-thread gate and unloaded-actor reconciliation queue (`PendingEngineSyncSet`).
     *
     * Responsibilities:
     * 1. Tracks the Skyrim WinMain thread ID (`CaptureMainThread()` at `SKSEPluginLoad` /
     *    `kInputLoaded` only — never `kDataLoaded` [`InitTESThread`] and never `ScopedMainThreadTask`,
     *    because `BSTaskPool` rotates across `BSJobs` worker threads where `IsInMainThreadTask()` scopes execution).
     * 2. When off-thread callers request faction/package reconciliation, merges `SyncFlags`
     *    into `pendingSync_` and schedules a single coalesced `SessionEpoch`-fenced task
     *    to flush on the main thread.
     * 3. When a reconciler encounters a `FormID` whose `RE::Actor*` is not yet resolved
     *    in memory (`LookupByID<RE::Actor>` == nullptr), `MarkUnloaded()` retains its pending
     *    flags until `OnActorLoaded(actor)` fires from `ActorInitLoadedEventSink`.
     * 4. `Revert()` clears all pending/unloaded entries cleanly across save boundaries.
     *
     * Lock ordering: `mutex_` is a strict leaf mutex; callers NEVER hold `mutex_` while
     * calling SKSE/Skyrim/WorldStore/reconciler APIs.
     */
    class DeferredActorResolver {
    public:
        enum class SyncFlags : std::uint8_t {
            None = 0,
            Factions = 1u << 0,
            Package = 1u << 1,
            All = Factions | Package
        };

        static DeferredActorResolver& GetSingleton();

        void CaptureMainThread() noexcept;
        [[nodiscard]] bool IsOnMainThread() const noexcept;

        // Queues main-thread reconciliation for `actorId`, coalescing repeated requests per burst.
        void RequestSync(RE::FormID actorId, SyncFlags flags = SyncFlags::All);

        // Records an unresolved/unloaded actor so `OnActorLoaded` reconciles it once loaded.
        void MarkUnloaded(RE::FormID actorId, SyncFlags flags = SyncFlags::All);

        // Drains any unloaded or queued flags for `actor` (must be called on the main thread).
        void OnActorLoaded(RE::Actor* actor);

        // Clears all queued and unloaded actor entries across save/revert boundaries.
        void Revert();

        [[nodiscard]] std::size_t GetUnloadedCount() const;
        [[nodiscard]] std::size_t GetPendingCount() const;

    private:
        DeferredActorResolver() = default;
        ~DeferredActorResolver() = default;
        DeferredActorResolver(const DeferredActorResolver&) = delete;
        DeferredActorResolver& operator=(const DeferredActorResolver&) = delete;

        void DrainPending();
        static void ExecuteSync(RE::Actor* actor, std::uint8_t flagMask);

        mutable std::mutex mutex_{};
        std::unordered_map<RE::FormID, std::uint8_t> pendingSync_{};
        std::unordered_map<RE::FormID, std::uint8_t> unloadedActors_{};
        bool drainScheduled_{ false };

        std::atomic<bool> hasMainThread_{ false };
        std::atomic<std::thread::id> mainThreadId_{};
        std::atomic<std::uint32_t> mainOsThreadId_{ 0 };
    };

    [[nodiscard]] constexpr DeferredActorResolver::SyncFlags operator|(
        DeferredActorResolver::SyncFlags a,
        DeferredActorResolver::SyncFlags b) noexcept {
        return static_cast<DeferredActorResolver::SyncFlags>(
            static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    [[nodiscard]] constexpr DeferredActorResolver::SyncFlags operator&(
        DeferredActorResolver::SyncFlags a,
        DeferredActorResolver::SyncFlags b) noexcept {
        return static_cast<DeferredActorResolver::SyncFlags>(
            static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }

    [[nodiscard]] constexpr bool HasSyncFlag(
        std::uint8_t mask,
        DeferredActorResolver::SyncFlags flag) noexcept {
        return (mask & static_cast<std::uint8_t>(flag)) != 0;
    }

}  // namespace MARAS
