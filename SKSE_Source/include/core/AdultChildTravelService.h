#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/Common.h"

namespace MARAS {

    struct TravelDestination {
        RE::FormID markerRefID{ 0 };
        RE::FormID locationID{ 0 };
        RE::FormID cellID{ 0 };
        std::string name;
        bool isFamilyVisit{ false };
    };

    struct AdultChildTravelState {
        RE::FormID targetMarkerID{ 0 };
        RE::FormID targetLocationID{ 0 };
        RE::FormID targetCellID{ 0 };
        std::string destinationName;
        float assignedGameDay{ -1.0f };
        float arrivedGameDay{ -1.0f };
        float stayDurationDays{ 3.0f };
        bool isFamilyVisit{ false };
    };

    /**
     * Manages independent travel & sandbox behavior for spawned Stage 4 (Adult) children.
     * Instead of sitting permanently in a player house as a tenant, adult children travel
     * between inns/taverns across Skyrim's cities and towns, occasionally visiting player
     * houses or their parents' homes, while remaining available for hire as followers.
     */
    class AdultChildTravelService {
    public:
        static AdultChildTravelService& GetSingleton();

        // Scans TESDataHandler::interiorCells at kDataLoaded to index all LocTypeInn markers
        void BuildDestinationIndex();

        // Assigns or refreshes travel destination and package override for a spawned Stage 4 adult child
        bool AssignOrUpdateDestination(RE::FormID childID, bool forceRotate = false);

        // Removes travel state and package override for a child
        void UnregisterAdultChild(RE::FormID childID);

        // Checks arrival at destination and rotates after 2-4 days of staying at the destination
        void Tick(float currentGameDay);
        void OnDayChanged(float currentGameDay);

        // Re-asserts travel destinations and package overrides for all spawned Stage 4 adult children on save load
        void ReassertAllOnLoad();

        // Returns current target marker FormID for PackageOverrideService rebuilds (0 if none)
        RE::FormID GetCurrentTargetMarkerID(RE::FormID childID) const;

        // Returns human-readable current destination label (e.g., "The Bannered Mare" or "Visiting Breezehome")
        std::string GetCurrentDestinationName(RE::FormID childID) const;

        // Clears runtime travel state on game revert
        void Revert();

        // Co-save serialization ('ACTR')
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization, std::uint32_t version);

        // Data dump
        nlohmann::json DumpJson() const;

    private:
        AdultChildTravelService() = default;
        ~AdultChildTravelService() = default;
        AdultChildTravelService(const AdultChildTravelService&) = delete;
        AdultChildTravelService& operator=(const AdultChildTravelService&) = delete;

        TravelDestination PickNextDestination(RE::FormID childID, RE::FormID currentMarkerID) const;
        std::vector<TravelDestination> BuildFamilyVisitDestinations(RE::FormID childID) const;

        mutable std::shared_mutex m_mutex;
        std::vector<TravelDestination> m_innDestinations;
        std::unordered_map<RE::FormID, AdultChildTravelState> m_activeDestinations;
    };

}  // namespace MARAS
