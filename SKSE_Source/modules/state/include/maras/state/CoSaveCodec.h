#pragma once

#include "maras/foundation/IRng.h"
#include "maras/state/CoSaveStream.h"
#include "maras/state/WorldState.h"

#include <cstdint>
#include <vector>

namespace MARAS::State {

    struct CoSaveLoadReport {
        bool allChunksSucceeded{ true };
        std::uint32_t loadedChunkCount{ 0 };
        std::uint32_t failedChunkCount{ 0 };
        std::uint32_t skippedChunkCount{ 0 };
        std::vector<std::uint32_t> failedChunkTypes{};
        std::vector<RefId> retiredSceneMarkers{};
    };

    /**
     * Serializes the entire normalized `WorldState` into the 12 canonical `'MARS'` v7 co-save chunks
     * (`NPCR`, `SPHR`, `AFCT`, `PHOU`, `SPAS`, `PLCH`, `CFSN`, `MKMK`, `DCSD`, `BNTR`, `ACTR`, `CNFG`).
     */
    bool SaveWorldState(const WorldState& state, ICoSaveWriter& writer);

    /**
     * Two-phase `'MARS'` v1–v7 co-save deserializer:
     *   - Phase 1: Atomically decodes each present chunk in any arrival order; a truncated or corrupt
     *              chunk rolls back only that chunk's staged state without corrupting sibling chunks.
     *   - Phase 2: Runs `FinalizeWorldStateAfterLoad` across all 10 tables once every chunk is loaded.
     */
    CoSaveLoadReport LoadWorldState(
        WorldState& state,
        ICoSaveReader& reader,
        const IFormIdResolver& resolver = IdentityFormIdResolver{},
        Foundation::IRng* optionalRng = nullptr);

    /**
     * Cross-table post-load normalization (idempotent, order-independent):
     *   - Prunes orphaned `0xFFxxxxxx` dynamic runtime actors (while preserving `0xFF700000..0xFF7FFFFF` synthetic children).
     *   - Reconciles deceased / dead-on-load actors across all active tables while preserving unloaded placed NPCs.
     *   - Synchronizes `HouseholdTable` child house assignments with `PlayerHouseRecord::tenantOrder`.
     *   - Repairs invalid `presumedFather` fields, synchronizes married couples' child custody, and fills `SpouseHierarchyTable` gaps.
     */
    WorldMutationResult FinalizeWorldStateAfterLoad(
        WorldState& state,
        const IFormIdResolver& resolver = IdentityFormIdResolver{},
        Foundation::IRng* optionalRng = nullptr);

}  // namespace MARAS::State
