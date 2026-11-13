# AGENTS.md — SKSE C++ Plugin (`SKSE_Source/`)

These rules apply to all C++ headers and source files in `SKSE_Source/` (`MARAS.dll`). They supplement the root `AGENTS.md`.

---

## 0. Build & Tooling Prohibitions (STRICT)

- **NEVER search for CMake or try to build C++**: The agent is **NEVER** supposed to search for `cmake`, search for `cmake.exe`, execute `cmake`, invoke MSBuild, or attempt to compile/build the C++ codebase. C++ compilation is handled exclusively by the user outside the agent.

---

## 1. Namespace, Logging & CommonLibSSE Conventions

- **Namespace**: Enclose all declarations and implementations in `namespace MARAS { ... }`.
- **Logging**: Always use `MARAS_LOG_TRACE(...)`, `MARAS_LOG_DEBUG(...)`, `MARAS_LOG_INFO(...)`, `MARAS_LOG_WARN(...)`, or `MARAS_LOG_ERROR(...)` macros (`include/utils/Common.h`). Never use `std::cout` or raw `spdlog::info`.
  - Use `MARAS_LOG_ERROR` and fail fast when critical config fields or required forms are missing (never silently guess fallbacks by string/name).
  - Use `MARAS_LOG_INFO` only for high-level state transitions (one summary line per transition with `{:08X}` FormIDs, names, and key outcomes).
  - Use `MARAS_LOG_DEBUG` for intermediate resolution steps (e.g., `ActorBase` / `TESClass` / `TESCombatStyle` / `BGSOutfit` resolution, `kLoad3D` visual/gear enforcement, formula inputs) so issues are diagnosable without flooding `INFO`.
  - Use `MARAS_LOG_TRACE` for per-item or high-frequency loops.
- **NPCIndex Scope & Usage vs. `RE::TESForm::GetAllForms()`**:
  - `NPCIndex` maps Base FormIDs (`RE::TESNPC`) to Placed Reference FormIDs (`RE::Actor` / `RE::TESObjectREFR`), supports name search (`FindByName`, `SearchBySubstring`, `GetRefID`, `GetBaseID`), and provides pre-indexed bulk iteration (`GetAllEntries()`).
  - For a **single known actor** (`RE::Actor*`), always use standard CommonLibSSE methods directly (e.g., `actor->IsChild()`, `actor->IsDead()`, `actor->IsDisabled()`, `actor->IsInCombat()`, `actor->IsPlayerTeammate()`). **Never** query `NPCIndex` to check properties of a single loaded `RE::Actor*`.
  - For **bulk enumeration** of all placed NPCs or all child NPCs in the load order (e.g., in `PrismaUIDataBridge`), use `NPCIndex::GetSingleton().GetAllEntries()`. **NEVER** iterate `RE::TESForm::GetAllForms()` at runtime during gameplay, polling, or UI actions.

---

## 2. SkyrimNet DLL Safety & C++ Caller-Level Gating

- **Dynamic DLL Lookup**: Use `GetProcAddress` via `GetModuleHandleW(L"SkyrimNet.dll")` (`SkyrimNetIntegration.cpp`) to avoid link-time errors when SkyrimNet is absent.
- **Caller-Level Gating (`MARAS::IsSkyrimNetAvailable()`)**:
  - Check `MARAS::IsSkyrimNetAvailable()` (which verifies `GetModuleHandleW(L"SkyrimNet.dll") != nullptr` and `!Settings::GetSingleton().GetSimulateNoSkyrimNet()`) **at the call site before** performing preparatory work (dice rolls, data gathering, state mutations):
    ```cpp
    // ❌ BAD: Caller does work then silently fails inside EvaluatePaternitySecret
    if (roll < chance) {
        EvaluatePaternitySecret(childID);
    }

    // ✅ GOOD: Caller checks before doing the work
    if (IsSkyrimNetAvailable() && roll < chance) {
        EvaluatePaternitySecret(childID);
    }
    ```
- **UI Confirmation Dialogs (`ConfirmationModal.h` / `PrismaModalManager.h`)**:
  - Default confirmation dialog presents **2 buttons**: `[Accept]` and `[Cancel]`. The 3rd button `[Decline (Explain)]` **must only be added** if `MARAS::IsSkyrimNetAvailable() == true`.

---

## 3. `FormCache` Mandatory Usage

- Never call `RE::TESDataHandler::GetSingleton()->LookupForm(...)` or `Utils::LookupForm(..., "TT_MARAS.esp")` repeatedly during gameplay, UI bridge construction, or SkyrimNet decorator evaluation.
- All `TT_MARAS.esp`, `Skyrim.esm`, `HearthFires.esm`, and integration forms must be cached once in `FormCache::Initialize()` (`include/core/FormCache.h` & `src/core/FormCache.cpp`) at `kDataLoaded` and accessed via `FormCache::GetSingleton()` getters.

---

## 4. Faction Mutations (`Utils::AddToFaction` / `Utils::RemoveFromFaction`) & `InternalFactionScope`

- **Always Use `Utils::AddToFaction` and `Utils::RemoveFromFaction`** (`include/utils/ActorUtils.h`) when adding, updating, or removing factions on actors (by `RE::FormID` or `RE::Actor*`). Do **not** call raw `actor->AddToFaction(...)` or `actor->RemoveFromFaction(...)` directly in service code.
- `Utils::AddToFaction` and `Utils::RemoveFromFaction` automatically resolve the actor via `RE::TESForm::LookupByID<RE::Actor>`, instantiate `FactionService::InternalFactionScope` so `FactionService` VTable hooks do not trigger recursive callbacks, and handle engine relocation fallbacks for faction removal.

---

## 5. Concurrency, `SessionEpoch`, Runtime Clones & Prisma UI

- **Game Thread Execution & Pointer Capture Prohibition**:
  - Never mutate `RE::Actor` state (`EvaluatePackage`, `MoveTo`, `AddSpell`, etc.), UI, Factions, Globals, or Papyrus VM state from non-game threads; marshal via `AddCurrentEpochTask` / `AddEpochTask` (`include/core/SessionEpoch.h`).
  - **NEVER** capture raw `RE::Actor*`, `RE::TESObjectREFR*`, or `RE::TESForm*` pointers in async task lambdas, timers, or callbacks—capture `RE::FormID` (or `RE::ActorHandle`) by value and re-lookup/validate (`!form->IsDeleted()`, `actor->IsDead()`, `actor->Is3DLoaded()`) on the main thread.
- **`SessionEpoch` Fencing (`include/core/SessionEpoch.h`)**:
  - Fence all async LLM callbacks, deferred main-thread tasks, and modal queues with `SessionEpoch` (`IsSessionEpochCurrent(expectedEpoch)`) so stale tasks from a prior save never execute after `Revert` / `Load`. Reset plugin/service event-sink queues (`ResetPluginEventSinks()`) *before* bumping/adopting a new session epoch.
- **Lock Ordering Invariant (`PapyrusInterface::GetPapyrusMutex()` First)**:
  - `PapyrusInterface::GetPapyrusMutex()` (`std::recursive_mutex`) is the outer domain perimeter lock. Acquire it at main-thread entry points (`PollingService`, `PrismaUIDataBridge::HandleAction`, `FactionService` hooks, marshaled SKSE event sinks), Papyrus natives, and SkyrimNet decorator callbacks.
  - Leaf mutexes (`Settings::settingsMutex_`, `AdultChildTravelService::m_mutex`, `PackageOverrideService::m_mutex`, `DeceasedRegistry::mutex_`, `SkyrimNetMemoryService::mutex_`, `PrismaModalManager::m_mutex`) must **never** acquire `GetPapyrusMutex()` while held.
  - Read `Settings` fields accessed by off-thread callers (`GetSimulateNoSkyrimNet()`, `GetSkyrimNetActionRadius()`) through `std::shared_lock`-guarded getters, and never return raw `const char*` into mutex-guarded maps after unlock.
- **Background Thread & Loader-Lock Safety**:
  - **NEVER** spawn `std::thread(...).detach()` for gameplay, matchmaking, or service logic (use `PollingService` ticks + async SkyrimNet callbacks), and **NEVER** call `AddUITask` when `AddTask` suffices.
  - **NEVER** blockingly `join()` a worker `std::thread` / `std::jthread` inside a static singleton destructor (deadlocks under Windows `LdrpLoaderLock` during `ExitProcess`); isolate queue state in `std::shared_ptr` and call `request_stop()` + `detach()` on shutdown.
  - Never create self-referencing `std::shared_ptr` cycles in async callback holders.
- **Runtime-Cloned Engine Forms (`AdultChildProgression`)**:
  - When cloning `RE::TESNPC` via `RE::malloc`, deep-clone raw pointer sub-arrays (`TESNPC::keywords` via `RE::calloc<RE::BGSKeyword*>`) so keyword mutations never free shared template heap memory.
  - Before reverting `actor->SetObjectReference(origNpc)`, verify `!actor->IsDeleted() && actor->GetObjectReference() == cloneNpc` (preventing recycled `0xFF` FormID clobbering), and retire unlinked cloned `TESNPC`/`TESClass` forms into a deferred list flushed post-load (`FlushRetiredForms()`) rather than calling `RE::free` inside `RevertCallback` before engine GC.
- **Prisma UI & `PrismaModalManager` (`include/ui/PrismaModalManager.h`)**:
  - Coordinate view visibility and focus exclusively through `PrismaUIManager::SyncViewVisibilityAndFocus()` (checking `IsAvailable()` / `IsViewValid()`); never call raw `Hide` or `Unfocus` while `PrismaModalManager::HasActiveModal()` is true.
  - Always use `PrismaModalManager` (never native `RE::MessageBoxMenu`) with `Focus(view, /*pauseGame=*/true, /*disableFocusMenu=*/false)`. See [docs/modder/services/PrismaModalManager.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/PrismaModalManager.md) for declarative `ModalRequest` / `ModalControl` builders.

---

## 6. Container Reference Stability & `nlohmann::json` Safety

### `std::unordered_map` Reference & Iteration Safety
- **No Simultaneous References Across Map Insertions**: Never hold a reference or pointer to an element in an `std::unordered_map` (e.g. `auto& dataA = GetOrCreateNPCData(a, b);`) while calling `GetOrCreateNPCData(a, c)` or any function that may insert into the same map and trigger a rehash (which invalidates `dataA`).
- **No Mutation During Map Iteration**: Never call mutating methods (`PromoteToStatusWith`, `SetChildGuardians`, `EvaluatePaternitySecret`, `PromoteSyntheticChild`, `RemoveChild`, etc.) while iterating directly over `npcData` or `childrenByID`. Always copy target keys/IDs into a local `std::vector` snapshot first, then iterate the snapshot.

### `nlohmann::json` Exception & UTF-8 Safety
- **Always Use `error_handler_t::replace` in `.dump()`**: Skyrim NPC names, cell/location names, and plugin strings from localized mods may contain non-UTF-8 bytes (e.g., Windows-1252 / CP1251). Calling `json.dump()` with default `error_handler_t::strict` throws `nlohmann::json::type_error` (error 316) and crashes the game. Always pass `nlohmann::json::error_handler_t::replace` (e.g., `json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)`).
- **Guard Task Queue & Event Sink JSON Handlers with `try / catch`**: Always wrap the body of `SKSE::GetTaskInterface()->AddTask` lambdas and SKSE event sinks that parse or read external JSON (`PrismaUIDataBridge`, `SkyrimNetIntegration`, `QuestEventHandler`) in `try / catch (const std::exception&)`.
- **Type-Check Before `.get<T>()` or `.value()`**: In `nlohmann::json`, `payload.value("key", defaultVal)` still throws `nlohmann::json::type_error` if `"key"` is present in the JSON with `null` or a different type (e.g. `"10"` instead of `10`). Always verify `.is_string()`, `.is_number()`, or `.is_boolean()` before extracting values from untrusted JSON payloads.

---

## 7. Unloaded Placed NPCs, Dynamic `0xFF` Actors & Deferred Resolution

- **Unloaded Non-`0xFF` Placed NPCs**: Non-persistent exterior/interior placed `ACHR` references can return `nullptr` from `RE::TESForm::LookupByID` while their cell is unloaded. Always use `ActorSvc::IsValid(formID)` / `ActorSvc::GetActorBase(formID)` / `HomeCellService` FormID overloads (which fall back to `NPCIndex`) so unloaded spouses, tenants, hierarchy ranks, shared homes, and affection scores are never wiped on load or status transitions.
- **Dynamic `0xFFxxxxxx` Spawned Actors vs. Synthetic Child IDs**:
  - Dynamically spawned `PlaceAtMe` actors (`(id >> 24) == 0xFF && !IsSyntheticChildID(id)`) exist in `TESForm::LookupByID` whenever alive in the save; if missing or `form->IsDeleted()`, prune them during `FinalizeLoad()`.
  - In `DeceasedRegistry`, key dynamic `0xFF` actors by unique `RefID` (never shared template `BaseID`, which would mark all FMR/spawned siblings deceased when one dies).
- **Deferred Queues**: Use `NPCRelationshipManager`'s `pendingRelationshipsByBaseID` and `pendingChildrenByBaseID` (persisted inline in `'NPCR'`) when base-ID registrations await placed-actor initialization (`TESInitScriptEvent` / `TESObjectLoadedEvent`).

---

## 8. SKSE Co-Save Serialization (`'MARS'` v7), Two-Phase `FinalizeLoad` & `Revert()`

All persistent state is serialized in `src/core/Serialization.cpp` under UID `'MARS'` (`kDataVersion = 7`):

1. **Backward Save Compatibility (`v1`–`v7`)**: Existing player co-saves **must** load without data loss across all 12 top-level chunks (`NPCR`, `SPHR`, `AFCT`, `PHOU`, `SPAS`, `PLCH`, `CFSN`, `MKMK`, `DCSD`, `BNTR`, `ACTR`, `CNFG`). Bump `kDataVersion` (or chunk sub-version) and guard new `ReadRecordData` calls with version checks when changing binary layouts.
2. **Atomic Deserialization & Bounded Counts**: Enforce a `kMaxEntries` sanity cap before `.resize(count)` / `.reserve(count)` and check every `ReadRecordData(...)` return value. **Never** `break;` mid-loop on a truncated/corrupt stream—immediately `Revert()` the service and `return false;` so misaligned trailing bytes are never parsed into bogus state.
3. **Two-Phase Load (`Load()` $\to$ `FinalizeLoad()`)**:
   - **Phase 1 (`Load()`)**: Pure per-service chunk deserialization + `ResolveFormID`. Never query other services (such as `NPCRelationshipManager` or `DeceasedRegistry`) or require loaded `RE::Actor*` pointers inside `Load()`, and keep `PlayerChildrenService::Load` (`'PLCH'`) independent of `'CFSN'` (`factionSnapshots_`).
   - **Phase 2 (`FinalizeLoad()`)**: Executed in `LoadCallback` after all 12 chunks finish loading to prune deceased/orphaned `0xFF` entries, reconcile factions/bed ownership on loaded refs, run `SpouseHierarchyManager::FillGaps`, and rebuild `PackageOverrideService` (mapping actively traveling Stage 4 `Adult` offspring to `0xDD3`, not young-child `0x376`).
4. **Complete, Engine-Safe `Revert()` Parity**: Every stateful container, cache, cooldown map, polling timer (`PollingService::Reset()`), and `static` tracker must reset in `RevertCallback`. Service `Revert()` methods called during `RevertGlobalData` / `Load` must clear C++ containers only without mutating engine factions or ownership on partially torn-down forms.
5. **Mandatory `ResolveFormID` & Synthetic Child Pool (`0xFF700000`–`0xFF7FFFFF`)**:
   - Pass every persisted non-synthetic `RE::FormID` through `a_intfc->ResolveFormID(oldID, resolvedID)` (preserving explicit `0` sentinels where `0` means "unowned/none", e.g. `SpouseAssetsService::originalBedOwners_`).
   - Check `IsSyntheticChildID(id)` (`0xFF700000`–`0xFF7FFFFF`) before `ResolveFormID` or `TESForm::LookupByID`, and **never** advance `nextSyntheticChildID_` from real `0xFF800000+` spawned actor FormIDs.

---

## 9. Native Bindings, Header Parity & Input Validation

- **Papyrus Native Bindings (`PapyrusInterface.h` & `src/papyrus/Papyrus*.cpp` $\leftrightarrow$ `MARAS.psc`)**: Whenever adding, removing, or changing a native function registered in `RegisterPapyrusFunctions` (`src/papyrus/PapyrusInterface.cpp`), immediately update `include/papyrus/PapyrusInterface.h`, the corresponding domain implementation file (`PapyrusInterface.cpp`, `PapyrusRelationship.cpp`, `PapyrusHousehold.cpp`, `PapyrusChildren.cpp`, or `PapyrusSettings.cpp`), and `Scripts/Source/MARAS.psc` with the exact matching signature. Guard native entry points with `std::lock_guard lock(GetPapyrusMutex())` and never leave unimplemented function declarations in `.h` files.
- **Validate Enum Casts from Papyrus `Int` Parameters**: Always bounds-check `int32_t` arguments received from Papyrus before `static_cast`ing to `uint8_t`-backed enums (`RelationshipStatus`, `ChildStage`, `AffairStatus`, gender codes).
- **Public C DLL API (`include/MARAS_PublicAPI.h`)**: When exposing new core C++ capabilities for companion DLLs (such as `MARASUnleashed.dll`), update `MARAS_API` in `MARAS_PublicAPI.h` and `src/MARAS_PublicAPI.cpp`.

---

## 10. Slot-Neutral Custodial Guardian Operations (`ChildRegistryEntry`)

- **Never Perform Positional `guardianAID` / `guardianBID` Comparisons**:
  - `ChildRegistryEntry::guardianAID` and `ChildRegistryEntry::guardianBID` are strictly interchangeable neutral slots (`(A, B)` is identical to `(B, A)`, and `(A, 0)` is identical to `(0, A)`).
  - **NEVER** write positional inequality checks (`oldGA != entry.guardianAID || oldGB != entry.guardianBID`), single-slot assumptions (`entry.guardianAID != 0 && entry.guardianBID == 0`), or manual slot-shifting (`entry.guardianAID = entry.guardianBID; entry.guardianBID = 0;`).
  - **NEVER** use `Utils::MatchesActor(a, b)` alone to compare two guardian pairs when a slot can be `0`, because `Utils::MatchesActor(0, 0)` returns `false`. Use `Utils::MatchesActorOrEmpty`, `Utils::SameGuardianSet`, `Utils::DiffGuardianSets`, or `ChildRegistryEntry` methods instead.
- **Always Use `ChildRegistryEntry` Slot-Neutral Methods**:
  - Membership & Queries: `entry.HasGuardian(actorID)`, `entry.HasAnyGuardian()`, `entry.GuardianCount()`, `entry.IsSingleGuardian()`, `entry.GetSingleGuardian()`, `entry.IsSoleGuardian(actorID)`, `entry.GetOtherGuardian(actorID)`, `entry.HasSameGuardians(gA, gB)`.
  - Mutations & Diffing: `entry.AssignGuardians(newA, newB)` and `entry.RemoveGuardian(actorID)`, which return a `GuardianSetDiff` (`diff.changed`, `diff.PrimaryRemoved()`, `diff.PrimaryAdded()`) and preserve existing slot positions whenever the guardian set is unchanged or a surviving guardian remains.


