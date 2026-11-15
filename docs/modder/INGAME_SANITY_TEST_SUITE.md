# M.A.R.A.S — In-Game Sanity & Config Verification Suite

The in-game sanity suite verifies live Skyrim SE engine integration, Papyrus $\leftrightarrow$ C++ bindings, UI Intrigue Mode masking, and `'MARS'` v7 SKSE co-save serialization round-trips end-to-end inside a real game session—complementing the **113** hermetic pure-C++ unit tests (**2451** assertions) in `maras_tests.exe` ([SKSE_Source/tests/AGENTS.md](../../SKSE_Source/tests/AGENTS.md)).

---

## 1. Suite Components (Zero `.esp` Changes)

| Component | Path | Role |
| :--- | :--- | :--- |
| **Papyrus Sanity Runner** | [`Scripts/Source/TTM_SanitySuite.psc`](../../Scripts/Source/TTM_SanitySuite.psc) | Global test scenarios (`PrepEnvironment`, `RunScenario1`–`RunScenario4PostLoad`, `RunAllPreSave`) using deterministic vanilla actors (`Skyrim.esm` / `HearthFires.esm`) with zero `TT_MARAS.esp` quest edits. |
| **Diagnostic Dump Native** | [`MARAS.DumpStoredData`](../../Scripts/Source/MARAS.psc) | Scriptable Papyrus $\to$ `IUiBridgeGateway` $\to$ [`UIDataBridge`](services/UIDataBridge.md) entrypoint writing tagged snapshots (`maras_dump_<tag>_<YYYY-MM-DD_HH-MM-SS>.json`) under `Data/SKSE/Plugins/MARAS/dumps/`. |
| **Console Batch Scripts** | [`tools/ingame_sanity/bat/`](../../tools/ingame_sanity/bat/) | Short console `.txt` commands (`maras_prep`, `maras_s1`, `maras_s2`, `maras_s3`, `maras_s4_pre`, `maras_s4_post`, `maras_all_pre`). |
| **Batch Installer** | [`tools/ingame_sanity/install-bats.ps1`](../../tools/ingame_sanity/install-bats.ps1) | Copies `bat/*.txt` into a target Skyrim root directory (`SkyrimSE.exe` / `SkyrimVR.exe`). |
| **Offline Deterministic Verifier** | [`tools/ingame_sanity/verify-maras-sanity.ps1`](../../tools/ingame_sanity/verify-maras-sanity.ps1) | Validates shipped configs, checks all S1–S4 dump assertions, compares pre-save vs. post-load co-save parity, scans `MARAS.log` for `[error]`/`[warn]`, and emits a Markdown judge digest. |

---

## 2. In-Game Quickstart (Clean Save Runbook)

1. **Ensure Latest Build & Compiled Scripts Are Deployed**:
   - C++ plugin built (`SKSE/Plugins/MARAS.dll`) via [build-and-test.ps1](../../.agents/skills/maras-build-and-test/scripts/build-and-test.ps1).
   - Papyrus scripts compiled (`Scripts/MARAS.pex` and `Scripts/TTM_SanitySuite.pex`) via `skyrimse.ppj`.
2. **Install Console Batch Scripts to Skyrim Root** (same folder as `SkyrimSE.exe`):
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/install-bats.ps1" -SkyrimRoot "<Path\To\Skyrim Special Edition>"
   ```
3. **Load a Clean Save in Skyrim SE**:
   - Open the console (`~`) and run:
     ```text
     bat maras_all_pre
     ```
4. **Close the Console for 2+ Seconds**:
   - Closing the console lets `coc WhiterunBreezehome` settle and allows `Utility.Wait(2.0)` inside `RunScenario4PreSave` to yield unpaused main-thread frames so `DeathEventHandler` processes Sven's `TESDeathEvent` via `AddCurrentEpochTask`.
   - Wait for the HUD notification: `[MARAS Sanity] S4 pre-save ready - Save & Reload now!`.
5. **Save & Reload**:
   - Create a manual save, then immediately load that save (or relaunch Skyrim and load it to test cold `'MARS'` v7 co-save loading).
6. **Capture the Post-Load Dump**:
   - Open the console (`~`) and run:
     ```text
     bat maras_s4_post
     ```
7. **Run the Deterministic Offline Verifier**:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" `
     -DumpsDir "<Path\To\Data\SKSE\Plugins\MARAS\dumps>" `
     -LogPath "<Path\To\Documents\My Games\Skyrim Special Edition\SKSE\MARAS.log>" `
     -ReportPath "sanity_digest.md"
   ```
   - Exit code `0` means all checks passed; exit code `1` prints exact failed assertions and log errors.
   - To request an AI/human review, attach the generated `sanity_digest.md` alongside `MARAS.log` and the `maras_dump_*.json` files.

---

## 3. Scenario Coverage Matrix (`S1`–`S4` & `configs`)

You can run the full chain via `bat maras_all_pre` or run scenarios one by one after `bat maras_prep`:

| Batch File | Papyrus Entrypoint | Emitted Dump Tag(s) | Key Invariants Verified by `verify-maras-sanity.ps1` |
| :--- | :--- | :--- | :--- |
| `maras_prep.txt` | `TTM_SanitySuite.PrepEnvironment` | `prep_baseline` | Enables debug log (`SetLogLevel(1)`), registers Breezehome (`0001F86F` / marker `0001F88E`), and positions vanilla test actors (`Ysolda` `0001A69A`, `Lydia` `000A2C94`, `Mikael` `0001A671`, `Carlotta` `0001A675`, `Camilla` `00013488`, `Sven` `00013491`, `Faendal` `0001348C`, child `Lucia`/`Braith`). |
| `maras_s1.txt` | `TTM_SanitySuite.RunScenario1` | `s1_household` | `meta.serializationVersion == 7`; `enablePolygamy == true`; `(Player, Ysolda)` & `(Player, Lydia)` Married; hierarchy `0=Ysolda`, `1=Lydia`; permanent affection `85`/`72` + daily `gift == 5`; Breezehome tenants (`Ysolda` `stayAtHome=true`, `Lydia` `stayAtHome=false`, child `Lucia`/`Braith`). |
| `maras_s2.txt` | `TTM_SanitySuite.RunScenario2` | `s2_masked`, `s2_omniscient` | `(Camilla, Sven)` w/ secret Physical (`stage=1`) `Faendal` affair + `(Player, Ysolda)` w/ secret Talking (`stage=0`) `Carlotta` affair: verifies `s2_masked` hides **both** affairs (zero `0x14` Player exemption in `PerspectiveView::ForDashboard`), `s2_omniscient` exposes both stages, and opening/closing `(Player, Lydia)` transitions `(Lydia, Mikael)` to `ExLover`. |
| `maras_s3.txt` | `TTM_SanitySuite.RunScenario3` | `s3_masked`, `s3_omniscient`, `s3_partial_masked`, `s3_partial_omniscient` | `RegisterUnbornChild` $\to$ `PromoteUnbornToInfant("SanityInfant")` + `SetPaternitySecretEdges(infantId, true, true)`: confirms masked view substitutes presumed father `Sven` (`00013491`) and omits secret flags while omniscient view exposes `Faendal` (`0001348C`); `ResolvePaternitySecretForBioFather` clears only the bio-father edge while keeping the partner edge secret. |
| `maras_s4_pre.txt` + Save/Reload + `maras_s4_post.txt` | `RunScenario4PreSave` / `RunScenario4PostLoad` | `s4_pre_save_masked`, `s4_pre_save_omniscient`, `s4_post_load_masked`, `s4_post_load_omniscient` | `Breakup(Player, Lydia)` $\to$ `Divorced` & Breezehome tenant eviction; `Sven.KillSilent()` $\to$ `deceasedRegistry` w/ Camilla `Married` at death; full `'MARS'` v7 co-save parity across relationships, hierarchy, affection, houses, children, and deceased state before save vs. after reload. |
| *(offline)* | `-Scenario configs` | *(n/a)* | Validates UTF-8 + syntax/schema keys for all shipped `.json`, `.ini`, and `.yaml` files under `SKSE/Plugins/MARAS/` and `SKSE/Plugins/SkyrimNet/external/tetherball88.maras/`. |

---

## 4. Offline Verifier CLI (`verify-maras-sanity.ps1`)

```powershell
# Run verifier self-test (validates positive & tampered S1-S4 fixtures + shipped configs in temp folder)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -SelfTest

# Validate shipped configs only (offline CI/dev check)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -Scenario configs

# Validate full in-game run + MARAS.log and write Markdown judge digest
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" `
  -Scenario all `
  -DumpsDir "<Path\To\Data\SKSE\Plugins\MARAS\dumps>" `
  -LogPath "<Path\To\SKSE\MARAS.log>" `
  -ReportPath "sanity_digest.md"
```

| Parameter | Default | Description |
| :--- | :--- | :--- |
| `-Scenario` | `all` | Scope to check: `all`, `configs`, `s1`, `s2`, `s3`, or `s4`. |
| `-DumpsDir` | `<RepoRoot>\SKSE\Plugins\MARAS\dumps` | Directory containing `maras_dump_<tag>_*.json` files. Uses the newest dump matching each tag. |
| `-LogPath` | *(empty)* | Optional path to `MARAS.log`. Fails on any `[error]` line and surfaces `[warn]` highlights in the digest. |
| `-RepoRoot` | Resolved from `$PSScriptRoot\..\..` | Repository root used when scanning shipped JSON/INI/YAML configs. |
| `-ReportPath` | *(empty)* | Optional output path to save the Markdown verification digest. |
| `-SelfTest` | `$false` | Creates temporary valid + tampered S1–S4 fixtures and clean/error logs, verifies pass + fault detection, cleans up temp files, and exits `0`/`1`. |
