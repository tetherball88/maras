#include "maras/state/WorldStore.h"

namespace MARAS::State {

    WorldStore::WorldStore() {
        publishedSnapshot_.store(
            std::make_shared<const WorldStateSnapshot>(liveState_.Snapshot()),
            std::memory_order_release);
    }

    WorldStore& WorldStore::GetSingleton() {
        static WorldStore instance{};
        return instance;
    }

    std::shared_ptr<const WorldStateSnapshot> WorldStore::Snapshot() const noexcept {
        return publishedSnapshot_.load(std::memory_order_acquire);
    }

    void WorldStore::SetIniBaselineSettings(const SettingsData& iniBaseline) {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        iniBaselineSettings_ = iniBaseline;
        liveState_.settings.Reset(iniBaselineSettings_);
        PublishSnapshotLocked();
    }

    SettingsData WorldStore::GetIniBaselineSettings() const {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        return iniBaselineSettings_;
    }

    void WorldStore::ReplaceState(WorldState&& nextState) {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        liveState_ = std::move(nextState);
        PublishSnapshotLocked();
    }

    void WorldStore::Revert() {
        std::lock_guard<std::recursive_mutex> lock(writeMutex_);
        liveState_.Revert();
        liveState_.settings.Reset(iniBaselineSettings_);
        PublishSnapshotLocked();
    }

    void WorldStore::PublishSnapshotLocked() noexcept {
        publishedSnapshot_.store(
            std::make_shared<const WorldStateSnapshot>(liveState_.Snapshot()),
            std::memory_order_release);
    }

}  // namespace MARAS::State
