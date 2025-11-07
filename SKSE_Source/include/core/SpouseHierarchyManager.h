#pragma once

#include "PCH.h"
#include "utils/Common.h"

namespace MARAS {

    class SpouseHierarchyManager {
    public:
        static SpouseHierarchyManager& GetSingleton();

        // Get the hierarchy rank for an NPC. Returns 0..2 for top spots, 4 for 4+
        int GetRank(RE::FormID npcFormID) const;

        // Set an NPC to a specific rank. rank: 0..2 = promote to that spot, -1 or >=4 = remove from top spots.
        // Returns true on success.
        bool SetRank(RE::FormID npcFormID, int rank);

        // Called when a spouse is added (married) to attempt to fill empty slots automatically
        void OnSpouseAdded(RE::FormID npcFormID);

        // Called when a spouse is removed (divorce/death/unregister) to clear and fill gaps
        void OnSpouseRemoved(RE::FormID npcFormID);

        // Serialization
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

    private:
        SpouseHierarchyManager();

        // Top three ranks; 0 means empty
        std::array<RE::FormID, 3> ranks_;

        // Helper to fill gaps using the global married list
        void FillGaps();
    };

}  // namespace MARAS
