#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "utils/Common.h"

namespace MARAS {

    // Tracks discovered data for a registered cell
    struct RegisteredCellData {
        // Minimal data to track a shared home
        bool sharedWithPlayer = false;        // whether this cell is currently shared with player
        std::set<RE::FormID> sharingSpouses;  // set of NPCs who have requested the cell be shared
        // Remember the original 'public' flag on the cell so it can be restored
        bool originalPublicRecorded = false;  // true if originalPublicState was captured
        bool originalPublicState = false;     // original value of the cell's public flag
    };

    class SpouseAssetsService {
    public:
        static SpouseAssetsService& GetSingleton();

        SpouseAssetsService();

        // Share the specified NPC's home with the player (looked up from HomeCellService)
        bool ShareHouseWithPlayer(RE::FormID npcFormID);

        // Stop sharing any houses owned by the npc with the player
        // (npc provided as FormID for callers that only have the id)
        bool StopShareHouseWithPlayer(RE::FormID npcFormID);

        // Query whether a cell is currently shared with the player
        bool IsHouseSharedWithPlayer(RE::FormID cellFormID) const;

        // Persistence
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

    private:
        // Helpers to transfer/restore furniture ownership (use HomeCellService data)
        void ShareFurniture(RE::FormID npcFormID);
        void StopShareFurniture(RE::FormID npcFormID);

        // Data
        std::map<RE::FormID, RegisteredCellData> sharedHomes_;  // cellFormID -> data
    };

}  // namespace MARAS
