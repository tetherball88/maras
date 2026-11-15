# `PollingService` — Architecture & Behavioral Guide

**Layer**: Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [PollingService.h](../../../SKSE_Source/modules/engine/include/maras/engine/PollingService.h)
- [PollingService.cpp](../../../SKSE_Source/modules/engine/src/PollingService.cpp)
- [plugin.cpp](../../../SKSE_Source/plugin/plugin.cpp)
- [AppLifecycleCoordinator.cpp](../../../SKSE_Source/modules/app/src/AppLifecycleCoordinator.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`PollingService` provides M.A.R.A.S.'s `2 Hz` (`500 ms`) main-thread heartbeat during active gameplay (paused while UI menus or main menu are open). It captures zero-allocation spatial snapshots of loaded actors, tracks player teammate transitions, detects in-game day changes and player level-ups, and dispatches periodic ticks into engine and Layer-4 application coordinators without requiring active Papyrus polling scripts.

---

## 2. Scheduling & Frame Synchronization

`plugin.cpp` registers `UpdateEventSink` for `RE::MenuOpenCloseEvent` and `RE::InputEvent*` events:

- Whenever an input event arrives (throttled to `500 ms` intervals) or a menu closes, `UpdateEventSink` atomically swaps `pollTaskQueued_` and enqueues `PollingService::Update()` onto the SKSE main-thread task queue tagged with the current `SessionEpoch`.
- If the game transitions across a save load/revert (`kPreLoadGame` bumps `SessionEpoch` and calls `PollingService::Shutdown()`), queued stale-epoch tasks return immediately without touching cleared state.
- `PollingService::Update()` skips execution when `initialized_` is false, the game is in Main Menu, or `RE::UI::GameIsPaused()` is true.

---

## 3. `2 Hz` Tick Execution & Snapshot Publication

When `500 ms` has elapsed since the previous tick:

1. **Lock-Free Spatial Snapshot Publication**: Collects loaded `ProcessLists` actors + the Player into a pooled `SpatialActorSnapshot` via `SpatialSnapshotStore::Publish()`, computing cached loaded flags (dead, deleted, disabled, child, ghost, in-combat, in-scene, teammate, werewolf, tracked faction) and `FormCache` checks without heap allocations under locks. See [`SpatialWorldIndex`](SpatialWorldIndex.md) for snapshot pool mechanics.
2. **Engine Sub-Ticks**:
   - [`ChildrenFleeService::Tick()`](ChildrenFleeService.md) (`1000 ms` throttled while threats are monitored).
   - [`IntimateThreadService::Tick()`](IntimateThreadService.md) (`1000 ms` throttled while active intimate threads require walk-in checks).
3. **Teammate Delta Detection (`CheckTeammatesChanged`)**:
   - Compares current teammates against `previousTeammates_`. When teammates join or leave:
     - Emits `maras_teammate_change`, `maras_teammate_added`, and `maras_teammate_removed` SKSE mod events.
     - Invokes [`BuffReconciler::ReconcilePlayerBuffs()`](BuffReconciler.md) so follower Skill Type spouse perks update immediately.
     - Reconciles factions ([`FactionReconciler`](FactionReconciler.md)) and packages ([`PackageReconciler`](PackageReconciler.md)) for added/removed actors.
4. **In-Game Calendar & Player Progression Checks**:
   - `CheckDayChanged()`: When `floor(currentDay)` advances, calls `EngineEventHooks::OnPollingDayChanged(currentDay)` (firing daily affection decay, lineage/paternity/travel daily ticks, matchmaking, partner agency, and banter evaluation) and emits `maras_day_changed`.
   - `CheckPlayerLevelChanged()`: When the Player levels up, invokes `OnPollingRefreshChildVisuals()` so graduated adult children scale cleanly to the new player level (see [`ChildGrowthAndVisuals`](ChildGrowthAndVisuals.md)).
5. **Continuous Coordinator & Marriage Ticks**:
   - Invokes `OnPollingUpdate(currentDay)` (SkyrimNet memory episode flush, fertility polling, expiring child orders, child training completion, adult child travel progress) and `OnPollingAutoMarriageTick(currentDay)` (automatic NPC-to-NPC marriage ceremonies after courtship).

---

## 4. Read-Only Teammate Queries (CQS)

`PollingService::GetCurrentTeammates()` strictly follows Command-Query Separation: it acquires the lock-free `SpatialSnapshotStore::Snapshot()` and `WorldStore::Snapshot()` and returns the current teammate `FormID` set without mutating any state or touching `RE::TESForm::LookupByID`. External consumers such as [`MARAS_GetCurrentTeammates`](../../../SKSE_Source/modules/bridge_c_api/include/MARAS_PublicAPI.h) and `HouseholdBuffsJsonBuilder` consume this read-only path directly.
