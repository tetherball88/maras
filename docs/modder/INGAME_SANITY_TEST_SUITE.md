# M.A.R.A.S — In-Game Sanity & Config Verification Suite

The in-game sanity suite verifies live Skyrim SE engine integration, machine-code trampoline detours, Papyrus $\leftrightarrow$ C++ bindings, UI Intrigue Mode masking, dynamic `0xFF` runtime actors/markers, unloaded-cell actor preservation, `'CFSN'`/`'SPAS'` engine state restoration, and `'MARS'` v7 SKSE co-save serialization round-trips end-to-end inside a real game session—complementing the headless pure-C++ unit tests in `maras_tests.exe` ([SKSE_Source/tests/AGENTS.md](../../SKSE_Source/tests/AGENTS.md)).

---

## 1. Suite Components (Zero `.esp` Changes, Zero Extra Files)

| Component | Path | Role |
| :--- | :--- | :--- |
| **Native Console Command** | [`SKSE_Source/modules/bridge_papyrus/src/SanityConsoleCommand.cpp`](../../SKSE_Source/modules/bridge_papyrus/src/SanityConsoleCommand.cpp) | Built-in `maras_sanity` (short alias: `msanity`) console command registered directly by `MARAS.dll`; cleans up previous dynamic/synthetic sanity test children (`SanityAdultFF`, `SanityOrphanFF`, `SanityTrainee`) before `prep`/`all_pre`, marks `SanityOrphanFF` deleted after `s4_pre`/`all_pre` to test reload orphan pruning, and runs `RollForcegreetForHousehold()` + dumps `s4_post_forcegreet` after `s4_post`. |
| **Papyrus Sanity Runner** | [`Scripts/Source/TTM_SanitySuite.psc`](../../Scripts/Source/TTM_SanitySuite.psc) | Global test scenarios (`PrepEnvironment`, `RunScenario5`, `RunScenario1`–`RunScenario4PostLoad`, `RunAllPreSave`) using deterministic vanilla actors (`Skyrim.esm` / `HearthFires.esm`) with zero `TT_MARAS.esp` quest edits. |
| **Read-Only Engine Probe** | [`SKSE_Source/modules/bridge_ui/src/SanityEngineProbe.cpp`](../../SKSE_Source/modules/bridge_ui/src/SanityEngineProbe.cpp) | Emits `root["engineProbe"]` inside [`UIDataBridge::DumpStoredData`](services/UIDataBridge.md) (`TESGlobal` values, `PlayerCharacter` bonus perks & `PlayerMarriedFaction`/`CanFormNonCheatingMainFaction` state, vanilla `RelationshipMarriage`/`RelationshipMarriageBreakUp` quest state & `LoveInterest` alias, live `RE::Actor` faction ranks & vanilla relationship rank to player, `Variable06`/`Variable07` orders, `0xFF` home `XMarker` links, runtime-cloned `TESNPC` adult-child bases, `'CFSN'` snapshots, and live cell/bed ownership) with strict CQS and `PerspectiveView` secret masking. |
| **Offline Deterministic Verifier** | [`tools/ingame_sanity/verify-maras-sanity.ps1`](../../tools/ingame_sanity/verify-maras-sanity.ps1) | Validates shipped JSON/INI/YAML configs, SkyrimNet `.prompt` hygiene & C++ $\leftrightarrow$ evaluation-prompt context-key contracts, checks all S1–S5 domain & `engineProbe` assertions, compares pre-save vs. post-load co-save parity + post-load engine restoration (`s4_post_restore` + `s4_post_forcegreet`), enforces `0 [error]` and `0 unexpected [warning]` lines in `MARAS.log`, and emits a Markdown judge digest. |

---

## 2. In-Game Quickstart (Copy-Paste Console Commands)

> **Tip**: Every `maras_sanity <step>` command also has the short alias **`msanity <step>`** (and typing bare `maras_sanity` in-game prints usage help in the console).

1. **Load a Clean Save in Skyrim SE** (with `MARAS.dll`, `MARAS.pex`, and `TTM_SanitySuite.pex` deployed).
2. **Run Pre-Save Scenarios (`Prep` + `S1`–`S4`)**:
   - Open the console (`~`) and run:
     ```text
     coc WhiterunBreezehome
     maras_sanity all_pre
     ```
     *(Note: `coc WhiterunBreezehome` puts you in an interior cell so distant actors like `Niranye` in Windhelm remain 3D-unloaded.)*
3. **Close the Console (`~`) for 3–5 Seconds**:
   - Closing the console unpauses the Papyrus VM, finishes the cell transition, lets `Utility.Wait(2.0)` inside `RunScenario4PreSave` process Sven's `TESDeathEvent`, and marks `SanityOrphanFF` deleted for co-save pruning verification.
   - Wait for the top-left HUD notification:
     > `[MARAS Sanity] S4 pre-save ready - Save & Reload now!`
4. **Save & Reload**:
   - Create a **new manual save**, then immediately load that save *(or relaunch Skyrim and load it to test cold `'MARS'` v7 co-save loading)*.
5. **Capture the Post-Load & Engine-Restore Dumps**:
   - Open the console (`~`), run:
     ```text
     maras_sanity s4_post
     ```
   - **Close the console (`~`) for 2 seconds** so the Papyrus VM captures `s4_post_load_*`, runs the post-load convergence/restore steps (`MoveTo` on unloaded `Niranye`, `RemoveChild(Braith)` `'CFSN'` restore, `StopShareHouseWithPlayer(Ysolda)` `'SPAS'` restore), writes `s4_post_restore`, and displays:
     > `[MARAS Sanity] S4 post-load & restore dumps complete!`
6. **Run the Deterministic Offline Verifier** (from repo root):
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" `
     -Scenario all `
     -DumpsDir "<Path\To\SKSE\Plugins\MARAS\dumps>" `
     -LogPath "$env:USERPROFILE\Documents\My Games\Skyrim Special Edition\SKSE\MARAS.log" `
     -ReportPath "sanity_digest.md"
   ```
   - **MO2 Note**: New runtime dumps are written to `<MO2>\overwrite\SKSE\Plugins\MARAS\dumps` (unless a dedicated output mod is configured); direct `Data/` installs write to `<Skyrim Special Edition>\Data\SKSE\Plugins\MARAS\dumps`.
   - Exit code `0` means all checks passed; exit code `1` prints exact failed assertions and unexpected log warnings/errors.

---

## 3. Direct Console Commands & Covered Engine Integration Edge Cases

| Console Command | Short Alias | Dispatched Target | Emitted Dump Tag(s) | Key Invariants Verified |
| :--- | :--- | :--- | :--- | :--- |
| `maras_sanity` *(or `maras_sanity help`)* | `msanity` | *(prints usage in console)* | *(none)* | Lists all valid subcommands directly in the Skyrim console. |
| `maras_sanity dump [tag]` | `msanity dump [tag]` | `UIDataBridge::DumpStoredData` | `<tag>` *(default: `console_manual`)* | Writes an immediate C++ JSON snapshot (including `engineProbe`) and prints the output path. |
| `maras_sanity migration_test` | `msanity migration_test` | `MigrationDiagnosticsCoordinator::TriggerPreviewFailureForTesting` | *(writes `MARAS_MigrationReport.log`)* | Non-destructively previews the standalone `save_migration_failed` modal and writes a labeled synthetic `MARAS_MigrationReport.log` without altering `WorldStore`. |
| `maras_sanity prep` | `msanity prep` | `TTM_SanitySuite.PrepEnvironment` | `prep_baseline` | Enables debug log (`SetLogLevel(1)`), cleans prior dynamic `0xFF` test children & shared houses, registers Breezehome (`0001F86F` / marker `0001F88E`), prepares local Whiterun/Riverwood actors, and resets `Niranye` (`0001B11C`) in Windhelm **without** `MoveTo` so her cell stays unloaded. |
| `maras_sanity s1` | `msanity s1` | `TTM_SanitySuite.RunScenario1` | `s1_household` | `meta.serializationVersion == 7`; polygamy + hierarchy (`0=Ysolda`, `1=Lydia`) + affection (`85`/`72`, daily `gift == 5`) + Breezehome tenants (`Ysolda`, `Lydia`, `Lucia`); **Engine Mirrors**: live `TESGlobal`s (`SpousesCount == 2`, `LoveInterestsCount == 2`, `PlayerHousesCount >= 1`, `EnablePolygamy == 1`), `BuffReconciler` active `BGSPerk`s (`>= 2`) on `PlayerCharacter`, `PlayerMarriedFaction` kept absent on `PlayerCharacter` under polygamy, and live MARAS/vanilla faction ranks on `Ysolda` & `Lydia`. |
| `maras_sanity s2` | `msanity s2` | `TTM_SanitySuite.RunScenario2` | `s2_masked`, `s2_omniscient`, `s2_caught` | `(Camilla, Sven)` w/ secret `Faendal` affair + `(Player, Ysolda)` w/ secret `Carlotta` affair: verifies `s2_masked` hides **both** affairs (zero `0x14` Player exemption in `PerspectiveView::ForDashboard`), `s2_masked.engineProbe` masks secret paramour `Carlotta` (`trackedRank <= 0` Candidate/hidden, `spouseCheatingRank == -2`, `loveInterestsCount == 2`) and confirms Player `canFormNonCheatingMainRank == 0` after closing `(Player, Lydia)` (`(Lydia, Mikael)` $\to$ `ExLover`), `s2_omniscient` exposes both affairs, and `s2_caught` confirms `SetCheatingDiscovered(..., "caught", player)` reveals `Lydia` $\to$ `Faendal` (`isSecret == false`, `Ongoing`, `spouseCheatingRank == 1`) even in Intrigue Mode. |
| `maras_sanity s3` | `msanity s3` | `TTM_SanitySuite.RunScenario3` | `s3_masked`, `s3_omniscient`, `s3_partial_masked`, `s3_partial_omniscient` | `RegisterUnbornChild` $\to$ `PromoteUnbornToInfant("SanityInfant")` + `SetPaternitySecretEdges(infantId, true, true)`: confirms masked view substitutes presumed father `Sven` (`00013491`) and omits secret flags while omniscient view exposes `Faendal` (`0001348C`); `ResolvePaternitySecretForBioFather` clears only the bio-father edge while keeping the partner edge secret. |
| `maras_sanity s4_pre` $\to$ Save/Reload $\to$ `maras_sanity s4_post` | `msanity s4_pre` / `msanity s4_post` | `RunScenario4PreSave` / `RunScenario4PostLoad` (+ C++ forcegreet callback) | `s4_pre_save_masked`, `s4_pre_save_omniscient`, `s4_post_load_masked`, `s4_post_load_omniscient`, `s4_post_restore`, `s4_post_forcegreet` | **(1) `Hook_RemoveFromFaction` + Rank-0 Hierarchy Shift**: places `Lydia` at rank `0` and `Ysolda` at rank `1`, then external `lydia.RemoveFromFaction(PlayerMarriedFaction)` auto-demotes `(Player, Lydia)` to `Divorced`, shifts `Ysolda` from rank `1` $\to$ `0` in both `spouseHierarchy` and live `HierarchyFaction`, evicts Lydia from Breezehome, strips MARAS/marriage factions, and updates `SpousesCount == 1` + Player bonus perks across reload; **(2) Dynamic `0xFF` Child + Cloned `TESNPC` + Stage-3 Training + Orphan Pruning**: persistent Stage 4 `SanityAdultFF` (`PlaceAtMe`) gets a runtime-cloned `RE::TESNPC` (`actorBase != originalAdultBase`) reasserted post-load, `SanityTrainee` preserves `stage == training`, `trainingLocation == "College of Winterhold"`, `trainingClassIndex >= 0`, and `trainingStartGameDay > 0` across reload, `nextSyntheticChildId` stays in `0xFF700000`–`0xFF7FFFFF`, and deleted `SanityOrphanFF` (`0xFF`) is pruned cleanly on reload; **(3) Unloaded Placed NPC (`0001B11C` Niranye)**: paired as `Dating` while `is3DLoaded == false` & `parentCellAttached == false`, preserved across reload without pruning, then moved to Player in `s4_post_restore` with 3D + MARAS factions converged; **(4) `0xFF` Home `XMarker`s, `Variable06` Orders, `'CFSN'` Restore & Daily Forcegreet**: `GetTrackedNpcHomeMarker(ysolda)` & `GetChildHomeMarker(lucia)` resolve & link (`0x6B`) across reload, `IssueChildOrder(lucia, 2.0, 12.0)` persists `Variable06 == 2.0`, `Braith` (`0001A66B`) captures `'CFSN'` pre-save, clears external factions while housed, and restores original Whiterun factions + crime faction `000267EA` upon `RemoveChild(braith)` post-load, and `s4_post_forcegreet` confirms `RollForcegreetForHousehold()` sets `forcegreetPending == true` and live `Variable07 == 1.0` on `Lucia`; **(5) `'SPAS'` Shared Cell & Bed Ownership**: `ShareHouseWithPlayer(ysolda)` sets `Ysolda's House` (`000165AC`) `public=true` in `'SPAS'` (plus `PlayerFaction` `00000DB1` bed ownership when interior bed refs are loaded) across reload, and `StopShareHouseWithPlayer(ysolda)` clears `'SPAS'` and restores `public=false` (+ original bed owner when loaded); **(6) Death Cascade & Co-Save Parity**: `Sven.KillSilent()` $\to$ `deceasedRegistry` + preserved `presumedFather` on `SanityInfant`, with full `'MARS'` v7 pre-save vs. post-load parity. |
| `maras_sanity s5` | `msanity s5` | `TTM_SanitySuite.RunScenario5` | `s5_engaged`, `s5_married`, `s5_child_ops` | **Vanilla Engagement & SkipWedding Lifecycle + Sole-Player Child Eviction**: engages rank-0 `Carlotta` (`0001A675`) with `skipWedding == false`, confirming `RelationshipMarriage` (`00074793`) reaches Stage `20` with `LoveInterest == 0001A675`, `RelationshipMarriageBreakUp` (`0007431B`) remains idle at Stage `0` with empty `LoveInterest`, and `vanillaRelationshipRankToPlayer >= 3`; transitions `Engaged` $\to$ `Married` (`SkipWedding`), confirming `RelationshipMarriage` resets below Stage `20` with cleared `LoveInterest`, `RelationshipMarriageBreakUp` remains idle at Stage `0`, and `vanillaRelationshipRankToPlayer == 4`; unregisters `Carlotta` and verifies `RemoveTenantFromPlayerHouse(braith)` succeeds for a sole-player child (`CanRemoveTenant`), then cleans up before `S1`. |
| `maras_sanity all_pre` | `msanity all_pre` | `TTM_SanitySuite.RunAllPreSave` | All `prep` + `s5` + `s1`–`s4_pre` tags | Runs `PrepEnvironment` $\to$ `RunScenario5` $\to$ `RunScenario1` $\to$ `RunScenario2` $\to$ `RunScenario3` $\to$ `RunScenario4PreSave` in one call. |

---

## 4. Offline Verifier CLI & Strict Log Audit (`verify-maras-sanity.ps1`)

```powershell
# Run verifier self-test (validates positive & tampered S1-S4/engineProbe fixtures + strict log checks in temp folder)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -SelfTest

# Validate shipped configs only (offline CI/dev check, no game needed)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -Scenario configs

# Validate full in-game run + MARAS.log and write Markdown judge digest
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" `
  -Scenario all `
  -DumpsDir "<Path\To\SKSE\Plugins\MARAS\dumps>" `
  -LogPath "$env:USERPROFILE\Documents\My Games\Skyrim Special Edition\SKSE\MARAS.log" `
  -ReportPath "sanity_digest.md"
```

| Parameter | Default | Description |
| :--- | :--- | :--- |
| `-Scenario` | `all` | Scope to check: `all`, `configs`, `s1`, `s2`, `s3`, `s4`, or `s5`. |
| `-DumpsDir` | `<RepoRoot>\SKSE\Plugins\MARAS\dumps` | Directory containing `maras_dump_<tag>_*.json` files. Uses the newest dump matching each tag. |
| `-LogPath` | *(empty)* | Optional path to `MARAS.log`. **Fails** on any `[error]` line **and** on any unexpected `[warning]` line (whitelisting only benign missing optional override-folder notes for `Data/SKSE/Plugins/MARAS/spousesTypes` and `Data/SKSE/Plugins/MARAS/questEvents`). |
| `-RepoRoot` | Resolved from `$PSScriptRoot\..\..` | Repository root used when scanning shipped JSON/INI/YAML configs, SkyrimNet `.prompt` templates, and C++ prompt producers. |
| `-ReportPath` | *(empty)* | Optional output path to save the Markdown verification digest. |
| `-SelfTest` | `$false` | Creates temporary valid + tampered S1–S5/`engineProbe`/prompt-contract fixtures and clean/error/warning logs, verifies pass + fault detection across 10 self-test cases, cleans up temp files, and exits `0`/`1`. |
