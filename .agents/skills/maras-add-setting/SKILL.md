---
name: maras-add-setting
description: >-
  End-to-end cross-layer synchronization checklist for adding, renaming, or removing a configuration setting in M.A.R.A.S. Activate whenever modifying Settings.h, Settings.cpp, SettingsData, or Prisma UI / Papyrus settings endpoints.
---

# Cross-Layer Settings Synchronization Checklist

Whenever adding, removing, or renaming a configuration setting, update **all** applicable layers in the same change:

1. **Pure State & Co-Save Codec (`SKSE_Source/modules/state/`):**
   - `modules/state/include/maras/state/DomainTypes.h` (`MARAS::State::SettingsData` field + default value + `operator==`).
   - `modules/state/src/CoSaveCodec.cpp` (`'CNFG'` chunk serializer/deserializer: bump `'CNFG'` sub-version if changing binary layout and guard reads for older sub-versions).
   - `src/core/WorldStateBridge.cpp` (if bridging between `MARAS::Settings` and `MARAS::State::SettingsData`).
   - `tests/test_cosave_migrations.cpp` (assert non-default value round-trips losslessly through `'CNFG'` and resets on `Revert()`).

2. **Engine Settings Service (`SKSE_Source/include/utils/Settings.h` & `src/utils/Settings.cpp`):**
   - Declare member + default in `Settings.h` (add `std::shared_lock lock(settingsMutex_);` getter if read from off-main-thread SkyrimNet decorators or worker threads).
   - Update `Settings.cpp`: INI `Load` (if INI-backed), `SaveGameSettings`, `LoadGameSettings`, `RevertGameSettingsUnlocked`, `DumpJson`, key-lookup maps (`GetBoolSettingMap` / `GetIntSettingMap` / `GetFloatSettingMap`), and `SaveToSkyrimNetConfig` (if SkyrimNet-relevant).

3. **Prisma UI C++ Bridge (`SKSE_Source/src/ui/PrismaUIDataBridge.cpp`):**
   - Include in outgoing payloads (`SendOverviewData` and/or `SendMatchmakingSettings`).
   - Handle incoming updates (`ProcessUpdateSetting` and/or `ProcessUpdateMatchmakingSettings`, ensuring `Settings` persists properly and checking `.is_boolean()` / `.is_number()` / `.is_string()` before extracting from `nlohmann::json`).

4. **Prisma UI React Frontend (`prisma-src/`):**
   - Update TypeScript interfaces (`src/types/`), Zustand store defaults/handlers (`src/store/useMarasStore.ts`), browser mock defaults (`src/store/prismaBridge.ts`), and settings tab UI controls.
   - Note: MCM (`TTM_MCM_SettingsPage.psc`) is **deprecated**—never add new settings to MCM.

5. **Papyrus Native API (if exposed to `.psc` scripts):**
   - `SKSE_Source/include/papyrus/PapyrusInterface.h`, `SKSE_Source/src/papyrus/PapyrusSettings.cpp`, `PapyrusInterface.cpp` (`RegisterPapyrusFunctions`), and `Scripts/Source/MARAS.psc` (exact signature parity).

6. **Verification:**
   - Run `.agents/skills/maras-build-and-test/scripts/build-and-test.ps1 -Target all` and `npm run build` in `prisma-src/` (if UI changed).
