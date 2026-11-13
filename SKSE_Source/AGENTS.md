# AGENTS.md — SKSE C++ Plugin (`SKSE_Source/`)

Applies to all C++ headers and sources in `SKSE_Source/`. Supplements root `AGENTS.md`; see also [modules/AGENTS.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/modules/AGENTS.md) and [tests/AGENTS.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/tests/AGENTS.md).

---

## 0. Exact Build & Test Commands (Zero Path-Hunting)

Run from workspace root via `.agents/skills/maras-build-and-test`:
```powershell
# Build maras_tests + MARAS.dll + run all headless unit tests
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target all

# Fast test-only build & run (~1-2s, never touches MARAS.dll; optional -Filter "<Substr>")
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/maras-build-and-test/scripts/build-and-test.ps1" -Target tests
```
- Pass `-Reconfigure` after editing `SKSE_Source/CMakeLists.txt` or adding new `.cpp` files.
- **Locked `MARAS.dll` Note**: When Skyrim is open, `SKSE/Plugins/MARAS.dll` is locked by Windows; `MARAS.vcxproj` still compiles/links `build/release/Release/MARAS.dll` cleanly and only the `POST_BUILD` copy (`MSB3073`) fails (handled as `[WARN]` + exit `0` by `build-and-test.ps1`). Never retry in a loop or kill `SkyrimSE.exe`.

---

## 1. Namespaces, Logging, `FormCache` & `NPCIndex`

- **Namespaces & Logging**:
  - Engine/bridge code (`include/`, `src/`) uses `namespace MARAS` and `MARAS_LOG_TRACE/DEBUG/INFO/WARN/ERROR` (`include/utils/Common.h`)—never `std::cout` or raw `spdlog::*`.
  - Pure modules (`modules/`) use `MARAS::Foundation` / `MARAS::State` and `MARAS::Foundation::Log*` (`maras/foundation/Logging.h`), zero engine/JSON/YAML/spdlog includes (see [modules/AGENTS.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/modules/AGENTS.md)).
- **`FormCache` Mandatory**: Cache all `TT_MARAS.esp`, `Skyrim.esm`, `HearthFires.esm`, and integration forms once in `FormCache::Initialize()` at `kDataLoaded`; never call `TESDataHandler::LookupForm` repeatedly at runtime.
- **`NPCIndex` vs. `RE::Actor*`**:
  - Single loaded `RE::Actor*`: call CommonLibSSE methods directly (`IsChild()`, `IsDead()`, `IsDisabled()`, `IsInCombat()`, `IsPlayerTeammate()`)—never query `NPCIndex`.
  - Bulk enumeration: use `NPCIndex::GetSingleton().GetAllEntries()`—**never** call `RE::TESForm::GetAllForms()` at runtime.

---

## 2. SkyrimNet DLL Safety & Faction Mutations

- **SkyrimNet Dynamic Lookup & Gating**: Resolve exports via `GetModuleHandleW(L"SkyrimNet.dll")` + `GetProcAddress`. Check `MARAS::IsSkyrimNetAvailable()` (checks DLL loaded && `!Settings::GetSimulateNoSkyrimNet()`) **at the call site** before preparatory work. Add the 3rd `[Decline (Explain)]` confirmation modal button **only** when `MARAS::IsSkyrimNetAvailable() == true`.
- **Faction Helper Mandatory**: Always mutate actor factions via `Utils::AddToFaction` / `Utils::RemoveFromFaction` (`include/utils/ActorUtils.h`), which wrap `FactionService::InternalFactionScope` to suppress recursive VTable hook triggers and handle relocation fallbacks. Never call raw `actor->AddToFaction` / `RemoveFromFaction` in services.

---

## 3. Concurrency, `SessionEpoch`, Runtime Clones & Prisma UI

- **Main-Thread Execution & `SessionEpoch` (`include/core/SessionEpoch.h`)**:
  - Never touch `RE::Actor` state (`EvaluatePackage`, `MoveTo`, `AddSpell`), UI, Factions, Globals, or Papyrus VM off the main thread; marshal via `AddCurrentEpochTask` / `AddEpochTask`.
  - **Never** capture raw `RE::Actor*`, `RE::TESObjectREFR*`, or `RE::TESForm*` in async/timer lambdas—capture `RE::FormID` / strong IDs and re-validate (`!IsDeleted()`, `IsDead()`, `Is3DLoaded()`) on the main thread.
  - Fence all async LLM callbacks, deferred tasks, and modal queues with `IsSessionEpochCurrent(expectedEpoch)`. Call `ResetPluginEventSinks()` *before* bumping/adopting a new epoch.
- **Lock Ordering (`PapyrusInterface::GetPapyrusMutex()` Outer First)**:
  - Acquire `GetPapyrusMutex()` (`std::recursive_mutex`) before any leaf mutex (`Settings::settingsMutex_`, `AdultChildTravelService::m_mutex`, `PackageOverrideService::m_mutex`, `DeceasedRegistry::mutex_`, `SkyrimNetMemoryService::mutex_`, `PrismaModalManager::m_mutex`). Leaf mutexes must **never** acquire `GetPapyrusMutex()`.
  - Read off-thread `Settings` fields through `std::shared_lock`-guarded getters (`GetSimulateNoSkyrimNet()`, `GetSkyrimNetActionRadius()`), and never return `const char*` into mutex-guarded maps after unlock.
- **Threads & Loader-Lock Safety**:
  - **Never** use `std::thread(...).detach()` for gameplay/matchmaking/timers (use `PollingService` ticks), **never** use `AddUITask` when `AddTask` suffices, **never** create self-capturing `std::shared_ptr` cycles, and **never** `join()` a worker thread inside a static destructor (deadlocks under Windows `LdrpLoaderLock` at `ExitProcess`—isolate queue state in `std::shared_ptr` and `request_stop()` + `detach()`).
- **Runtime-Cloned Forms (`AdultChildProgression`)**:
  - Deep-clone raw pointer sub-arrays (`TESNPC::keywords` via `RE::calloc<RE::BGSKeyword*>`) when cloning `RE::TESNPC` via `RE::malloc`.
  - Verify `!actor->IsDeleted() && actor->GetObjectReference() == cloneNpc` before reverting `SetObjectReference(origNpc)`, and retire unlinked clones into `FlushRetiredForms()` post-load instead of calling `RE::free` inside `RevertCallback`.
- **Prisma UI Focus**: Coordinate view visibility/focus solely via `PrismaUIManager::SyncViewVisibilityAndFocus()` (`IsAvailable()` / `IsViewValid()`); never call raw `Hide`/`Unfocus` while `PrismaModalManager::HasActiveModal()` is true.

---

## 4. Container Stability & `nlohmann::json` Safety

- **`std::unordered_map` Safety**: Never hold an element reference/pointer across a potential map insertion/rehash (`GetOrCreateNPCData`), and never call mutating methods while iterating `npcData` or `childrenByID` directly (snapshot keys into a local `std::vector` first).
- **`nlohmann::json` Safety**:
  - Always pass `nlohmann::json::error_handler_t::replace` to `.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)` so non-UTF-8 localized NPC/cell strings never throw error 316 and CTD.
  - Wrap `AddTask` and SKSE event-sink JSON handlers in `try / catch (const std::exception&)`.
  - Check `.is_string()` / `.is_number()` / `.is_boolean()` before `.get<T>()` or `.value()` (`payload.value("k", def)` still throws on `null` or mismatched type).

---

## 5. Unloaded Placed NPCs, `0xFF` Actors & `'MARS'` v7 Co-Save

- **Unloaded Placed NPCs (`0x00`–`0xFE`)**: `TESForm::LookupByID` can return `nullptr` when an actor's cell is unloaded. Use `ActorSvc::IsValid` / `ActorSvc::GetActorBase` / `HomeCellService` FormID overloads (falling back to `NPCIndex`) so unloaded spouses, tenants, hierarchy, homes, and affection are preserved. Use `pendingRelationshipsByBaseID` / `pendingChildrenByBaseID` for unspawned base-ID registrations.
- **Dynamic `0xFF` Spawned Actors vs. Synthetic Child IDs (`0xFF700000`–`0xFF7FFFFF`)**:
  - Check `IsSyntheticChildID(id)` *before* `ResolveFormID` or `LookupByID`, and never advance `nextSyntheticChildID_` from `0xFF800000+` FormIDs.
  - Prune missing/deleted non-synthetic `0xFF` actors in `FinalizeLoad()`, and key dynamic `0xFF` actors in `DeceasedRegistry` by unique `RefID`—never shared template `BaseID` (prevents FMR sibling poisoning).
- **`'MARS'` v7 Co-Save Invariants** (activate `maras-cosave-and-tests` skill when modifying chunks):
  1. Preserve `v1`–`v7` load compatibility across all 12 chunks (`NPCR`, `SPHR`, `AFCT`, `PHOU`, `SPAS`, `PLCH`, `CFSN`, `MKMK`, `DCSD`, `BNTR`, `ACTR`, `CNFG`).
  2. Enforce `kMaxEntries` caps and check every read; on failure, immediately `Revert()` and `return false` (never `break;` mid-stream).
  3. **Two-Phase Load**: `Load()` performs pure per-service deserialization + `ResolveFormID` (preserving `0` sentinels like `originalBedOwners_`; `'PLCH'` independent of `'CFSN'`). Cross-table pruning, hierarchy gap-filling, faction/bed sync, and `PackageOverrideService` rebuilds (`0xDD3` for traveling Stage 4 adults) run only in `FinalizeLoad()` after all 12 chunks finish.
  4. **Engine-Safe `Revert()`**: Clear all C++ containers, cooldowns, and `PollingService::Reset()` without mutating engine factions/ownership on torn-down forms.

---

## 6. Native Bindings & Slot-Neutral Guardians

- **Bindings Parity**: Keep `PapyrusInterface.h`, `src/papyrus/Papyrus*.cpp`, `RegisterPapyrusFunctions`, and `Scripts/Source/MARAS.psc` in exact signature sync, guarded by `GetPapyrusMutex()`. Always bounds-check `int32_t` Papyrus parameters before casting to `uint8_t` enums. Sync companion C exports in `MARAS_PublicAPI.h/.cpp`.
- **Slot-Neutral Guardians (`ChildRegistryEntry` / `GuardianSlots`)**: Never write positional `guardianAID`/`guardianBID` checks, manual slot-shifts, or `Utils::MatchesActor(0, 0)` comparisons. Always use `HasGuardian`, `HasAnyGuardian`, `GuardianCount`, `IsSingleGuardian`, `GetSingleGuardian`, `IsSoleGuardian`, `GetOtherGuardian`, `HasSameGuardians`, `AssignGuardians`, and `RemoveGuardian`.
