# HomeCellService — Feature & Mechanics Guide

**Implementation**: [HomeCellService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp)

`HomeCellService` builds a fast, in-memory spatial index of every residential interior cell across the entire load order at startup/load time. It enables M.A.R.A.S to know where any NPC originally lives, which bed(s) they own, which interior marker represents the center of their home, and which doors belong to their residence—supporting both **Player-to-NPC home cohabitation** (moving into a spouse's house and receiving their house keys/bed access) and **NPC-to-NPC marriage cohabitation**.

---

## 1. Automated Interior Cell Scanning

When the game data finishes loading, `HomeCellService` performs a single-pass scan across all interior cells registered in `TESDataHandler`:

1. **Residential Location Filtering**:
   - Inspects each interior cell's `BGSLocation` and only indexes cells whose location carries the vanilla **Dwelling** (`LocTypeDwelling` / `0x000130DC`) or **House** (`LocTypeHouse` / `0x0001CB85`) keyword.
2. **Persistent Resident Requirement**:
   - Only retains interior cells that contain at least one **persistent NPC reference**, ignoring empty or generic uninhabited interiors.

---

## 2. Single-Pass Reference Indexing

Within each qualifying residential cell, the service inspects every object reference in a single pass and extracts four categories of household data:

### 1. NPC Original Home Cell Mapping
- Every persistent NPC reference found inside the cell is mapped to that cell's FormID as their **original home cell** (recording the first residential cell encountered for that actor).
- This allows M.A.R.A.S to determine whether two NPCs natively live in the same house or where an NPC should return after a divorce.

### 2. Center Marker Resolution (with Fallback)
- Searches the cell for an `XMarker` carrying the `LocationCenterMarker` (`0x0001BDF1`) `ExtraLocationRefType`.
- **Priority & Fallback**:
  - If an explicit `LocationCenterMarker` is found, it always takes priority as the cell's anchor point.
  - If a mod-added or vanilla house interior lacks an explicit `LocationCenterMarker`, the service automatically falls back to the first valid object reference in the cell so NPC home markers teleported into that cell always have a valid physical target.

### 3. Bed Ownership Registry
- Inspects every `TESFurniture` reference in the cell and checks its active furniture flags for `kCanSleep` (beds, bedrolls, double beds).
- Reads the bed reference's `ExtraOwnership` data and maps the **owner's Base NPC FormID (`TESNPC`)** to the list of bed references they own.
- Used when the player marries an NPC to grant the player legal sleep ownership over their spouse's beds.

### 4. Door & Lock Registry
- Inspects every `TESObjectDOOR` reference in the cell and checks its lock state (`GetLock()`) and linked key (`lock->key`).
- Records doors associated with the cell so `SpouseAssetsService` can grant the player copies of the spouse's house keys or unlock shared doors upon marriage.

