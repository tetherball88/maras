#pragma once

#include "maras/engine/EngineLogging.h"
#include "maras/state/WorldState.h"
#include "maras/state/WorldTables.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MARAS {

    // Reconciles cell public-area flags and bed reference ownership in Skyrim's engine
    // with WorldStore's shared cell & original bed-owner tracking.
    class BedOwnershipReconciler {
    public:
        static BedOwnershipReconciler& GetSingleton();

        // Looks up an NPC's home cell FormID via SpatialWorldIndex (0 if none).
        [[nodiscard]] RE::FormID GetNpcHomeCell(RE::FormID npcFormId) const;
        [[nodiscard]] static std::string GetCellName(RE::FormID cellFormId);

        // Captures original cell public flag if not yet recorded and ensures the cell is set public.
        void ApplyCellSharedPublicState(RE::FormID cellFormId, State::SharedCellRecord& data) const;

        // Restores public-area flags for cells that were originally private and are no longer shared.
        void RestoreUnsharedCells(
            const std::vector<std::pair<RE::FormID, State::SharedCellRecord>>& cellsToRestore) const;

        // Transfers owned beds of `npcFormId` to PlayerFaction (recording original owners in WorldStore).
        void ApplyShareFurniture(RE::FormID npcFormId);

        // Restores original bed ownership for `npcFormId` from WorldStore (or falls back to NPC base form).
        void RestoreFurniture(RE::FormID npcFormId);

        // Full engine+store workflows for sharing/unsharing an NPC's home cell & beds with the Player.
        bool ApplyShareHomeWithPlayer(RE::FormID npcFormId);
        bool StopShareHomeWithPlayer(RE::FormID npcFormId, std::string_view reason = "");
        bool ReconcileStoppedSharingFromMutation(
            RE::FormID npcFormId,
            const State::WorldMutationResult& mut,
            std::string_view reason = "");

        // Re-applies all shared cells' public flags and bed ownership after loading a save game.
        void ReapplyAllSharedCellsOnLoad();

        // Lazily indexes streamed beds and reconciles shared/unshared bed ownership when an interior cell loads.
        void OnCellFullyLoaded(RE::FormID cellFormId);

    private:
        BedOwnershipReconciler() = default;
        ~BedOwnershipReconciler() = default;
        BedOwnershipReconciler(const BedOwnershipReconciler&) = delete;
        BedOwnershipReconciler& operator=(const BedOwnershipReconciler&) = delete;
    };

}  // namespace MARAS
