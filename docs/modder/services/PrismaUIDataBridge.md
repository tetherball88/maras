# `PrismaUIDataBridge`, `PrismaUIManager` & `HouseholdBuffsJsonBuilder` — Architecture & Behavioral Guide

**Layer**: Bridge UI (`SKSE_Source/modules/bridge_ui`)  
**Sources**:
- [PrismaUIManager.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/PrismaUIManager.h) / [PrismaUIManager.cpp](../../../SKSE_Source/modules/bridge_ui/src/PrismaUIManager.cpp)
- [PrismaUIDataBridge.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/PrismaUIDataBridge.h) / [PrismaUIDataBridge.cpp](../../../SKSE_Source/modules/bridge_ui/src/PrismaUIDataBridge.cpp)
- [HouseholdBuffsJsonBuilder.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/HouseholdBuffsJsonBuilder.h) / [HouseholdBuffsJsonBuilder.cpp](../../../SKSE_Source/modules/bridge_ui/src/HouseholdBuffsJsonBuilder.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`PrismaUIManager`, `PrismaUIDataBridge`, and `HouseholdBuffsJsonBuilder` form the C++ $\leftrightarrow$ React (Ultralight / PrismaUI) transport layer for M.A.R.A.S.:

- **`PrismaUIManager`**: Owns the `PrismaUI` view (`MARAS/index.html`), hotkey/menu event sinks (`RE::InputEvent*`, `RE::MenuOpenCloseEvent`), visibility/focus reconciliation (`Focus` / `Unfocus`), game-pause synchronization, deferred new-game relationship scan deferral, and `RunAfterFocusReleased` callbacks.
- **`PrismaUIDataBridge`**: Implements `IUiBridgeGateway`, registers UI JS listeners (`onAction`), serializes read-only JSON snapshots through [`PerspectiveView`](PerspectiveView.md), executes dashboard-initiated mutations safely on the main game thread under `GetPapyrusMutex()`, pushes live change events, and writes diagnostic JSON dumps (`dumpData`, `dumpNPCIndex`).
- **`HouseholdBuffsJsonBuilder`**: Assembles read-only household spouse-buff JSON payloads from [`WorldStore`](WorldStoreAndCoSave.md), `BonusesConfigLoader`, and [`PollingService`](PollingService.md) teammate state.

---

## 2. View Lifecycle, Focus Policy & Threading

- **Optional Dependency**: Acquires `PRISMA_UI_API::IVPrismaUI1` (`V1`) at `kDataLoaded`. If `PrismaUI.dll` is absent, all methods degrade cleanly into no-ops without affecting gameplay or Papyrus.
- **Unified Focus Semantics (`disableFocusMenu = false`)**: Both active modals (via [`PrismaModalManager`](PrismaModalManager.md)) and the open dashboard invoke `Focus(view_, pauseGame, /*disableFocusMenu=*/false)`. When the user toggles dashboard pause (`setGamePaused`), `SyncViewVisibilityAndFocus` unfocuses and refocuses with the updated `pauseGame` flag so Skyrim pauses or resumes cleanly.
- **Blocking Game Menus**: Dashboard hotkey toggles ignore `MainMenu`, `LoadingMenu`, `Console`, `BookMenu`, and `TextInputMenu` states (`IsDashboardToggleBlockedByGameState`).
- **Teleport Focus Release**: Actions such as `teleportPlayerToNpc` and `teleportNpcToPlayer` hide the dashboard and schedule the world move inside `RunAfterFocusReleased(...)`, ensuring Skyrim camera/engine movement only fires after Prisma has released focus and closed cleanly.
- **Inbound Dispatch (`HandleActionJson`)**:
  - `uiReady` runs on an SKSE task acquiring `GetPapyrusMutex()` directly so initialization never misses an epoch handshake.
  - All other inbound `onAction` payloads enqueue via `AddCurrentEpochTask` and acquire `GetPapyrusMutex()` before invoking `HandleAction` handlers (protecting against load/revert races).
  - Outbound pushes (`SendOverviewData`, `SendNPCList`, `SendNPCDetail`, `SendHouseholdBuffs`, etc.) dispatch to the main thread before invoking `PrismaUIManager::SendToUI`.

---

## 3. Strict CQS, Intrigue Masking & `DumpAllData`

Every query builder and JSON dump in `PrismaUIDataBridge` and `HouseholdBuffsJsonBuilder` obeys strict Command-Query Separation and uniform Intrigue Mode masking:

- **No Lazy Mutation on Read**: `BuildOverviewJson`, `BuildNPCProfileJson`, `BuildNPCDetailJson`, `BuildHouseholdBuffsJson`, `ProcessDumpData`, and `ProcessDumpNPCIndex` read only lock-free `WorldStore::Snapshot()` / `SpatialSnapshotStore::Snapshot()` views—never registering actors, modifying custody, or emitting events during serialization.
- **Uniform `PerspectiveView::ForDashboard` Intrigue Masking**:
  - All dashboard views and `ProcessDumpData` (`dumpData`) construct a single [`PerspectiveView::ForDashboard`](PerspectiveView.md) bound to `SettingsData::revealSecretsInUI` (with **zero `0x14` Player exemption**).
  - When `!revealSecretsInUI`, undiscovered cheating records, secret paternity links (`bioFather`), unannounced NPC pregnancies, secret werewolf inheritance, and masked deceased-registry records (`'DCS1'`) remain hidden identically across dashboard cards, relationship timelines, NPC detail drawers, and full JSON dumps.
- **Diagnostic Data Dumps (`DumpStoredData` / `ProcessDumpData`)**:
  - Exposed both to Papyrus via `IUiBridgeGateway::DumpStoredData(tag)` (`MARAS.DumpStoredData(string tag = "")`) and to the UI `dumpData` action (`ProcessDumpData` forwards optional `payload.tag`).
  - Sanitizes `tag` to `[A-Za-z0-9_-]` (capped at `48` characters); when non-empty, writes `meta.dumpTag` and exports `Data/SKSE/Plugins/MARAS/dumps/maras_dump_<tag>_<YYYY-MM-DD_HH-MM-SS>.json` (or `maras_dump_<YYYY-MM-DD_HH-MM-SS>.json` when untagged).
  - Exports `meta` (including `serializationVersion`, optional `dumpTag`, `hasSkyrimNet`, `simulateNoSkyrimNet`, `secretsMasked`, calendar timestamps), `settings`, `spouseHierarchy`, `relationshipManager`, `affectionService`, `playerHouses`, `spouseAssets`, `playerChildrenService`, `matchmakingService`, `deceasedRegistry`, `banterCoordinator`, and `adultChildTravel`, while emitting `onDataDumpCompleted` notifications strictly when triggered from the UI (`notifyUi = true`).
- **Training Asset Resolution**: `ResolveTrainingImageSrc` caches encoded image references under `PrismaUIDataBridge::mutex_` to keep `SendPlayerTrainingEligibility` / catalog payloads lightweight.
