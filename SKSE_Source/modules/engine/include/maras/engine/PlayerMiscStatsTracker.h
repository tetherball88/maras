#pragma once

#include "maras/engine/EngineLogging.h"

#include <cstdint>
#include <mutex>

namespace MARAS {

    struct PlayerTrackedMiscStats {
        int mostGold{ 0 };
        int housesOwned{ 0 };
        int horsesOwned{ 0 };
        int questsCompleted{ 0 };
        int dungeonsCleared{ 0 };
        int dragonSoulsCollected{ 0 };
    };

    /**
     * PlayerMiscStatsTracker
     * ----------------------
     * Listens to RE::TESTrackedStatsEvent and seeds initial tracked player stats
     * asynchronously via Game.QueryStat on save load / new game so dialogue-open and
     * divorce calculations read native C++ cached stats instead of synchronous VM calls.
     */
    class PlayerMiscStatsTracker final : public RE::BSTEventSink<RE::TESTrackedStatsEvent> {
    public:
        static PlayerMiscStatsTracker* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESTrackedStatsEvent* a_event,
            RE::BSTEventSource<RE::TESTrackedStatsEvent>*) override;

        void Revert() noexcept;
        void SeedFromVmAsync();

        [[nodiscard]] PlayerTrackedMiscStats GetStats() const;

    private:
        enum class StatSlot : std::size_t {
            MostGold = 0,
            HousesOwned = 1,
            HorsesOwned = 2,
            QuestsCompleted = 3,
            DungeonsCleared = 4,
            DragonSoulsCollected = 5,
            _Count
        };

        PlayerMiscStatsTracker() = default;
        ~PlayerMiscStatsTracker() override = default;

        PlayerMiscStatsTracker(const PlayerMiscStatsTracker&) = delete;
        PlayerMiscStatsTracker& operator=(const PlayerMiscStatsTracker&) = delete;
        PlayerMiscStatsTracker(PlayerMiscStatsTracker&&) = delete;
        PlayerMiscStatsTracker& operator=(PlayerMiscStatsTracker&&) = delete;

        void ApplyVmSeededStat(std::uint64_t expectedEpoch, StatSlot slot, std::int32_t value) noexcept;

        class QueryStatCallback;

        mutable std::mutex mutex_{};
        PlayerTrackedMiscStats stats_{};
        bool liveUpdated_[static_cast<std::size_t>(StatSlot::_Count)]{};
    };

}  // namespace MARAS
