#pragma once

#include "maras/foundation/DomainEnums.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_set>

namespace MARAS::Bridge::SkyrimNet {

    struct ChildProfileContextData;

    /**
     * Service generating and updating SkyrimNet character profiles for placed
     * child actors (Stage 2 Child and Stage 4 Adult). Deduplicates per
     * (actorFormID, ChildStage) within a save session and executes HTTP
     * template rendering on a bounded std::jthread worker cleanly joined on teardown.
     */
    class SkyrimNetProfileService {
    public:
        static SkyrimNetProfileService& GetSingleton();

        class SuppressScope {
        public:
            SuppressScope() { PushSuppress(); }
            ~SuppressScope() { PopSuppress(); }
            SuppressScope(const SuppressScope&) = delete;
            SuppressScope& operator=(const SuppressScope&) = delete;
        };

        static void PushSuppress() noexcept { ++suppressDepth_; }
        static void PopSuppress() noexcept {
            int expected = suppressDepth_.load(std::memory_order_relaxed);
            while (expected > 0 &&
                   !suppressDepth_.compare_exchange_weak(expected, expected - 1, std::memory_order_relaxed)) {
            }
        }
        [[nodiscard]] static bool IsSuppressed() noexcept { return suppressDepth_.load() > 0; }

        void Revert();

        void MarkActorStageUpdated(std::uint32_t childID, MARAS::Foundation::ChildStage stage);
        [[nodiscard]] bool HasUpdatedActorStage(std::uint32_t childID, MARAS::Foundation::ChildStage stage) const;

        void RequestChildProfileUpdate(std::uint32_t childID, MARAS::Foundation::ChildStage stage);

        [[nodiscard]] std::string GetActorBioTemplateName(std::uint32_t actorFormID) const;

        bool RequestActorProfileUpdate(std::uint32_t actorFormID,
                                       const std::string& additionalInstructions = "",
                                       const std::string& updatableBlocksCSV = "");

    private:
        struct WorkerQueueState {
            std::mutex queueMutex;
            std::deque<std::unique_ptr<ChildProfileContextData>> pendingQueue;
            bool workerActive{ false };
            std::atomic<bool> shuttingDown{ false };
        };

        SkyrimNetProfileService();
        ~SkyrimNetProfileService();
        SkyrimNetProfileService(const SkyrimNetProfileService&) = delete;
        SkyrimNetProfileService& operator=(const SkyrimNetProfileService&) = delete;

        void EnqueueProfileTask(std::unique_ptr<ChildProfileContextData> taskData);
        static void WorkerLoop(std::shared_ptr<WorkerQueueState> queueState, std::stop_token stopToken);

        static constexpr std::uint64_t MakeStageKey(std::uint32_t childID,
                                                    MARAS::Foundation::ChildStage stage) noexcept {
            return (static_cast<std::uint64_t>(childID) << 8) | static_cast<std::uint64_t>(static_cast<std::uint8_t>(stage));
        }

        static inline std::atomic<int> suppressDepth_{ 0 };
        mutable std::mutex mutex_;
        std::unordered_set<std::uint64_t> updatedActorStages_;

        std::shared_ptr<WorkerQueueState> queueState_;
        std::jthread workerThread_;
    };

}  // namespace MARAS::Bridge::SkyrimNet
