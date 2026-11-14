#pragma once

#include "maras/foundation/StrongIds.h"
#include "maras/state/DomainTypes.h"
#include "maras/state/WorldTables.h"

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace MARAS::State {

    struct LoadedActorFlags {
        static constexpr std::uint16_t None              = 0u;
        static constexpr std::uint16_t IsPlayer          = 1u << 0;
        static constexpr std::uint16_t HasCurrentProcess = 1u << 1;
        static constexpr std::uint16_t Is3DLoaded        = 1u << 2;
        static constexpr std::uint16_t IsDead            = 1u << 3;
        static constexpr std::uint16_t IsDisabled        = 1u << 4;
        static constexpr std::uint16_t IsDeleted         = 1u << 5;
        static constexpr std::uint16_t IsChild           = 1u << 6;
        static constexpr std::uint16_t IsInCombat        = 1u << 7;
        static constexpr std::uint16_t IsInScene         = 1u << 8;
        static constexpr std::uint16_t IsMannequin       = 1u << 9;
        static constexpr std::uint16_t HasActorTypeNpc   = 1u << 10;
        static constexpr std::uint16_t IsPlayerTeammate  = 1u << 11;
        static constexpr std::uint16_t IsWerewolf        = 1u << 12;
        static constexpr std::uint16_t IsTrackedFaction  = 1u << 13;
        static constexpr std::uint16_t IsRegisteredAdult = 1u << 14;
    };

    /**
     * Fixed-size, trivially copyable per-actor sample populated at 2 Hz on the SKSE main thread.
     * Uses an inline fixed UTF-8 buffer so steady-state polling performs zero heap allocations.
     */
    struct LoadedActorSample {
        static constexpr std::size_t kDisplayNameBufferSize = 64u;
        static constexpr std::size_t kMaxDisplayNameBytes = kDisplayNameBufferSize - 1u;

        RefId refId{};
        BaseId baseId{};
        CellId parentCellId{};
        LocationId currentLocationId{};
        FormRawId raceId{};
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
        ActorSex sex{ ActorSex::Unknown };
        std::uint8_t displayNameLen{ 0 };
        std::uint16_t flags{ LoadedActorFlags::None };
        std::array<char, kDisplayNameBufferSize> displayName{};

        [[nodiscard]] constexpr bool HasFlag(std::uint16_t flag) const noexcept {
            return (flags & flag) == flag;
        }

        constexpr void SetFlag(std::uint16_t flag, bool enabled) noexcept {
            if (enabled) {
                flags = static_cast<std::uint16_t>(flags | flag);
            } else {
                flags = static_cast<std::uint16_t>(flags & ~flag);
            }
        }

        [[nodiscard]] constexpr bool IsPlayer() const noexcept { return HasFlag(LoadedActorFlags::IsPlayer); }
        [[nodiscard]] constexpr bool HasCurrentProcess() const noexcept { return HasFlag(LoadedActorFlags::HasCurrentProcess); }
        [[nodiscard]] constexpr bool Is3DLoaded() const noexcept { return HasFlag(LoadedActorFlags::Is3DLoaded); }
        [[nodiscard]] constexpr bool IsDead() const noexcept { return HasFlag(LoadedActorFlags::IsDead); }
        [[nodiscard]] constexpr bool IsDisabled() const noexcept { return HasFlag(LoadedActorFlags::IsDisabled); }
        [[nodiscard]] constexpr bool IsDeleted() const noexcept { return HasFlag(LoadedActorFlags::IsDeleted); }
        [[nodiscard]] constexpr bool IsChild() const noexcept { return HasFlag(LoadedActorFlags::IsChild); }
        [[nodiscard]] constexpr bool IsInCombat() const noexcept { return HasFlag(LoadedActorFlags::IsInCombat); }
        [[nodiscard]] constexpr bool IsInScene() const noexcept { return HasFlag(LoadedActorFlags::IsInScene); }
        [[nodiscard]] constexpr bool IsMannequin() const noexcept { return HasFlag(LoadedActorFlags::IsMannequin); }
        [[nodiscard]] constexpr bool HasActorTypeNpc() const noexcept { return HasFlag(LoadedActorFlags::HasActorTypeNpc); }
        [[nodiscard]] constexpr bool IsPlayerTeammate() const noexcept { return HasFlag(LoadedActorFlags::IsPlayerTeammate); }
        [[nodiscard]] constexpr bool IsWerewolf() const noexcept { return HasFlag(LoadedActorFlags::IsWerewolf); }
        [[nodiscard]] constexpr bool IsTrackedFaction() const noexcept { return HasFlag(LoadedActorFlags::IsTrackedFaction); }
        [[nodiscard]] constexpr bool IsRegisteredAdult() const noexcept { return HasFlag(LoadedActorFlags::IsRegisteredAdult); }

        void SetDisplayName(std::string_view raw) noexcept;

        [[nodiscard]] constexpr std::string_view DisplayNameView() const noexcept {
            const std::size_t len = (displayNameLen <= kMaxDisplayNameBytes)
                                        ? static_cast<std::size_t>(displayNameLen)
                                        : kMaxDisplayNameBytes;
            return std::string_view{ displayName.data(), len };
        }

        [[nodiscard]] constexpr float DistanceSquaredTo(const LoadedActorSample& other) const noexcept {
            const float dx = x - other.x;
            const float dy = y - other.y;
            const float dz = z - other.z;
            return dx * dx + dy * dy + dz * dz;
        }
    };

    static_assert(std::is_trivially_copyable_v<LoadedActorSample>);

    /**
     * Immutable (once published) flat array of loaded actors sorted ascending by RefId.
     * Reused in-place by SpatialSnapshotStore when its pool slot has use_count() == 1.
     */
    class SpatialActorSnapshot {
    public:
        void Clear() noexcept;
        void Reserve(std::size_t capacity);
        void SetMetadata(float currentGameDay, std::uint64_t sequence) noexcept;
        void AddSample(const LoadedActorSample& sample);
        void FinalizeSortAndDedupe() noexcept;

        [[nodiscard]] float CurrentGameDay() const noexcept { return currentGameDay_; }
        [[nodiscard]] std::uint64_t Sequence() const noexcept { return sequence_; }
        [[nodiscard]] std::size_t Size() const noexcept { return samples_.size(); }
        [[nodiscard]] bool Empty() const noexcept { return samples_.empty(); }
        [[nodiscard]] std::size_t Capacity() const noexcept { return samples_.capacity(); }
        [[nodiscard]] const std::vector<LoadedActorSample>& AllSamples() const noexcept { return samples_; }

        [[nodiscard]] const LoadedActorSample* Find(RefId refId) const noexcept;
        [[nodiscard]] bool Contains(RefId refId) const noexcept { return Find(refId) != nullptr; }
        [[nodiscard]] std::optional<float> GetDistanceSquared(RefId a, RefId b) const noexcept;
        [[nodiscard]] bool IsWithinRadius(RefId a, RefId b, float radius) const noexcept;

        template <typename Fn>
        void ForEachNearbyCandidate(RefId centerId, float radius, Fn&& visitor) const {
            if (radius <= 0.0f || centerId.IsNull()) {
                return;
            }
            const LoadedActorSample* center = Find(centerId);
            if (!center || !center->HasCurrentProcess() || center->IsDeleted()) {
                return;
            }

            const float radiusSq = radius * radius;
            for (const LoadedActorSample& target : samples_) {
                if (target.refId == centerId) {
                    continue;
                }
                if (!target.HasCurrentProcess() ||
                    target.IsDeleted() ||
                    target.IsDead() ||
                    target.IsInScene() ||
                    target.IsInCombat() ||
                    target.IsChild() ||
                    target.IsMannequin() ||
                    !target.HasActorTypeNpc()) {
                    continue;
                }
                if (center->DistanceSquaredTo(target) >= radiusSq) {
                    continue;
                }
                visitor(target);
            }
        }

        [[nodiscard]] std::vector<RefId> GetNearbyCandidates(RefId centerId, float radius = 2000.0f) const;
        [[nodiscard]] std::vector<RefId> GetPlayerTeammates(
            const ActorProfileTable* optionalRegisteredFilter = nullptr) const;

    private:
        float currentGameDay_{ -1.0f };
        std::uint64_t sequence_{ 0 };
        std::vector<LoadedActorSample> samples_{};
    };

    /**
     * Lock-free reader / pooled zero-alloc writer owner of the latest SpatialActorSnapshot.
     * Decoupled from WorldStore so high-frequency 2 Hz spatial polling never churns CoW world tables.
     */
    class SpatialSnapshotStore {
    public:
        static constexpr std::size_t kInitialPoolSlots = 4u;
        static constexpr std::size_t kDefaultSampleReserve = 256u;
        static constexpr std::size_t kMaxPoolSlots = 16u;

        SpatialSnapshotStore();
        ~SpatialSnapshotStore() = default;

        SpatialSnapshotStore(const SpatialSnapshotStore&) = delete;
        SpatialSnapshotStore& operator=(const SpatialSnapshotStore&) = delete;
        SpatialSnapshotStore(SpatialSnapshotStore&&) = delete;
        SpatialSnapshotStore& operator=(SpatialSnapshotStore&&) = delete;

        static SpatialSnapshotStore& GetSingleton();

        [[nodiscard]] std::shared_ptr<const SpatialActorSnapshot> Snapshot() const noexcept;

        template <std::invocable<SpatialActorSnapshot&> PopulateFn>
        std::shared_ptr<const SpatialActorSnapshot> Publish(float currentGameDay, PopulateFn&& populateFn) {
            std::lock_guard<std::mutex> lock(publishMutex_);

            std::shared_ptr<SpatialActorSnapshot> targetSlot{};
            for (const auto& slot : pool_) {
                if (slot && slot.use_count() == 1) {
                    // Establish happens-before with the last external reader releasing this slot.
                    std::atomic_thread_fence(std::memory_order_acquire);
                    targetSlot = slot;
                    break;
                }
            }

            if (!targetSlot) {
                targetSlot = std::make_shared<SpatialActorSnapshot>();
                targetSlot->Reserve(kDefaultSampleReserve);
                if (pool_.size() < kMaxPoolSlots) {
                    pool_.push_back(targetSlot);
                }
            }

            targetSlot->Clear();
            std::forward<PopulateFn>(populateFn)(*targetSlot);
            targetSlot->FinalizeSortAndDedupe();
            targetSlot->SetMetadata(currentGameDay, ++nextSequence_);

            std::shared_ptr<const SpatialActorSnapshot> nextPublished(targetSlot);
            published_.store(nextPublished, std::memory_order_release);
            return nextPublished;
        }

        void Reset() noexcept;

        [[nodiscard]] std::size_t PoolSize() const noexcept;

    private:
        mutable std::mutex publishMutex_{};
        std::vector<std::shared_ptr<SpatialActorSnapshot>> pool_{};
        std::shared_ptr<const SpatialActorSnapshot> emptySnapshot_{};
        std::uint64_t nextSequence_{ 0 };
        std::atomic<std::shared_ptr<const SpatialActorSnapshot>> published_{};
    };

}  // namespace MARAS::State
