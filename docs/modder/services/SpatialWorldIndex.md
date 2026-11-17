# SpatialWorldIndex

**Sources**: [SpatialWorldIndex.h](../../../SKSE_Source/modules/engine/include/maras/engine/SpatialWorldIndex.h) · [SpatialWorldIndex.cpp](../../../SKSE_Source/modules/engine/src/SpatialWorldIndex.cpp) · [SpatialActorSnapshot.h](../../../SKSE_Source/modules/state/include/maras/state/SpatialActorSnapshot.h) · [SpatialActorSnapshot.cpp](../../../SKSE_Source/modules/state/src/SpatialActorSnapshot.cpp) · [npc_index_overrides.ini](../../../SKSE/Plugins/MARAS/npc_index_overrides.ini)  
**Cross-References**: [HomeCellDetector.md](HomeCellDetector.md) · [WorldStoreAndCoSave.md](WorldStoreAndCoSave.md) · [PollingService.md](PollingService.md) · [ARCHITECTURE.md](../ARCHITECTURE.md)

## Overview

Spatial awareness in M.A.R.A.S is intentionally split into two complementary structures so static world catalogs never stall high-frequency gameplay polling:

1. **`SpatialWorldIndex` (`modules/engine`, `pch_engine.h`)**: Thread-safe world/NPC/home-cell/inn index built at `kDataLoaded` and updated on save loads (`std::shared_mutex m_mutex`).
2. **`SpatialSnapshotStore` & `SpatialActorSnapshot` (`modules/state`, `pch_core.h`)**: Lock-free, zero-churn immutable actor telemetry published at 2 Hz by `PollingService`.

---

## `SpatialWorldIndex` Static & Session Catalog

### Initialization & Lifecycle

| Event | Method | Behavior |
| :--- | :--- | :--- |
| `kDataLoaded` | `BuildIndex()` | Runs `BuildNpcIndex()`, `BuildHomeCellIndexLocked()` (see [HomeCellDetector.md](HomeCellDetector.md)), and `BuildInnDestinationIndexLocked()`. |
| `kPreLoadGame` | `ClearLoadedActors()` | Drops cached runtime `RE::Actor*` pointers before Skyrim destroys actors during load transitions. |
| `kPostLoadGame` / `kNewGame` | `RefreshIndex()` | Evicts invalid/dead/deleted loaded actor pointers and re-sweeps loaded actors via `ProcessUtils::ForEachLoadedActor` (or triggers `BuildNpcIndex()` if not yet built). |
| `TESInitScriptEvent` / `TESObjectLoadedEvent` (Actors) | `RegisterLoadedActors()` | Batch-registers newly loaded `ActorCharacter` forms under a single exclusive `m_mutex` lock (filtering out non-actor 3D objects in `ActorInitLoadedEventSink` before queuing, and avoiding string lowercasing allocations for already-indexed actors). |
| `TESCellFullyLoadedEvent` (Interior) | `RefreshHomeCellBeds()` | Lazily scans newly streamed non-persistent furniture beds inside known interior home cells on the main thread outside `m_mutex`, resolving direct `TESNPC`, house `TESFaction`, and `cell->GetOwner()` inheritance (excluding `kChildCanUse` beds) into deduplicated `m_bedsWithOwners` entries. |
| Save Revert | `Revert()` | Clears NPC entries, loaded actor pointers, home cells, bed owners, and inn destinations cleanly. |

### Base-to-Reference Resolution & INI Overrides

`BuildNpcIndex()` solves Skyrim's base-FormID vs. placed-reference mismatch in three passes:

1. **INI Filtering (`LoadOverridesLocked`)**: Parses [npc_index_overrides.ini](../../../SKSE/Plugins/MARAS/npc_index_overrides.ini) (`Data/SKSE/Plugins/MARAS/npc_index_overrides.ini`), supporting `[Include]`, `[Exclude]`, `[ExcludeNames]`, `[ExcludeEditorIDs]`, and `[ExcludePlugins]` with exact matches and `*`/`?` wildcards.
2. **Base Form Scan**: Iterates `TESNPC` records in `TESDataHandler`, validating humanoid `ActorTypeNPC` traits, display names, ghosts/chargen presets, unique flags, and INI exclusions. Records base FormIDs initially with `refFormID = baseID`.
3. **Placed Reference Upgrade**: Walks `TESWorldSpace::persistentCell` and `interiorCells` to upgrade unique (or `[Include]`-promoted non-unique) base NPCs to their persistent placed `ActorCharacter` / `Reference` `FormID`, skipping dynamic `0xFF` refs and leveled spawns. Any remaining unplaced base-only entries lacking a valid placed reference are pruned.

When `settings->logLevel <= 1`, `NPCIndexReportWriter::WriteReport` writes the promotions/exclusions diagnostic report **outside** `m_mutex` so disk I/O never blocks index readers.

### Inn Travel Destination Indexing

`BuildInnDestinationIndexLocked()` scans interior cells flagged with `LocTypeInn`, selecting each inn location's `LocationCenterMarker` reference (falling back to the first non-disabled, non-deleted reference in the cell) to build the candidate list consumed by [TravelRules.md](TravelRules.md).

### Lookup Discipline & `m_mutex` Invariant

- **Exact + Fuzzy Search**: `FindByName` checks lowercase exact matches first, then performs Levenshtein fuzzy matching (`kMaxFuzzyDist = 3`) over `m_allNames`.
- **Safe Engine Lookup Outside `m_mutex`**: Query entry points such as `GetActorFromFormId` and `FindActorByName` acquire `std::shared_lock lock(m_mutex)` strictly to copy `FormID` sets (`mappedRefID`, `loadedExactCandidates`, `fallbackRefID`), release `m_mutex`, and only then invoke `RE::TESForm::LookupByID` or fallback scans of `SpatialSnapshotStore::Snapshot()`. Never perform heavy engine lookups that can trigger engine locks while holding `m_mutex`.

---

## `SpatialSnapshotStore` & `SpatialActorSnapshot` (2 Hz Telemetry)

To eliminate heap allocations and prevent `WorldStore` copy-on-write churn during 2 Hz polling:

| Component | Design Detail |
| :--- | :--- |
| **`LoadedActorSample`** | Trivially copyable (`static_assert(std::is_trivially_copyable_v<LoadedActorSample>)`) per-actor sample storing `refId`, `baseId`, `parentCellId`, `currentLocationId`, `raceId`, 3D coordinates, `sex`, 16 `LoadedActorFlags` bits, and a fixed inline 64-byte (`kMaxDisplayNameBytes = 63`) UTF-8 `displayName` buffer. |
| **Sample Bitflags (`LoadedActorFlags`)** | `IsPlayer`, `HasCurrentProcess`, `Is3DLoaded`, `IsDead`, `IsDisabled`, `IsDeleted`, `IsChild`, `IsInCombat`, `IsInScene`, `IsMannequin`, `HasActorTypeNpc`, `IsPlayerTeammate`, `IsWerewolf`, `IsTrackedFaction`, `IsRegisteredAdult`, `IsGhost`. |
| **Immutable Sorted Vector** | `SpatialActorSnapshot` sorts samples ascending by `RefId` (`FinalizeSortAndDedupe`), enabling binary search (`Find`), squared-distance checks (`GetDistanceSquared`, `IsWithinRadius`), radius candidate scans (`ForEachNearbyCandidate`), and teammate queries (`GetPlayerTeammates`). |
| **Lock-Free Reader / Pooled Writer** | `SpatialSnapshotStore::Snapshot()` performs a lock-free `std::atomic<std::shared_ptr<const SpatialActorSnapshot>>::load(std::memory_order_acquire)`. `Publish()` runs under `publishMutex_`, scanning a pool (`kInitialPoolSlots = 4`, `kMaxPoolSlots = 16`, `kDefaultSampleReserve = 256`) for any buffer with `use_count() == 1`, issuing `std::atomic_thread_fence(std::memory_order_acquire)` to synchronize with the last reader before clearing and reusing it in-place. |
