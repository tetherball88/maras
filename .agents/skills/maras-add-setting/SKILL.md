---
name: maras-add-setting
description: >-
  End-to-end cross-layer synchronization checklist for adding, renaming, or removing a configuration setting in M.A.R.A.S. Activate whenever modifying SettingsCoordinator, SettingsData, or Prisma UI / Papyrus settings endpoints.
---

# Cross-Layer Settings Synchronization Checklist

Whenever adding, removing, or renaming a configuration setting, update **all** applicable layers in the same change:

1. **Pure State & Co-Save Codec (`SKSE_Source/modules/state/`):**
   - [WorldTables.h](../../../SKSE_Source/modules/state/include/maras/state/WorldTables.h) (`MARAS::State::SettingsData` field + default value + `operator==`, and register the field in `ForEachPersistedSettingField` if it is co-save persisted).
   - [CoSaveCodec.cpp](../../../SKSE_Source/modules/state/src/CoSaveCodec.cpp) (`'CNFG'` chunk serializer/deserializer in `SaveChunkCNFG` / `LoadChunkCNFG`: bump `'CNFG'` sub-version if changing binary layout and guard reads for older sub-versions).
   - [test_cosave_migrations.cpp](../../../SKSE_Source/tests/test_cosave_migrations.cpp) (assert non-default value round-trips losslessly through `'CNFG'` and resets on `Revert()`).

2. **Application Settings Coordinator & Engine Globals (`SKSE_Source/modules/app/` & `engine/`):**
   - [SettingsCoordinator.h](../../../SKSE_Source/modules/app/include/maras/app/SettingsCoordinator.h) & [SettingsCoordinator.cpp](../../../SKSE_Source/modules/app/src/SettingsCoordinator.cpp): expose read/write accessors backed by `MARAS::State::WorldStore` (`Snapshot()` for lock-free reads; `Modify(...)` for writes + engine reconciler calls).
   - Update `SettingsCoordinator.cpp`: INI `Load` (updates `WorldStore::SetIniBaselineSettings` + live state), `ApplyPostLoadEngineSideEffects`, `DumpJson` (if non-persisted or custom formatted), and key-lookup maps (`GetBoolSettingMap` / `GetIntSettingMap` / `GetFloatSettingMap` using `State::SettingsData::*` member pointers).
   - If mirrored to an `ESP` Global or `EngineSyncPlan`, also update [EngineSyncPlan.h](../../../SKSE_Source/modules/state/include/maras/state/EngineSyncPlan.h) / [EngineSyncPlan.cpp](../../../SKSE_Source/modules/state/src/EngineSyncPlan.cpp) and [GlobalVarReconciler.cpp](../../../SKSE_Source/modules/engine/src/GlobalVarReconciler.cpp).

3. **Prisma UI C++ Bridge ([PrismaUIDataBridge.cpp](../../../SKSE_Source/modules/bridge_ui/src/PrismaUIDataBridge.cpp)):**
   - Include in outgoing payloads (`SendOverviewData` and/or `SendMatchmakingSettings`).
   - Handle incoming updates (`ProcessUpdateSetting` and/or `ProcessUpdateMatchmakingSettings`, ensuring `SettingsCoordinator` updates `WorldStore` cleanly and checking `.is_boolean()` / `.is_number()` / `.is_string()` before extracting from `nlohmann::json`).

4. **Prisma UI React Frontend (`prisma-src/`):**
   - Update TypeScript interfaces (`prisma-src/src/types/`), Zustand store defaults/handlers (`prisma-src/src/store/useMarasStore.ts`), browser mock defaults (`prisma-src/src/store/prismaBridge.ts`), and settings tab UI controls.
   - Note: Legacy MCM (`TTM_MCM_*.psc`) is **retired**—never add new settings to MCM.

5. **Papyrus Native API (if exposed to `.psc` scripts):**
   - [PapyrusInterface.h](../../../SKSE_Source/modules/bridge_papyrus/include/maras/bridge_papyrus/PapyrusInterface.h), [PapyrusSettings.cpp](../../../SKSE_Source/modules/bridge_papyrus/src/PapyrusSettings.cpp), [PapyrusInterface.cpp](../../../SKSE_Source/modules/bridge_papyrus/src/PapyrusInterface.cpp) (`RegisterPapyrusFunctions`), and [MARAS.psc](../../../Scripts/Source/MARAS.psc) (exact signature parity).

6. **Verification:**
   - Run `.agents/skills/maras-build-and-test/scripts/build-and-test.ps1 -Target all` and `npm run build` in `prisma-src/` (if UI changed).
