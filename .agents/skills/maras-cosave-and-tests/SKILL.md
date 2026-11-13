---
name: maras-cosave-and-tests
description: >-
  Guide and checklist for modifying 'MARS' v7 SKSE co-save serialization (CoSaveCodec / Serialization.cpp) and authoring deterministic headless unit tests in SKSE_Source/tests/. Activate whenever touching persistent state tables, co-save chunks, version migrations, FormID resolution, or C++ unit tests.
---

# `'MARS'` v7 Co-Save & Headless Unit Test Runbook

## 1. The 12 `'MARS'` (`kDataVersion = 7`) Co-Save Chunks

All persistent mod state lives under SKSE serialization UID `'MARS'`:
- `'NPCR'` — Registered NPCs, canonical pair relationship edges & history, directional cheating/open flags, pending partner intents, lineage/children, and base-ID deferred queues.
- `'SPHR'` — Player spouse hierarchy ranks (`Primary`, `Secondary`, `Tertiary`, `Subordinate`).
- `'AFCT'` — Permanent & daily affection scores, interaction timestamps, loneliness tracking.
- `'PHOU'` — Registered player houses, spouse/tenant home assignments, shared NPC home cells.
- `'SPAS'` — Shared spouse beds & `originalBedOwners_` (`0` sentinel = originally unowned).
- `'PLCH'` — Player child critters, pets, banned flags, interaction timestamps.
- `'CFSN'` — Pre-registration child faction snapshots (`factionSnapshots_`, independent of `'PLCH'`).
- `'MKMK'` — Matchmaking & partner-agency cooldowns/timestamps.
- `'DCSD'` — Deceased actor registry (`RefId` primary key; dynamic `0xFF` actors must never index by shared template `BaseId`).
- `'BNTR'` — Couple banter cooldowns.
- `'ACTR'` — Stage 4 Adult Child travel states, home/destination assignments, return timers.
- `'CNFG'` — Co-save persisted `SettingsData` (with internal sub-version header).

## 2. Mandatory Co-Save Serialization Rules

1. **Save-Game Backward Compatibility (`v1`–`v7`):**
   - Bump `kDataVersion` (or chunk sub-version) and guard new reads behind version checks with safe defaults for older saves.
2. **Bounded Counts & Atomic Rollback:**
   - Validate `count <= kMaxEntries` before `.resize(count)` / `.reserve(count)`.
   - Check every `Read*` return value. **Never** `break;` mid-loop on a truncated stream—immediately `Revert()` / discard staging state and `return false;` so misaligned trailing bytes never corrupt live tables.
3. **Two-Phase Load (`LoadChunk` $\to$ `FinalizeLoad`):**
   - **Phase 1 (`LoadChunk`):** Pure per-chunk binary parsing + `ResolveFormID` into staging tables. Zero cross-table queries, zero `DeceasedTable` pruning, zero engine `RE::Actor*` calls.
   - **Phase 2 (`FinalizeLoad`):** Runs **after all 12 chunks load** so chunk arrival order never changes final state. Prunes orphaned dynamic `0xFF` refs (`IsDynamicPrefix && !IsSyntheticChild && !IsLoadedInSave`), preserves unloaded placed NPCs (`0x00`–`0xFE`), reconciles custody/hierarchy gaps, and schedules main-thread engine syncs.
4. **FormID Resolution & Synthetic Child Pool (`0xFF700000`–`0xFF7FFFFF`):**
   - Always check `IsSyntheticChildRawId(id)` (`0xFF700000`–`0xFF7FFFFF`) **before** calling `ResolveFormID` or `TESForm::LookupByID`.
   - Never advance `nextSyntheticChildId` from real `0xFF800000+` `PlaceAtMe` FormIDs.
   - Preserve explicit `0` sentinels (e.g., `originalBedOwners_` where `0` means unowned) instead of passing `0` to `ResolveFormID`.

## 3. Writing & Updating Headless Unit Tests (`SKSE_Source/tests/`)

1. **Where Tests Live:**
   - `tests/test_cosave_migrations.cpp` — Legacy `v1`–`v7` migrations, full 12-chunk lossless round-trip, reversed chunk arrival order parity, synthetic pool bounds, truncated-stream rollback, `Revert()` zero-bleed, `CowTable` structural sharing.
   - `tests/test_custody_and_death.cpp` — `GuardianSlots` neutrality, `ResolveSingleCustodian` policies, marriage/divorce cleanup, spouse/child/adult-child death cascades, FMR dynamic `0xFF` sibling isolation.
   - `tests/test_affair_ledger.cpp` — Directional infidelity, `Talking` $\to$ `Physical` escalation, open/polygamy exemptions, perspective-bound secret visibility, $O(1)$ 3-participant closure.
   - New pure modules (`maras_projection`, `maras_domain_*`) $\to$ add `tests/test_<module>.cpp` and list it in `add_executable(maras_tests ...)` inside `SKSE_Source/CMakeLists.txt`.
2. **Authoring Rules (`tests/TestHarness.h`):**
   - Use `MARAS_TEST(Suite, Name)` and `MARAS_CHECK` / `MARAS_REQUIRE` / `MARAS_CHECK_EQ` / `MARAS_REQUIRE_EQ`.
   - Use deterministic `MockFormIdResolver`, `IActorMetadataProvider`, and `MARAS::Foundation::IRng` fakes—zero sleeps, zero `std::random_device`, zero engine/JSON/YAML includes.
   - Run via `.agents/skills/maras-build-and-test/scripts/build-and-test.ps1 -Target tests`.
