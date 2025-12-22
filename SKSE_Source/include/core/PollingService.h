#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_set>

namespace RE {
    using FormID = std::uint32_t;
}

namespace MARAS {

    class PollingService {
    public:
        using FormID = RE::FormID;
        using TimePoint = std::chrono::steady_clock::time_point;

        static PollingService& GetSingleton();

        // Initialize the service and start polling
        // Can be called multiple times (e.g., on game load) to reset state
        void Initialize();

        // Shutdown the service
        void Shutdown();

        // Update loop - call this periodically (e.g., from a game event hook)
        void Update();

        // Get current teammates from ProcessLists (exposed for external callers)
        std::unordered_set<FormID> GetCurrentTeammates();

        // Check if a specific actor is currently a player teammate
        bool IsPlayerTeammate(RE::Actor* actor);

    private:
        PollingService() = default;

        // Polling logic
        void CheckTeammateChanges();
        void CheckDayChanged();

        // Get current in-game day
        float GetCurrentGameDay();

        // Send events
        void SendTeammateChangeEvent(const std::unordered_set<FormID>& added,
                                     const std::unordered_set<FormID>& removed);
        void SendDayChangeEvent(float newDay);

        // Timing
        TimePoint lastTeammateCheck_;
        TimePoint lastDayCheck_;

        // State tracking
        std::unordered_set<FormID> previousTeammates_;
        float previousGameDay_ = -1.0f;

        // Intervals (in milliseconds)
        static constexpr std::chrono::milliseconds kTeammateCheckInterval{15000};  // 15 seconds
        static constexpr std::chrono::milliseconds kDayCheckInterval{60000};       // 60 seconds

        bool initialized_ = false;
    };

}  // namespace MARAS
