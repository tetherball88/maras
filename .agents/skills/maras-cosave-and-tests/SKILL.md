---
name: maras-cosave-and-tests
description: >-
  Guide and checklist for modifying 'MARS' v7 SKSE co-save serialization (CoSaveCodec / SkseSerializationCallbacks) and authoring deterministic headless unit tests in SKSE_Source/tests/. Activate whenever touching persistent state tables, co-save chunks, version migrations, FormID resolution, or C++ unit tests.
---

# `'MARS'` v7 Co-Save & Headless Unit Test Runbook

## 1. The 12 `'MARS'` (`kDataVersion = 7`) Co-Save Chunks

All persistent mod state lives under SKSE serialization UID `'MARS'` (implemented in [CoSaveCodec.cpp](../../../SKSE_Source/modules/state/src/CoSaveCodec.cpp) & [WorldTables.h](../../../SKSE_Source/modules/state/include/maras/state/WorldTables.h), invoked from [SkseSerializationCallbacks.cpp](../../../SKSE_Source/modules/engine/src/SkseSerializationCallbacks.cpp)):
- `'NPCR'` — Registered NPCs, canonical pair relationship edges & history, directional cheating/open flags, pending partner intents, lineage/children, and base-ID deferred queues.
- `'SPHR'` — Player spouse hierarchy ranks (`Primary`, `Secondary`, `Tertiary`, `Subordinate`).
- `'AFCT'` — Permanent & daily affection scores, interaction timestamps, loneliness tracking.
- `'PHOU'` — Registered player houses, spouse/tenant home assignments, shared NPC home cells.
- `'SPAS'` — Shared spouse beds & `originalBedOwners_` (`0` sentinel = originally unowned).
- `'PLCH'` — Player child critters, pets, banned flags, interaction timestamps.
- `'CFSN'` — Pre-registration child faction snapshots (`factionSnapshots_`, independent of `'PLCH'`).
- `'MKMK'` — Matchmaking & partner-agency cooldowns/timestamps.
- `'DCSD'` — Deceased actor registry (`RefId` primary key; dynamic `0xFF` actors must never index by shared template `BaseId`), plus optional trailing `'DCS1'` extension (`kExtDeceasedSecrecyDCS1`, omitted when empty) persisting `DeceasedPartnerEntry::{isSecretAffair, publicStatusAtDeath}` and `DeceasedChildMetadata::{hasUndiscoveredPaternitySecret, publicBioFatherBaseId}`.
- `'BNTR'` — Couple banter cooldowns.
- `'ACTR'` — Stage 4 Adult Child travel states, home/destination assignments, return timers.
- `'CNFG'` — Co-save persisted `SettingsData` (with internal sub-version header, sub-version `4`).

## 2. Mandatory Co-Save Serialization Rules

1. **Save-Game Backward Compatibility (`v1`–`v7`):**
   - Bump `kDataVersion` (or chunk sub-version / trailing tagged extension like `'DCS1'`) and guard new reads behind version/extension checks with safe defaults for older saves.
2. **Bounded Counts & Atomic Rollback:**
   - Validate `count <= kMaxEntries` before `.resize(count)` / `.reserve(count)`.
   - Check every `Read*` return value. **Never** `break;` mid-loop on a truncated stream—immediately discard staging state (`Revert()`) and `return false;` so misaligned trailing bytes never corrupt live tables.
3. **Two-Phase Load (`LoadChunk` $\to$ `FinalizeWorldStateAfterLoad`):**
   - **Phase 1 (`LoadChunk`):** Pure per-chunk binary parsing + `IFormIdResolver::ResolveFormID` into staged `WorldState`. Zero cross-table queries, zero `DeceasedTable` pruning, zero engine `RE::Actor*` calls.
   - **Phase 2 (`FinalizeWorldStateAfterLoad` $\to$ `WorldStore::ReplaceState` $\to$ `AppLifecycleCoordinator::FinalizeCoSaveLoad`):** Runs **after all 12 chunks load** so chunk arrival order never changes final state. Prunes orphaned dynamic `0xFF` refs (`IsDynamicPrefix && !IsSyntheticChild && !IsLoadedInSave`), preserves unloaded placed NPCs (`0x00`–`0xFE`), reconciles custody/hierarchy gaps, and triggers main-thread engine syncs (`GlobalVarReconciler`, `FactionReconciler`, `PackageReconciler`).
4. **FormID Resolution & Synthetic Child Pool (`0xFF700000`–`0xFF7FFFFF`):**
   - Always check `IsSyntheticChildRawId(id)` (`0xFF700000`–`0xFF7FFFFF`) **before** calling `ResolveFormID` or `TESForm::LookupByID`.
   - Never advance `nextSyntheticChildId` from real `0xFF800000+` `PlaceAtMe` FormIDs.
   - Preserve explicit `0` sentinels (e.g., `originalBedOwners_` where `0` means unowned) instead of passing `0` to `ResolveFormID`.

## 3. Writing & Updating Headless Unit Tests (`SKSE_Source/tests/`)

1. **Where Tests Live (`maras_tests.exe` — 10 Suites, 59 Test Cases, 1131 Assertions):**
   - `tests/test_cosave_migrations.cpp` (9) — Legacy `v1`–`v7` migrations, full 12-chunk lossless round-trip, `'DCS1'` deceased secrecy extension round-trip & omission when absent, reversed chunk arrival order parity, synthetic pool bounds, truncated-stream rollback, `Revert()` zero-bleed, `CowTable` structural sharing.
   - `tests/test_custody_and_death.cpp` (6) — `GuardianSlots` neutrality, `ResolveSingleCustodian` policies, marriage/divorce cleanup, spouse/child/adult-child death cascades, FMR dynamic `0xFF` sibling isolation, deceased secret affair/paternity preservation.
   - `tests/test_affair_ledger.cpp` (5) — Directional infidelity, `Talking` $\to$ `Physical` escalation, open/polygamy exemptions, perspective-bound secret visibility, $O(1)$ 3-participant closure.
   - `tests/test_world_store.cpp` (4) — `WorldStore` snapshot immutability, non-void & reentrant `Modify`, INI baseline revert, concurrent reader consistency.
   - `tests/test_perspective_lens.cpp` (11) — `SpatialSnapshot` (3) & `PerspectiveView`/`PerspectiveLens` (8) secrecy matrices (`IsSecretForPartner`, `IsSecretForBioFather`, beastblood/siblings, zero `0x14` UI exemption).
   - `tests/test_reconcilers.cpp` (9) — `EngineSyncPlan` desired globals, factions, package precedence, buff formulas, fresh/reverted store defaults.
   - `tests/test_domain_relationships.cpp` (5) — Family guards, formation, auto-marriage backfill, infidelity/open-close, affection, hierarchy, difficulty/classification/bonuses.
   - `tests/test_domain_lineage.cpp` (3) — Adoption blocks, beastblood/guardian inheritance, fertility phenotype & paternity secret lifecycle, growth, training catalog, `NamePool`.
   - `tests/test_domain_household.cpp` (3) — Houses, tenants, relocation/shared assets, `DeceasedQueries`, adult-child travel, household buff evaluation.
   - `tests/test_domain_agency.cpp` (4) — Matchmaking rules, partner agency, encounter templates, banter planning.
   - Register any additional `tests/test_<module>.cpp` inside `add_executable(maras_tests ...)` in [SKSE_Source/CMakeLists.txt](../../../SKSE_Source/CMakeLists.txt).
2. **Authoring Rules (`tests/TestHarness.h`) & Physical `LNK2019` Link Gate:**
   - Reuses `pch_core.h` from `maras_foundation` and links **only** the 7 pure `STATIC` targets (`maras_foundation`, `maras_state`, `maras_projection`, `maras_domain_*`).
   - Use `MARAS_TEST(Suite, Name)` and `MARAS_CHECK` / `MARAS_REQUIRE` / `MARAS_CHECK_EQ` / `MARAS_REQUIRE_EQ`.
   - Use deterministic `MockFormIdResolver`, `IActorMetadataProvider`, `IWerewolfStateProvider`, and `MARAS::Foundation::IRng` fakes—zero sleeps, zero `std::random_device`, zero `<RE/...>`/`<SKSE/...>`/`pch_engine.h`/`nlohmann`/`yaml-cpp`/`spdlog` includes.
   - Run via `.agents/skills/maras-build-and-test/scripts/build-and-test.ps1 -Target tests`.
