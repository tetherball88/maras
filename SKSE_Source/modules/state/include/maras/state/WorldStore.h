#pragma once

#include "maras/state/WorldState.h"

#include <atomic>
#include <concepts>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace MARAS::State {

    /**
     * WorldStore — Single authoritative runtime owner of `WorldState`.
     *
     * Concurrency Contract:
     * - Readers (`Snapshot()`): 100% lock-free `std::atomic<std::shared_ptr<const WorldStateSnapshot>>`
     *   loads (`memory_order_acquire`), safe from any thread without blocking writes.
     * - Writers (`Modify()`, `SetIniBaselineSettings()`, `ReplaceState()`, `Revert()`): Serialize
     *   under `writeMutex_` (strict leaf mutex — callers MUST NOT invoke engine/Papyrus/UI/network
     *   callbacks while holding `Modify`), then publish an immutable snapshot via `memory_order_release`.
     */
    class WorldStore {
    public:
        WorldStore();
        ~WorldStore() = default;

        WorldStore(const WorldStore&) = delete;
        WorldStore& operator=(const WorldStore&) = delete;
        WorldStore(WorldStore&&) = delete;
        WorldStore& operator=(WorldStore&&) = delete;

        static WorldStore& GetSingleton();

        // Lock-free point-in-time snapshot across all 10 normalized tables.
        [[nodiscard]] std::shared_ptr<const WorldStateSnapshot> Snapshot() const noexcept;

        // Serialized write transaction under the leaf mutex; publishes a fresh snapshot upon completion.
        template <std::invocable<WorldState&> Fn>
        decltype(auto) Modify(Fn&& fn) {
            using ReturnT = std::invoke_result_t<Fn, WorldState&>;
            std::lock_guard<std::recursive_mutex> lock(writeMutex_);
            if constexpr (std::is_void_v<ReturnT>) {
                std::forward<Fn>(fn)(liveState_);
                PublishSnapshotLocked();
                return;
            } else {
                decltype(auto) result = std::forward<Fn>(fn)(liveState_);
                PublishSnapshotLocked();
                return result;
            }
        }

        // Serialized read when callers require consistency against an ongoing write transaction.
        template <std::invocable<const WorldState&> Fn>
        decltype(auto) ReadLive(Fn&& fn) const {
            std::lock_guard<std::recursive_mutex> lock(writeMutex_);
            return std::forward<Fn>(fn)(static_cast<const WorldState&>(liveState_));
        }

        // Updates the INI-loaded baseline settings (preserved across SKSE Revert / New Game) and
        // applies them onto liveState_.settings.
        void SetIniBaselineSettings(const SettingsData& iniBaseline);

        [[nodiscard]] SettingsData GetIniBaselineSettings() const;

        // Atomically swaps in a freshly deserialized WorldState and publishes its snapshot.
        void ReplaceState(WorldState&& nextState);

        // Resets all 10 domain tables and restores SettingsData from iniBaselineSettings_.
        void Revert();

    private:
        void PublishSnapshotLocked() noexcept;

        mutable std::recursive_mutex writeMutex_{};
        WorldState liveState_{};
        SettingsData iniBaselineSettings_{};
        std::atomic<std::shared_ptr<const WorldStateSnapshot>> publishedSnapshot_{};
    };

}  // namespace MARAS::State
