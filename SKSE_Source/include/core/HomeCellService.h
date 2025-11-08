#pragma once

#include <map>
#include <string>
#include <vector>

#include "utils/Common.h"

namespace MARAS {

    struct HomeCellData {
        std::vector<RE::FormID> doors;  // door reference FormIDs inside the cell
        RE::FormID centerMarker = 0;    // LocationCenterMarker reference FormID
    };

    struct ActorHomeData {
        RE::FormID homeCell = 0;  // cell formID where actor has a persistent ref
    };
    // ownedFurniture storage removed; we'll compute owned furniture on demand.

    // Service that scans interior cells after data load and builds lookup maps:
    // - homeCells: cellID -> HomeCellData
    // - actorsWithHome: actorID -> ActorHomeData
    // - bedsWithOwners: ownerID -> [bedRefID]
    class HomeCellService {
    public:
        static HomeCellService& GetSingleton();

        HomeCellService();

        // Build/refresh the index (call once after SKSE data is loaded)
        void BuildIndex();

        // Queries
        RE::FormID GetNpcHome(RE::Actor* npc) const;
        std::vector<RE::FormID> GetNpcBeds(RE::Actor* npc) const;
        std::vector<RE::FormID> GetCellDoors(RE::FormID cellFormID) const;
        RE::FormID GetNpcOriginalHouseCenterMarker(RE::Actor* npc) const;

        // Log stored index contents (cell name, location, doors, actors, beds)
        void LogIndex() const;

        void Revert();

    private:
        // Scans a single cell, fills outData with doors and records furniture/actors.
        // Returns true if the cell contains at least one persistent actor reference.
        bool ScanCell(RE::TESObjectCELL* cell, HomeCellData& outData);

        // Helper methods for ScanCell
        void ProcessBedOwnership(RE::TESObjectREFR& bedRef);
        void ProcessActorHome(RE::FormID actorFormId, RE::FormID cellId);

        // Helper methods for LogIndex
        std::string GetLocationInfo(const RE::TESObjectCELL* cell) const;
        std::string GetActorsInCell(RE::FormID cellId) const;
        std::string GetBedsInCell(RE::FormID cellId) const;

        std::map<RE::FormID, HomeCellData> homeCells_;                  // cellFormID -> data
        std::map<RE::FormID, ActorHomeData> actorsWithHome_;            // actorFormID -> data
        std::map<RE::FormID, std::vector<RE::FormID>> bedsWithOwners_;  // ownerActorFormID -> [bedRefFormID]
    };

}  // namespace MARAS
