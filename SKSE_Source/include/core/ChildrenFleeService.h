#pragma once

#include "PCH.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace MARAS {

    // Makes children run away (TTM_ChildrenFleeIntimateScene package, via the PackageOverrideService
    // hook) from cells that contain a "threat": intimate-scene participants (supplied by
    // IntimateThreadService through a provider) or a manual request (Papyrus / Prisma UI).
    //
    // A child flees from the cell it was in when it started and keeps fleeing for as long as a threat
    // remains in that cell, even if the flee package carries it out of the cell. A child that is still
    // next to the threat after kFleeTeleportDelay is teleported out of the way (it keeps fleeing).
    class ChildrenFleeService {
    public:
        // Danger points grouped by the FormID of the cell they are in.
        using ThreatMap = std::unordered_map<RE::FormID, std::vector<RE::NiPoint3>>;
        using ThreatProvider = std::function<void(ThreatMap&)>;

        static ChildrenFleeService& GetSingleton();

        void Initialize();
        void Shutdown();
        void Reset();  // Silently drops all flee state (session reset); sources must re-request

        // Called on the game thread by every refresh to collect additional threats (intimate scenes).
        void SetThreatProvider(ThreatProvider provider);

        // Manual control: children in the player's current cell flee until StopFleeing().
        void FleeFromPlayerCell();
        void StopFleeing();

        // Hot path (AI package-selection hook): cheap atomic check, then a shared-lock lookup.
        bool IsFleeing(RE::FormID actorID) const;

        // Sources call this when their threats changed; the refresh then runs on the game thread.
        void QueueRefresh();

        // Game-thread only: recompute which children flee and re-evaluate the ones that changed.
        void Refresh();

    private:
        ChildrenFleeService() = default;
        ~ChildrenFleeService();
        ChildrenFleeService(const ChildrenFleeService&) = delete;
        ChildrenFleeService& operator=(const ChildrenFleeService&) = delete;

        struct FleeState {
            RE::FormID fromCellID{ 0 };                   // cell with the threat the child flees from
            std::chrono::steady_clock::time_point since;  // when it started fleeing
        };

        // Load doors of a cell and the door reference each one leads to. Load doors are static, so
        // each cell is scanned once and the result is kept until Reset(). Destinations are stored as
        // FormIDs and resolved when used.
        struct ExitDoor {
            RE::NiPoint3 pos;
            RE::FormID destinationID{ 0 };
        };

        struct Worker {
            std::mutex mutex;
            std::condition_variable cv;
            std::atomic<bool> stopRequested{ false };
        };

        void StartWorker();
        void StopWorker();
        static void WorkerLoop(std::shared_ptr<Worker> worker);

        ThreatMap CollectThreats();
        RE::TESObjectREFR* FindExitDestination(RE::TESObjectCELL* cell, const RE::NiPoint3& from);
        bool TeleportChildAwayFromThreat(RE::Actor* child, const RE::NiPoint3& threatCenter);

        // Flee state: only the game thread writes it, the AI hook reads it.
        std::atomic<bool> fleeActive_{ false };
        mutable std::shared_mutex fleeMutex_;
        std::unordered_map<RE::FormID, FleeState> fleeingChildren_;

        // Game-thread only.
        std::unordered_map<RE::FormID, std::vector<ExitDoor>> exitDoorCache_;

        std::mutex sourceMutex_;
        ThreatProvider threatProvider_;
        ThreatMap manualThreats_;

        // The worker only posts refreshes while there is something to monitor.
        std::atomic<bool> monitoring_{ false };
        std::mutex workerMutex_;
        std::shared_ptr<Worker> worker_;

        static constexpr std::chrono::milliseconds kPollInterval{ 1500 };
        // A child still this close to a threat after this long is considered blocked.
        static constexpr std::chrono::seconds kFleeTeleportDelay{ 12 };
        static constexpr float kFleeBlockedDistance = 800.0f;
        // Outdoor teleport distance away from the threat.
        static constexpr float kFleeTeleportDistance = 2000.0f;
    };

}  // namespace MARAS
