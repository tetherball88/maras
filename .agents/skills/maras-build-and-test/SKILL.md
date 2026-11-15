---
name: maras-build-and-test
description: >-
  Build MARAS.dll, compile and run maras_tests.exe headless C++ unit tests, or build the Prisma UI frontend without path-hunting. Activate whenever compiling C++, running C++ unit tests, adding new C++ source/test files, or building prisma-src.
---

# M.A.R.A.S Build & Test Runbook

Never search the filesystem for `cmake.exe`, Visual Studio, `vcpkg`, or `CommonLibSSE`. Use the exact one-command pipeline below from the workspace root (`e:\Skyrim\development\mods\M.A.R.A.S Dev`).

## 1. C++ Plugin (`MARAS.dll`) & Headless Unit Tests (`maras_tests.exe`)

Run [build-and-test.ps1](./scripts/build-and-test.ps1):

```powershell
# Full verification: header-hygiene check + build maras_tests + build MARAS.dll + run all tests
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target all

# Fast inner loop (~1-2s): pure-module header check + build & run maras_tests.exe ONLY (never touches MARAS.dll)
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target tests

# Run a filtered subset of unit tests (substring match on SuiteName.TestName)
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target tests -Filter "AffairLedger"

# Force CMake reconfigure (use after editing SKSE_Source/CMakeLists.txt or adding new files)
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target all -Reconfigure
```

### Underlying Toolchain Paths (Reference Only)
- **CMake binary:** `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`
- **Required env vars:**
  - `COMMONLIB_SSE_FOLDER = E:/Skyrim/development/mods/_Sources/CommonLibSSE-NG`
  - `VCPKG_ROOT = C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg`
- **Build outputs (`release` preset):**
  - Plugin DLL: `SKSE_Source/build/release/Release/MARAS.dll` (copied post-build to `SKSE/Plugins/MARAS.dll`)
  - Unit Test Binary: `SKSE_Source/build/release/Release/maras_tests.exe`

### Locked Target DLL Behavior (Important)
When `SkyrimSE.exe` is running, Windows locks `SKSE/Plugins/MARAS.dll`.
- Compiling and linking `SKSE_Source/build/release/Release/MARAS.dll` still succeeds, and **only** the CMake `POST_BUILD` `copy_if_different` step (`MSB3073` / `Error copying file`) fails.
- `build-and-test.ps1` detects this automatically, prints a `[WARN]`, and exits `0`.
- **Never** retry the build in a loop and **never** terminate `SkyrimSE.exe`—simply note to the user that compilation succeeded and deployment copy to `SKSE/Plugins/MARAS.dll` awaits closing Skyrim.

## 2. Prisma UI Dashboard (`prisma-src/`)

```powershell
Set-Location prisma-src; npm run build
```
Emits production bundle (`base: './'`) with unbundled, user-editable `variables.css`.

## 3. In-Game Sanity & Shipped Config Verifier (`tools/ingame_sanity/`)

```powershell
# Verifier self-test (temp synthetic fixtures + fault injection + shipped configs)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -SelfTest

# Validate all shipped JSON/INI/YAML config files offline
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -Scenario configs

# Verify real in-game S1-S4 dumps + MARAS.log and emit Markdown digest
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/ingame_sanity/verify-maras-sanity.ps1" -Scenario all -DumpsDir "<Path\To\Data\SKSE\Plugins\MARAS\dumps>" -LogPath "<Path\To\MARAS.log>" -ReportPath "sanity_digest.md"
```
See [INGAME_SANITY_TEST_SUITE.md](../../../docs/modder/INGAME_SANITY_TEST_SUITE.md) for the console `bat` workflow (`TTM_SanitySuite.psc`).
