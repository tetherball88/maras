# `UIDataBridge`, `UIManager` & `HouseholdBuffsJsonBuilder` — Architecture & Behavioral Guide

**Layer**: Bridge UI (`SKSE_Source/modules/bridge_ui`)  
**Sources**:
- [UIManager.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/UIManager.h) / [UIManager.cpp](../../../SKSE_Source/modules/bridge_ui/src/UIManager.cpp)
- [UIDataBridge.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/UIDataBridge.h) / [UIDataBridge.cpp](../../../SKSE_Source/modules/bridge_ui/src/UIDataBridge.cpp)
- [HouseholdBuffsJsonBuilder.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/HouseholdBuffsJsonBuilder.h) / [HouseholdBuffsJsonBuilder.cpp](../../../SKSE_Source/modules/bridge_ui/src/HouseholdBuffsJsonBuilder.cpp)
- [LocaleJsonBuilder.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/LocaleJsonBuilder.h) / [LocaleJsonBuilder.cpp](../../../SKSE_Source/modules/bridge_ui/src/LocaleJsonBuilder.cpp)
- [SanityEngineProbe.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/SanityEngineProbe.h) / [SanityEngineProbe.cpp](../../../SKSE_Source/modules/bridge_ui/src/SanityEngineProbe.cpp)
- [Architecture Overview](../ARCHITECTURE.md) & [Localization Guide](../LOCALIZATION_GUIDE.md)

---

## 1. Purpose & Responsibility

`UIManager`, `UIDataBridge`, `HouseholdBuffsJsonBuilder`, and `LocaleJsonBuilder` form the C++ $\leftrightarrow$ React (Ultralight / Magelight UI) transport layer for M.A.R.A.S.:

- **`UIManager`**: Registers the Magelight mod handle (`"MARAS"`), owns the single persistent hidden `Layer::Popup` Magelight view (`Data/Magelight/MARAS/views/MARAS/index.html`, keeping `SetViewPrepaint` disabled because React unmounts to `null` while hidden, avoiding offscreen VRAM/frame-spike overhead), hotkey/menu event sinks (`RE::InputEvent*`, `RE::MenuOpenCloseEvent`), UI-mode focus/pause reconciliation (`RequestUIMode` / `ReleaseUIMode`), Escape capture (`SetEscapeCapture`), gamepad support (`kGamepadStyleKeys`), deferred new-game relationship scan deferral, `PostToGameThread` marshaling, and `RunAfterFocusReleased` callbacks.
- **`UIDataBridge`**: Implements `IUiBridgeGateway`, registers UI JS listeners (`marasAction`), pushes active localization dictionaries (`SendLocaleData`), serializes read-only JSON snapshots through [`PerspectiveView`](PerspectiveView.md), executes dashboard-initiated mutations safely on the main game thread under `GetPapyrusMutex()`, pushes live change events, and writes diagnostic JSON dumps (`dumpData`, `dumpNPCIndex`).
- **`HouseholdBuffsJsonBuilder`**: Assembles read-only household spouse-buff JSON payloads from [`WorldStore`](WorldStoreAndCoSave.md), `BonusesConfigLoader`, `LocalizationService` display overlays, and [`PollingService`](PollingService.md) teammate state.
- **`LocaleJsonBuilder`**: Serializes the active `LocalizationCatalog` snapshot (`selectedLanguage`, `resolvedLanguage`, `detectedSkyrimLanguage`, `fontFamily`, discovered `_meta.json` `availableLanguages`, and merged `ui_*` dictionary strings) for `window.receiveMARASLocale`.
- **`SanityEngineProbe`**: Builds the strictly read-only `engineProbe` section inside `DumpStoredData` snapshots (`globals`, `player` bonus perks & vanilla marriage faction state, visible `RE::Actor` faction ranks, `Variable06`/`Variable07` orders, dynamic `0xFF` home `XMarker` resolution, runtime-cloned `TESNPC` adult-child pointers, `'CFSN'` snapshot counts, and live cell/bed ownership) for [in-game sanity verification](../INGAME_SANITY_TEST_SUITE.md) while respecting `PerspectiveView` secret masking.

---

## 2. View Lifecycle, Focus Policy & Threading

- **Optional Dependency**: Acquires `MAGELIGHT_API::MagelightApi4` via `RequestApi4()` at `kDataLoaded` and creates a single persistent view (`startVisible = false`). If Magelight UI is absent, all methods degrade cleanly into no-ops without affecting gameplay or Papyrus. On save-load / `Revert()`, `UIManager::Revert()` hides the view (`ShowView(view_, false)`) and releases UI mode instead of destroying/recreating the view.
- **Unified UI-Mode Focus & Pause (`RequestUIMode`)**: Both active modals (via [`ModalManager`](ModalManager.md)) and the open dashboard acquire Magelight UI mode via `RequestUIMode` (with `kUIModeFlagPause` when paused, plus `kUIModeFlagReclaim` / `CancelUIModeRequest` when available, and `SetEscapeCapture(view_, true)` so React controls Escape and dashboard hotkeys while focused). When the user toggles dashboard pause (`SetDashboardPaused`), `SyncViewVisibilityAndFocus` re-issues `RequestUIMode` with the updated pause flag so Skyrim pauses or resumes cleanly.
- **Blocking Game Menus**: Dashboard hotkey toggles ignore `MainMenu`, `LoadingMenu`, `Console`, `BookMenu`, and `TextInputMenu` states (`IsDashboardToggleBlockedByGameState`).
- **Teleport & Child-Spawn Focus Release**: Actions such as `teleportPlayerToNpc`, `teleportNpcToPlayer`, `spawnChild`, and player-household `addChild` hide the dashboard and schedule world moves / child-spawning events inside `RunAfterFocusReleased(...)` (and residence / adoption-spouse modals `ShowChooseHomeModal` / `ShowChooseAdoptionSpouseModal` defer their latent VM return via `IUiBridgeGateway::RunAfterFocusReleased`), ensuring Skyrim camera/engine movement and `ForceGreet` packages only fire after Magelight's `UIModeExited` event settles and focus has cleanly released.
- **Inbound Dispatch (`HandleActionJson`)**:
  - Magelight JS listeners execute on the render/page thread mid-frame, so `UIManager::PostToGameThread` marshals every inbound `marasAction` payload via `MagelightApi4::PostGameTask` (falling back to a short-lived worker thread + `SKSE::GetTaskInterface()->AddTask` on older hosts), entering `ScopedMainThreadTask` and acquiring `MARAS::GetPapyrusMutex()`.
  - `uiReady` runs on the game thread so initialization never misses an epoch handshake, immediately pushing `SendLocaleDataOnMainThread` and `SendSettingsDataOnMainThread` so standalone `<ModalHost />` dialogs have localized strings and `--ui-scale` even before the dashboard is opened; all other actions stamp `SessionEpoch` and drop silently if the session changed before execution.
  - **Hidden Dashboard Push Gating**: Dashboard-only outbound snapshots and real-time broadcasts (`SendOverviewData`, `SendHouseholdBuffs`, `SendNPCList`, `SendNPCDetail`, `SendMatchmakingData`, `SendAllSkyrimNPCs`, `SendMatchmakingSettings`, `OnAffectionChanged`, `OnStatusChanged`, `OnHierarchyChanged`, `OnChildUpdated`) short-circuit on `UIManager::IsDashboardOpen()` (and background IPC re-fetches in `uiBridge.ts` / `useMarasStore.ts` gate on `isOpen`) so gameplay mutations incur zero JSON serialization or webview IPC overhead while the dashboard is closed. When opened (`UIManager::Show` / `OnUIReady`), `UIDataBridge::RefreshDashboardOnOpen` + `openMARAS` synchronize overview, household buffs, matchmaking settings, and any open NPC detail view on the main thread before invoking `UIManager::SendToUI` (`InteropCall`), while `ModalManager`, `SendLocaleData`, `SendSettingsData`, and async LLM results remain ungated by dashboard visibility.

---

## 3. Strict CQS, Intrigue Masking & `DumpAllData`

Every query builder and JSON dump in `UIDataBridge` and `HouseholdBuffsJsonBuilder` obeys strict Command-Query Separation and uniform Intrigue Mode masking:

- **No Lazy Mutation on Read**: `BuildOverviewJson`, `BuildNPCProfileJson`, `BuildNPCDetailJson`, `BuildHouseholdBuffsJson`, `ProcessDumpData`, and `ProcessDumpNPCIndex` read only lock-free `WorldStore::Snapshot()` / `SpatialSnapshotStore::Snapshot()` views—never registering actors, modifying custody, or emitting events during serialization.
- **Uniform `PerspectiveView::ForDashboard` Intrigue Masking**:
  - All dashboard views and `ProcessDumpData` (`dumpData`) construct a single [`PerspectiveView::ForDashboard`](PerspectiveView.md) bound to `SettingsData::revealSecretsInUI` (with **zero `0x14` Player exemption**).
  - When `!revealSecretsInUI`, undiscovered cheating records, secret paternity links (`bioFather`), unannounced NPC pregnancies, secret werewolf inheritance, and masked deceased-registry records (`'DCS1'`) remain hidden identically across dashboard cards, relationship timelines, NPC detail drawers, and full JSON dumps.
- **Diagnostic Data Dumps (`DumpStoredData` / `ProcessDumpData`)**:
  - Exposed both to Papyrus via `IUiBridgeGateway::DumpStoredData(tag)` (`MARAS.DumpStoredData(string tag = "")`) and to the UI `dumpData` action (`ProcessDumpData` forwards optional `payload.tag`).
  - Sanitizes `tag` to `[A-Za-z0-9_-]` (capped at `48` characters); when non-empty, writes `meta.dumpTag` and exports `Data/SKSE/Plugins/MARAS/dumps/maras_dump_<tag>_<YYYY-MM-DD_HH-MM-SS>.json` (or `maras_dump_<YYYY-MM-DD_HH-MM-SS>.json` when untagged).
  - Exports `meta` (including `serializationVersion`, optional `dumpTag`, `hasSkyrimNet`, `simulateNoSkyrimNet`, `secretsMasked`, calendar timestamps), `settings`, `spouseHierarchy`, `relationshipManager`, `affectionService`, `playerHouses`, `spouseAssets`, `playerChildrenService`, `matchmakingService`, `deceasedRegistry`, `banterCoordinator`, `adultChildTravel`, and `engineProbe` (`BuildSanityEngineProbeJson`), while emitting `onDataDumpCompleted` notifications strictly when triggered from the UI (`notifyUi = true`).
- **Training Asset & Localization Overlay Resolution**: `ResolveTrainingImageSrc` caches encoded image references under `UIDataBridge::mutex_` to keep `SendPlayerTrainingEligibility` / catalog payloads lightweight. Because Magelight UI enforces `NetworkPolicy::FileOnly` on `file:///` views, localization strings are pushed directly from C++ via `SendLocaleData` (`window.receiveMARASLocale`) on `ViewDomReady`, `ViewReloaded`, `uiReady`, language changes (`updateSetting` `"language"`), and live `reloadLocalization` actions, while `WorldStore` state (`ChildRecord::trainingLocation`, `AdultChildTravelRecord::destinationName`) stays language-agnostic and is overlaid via `LocalizationService` solely at UI serialization time.
