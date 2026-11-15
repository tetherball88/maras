# AGENTS.md — Headless C++ Unit Tests (`SKSE_Source/tests/`)

Applies to `SKSE_Source/tests/` (`maras_tests.exe`). Supplements [SKSE_Source/AGENTS.md](../AGENTS.md) and root [AGENTS.md](../../AGENTS.md).

---

## 1. Mandatory Test Coverage & 10-Suite Layout

Every addition, fix, or refactor in `SKSE_Source/modules/` **MUST** update or add unit tests in the same change (`maras_tests.exe` currently runs **82 test cases / 1404 assertions** across **10 test files**):
- `test_cosave_migrations.cpp` (9) — `'MARS'` `v1`–`v7` migrations, full 12-chunk lossless round-trip, `'DCS1'` deceased secrecy extension round-trip & omission when empty, chunk-order independence, `0xFF700000`–`0xFF7FFFFF` synthetic bounds, unloaded placed NPC preservation vs. orphaned `0xFF` pruning, truncated-chunk atomic rollback, `Revert()` zero-bleed, and `CowTable` structural sharing.
- `test_custody_and_death.cpp` (6) — `GuardianSlots` neutrality, `ResolveSingleCustodian` policies, marriage step-child linking & divorce household/hierarchy cleanup, spouse/child/Stage 4 adult-child death cascades, FMR `0xFF` sibling `DeceasedTable` isolation, and deceased secret affair/paternity preservation.
- `test_affair_ledger.cpp` (5) — Directional infidelity asymmetry, `Talking` $\to$ `Physical` escalation + `ConfessPhysical` auto-upgrade, open/polygamy/co-spouse exemptions, perspective-bound secret visibility, and $O(1)$ 3-participant closure.
- `test_world_store.cpp` (4) — Snapshot immutability & structural sharing, non-void & reentrant `WorldStore::Modify`, INI baseline revert, and concurrent reader consistency.
- `test_perspective_lens.cpp` (11) — `SpatialSnapshot` (`3`: sorted lookup/UTF-8 truncation, nearby/teammate parity, zero-alloc concurrency) and `PerspectiveView`/`PerspectiveLens` (`8`: married/single mother secrecy matrices, `IsSecretForBioFather` discovery, beastblood/sibling masking, zero `0x14` Player exemption in `DashboardIntrigue`, adopted orphan semantics).
- `test_reconcilers.cpp` (9) — `EngineSyncPlan` desired globals, player/adult/child/companion faction plans, package precedence (`Tenant > Pet > Child > AdultTravel`), buff multipliers, and fresh/reverted store defaults.
- `test_domain_relationships.cpp` (10) — Family guards & relationship formation, auto-marriage backfill, infidelity escalation & open/close/discovery resolution, pending intent cleanup, engagement/auto-open propagation, affection decay, hierarchy ranking, marriage difficulty, classification, and bonuses.
- `test_domain_lineage.cpp` (9) — Adoption blocks, beastblood & guardian inheritance, unborn twin promotion & removal, fertility phenotype & two-edge paternity secret lifecycle (`ResolveForBioFather`, `SetPaternitySecretEdges` cooldown bypass, bio-father step-in filters, fallback breakup, visual discrepancy tiers), child growth, training catalog, and `NamePool`.
- `test_domain_household.cpp` (7) — Houses, tenants, relocation, divorce tenant eviction & shared assets, `DeceasedQueries` & dynamic actor isolation + secret masking, adult-child travel lifecycle (`SelectNextDestination`, arrival timeout, invalid-adult cleanup), and household buff evaluation.
- `test_domain_agency.cpp` (12) — Matchmaking orientation/slot/cap/setting gates & deterministic dice-roll proposals, partner agency cheating immunities, temperament/stage chance caps, intent expiry, home-arrival cooldown branching, encounter template rendering & lover masking, and `BanterPlanner`.

When adding new test files, register them under `add_executable(maras_tests ...)` in [SKSE_Source/CMakeLists.txt](../CMakeLists.txt).

## 2. Authoring & `LNK2019` Link-Gate Rules

- **Framework (`TestHarness.h`)**: Define cases with `MARAS_TEST(Suite, Name)` and assert with `MARAS_CHECK`, `MARAS_REQUIRE`, `MARAS_CHECK_EQ`, `MARAS_REQUIRE_EQ`.
- **100% Hermetic & Deterministic**: Reuses `pch_core.h` from `maras_foundation`. Use `MockFormIdResolver`, `IActorMetadataProvider` stubs, `IWerewolfStateProvider` fakes, and deterministic `MARAS::Foundation::IRng` fakes. Zero `<RE/...>`/`<SKSE/...>`/`pch_engine.h`/`nlohmann`/`yaml-cpp`/`spdlog` includes, zero sleeps, zero `std::random_device`, zero external files.
- **Physical Link Gate**: `maras_tests` links **only** the 7 pure `STATIC` modules (`maras_foundation`, `maras_state`, `maras_projection`, `maras_domain_relationships`, `maras_domain_lineage`, `maras_domain_household`, `maras_domain_agency`) so any upward engine dependency fails immediately with `LNK2019`.
- **Build & Run**: `powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target tests` (append `-Filter "<Substring>"` to scope).
