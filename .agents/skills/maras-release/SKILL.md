---
name: maras-release
description: >-
  Prepare, bump, and verify a new M.A.R.A.S version release (vX.Y.Z or vX.Y.Z.alpha). Activate when asked to prepare a release, bump version numbers, generate or finalize CHANGELOG.md for a release, audit release readiness (MARAS.dll, maras_tests, Magelight UI, Papyrus .pex, localization, shipped configs, requirements), or prepare GitHub/Nexus release notes.
---

# M.A.R.A.S Version Release Runbook

Use this workflow whenever preparing a new M.A.R.A.S release (`vX.Y.Z` or pre-release `vX.Y.Z.alpha`).

## How M.A.R.A.S Releases Work

- **Main Release (`.github/workflows/main-release.yml`)**:
  - Pushing a `v*` tag (`.\tag-push.bat vX.Y.Z` or `.\tag-push.bat vX.Y.Z.alpha`) triggers [.github/workflows/main-release.yml](../../../.github/workflows/main-release.yml) on `ubuntu-latest`.
  - The workflow **does not compile code in CI**; it directly packages committed `Magelight/`, `Scripts/`, `SKSE/`, `Seq/`, `sound/`, `TT_MARAS.esp`, and `*.ini` (plus `interface/` and `Strings/` if present) into `out/MARAS_${TAG}.zip`, writing `${TAG}` into `SKSE/Plugins/MARAS/version.txt` and shipping **only** `SKSE/Plugins/MARAS/localization/en/` (excluding `*.pdb` and `DynamicStringDistributor*`).
  - It also runs `python3 localization/scripts/package_translations_fomod.py` to build `out/MARAS_Translations_${TAG}.zip` and publishes both archives to the GitHub Release for `${TAG}`.
  - Every compiled artifact (`SKSE/Plugins/MARAS.dll`, `Magelight/MARAS/views/MARAS/*`, and `Scripts/*.pex`) **must** be up to date and committed before tagging.
- **Standalone Patches Release (`.github/workflows/patches-release.yml`)**:
  - Pushing an updated `patches/version.txt` to `master` packages `patches/` into `MARAS_patches_<version>.zip` and publishes `patches-<version>`.
- **Standalone Translations Release (`.github/workflows/translations-release.yml`)**:
  - Pushing a `translations-*` tag (or running `workflow_dispatch`) builds and publishes `MARAS_Translations_<version>.zip` independently of a main release.

---

## Release Preparation Steps

### 1. Inspect Changes Since Previous Release

1. Find the latest release tag and review everything queued since that tag:
   ```powershell
   git tag --list "v*" --sort=-creatordate | Select-Object -First 5
   git log <prev_tag>..HEAD --stat
   git status -s
   ```
2. Determine the target version tag `vX.Y.Z` or `vX.Y.Z.alpha` (e.g., `v2.6.0` or `v2.7.0.alpha`).

### 2. Bump Version Metadata

Run [prepare_release.ps1](./scripts/prepare_release.ps1) with `-ApplyVersion`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-release/scripts/prepare_release.ps1" -Version vX.Y.Z -ApplyVersion
```

This updates version strings across:
- `SKSE/Plugins/SkyrimNet/external/tetherball88.maras/manifest.json` (`"version": "X.Y.Z"` — strictly numeric `X.Y.Z`; **never** append `.alpha` or pre-release suffixes, even for `vX.Y.Z.alpha` releases)
- `SKSE_Source/CMakeLists.txt` (`project(MARAS VERSION X.Y.Z LANGUAGES CXX)`)
- `SKSE_Source/version.rc` (`FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, `ProductVersion`)
- `SKSE_Source/modules/foundation/include/maras/foundation/VersionInfo.h` (`kCompiledModVersion`)
- `SKSE/Plugins/MARAS/version.txt` (`vX.Y.Z` / `vX.Y.Z.alpha`)

### 3. Update `CHANGELOG.md` & Audit Requirements

1. Prepend a `## X.Y.Z - YYYY-MM-DD` (or `## X.Y.Z.alpha - YYYY-MM-DD`) section (`### Added`, `### Changed`, `### Fixed`) above the previous release in `CHANGELOG.md`, matching existing player-facing style (bold lead-ins and concise descriptions without internal C++ jargon).
2. Inspect commits since `<prev_tag>` for added, removed, or bumped dependencies and integrations:
   - Core dependencies (`SKSE64`, `Address Library`, `Magelight UI`, `SPID`, `PapyrusUtil SE`, `po3 Papyrus Extender`, `SkyUI` / `MCM`).
   - Optional integrations (`SkyrimNet`, `Fertility Mode Reloaded`, `Beeing Female NG`, `OStim Standalone`, `SexLab`, `Dynamic String Distributor` for translations).
3. Update `README.md`, `NEXUS_DESCRIPTION.bbcode`, and any affected `docs/player/` or `docs/modder/` pages (activate [maras-doc-sync](../maras-doc-sync/SKILL.md) if mechanics/settings/APIs changed), and note any updates needed in the **Nexus Mods Requirements** widget.

### 4. Rebuild Compiled Binaries, Run Tests & Verify Configs/Localization

1. Activate [maras-build-and-test](../maras-build-and-test/SKILL.md) and rebuild using `-Reconfigure` so updated `version.rc`, `CMakeLists.txt`, and `VersionInfo.h` metadata are compiled into `MARAS.dll` and verified against `maras_tests.exe`:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target all -Reconfigure
   ```
2. Verify `SkyrimSE.exe` is closed before final commit/tag so `SKSE/Plugins/MARAS.dll` receives the post-build copy (a locked DLL leaves `SKSE/Plugins/MARAS.dll` stale on disk).
3. Rebuild the Magelight UI bundle if `ui-src/` or `SKSE/Plugins/MARAS/localization/en/ui_*.json` changed since `<prev_tag>`:
   ```powershell
   Set-Location ui-src; npm run build
   ```
4. Validate shipped configs and English localization / DSD parity:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -Scenario configs
   python localization/scripts/export_dsd_template.py --check
   python localization/scripts/sync_localization.py --lang en --strict
   ```
5. **Papyrus scripts (`.psc` -> `.pex`)**: `prepare_release.ps1` performs a read-only check for missing or stale `.pex` files. Do **not** compile Papyrus unless explicitly requested; if any `.psc` files were modified without recompiled `.pex` counterparts, list those exact scripts so the user can compile them via `skyrimse.ppj` (`pyro: Compile Project`) before tagging.

### 5. Run Full Pre-Release Audit

Re-run [prepare_release.ps1](./scripts/prepare_release.ps1) in audit mode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-release/scripts/prepare_release.ps1" -Version vX.Y.Z
```

Confirm all automated checks report `[PASS]` (or surface any remaining `[FAIL]` items such as uncompiled `.pex` files, stale UI bundle, or a locked `MARAS.dll`).

### 6. Hand-off Checklist (Never Auto-Commit or Auto-Tag)

Do **not** commit, push, or run `tag-push.bat` unless the user explicitly asks you to. Instead, provide:
1. The `prepare_release.ps1` audit summary (`PASS` / `WARN` / `FAIL`) plus `maras_tests.exe`, config verifier, and localization check results.
2. Any manual actions required before tagging (compiling flagged `.pex` files, closing `SkyrimSE.exe` if DLL copy was blocked, updating translation packs if English strings changed, updating Nexus Mods description/requirements, or bumping `patches/version.txt` if `patches/` changed).
3. Suggested release commit & tag commands:
   ```powershell
   git commit -m "release vX.Y.Z"
   git push
   .\tag-push.bat vX.Y.Z
   ```
