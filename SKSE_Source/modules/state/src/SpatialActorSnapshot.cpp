#include "maras/state/SpatialActorSnapshot.h"

#include <algorithm>
#include <cstring>

namespace MARAS::State {

    namespace {

        [[nodiscard]] std::size_t TrimIncompleteUtf8Tail(const char* data, std::size_t len) noexcept {
            if (len == 0) {
                return 0;
            }

            const auto lastByte = static_cast<unsigned char>(data[len - 1]);
            if ((lastByte & 0x80u) == 0u) {
                return len;
            }

            // Walk backward across up to 3 trailing continuation bytes (10xxxxxx) to inspect the lead byte.
            std::size_t back = 0;
            while (back < 3 && (len - 1 - back) > 0 &&
                   (static_cast<unsigned char>(data[len - 1 - back]) & 0xC0u) == 0x80u) {
                ++back;
            }

            const std::size_t leadIdx = len - 1 - back;
            const auto lead = static_cast<unsigned char>(data[leadIdx]);

            std::size_t expectedSeqLen = 0;
            if ((lead & 0x80u) == 0u) {
                // Previous byte was ASCII, meaning everything after leadIdx is orphaned continuation bytes.
                return leadIdx + 1;
            } else if ((lead & 0xE0u) == 0xC0u) {
                expectedSeqLen = 2;
            } else if ((lead & 0xF0u) == 0xE0u) {
                expectedSeqLen = 3;
            } else if ((lead & 0xF8u) == 0xF0u) {
                expectedSeqLen = 4;
            } else {
                // Invalid leading/continuation byte at boundary; drop from leadIdx onward.
                return leadIdx;
            }

            const std::size_t actualSeqLen = len - leadIdx;
            if (actualSeqLen < expectedSeqLen) {
                return leadIdx;
            }
            if (actualSeqLen > expectedSeqLen) {
                return leadIdx + expectedSeqLen;
            }
            return len;
        }

    }  // namespace

    void LoadedActorSample::SetDisplayName(std::string_view raw) noexcept {
        displayName.fill('\0');
        displayNameLen = 0;

        if (raw.empty()) {
            return;
        }

        std::size_t copyLen = std::min<std::size_t>(raw.size(), kMaxDisplayNameBytes);
        if (raw.size() > kMaxDisplayNameBytes) {
            copyLen = TrimIncompleteUtf8Tail(raw.data(), copyLen);
        }

        if (copyLen > 0) {
            std::memcpy(displayName.data(), raw.data(), copyLen);
        }
        displayName[copyLen] = '\0';
        displayNameLen = static_cast<std::uint8_t>(copyLen);
    }

    void SpatialActorSnapshot::Clear() noexcept {
        currentGameDay_ = -1.0f;
        sequence_ = 0;
        samples_.clear();
    }

    void SpatialActorSnapshot::Reserve(std::size_t capacity) {
        samples_.reserve(capacity);
    }

    void SpatialActorSnapshot::SetMetadata(float currentGameDay, std::uint64_t sequence) noexcept {
        currentGameDay_ = currentGameDay;
        sequence_ = sequence;
    }

    void SpatialActorSnapshot::AddSample(const LoadedActorSample& sample) {
        if (!sample.refId.IsNull()) {
            samples_.push_back(sample);
        }
    }

    void SpatialActorSnapshot::FinalizeSortAndDedupe() noexcept {
        std::sort(samples_.begin(), samples_.end(), [](const LoadedActorSample& a, const LoadedActorSample& b) {
            return a.refId < b.refId;
        });
        auto uniqueEnd = std::unique(samples_.begin(), samples_.end(), [](const LoadedActorSample& a, const LoadedActorSample& b) {
            return a.refId == b.refId;
        });
        samples_.erase(uniqueEnd, samples_.end());
    }

    const LoadedActorSample* SpatialActorSnapshot::Find(RefId refId) const noexcept {
        if (refId.IsNull() || samples_.empty()) {
            return nullptr;
        }
        const auto it = std::lower_bound(
            samples_.begin(),
            samples_.end(),
            refId,
            [](const LoadedActorSample& sample, RefId key) {
                return sample.refId < key;
            });
        if (it != samples_.end() && it->refId == refId) {
            return &(*it);
        }
        return nullptr;
    }

    std::optional<float> SpatialActorSnapshot::GetDistanceSquared(RefId a, RefId b) const noexcept {
        if (a.IsNull() || b.IsNull()) {
            return std::nullopt;
        }
        const LoadedActorSample* sa = Find(a);
        const LoadedActorSample* sb = Find(b);
        if (!sa || !sb) {
            return std::nullopt;
        }
        return sa->DistanceSquaredTo(*sb);
    }

    bool SpatialActorSnapshot::IsWithinRadius(RefId a, RefId b, float radius) const noexcept {
        if (radius < 0.0f || a.IsNull() || b.IsNull()) {
            return false;
        }
        const LoadedActorSample* sa = Find(a);
        const LoadedActorSample* sb = Find(b);
        if (!sa || !sb) {
            return false;
        }
        if (sa->IsDeleted() || sa->IsDisabled() || sb->IsDeleted() || sb->IsDisabled()) {
            return false;
        }
        return sa->DistanceSquaredTo(*sb) <= (radius * radius);
    }

    std::vector<RefId> SpatialActorSnapshot::GetNearbyCandidates(RefId centerId, float radius) const {
        std::vector<RefId> out{};
        ForEachNearbyCandidate(centerId, radius, [&](const LoadedActorSample& sample) {
            out.push_back(sample.refId);
        });
        return out;
    }

    std::vector<RefId> SpatialActorSnapshot::GetPlayerTeammates(
        const ActorProfileTable* optionalRegisteredFilter) const {
        std::vector<RefId> out{};
        for (const LoadedActorSample& sample : samples_) {
            if (sample.IsPlayer() || !sample.Is3DLoaded() || sample.IsDeleted() || !sample.IsPlayerTeammate()) {
                continue;
            }
            const bool isRegistered = optionalRegisteredFilter != nullptr
                                          ? optionalRegisteredFilter->IsRegisteredAdult(sample.refId)
                                          : sample.IsRegisteredAdult();
            if (isRegistered) {
                out.push_back(sample.refId);
            }
        }
        return out;
    }

    SpatialSnapshotStore::SpatialSnapshotStore() {
        pool_.reserve(kInitialPoolSlots);
        for (std::size_t i = 0; i < kInitialPoolSlots; ++i) {
            auto slot = std::make_shared<SpatialActorSnapshot>();
            slot->Reserve(kDefaultSampleReserve);
            pool_.push_back(std::move(slot));
        }
        emptySnapshot_ = std::make_shared<const SpatialActorSnapshot>();
        published_.store(emptySnapshot_, std::memory_order_release);
    }

    SpatialSnapshotStore& SpatialSnapshotStore::GetSingleton() {
        static SpatialSnapshotStore instance{};
        return instance;
    }

    std::shared_ptr<const SpatialActorSnapshot> SpatialSnapshotStore::Snapshot() const noexcept {
        return published_.load(std::memory_order_acquire);
    }

    void SpatialSnapshotStore::Reset() noexcept {
        std::lock_guard<std::mutex> lock(publishMutex_);
        published_.store(emptySnapshot_, std::memory_order_release);
    }

    std::size_t SpatialSnapshotStore::PoolSize() const noexcept {
        std::lock_guard<std::mutex> lock(publishMutex_);
        return pool_.size();
    }

}  // namespace MARAS::State
