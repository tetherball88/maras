# AGENTS.md — Headless C++ Unit Tests (`SKSE_Source/tests/`)

Applies to `SKSE_Source/tests/` (`maras_tests.exe`). Supplements `SKSE_Source/AGENTS.md` and root `AGENTS.md`.

---

## 1. Mandatory Test Coverage & Suite Layout

Every addition, fix, or refactor in `SKSE_Source/modules/` **MUST** update or add unit tests in the same change:
- `test_cosave_migrations.cpp` — `'MARS'` `v1`–`v7` migrations, full 12-chunk lossless round-trip, chunk-order independence, `0xFF700000`–`0xFF7FFFFF` synthetic bounds, unloaded placed NPC preservation vs. orphaned `0xFF` pruning, truncated-chunk atomic rollback, `Revert()` zero-bleed, and `CowTable` structural sharing.
- `test_custody_and_death.cpp` — `GuardianSlots` neutrality, `ResolveSingleCustodian` policies, marriage step-child linking & divorce household/hierarchy cleanup, spouse/child/Stage 4 adult-child death cascades, and FMR `0xFF` sibling `DeceasedTable` isolation.
- `test_affair_ledger.cpp` — Directional infidelity asymmetry, `Talking` $\to$ `Physical` escalation + `ConfessPhysical` auto-upgrade, open/polygamy/co-spouse exemptions, perspective-bound secret visibility, and $O(1)$ 3-participant closure.
- New pure modules (`maras_projection`, `maras_domain_*`) — Add `test_<module>.cpp` and register it under `add_executable(maras_tests ...)` in `SKSE_Source/CMakeLists.txt`.

## 2. Authoring & `LNK2019` Link-Gate Rules

- **Framework (`TestHarness.h`)**: Define cases with `MARAS_TEST(Suite, Name)` and assert with `MARAS_CHECK`, `MARAS_REQUIRE`, `MARAS_CHECK_EQ`, `MARAS_REQUIRE_EQ`.
- **100% Hermetic & Deterministic**: Use `MockFormIdResolver`, `IActorMetadataProvider` stubs, and deterministic `MARAS::Foundation::IRng` fakes. Zero `RE/`/`SKSE/`/`PCH.h`/`nlohmann`/`yaml-cpp`/`spdlog` includes, zero sleeps, zero `std::random_device`, zero external files.
- **Physical Link Gate**: `maras_tests` links **only** pure `STATIC` modules (`maras_foundation`, `maras_state`, and future `maras_projection` / `maras_domain_*`) so any upward engine dependency fails immediately with `LNK2019`.
- **Build & Run**: `powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target tests` (append `-Filter "<Substring>"` to scope).
