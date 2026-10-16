#pragma once

#include "utils/Common.h"

namespace MARAS {

    class SpouseHierarchyManager {
    public:
        static SpouseHierarchyManager& GetSingleton();

        // Get the hierarchy rank for an NPC. Returns 0..2 for top spots, 4 for 4+
        int GetRank(RE::FormID npcFormID) const;

        // Get the spouse FormID at a specific rank (0..2), or 0 if empty/invalid
        RE::FormID GetSpouseByRank(int rank) const {
            if (rank >= 0 && rank < static_cast<int>(ranks_.size())) {
                return ranks_[rank];
            }
            return 0;
        }

        const std::array<RE::FormID, 3>& GetRanks() const { return ranks_; }

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

        // Data dump
        nlohmann::json DumpJson() const;

    private:
        SpouseHierarchyManager();

        // Top three ranks; 0 means empty
        std::array<RE::FormID, 3> ranks_;

        // Helper to fill gaps using the global married list
        void FillGaps(RE::FormID excludeFormID = 0, bool sendEvents = true);
    };

}  // namespace MARAS
