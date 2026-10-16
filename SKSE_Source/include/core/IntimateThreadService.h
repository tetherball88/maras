#pragma once

#include "PCH.h"
#include "core/ChildrenFleeService.h"

namespace MARAS {

    struct PotentialCheater {
        RE::FormID cheaterID{ 0 };
        std::vector<RE::FormID> cachedVictimIDs; // Committed partners who are absent and closed
    };

    struct OpenRelationshipWatcher {
        RE::FormID actorID{ 0 }; // Actor participating in the thread
        std::vector<RE::FormID> cachedPartnerIDs; // Committed open-relationship partners who are absent from scene
    };

    struct SexualPairRecord {
        RE::FormID actorA{ 0 };
        RE::FormID actorB{ 0 };

        bool operator==(const SexualPairRecord& other) const {
            return (actorA == other.actorA && actorB == other.actorB) ||
                   (actorA == other.actorB && actorB == other.actorA);
        }
    };

    struct CaughtEventKey {
        RE::FormID cheaterID{ 0 };
        RE::FormID victimID{ 0 };
        RE::FormID affairPartnerID{ 0 };

        bool operator==(const CaughtEventKey& other) const {
            return cheaterID == other.cheaterID && victimID == other.victimID && affairPartnerID == other.affairPartnerID;
        }
    };

    struct CaughtEventKeyHash {
        std::size_t operator()(const CaughtEventKey& k) const {
            std::size_t h1 = std::hash<uint32_t>{}(k.cheaterID);
            std::size_t h2 = std::hash<uint32_t>{}(k.victimID);
            std::size_t h3 = std::hash<uint32_t>{}(k.affairPartnerID);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct TrackedIntimateThread {
        std::string framework;
        int32_t threadID{ 0 };
        std::vector<RE::FormID> actorIDs;
        std::vector<PotentialCheater> potentialCheaters;
        std::vector<OpenRelationshipWatcher> openRelationshipWatchers;
        std::vector<SexualPairRecord> sexualPairs;
        std::unordered_set<CaughtEventKey, CaughtEventKeyHash> caughtEvents;
        std::unordered_set<CaughtEventKey, CaughtEventKeyHash> openRelationshipWitnessEvents;
        bool hasCheatingPotential{ false };
        bool hasOpenRelationshipPotential{ false };
    };

    struct ThreadKey {
        std::string framework;
        int32_t threadID{ 0 };

        bool operator==(const ThreadKey& other) const {
            return threadID == other.threadID && framework == other.framework;
        }
    };

    struct ThreadKeyHash {
        std::size_t operator()(const ThreadKey& k) const {
            std::size_t h1 = std::hash<std::string>{}(k.framework);
            std::size_t h2 = std::hash<int32_t>{}(k.threadID);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    class IntimateThreadService {
    public:
        static IntimateThreadService& GetSingleton();

        // Lifecycle management
        void Initialize();
        void Shutdown();
        void ClearAll(); // Called on kPreLoadGame and Revert

        // Papyrus-facing interface
        bool RegisterIntimateThread(const std::string& framework, int32_t threadID, const std::vector<RE::Actor*>& actors);
        void ReportIntimateInteraction(const std::string& framework, int32_t threadID, RE::Actor* actorA, RE::Actor* actorB);
        void ClearIntimateThread(const std::string& framework, int32_t threadID);

        // Polling loop logic (runs on game thread via SKSE TaskInterface)
        void PollActiveThreads();

    private:
        IntimateThreadService() = default;
        ~IntimateThreadService();

        IntimateThreadService(const IntimateThreadService&) = delete;
        IntimateThreadService& operator=(const IntimateThreadService&) = delete;

        struct WorkerSync {
            std::mutex mutex;
            std::condition_variable cv;
            std::atomic<bool> stopRequested{ false };
            bool hasMonitorableThreads{ false };
        };

        void StartWorkerIfNeeded();
        void StopWorker(bool joinThread);
        static void WorkerLoop(std::shared_ptr<WorkerSync> sync);
        bool HasActiveThreadsToMonitorLocked() const;
        void RefreshWorkerMonitorStateLocked();

        static void EvaluateParticipants(const std::vector<RE::FormID>& actorIDs,
                                         std::vector<PotentialCheater>& outCheaters,
                                         std::vector<OpenRelationshipWatcher>& outOpenWatchers,
                                         bool& outHasCheatingPotential,
                                         bool& outHasOpenRelationshipPotential);

        // Proximity and sight helper (game-thread only)
        bool CheckVictimExposure(RE::Actor* victimActor, RE::Actor* cheaterActor);

        // ModCallback event dispatches
        void SendCheatingCaughtEvent(RE::Actor* victimActor, RE::Actor* cheaterActor, RE::Actor* partnerActor,
                                     const std::string& framework, int32_t threadID, bool isWalkIn);
        void SendOpenRelationshipWalkInEvent(RE::Actor* witnessActor, RE::Actor* partnerActor, RE::Actor* sexPartnerActor,
                                             const std::string& framework, int32_t threadID, bool isWalkIn);

        mutable std::mutex mutex_;
        std::shared_ptr<WorkerSync> workerSync_;
        std::thread workerThread_;

        std::unordered_map<ThreadKey, TrackedIntimateThread, ThreadKeyHash> activeThreads_;

        // Feeds the participants' positions of all active threads (grouped by cell) to the
        // ChildrenFleeService. Called by it on the game thread.
        void CollectSceneThreats(ChildrenFleeService::ThreatMap& threats);

        // Detection thresholds
        static constexpr float kVisualDistance = 1500.0f;
        static constexpr float kCloseProximityDistance = 400.0f;
        static constexpr std::chrono::milliseconds kPollInterval{ 1500 };
    };

}  // namespace MARAS
