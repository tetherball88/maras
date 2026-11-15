# M.A.R.A.S — Cross-Layer Architecture & Project Rules

Project-wide invariants across all layers of **M.A.R.A.S**. **Mandatory First Step**: Before planning or editing code in any layer, read that layer's `AGENTS.md` first:
1. **SKSE C++ Plugin**: [SKSE_Source/AGENTS.md](SKSE_Source/AGENTS.md) ([modules/](SKSE_Source/modules/AGENTS.md), [tests/](SKSE_Source/tests/AGENTS.md))
2. **Prisma UI Dashboard**: [prisma-src/AGENTS.md](prisma-src/AGENTS.md)
3. **Papyrus Scripts**: [Scripts/Source/AGENTS.md](Scripts/Source/AGENTS.md)
4. **SkyrimNet Bundle**: [SKSE/Plugins/SkyrimNet/external/tetherball88.maras/AGENTS.md](SKSE/Plugins/SkyrimNet/external/tetherball88.maras/AGENTS.md)

Architecture docs live in `docs/modder/` (`ARCHITECTURE.md`, `SKYRIMNET_AND_AI_INTEGRATION.md`, `HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md`, `API_AND_PRISMA_BRIDGE_REFERENCE.md`, `RELATIONSHIP_DYNAMICS_GUIDE.md`, `FERTILITY_MODE_RELOADED_INTEGRATION.md`, `INGAME_SANITY_TEST_SUITE.md`). Step-by-step runbooks live in `.agents/skills/` (`maras-build-and-test`, `maras-cosave-and-tests`, `maras-add-setting`, `maras-doc-sync`).

---

## 0. Build & Test Policy

- **Build & Test Directly Without Path-Hunting**: Never search the filesystem for `cmake.exe`, Visual Studio, or `vcpkg`. Use the one-command script in `.agents/skills/maras-build-and-test`:
  `powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target all` (or `-Target tests` for ~1s unit-test loop; `Set-Location prisma-src; npm run build` for UI).
- **Locked Target DLL (`SkyrimSE.exe` Open)**: If Skyrim is running, `SKSE/Plugins/MARAS.dll` is locked by Windows, causing CMake's `POST_BUILD` copy (`MSB3073`) to fail *after* `SKSE_Source/build/release/Release/MARAS.dll` compiles and links cleanly. Treat compilation as **succeeded**—never retry in a loop or kill Skyrim; simply note that deploying the new DLL awaits closing the game.
- **Mandatory Unit Tests**: Changes to pure state tables, `'MARS'` co-save serialization, domain lifecycle rules, or secret masking in `SKSE_Source/modules/` **must** include new/updated tests in `SKSE_Source/tests/` verified green via `maras_tests.exe`.

---

## 1. Module Placement & Anti-Monolith Discipline

- **C++ 13-Module DAG (`7 STATIC` Pure + `6 OBJECT` Engine/App/Bridge in `SKSE_Source/modules/`)**:
  - **Pure State (`maras_state`) & Visibility (`maras_projection`)**: Persistent/runtime tables, `SessionEpoch`, and `'MARS'` v7 co-save live **only** in `modules/state/`; observer/UI/SkyrimNet secret filtering lives **only** in `modules/projection/` (`PerspectiveView`). Zero ad-hoc state caches in `app/` or `bridge_*/`.
  - **Pure Domain First (`maras_domain_{relationships,lineage,household,agency}`)**: All gameplay rules, eligibility checks, math/scoring, daily ticks, and state-transition decisions **must** be authored as pure, engine-free functions/types in `modules/domain_*/` (`pch_core.h`; zero `<RE/...>`, `<SKSE/...>`, JSON, YAML, or spdlog) with headless unit tests in `SKSE_Source/tests/`. **Never** inline domain rules or formulas inside `*UseCases.cpp`, coordinators, reconcilers, or bridges.
  - **Thin Engine (`maras_engine`), App (`maras_app`), & Bridges (`maras_bridge_{papyrus,ui,skyrimnet,c_api}`)**: `engine/` owns SKSE hooks, `FormCache`, `SpatialWorldIndex`, and main-thread `DirtyActorSet` reconcilers; `app/` coordinates `WorldStore::Modify` + pure domain calls + reconciler triggers; `bridge_*/` marshals Papyrus, Prisma UI, SkyrimNet, and C API payloads only.
- **No God-File Dumping (Split Focused Files Instead of Growing Monoliths)**:
  - **Never** dump new features, subsystems, JSON builders, or unrelated helpers into large existing files (`RelationshipUseCases.cpp`, `LineageUseCases.cpp`, `PaternityWorkflowCoordinator.cpp`, `AgencyWorkflowCoordinator.cpp`, `PrismaUIDataBridge.cpp`, `SkyrimNetDecorators.cpp`, `SkyrimNetMemoryService.cpp`, `useMarasStore.ts`, `TTM_ServiceChildrenLogic.psc`). Treat oversized files as **extraction targets, not dumping grounds**.
  - For any distinct new workflow, calculator, payload builder, or UI feature, create focused single-responsibility `.h`/`.cpp` (or `.ts`/`.tsx`) files in the proper module directory, register new C++ sources in [SKSE_Source/CMakeLists.txt](SKSE_Source/CMakeLists.txt), and build with `-Reconfigure`. When modifying an oversized file, extract touched domain/helper logic out rather than growing it.

---

## 2. Core Cross-Layer Domain Rules

- **Single Source of Truth & Strict CQS**:
  - Domain state (relationships, affection, hierarchy, houses, children, cooldowns, settings) is owned by C++ (`WorldStore` in `modules/state/`) and persisted in the `'MARS'` v7 SKSE co-save (12 chunks, plus 5 optional trailing extensions: `'FRT1'` in `'NPCR'`, `'PLC1'` in `'PLCH'`, `'REL1'` in `'MKMK'`, `'DCS1'` in `'DCSD'`, and `'BNT1'` in `'BNTR'`). `TT_MARAS.esp` factions (`0x7`, `0x111`, `0x119`, `0x6C`, `0xC23`, etc.) are **engine mirrors** for CK conditions/packages—never treat faction rank alone as authoritative when C++ APIs exist.
  - **Command-Query Separation (CQS)**: Query snapshots, UI bridge payloads (`SendOverviewData`, `SendNPCList`, `SendNPCDetail`), JSON builders (`Build*Json`, `DumpJson`), Papyrus queries (`GetTrackedNpcHomeMarker`, `GetChildHomeMarker`), and SkyrimNet decorators (`ttm_*`) **must** be strictly read-only—never mutate state, sync custody, lazy-register actors, or record memories while reading.
- **Pair Symmetry vs. Directional Asymmetry**:
  - Romantic statuses (`Dating`, `Lover`, `Engaged`, `Married`, `ExDating`, `ExLover`, `Jilted`, `Divorced`) live on canonical `PairKey(min(A,B), max(A,B))`.
  - Cheating/infidelity flags, open-relationship consent, and paternity secrets (`trueFatherID` vs. `deceivedPartnerID`) are **directional/role-specific**—never assume symmetry across a pair.
- **Guardian, Parental & Adoption Semantics**:
  - **Strict Slot Neutrality**: `guardianA/B`, `guardianAID/BID`, `parentA/B`, `parentAID/BID` are interchangeable neutral slots—never infer sex or maternal/paternal roles from slot position (inspect `bioMotherID`/`bioFatherID` or actor sex).
  - **Guardians vs. Bio Parents vs. Adoption**: Guardians = custodial caretakers; Bio Parents = genetic lineage (never conflate). `isAdopted` marks an explicit household adoption process—never infer `isAdopted` from guardian presence or count.
- **Secret Masking Semantics (`PerspectiveView` in `SKSE_Source/modules/projection/`)**:
  - **SkyrimNet Self-Perspective (`render_mode != "dialogue_target"`)**: Never leak undiscovered secrets (`isSecretForPartner`, `isPregnancySecretForPartner`, `isSecretForBioFather`, secret `hasBeastblood`, undiscovered `CheatingRecord`s) when `!canActorKnow` (including to an unaware Player). Derived lineage fields (`bio_father`, `biological_parents`, `step_parents`, `siblings`, `is_actor_bio_parent`, `is_actor_step_parent`) must reflect what that actor *believes*.
  - **SkyrimNet Target Bio (`render_mode == "dialogue_target"`)**: Public knowledge only for *another* speaker's prompt. Never expose undiscovered affairs (`is_secret_affair`), secret paternity (`has_undiscovered_paternity_secret`, `is_paternity_secret`, `is_other_parent_secret`), secret `siblings` (use `siblings_public`), or `has_beastblood`—even when the target personally knows.
  - **Prisma UI & Dumps — Intrigue Mode (`!SettingsData::revealSecretsInUI`)**: Uniform world-wide masking (`PerspectiveView::ForDashboard`) across Dashboard and JSON dumps—no undiscovered affair, secret paternity, secret werewolf inheritance, or unannounced NPC pregnancy is shown until discovered in-game or `revealSecretsInUI` is enabled. **Never add `0x14` Player exemptions to UI masking.**
- **Cross-Layer Settings Sync**: Adding/renaming/removing any setting requires updating `WorldTables.h` (`SettingsData`) + `CoSaveCodec.cpp` (`'CNFG'`) + `test_cosave_migrations.cpp`, `SettingsCoordinator.h/.cpp` (`modules/app/`), `PrismaUIDataBridge.cpp` (`modules/bridge_ui/`), `prisma-src`, and `PapyrusSettings.cpp` / `PapyrusInterface.cpp` (`MARAS.psc` in `modules/bridge_papyrus/`) together (activate `maras-add-setting` skill).
- **Standalone Prisma UI Modals**: All confirmations, retry/cancel prompts, and interactive dialogs **must** use `PrismaModalManager` $\leftrightarrow$ `useModalStore` / `<PrismaModalHost />` (never native `RE::MessageBoxMenu`), mounted independently of `<DashboardLayout>`, pausing the game with `Focus(view, /*pauseGame=*/true, /*disableFocusMenu=*/false)`, and resolvable only via modal control buttons.

---

## 3. SkyrimNet Cross-Layer Gating Policy

- **Caller-Level Runtime Gate**: Gate all SkyrimNet features/UI/logic at the **call site** (`MARAS::IsSkyrimNetAvailable()` in C++, `TTM_Data.GetHasSkyrimNet()` in Papyrus) *before* dice rolls, data gathering, or state mutations. *(Exempt: C++ callbacks registered via `PublicRegisterDecorator` and functions invoked solely by `external/tetherball88.maras/actions/*.yaml`.)*
- **Non-SkyrimNet Fallback**: Never silently assume "skip if no SkyrimNet" on new features without fallback—explicitly ask the user whether to (1) no-op, (2) use a deterministic/dice-roll fallback, or (3) hide/disable in UI.

---

## 4. Refactoring, Documentation & Logging Discipline

- **Clean Cut Refactoring**: Delete superseded logic, obsolete helpers, and dead parameters outright. Preserve **save-game (`'MARS'` v1–v7 co-save) backward compatibility**, **not** internal code/API compatibility. **Never** add unsolicited duplicate alias functions.
- **Documentation Sync**: Update affected `docs/modder/*` and `docs/player/*` files in the same task (activate `maras-doc-sync` skill; describe behavior/formulas/edge cases in `docs/modder/services/`, never copy-paste `.h` signatures).
- **Logging (`MARAS.log`)**: Use `ERROR` (broken invariants, missing required config/forms—fail fast, never guess by name), `WARN` (recoverable anomalies), `INFO` (one-line lifecycle transitions only—never per-tick/loop), `DEBUG` (decision inputs, FormIDs, rolls vs. thresholds), `TRACE` (inner loops). Include hex FormID `{:08X}`, actor/child name, and enum/index on `INFO`/`DEBUG`/`WARN`/`ERROR`. Add targeted boundary logs instead of guessing on repro bugs.
