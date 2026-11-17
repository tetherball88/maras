#pragma once

#include "maras/engine/EngineLogging.h"
#include "maras/state/WorldTables.h"

namespace MARAS {

    // Skyrim engine operations for Player's adopted/household children:
    // home markers, linked refs, custody faction snapshots, Variable06/07 package state, and load reassertion.
    class PlayerChildrenEngineOps {
    public:
        static PlayerChildrenEngineOps& GetSingleton();

        // Ensures the child has a persistent home sandbox marker ref (updates WorldStore if newly placed).
        RE::TESObjectREFR* GetOrCreateChildHomeMarker(RE::Actor* childActor);
        bool SetChildHomeMarkerLinkedRef(RE::FormID childId, RE::FormID markerFormId) const;

        // Moves the child's home marker to the house's marker and optionally moves the child actor there.
        bool MoveChildAndMarkerToHouse(RE::FormID childId, RE::FormID houseId, bool moveChild) const;

        // Custody faction snapshotting & restoration (around non-household external factions).
        void SnapshotChildFactions(RE::FormID childId);
        void ClearChildExternalFactions(RE::FormID childId);
        void RestoreChildFactions(
            RE::FormID childId,
            const State::ChildFactionSnapshotRecord* fallbackSnapshot = nullptr);

        // Mirrors Variable06 (order) and Variable07 (forcegreetPending) onto the loaded child actor & re-evaluates AI.
        void ApplyOrderAndForcegreetOnActor(RE::FormID childId, float currentOrder, bool forcegreetPending) const;
        void ApplyOrderOnActorFromWorld(RE::FormID childId) const;
        void ResetForceGreetingOnActor(RE::FormID childId) const;

        // Re-applies Variable06/07, factions, and package overrides for all household children on save load.
        void ReassertChildPackagesOnLoad();

        // Clears BYOHRelationshipAdoptableFaction for dynamic 0xFFxxxxxx child refs (dispatches to main thread if needed).
        void ClearAdoptableFactionOnRemove(RE::FormID childId) const;

        // Dispatches mod event notifying scripts when child chest state/intro changes.
        void SendChildChestEvent(RE::FormID childOrHouseId, bool introduced) const;

    private:
        PlayerChildrenEngineOps() = default;
        ~PlayerChildrenEngineOps() = default;
        PlayerChildrenEngineOps(const PlayerChildrenEngineOps&) = delete;
        PlayerChildrenEngineOps& operator=(const PlayerChildrenEngineOps&) = delete;
    };

}  // namespace MARAS
