#pragma once

#include "utils/Common.h"

namespace MARAS {

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

        // Post-load engine sync and runtime revert helpers
        void FinalizeLoad();
        void Revert();

        // Data dump
        nlohmann::json DumpJson() const;

    private:
        // Helpers to transfer/restore furniture ownership (use HomeCellService data)
        void ShareFurniture(RE::FormID npcFormID);
        void StopShareFurniture(RE::FormID npcFormID);
    };

}  // namespace MARAS
