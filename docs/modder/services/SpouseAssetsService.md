# SpouseAssetsService — Feature & Mechanics Guide

**Implementation**: [SpouseAssetsService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp)

`SpouseAssetsService` manages the legal access rights, cell trespassing flags, and furniture ownership when the player marries an NPC who owns their own residential interior in Skyrim. It allows the player to freely enter, stay overnight, and sleep in their spouse's original home without triggering trespassing crimes or "owned bed" blocks, and cleanly revokes those privileges upon divorce or spouse death.

---

## 1. Sharing a Spouse's Home with the Player

When an NPC shares their original home with the player (typically upon marriage):

1. **Home Cell Lookup**:
   - Queries [HomeCellService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp) to find the spouse's indexed residential interior cell (`TESObjectCELL`). If the NPC does not have an indexed home cell, the operation safely no-ops.
2. **Original Trespassing State Recording & Public Access**:
   - Before modifying the cell's access rules, the service inspects the cell's `kPublicArea` flag and records whether the cell was originally public or private.
   - If the cell was originally **private** (meaning the player would be accused of trespassing after hours), the service sets the cell to **public** (`SetPublic(true)`) so the player can live there freely at any hour.
3. **Multi-Spouse Reference Counting per Cell**:
   - Multiple spouses can originate from the same residential cell (for example, if the player marries two members of the same household).
   - The service maintains a set of `sharingSpouses` per cell so that divorcing one spouse does not lock the player out of a home still shared by another active spouse in that same cell.
4. **Bed Ownership Transfer**:
   - Queries `HomeCellService` for all bed furniture references originally owned by the spouse's base actor form (`TESNPC`) and reassigns their `ExtraOwnership` to `PlayerFaction`. This allows both the player and the spouse to sleep in those beds legally.

---

## 2. Revoking Home Access (Divorce or Death)

When a spouse stops sharing their home with the player (such as after a divorce):

1. **Bed Ownership Restoration**:
   - Reassigns all beds originally belonging to that spouse back from `PlayerFaction` to the spouse's original `TESNPC` base form, preventing the player from sleeping in their ex-spouse's bed.
2. **Multi-Spouse Check & Trespassing Flag Restoration**:
   - Removes the ex-spouse from the cell's `sharingSpouses` set.
   - **Only when the last sharing spouse in that cell is removed** does the service check the cell's recorded original state: if the cell was originally private before marriage, it restores `SetPublic(false)` so normal trespassing rules apply again, and removes the cell from the shared-homes registry.
3. **Unloaded/Fallback Cleanup**:
   - If the spouse's actor reference or home cell lookup is temporarily unavailable, the service scans all registered shared-home entries to ensure the spouse is removed and cell flags are restored if no sharing spouses remain.

---

## 3. Co-Save Persistence & Post-Mortem Cleanup on Load

- Persists all shared home cells, their recorded original `kPublicArea` flags, and the set of spouses sharing each cell in the SKSE co-save.
- **Dead Spouse Cleanup on Load**:
  - When loading a save, the service validates each sharing spouse in every shared home cell. Any spouse who is now dead or unresolved is automatically pruned from the cell's `sharingSpouses` set.
  - If all sharing spouses for a cell have died, the service automatically restores the cell's original private/trespassing state (`SetPublic(false)`) during load and discards the entry.

