#pragma once

#include "utils/Common.h"

namespace MARAS {

    class SpouseHierarchyManager {
    public:
        static SpouseHierarchyManager& GetSingleton();

        // Get the hierarchy rank for an NPC. Returns 0..2 for top spots, 4 for 4+
        int GetRank(RE::FormID npcFormID) const;

        // Get the spouse FormID at a specific rank (0..2), or 0 if empty/invalid
        RE::FormID GetSpouseByRank(int rank) const;

        // Set an NPC to a specific rank. rank: 0..2 = promote to that spot, -1 or >=4 = remove from top spots.
        // Returns true on success.
        bool SetRank(RE::FormID npcFormID, int rank);

        // Called when a spouse is added (married) to attempt to fill empty slots automatically
        void OnSpouseAdded(RE::FormID npcFormID);

        // Called when a spouse is removed (divorce/death/unregister) to clear and fill gaps
        void OnSpouseRemoved(RE::FormID npcFormID);

        // Helper to fill gaps using the global married list
        void FillGaps(RE::FormID excludeFormID = 0, bool sendEvents = true);

        // Re-applies faction ranks & globals from persisted hierarchy slots after save load
        void SyncAllFactionsAndGlobals();

        // Data dump
        nlohmann::json DumpJson() const;

    private:
        SpouseHierarchyManager();
    };

}  // namespace MARAS
