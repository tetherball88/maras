# `HouseholdUseCases`, Player Houses & Shared Spouse Assets — Architecture & Behavioral Guide

**Layers**: App (`SKSE_Source/modules/app`), Domain Household (`SKSE_Source/modules/domain_household`), Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [HouseholdUseCases.h](../../../SKSE_Source/modules/app/include/maras/app/HouseholdUseCases.h) / [HouseholdUseCases.cpp](../../../SKSE_Source/modules/app/src/HouseholdUseCases.cpp)
- [HouseholdRules.h](../../../SKSE_Source/modules/domain_household/include/maras/domain/household/HouseholdRules.h) / [HouseholdRules.cpp](../../../SKSE_Source/modules/domain_household/src/HouseholdRules.cpp)
- [SpouseAssetRules.h](../../../SKSE_Source/modules/domain_household/include/maras/domain/household/SpouseAssetRules.h) / [SpouseAssetRules.cpp](../../../SKSE_Source/modules/domain_household/src/SpouseAssetRules.cpp)
- [HouseholdEngineOps.h](../../../SKSE_Source/modules/engine/include/maras/engine/HouseholdEngineOps.h) / [HouseholdEngineOps.cpp](../../../SKSE_Source/modules/engine/src/HouseholdEngineOps.cpp)
- [ChildForcegreetRules.h](../../../SKSE_Source/modules/domain_household/include/maras/domain/household/ChildForcegreetRules.h) / [ChildInteractionCoordinator.h](../../../SKSE_Source/modules/app/include/maras/app/ChildInteractionCoordinator.h)
- [BedOwnershipReconciler.h](../../../SKSE_Source/modules/engine/include/maras/engine/BedOwnershipReconciler.h) / [BedOwnershipReconciler.cpp](../../../SKSE_Source/modules/engine/src/BedOwnershipReconciler.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`HouseholdUseCases` coordinates player houses, spouse/child tenant assignments, family relocation, shared spouse residences/beds/keys (`'SPAS'`), and adult-child travel schedules (see [`TravelRules`](TravelRules.md)). Pure residency and sharing mutations execute inside `HouseholdRules` and `SpouseAssetRules` on [`WorldStore`](WorldStoreAndCoSave.md), while Skyrim marker, linked-ref, bed-ownership, and cell public-area changes execute through `HouseholdEngineOps` and `BedOwnershipReconciler`. Companion coordinator `ChildInteractionCoordinator` pairs with `ChildForcegreetRules` to handle player household welcome-home forcegreet planning, pet race suitability checks, `'PLC1'` transient critters, allowance/`PoorCounter` tracking, tiered child gifts, and bounds-safe child chest refills.

---

## 2. Houses, Tenants & Family Relocation (`HouseholdRules` + `HouseholdEngineOps`)

- **Player House Registry (`'PHOU'`)**: Tracks registered house `HouseId`s (`BGSLocation` / home forms), anchor marker `RefId`s, `initialMoveDone` flags, ordered tenant rosters (`tenantOrder`), and per-tenant `stayAtHome` flags.
- **Tenant Eligibility (`CanRemoveTenant`)**: Adult tenants may always be evicted; child tenants can only be removed directly when `!guardians.HasGuardian(Player)`, keeping Player-parented children tethered to custodial unit relocation (`ApplyRelocateFamilyUnit`).
- **Home Marker Lifecycle (`HouseholdEngineOps`)**:
  - Resolves or creates runtime home sandbox markers (`GetOrCreateHomeMarker`), sets `homeSandboxKeyword` linked refs (`SetLinkedRefForHomeMarker`) / removes stale linked refs (`ClearHomeMarkerLinkedRef`), and preserves strict read-only CQS on house queries (`GetHouseMarkerFormID`, `GetHouseMarkerRef`, `FindHouseByName`).
  - Coordinates with [`PackageReconciler`](PackageReconciler.md) so housed tenants receive `HousedTenant` sandbox packages linked to the house marker.

---

## 3. Shared Spouse Cells, Beds & Keys (`SpouseAssetRules` + `BedOwnershipReconciler`)

When the Player marries an NPC with an indexed original residence (detected by [`HomeCellDetector`](HomeCellDetector.md)):
- **Multi-Spouse Reference Counting (`SharedCellRecord`)**: `ApplyShareCellsWithPlayer` inserts the spouse into `sharingSpouses` for each residential interior cell. `ApplyStopSharingForSpouse` removes the spouse upon divorce, death, or eviction, restoring private status **only** when zero sharing spouses remain in that cell (`!IsCellSharedWithPlayer`).
- **Safe Public-Area Toggling, Factions & Notifications (`BedOwnershipReconciler` + `FactionReconciler`)**: When a spouse has no indexed home cell, `ApplyShareHomeWithPlayer` assigns `NoInitialHouseFaction` (`0x7F`) and queues `$TTM_MSG_SpouseNoHome`. For indexed homes, `ApplyCellSharedPublicState` records `originalPublicRecorded` and `originalPublicState` before setting `cell->SetPublic(true)` so the Player is never flagged for trespassing, `FactionReconciler` syncs `SharedHouseFaction` (`0x7B`), and `BedOwnershipReconciler` emits `$TTM_MSG_SpouseSharedHome` (and reason-specific `$TTM_MSG_SpouseSharedHomeStopped*` notifications for `"divorce"`, `"deceased"`, `"affection"`, or generic stops when `StopShareHouseWithPlayer` runs). When `RestoreUnsharedCells` runs on dissolution/eviction, cells that were originally private revert cleanly via `cell->SetPublic(false)`.
- **Bed Ownership & Door Keys (`HouseholdEngineOps::UnlockAndGrantSpouseHomeAccess`)**: `ApplyShareFurniture` grants Player bed ownership across beds indexed in `SpatialWorldIndex::GetNpcBeds()`, `UnlockAndGrantSpouseHomeAccess` unlocks non-keyed lockable cell doors or grants missing door keys to the Player, and post-load reconciliation (`FinalizeLoad` $\rightarrow$ `ReconcileTenantsOnLoad` + `ReapplyAllSharedCellsOnLoad` + `ReassertAllTravelOnLoad`) reasserts transient engine cell/bed/package states across save loads.

---

## 4. Strict CQS Diagnostic Dumps (`DumpHousesJson`, `DumpSharedAssetsJson`)

Both `DumpHousesJson()` and `DumpSharedAssetsJson()` are `const` snapshot readers reading only `WorldStore::Snapshot()` and resolving read-only display names (`GetHouseName`, `BedOwnershipReconciler::GetCellName`), consumed by `UIDataBridge::ProcessDumpData`.
