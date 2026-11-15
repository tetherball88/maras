#pragma once

#include "maras/engine/ChildrenFleeService.h"
#include "maras/engine/EngineLogging.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MARAS {

    struct PotentialCheater {
        RE::FormID cheaterID{ 0 };
        std::vector<RE::FormID> cachedVictimIDs;  // Committed partners who are absent and closed
    };

    struct OpenRelationshipWatcher {
        RE::FormID actorID{ 0 };                  // Actor participating in the thread
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
            return cheaterID == other.cheaterID && victimID == other.victimID &&
                   affairPartnerID == other.affairPartnerID;
        }
    };

    struct CaughtEventKeyHash {
        std::size_t operator()(const CaughtEventKey& k) const {
            std::size_t h1 = std::hash<std::uint32_t>{}(k.cheaterID);
            std::size_t h2 = std::hash<std::uint32_t>{}(k.victimID);
            std::size_t h3 = std::hash<std::uint32_t>{}(k.affairPartnerID);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct TrackedIntimateThread {
        std::string framework;
        std::int32_t threadID{ 0 };
        std::vector<RE::FormID> actorIDs;
        std::vector<PotentialCheater> potentialCheaters;
        std::vector<OpenRelationshipWatcher> openRelationshipWatchers;
        std::vector<SexualPairRecord> sexualPairs;
        std::unordered_set<CaughtEventKey, CaughtEventKeyHash> caughtEvents;
        std::unordered_set<CaughtEventKey, CaughtEventKeyHash> openRelationshipWitnessEvents;
        std::vector<RE::FormID> playerIntimacyPartners;
        std::vector<RE::FormID> caughtMidActFactionActors;
        bool hadSex{ false };
        bool hasCheatingPotential{ false };
        bool hasOpenRelationshipPotential{ false };
    };

    struct ThreadKey {
        std::string framework;
        std::int32_t threadID{ 0 };

        bool operator==(const ThreadKey& other) const {
            return threadID == other.threadID && framework == other.framework;
        }
    };

    struct ThreadKeyHash {
        std::size_t operator()(const ThreadKey& k) const {
            std::size_t h1 = std::hash<std::string>{}(k.framework);
            std::size_t h2 = std::hash<std::int32_t>{}(k.threadID);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    class IntimateThreadService {
    public:
        static IntimateThreadService& GetSingleton();

        // Lifecycle management
        void Initialize();
        void Shutdown();
        void ClearAll();  // Called on kPreLoadGame and Revert

        // Papyrus-facing interface
        bool RegisterIntimateThread(const std::string& framework,
                                    std::int32_t threadID,
                                    const std::vector<RE::Actor*>& actors);
        void ReportIntimateInteraction(const std::string& framework,
                                       std::int32_t threadID,
                                       RE::Actor* actorA,
                                       RE::Actor* actorB);
        [[nodiscard]] bool IsIntimateThreadTracked(const std::string& framework, std::int32_t threadID) const;
        [[nodiscard]] bool HasIntimateThreadPlayerParticipants(const std::string& framework, std::int32_t threadID) const;
        void MarkIntimateThreadSexual(const std::string& framework, std::int32_t threadID);
        void ClearIntimateThread(const std::string& framework, std::int32_t threadID, bool hadSex = false);

        // Game-thread periodic tick driven by PollingService::Update()
        void Tick(std::chrono::steady_clock::time_point now);

        // Polling loop logic (runs on game thread via PollingService::Update())
        void PollActiveThreads();

    private:
        IntimateThreadService() = default;
        ~IntimateThreadService() = default;
        IntimateThreadService(const IntimateThreadService&) = delete;
        IntimateThreadService& operator=(const IntimateThreadService&) = delete;

        static void EvaluateParticipants(const std::vector<RE::FormID>& actorIDs,
                                         std::vector<PotentialCheater>& outCheaters,
                                         std::vector<OpenRelationshipWatcher>& outOpenWatchers,
                                         bool& outHasCheatingPotential,
                                         bool& outHasOpenRelationshipPotential);

        // Proximity and sight helper (game-thread only)
        bool CheckVictimExposure(RE::Actor* victimActor, RE::Actor* cheaterActor);

        // ModCallback event dispatches & engine hooks
        void SendCheatingCaughtEvent(RE::Actor* victimActor,
                                     RE::Actor* cheaterActor,
                                     RE::Actor* partnerActor,
                                     const std::string& framework,
                                     std::int32_t threadID,
                                     bool isWalkIn);
        void SendOpenRelationshipWalkInEvent(RE::Actor* witnessActor,
                                             RE::Actor* partnerActor,
                                             RE::Actor* sexPartnerActor,
                                             const std::string& framework,
                                             std::int32_t threadID,
                                             bool isWalkIn);

        mutable std::mutex mutex_;
        std::atomic<bool> hasActiveThreads_{ false };
        std::chrono::steady_clock::time_point lastPollTime_{};

        std::unordered_map<ThreadKey, TrackedIntimateThread, ThreadKeyHash> activeThreads_;

        // Feeds the participants' positions of all active threads (grouped by cell) to
        // ChildrenFleeService. Called by it on the game thread.
        void CollectSceneThreats(ChildrenFleeService::ThreatMap& threats);

        // Detection thresholds
        static constexpr float kVisualDistance = 1500.0f;
        static constexpr float kCloseProximityDistance = 400.0f;
        static constexpr std::chrono::milliseconds kPollInterval{ 1500 };
    };

}  // namespace MARAS
