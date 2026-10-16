# M.A.R.A.S — Comprehensive C++ Codebase Audit (`SKSE_Source/`)

This document provides an exhaustive, file-by-file and section-by-section static analysis of the C++ SKSE plugin (`SKSE_Source/`). Every header (`.h`) and implementation (`.cpp`) file is analyzed individually across three categories:

1. **🔴 Critical Issues / Breaking Bugs** — Crashes, undefined behavior (UB), data races, deadlocks, memory/reference lifetime bugs, co-save corruption risks, logic bugs, and violations of project architecture rules (`AGENTS.md`).
2. **🟡 What Can Be Improved** — Performance bottlenecks, redundant I/O or lookups, missing edge-case guards, error handling gaps, and suboptimal data structures.
3. **🔵 What Can Be Refactored / Organized Better** — Code duplication, inconsistent abstractions, coupling, magic numbers, dead code, and structural organization.

---

## Section 1: Entry Point, Public C API & Serialization Constants

### 1.1 `PCH.h` ✅ Resolved
*File: [PCH.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/PCH.h)*

- **🟡 What Can Be Improved**:
  - ✅ **Minimal precompiled header utilization**: Added all standard library (`<algorithm>`, `<filesystem>`, `<format>`, `<mutex>`, `<shared_mutex>`, `<string>`, `<string_view>`, `<unordered_map>`, `<unordered_set>`, `<vector>`, etc.) and third-party (`<nlohmann/json.hpp>`, `<spdlog/common.h>`, `<spdlog/spdlog.h>`, `<spdlog/sinks/basic_file_sink.h>`) headers, plus `WIN32_LEAN_AND_MEAN` and `NOMINMAX` macro guards, into [PCH.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/PCH.h#L1-L63), and removed redundant `#include`s of those headers across all `.h` and `.cpp` files in `SKSE_Source/`.
- **🔵 What Can Be Refactored / Organized Better**:
  - ✅ **`using namespace std::literals;` in global PCH scope**: Narrowed to `using namespace std::string_view_literals;` (retained at global scope only for CommonLibSSE's auto-generated `__MARASPlugin.cpp` `"MARAS"sv` literal, and imported into `namespace MARAS`).

---

### 1.2 `plugin.cpp` ✅ Resolved
*Files: [plugin.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp), [Serialization.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Missing `FormCache::GetSingleton().Initialize()` in `kDataLoaded` ([plugin.cpp:232](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L232), [FormCache.cpp:40-98](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp#L40-L98))**:
     - Added `FormCache::Initialize()` and invoked it at the start of `kDataLoaded` so all cached forms are pre-populated on the main thread before any background/decorator/Papyrus access.
  2. ✅ **`SaveCallback` Early-Return Aborts Entire Co-Save on Single Record Failure ([Serialization.cpp:25-80](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp#L25-L80))**:
     - Replaced early-return `if (!OpenRecord)` blocks with an independent `saveRecord` helper lambda so a failure in one chunk logs the error and continues saving all remaining chunks.
  3. ✅ **Missing Version Validation in `LoadCallback` for `MKMK`, `DCSD`, and `BNTR` ([Serialization.cpp:100-104](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp#L100-L104))**:
     - Added a unified top-level `if (version < 1 || version > kDataVersion)` guard before dispatching to any chunk loader.
  4. ✅ **`PollingService` Driven Solely by `RE::MenuOpenCloseEvent` ([plugin.cpp:41-75](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L41-L75), [plugin.cpp:184-191](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L184-L191))**:
     - Extended `UpdateEventSink` to also inherit from `RE::BSTEventSink<RE::InputEvent*>` (throttled to 1 second) and registered it on `BSInputDeviceManager` at `kInputLoaded` so `PollingService::Update()` runs reliably during active gameplay even when no menus open or close.
  5. ✅ **Undefined Behavior in `PrintToConsole`**:
     - Removed dead `PrintToConsole` function.

- **🟡 What Can Be Improved**:
  1. ✅ **Synchronous Disk Flush on Every Log Line ([plugin.cpp:139-140](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L139-L140))**:
     - Changed `logger->flush_on(spdlog::level::trace)` to `logger->flush_on(spdlog::level::warn)` with `spdlog::flush_every(std::chrono::seconds(3))`.
  2. ✅ **Redundant Disk I/O During Startup and Save Load**:
     - Removed duplicate `Settings::GetSingleton().Load()` in `kDataLoaded`, duplicate `MarriageDifficulty::LoadConfig()` in `RevertCallback`, and redundant `NameService::GetSingleton().LoadFromFile()` on every save load.
  3. ✅ **Input Listener Re-Registered Repeatedly**:
     - Stopped unregistering the input hotkey sink in `PrismaUIManager::Destroy()` and removed redundant `StartListeningInput()` calls on `kPostLoadGame`/`kNewGame` and `Show()`.

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Unified Co-Save `OpenRecord` Responsibility ([Serialization.cpp:25-80](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp#L25-L80))**:
     - Removed internal `OpenRecord` calls from `DeceasedRegistry::Save` and `BanterCoordinatorService::Save` so `SaveCallback` opens all 11 records uniformly.
  2. ✅ **Extracted `SaveCallback`, `LoadCallback`, and `RevertCallback` into [Serialization.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp)**.
  3. ✅ **Removed Dead Function `PrintToConsole`**.

---

### 1.3 `include/MARAS_PublicAPI.h` & `src/MARAS_PublicAPI.cpp` ✅ Resolved
*Files: [MARAS_PublicAPI.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/MARAS_PublicAPI.h), [MARAS_PublicAPI.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/MARAS_PublicAPI.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Dangling Reference / Thread-Safety Race in `MARAS_GetStatusHistoryEntry` ([MARAS_PublicAPI.cpp:69-86](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/MARAS_PublicAPI.cpp#L69-L86))**:
     - Added value-returning [`NPCRelationshipManager::GetStatusHistoryEntry`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1749-L1755) (`std::optional<StatusChangeRecord>`) and updated `MARAS_GetStatusHistoryEntry` to copy the entry by value before writing to caller output pointers.
  2. ✅ **Timestamp Truncation & Negative-Float-to-Unsigned UB (`float` to `uint32_t`) ([MARAS_PublicAPI.h:67](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/MARAS_PublicAPI.h#L67), [MARAS_PublicAPI.cpp:74-83](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/MARAS_PublicAPI.cpp#L74-L83))**:
     - Changed `outTimestamp` parameter in `MARAS_GetStatusHistoryEntry` from `std::uint32_t*` to `float*` to preserve fractional sub-day game time and avoid undefined behavior when `entry.timestamp` is `-1.0f` (pre-existing relationships).

- **🟡 What Can Be Improved**:
  1. ✅ **Missing `static_assert` for `MARAS_Temperament` Parity ([MARAS_PublicAPI.cpp:9-19](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/MARAS_PublicAPI.cpp#L9-L19))**:
     - Added compile-time `static_assert` checks for all five `MARAS_Temperament` enumerators (`Proud`, `Humble`, `Jealous`, `Romantic`, `Independent`) against `MARAS::Temperament`.
  2. ✅ **Misleading Doc Comment on `MARAS_GetTemperament` ([MARAS_PublicAPI.h:15-26](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/MARAS_PublicAPI.h#L15-L26), [MARAS_PublicAPI.h:50-53](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/MARAS_PublicAPI.h#L50-L53))**:
     - Clarified in `MARAS_PublicAPI.h` that `MARAS_Temperament::Unknown (255)` is a caller-side sentinel and that `MARAS_GetTemperament` always returns `Proud (0)`–`Independent (4)` (falling back to `DeceasedRegistry`, or `Proud (0)` if unregistered).

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Deduplicate Buffer-Copy Boilerplate ([MARAS_PublicAPI.cpp:21-34](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/MARAS_PublicAPI.cpp#L21-L34))**:
     - Extracted `CopyRangeToBuffer(const Range& src, std::uint32_t* buf, std::uint32_t maxCount)` template helper and simplified `MARAS_GetAllMarried`, `MARAS_GetAllEngaged`, `MARAS_GetPlayerHouseTenants`, and `MARAS_GetCurrentTeammates` to single-line delegations.

---

### 1.4 `include/core/Serialization.h` ✅ Resolved
*Files: [Serialization.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/Serialization.h), [Serialization.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Dead / Unhandled Record Type Constants (`kStatusHistoryData`, `kPendingRelationshipData`, `kPendingChildrenData`, `kChildRegistryData`) ([Serialization.h:19-45](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/Serialization.h#L19-L45), [Serialization.cpp:172-175](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp#L172-L175))**:
     - Removed dead FourCC constants (`SHST`, `PNDR`, `PDCH`, `CHDR`) that were consolidated inline into `NPCRelationshipManager`'s `NPCR` stream in v4, added a warning log in `LoadCallback` for any unrecognized record types, and synchronized `SKSE_Source/AGENTS.md`, `docs/modder/ARCHITECTURE.md`, and `docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md`.

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Distinguish Top-Level SKSE Record FourCCs from Internal Section Markers**:
     - Documented in `Serialization.h` that `NPCR` encapsulates status history, deferred queues, and the child registry inline, leaving only the 11 active top-level SKSE `OpenRecord` FourCC constants.

---

## Section 2: Utility Headers & Services (`include/utils/`, `src/utils/`, `include/services/`, `src/services/`)

### 2.1 `include/utils/Common.h` ✅ Resolved
*File: [Common.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Common.h)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Unchecked Null Pointer Dereference in `MARAS_LOG_*` Macros ([Common.h:32-43](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Common.h#L32-L43))**:
     - Updated `GetLogger()` to return `const std::shared_ptr<spdlog::logger>&` (avoiding atomic ref-count churn on every log call) and wrapped all `MARAS_LOG_*` macros in `do { if (const auto& _logger = MARAS::GetLogger()) _logger->...(__VA_ARGS__); } while (0)` so logging is a safe no-op if `SetupLogging()` fails or before logger initialization.

- **🟡 What Can Be Improved**:
  1. ✅ **Unnecessary Heavy Includes in `Common.h` ([Common.h:4](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Common.h#L4), [plugin.cpp:3](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L3))**:
     - Removed redundant includes from `Common.h` and moved `<spdlog/sinks/basic_file_sink.h>` exclusively to `plugin.cpp` where `SetupLogging()` constructs the file sink.

---

### 2.2 `include/utils/ProcessUtils.h` ✅ Resolved
*File: [ProcessUtils.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ProcessUtils.h)*

- **🟡 What Can Be Improved**:
  1. ✅ **Game-Thread-Only Assumption & Player Exclusion Not Guarded/Documented ([ProcessUtils.h:7-55](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ProcessUtils.h#L7-L55))**:
     - Documented the main-thread-only requirement and `RE::PlayerCharacter` exclusion on `ForEachLoadedActor`, added an optional `bool includePlayer = false` parameter, and snapshotted `RE::ActorHandle`s per tier before invoking the visitor callback so callbacks that trigger `EvaluatePackage` or process-tier transitions cannot invalidate the active `BSTArray` iterator.

---

### 2.3 `include/utils/StringUtils.h` ✅ Resolved
*File: [StringUtils.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/StringUtils.h)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **`MatchesPattern` Ignores `caseInsensitive` Parameter and Mixes Prefix vs. Full-String Matching ([StringUtils.h:139-145](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/StringUtils.h#L139-L145))**:
     - Updated `MatchesPattern` to perform full-string matching (`CaseInsensitiveEquals(text, pattern)` or `text == pattern`) respecting `caseInsensitive` when no wildcard (`*` or `?`) is present.

- **🟡 What Can Be Improved**:
  1. ✅ **Excessive Heap Allocations in `StartsWith`, `EndsWith`, and `Contains` ([StringUtils.h:57-93](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/StringUtils.h#L57-L93))**:
     - Rewrote `StartsWith`, `EndsWith`, and `Contains` to operate directly on `std::string_view` with zero heap allocations (using `CaseInsensitiveEquals` on `substr` and `std::search`), and added `constexpr TrimView(std::string_view)`.
  2. ✅ **`BSFixedString` Pool Pollution in `ToLower`, `ToUpper`, and `Trim` ([StringUtils.h:18-55](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/StringUtils.h#L18-L55))**:
     - Removed unused `RE::BSFixedString`-returning `ToLower`, `ToUpper`, and `Trim` wrappers that polluted Skyrim's global `BSStringPool`.
  3. ✅ **Per-Call Vector Allocations in `LevenshteinDistance` ([StringUtils.h:174-224](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/StringUtils.h#L174-L224))**:
     - Swapped `a` and `b` so `n <= m` and used a 128-int stack buffer (`std::array<int, 128>`) when `n + 1 <= 128`, eliminating heap allocations during `FuzzyFind` searches.

---

### 2.4 `include/utils/EnumUtils.h` & `src/utils/EnumUtils.cpp` ✅ Resolved
*Files: [RelationshipTypes.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/RelationshipTypes.h), [EnumUtils.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/EnumUtils.h), [EnumUtils.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/EnumUtils.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Silent Fallback Defaults Hide Invalid Config Values ([EnumUtils.cpp:7-105](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/EnumUtils.cpp#L7-L105))**:
     - Added `MARAS_LOG_WARN` diagnostics across `StringToSocialClass`, `StringToSkillType`, `StringToTemperament`, `StringToRelationshipStatus`, `StringToAffairType`, and `StringToAffairStatus` whenever a non-empty unrecognized enum string is passed, and added support for `"ex_dating"` and `"ex_lover"` variants in `StringToRelationshipStatus`.

- **🟡 What Can Be Improved**:
  1. ✅ **Heap Allocation in Every `StringTo*` Conversion ([EnumUtils.cpp:7-119](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/EnumUtils.cpp#L7-L119))**:
     - Replaced `ToLower(str)` heap allocations in all `StringTo*` functions with zero-allocation `StringUtils::CaseInsensitiveEquals(str, "...")`.

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Duplicate `ToLower` Implementation ([EnumUtils.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/EnumUtils.h), [EnumUtils.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/EnumUtils.cpp))**:
     - Removed duplicate `MARAS::Utils::ToLower` and standardized callers across the codebase on `StringUtils::ToLowerStd` / `StringUtils::CaseInsensitiveEquals`.
  2. ✅ **Heavy Header Coupling (`#include "core/NPCRelationshipManager.h"`) ([RelationshipTypes.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/RelationshipTypes.h), [EnumUtils.h:3](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/EnumUtils.h#L3))**:
     - Extracted core enums (`SocialClass`, `SkillType`, `Temperament`, `RelationshipStatus`, `AffairType`, `AffairStatus`, `AffairDiscovery`, `AffairResolution`) and their compile-time `static_assert`s into a lightweight [RelationshipTypes.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/RelationshipTypes.h) header, decoupling `EnumUtils.h` from `NPCRelationshipManager.h`.
  3. ✅ **Misplaced `RandomFloat` and `GetNPCName` in `EnumUtils.h` ([Common.h:36-42](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Common.h#L36-L42))**:
     - Moved `Utils::RandomFloat` to `Common.h` and removed the dead 1-line `Utils::GetNPCName` wrapper.

---

### 2.5 `include/utils/FormUtils.h` & `src/utils/FormUtils.cpp` ✅ Resolved
*Files: [FormUtils.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/FormUtils.h), [FormUtils.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/FormUtils.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Broken ESL / Light Plugin FormID Construction in `LookupForm` Fallback ([FormUtils.h:45-70](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/FormUtils.h#L45-L70))**:
     - Updated `LookupForm` to check `file->IsLight()`, mask `localFormID` with `0x00000FFFu` for light plugins (or `0x00FFFFFFu` for regular plugins), and construct the fallback `fullFormID` via `0xFE000000u | (file->GetSmallFileCompileIndex() << 12) | maskedFormID` for light plugins.

- **🟡 What Can Be Improved**:
  1. ✅ **Redundant `TESDataHandler` and `LookupModByName` Calls in `ParseAndResolveFormKey` ([FormUtils.cpp:38-46](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/FormUtils.cpp#L38-L46))**:
     - Removed duplicate `TESDataHandler::GetSingleton()` and `LookupModByName` checks from `ParseAndResolveFormKey`, delegating directly to `LookupForm<RE::TESForm>`, and changed `LookupForm` / `GetFormID` `pluginName` parameters to `std::string_view`.
  2. ✅ **Slow `std::regex` and `std::stoul` Exception Handling in `ParseFormKey` / `HexStringToFormID` ([FormUtils.cpp:5-36](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/FormUtils.cpp#L5-L36), [FormUtils.cpp:48-72](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/FormUtils.cpp#L48-L72))**:
     - Replaced `std::regex_match` in `ParseFormKey` with `std::string_view` prefix/delimiter splitting and replaced `std::stoul` try/catch in `HexStringToFormID` with exception-free, zero-allocation `std::from_chars`.

---

### 2.6 `include/utils/ActorUtils.h` ✅ Resolved
*Files: [Common.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Common.h), [ActorUtils.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Violation of Faction Hook Re-Entrancy Rule (`SKSE_Source/AGENTS.md` §4) in `AddToFaction` & `RemoveFromFaction` ([ActorUtils.h:154-200](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L154-L200))**:
     - Added `FactionService::InternalFactionScope scope;` inside `Utils::RemoveFromFaction` and `Utils::AddToFaction` so programmatic faction updates do not trigger recursive `FactionService` VTable hook notifications.
  2. ✅ **Mutating Shared Base Record `TESNPC::crimeFaction` in `SetCrimeFaction` ([ActorUtils.h:214-227](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L214-L227))**:
     - Removed the direct mutation of `base->crimeFaction` on the shared `RE::TESNPC` base record in `SetCrimeFaction`, keeping crime faction changes scoped to `RE::ExtraFactionChanges` on the actor reference.
  3. ✅ **No Synthetic Child FormID Check Before `RE::TESForm::LookupByID` (`SKSE_Source/AGENTS.md` §8.4) ([Common.h:24-34](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Common.h#L24-L34), [ActorUtils.h:25](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L25), [ActorUtils.h:69](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L69), [ActorUtils.h:103](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L103), [ActorUtils.h:130](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L130), [ActorUtils.h:176](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L176), [ActorUtils.h:203](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L203), [ActorUtils.h:230](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L230))**:
     - Promoted `IsSyntheticChildID` and `HasPhysicalActor` (`id != 0 && !IsSyntheticChildID(id)`) to `Common.h` and guarded all `FormID`-accepting helpers in `ActorUtils.h` with `HasPhysicalActor(...)`.

- **🟡 What Can Be Improved**:
  1. ✅ **Log Spam in `detail::CheckPackageTargetsPlayer` ([ActorUtils.h:425-443](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L425-L443))**:
     - Downgraded the three per-check `MARAS_LOG_INFO` calls in `CheckPackageTargetsPlayer` to `MARAS_LOG_TRACE`.
  2. ✅ **Repeated String Allocations & Redundant Race Checks in `IsMannequin` ([ActorUtils.h:241-312](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L241-L312), [ActorUtils.h:339](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L339))**:
     - Extracted `detail::IsMannequinRace` (using zero-allocation `StringUtils::Contains`), skipped redundant race checks in `IsMannequin(RE::Actor*)` when `race == base->race`, and checked squared distance (`GetSquaredDistance < radiusSq`) before `IsMannequin` in `GetNearbyActors`.

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Duplicate Synonym Function `matchesActor` vs. `MatchesActor` (`AGENTS.md` §3 Violation) ([ActorUtils.h:381-398](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/ActorUtils.h#L381-L398))**:
     - Removed `matchesActor` and standardized all callers across `NPCRelationshipManager.cpp` and `SkyrimNetIntegration.cpp` on `MatchesActor`.

---

### 2.7 `include/services/ActorService.h` & `src/services/ActorService.cpp` ✅ Resolved
*Files: [ActorService.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/services/ActorService.h), [ActorService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Synthetic Child FormIDs Passed Directly to `RE::TESForm::LookupByID` (`SKSE_Source/AGENTS.md` §8.4 Violation) ([ActorService.cpp:13-15](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L13-L15), [ActorService.cpp:77-113](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L77-L113), [ActorService.cpp:134-154](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L134-L154), [ActorService.cpp:207-269](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L207-L269))**:
     - Added `CanLookupEngineForm(formID)` (`formID != 0 && formID != kUnknownFatherFormID && !IsSyntheticChildID(formID)`) across `GetActorBase`, `GetReference`, `GetName`, `GetRace`, `GetPluginName`, and `IsValid`, added `ChildRegistryEntry::gender` fallback in `GetSex`, and updated `IsValid` to validate synthetic children against the child registry.
  2. ✅ **Tautological Player FormID Check (`formID == kPlayerFormID || formID == kPlayerRefID`) Misses `kPlayerBaseID` (`0x7`) ([ActorService.cpp:9-11](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L9-L11), [ActorService.cpp:22](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L22), [ActorService.cpp:137](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L137), [ActorService.cpp:210](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L210), [ActorService.cpp:236](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L236))**:
     - Added `IsPlayerFormID(formID)` (`formID == kPlayerRefID || formID == kPlayerBaseID`) and used it consistently in `GetName`, `GetRaceName`, `GetRace`, and `GetPluginName`.
  3. ✅ **Potential Infinite Recursion & Sentinel Bug in `GetRaceName` ([ActorService.cpp:41-61](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/services/ActorService.cpp#L41-L61))**:
     - Guarded parent race lookup in `GetRaceNameImpl` with `CanLookupEngineForm(pid) && pid != formID` (skipping `kUnknownFatherFormID` and synthetic IDs) and capped recursion depth at `depth < 2`.

---

### 2.8 `include/utils/JsonService.h` ✅ Resolved
*File: [JsonService.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **`GetActorArray` Drops Signed Integer FormIDs ([JsonService.h:69-102](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h#L69-L102), [JsonService.h:204-221](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h#L204-L221))**:
     - Extracted `detail::ExtractFormID` (handling `is_number_unsigned()`, `is_number_integer()`, and `std::from_chars` hex/decimal strings) and shared it between `GetActor` and `GetActorArray`.
  2. ✅ **`SanitizeLLMJson` Extracts Prose Brackets Instead of JSON Object When Prose Contains `[...]` ([JsonService.h:229-310](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h#L229-L310))**:
     - Updated `SanitizeLLMJson` to narrow to fenced markdown code blocks (` ```...``` `) when present and verify whether a leading `[` closes before `{` (ignoring prose brackets such as `"[Note]: {...}"`).

- **🟡 What Can Be Improved**:
  1. ✅ **Full JSON Re-Parsing on Every Field Lookup & Heap Allocations in `Traverse()` ([JsonService.h:11-67](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h#L11-L67), [JsonService.h:105-221](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h#L105-L221))**:
     - Added a `thread_local` single-entry parse cache (`detail::GetParsedJson`) so sequential `GetString`/`GetActor`/`GetInt` calls on the same JSON payload parse once, and rewrote `Traverse` to try an exact `cur->find(key)` first before falling back to zero-allocation `StringUtils::CaseInsensitiveEquals`.

---

### 2.9 `include/utils/JsonOverrideLoader.h` & `src/utils/JsonOverrideLoader.cpp` ✅ Resolved
*Files: [JsonOverrideLoader.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonOverrideLoader.h), [JsonOverrideLoader.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/JsonOverrideLoader.cpp)*

- **🟡 What Can Be Improved**:
  1. ✅ **Unreachable `unresolvedForms` Metric & Redundant Form Lookup ([JsonOverrideLoader.cpp:97-115](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/JsonOverrideLoader.cpp#L97-L115))**:
     - Separated key syntax parsing (`ParseFormKey(key)`) from engine form lookup (`LookupForm<RE::TESForm>(parsedKey.localFormID, parsedKey.pluginName, "JsonOverrideLoader")`), accurately distinguishing `s_lastStats.invalidFormKeys` from `s_lastStats.unresolvedForms` and eliminating the redundant `RE::TESForm::LookupByID` call. Also default-initialized `LoadStatistics` members in [JsonOverrideLoader.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonOverrideLoader.h#L39-L46).
  2. ✅ **Uncaught `nlohmann::json` Parse Exceptions in `ParseJsonFile` When Called Directly ([JsonOverrideLoader.cpp:70-74](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/JsonOverrideLoader.cpp#L70-L74))**:
     - Replaced `file >> json;` with non-throwing `nlohmann::json::parse(file, nullptr, false, true)` and checked `json.is_discarded()`.

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Duplicate `caseInsensitiveEquals` Lambda ([JsonOverrideLoader.cpp:166-198](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/JsonOverrideLoader.cpp#L166-L198))**:
     - Removed the duplicate `caseInsensitiveEquals` lambda in `ValidateOverrideData` and used `MARAS::StringUtils::CaseInsensitiveEquals`.

---

### 2.10 `include/utils/Settings.h` & `src/utils/Settings.cpp` ✅ Resolved
*Files: [Settings.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Settings.h), [Settings.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Repeated Uncached `TESDataHandler::LookupForm` Calls in `LookupGlobal` (`SKSE_Source/AGENTS.md` §3 Violation) ([Settings.cpp:240-303](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp#L240-L303))**:
     - Added cached getters (`GetEnablePolygamyGlobal`, `GetStartDialGenderGlobal`, `GetSkyrimNetInstalledGlobal`, `GetMinRelRankForDialGlobal`) to [FormCache.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/FormCache.h#L114-L120) / [FormCache.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp#L328-L339) (pre-populated in `FormCache::Initialize()`), removed `LookupGlobal`, and routed all global-backed accessors in `Settings.cpp` through `FormCache`.
  2. ✅ **Missing `FactionService::InternalFactionScope` in `Settings::SetEnablePolygamy` (`SKSE_Source/AGENTS.md` §4 Violation) ([Settings.cpp:271-273](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp#L271-L273))**:
     - Added `FactionService::InternalFactionScope` around `Utils::AddToFaction(player, marriedFaction, 0)` in `Settings::SetEnablePolygamy`.
  3. ✅ **Fragile Versionless Co-Save Stream in `SaveGameSettings` / `LoadGameSettings` ([Settings.cpp:305-445](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp#L305-L445))**:
     - Added an explicit `kSettingsVersionMarker` (`0xFF`) + `kSettingsSubVersion` (`1`) header in `SaveGameSettings`, while preserving seamless backward compatibility in `LoadGameSettings` when `firstByte != kSettingsVersionMarker` (`0` or `1` from existing v4 versionless records). Removed the buggy legacy fallback that cast 1-byte `bSkip` to `legacyLogLevel`.
  4. ✅ **Unsynchronized Multi-Threaded Access to `Settings` Fields ([Settings.h:189-201](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Settings.h#L189-L201), [Settings.cpp:225-492](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp#L225-L492))**:
     - Added `mutable std::shared_mutex settingsMutex_` (exposed via `GetMutex()`) and synchronized `Load`, `SetSimulateNoSkyrimNet`, `SaveGameSettings`, `LoadGameSettings`, `RevertGameSettings`, and `DumpJson`.

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Duplicate `ToUpper` and `Trim` Helpers ([Settings.cpp:166-211](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp#L166-L211))**:
     - Removed the duplicate anonymous-namespace `ToUpper` and `Trim` functions in favor of `StringUtils::ToUpperStd` and `StringUtils::TrimStd`, and replaced `std::stoul` exception blocks in `ParseSingleKey` with `std::from_chars`.
  2. ✅ **Table-Driven Settings Definition ([Settings.cpp:59-163](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp#L59-L163), [Settings.cpp:305-492](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp#L305-L492))**:
     - Consolidated all 82 co-save persisted settings into a single canonical `ForEachPersistedSetting` table driving `SaveGameSettings`, `LoadGameSettings`, `RevertGameSettings` (resetting directly from default member initializers in `Settings.h`), and `DumpJson`, cutting over 400 lines of duplicate boilerplate.

---

## Section 3: Core Infrastructure (`LoggingService`, `FormCache`, `FactionService`, `PollingService`, Event Sinks & Quest Events)

### 3.1 `include/core/LoggingService.h` & `src/core/LoggingService.cpp` ✅ Resolved
*Files: [LoggingService.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/LoggingService.h), [LoggingService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/LoggingService.cpp)*

- **🟡 What Can Be Improved**:
  1. ✅ **`logger->flush_on(lvl)` Forces Synchronous Disk Flushes on Every Log Message ([LoggingService.cpp:43-58](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/LoggingService.cpp#L43-L58))**:
     - Updated `LoggingService::SetLogLevel` so `flush_on` stays at `spdlog::level::warn` (or higher) during normal `info` operation and only flushes on every message when `level <= 1` (`trace`/`debug`), and synchronized `Settings::logLevel` reads/writes via `Settings::GetMutex()`.

---

### 3.2 `include/core/FormCache.h` & `src/core/FormCache.cpp` ✅ Resolved
*Files: [FormCache.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/FormCache.h), [FormCache.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Use of `SPDLOG_WARN` Instead of `MARAS_LOG_WARN` Drops All Form Load Warnings (`SKSE_Source/AGENTS.md` §1 Violation) ([FormCache.cpp:13-39](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp#L13-L39))**:
     - Replaced `#include "spdlog/spdlog.h"` with `#include "utils/Common.h"` and replaced all `SPDLOG_WARN(...)` calls with `MARAS_LOG_WARN(...)`.
  2. ✅ **Thread-Unsafe Lazy Initialization Without `FormCache::Initialize()` (`SKSE_Source/AGENTS.md` §3 & §5 Violation) ([FormCache.h:11-12](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/FormCache.h#L11-L12), [FormCache.cpp:46-150](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp#L46-L150))**:
     - Implemented `FormCache::Initialize()` (called on the main thread at `kDataLoaded` in `plugin.cpp`) to pre-populate all cached forms from `TT_MARAS.esp`, `HearthFires.esm`, and `Skyrim.esm` before any background or decorator threads run.
  3. ✅ **39 Getters in `FormCache` Do Not Cache Anything ([FormCache.h:158-259](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/FormCache.h#L158-L259), [FormCache.cpp:218-489](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp#L218-L489))**:
     - Added cached member pointers for all 39 `Skyrim.esm` getters (marriage factions, general/guild factions, merchant factions, Thane/guild quests, keywords, and `HonorhallOrphanageCenterMarker`), routed them through `LazyLoadSkyrimForm`, and pre-populated all of them in `FormCache::Initialize()`.
  4. ✅ **Missing `TT_MARAS.esp` Globals Required by `Settings.cpp` ([FormCache.h:114-120](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/FormCache.h#L114-L120), [FormCache.cpp:382-393](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp#L382-L393))**:
     - Added cached getters for `TTM_EnablePolygamyToggle` (`0x6`), `TTM_MCM_StartDialGender` (`0x4B`), `TTM_SkyrimNetInstalled` (`0xC4`), and `TTM_MCM_MinRelRankForDial` (`0xE4`).

- **🔵 What Can Be Refactored / Organized Better**:
  1. ✅ **Unused Header Includes in `FormCache.h` ([FormCache.h:1-4](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/FormCache.h#L1-L4))**:
     - Replaced unused headers in `FormCache.h` with `#include "PCH.h"`.
  2. ✅ **Add `FormCache::Initialize()` Called at `kDataLoaded` ([FormCache.cpp:46-150](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FormCache.cpp#L46-L150), [plugin.cpp:234-235](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L234-L235))**:
     - Populated all cached pointers eagerly in `FormCache::Initialize()` at `kDataLoaded`.

---

### 3.3 `include/core/FactionService.h` & `src/core/FactionService.cpp` ✅ Resolved
*Files: [FactionService.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/FactionService.h), [FactionService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FactionService.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **`InternalFactionScope` Is Not Re-Entrant (`bool` Instead of Reference Count) ([FactionService.cpp:52-68](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FactionService.cpp#L52-L68))**:
     - Replaced `thread_local bool s_internalFactionUpdate` with a re-entrant reference counter `thread_local int s_internalFactionDepth = 0`.
  2. ✅ **`SanitizePlayerMarriedFaction()` Does Not Use `InternalFactionScope` (`SKSE_Source/AGENTS.md` §4 Violation) ([FactionService.cpp:70-90](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FactionService.cpp#L70-L90))**:
     - Added an explicit `InternalFactionScope` guard before `Utils::RemoveFromFaction(player, marriedFaction)` in `SanitizePlayerMarriedFaction()`.
  3. ✅ **`NotifyFactionAdded` / `NotifyFactionRemoved` Fired *Before* Engine State Is Updated ([FactionService.cpp:208-333](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FactionService.cpp#L208-L333))**:
     - Reordered `Hook_AddToFaction` and `Hook_RemoveFromFaction` so `_originalAddToFaction` / `_originalRemoveFromFaction` always executes before `NotifyFactionAdded` / `NotifyFactionRemoved` is dispatched, and copied listener vectors under `std::shared_lock` before invoking callbacks outside the lock.
  4. ✅ **Dangerous Manual x86 Prologue Decoder & Unrelocated Relative Jump Copy in `InstallHooks` ([FactionService.cpp:102-206](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FactionService.cpp#L102-L206), [FactionService.cpp:337-359](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FactionService.cpp#L337-L359))**:
     - Updated `DecodePrologueSize` to decode ModR/M + SIB + disp8/disp32 instruction lengths accurately (rejecting RIP-relative or unknown instructions instead of blindly slicing at 5 bytes), and added `InstallDetourHook` which properly chains existing `0xE9` relative `JMP` hooks by relocating the target into an absolute bridge jump.

---

### 3.4 `include/core/PollingService.h` & `src/core/PollingService.cpp` ✅ Resolved
*Files: [PollingService.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/PollingService.h), [PollingService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PollingService.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ✅ **Uninitialized `lastChildOrderCheck_` in `PollingService::Initialize()` ([PollingService.h:44-47](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/PollingService.h#L44-L47), [PollingService.cpp:24-28](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PollingService.cpp#L24-L28))**:
     - Initialized `lastChildOrderCheck_ = now;` in `PollingService::Initialize()` and default-initialized all `TimePoint` members in `PollingService.h`.
  2. ✅ **Unchecked `RE::UI::GetSingleton()` Dereference in `Update()` ([PollingService.cpp:62-66](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PollingService.cpp#L62-L66))**:
     - Added null check `auto* ui = RE::UI::GetSingleton(); if (!ui || ui->GameIsPaused())` in `PollingService::Update()`.
  3. ✅ **False Day-Change Event When `previousGameDay_` Starts at `-1.0f` on Day `0` ([PollingService.cpp:124-156](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PollingService.cpp#L124-L156))**:
     - Guarded `CheckDayChanged()` with `if (currentDay < 0.0f) return;` and initialized `previousGameDay_ = currentDay; return;` when `previousGameDay_ < 0.0f`, preventing a spurious `-1.0f -> 0.0f` day-change transition.

---

### 3.5 `include/core/DeathEventHandler.h` & `src/core/DeathEventHandler.cpp` ✅ Resolved
*Files: [DeathEventHandler.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/DeathEventHandler.h), [DeathEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeathEventHandler.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ~~**Missing `a_event->dead` Check Causes `ProcessEvent` to Execute Twice per Death (`OnDying` and `OnDeath`) ([DeathEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeathEventHandler.cpp#L18-L20))**~~ — **Resolved**: Added `!a_event->dead` to the early-return guard in `DeathEventHandler::ProcessEvent` so death handling only executes once when `a_event->dead == true` (`OnDeath`) and ignores the preceding `OnDying` (`a_event->dead == false`) notification.
  2. ~~**Dead Pet/Critter Not Cleaned Up in `PlayerChildrenService` ([DeathEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeathEventHandler.cpp#L49-L73))**~~ — **Resolved**: When a child's pet or critter dies, `DeathEventHandler::ProcessEvent` now removes the pet/critter assignment (`RemovePet` / `RemoveCritter`), records the 15-day retry cooldown (`RecordPetRejected` / `RecordCritterRejected`), issues the 24-hour `10.0f` ("Never Speak to You Again") order if the player was the killer, and then dispatches `maras_child_pet_died`.

---

### 3.6 `include/core/DialogueEventSink.h` & `src/core/DialogueEventSink.cpp` ✅ Resolved
*Files: [DialogueEventSink.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/DialogueEventSink.h), [DialogueEventSink.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DialogueEventSink.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ~~**Duplicate `SendDialogueEvent` Dispatch When `waitingForSpeaker_` Resolves on a `DialogueMenu` Open Event ([DialogueEventSink.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DialogueEventSink.cpp#L56-L109))**~~ — **Resolved**: Extracted `DialogueEventSink::TryResolveDeferredSpeaker()` using atomic `waitingForSpeaker_.exchange(false)` and invoked it in `ProcessEvent` only when `a_event->menuName != RE::DialogueMenu::MENU_NAME` (followed by an immediate `return`), while `DialogueMenu` open events reset `waitingForSpeaker_` and resolve the speaker once without fallthrough.
  2. ~~**Deferred Speaker Never Resolves If No Other Menu Opens/Closes During Dialogue ([DialogueEventSink.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DialogueEventSink.cpp#L12-L54), [plugin.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L59-L69))**~~ — **Resolved**: When `tm->speaker` is empty at `DialogueMenu` open time, `DialogueEventSink` now schedules a next-frame `SKSE::GetTaskInterface()->AddTask` check and `UpdateEventSink::ProcessEvent(RE::InputEvent* const*, ...)` also invokes `TryResolveDeferredSpeaker()` on input ticks while `waitingForSpeaker_` is set, ensuring deferred speakers resolve promptly without requiring another menu to open or close.

---

### 3.7 `include/core/QuestEventConfigLoader.h` & `src/core/QuestEventConfigLoader.cpp` ✅ Resolved
*Files: [QuestEventConfigLoader.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/QuestEventConfigLoader.h), [QuestEventConfigLoader.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventConfigLoader.cpp)*

- **🟡 What Can Be Improved**:
  1. ~~**Missing Validation of `command.argument` in `ParseCommandArray` ([QuestEventConfigLoader.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventConfigLoader.cpp#L132-L147))**~~ — **Resolved**: `ParseCommandArray` now checks `!command.argument.empty()` in addition to `!command.commandType.empty() && !command.npcSpecifier.empty()`, and `ParseCommand` also accepts numeric/boolean `"arg"` values in object-formatted commands.
  2. ~~**Duplicate Boilerplate With `JsonOverrideLoader` ([JsonOverrideLoader.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonOverrideLoader.h#L28-L50), [QuestEventConfigLoader.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventConfigLoader.cpp#L11-L94))**~~ — **Resolved**: Extracted shared `JsonOverrideLoader::ScanFormDataJsonFolder`, `ScanFormDataJsonFile`, and `IsValidJsonFile` helpers (with non-throwing comment-enabled `nlohmann::json::parse`, `__metaInfo` / `__formData|` filtering, and separated `ParseFormKey` + `LookupForm<RE::TESQuest>`) and delegated both `JsonOverrideLoader` and `QuestEventConfigLoader` to them.

---

### 3.8 `include/core/QuestEventHandler.h` & `src/core/QuestEventHandler.cpp` ✅ Resolved
*Files: [QuestEventHandler.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/QuestEventHandler.h), [QuestEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventHandler.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. ~~**`ExecutePromoteToStatus` Rejects Valid Statuses (`Dating`, `Lover`, `ExDating`, `ExLover`) ([QuestEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventHandler.cpp#L146-L180))**~~ — **Resolved**: Added `Dating`, `Lover`, `ExDating`, and `ExLover` cases to `ExecutePromoteToStatus` calling `PromoteToDating`, `PromoteToLover`, `PromoteToExDating`, and `PromoteToExLover`.
  2. ~~**`ExecutePromoteToStatus("deceased")` Calls `UnregisterNPC` Instead of `HandleNPCDeath` / `DeceasedRegistry` ([QuestEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventHandler.cpp#L130-L142))**~~ — **Resolved**: `"deceased"` now archives the adult in `DeceasedRegistry::RecordDeceasedAdult` (when an `RE::Actor*` is available) and invokes `NPCRelationshipManager::HandleNPCDeath` (falling back to `EvaluateChildCustodyOnDeath` if unregistered).
  3. ~~**Insensitivity to Case in `commandType` Dispatch ([QuestEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventHandler.cpp#L94-L115))**~~ — **Resolved**: Updated `ExecuteCommand` to use `StringUtils::CaseInsensitiveEquals` for all command type comparisons.

- **🟡 What Can Be Improved**:
  1. ~~**Unnecessary `TESForm::LookupByID<RE::TESQuest>` on Every Engine Quest Event ([QuestEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventHandler.cpp#L397-L457))**~~ — **Resolved**: `QuestStartStopEventSink::ProcessEvent` and `QuestStageEventSink::ProcessEvent` now check `GetConfigForQuest(event->formID)` and verify that non-empty commands exist for the event/stage before calling `RE::TESForm::LookupByID<RE::TESQuest>(event->formID)`.
  2. ~~**`ResolveNPC` Requires an Active `RE::Actor*` When All Callers Only Need `RE::FormID` ([QuestEventHandler.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/QuestEventHandler.h#L85-L86), [QuestEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventHandler.cpp#L337-L390))**~~ — **Resolved**: Changed `ResolveNPC` to return `RE::FormID` (`0` if unresolved), resolving `__formData|` keys via `NPCIndex::GetRefID` (falling back to the resolved form ID) and name searches via `FindActorByName` / `FindByName`, and using `ActorSvc::GetName` across all `Execute*` handlers.

---

## Section 4: NPC Indexing, Naming, Type Determination & Deceased Registry

### 4.1 `include/core/NPCIndex.h` & `src/core/NPCIndex.cpp` ✅ Resolved
*Files: [NPCIndex.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NPCIndex.h), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. **Thread-Unsafe Raw Pointer Return (`const NPCEntry*`) After Releasing `m_mutex` ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L876-L879), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L979-L999))**:
     - `FindByName` and `FindByFormID` acquire a `std::shared_lock lock(m_mutex)`, locate an element inside `std::unordered_map<RE::FormID, NPCEntry> m_entriesByRef`, and return `&entry` **after `lock` is destroyed**.
     - Concurrent calls to `RegisterLoadedActor` (fired on `TESInitScriptEvent` / `TESObjectLoadedEvent`) or `RefreshIndex` acquire a `std::unique_lock` and insert into `m_entriesByRef` (`m_entriesByRef[refID] = entry` at line 707) or mutate `it->second.currentCell` (line 683). Any caller reading a `const NPCEntry*` across threads (such as SkyrimNet prompt decorators or Prisma UI data bridge builders) races with `currentCell` mutation or `unordered_map` rehash invalidation. Return `std::optional<NPCEntry>` by value instead of a raw interior pointer.
  2. **`m_baseToRefMap` Retains Unplaced Base FormIDs (`baseID -> baseID`) After `BuildIndex` Purges Them from `m_entriesByRef` ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L481-L482), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L620-L638), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L1001-L1026))**:
     - In Phase 1 (`line 482`), every unique `TESNPC` sets `m_baseToRefMap[baseID] = baseID;`.
     - At the end of `BuildIndex` (`lines 622–626`), entries whose `refFormID` was not upgraded from `baseID` to a placed reference are skipped and discarded from `m_entriesByRef`—**but their `m_baseToRefMap[baseID] = baseID` entries are never removed from `m_baseToRefMap`**!
     - Consequently, `GetRefID(baseID)` (`lines 1009–1012` and `1020–1025`) returns `baseID` (a `TESNPC` Base FormID!) instead of `0` when asked for the Reference FormID of an unplaced NPC.
  3. **Synthetic Child FormIDs Passed to `RE::TESForm::LookupByID` (`SKSE_Source/AGENTS.md` §7.2 Violation) ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L1014), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L1048), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L1073))**:
     - `GetRefIDLocked`, `GetBaseIDLocked`, and `GetActorFromFormIdLocked` are frequently called on child FormIDs (e.g., via `ActorSvc::GetName` and `Utils::MatchesActor`), and all three pass `formID` directly to `RE::TESForm::LookupByID(formID)` without checking `NPCRelationshipManager::IsSyntheticChildId(formID)`!
  4. **`m_loadedActors` Keyed Only by `lowerName` Collides on Duplicate Display Names ([NPCIndex.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NPCIndex.h#L255), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L673), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L1242))**:
     - While `m_nameToRefMap` supports multiple NPCs sharing the same display name (`std::vector<RE::FormID>`), `m_loadedActors` is `std::unordered_map<std::string, RE::Actor*>`. If two loaded NPCs share a name, `m_loadedActors[lowerName]` overwrites one with the other, and `DumpIndexJson` (`line 1242`) marks *all* NPCs with that name as `"isLoaded": true`.
  5. **ESL / Light Plugin Bug in `LoadOverrides` `PluginName|0xLocalID` Parser ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L360-L373))**:
     - Line 369 calls `dataHandler->LookupFormID(*optHex & 0x00FFFFFF, pluginName);`. If a user writes `MyLightMod.esp|0xFE000801` (copying the full in-game FormID from xEdit/console for an ESL-flagged plugin), `*optHex & 0x00FFFFFF` leaves the ESL small-file index bits (`0x00XXX801`) intact instead of masking to 12 bits (`0xFFF`), causing `LookupFormID` to fail.
  6. **Arbitrary Hash-Order Substring & Reverse-Substring Matching in `FindByNameLocked` ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L815-L820), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L944))**:
     - Step 3 of `FindByNameLocked` iterates `m_entriesByRef` (an `unordered_map` with arbitrary bucket order) and returns the first entry matching `entry.nameLower.find(lowerSearch) != npos || lowerSearch.find(entry.nameLower) != npos`. Because of `lowerSearch.find(entry.nameLower)`, an NPC with a short 2–3 character name (e.g., `"Al"` or `"Bo"`) will match any longer search string containing those letters!

- **🟡 What Can Be Improved**:
  1. **Unconditional Multi-Megabyte Report File Dump on Every Game Launch ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L434-L436), [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L609-L610))**:
     - Every time Skyrim boots (`kDataLoaded`), `BuildIndex()` collects every single excluded non-unique NPC in the entire load order into `excludedNonUnique`, sorts thousands of strings, and writes `Data/SKSE/Plugins/MARAS/dumps/npc_index_promotions.txt` to disk. This should only run when a debug setting or dump command is requested.
  2. **$O(N)$ Linear Scan in `GetBaseIDLocked` ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L1042-L1046))**:
     - `GetBaseIDLocked` loops linearly over all entries in `m_baseToRefMap` when `formID` is not in `m_entriesByRef`.

---

### 4.2 `include/core/NPCIndexReportWriter.h` & `src/core/NPCIndexReportWriter.cpp` ✅ Resolved
*Files: [NPCIndexReportWriter.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NPCIndexReportWriter.h), [NPCIndexReportWriter.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndexReportWriter.cpp)*

- **🟡 What Can Be Improved**:
  1. **$O(N \log N)$ String Allocations Inside Sort Comparators ([NPCIndexReportWriter.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndexReportWriter.cpp#L34-L47))**:
     - `nameComparator` and `reasonNameComparator` call `StringUtils::ToLowerStd(a.name)` and `StringUtils::ToLowerStd(b.name)` on every comparison step during `std::sort`, allocating two temporary heap strings per comparison across thousands of NPC records.

- **🔵 What Can Be Refactored / Organized Better**:
  1. **Dead Phase 1.7 Report Sections (`promoted` and `rejectedCandidates`) (`AGENTS.md` §3 Violation) ([NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp#L605-L610), [NPCIndexReportWriter.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndexReportWriter.cpp#L92-L114), [NPCIndexReportWriter.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndexReportWriter.cpp#L139-L160))**:
     - `PromotedCandidateInfo`, `promotedCandidates`, and `rejectedCandidates` are vestigial remnants of a removed "Phase 1.7" heuristic. They are always passed as empty vectors from `NPCIndex.cpp:605-606`. Remove `PromotedCandidateInfo` and both dead report sections per `AGENTS.md` §3.

---

### 4.3 `include/core/NameService.h` & `src/core/NameService.cpp` ✅ Resolved
*Files: [NameService.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NameService.h), [NameService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NameService.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. **Data Race in `NormalizeRaceKey`, `NormalizeCritterRaceKey`, `GetRandomChildName`, and `GetRandomCritterName` ([NameService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NameService.cpp#L219-L252), [NameService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NameService.cpp#L289), [NameService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NameService.cpp#L374-L413), [NameService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NameService.cpp#L444-L458))**:
     - `NormalizeRaceKey` (line 235: `if (maleNames_.contains(s) || femaleNames_.contains(s))`) and `NormalizeCritterRaceKey` (line 380: `if (critterNames_.contains(s))`) are public methods that read internal `unordered_map`s without acquiring `mutex_`.
     - Worse, `GetRandomCritterName(const std::string&)` (line 450) calls `NormalizeCritterRaceKey(raceName)` **before** acquiring `std::shared_lock lock(mutex_)` at line 452!
     - `GetRandomChildName` (line 289) and `GetRandomCritterName` (line 444) read non-atomic `isChildLoaded_` / `isCritterLoaded_` outside `mutex_`.
  2. **Overly Broad Substring Match in `NormalizeCritterRaceKey` ([NameService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NameService.cpp#L407-L410))**:
     - `s.find("rat") != std::string::npos` matches any race or EditorID containing the 3-letter substring `"rat"` (e.g., `"crate"`, `"pirate"`, `"curate"`), misclassifying it as `"skeever"`.

---

### 4.4 `include/core/NPCTypeDeterminer.h` & `src/core/NPCTypeDeterminer.cpp` ✅ Resolved
*Files: [NPCTypeDeterminer.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NPCTypeDeterminer.h), [NPCTypeDeterminer.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCTypeDeterminer.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. **`DetermineSocialClassByFaction` Aborts When `RE::Actor*` Is Unavailable Even Though Factions Are Read from `RE::TESNPC*` ([NPCTypeDeterminer.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCTypeDeterminer.cpp#L118-L138))**:
     - Lines 119–123 check `auto actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID); if (!actor) return SocialClass::Working;`.
     - However, lines 133–160 inspect `npcBase->factions` (`ActorSvc::GetActorBase(npcFormID)`), which does **not** require `actor` at all (`actor` is only needed at line 197 for the optional `GetWornArmor` rich-clothing fallback).
     - If `npcFormID` is a base FormID whose placed reference is not yet resolved in `NPCIndex`, `DetermineSocialClassByFaction` logs an error and returns `SocialClass::Working`, ignoring the NPC's actual Ruler/Noble/Religious/Wealthy base factions!
  2. **Runtime Faction Changes (`ExtraFactionChanges`) Ignored During Social Class Detection ([NPCTypeDeterminer.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCTypeDeterminer.cpp#L139-L160))**:
     - `DetermineSocialClassByFaction` only iterates `npcBase->factions` (static Creation Kit factions on the `TESNPC` record). If `actor` is available and was added to a faction dynamically at runtime via `AddToFaction` (stored in `actor->extraList`'s `ExtraFactionChanges`), it is ignored.

- **🔵 What Can Be Refactored / Organized Better**:
  1. **Replace 50-Line Boolean Branch Chain in `ComputeTemperament` With a Static `8x6` Lookup Table ([NPCTypeDeterminer.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCTypeDeterminer.cpp#L61-L114))**:
     - `ComputeTemperament` encodes an $8 \times 6$ matrix (`SocialClass` $0..7 \times$ `SkillType` $0..5$) using 48 hardcoded integer comparisons (`socialClassIndex == 3 && skillTypeIndex == 5`, etc.). Replacing this with a `constexpr Temperament kTemperamentMatrix[8][6]` table makes the matrix self-documenting, $O(1)$, and impossible to have missing/overlapping cells.

---

### 4.5 `include/core/DeceasedRegistry.h` & `src/core/DeceasedRegistry.cpp` ✅ Resolved
*Files: [DeceasedRegistry.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/DeceasedRegistry.h), [DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp)*

- **🔴 Critical Issues / Breaking Bugs**:
  1. **Thread-Unsafe Public Accessors (`GetAllRecords`, `GetDeceasedCount`, `FindRecord`) & Unprotected Iteration in `WasMarriedTo` / `WasEngagedTo` / `WasExOf` ([DeceasedRegistry.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/DeceasedRegistry.h#L90-L92), [DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L219-L268), [DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L376-L394))**:
     - `GetAllRecords()` returns a raw `const std::unordered_map&` reference without acquiring `mutex_`.
     - `GetDeceasedCount()` reads `recordsByBaseID_.size()` without acquiring `mutex_`.
     - `FindRecord(formID)` acquires a `std::shared_lock`, finds a `DeceasedNPCRecord` inside `recordsByBaseID_`, and returns a raw `const DeceasedNPCRecord*` pointer **after releasing the lock**.
     - `WasMarriedTo`, `WasEngagedTo`, and `WasExOf` call `FindRecord(deceasedFormID)` and then iterate `rec->partnersAtDeath` completely outside any lock!
  2. **`WasExOf` Always Returns `false` (or Misses `ExDating` / `ExLover`) ([DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L86), [DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L253-L268))**:
     - First, `WasExOf` only checks `p.statusAtDeath == RelationshipStatus::Divorced || p.statusAtDeath == RelationshipStatus::Jilted`, completely forgetting `RelationshipStatus::ExDating` and `RelationshipStatus::ExLover`!
     - Second, in `RecordDeceasedAdult` (line 86), `partnersAtDeath` is populated from `relMgr.GetActiveRelationships(refID)`—if `GetActiveRelationships` only returns *active* bonds (`Dating`, `Lover`, `Engaged`, `Married`) and excludes dissolved bonds (`Divorced`, `Jilted`, `ExDating`, `ExLover`), `partnersAtDeath` will never contain ex-partners and `WasExOf` will permanently return `false`!
  3. **`GetDeceasedChildrenOf` and `HasDeceasedChildren` Ignore Biological Lineage (`bioMotherBaseID`, `bioFatherBaseID`) ([DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L318-L374))**:
     - Both methods only check `cInfo.guardianARefID`, `cInfo.guardianBRefID`, `cInfo.guardianABaseID`, and `cInfo.guardianBBaseID`, ignoring `cInfo.bioMotherBaseID` and `cInfo.bioFatherBaseID`. If a biological parent is not one of the two custodial guardians at the time of the child's death, `GetDeceasedChildrenOf` and `HasDeceasedChildren` fail to report their deceased biological child.
  4. **`Load` Passes Synthetic Child IDs and `kUnknownFatherFormID` (`0xFFFFFFFE`) to `serialization->ResolveFormID` (`SKSE_Source/AGENTS.md` §7.2 Violation) ([DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L512-L517), [DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L591-L592))**:
     - `resolveOrKeep` only skips `0` and `kPlayerFormID` (`0x14`), passing `kUnknownFatherFormID` (`0xFFFFFFFE`), `kPlayerBaseID` (`0x7`), and synthetic child FormIDs (`0xFF700000`–`0xFF7FFFFF`) into `serialization->ResolveFormID`.
  5. **Co-Save Record Version Inconsistency (`OpenRecord('DCSD', 1)`) ([DeceasedRegistry.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L407))**:
     - `DeceasedRegistry::Save` opens record `'DCSD'` with hardcoded version `1` instead of `Serialization::kDeceasedRegistryData` and `Serialization::kDataVersion`.
---

## Section 5: Spouse Hierarchy, Affection, Bonuses, Buffs, Assets & Marriage Difficulty

### 5.1 `SKSE_Source/include/core/SpouseHierarchyManager.h` & `SKSE_Source/src/core/SpouseHierarchyManager.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **`SetRank(npcFormID, -1)` (or `rank >= 3`) Fails to Remove the Spouse, Fails to Clear Faction Rank, and Fails to Send Demotion Events ([`SpouseHierarchyManager.cpp:90-103`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L90-L103), [`SpouseHierarchyManager.cpp:217-305`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L217-L305))**:
  - When `SetRank` is called with `rank < 0` or `rank >= 3` to remove a spouse from the top 3 slots:
    1. Lines 94–99 zero out `ranks_[i] = 0` **before** calling `FillGaps()`.
    2. `FillGaps()` ([`SpouseHierarchyManager.cpp:224-244`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L224-L244)) fetches `rel.GetAllMarried()` and appends every married spouse not currently in `ranks_` to `candidates`. Because `npcFormID` is still married, if the player has $\le 3$ married spouses (or if `npcFormID` appears early in `married`), `FillGaps()` immediately puts `npcFormID` right back into the top 3!
    3. Even when the player has $\ge 4$ married spouses and another spouse takes the slot, because `ranks_[i]` was already set to `0` prior to `FillGaps()`, the cleanup loop `for (auto old : ranks_)` ([`SpouseHierarchyManager.cpp:254-266`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L254-L266)) never sees `npcFormID` and **never calls `ClearFactionRank(npcFormID)`**, leaving the demoted spouse with their old top-3 faction rank on `HierarchyFaction` (`0x111`)!
    4. For the same reason, the event loop in `FillGaps()` ([`SpouseHierarchyManager.cpp:280-300`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L280-L300)) computes `oldRank = 4` for `npcFormID` (since `ranks_[i]` was already cleared), so `oldRank != newRank` (`4 != 4`) is false and no `"demote"` `ModCallbackEvent` is ever sent for `npcFormID`.
- **Moving an Existing Top-3 Spouse Into an Empty Lower Slot Sends `"promote"` with a Negative Diff and Immediately Gets Overridden by `FillGaps()` ([`SpouseHierarchyManager.cpp:115-131`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L115-L131))**:
  - When `occupant == 0` (e.g., `ranks_ = [A, B, 0]` and `SetRank(A, 2)` is called), `ranks_[0]` is set to `0`, `ranks_[2]` is set to `A`, `SendHierarchyChangedEvent(A, "promote", -2.0f)` is fired (labeling a move from rank 0 to rank 2 as `"promote"`), and then `FillGaps()` is called at line 128, which immediately shifts `B` to slot 0 and `A` to slot 1 and fires a second `ModCallbackEvent`!
- **`Load()` Fires `maras_hierarchy_changed` Papyrus Mod Events Mid-Deserialization and Truncates Unread Slot Data ([`SpouseHierarchyManager.cpp:322-360`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L322-L360))**:
  - `Load()` calls `FillGaps()` at line 349 during the SKSE co-save `LoadCallback`, which calls `NPCRelationshipManager::GetSingleton().GetAllMarried()` (whose load order relative to `SpouseHierarchyManager::Load` matters!) and dispatches `maras_hierarchy_changed` `SKSE::ModCallbackEvent`s while the game is still deserializing.
  - Additionally, lines 352–356 call `ApplyFactionRank` a second time right after `FillGaps()` already applied faction ranks at line 272.

#### 🔵 What Can Be Refactored / Organized Better
- **Unscoped Helper Functions in `namespace MARAS` ([`SpouseHierarchyManager.cpp:20-69`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseHierarchyManager.cpp#L20-L69))**:
  - `GetHierarchyFaction`, `SetActorHierarchyFactionRank`, `MakeRankOpt`, `ApplyFactionRank`, `ClearFactionRank`, and `SendHierarchyChangedEvent` are defined directly in `namespace MARAS` with external linkage instead of an anonymous namespace (`namespace { ... }`).

---

### 5.2 `SKSE_Source/include/core/AffectionService.h` & `SKSE_Source/src/core/AffectionService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **Loneliness Decay Resets `lastAffectionDay_` via `AddAffection`, Causing Decay to Only Trigger Once Every 3 Days and Falsely Reporting 0 Days Since Last Affection ([`AffectionService.cpp:69-82`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L69-L82), [`AffectionService.cpp:329-348`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L329-L348))**:
  - In `OnDayChanged()` ([`AffectionService.cpp:330-347`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L330-L347)), when an NPC has gone more than `kDaysBeforeDecayStarts` (`2.0f` days) without affection, `CalculateLonelinessDecay` returns a negative delta (e.g. `-3.0f`) and applies it by calling `AddAffection(npcFormID, lonelinessAffection, "loneliness")` ([`AffectionService.cpp:344`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L344)).
  - However, `AddAffection` ([`AffectionService.cpp:78`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L78)) **unconditionally calls `RecordAffectionInteraction(npcFormID)`**, which overwrites `lastAffectionDay_[npcFormID] = calendar->GetDaysPassed()`!
  - As a result:
    1. Applying loneliness decay resets the NPC's `daysSinceLastAffection` to `0.0f`!
    2. On the next 2 days, `if (daysSince <= kDaysBeforeDecayStarts)` (`<= 2.0f`) skips the NPC, meaning loneliness decay only fires **once every 3 days** instead of daily after 2 days of neglect.
    3. Any negative `AddAffection` call (e.g. from a negative event or `"loneliness"`) updates `lastAffectionDay_` as if the player gave the NPC positive attention, corrupting `GetDaysSinceLastAffection(npcFormID)` in the UI and AI prompts.
- **Co-Save `Save()` and `Load()` Overwrite `Settings::affectionDecayMultiplier` from `MARAS_Settings.ini` ([`AffectionService.cpp:184-187`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L184-L187), [`AffectionService.cpp:253-258`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L253-L258))**:
  - Although line 253 is commented `// Load decay multiplier (legacy compatibility check)`, `Save()` ([`AffectionService.cpp:185-186`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L185-L186)) still writes `Settings::GetSingleton().affectionDecayMultiplier` into every co-save, and `Load()` overwrites `Settings::GetSingleton().affectionDecayMultiplier` with the saved float. If a user changes `fAffectionDecayMultiplier` in `MARAS_Settings.ini`, loading any save silently clobbers their INI setting.
- **`Load()` Never Syncs `AffectionFaction` (`0x119`) Ranks on Loaded Actors ([`AffectionService.cpp:201-220`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L201-L220))**:
  - `Load()` populates `permanentAffection_[newFormID] = amount` without calling `UpdateAffectionFaction(actor, newFormID, ClampAffection(amount))` (nor clamping `amount` on load).
- **`Revert()` Wipes `minMaxByType_` Configuration ([`AffectionService.cpp:263-269`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L263-L269))**:
  - `minMaxByType_` is not serialized in `Save()`/`Load()`, yet `Revert()` clears `minMaxByType_` on every save load. Any per-type min/max limits registered before save load are lost unless re-registered by Papyrus on every load.

#### 🟡 What Can Be Improved
- **Repeated `O(N)` Vector Allocations Inside `OnDayChanged` Partner Loop ([`AffectionService.cpp:321-348`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L321-L348), [`AffectionService.cpp:483-486`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L483-L486))**:
  - `OnDayChanged()` already builds `uniquePartners` from `GetAllMarried`, `GetAllEngaged`, `GetAllDating`, and `GetAllLovers`. Inside the loop, `CalculateLonelinessDecay` re-queries all four vectors (`manager.GetAllMarried(kPlayerFormID).size() + ...`) on every iteration instead of passing `uniquePartners.size()`.
- **Missing Null Check on `npcForm` in `SendAffectionChangeEvent` ([`AffectionService.cpp:393-405`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L393-L405))**:
  - `auto npcForm = RE::TESForm::LookupByID(npcFormID);` is passed to `SKSE::ModCallbackEvent` without checking `if (!npcForm) return;`.

---

### 5.3 `SKSE_Source/include/core/BonusesService.h` & `SKSE_Source/src/core/BonusesService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **Case-Sensitivity Mismatch Between `ParseJson` and `GetBonusCount` / `GetBonusEntry` ([`BonusesService.cpp:25-27`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BonusesService.cpp#L25-L27) vs. [`BonusesService.cpp:97-108`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BonusesService.cpp#L97-L108))**:
  - `ParseJson` normalizes all category keys to lowercase (`std::transform(..., std::tolower)` at line 27) with the comment `// normalize keys to lowercase for case-insensitive lookup`.
  - However, `GetBonusCount(const std::string& type)` and `GetBonusEntry(const std::string& type, int index)` perform a raw case-sensitive `data_.find(type)` without calling `Utils::ToLower(type)`! Any caller passing `"Warrior"`, `"Outcast"`, etc. gets `0` / `nullptr`.

---

### 5.4 `SKSE_Source/include/core/SpouseBuffService.h` & `SKSE_Source/src/core/SpouseBuffService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **Inconsistent Spouse Count & Unloaded Actor Handling Between `GetSpouseMultiplier` / `GetPermanentMultipliers` and `GetHouseholdBuffsSnapshot` ([`SpouseBuffService.cpp:58`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp#L58), [`SpouseBuffService.cpp:102`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp#L102), [`SpouseBuffService.cpp:135`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp#L135))**:
  - `GetPermanentMultipliers()` ([`SpouseBuffService.cpp:102`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp#L102)) calls `MARAS::PapyrusInterface::GetNPCsByStatus(nullptr, "married")` (creating an upward dependency from a core service into the Papyrus binding layer), which resolves `RE::Actor*` pointers and skips any spouse whose `RE::Actor*` cannot currently be resolved, only to immediately extract `spouse->GetFormID()` at line 110.
  - Meanwhile, `GetHouseholdBuffsSnapshot()` ([`SpouseBuffService.cpp:135`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp#L135)) queries `relMgr.GetAllMarried(kPlayerFormID)` directly by `FormID`.

#### 🔵 What Can Be Refactored / Organized Better
- **Duplicated Buff Aggregation Logic ([`SpouseBuffService.cpp:66-122`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp#L66-L122) vs. [`SpouseBuffService.cpp:124-286`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp#L124-L286))**:
  - `GetPermanentMultipliers` should iterate `NPCRelationshipManager::GetSingleton().GetAllMarried(kPlayerFormID)` directly by `FormID` (without going through `PapyrusInterface`), and share the per-spouse multiplier helper with `GetHouseholdBuffsSnapshot`.

---

### 5.5 `SKSE_Source/include/core/SpouseAssetsService.h` & `SKSE_Source/src/core/SpouseAssetsService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **`Revert()` Leaks In-Memory Cell Public Flags and Bed Ownership Across Save Loads ([`SpouseAssetsService.cpp:331-334`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp#L331-L334))**:
  - `ShareHouseWithPlayer` mutates engine forms in memory (`cell->SetPublic(true)` at line 73 and `ref->extraList.SetOwner(playerFaction)` at line 316).
  - When `Revert()` is called (when loading a different save or starting a new game), it simply calls `sharedHomes_.clear()` without restoring `cell->SetPublic(false)` (when `originalPublicRecorded && !originalPublicState`) or calling `StopShareFurniture`. Those cells remain public and beds remain player-faction-owned in the new session.
- **`Load()` Does Not Re-Apply `cell->SetPublic(true)` or `ShareFurniture` for Active Shared Homes ([`SpouseAssetsService.cpp:222-302`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp#L222-L302))**:
  - When loading a save with active shared homes (`!data.sharingSpouses.empty()`), `Load()` restores the `sharedHomes_` map in memory ([`SpouseAssetsService.cpp:285-286`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp#L285-L286)) and even restores `cell->SetPublic(false)` if all spouses died ([`SpouseAssetsService.cpp:289-296`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp#L289-L296)), but **never calls `cell->SetPublic(true)` or `ShareFurniture(spouseId)`** for the surviving shared homes!

#### 🟡 What Can Be Improved
- **`StopShareFurniture` Overwrites Original Faction Ownership with `actorBase` ([`SpouseAssetsService.cpp:319-329`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp#L319-L329))**:
  - If a bed was originally owned by a household faction rather than the NPC's `TESNPC*` base form, `StopShareFurniture` overwrites its owner with `actorBase` instead of restoring the original `TESForm*` owner.
- **Duplicated Cell Public Flag Capture Block ([`SpouseAssetsService.cpp:66-81`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp#L66-L81) and [`SpouseAssetsService.cpp:86-99`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseAssetsService.cpp#L86-L99))**:
  - The `if (it == sharedHomes_.end())` and `else` branches duplicate the exact same logic; `sharedHomes_[homeCell]` can be accessed once and initialized uniformly.

---

### 5.6 `SKSE_Source/include/core/MarriageDifficulty.h` & `SKSE_Source/src/core/MarriageDifficulty.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **Division by Zero / `NaN` Propagation in `CalculatePlayerPrestige` When `prestigeMostGoldDivisor` Is `0.0f` ([`MarriageDifficulty.cpp:435-437`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MarriageDifficulty.cpp#L435-L437))**:
  - At line 436:
    ```cpp
    float wealthScore =
        ClampValue(mostGold / GetParam("prestigeMostGoldDivisor"), 0.0f, GetParam("prestigeMostGoldClampMax"));
    ```
  - If `marriageDifficultyConfig.json` is missing or omits `"mostGoldDivisor"`, `GetParam("prestigeMostGoldDivisor")` returns `0.0f`. When `mostGold == 0.0f`, `0.0f / 0.0f` produces IEEE-754 `NaN`. In C++, `std::clamp(NaN, 0.0f, max)` returns `NaN`, which poisons `result`, `finalPrestige`, `complexity`, `difficulty`, and `chance`, causing `CalculateMarriageSuccessChance` to return `NaN` to Papyrus.
- **`"datingBonus"` and `"loverBonus"` Are Queried in `CalculateMarriageSuccessChance` But Never Loaded in `LoadConfig()` ([`MarriageDifficulty.cpp:129-143`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MarriageDifficulty.cpp#L129-L143) vs. [`MarriageDifficulty.cpp:228-366`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MarriageDifficulty.cpp#L228-L366))**:
  - Lines 131 and 139 call `GetParam("datingBonus")` and `GetParam("loverBonus")`, but `LoadConfig()` never reads `"datingBonus"` or `"loverBonus"` from `marriageDifficultyConfig.json`!
  - Consequently, every proposal calculation for a Dating or Lover partner logs a `WARN` (`Parameter 'datingBonus' not found in config, returning 0.0f`) and falls back to the hardcoded `-15.0f` / `-30.0f`, ignoring any values in the JSON file.

#### 🟡 What Can Be Improved
- **`prestigeMaxThaneHolds` Is Loaded But Never Used ([`MarriageDifficulty.cpp:273`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MarriageDifficulty.cpp#L273), [`MarriageDifficulty.cpp:420-425`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MarriageDifficulty.cpp#L420-L425))**:
  - `LoadConfig()` stores `p["maxThaneHolds"]` in `config_["prestigeMaxThaneHolds"]`, but `CalculatePlayerPrestige` multiplies `GetThaneHolds() * GetParam("prestigeThaneHoldValue")` without clamping `thaneHolds` to `prestigeMaxThaneHolds`.
- **Dead Declaration `IsAlwaysSuccessMarriage()` ([`MarriageDifficulty.h:30`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/MarriageDifficulty.h#L30))**:
  - Declared in `MarriageDifficulty.h` but never defined or called anywhere.
---

## Section 6: Household, Children & Package Overrides

### 6.1 `SKSE_Source/include/core/HomeCellService.h` & `SKSE_Source/src/core/HomeCellService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **Inverted Lock-Level Check in `DoorRequiresKey` Collects Only Doors That Do *Not* Require a Key ([`HomeCellService.cpp:39-58`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp#L39-L58))**:
  - Lines 51–57 read:
    ```cpp
    auto lvl = lock->GetLockLevel(&doorRef);
    bool notRequiresKey = (lvl != RE::LOCK_LEVEL::kRequiresKey);
    // True only when the lock level explicitly requires a key
    return notRequiresKey;
    ```
  - Despite the comment (`// True only when the lock level explicitly requires a key`), the function computes `lvl != RE::LOCK_LEVEL::kRequiresKey` and returns `notRequiresKey`!
  - Consequently, doors whose lock level is `kRequiresKey` return `false` and are **excluded** from `HomeCellData::doors`, while pickable locked doors (Novice–Master) that happen to have an associated key return `true`.
- **`ScanCell` Mutates `bedsWithOwners_` Before Knowing Whether the Cell Has Any Persistent Actors ([`HomeCellService.cpp:135-141`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp#L135-L141), [`HomeCellService.cpp:175-181`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp#L175-L181))**:
  - In `BuildIndex()`, `ScanCell(cell, hdata)` is called and its result is discarded if `!hasPersistentActor` (`if (!hasPersistentActor) continue;`).
  - However, inside `ScanCell`, `ProcessBedOwnership(*ref)` ([`HomeCellService.cpp:178`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp#L178)) directly mutates `this->bedsWithOwners_`, polluting `bedsWithOwners_` with beds from cells that were rejected from `homeCells_`.

#### 🟡 What Can Be Improved
- **Repeated `RE::TESForm::LookupByID` Calls Inside `IsHomeLocation` Per Cell ([`HomeCellService.cpp:25-36`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp#L25-L36))**:
  - `LookupKeyword(KEYWORD_DWELLING)` and `LookupKeyword(KEYWORD_HOUSE)` are called for every single interior cell in `dataHandler->interiorCells`. They should be looked up once at the start of `BuildIndex()`.
- **Hand-Rolled `std::ostringstream` Hex Formatting in `FormatFormID` ([`HomeCellService.cpp:83-87`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/HomeCellService.cpp#L83-L87))**:
  - Can use `std::format("0x{:08X}", formID)` instead of `std::ostringstream`.

---

### 6.2 `SKSE_Source/include/core/PlayerHouseService.h` & `SKSE_Source/src/core/PlayerHouseService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **`RelocateFamilyUnit` Conflates `bioMotherID` / `bioFatherID` with Custodial Guardians (`guardianAID` / `guardianBID`), Violating `AGENTS.md` §1 and Stealing Children Across Polygamous / Affair Households ([`PlayerHouseService.cpp:439-491`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp#L439-L491), [`PlayerHouseService.cpp:513-521`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp#L513-L521))**:
  - Per `AGENTS.md` §1 (*Guardians vs. Bio Parents*):
    > **Guardians**: Official custodial caretakers... **Bio Parents**: Genetic lineage only. Never conflate with guardians.
  - In `RelocateFamilyUnit`:
    1. Lines 439–440 and 453–457 check `matches(bM, kPlayerFormID) || matches(bF, kPlayerFormID)` to determine whether a child belongs to the Player's household and who the custodial spouse is. If the Player fathered a child in an affair with a married NPC (`bioFatherID == kPlayerFormID`) whose custody belongs to `(NPC Mother, NPC Husband)` (`guardianAID == mother, guardianBID == husband`), `RelocateFamilyUnit` treats the child and NPC mother as belonging to the Player's household!
    2. Lines 468–471 and 514–517 collect all children where `targetSpouseID` matches `guardianAID`, `guardianBID`, **`bioMotherID`, OR `bioFatherID`** — without even checking that `kPlayerFormID` is the other guardian!
       - If `targetSpouseID` has a biological child from a prior NPC marriage (where custody is held by the NPC ex-spouse), relocating `targetSpouseID` into a player house steals the NPC ex-spouse's child into the player house!
       - If the Player has two spouses (`Spouse A` and `Spouse B`) and a child whose biological mother is `Spouse B` was given in custody to `(Player, Spouse A)`, relocating `Spouse B` matches `cEntry.bioMotherID == Spouse B` and steals `Spouse A`'s custodial child!
- **`RegisterTenantInPlayerHouse` and `RelocateFamilyUnit` Register Unspawned Synthetic Stage 0/1 Children (`0xFF700000`–`0xFF7FFFFF`) as House Tenants and Inject AI Packages on Them ([`PlayerHouseService.cpp:267-325`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp#L267-L325), [`PlayerHouseService.cpp:540-547`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp#L540-L547))**:
  - `RelocateFamilyUnit` iterates `relMgr.GetAllChildren()`, which includes unspawned Stage 0 (Conceived) and Stage 1 (Infant) children with synthetic IDs (`0xFF700000`–`0xFF7FFFFF`), and calls `childSvc.AssignChildToHouse(cID, playerHouseFormID, false)` -> `RegisterTenantInPlayerHouse(cID, playerHouseFormID, true)`.
  - `RegisterTenantInPlayerHouse` then calls `ApplyStayAtHome(cID, true)` -> `PackageOverrideService::GetSingleton().RegisterActor(cID)`, which passes the synthetic child FormID to `RE::TESForm::LookupByID<RE::Actor>(actorID)` (violating `SKSE_Source/AGENTS.md` §7.2) and registers the synthetic ID in `PackageOverrideService::m_registry`.
- **`Load()` Does Not Sync the `PlayerHousesCount` Global (`0xD75`), Does Not Prune Dead Tenants, and Does Not Re-Apply Housed/Merchant Factions ([`PlayerHouseService.cpp:606-675`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp#L606-L675))**:
  - `RegisterPlayerHouseCell` updates `FormCache::GetSingleton().GetPlayerHousesCount()->value` ([`PlayerHouseService.cpp:178-181`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp#L178-L181)), but `Load()` never updates that global after loading `houses_`.
  - Unlike `SpouseHierarchyManager::Load`, `AffectionService::Load`, and `SpouseAssetsService::Load`, `PlayerHouseService::Load` ([`PlayerHouseService.cpp:659-666`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp#L659-L666)) never checks if resolved tenants are dead.

---

### 6.3 `SKSE_Source/include/core/PlayerChildrenService.h` & `SKSE_Source/src/core/PlayerChildrenService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **`SetIsNewlyAdopted(childID, true, true)` Immediately Clears `isNewlyAdopted` on `childID` Itself ([`PlayerChildrenService.cpp:722-741`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L722-L741))**:
  - Lines 726–738 set `it->second.isNewlyAdopted = isNewlyAdopted;` and then, when `clearSameHouse == true`, loop over `for (auto& [otherID, state] : children_)` and set `state.isNewlyAdopted = false` on **every** child in the house **without checking `if (otherID == childID) continue;`**!
  - If called with `isNewlyAdopted = true, clearSameHouse = true`, `childID`'s own `isNewlyAdopted` flag is immediately overwritten back to `false`.
- **`RollForcegreetForHousehold()` Violates the Single-Slot-Per-House Invariant and Uses Uniform Random Instead of Weighted Random ([`PlayerChildrenService.h:71`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/PlayerChildrenService.h#L71), [`PlayerChildrenService.cpp:423-447`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L423-L447))**:
  - Per `PlayerChildrenService.h:71`: `// Forcegreet (decision 8) — ONE shared per-household slot, weighted random pick`.
  - In `RollForcegreetForHousehold()`:
    1. When building `houseChildrenMap` ([`PlayerChildrenService.cpp:428-432`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L428-L432)), it simply skips children who already have `state.forcegreetPending == true` instead of skipping the entire **house** if any child in that house already has `forcegreetPending == true`! As a result, each subsequent roll adds another child with `forcegreetPending = true` in the same house until every child in the house has forcegreet active simultaneously.
    2. Lines 438–439 use `std::uniform_int_distribution<size_t>` instead of `PickWeightedChild(list)`.
- **Synthetic Child FormIDs Passed to `RE::TESForm::LookupByID<RE::Actor>` Without `IsSyntheticChildID` Guards ([`PlayerChildrenService.cpp:48, 102, 119, 162, 225, 265, 354`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L48))**:
  - `SnapshotChildFactions`, `ClearChildExternalFactions`, `RestoreChildFactions`, `OnChildAddedToHousehold`, `OnChildRemovedFromHousehold`, `AssignChildToHouse`, and `ApplyPackageForCurrentState` all pass `childID` directly to `RE::TESForm::LookupByID<RE::Actor>(childID)` without checking `IsSyntheticChildID(childID)` first (`SKSE_Source/AGENTS.md` §7.2 violation).
- **Thread-Unsafe `static std::mt19937` Mutated Inside `const` Methods `PickWeightedChild` and `GetChildrenWithoutScene` ([`PlayerChildrenService.cpp:893`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L893), [`PlayerChildrenService.cpp:948`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L948))**:
  - `static std::mt19937 gen(std::random_device{}());` is shared across threads without synchronization. Concurrent calls from Papyrus VM threads race on `gen`'s internal state array. Use `thread_local std::mt19937`.

#### 🔵 What Can Be Refactored / Organized Better
- **Duplicate Pet vs. Critter Logic ([`PlayerChildrenService.cpp:449-593`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L449-L593) vs. [`PlayerChildrenService.cpp:595-720`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L595-L720) & [`PlayerChildrenService.cpp:1139-1177`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L1139-L1177))**:
  - `AssignPet` / `AssignCritter`, `RemovePet` / `RemoveCritter`, `IsPetRetryAvailable` / `IsCritterRetryAvailable`, and the pet/critter blocks in `ReassertAllOnLoad` are nearly identical and can share an internal helper parameterized on the slot.

---

### 6.4 `SKSE_Source/include/core/PackageOverrideService.h` & `SKSE_Source/src/core/PackageOverrideService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **Data Race on Whitelist Vectors and `m_questPriorityThreshold` in `PackageStartHook::thunk` ([`PackageOverrideService.cpp:559-605`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L559-L605))**:
  - `PackageStartHook::thunk` executes on the engine's AI worker threads. It acquires `std::shared_lock lock(svc.m_mutex)` only for `m_registry.count(...)` ([`PackageOverrideService.cpp:438-440`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L438-L440)) and inside `GetBasePackageForActor` ([`PackageOverrideService.cpp:528`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L528)).
  - It then calls `svc.MatchesWhitelist(editorID)`, `svc.MatchesPluginWhitelist(candidate)`, `svc.MatchesQuestWhitelist(questEdid)`, and reads `svc.m_questPriorityThreshold` **without holding `m_mutex`**, while `LoadConfig` mutates those vectors under `std::unique_lock lock(m_mutex)`.
- **Unlocked Iteration of `m_registry` in `RebuildFromTenants()` ([`PackageOverrideService.cpp:716-738`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L716-L738))**:
  - At lines 716–719, `RebuildFromTenants()` swaps `m_registry = std::move(newRegistry)` under `std::unique_lock lock(m_mutex)` and immediately releases the lock.
  - Lines 721–738 then read `m_registry.size()` and iterate `m_registry` **twice without holding `m_mutex`**, while calling `ReassertBasePackage(actor, pkg)`! Any concurrent `RegisterActor` / `UnregisterActor` call invalidates `m_registry` iterators mid-loop.
- **`RegisterPetCritter` and `RegisterNonPlayerChild` Silently Fall Back to Adult Spouse Sandbox Package (`0x6A`) If Called Before `LoadConfig` ([`PackageOverrideService.cpp:344-379`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L344-L379), [`PackageOverrideService.cpp:413-420`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L413-L420))**:
  - In `RegisterActorWithPackage(actorID, targetPackageID)`, if `targetPackageID == 0` (e.g. if `FormCache` hadn't resolved `0xC0` or `0x376` yet), `m_registry[actorID]` is set to `0`.
  - Later, `GetBasePackageForActor(actorID)` ([`PackageOverrideService.cpp:419`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L419)) treats `it->second == 0` as `m_baseSandboxPkgID` (`0x6A`, the adult spouse home sandbox package) instead of the pet (`0xC0`) or non-player child (`0x376`) package!
- **Synthetic Child FormIDs (`0xFF700000`–`0xFF7FFFFF`) Matched by `(childID >> 24) == 0xFF` Check in `RebuildFromTenants()` ([`PackageOverrideService.cpp:704`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L704))**:
  - Line 704 checks `if ((childID >> 24) == 0xFF && entry.stage == ChildStage::Child)` without checking `!IsSyntheticChildID(childID)` (which is `0xFF700000`–`0xFF7FFFFF`, also having upper byte `0xFF`!). If a child entry ever has `stage == ChildStage::Child` while still holding a synthetic ID (or if a tenant in `PlayerHouseService` is a synthetic child at line 679), `RebuildFromTenants` passes the synthetic ID to `ActorSvc::GetName(id)` ([`PackageOverrideService.cpp:724`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L724)) and `RE::TESForm::LookupByID<RE::Actor>(actorID)` ([`PackageOverrideService.cpp:731`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L731)).

#### 🔵 What Can Be Refactored / Organized Better
- **Duplicate `ToLower` and `Trim` Helpers ([`PackageOverrideService.cpp:34-46`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L34-L46))**:
  - Re-implements `ToLower` and `Trim` locally instead of using `Utils::ToLower` and `Utils::Trim` from `StringUtils.h`.
- **Large Commented-Out Debug Blocks ([`PackageOverrideService.cpp:457-526`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp#L457-L526))**:
  - 70 lines of commented-out debug logging clutter `PackageStartHook::thunk`.
---

## Section 7: Intimate Threads, Banter & Matchmaking

### 7.1 `SKSE_Source/include/core/IntimateThreadService.h` & `SKSE_Source/src/core/IntimateThreadService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **`ReportIntimateInteraction` Does Not Check `victimID == partnerID`, Allowing an Actor to Be Recorded as Cheating on Their Partner *With* That Same Partner ([`IntimateThreadService.cpp:239-276`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L239-L276))**:
  - In `processCheater` ([`IntimateThreadService.cpp:243-274`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L243-L274)), `for (RE::FormID victimID : pc.cachedVictimIDs)` never checks `if (victimID == partnerID) continue;`.
  - If a committed partner was not in the initial `RegisterIntimateThread` actor list (e.g., joined the scene dynamically before `RegisterIntimateThread` was re-called), `ReportIntimateInteraction` sees `victimActor == partnerActor` at distance `0.0f` (`CheckVictimExposure == true`) and calls `relMgr.RecordCheating(cheaterID, victimID, victimID, false, ...)` — recording that `cheaterID` was caught cheating on `victimID` with `victimID`!
  - Similarly, `processOpenWatcher` ([`IntimateThreadService.cpp:288-315`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L288-L315)) and `PollActiveThreads` ([`IntimateThreadService.cpp:406, 480`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L406)) never check `if (victimID == affairPartnerID) continue;` or `if (witnessID == sexPartnerID) continue;`.
- **Background Worker Thread Joined in Static Singleton Destructor (`~IntimateThreadService`) Causes Potential Windows Loader-Lock Deadlock at Process Exit ([`IntimateThreadService.cpp:22-24`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L22-L24), [`IntimateThreadService.cpp:53-63`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L53-L63))**:
  - `IntimateThreadService` is a function-local static singleton (`GetSingleton()`) that spawns `std::thread workerThread_` during `Initialize()`.
  - `Shutdown()` is never called during gameplay shutdown; it is only called from `~IntimateThreadService()` during C++ static destruction (`DLL_PROCESS_DETACH`).
  - Calling `workerThread_.join()` inside `DLL_PROCESS_DETACH` while the Windows Loader Lock is held can deadlock process termination if the thread is blocked or attempting thread teardown, and `MARAS_LOG_INFO("IntimateThreadService shutdown")` ([`IntimateThreadService.cpp:35`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L35)) accesses `LoggingService::GetSingleton()` after its static destructor may have already run.

#### 🟡 What Can Be Improved
- **Stale `cachedVictimIDs` / `cachedPartnerIDs` Snapshot Across Long Threads ([`IntimateThreadService.cpp:125-164`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L125-L164), [`IntimateThreadService.cpp:399-451`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp#L399-L451))**:
  - `cachedVictimIDs` is computed once in `RegisterIntimateThread`. If a relationship status changes during the scene (e.g., via dialogue/action before or during the thread), `ReportIntimateInteraction` does not re-verify that `(cheaterID, victimID)` is still a closed `IsMainTier` relationship.

---

### 7.2 `SKSE_Source/include/core/BanterCoordinatorService.h` & `SKSE_Source/src/core/BanterCoordinatorService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **Severe Log Spam: Unconditional `SKSE::log::info` Inside `IsValidBanterActor` and `GetNearbyLivingActors` Per Actor ([`BanterCoordinatorService.cpp:44-117`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L44-L117), [`BanterCoordinatorService.cpp:137, 152`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L137))**:
  - `GetNearbyLivingActors` and `IsValidBanterActor` call `SKSE::log::info(...)` directly (bypassing `MARAS_LOG_*` category filtering!) for **every single actor** in `RE::ProcessLists::GetSingleton()->highActorHandles` and every actor reference in the current interior cell on every banter scan.
  - A single call to `FindBanterCoupleCandidates` writes 50–200+ `[info]` lines to `MARAS.log`, causing disk I/O spikes and flooding the log file.
- **`FindBanterCoupleCandidates` Consumes Per-Couple Cooldown Before the Scene Is Confirmed to Play ([`BanterCoordinatorService.cpp:226-231`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L226-L231), [`BanterCoordinatorService.cpp:273-278`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L273-L278))**:
  - `FindBanterCoupleCandidates` immediately calls `SetCooldown(pickedA->GetFormID(), pickedB->GetFormID(), currentDay)` before returning the pair to Papyrus, even though `RecordScenePlayed(actorA, actorB)` ([`BanterCoordinatorService.cpp:325-346`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L325-L346)) exists specifically to record both the global `m_lastSceneDay` and `m_coupleCooldowns[key]` when the scene actually plays.
  - If the CK quest/scene fails to start after candidate selection, the selected couple is locked out on a 24-hour cooldown without ever having played a scene.
- **`Load()` and `Revert()` Clobber `Settings::scenesCooldownHours` from `MARAS_Settings.ini` ([`BanterCoordinatorService.cpp:434-438`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L434-L438), [`BanterCoordinatorService.cpp:482-488`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L482-L488))**:
  - `Load()` reads `m_cooldownHours` from the co-save and overwrites `Settings::GetSingleton().scenesCooldownHours = m_cooldownHours;`, ignoring any changes the user made in `MARAS_Settings.ini`.
  - Worse, on a New Game (`Revert()` without `Load()`), `Revert()` hard-resets `m_cooldownHours = 24.0f` ([`BanterCoordinatorService.cpp:485`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L485)), discarding `Settings::GetSingleton().scenesCooldownHours` loaded from `MARAS_Settings.ini` at startup!
- **Thread-Unsafe `static std::mt19937 rng` in `PickRandomElement` ([`BanterCoordinatorService.cpp:27-32`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L27-L32))**:
  - `static std::mt19937 rng(std::random_device{}());` is mutated without synchronization. Use `thread_local std::mt19937`.

#### 🟡 What Can Be Improved
- **15 Uncached `RE::TESForm::LookupByEditorID<RE::BGSKeyword>` Calls Per `IsLocationAllowedForScenes` Call ([`BanterCoordinatorService.cpp:501-532`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp#L501-L532))**:
  - Looking up 15 location-type keywords by EditorID string on every check should be replaced with cached `RE::BGSKeyword*` pointers in `FormCache`.

---

### 7.3 `SKSE_Source/include/core/MatchmakingService.h` & `SKSE_Source/src/core/MatchmakingService.cpp` ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs
- **`RunDiceRollMatchmaking` Ignores `settings.matchmakingEvaluationStep` When SkyrimNet Is Loaded ([`MatchmakingService.cpp:1129-1133`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1129-L1133))**:
  - At lines 1129–1133:
    ```cpp
    if (IsSkyrimNetAvailable()) {
        ...
        StartVerificationBatch(std::move(matches), true);
    }
    ```
  - Unlike `ProcessScheduledResults` ([`MatchmakingService.cpp:2135`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L2135)) and `VerifyAndApplyScheduledMatch` ([`MatchmakingService.cpp:1806`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1806)), `RunDiceRollMatchmaking` never checks `settings.matchmakingEvaluationStep`!
  - Even if the user disables `bEvaluationStep` (and even if they also disable `bGenerateMemories` and `bUseLLM`), having SkyrimNet installed forces every Dice Roll match through the LLM verification batch (`maras_matchmaking_verify`).
- **`RunGlobalMatchmaking(bool autoApply)` Ignores `matchmakingEvaluationStep` & `matchmakingGenerateMemories` and Emits Unvalidated Duplicate UI Events ([`MatchmakingService.cpp:1206-1211`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1206-L1211), [`MatchmakingService.cpp:1695`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1695), [`MatchmakingService.cpp:2135`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L2135))**:
  - When `autoApply == true`, `RunGlobalMatchmaking` calls `ProcessScheduledResults(respStr, /*isScheduledScan=*/false)` AND immediately calls `ProcessGlobalManualResults(respStr)`.
  - Because `isScheduledScan` is passed as `false`:
    1. Line 2135 (`if (isScheduledScan && !settings.matchmakingEvaluationStep)`) and line 1695 (`if ((isScheduledScan && !settings.matchmakingGenerateMemories) || ...)`) evaluate to `false`, forcing both LLM verification and LLM memory generation even when disabled in settings.
    2. `ProcessGlobalManualResults(respStr)` ([`MatchmakingService.cpp:2193-2225`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L2193-L2225)) parses the LLM JSON a second time without running `ValidateCandidate(r)` and immediately sends unvalidated results (with `applied = false`) to the UI while `ProcessScheduledResults` is asynchronously verifying and applying them in the background.
- **`GetCurrentGameDay()` Uses Fragile `RE::TESForm::LookupByEditorID<RE::TESGlobal>("GameDaysPassed")` Instead of `RE::Calendar::GetSingleton()->GetDaysPassed()` ([`MatchmakingService.cpp:68-73`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L68-L73))**:
  - Without `po3_Tweaks` (or if `GameDaysPassed` is not in the runtime EditorID table), `LookupByEditorID<RE::TESGlobal>("GameDaysPassed")` returns `nullptr`, causing `GetCurrentGameDay()` to always return `0.0f` and breaking all matchmaking intervals (`TryScheduledMatchmaking`) and per-NPC cooldowns (`IsNpcOnCooldown`)!
  - Every other service in `MARAS` uses `RE::Calendar::GetSingleton()->GetDaysPassed()`.
- **`ProcessManualResults` Corrupts `actorAFormID` / `actorAName` When LLM Returns a Non-Target Pair or Unresolved Partner ([`MatchmakingService.cpp:2160-2165`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L2160-L2165))**:
  - Lines 2160–2165 read:
    ```cpp
    if (r.actorBFormID == targetNpcFormID && r.actorAFormID != 0) {
        std::swap(r.actorAFormID, r.actorBFormID);
        std::swap(r.actorAName, r.actorBName);
    } else if (r.actorAFormID != targetNpcFormID && r.actorBFormID != targetNpcFormID) {
        r.actorAFormID = targetNpcFormID;
    }
    ```
  - If the LLM outputs a pair `(X, Y)` that does not include `targetNpcFormID`, the `else if` branch overwrites `r.actorAFormID = targetNpcFormID` without updating `r.actorAName`, silently pairing `targetNpcFormID` with `Y` under `X`'s display name. And if `r.actorBFormID == targetNpcFormID` while `r.actorAFormID == 0`, neither branch executes, leaving the target NPC in `actorBFormID` and `actorAFormID == 0`.
- **Inconsistent Thread Context for `onSuccess` vs. `onFailure` in `RequestMemoriesAsync` ([`MatchmakingService.cpp:1362-1371`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1362-L1371))**:
  - `RequestMemoriesAsync` dispatches `onSuccess` onto the main game thread via `SKSE::GetTaskInterface()->AddTask`, but invokes `onFailure()` directly on the background LLM worker thread.

#### 🔵 What Can Be Refactored / Organized Better
- **Duplicate `StatusToString` and `StringToStatus` Helpers ([`MatchmakingService.cpp:40-66`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L40-L66))**:
  - Duplicates `Utils::RelationshipStatusToString` and `Utils::StringToRelationshipStatus` from `EnumUtils.h`.
- **`CleanRaceDisplayName` Workaround for `NPCIndex` Storing Race EditorID Instead of Display Name ([`MatchmakingService.cpp:20-38`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L20-L38))**:
  - Only exists because `NPCIndex.cpp:150` stores `race->GetFormEditorID()` (`"NordRace"`) instead of `race->GetFullName()` (`"Nord"`).
---

## 8. `NPCRelationshipManager` (`NPCRelationshipManager.h/.cpp`)

### 8.1 `include/core/NPCRelationshipManager.h` & `src/core/NPCRelationshipManager.cpp` (Lines 1–1200: Registration, Status Transitions, Pending Queues & Availability) ✅ Resolved
- **Files**:
  - [NPCRelationshipManager.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NPCRelationshipManager.h)
  - [NPCRelationshipManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1-L1200)

#### 🔴 Critical Issues / Breaking Bugs
1. **[`NPCRelationshipManager::GetActiveRelationships`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L430-L441) Returns ALL Relationship Pairs Including `Candidate`, `Divorced`, `Jilted`, `ExDating`, and `ExLover`**:
   - Despite its name and header contract (`GetActiveRelationships(RE::FormID actorID)`), lines 430–441 iterate `relationshipPairs` and return **every** pair where `key.Contains(actorID)` without filtering out `RelationshipStatus::Candidate`, `Divorced`, `Jilted`, `ExDating`, or `ExLover`:
     ```cpp
     for (const auto& [key, data] : relationshipPairs) {
         if (key.Contains(actorID)) {
             results.push_back(&data);
         }
     }
     ```
   - **Cross-Service Bug**: [`DeceasedRegistry::RecordDeceasedAdult`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L54-L66) calls `relMgr.GetActiveRelationships(refID)` to populate `record.partnersAtDeath`, and [`DeceasedRegistry::WasExOf`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp#L124-L137) comments: *"partnersAtDeath only stores active relationships at the time of death (Married, Engaged, Dating, Lover); ex-statuses (Divorced, Jilted, ExDating, ExLover) are NOT included unless they were active when the NPC died."* Because `GetActiveRelationships` does not filter by `IsActivePartnerStatus(data.status)` (`Dating`, `Lover`, `Engaged`, `Married`), every `Candidate` and every already-broken-up ex is recorded as an active partner at the time of death!
2. **[`NPCRelationshipManager::TryResolvePendingChildren`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L443-L522) Passes Custody Guardians (`parentARef`, `parentBRef`) as Both `bioMotherID` and `bioFatherID`, Violating `AGENTS.md` §1**:
   - In both branch 1 (lines 480–482, when the child's `baseID` resolves) and branch 2 (lines 508–510, when a parent's `baseID` resolves), `TryResolvePendingChildren` calls:
     ```cpp
     RegisterChildActor(refID, pARef, pBRef, 0, pARef, pBRef, 0, 2);
     ```
     and:
     ```cpp
     RegisterChildActor(cRef, pending.parentARefID, pending.parentBRefID, 0, pending.parentARefID, pending.parentBRefID, 0, 2);
     ```
   - Look at [`RegisterChildActor`'s signature](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NPCRelationshipManager.h#L472-L480):
     `RegisterChildActor(childID, guardianAID, guardianBID, adoptedFlag, bioMotherID, bioFatherID, isBioChildFlag, genderFlag)`.
   - Passing `adoptedFlag = 0` (`false`), `bioMotherID = pARef`, and `bioFatherID = pBRef` violates multiple rules in `AGENTS.md` §1:
     1. It completely ignores `pending.isAdopted` (which defaults to `true` in `PendingChildData`) and hardcodes `adoptedFlag = 0` (`isAdopted = false`).
     2. It conflates custodial guardians (`parentARef`, `parentBRef`) with biological parents (`bioMotherID`, `bioFatherID`), setting biological parents on adopted children.
     3. It violates **Strict Slot Neutrality** by assuming slot A (`pARef`) is always the biological mother and slot B (`pBRef`) is always the biological father without checking actor sex or explicit bio parent fields.
3. **[`NPCRelationshipManager::ChangeStatusCommon`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L201-L207) Displays Player HUD Notification (`RE::SendHUDMessage::ShowHUDMessage`) for Blocked Autonomous NPC-NPC Relationships**:
   - At lines 201–207, whenever `CanFormRelationship(npcFormID, targetFormID, status, &blockReason)` returns `false`, `ChangeStatusCommon` unconditionally shows `blockReason` on the player's HUD:
     ```cpp
     if (!blockReason.empty()) {
         RE::SendHUDMessage::ShowHUDMessage(blockReason.c_str());
     }
     ```
   - `ChangeStatusCommon` is called not only for player-initiated transitions, but also for background NPC-NPC matchmaking, autonomous promotions, JSON override loading, and pending relationship resolution. Showing a HUD popup to the player when two background NPCs fail a relationship validation check is jarring and unintended; HUD messages should only be shown when `npcFormID == kPlayerFormID || targetFormID == kPlayerFormID` (or when triggered from UI).
4. **[`NPCRelationshipManager::CanFormRelationship(actorA, actorB, statusName, outReason)`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1169-L1181) Checks Single-Actor `IsAvailableForBreakup` Instead of Pair-Specific `IsAvailableForBreakup(actorA, actorB)`**:
   - At lines 1171–1173:
     ```cpp
     if (lowerStatus == "breakup") {
         return IsAvailableForBreakup(actorA) || IsAvailableForBreakup(actorB);
     }
     ```
   - Because it calls the single-argument overload `IsAvailableForBreakup(actorA) || IsAvailableForBreakup(actorB)`, `CanFormRelationship(actorA, actorB, "breakup")` returns `true` whenever `actorA` is dating/married to *anyone* OR `actorB` is dating/married to *anyone*, even if `actorA` and `actorB` are complete strangers (`Unknown`/`Candidate`) to each other! It should call `IsAvailableForBreakup(actorA, actorB)`.
   - Additionally, the `"breakup"`, `"open_relationship"`, and `"close_relationship"` branches never populate `*outReason` when returning `false`.
5. **[`NPCRelationshipManager::CanFormRelationship`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1009-L1167) Never Calls `AreFamilyMembers(actorA, actorB, outReason)`**:
   - `CanFormRelationship` validates `actorA == actorB`, `IsChildNPC`, and existing relationship tiers, but **never checks `AreFamilyMembers(actorA, actorB, outReason)`** when forming a romantic status (`Dating`, `Lover`, `Engaged`, `Married`)!
   - As a result, `CanFormRelationship` (and `ChangeStatusCommon`, which relies on `CanFormRelationship`) allows incestuous relationships between parents, children, and siblings unless the caller separately remembered to call `AreFamilyMembers` beforehand.
6. **[`NPCRelationshipManager::UnregisterNPC`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L372-L398) and [`HandleNPCDeath`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L400-L428) Leave Orphaned Tenant Entries in `PlayerHouseService` and Stale Engine Factions on Surviving NPC Partners**:
   - When an NPC dies (`HandleNPCDeath`) or is unregistered (`UnregisterNPC`), lines 390–394 and 419–423 erase all `relationshipPairs` containing `npcFormID` directly via `it = relationshipPairs.erase(it)` without calling `RefreshPartnerRelationshipFactions(partnerID)` on the surviving NPC partners!
   - Consequently, if NPC A and NPC B were married to each other (or to the player) and NPC A dies, NPC B's `RelationshipStatusFaction` (`0x7`), `CouplesFaction` (`0xC23`), and `SpouseShareHouseFaction` ranks are never refreshed (`RefreshPartnerRelationshipFactions(otherID)` is never called), leaving the surviving widow/widower permanently in the `Married` faction rank (`3`) in the engine!
   - Furthermore, neither `UnregisterNPC` nor `HandleNPCDeath` calls `PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcFormID)`.

#### 🟡 What Can Be Improved
1. **Inconsistent Game-Time Units Between [`ChangeStatusCommon`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L211) (`GetCurrentGameTime()`) and [`AffectionService`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp#L31) (`GetDaysPassed()`)**:
   - `ChangeStatusCommon` records `StatusChangeRecord.timestamp` using `calendar->GetCurrentGameTime()`, whereas `AffectionService` uses `calendar->GetDaysPassed()`. These should use a single consistent helper across all services.
2. **Linear Full-Map Scans Across All `IsAvailableFor*` and `GetAll*` Methods**:
   - Every call to `IsAvailableForDating`, `IsAvailableForEngaged`, `IsAvailableForMarried`, `IsAvailableForLover`, `IsAvailableForBreakup`, `GetAllCandidates`, `GetAllDating`, `GetAllMarried`, etc. iterates the entire `relationshipPairs` hash map (`O(P)` where `P` is total pairs across all NPCs). Maintaining a lightweight adjacency index `std::unordered_map<RE::FormID, std::vector<RelationshipPairKey>>` would make per-actor queries `O(degree(actor))` (typically 1–3 entries) instead of scanning all pairs in the world.

---

### 8.2 `src/core/NPCRelationshipManager.cpp` (Lines 1201–2400: Family Detection, Promotions & Co-Save Serialization) — ✅ Resolved
- **File**: [NPCRelationshipManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1201-L2400)

#### 🔴 Critical Issues / Breaking Bugs
1. **[`NPCRelationshipManager::Load`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2330-L2400) Reads Newer Child Registry Fields (`isBioChild`, `uStage`, `trainingLoc`, `secBio`, `childGender`) Unconditionally Without Checking Co-Save Version**:
   - In [`Save()`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1949-L2008), comments note that child registry fields were added across multiple schema evolutions (`version 11+`, `Version 15+ fertility fields`, `Version 16+ lifecycle fields`, `Version 17+ dual-edge secrecy fields`, `Version 19+ child gender`), while `Serialization::kVersion` in [`Serialization.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/Serialization.h#L11) remained `4`.
   - In [`Load()`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2356-L2399), all of those fields are read unconditionally inside the `for (std::uint32_t cIdx = 0; cIdx < childRegCount; ++cIdx)` loop without checking record version or remaining byte length.
   - If a user loads a v4 co-save created before `childGender` (or any earlier field) was added, child 0's `ReadRecordData(childGender)` consumes the first byte of child 1's `oldChild` FormID, misaligning the binary stream for all subsequent children!
   - Worse still, when the stream is misaligned by 1 byte on child 1, `ReadRecordData(nameLen)` (line 2351) or `ReadRecordData(locLen)` (line 2379) reads arbitrary shifted bytes as a `uint32_t` length and immediately calls `childName.resize(nameLen)` / `trainingLoc.resize(locLen)` **with no upper-bound check**, causing an immediate `std::bad_alloc` crash!
2. **[`NPCRelationshipManager::AreFamilyMembers`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1225-L1246) Fails to Normalize RefID vs. BaseID When Comparing Shared Parents Between Two MARAS Children**:
   - In `checkParentChildMaras` (lines 1198–1216), the code carefully checks `possibleParentID`, `NPCIndex::GetRefID(possibleParentID)`, and `ActorSvc::GetActorBase(possibleParentID)->GetFormID()` because `ChildEntry` fields (`guardianAID`, `guardianBID`, `bioMotherID`, `bioFatherID`) may store either a RefID or a BaseID.
   - However, immediately below in the sibling check (lines 1228–1239), it inserts raw `entryA->guardianAID/BID/bioMotherID/bioFatherID` into `parentsA` and checks `parentsA.count(entryB->...)` directly without normalizing RefID/BaseID! If child A's parent was stored as a RefID and child B's parent was stored as a BaseID (or vice versa), `AreFamilyMembers` fails to detect that they are siblings.

#### 🟡 What Can Be Improved
1. **Unbounded String Allocation in [`Load`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2349-L2382)**:
   - Both `nameLen` (line 2351) and `locLen` (line 2379) should be validated against a reasonable maximum (e.g., `nameLen <= 512`, `locLen <= 512`) before calling `.resize()`, and the return value of `serialization->ReadRecordData(str.data(), len)` should be checked.
2. **Redundant Count Wrappers (`GetCandidateCount`, `GetEngagedCount`, etc., [Lines 1769–1795](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L1769-L1795))**:
   - Each `Get*Count(targetFormID)` method allocates and populates a temporary `std::vector<RE::FormID>` via `GetAll*(targetFormID)` only to return `.size()` and immediately discard the heap-allocated vector. Counting matching pairs directly (or using a per-actor adjacency index) avoids heap allocations on every count query.

---

### 8.3 `src/core/NPCRelationshipManager.cpp` (Lines 2401–4000: Faction Management, Infidelity Tracking, Children Registry & Vanilla Relationship Scanning) — ✅ Resolved
- **File**: [NPCRelationshipManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2401-L4000)

#### 🔴 Critical Issues / Breaking Bugs
1. **Null-Pointer Dereference Crash in [`SafeAddToFaction`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2611-L2614), [`SafeRemoveFromFaction`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2616-L2619), and [`SafeRemoveKeyword`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2621-L2628) Before Null Checks**:
   - All three helper functions dereference `faction->GetFormID()`, `keyword->GetFormID()`, `actor->GetName()`, and `actor->GetFormID()` inside `SKSE::log::info(...)` **before** checking whether `faction`, `keyword`, or `actor` is `nullptr`:
     ```cpp
     void SafeAddToFaction(RE::Actor* actor, RE::TESFaction* faction) {
         SKSE::log::info("Adding faction {:08X} to actor {}({:08X})", faction->GetFormID(), actor->GetName(), actor->GetFormID());
         if (faction) Utils::AddToFaction(actor, faction, 0);
     }
     ```
   - If any `FormCache` lookup returns `nullptr` (e.g. `GetIgnoreProposeKeyword()`, `GetPlayerBedOwnershipFaction()`, `GetPotentialHirelingFaction()`), calling `SafeAddToFaction`, `SafeRemoveFromFaction`, or `SafeRemoveKeyword` immediately crashes the game with a null-pointer access violation on the log statement before reaching the null guard!
2. **[`NPCRelationshipManager::ScanExistingRelationships`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L3837-L3867) Blindly Passes `parentARefID` as `bioMotherID` and `parentBRefID` as `bioFatherID` Without Checking Actor Sex, Assigning Fathers as `bioMotherID` and Mothers as `bioFatherID`**:
   - During `ScanExistingRelationships`, each engine `BGSRelationship` with `kParentChildAssocFormID` produces a `ScannedChildEntry{ childBaseID, parentBaseID, false }` (line 3735).
   - At line 3856, for every `ScannedChildEntry`, it calls:
     ```cpp
     RegisterChildActor(childRefID, parentARefID, parentBRefID, 0, parentARefID, parentBRefID, 0, 2);
     ```
   - This causes two severe bugs:
     1. **Gender-Swapped Bio Parents (Violating `AGENTS.md` §1)**: `parentARefID` is whichever parent appeared in that `BGSRelationship` record, and `parentBRefID` is their spouse. When a child has two `BGSRelationship` records (one with the mother, one with the father), the second loop iteration overwrites `childrenByID[childRefID]` (`RegisterChildActor` line 3467). Whenever the father's `BGSRelationship` record is processed second (or when a child has a single father), the male father is stored in `bioMotherID` and the female mother is stored in `bioFatherID`!
     2. **Overwriting First Parent When Parents Are Not Yet in `relationshipPairs`**: If two vanilla parents do not have an active MARAS `Married`/`Dating`/`Lover` pair (e.g., separated/unmarried parents or deferred parent relationship), `parentBRefID` is `0`. The second `ScannedChildEntry` for the second parent completely overwrites `childrenByID[childRefID]` instead of merging into the empty guardian/bio-parent slot, wiping out the first parent!
3. **[`NPCRelationshipManager::RegisterChildActor`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L3463-L3465) Violates `AGENTS.md` §1 by Inferring `isAdopted` From Guardian/Bio-Parent ID Comparison**:
   - `AGENTS.md` §1 states:
     > *"Adoption (`isAdopted`): Represents an explicit adoption process into a household. Never infer `isAdopted` from presence, absence, or count of guardians."*
   - Yet lines 3463–3465 overwrite `entry.isAdopted` with:
     ```cpp
     entry.isAdopted = (entry.guardianAID != entry.bioMotherID && entry.guardianAID != entry.bioFatherID &&
                        entry.guardianBID != entry.bioMotherID && entry.guardianBID != entry.bioFatherID);
     ```
   - Edge-case bug: if an orphan child is registered with `guardianAID = 0, guardianBID = 0, bioMotherID = 0, bioFatherID = 0`, `entry.guardianAID != entry.bioMotherID` (`0 != 0`) is `false`, so `isAdopted` is set to `false`; conversely, if a biological child's `bioMotherID`/`bioFatherID` were not passed (both `0`) while `guardianAID != 0`, the child is falsely marked `isAdopted = true` and fires a bogus `"adopted"` `maras_child_event`!
4. **[`NPCRelationshipManager::ManageFactions`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2668-L2676) Removes `MarriedFaction` From the Player on Divorce Even When the Player Still Has Other Spouses**:
   - At lines 2673–2675, when divorcing an NPC (`targetFormID == kPlayerFormID`, `status == RelationshipStatus::Divorced`), it unconditionally removes `cache.GetMarriedFaction()` from `RE::PlayerCharacter::GetSingleton()` without checking `GetMarriedCount(kPlayerFormID) == 0`.
5. **[`NPCRelationshipManager::EvaluateChildCustodyOnMarriage`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L3889-L3902) (Case 1: Player Marriage) Strips Custody From a Living Co-Guardian NPC and Omits `SendChildEvent`**:
   - Unlike the NPC-x-NPC marriage branch (which checks `entry.guardianAID == 0 || entry.guardianBID == 0` at line 3948 so it only links single-parent children), Case 1 of Player marriage (lines 3890–3902) unconditionally overwrites `entry.guardianAID = kPlayerFormID; entry.guardianBID = spouseID;` even if the child already had a second living guardian (`otherParentID`).
   - It also fails to call `UpdateSpouseChildrenFaction(otherParentID)` for the displaced guardian and forgets to emit `SendChildEvent("guardians_changed", cID, kPlayerFormID, spouseID, 0, "marriage")` (which Case 2 and the NPC-x-NPC branches both emit).

#### 🔵 What Can Be Refactored / Organized Better
1. **Header vs. Implementation Parameter Name/Semantics Mismatch on [`RegisterChildActor`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/NPCRelationshipManager.h#L472-L480)**:
   - In `NPCRelationshipManager.h` (lines 478–480), the parameters are named:
     `(childID, guardianAID, guardianBID = 0, adoptedFlag = 0, bioMotherID = 0, bioFatherID = 0, isBioChildFlag = 0, genderFlag = 2)`.
   - In `NPCRelationshipManager.cpp` (lines 3423–3424), those exact same positional parameters are actually:
     `(childID, guardianAID, guardianBID, houseLocationID, bioMotherID, bioFatherID, syntheticID, stage)`.
   - This severe header/implementation parameter naming mismatch directly caused callers (such as `TryResolvePendingChildren` at lines 481 and 509) to pass wrong values thinking parameter 4 was `adoptedFlag` and parameter 8 was `genderFlag`! The header signature must be updated to match the actual implementation parameters.

---

### 8.4 `src/core/NPCRelationshipManager.cpp` (Lines 4001–5600: Divorce/Death Custody, Bio-Father Step-In, Affair Discovery & JSON Dump) — ✅ Resolved
- **File**: [NPCRelationshipManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L4001-L5600)

#### 🔴 Critical Issues / Breaking Bugs
1. **Container Mutation & Rehash Invalidating `relationshipPairs` Iterators Inside [`SetCheatingDiscovered`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L4729-L4807)**:
   - While iterating `for (auto& [key, pairData] : relationshipPairs)` and `for (auto& rec : pairData.cheatingRecords)`, `SetCheatingDiscovered` synchronously calls:
     - [`SetPairIsOpen(cheaterID, rec.victimID, true)`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L4767), which accesses `relationshipPairs[key]` and calls `BreakAllLoverConnections` -> `PromoteToExLover` -> `ChangeStatusCommon` -> `EvaluateCheatingOnStatusChange`.
     - [`PromoteToDivorced` / `PromoteToJilted` / `PromoteToExDating` / `PromoteToExLover`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L4776-L4784), which call `ChangeStatusCommon` -> `EvaluateCheatingOnStatusChange` (mutating `cheatingRecords`) and `EvaluateChildCustodyOnDivorce` -> `EvaluateBioFatherStepIn` -> `ApplyBioFatherStepIn` -> `PromoteToStatusWith(motherID, bioFatherID, RelationshipStatus::Married)`, which can **insert a new key into `relationshipPairs` and trigger a hash table rehash while `relationshipPairs` is being iterated**!
   - All relationship transitions (`SetPairIsOpen`, `PromoteToDivorced`, etc.) must be collected and executed **after** the `for (auto& [key, pairData] : relationshipPairs)` loop finishes (just as `partnersToBreak` is deferred to lines 4809–4817).
2. **[`NPCRelationshipManager::EvaluateChildCustodyOnDeath`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L4481-L4498) Runs `EvaluateBioFatherStepIn` on an Unrelated Mother and Has Unreachable Dead Code for Unborn Children**:
   - First, in the main loop (lines 4408–4478), for every non-Adult child (including `ChildStage::Unborn`) where `deceasedID` was `guardianAID` or `guardianBID`, the code overwrites `entry.guardianAID = survivingParent; entry.guardianBID = 0;` (lines 4455–4456), erasing `deceasedID` from the entry.
   - Then, at lines 4482–4489, it starts a brand-new loop over `childrenByID`:
     ```cpp
     for (auto& [cID, entry] : childrenByID) {
         if (entry.stage == ChildStage::Adult) continue;
         RE::FormID survivingParent = (entry.guardianAID != 0) ? entry.guardianAID : entry.guardianBID;
         if (survivingParent != 0 && survivingParent != kPlayerFormID && ActorSvc::GetSex(survivingParent) == 1) {
             EvaluateBioFatherStepIn(survivingParent, deceasedID);
             break;
         }
     }
     ```
     Because this second loop does **not** check whether `deceasedID` had anything to do with `entry`, it picks the **very first child in `childrenByID` across the entire game** whose guardian happens to be a female NPC (even if her spouse is alive or she never knew `deceasedID`), triggers `EvaluateBioFatherStepIn(survivingParent, deceasedID)` for her, and `break`s!
   - Then, at lines 4490–4498, a third loop checks `if (entry.stage != ChildStage::Unborn) continue; if (matchesActor(entry.guardianBID, deceasedID))` — which is **completely unreachable dead code** because the first loop (lines 4408–4478) already cleared `entry.guardianBID = 0` for all `Unborn` children whose guardian was `deceasedID`!
3. **[`NPCRelationshipManager::EvaluateChildCustodyOnDivorce`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L3990-L4160) Skips `maras_paternity_secret_resolved` Events for Unborn Children and Overwrites `entry.paternitySecret = PaternitySecret{}`**:
   - In loop 1 (lines 3990–4012), for `ChildStage::Unborn`, it clears `formerSpouseID` from `entry.guardianAID` / `entry.guardianBID` and sets `isSecretForPartner = false` and `isSecretForBioFather = false` without firing `"maras_paternity_secret_resolved"`. Because `formerSpouseID` is already cleared from `guardianAID/BID`, the second loop (lines 4118–4120 / 4145–4148, which requires `hasPlayer && hasSpouse` or `hasA && hasB`) never matches the unborn child!
   - Furthermore, for born children in loop 2, `processChildPaternityAndCustody` (line 4030) calls `ResolvePaternitySecret(cID, true)` (setting `discovered = true`), and then lines 4128 and 4159 immediately overwrite `entry.paternitySecret = PaternitySecret{}` — wiping out `discovered = true`, `isSecretForBioFather` (even if the biological father still doesn't know!), `hasPhenotypeMismatch`, `bioFatherName`, and `bioFatherRaceID`!
4. **[`NPCRelationshipManager::FindChildPaternitySecret`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L5492-L5496) Matches Every `childName` Against Any Child With an Empty `entry.name` (Including All Unborn Children)**:
   - At line 5494:
     ```cpp
     std::string entryNameLower = Utils::ToLower(entry.name);
     if (entryNameLower == nameLower || entryNameLower.find(nameLower) != std::string::npos || nameLower.find(entryNameLower) != std::string::npos) {
         return true;
     }
     ```
   - In C++, `std::string::find("")` returns `0` (`!= std::string::npos`) for any string! Because `Unborn` children (and any unnamed child) have `entry.name == ""`, `nameLower.find("") != std::string::npos` is **unconditionally `true`** for any `childName` query, causing `matchesChildName` to return `true` on every unnamed/unborn child regardless of what `childName` was requested!
5. **`std::shared_ptr` Circular Reference Memory Leak in [`EvaluateBioFatherStepIn`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L4296-L4364)**:
   - `auto processNext = std::make_shared<std::function<void(size_t)>>();` captures `processNext` by value inside the lambda assigned to `*processNext`. Because `*processNext` is never cleared (`*processNext = nullptr;`) when the chain finishes or breaks, the `shared_ptr` reference count never drops to zero, leaking the `std::function`, `candidates` vector, and captured strings on every LLM step-in evaluation.

---

### 8.5 `src/core/NPCRelationshipManager.cpp` (Lines 5601–6983: Pregnancy/Birth Lifecycle, Paternity Secrets & Daily Ticks) — ✅ Resolved
- **File**: [NPCRelationshipManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L5601-L6983)

#### 🔴 Critical Issues / Breaking Bugs
1. **[`NPCRelationshipManager::CheckPaternitySecretsDailyTick`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L6859-L6895) Rolls Visual Resemblance / Phenotype Discovery on Unborn Fetuses (`ChildStage::Unborn`) Using `currentDay` as `childAgeDays`**:
   - At lines 6862–6868:
     ```cpp
     for (auto& [cID, entry] : childrenByID) {
         if (!entry.paternitySecret.HasActiveSecret() || entry.paternitySecret.discovered) {
             continue;
         }

         float childAgeDays = currentDay - entry.birthTimestamp;
         if (childAgeDays <= 0.0f) continue;
     ```
   - When an unborn child is registered in [`RegisterUnbornChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L5671), its `birthTimestamp` is `0.0f` and its stage is `ChildStage::Unborn`.
   - Because `CheckPaternitySecretsDailyTick` does **not** check `if (entry.stage == ChildStage::Unborn || entry.birthTimestamp <= 0.0f) continue;`, for every unborn pregnancy `childAgeDays = currentDay - 0.0f = currentDay` (e.g. `50+` days into a playthrough)!
   - As a result, `childAgeDays > 0.0f` is always true for unborn pregnancies, and `childAgeDays * dailyGrowth` immediately hits `maxChance`, causing `CheckPaternitySecretsDailyTick` to roll and trigger visual physical resemblance / phenotype mismatch discovery on **unborn fetuses** every day before they are even born!
2. **[`NPCRelationshipManager::EvaluatePaternitySecretFallback`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L6675-L6684) Calls `PromoteToStatusWith(..., RelationshipStatus::ExDating)` for Engaged Partners, Causing Breakup to Fail With a HUD Error**:
   - At lines 6675–6684:
     ```cpp
     if (doesBreakup) {
         auto currentStatus = GetRelationshipStatus(motherID, partnerID);
         if (currentStatus == RelationshipStatus::Married) {
             partnerReaction = "divorce";
             PromoteToStatusWith(motherID, partnerID, RelationshipStatus::Divorced);
         } else {
             partnerReaction = "breakup";
             PromoteToStatusWith(motherID, partnerID, RelationshipStatus::ExDating);
         }
     }
     ```
   - If the mother and her deceived partner are currently `RelationshipStatus::Engaged`, `PromoteToStatusWith(motherID, partnerID, RelationshipStatus::ExDating)` fails inside `CanFormRelationship` (line 1092: `"Can only transition to Ex-Dating if currently Dating."`), displays that error on the player's HUD, and leaves the engaged couple together!
   - The `else` branch should call `Breakup(motherID, partnerID)` (or `PromoteToJilted` when `currentStatus == RelationshipStatus::Engaged`).
3. **[`NPCRelationshipManager::PromoteUnbornToInfant`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L5718-L5728) Leaves `paternitySecret` in a Corrupted State (`discoveredByPartner = true`, `isSecretForPartner = true`) and Suppresses Future Discovery**:
   - At lines 5719–5728:
     ```cpp
     if (it->second.paternitySecret.hasPhenotypeMismatch && it->second.paternitySecret.isSecretForPartner) {
         float roll = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 100.0f;
         if (roll < 40.0f) {
             it->second.paternitySecret.discoveredByPartner = true;
             if (it->second.paternitySecret.discoveredByBioFather) {
                 it->second.paternitySecret.discovered = true;
             }
             MARAS_LOG_INFO("[Fertility] Phenotype mismatch noticed at birth for child {:08X}! Partner discovered secret.", uID);
         }
     }
     ```
   - This block has three bugs:
     1. It uses raw C `rand()` with a hardcoded `40.0f%` threshold instead of `Utils::RandomFloat` and `Settings::GetSingleton().paternityPhenotypeBirthChance`.
     2. It sets `discoveredByPartner = true` **without** setting `isSecretForPartner = false`, leaving the secret flags contradictory.
     3. It never calls `EvaluatePaternitySecret(uID)` or `ResolvePaternitySecretForPartner(uID, true)`, so no `maras_paternity_secret_resolved` event is sent and no breakup/LLM evaluation occurs. If `discoveredByBioFather` was true, setting `discovered = true` also prevents `CheckPaternitySecretsDailyTick` from ever evaluating the secret later!
4. **[`NPCRelationshipManager::RegisterFertilityChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L6442-L6454) Improperly Gates Birth Phenotype Mismatch Roll on `IsSkyrimNetAvailable()`**:
   - Line 6443 checks `if (IsSkyrimNetAvailable() && pSec.isSecret && pSec.hasPhenotypeMismatch)` before rolling `paternityPhenotypeBirthChance` and calling `EvaluatePaternitySecret(childID)`.
   - However, `EvaluatePaternitySecret(childID)` (line 6623) **already** routes to `EvaluatePaternitySecretFallback(childID)` when SkyrimNet is unavailable! Gating line 6443 on `IsSkyrimNetAvailable()` disables birth phenotype mismatch evaluation completely for non-SkyrimNet users.
5. **Redundant Mutual Recursion Between [`SetNPCPregnant`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L5583-L5592) and [`RemoveUnbornChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L5740-L5752)**:
   - `SetNPCPregnant(motherID, false)` calls `RemoveUnbornChild(motherID)` (line 5590), and `RemoveUnbornChild(motherID)` calls `SetNPCPregnant(motherID, false)` at line 5750. Line 5750 in `RemoveUnbornChild` should be removed.

#### 🔵 What Can Be Refactored / Organized Better
1. **`NPCRelationshipManager.cpp` Is a 6,983-Line Monolith Mixing 5 Distinct Domains** — ✅ **Resolved**:
   - `NPCRelationshipManager.cpp` has been split into 5 domain-focused translation units (with `CONFIGURE_DEPENDS` enabled in [`CMakeLists.txt`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/CMakeLists.txt#L57-L61)):
     1. [`NPCRelationshipManager.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp): Adult relationship state machine, registration, pending queues, availability, family detection, status promotions, faction management, JSON overrides, and `BGSRelationship` scanning (`ScanExistingRelationships`).
     2. [`NPCRelationshipManager_Serialization.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Serialization.cpp): Co-save serialization (`Clear`, `Save`, `Load`, `Revert`), global statistics (`LogStatistics`, `RecalculateAndUpdateGlobals`, `LogNPCDetails`), and `DumpJson`.
     3. [`NPCRelationshipManager_Infidelity.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Infidelity.cpp): Open relationships (`IsNPCInOpenRelationship`, `GetPairIsOpen`, `SetPairIsOpen`), directional infidelity tracking (`CheatingRecord`, `RecordCheating`, `EvaluateCheatingOnStatusChange`, `SetCheatingDiscovered`), and cheating/open-relationship factions.
     4. [`NPCRelationshipManager_Children.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Children.cpp): Children registry (`childrenByID`, `RegisterChildActor`, `RemoveChild`), custody transfer (`EvaluateChildCustodyOnMarriage/Divorce/Death`), guardian/bio-parent queries, and non-player child AI packages (`UpdateNonPlayerChildPackageOverride`).
     5. [`NPCRelationshipManager_Fertility.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp): Fertility, pregnancy/birth narrative generation (`BuildPregnancyAnnouncedMessage/Json`, `BuildChildBirthMessage/Json`), racial phenotype classification, bio-father step-in (`EvaluateBioFatherStepIn`, `ApplyBioFatherStepIn`), and paternity secrets (`EvaluatePaternitySecret`, `CheckPaternitySecretsDailyTick`).
2. **Duplicated `matches` RefID/BaseID Closure Across 8+ Methods** — ✅ **Resolved**:
   - Replaced all duplicated local `matches` lambdas across `GetChildren`, `GetChildrenOfActor`, `GetCoGuardian`, `IsGuardian`, `GetMotherPartner`, `FindChildPaternitySecret`, `GetChildrenWithActiveSecretsForActor`, `GetUnbornChildren`, `RegisterFertilityChild`, and `FindSyntheticChild` with the shared `NPCRelationshipManager::MatchesActor(stored, target)` helper.

---

## 9. `SkyrimNetIntegration` (`SkyrimNetIntegration.h/.cpp`)

### 9.1 `include/core/SkyrimNetIntegration.h` & `src/core/SkyrimNetIntegration.cpp` — ✅ **Resolved**
- **Files**:
  - [SkyrimNetIntegration.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/SkyrimNetIntegration.h)
  - [SkyrimNetIntegration.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp)

#### 🔴 Critical Issues / Breaking Bugs
1. **[`EvaluateRelationshipStatusChange`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp) Mutates Game State and Engine Factions Directly on SkyrimNet's Background HTTP Thread Without `SKSE::GetTaskInterface()->AddTask`** — ✅ **Resolved**:
   - Marshaled `PromoteToStatusWith` onto the main game thread via `SKSE::GetTaskInterface()->AddTask(...)` with `try / catch` protection.
2. **Uncaught `nlohmann::json::type_error` Across DLL Boundary in [`EvaluateRelationshipStatusChange`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp)** — ✅ **Resolved**:
   - Added `.is_string()` / `.is_boolean()` type checks before extracting JSON values in both `EvaluateRelationshipStatusChange` and `EvaluatePaternitySecret`, wrapped the async callbacks in `try / catch` blocks across the DLL boundary, and passed `nlohmann::json::error_handler_t::replace` to `.dump()`.
3. **[`BuildRelationshipProfileJSON`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp) Leaks Undiscovered Paternity Secrets (`bio_father`, `biological_parents`, `step_parents`, `is_actor_bio_parent = false`) to the Deceived Partner and Child** — ✅ **Resolved**:
   - Added `canActorKnowPaternitySecret` to accurately evaluate perspective knowledge (including when the Player is the deceived partner or unaware bio-father) and masked `biological_parents`, `step_parents`, `bio_father`, `biological_father`, `is_actor_step_parent`, `is_actor_bio_parent`, `relation`, `siblings`, and `paternity_secret` in both `buildChildObj` and `asChildOpt` whenever the secret is hidden from the perspective actor.
4. **[`EvaluatePaternitySecret`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp) Fails to Break Up Engaged Couples or Married Couples When LLM Returns `partner_reaction = "breakup"`** — ✅ **Resolved**:
   - Replaced status-specific `PromoteToStatusWith(..., Divorced / ExDating)` calls with `mgr.Breakup(motherID, presumedFatherID)`.
5. **`std::shared_ptr` Self-Capture Memory Leak in [`EvaluateRelationshipStatusChange`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp)** — ✅ **Resolved**:
   - Explicitly reset `*callbackHolder = nullptr;` on completion, ignore, immediate queue failure, and exception paths.

#### 🟡 What Can Be Improved
1. **Inconsistent RefID/BaseID Matching in `isNear` Helpers** — ✅ **Resolved**:
   - Updated `getRevealChildSecretCandidates` and `getDiscoverChildPaternityCandidates` to use `Utils::MatchesActor`.
2. **Missing Nearby Partner Check for Breakup/Divorce Actions in [`getAvailableRelationshipActions`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp)** — ✅ **Resolved**:
   - Verified that a valid target partner is in `nearby` before advertising `BreakupDating`, `BreakupLover`, `CallOffEngagement`, and `Divorce`.

#### 🔵 What Can Be Refactored / Organized Better
1. **Dead Function Declarations in [`SkyrimNetIntegration.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/SkyrimNetIntegration.h) (`AGENTS.md` §3)** — ✅ **Resolved**:
   - Removed unimplemented `NormalizeSkyrimNetName` and `CalculateSkyrimNetUUID` declarations.
2. **Duplicated 100-Line Lambdas Inside [`RegisterSkyrimNetDecorators`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp)** — ✅ **Resolved**:
   - Defined `getRevealChildSecretCandidates` and `getDiscoverChildPaternityCandidates` once before `getAvailableRelationshipActions` and reused them in both `getAvailableRelationshipActions` and `RegisterDecorator`.

---

## Section 10: `PapyrusInterface`

### 10.1 [PapyrusInterface.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/papyrus/PapyrusInterface.h) & [PapyrusInterface.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp) — ✅ Resolved

#### 🔴 Critical Issues / Breaking Bugs

1. ✅ **Resolved — Data Races & Unsynchronized Multi-Threaded Mutation from Papyrus VM Worker Threads**:
   - Added `GetPapyrusMutex()` (`std::recursive_mutex`) in [PapyrusInterface.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/papyrus/PapyrusInterface.h) and guarded all Papyrus native endpoints accessing `NPCRelationshipManager`, `AffectionService`, `SpouseHierarchyManager`, `PlayerHouseService`, `PlayerChildrenService`, `SpouseAssetsService`, `HomeCellService`, `DeceasedRegistry`, and `NameService` with `std::lock_guard lock(GetPapyrusMutex())`.
   - Replaced the dangling reference read `GetStatusHistory(...)` in `GetStatusHistoryEntry` with `NPCRelationshipManager::GetStatusHistoryEntry(npcID, targetID, index)` returning `std::optional<StatusChangeRecord>` by value.

2. ✅ **Resolved — Eight Pairs of Duplicate / Synonymous Papyrus API Functions Violating `AGENTS.md` §3**:
   - Removed the 7 duplicate `Actor*`-based child/lineage wrappers (`GetChildGuardianA`, `GetChildGuardianB`, `GetChildBioMother`, `GetChildBioFather`, `GetChildMotherPartner`, `SetChildBioMother`, `SetChildBioFather`) and the duplicate `HasBeenCheatedOnBy` function from `PapyrusInterface.h`, `Papyrus*.cpp`, `NPCRelationshipManager.h/.cpp`, and `Scripts/Source/MARAS.psc`.
   - Updated the caller sites in `Scripts/Source/TTM_ServiceChildrenLogic.psc`, `Scripts/Source/TTM_NpcsScenes.psc`, and `Scripts/Source/TTM_ServiceIntimateMoments.psc` to use canonical `GetBioMother(child.GetFormID())`, `GetBioFather(child.GetFormID())`, and `HasCheatedOn(cheater, victim)`.

3. ✅ **Resolved — 13 Unimplemented Dead Function Declarations in `PapyrusInterface.h` & ~60 Undocumented Implementations in `PapyrusInterface.cpp`**:
   - Removed all 13 unimplemented `IsAvailableFor*` declarations from [PapyrusInterface.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/papyrus/PapyrusInterface.h) and added declarations for all previously undocumented native functions, achieving 1-to-1 parity between `PapyrusInterface.h`, `RegisterPapyrusFunctions`, and [MARAS.psc](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/MARAS.psc) (including `ShowUI`, `HideUI`, and `IsUIVisible`).

4. ✅ **Resolved — Unvalidated Enum / Integer Narrowing Casts From Papyrus `Int` Arguments**:
   - Added explicit range checks before narrowing/enum casts in `SetNpcCharacteristics` (`SocialClass::_Count`, `SkillType::_Count`, `Temperament::_Count`), `IsNPCStatusByEnum`, `IsNPCStatusWithByEnum`, `PromoteNPCToStatusByEnum`, `PromoteNPCToStatusWithByEnum`, `GetNPCsByStatusEnum`, `GetStatusCountByEnum`, `RegisterChildActor`, `SetChildStage` (`0..ChildStage::AwayTraining`), `SetChildGender` (`0..2`), and `SetChildGuardian` (`slot == 0 || slot == 1`).

5. ✅ **Resolved — Unvalidated Path Traversal in `ResolvePathInFile`**:
   - `GetJson*` functions operate exclusively on in-memory JSON strings via `JsonService` ([JsonService.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h)) with no file path resolution.

#### 🟡 What Can Be Improved

1. ✅ **Resolved — Synchronous Disk Read and Full JSON Re-Parsing on Every `GetJson*` Call**:
   - `JsonService` parses in-memory JSON strings with a `thread_local` single-entry parse cache (`detail::GetParsedJson`), avoiding repeated parsing across consecutive `GetJson*` calls on the same payload.

2. ✅ **Resolved — 50-Branch Linear `if (lowerKey == ...)` Chains Duplicated Across `GetSetting*` and `SetSetting*`**:
   - Added centralized, thread-safe $O(1)$ map-backed `GetBoolByKey`, `SetBoolByKey`, `GetIntByKey`, `SetIntByKey`, `GetFloatByKey`, and `SetFloatByKey` methods to `Settings` ([Settings.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Settings.h) & [Settings.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/utils/Settings.cpp)), and delegated `PapyrusSettings.cpp` (`GetSetting*` / `SetSetting*`) directly to them.

3. ✅ **Resolved — `SetSettingBool` / `SetSettingFloat` Side Effects Consolidated in `Settings`**:
   - Side effects (`SetEnablePolygamy`, `SetSimulateNoSkyrimNet`, `LoggingService::SetLogLevel`, `BanterCoordinatorService::SetCooldownHours`) are centrally dispatched inside `Settings::Set*ByKey`.

#### 🔵 What Can Be Refactored / Organized Better

1. ✅ **Resolved — Split Monolithic `PapyrusInterface.cpp` Into Domain-Specific Translation Units**:
   - Split the 2,882-line monolithic `PapyrusInterface.cpp` into 5 focused translation units:
     - [PapyrusInterface.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp) (core `RegisterPapyrusFunctions`, diagnostics/logging, keywords, JSON utilities, Prisma UI & confirmation modal, intimate threads, and banter coordinator)
     - [PapyrusRelationship.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusRelationship.cpp) (NPC registration, characteristics/types/determiners, status checks/transitions, partners, availability, open relationships, infidelity, hierarchy, marriage difficulty, buffs, bonuses, affection, and deceased registry)
     - [PapyrusHousehold.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusHousehold.cpp) (player houses, tenants, spouse assets/house sharing, home cells, and home markers)
     - [PapyrusChildren.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusChildren.cpp) (children registry, adoption/household children, pets/critters, fertility/unborn/paternity secrets, guardians/lineage, message builders, and child/critter name generation)
     - [PapyrusSettings.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusSettings.cpp) (generic and global-backed settings accessors and migration flags)

---

## Section 11: UI Layer (`PrismaUI_API.h`, `PrismaUIManager`, `ConfirmationModal`, `PrismaUIDataBridge`)

### 11.1 [PrismaUI_API.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/PrismaUI_API.h), [PrismaUIManager.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/PrismaUIManager.h) & [PrismaUIManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/PrismaUIManager.cpp)

#### 🔴 Critical Issues / Breaking Bugs

1. ✅ **Resolved — Removed Dead `SetGamePaused` / `m_pausedGame` State Management**:
   - Removed the unused `m_pausedGame` member and `SetGamePaused(bool pause)` method from `PrismaUIManager.h` and `PrismaUIManager.cpp`.

2. ✅ **Resolved — Verified `mgr.view == view` Identity in `OnDomReady`**:
   - Added `if (mgr.view != view || !mgr.IsViewValid()) return;` inside the `OnDomReady` task lambda before focusing the view or invoking initial callbacks.

#### 🟡 What Can Be Improved

1. ✅ **Resolved — Downgraded Pre-Modifier Keypress Log to `MARAS_LOG_TRACE`**:
   - Changed the pre-modifier diagnostic log in `ProcessEvent` to `MARAS_LOG_TRACE` and logged `MARAS_LOG_INFO` only after `toggleUIModCtrl`, `toggleUIModShift`, and `toggleUIModAlt` match.

2. ✅ **Resolved — Replaced `InvokeScript` (`Invoke`) with `InteropCall`**:
   - Replaced `PrismaUIManager::InvokeScript` with `PrismaUIManager::InteropCall(const char* functionName, const char* argument)` across `PrismaUIManager` and `PrismaUIDataBridge`, updated `prismaBridge.ts` to parse JSON string payloads received via `InteropCall`, and rebuilt the Prisma UI bundle.

3. ✅ **Resolved — Removed Unused `<iostream>` Include from `PrismaUI_API.h`**:
   - Confirmed `<iostream>` is removed from `PrismaUI_API.h`.

---

### 11.2 [ConfirmationModal.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/ConfirmationModal.h) & [ConfirmationModal.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/ConfirmationModal.cpp)

#### 🔴 Critical Issues / Breaking Bugs

1. ~~**Cooldown Keys Are Built Only From `mainActors`, Leaving `keys` Empty When Player Is the Sole Main Actor**~~ — ✅ **Resolved**: `ConfirmationModal::Show()` now collects non-player cooldown keys from both `mainActors` and `secondaryActors` (deduplicated) and checks if any key is currently on active cooldown (returning code `3` if blocked).
2. ~~**`_cooldowns` Map Is Never Cleared on Save Load / `Revert`**~~ — ✅ **Resolved**: Added `ConfirmationModal::Revert()` to clear `_cooldowns` under `std::unique_lock` and registered `ConfirmationModal::GetSingleton().Revert()` in `Serialization::RevertCallback`.
3. ~~**Undocumented `-1` Latent Return Code When `MessageBoxData` Creation Fails**~~ — ✅ **Resolved**: Replaced `-1` latent return code with `1` (declined/cancelled) when `creator` or `msgData` creation fails.

#### 🟡 What Can Be Improved & 🔵 Refactored

1. ~~**Duplicate JSON Parsing of `paramsJson` in `Show()` and `BuildMessage()`**~~ — ✅ **Resolved**: `Show()` now parses `paramsJson` once to extract both `activity` and `details`, passing them directly to `BuildMessage(...)`. Also updated `BuildDeclineJson` to pass `nlohmann::json::error_handler_t::replace` to `.dump()`.
2. ~~**Move 543-Line Implementation Out of `ConfirmationModal.h` Into `ConfirmationModal.cpp`**~~ — ✅ **Resolved**: Extracted all method implementations and `ModalCallback` into [`ConfirmationModal.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/ConfirmationModal.cpp), leaving [`ConfirmationModal.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/ConfirmationModal.h) as a clean class declaration header.

---

### 11.3 [PrismaUIDataBridge.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/PrismaUIDataBridge.h) & [PrismaUIDataBridge.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/PrismaUIDataBridge.cpp)

#### 🔴 Critical Issues / Breaking Bugs

1. ~~**Uncaught `nlohmann::json` Exceptions Inside `SKSE::GetTaskInterface()->AddTask` Can Crash the Game (`std::terminate`)**~~ — ✅ **Resolved**: Wrapped the `SKSE::GetTaskInterface()->AddTask` lambda body in `HandleAction` with `try / catch (const std::exception&)`, replaced raw `.get<T>()` and `.value()` calls with type-safe helpers (`GetJsonString`, `GetJsonBool`, `GetJsonInt`, `GetJsonFloat`), and passed `nlohmann::json::error_handler_t::replace` to all `.dump()` calls across `PrismaUIDataBridge.cpp`.

2. ~~**Full Iteration Over `RE::TESForm::GetAllForms()` on Every `SendOverviewData()` Call**~~ — ✅ **Resolved**: Replaced the `RE::TESForm::GetAllForms()` load-order scan in `SendOverviewData()` with `NPCIndex::GetSingleton().GetAllEntries(NPCFilter::ChildOnly)`.

3. ~~**Settings Desynchronization Between `SendOverviewData()` and `ProcessUpdateSetting()`**~~ — ✅ **Resolved**: Extracted `BuildSettingsJson()` (adding `toggleUIModAlt` and all 11 `paternity*` settings), added missing branches in `ProcessUpdateSetting()` (`matchmakingExcludePlayerActivePartners`, `logDestination`, `toggleUIKey`, `toggleUIModCtrl`, `toggleUIModShift`, `toggleUIModAlt`), and added `Settings::GetSingleton().SaveToSkyrimNetConfig()` when SkyrimNet-synced settings are updated.

4. ~~**Dead Declaration `BuildRelationshipJson` in `PrismaUIDataBridge.h`**~~ — ✅ **Resolved**: Removed the unimplemented `BuildRelationshipJson` declaration from [`PrismaUIDataBridge.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/PrismaUIDataBridge.h).

5. ~~**Redundant Nested `SKSE::GetTaskInterface()->AddTask` Capturing Raw `RE::Actor*` Pointers Across Frames**~~ — ✅ **Resolved**: Removed the redundant inner `SKSE::GetTaskInterface()->AddTask` wrappers from `SendAllSkyrimNPCs`, `ProcessTeleportPlayerToNpc`, `ProcessTeleportNpcToPlayer`, and `ProcessAddChild` since `HandleAction` already executes on the SKSE main task thread.

6. ~~**`cachedChildRefIDs` Is Never Cleared on Save Load / `Revert`**~~ — ✅ **Resolved**: Added `PrismaUIDataBridge::Revert()` to clear `cachedChildRefIDs` and registered `PrismaUIDataBridge::GetSingleton().Revert()` in `Serialization::RevertCallback`.

#### 🟡 What Can Be Improved & 🔵 Refactored

1. ~~**Avoid Full `SendOverviewData()` Rebuild on Granular Mutations**~~ — ✅ **Resolved**: Added `SendSettingsData()` for `ProcessUpdateSetting`, switched `ProcessSetHierarchyRank` to `OnHierarchyChanged()`, and switched single-NPC mutations (`ProcessSetNPCRelationshipImportance`, `ProcessSetNPCArchetype`, `ProcessSetNPCStayAtHome`, `ProcessAssignNPCHome`, `ProcessResetNPCName`, `ProcessSetChildCritterBanned`, `ProcessSetChildPetBanned`) to `SendNPCDetail(...)`.

2. ~~**Duplicate Legacy Setting Keys Exported in `SendOverviewData` and Accepted in `ProcessUpdateSetting`**~~ — ✅ **Resolved**: Removed deprecated duplicate keys (`"evaluateSecret"`, `"confirmAcceptProposal"`, `"confirmBreakupEngagement"`, `"confirmLowAffectionDivorce"`, `"isMatchmaking"`) across `PrismaUIDataBridge.cpp` and `prisma-src/`, standardizing on `"evaluateChildSecret"`, `"confirmStatusChange_Marry"`, `"confirmBreakup_Engagement"`, `"confirmDivorceDuringLowAffectionQuest"`, and `"fromMatchmaking"`.

3. ~~**Repeated Base-to-Reference FormID Resolution Boilerplate Across 10+ Action Processors**~~ — ✅ **Resolved**: Extracted `ResolveToPlacedRefID(RE::FormID id)` in the anonymous namespace of `PrismaUIDataBridge.cpp` and replaced all repeated base-to-placed reference resolution blocks.

---

## Section 12: Cross-Cutting Synthesis & Prioritized Remediation Roadmap — ✅ Resolved

Across all C++ headers and implementation files in `SKSE_Source/`, all six systemic architectural categories and prioritized remediation items have been resolved:

### Priority 0 — Crash, Memory Corruption & Undefined Behavior Risks ✅ Resolved
1. ~~**Iterator/Reference Invalidation & Re-Entrant Mutation (`NPCRelationshipManager.cpp`)**~~ — ✅ **Resolved**:
   - Eliminated re-entrant hash-map mutations while holding references or iterating (`SetCheatingDiscovered`, `SyncAllMarriedCouplesChildCustody`, `ReevaluateSyntheticChildSecrets`).
2. ~~**Thread-Safety & Concurrency Across Papyrus VM, SkyrimNet Worker Threads, and Main Thread**~~ — ✅ **Resolved**:
   - Guarded Papyrus native endpoints with `GetPapyrusMutex()`, protected `Settings`, `NPCIndex`, `NameService`, `DeceasedRegistry`, `PackageOverrideService`, `BanterCoordinatorService`, and `ConfirmationModal` with mutexes, pre-populated `FormCache::Initialize()` on the main thread at `kDataLoaded`, and marshaled main-thread-only mutations via `SKSE::GetTaskInterface()->AddTask`.
3. ~~**Uncaught Exceptions on SKSE Task Queue & Event Sinks (`std::terminate` CTDs)**~~ — ✅ **Resolved**:
   - Guarded `SKSE::GetTaskInterface()->AddTask` callbacks with `try / catch`, replaced raw `.get<T>()` / `.value()` JSON calls with type-checked extractors, passed `nlohmann::json::error_handler_t::replace` to `.dump()` calls, and replaced throwing `std::stoul` calls with `std::from_chars`.
4. ~~**Dangling Pointer / Lifetime Bugs**~~ — ✅ **Resolved**:
   - Eliminated raw interior pointer returns across locks (`NPCIndex`, `DeceasedRegistry`, `MARAS_GetStatusHistoryEntry`) in favor of value-returning `std::optional` snapshots, and removed redundant nested `AddTask` lambdas capturing raw `RE::Actor*` pointers across frames.

### Priority 1 — Save-Game Serialization & State Bleed Across Save Loads ✅ Resolved
1. ~~**Unbounded Vector & String Reads in Co-Save `Load()` Deserializers**~~ — ✅ **Resolved**:
   - Added explicit byte-length tracking (`hasRemainingBytes`), string length caps (`kMaxChildStringLen`), and collection count sanity caps across `NPCRelationshipManager::Load`, `PlayerHouseService::Load`, `PlayerChildrenService::Load`, `SpouseAssetsService::Load`, `AffectionService::Load`, `BanterCoordinatorService::Load`, `MatchmakingService::Load`, and `DeceasedRegistry::Load`.
2. ~~**Incomplete `Revert()` / State Reset on Save Load**~~ — ✅ **Resolved**:
   - Cleared all runtime maps, synthetic ID counters (`nextSyntheticChildID_`), `baseToRefIDMap`, shared cell/bed ownership (`SpouseAssetsService::Revert`), modal cooldowns (`ConfirmationModal::Revert`), and cached child ref IDs (`PrismaUIDataBridge::Revert`) in `Serialization::RevertCallback`, and reset `PollingService` on load.
3. ~~**Load-Order Index Shift Corruption**~~ — ✅ **Resolved**:
   - Resolved all persisted FormIDs via `serialization->ResolveFormID(...)` while preserving `kPlayerFormID`, `kUnknownFatherFormID`, and synthetic child FormIDs (`0xFF700000`–`0xFF7FFFFF`).

### Priority 2 — `AGENTS.md` Compliance Violations ✅ Resolved
1. ~~**Guardian Slot Gender Assumptions (`AGENTS.md` §1)**~~ — ✅ **Resolved**:
   - Enforced strict guardian slot neutrality (`guardianA/B`) and explicit biological parent assignment (`bioMotherID`/`bioFatherID`) checked by actor sex across `RegisterChildActor`, `TryResolvePendingChildren`, `ScanExistingRelationships`, and `RelocateFamilyUnit`.
2. ~~**SkyrimNet Caller-Level Gating & Non-SkyrimNet Fallback Policy (`AGENTS.md` §2)**~~ — ✅ **Resolved**:
   - Enforced `IsSkyrimNetAvailable()` caller-level gating and non-SkyrimNet fallback paths across `NPCRelationshipManager`, `MatchmakingService`, and `SkyrimNetIntegration`.
3. ~~**Dead / Vestigial Code & Unsolicited API Aliases (`AGENTS.md` §3)**~~ — ✅ **Resolved**:
   - Removed duplicate Papyrus API wrappers, unimplemented header declarations (`PapyrusInterface.h`, `PrismaUIDataBridge.h`, `SkyrimNetIntegration.h`, `MarriageDifficulty.h`), dead `SetGamePaused` in `PrismaUIManager`, dead `Phase 1.7` report sections in `NPCIndexReportWriter`, and legacy setting/action aliases across C++ and `prisma-src/`.

### Priority 3 — Performance & Modularization ✅ Resolved
1. ~~**Eliminate Full Load-Order Scans (`RE::TESForm::GetAllForms()`) at Runtime**~~ — ✅ **Resolved**:
   - Replaced `RE::TESForm::GetAllForms()` scans in `PrismaUIDataBridge::SendOverviewData()` with `NPCIndex::GetSingleton().GetAllEntries(NPCFilter::ChildOnly)`.
2. ~~**Eliminate Synchronous Per-Call JSON Disk Reads**~~ — ✅ **Resolved**:
   - Added `thread_local` single-entry JSON parse caching in `JsonService` (`detail::GetParsedJson`) and non-throwing comment-enabled JSON parsing in `JsonOverrideLoader`.
3. ~~**Unify `Settings` Key Dispatch & Split God Files**~~ — ✅ **Resolved**:
   - Consolidated settings persistence into `ForEachPersistedSetting` and centralized key-based dispatch into `Settings::Get*ByKey` / `Set*ByKey` (shared by both `PapyrusSettings.cpp` and `PrismaUIDataBridge::ProcessUpdateSetting`).
   - Decomposed monolithic `NPCRelationshipManager.cpp` into 5 domain translation units, `PapyrusInterface.cpp` into 5 domain translation units, and extracted `ConfirmationModal.cpp` and `Serialization.cpp`.

