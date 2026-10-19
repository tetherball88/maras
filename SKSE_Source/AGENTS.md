# AGENTS.md — SKSE C++ Plugin (`SKSE_Source/`)

These rules apply to all C++ headers and source files in `SKSE_Source/` (`MARAS.dll`). They supplement the root `AGENTS.md`.

---

## 0. Build & Tooling Prohibitions (STRICT)

- **NEVER search for CMake or try to build C++**: The agent is **NEVER** supposed to search for `cmake`, search for `cmake.exe`, execute `cmake`, invoke MSBuild, or attempt to compile/build the C++ codebase. C++ compilation is handled exclusively by the user outside the agent.

---

## 1. Namespace, Logging & CommonLibSSE Conventions

- **Namespace**: Enclose all declarations and implementations in `namespace MARAS { ... }`.
- **Logging**: Always use `MARAS_LOG_INFO(...)`, `MARAS_LOG_DEBUG(...)`, `MARAS_LOG_WARN(...)`, or `MARAS_LOG_ERROR(...)` macros (`core/Logging.h`). Never use `std::cout` or raw `spdlog::info`.
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

## 5. Thread Safety, Async Tasks, Prisma UI & `PrismaModalManager`

- **Game Thread Execution**: Never interact with `RE::Actor` state (`EvaluatePackage`, `MoveTo`, `AddSpell`, etc.), UI, Factions, Globals, or Papyrus VM state directly from non-game threads. Always wrap calls in `SKSE::GetTaskInterface()->AddTask(...)`.
- **Never Capture Raw Form/Actor Pointers in Async Tasks**:
  - **NEVER** capture raw `RE::Actor*`, `RE::TESObjectREFR*`, or `RE::TESForm*` pointers by value in `SKSE::GetTaskInterface()->AddTask` lambdas, timer callbacks, or background closures—an actor may unload or be deleted before the task runs.
  - Always capture `RE::FormID` (or `RE::ActorHandle`) by value and re-lookup/validate the pointer inside the task body on the main thread.
- **No Detached `std::thread`s for Gameplay or Service Logic**:
  - **NEVER** spawn `std::thread(...).detach()` to run matchmaking, LLM evaluations, or service queries that read `RE::Actor` state or service singletons.
  - Initiate async SkyrimNet requests from the main thread using SkyrimNet's async callback mechanism and marshal callback results back via `SKSE::GetTaskInterface()->AddTask`.
- **No Self-Referencing `std::shared_ptr` Cycles in Async Callbacks**:
  - Never capture a `std::shared_ptr<std::function<...>>` by value inside the lambda stored in that same `shared_ptr` without explicitly resetting it (`*holder = nullptr;`) on all completion and error paths.
- **Prisma UI Management (`PrismaUIManager`)**:
  - Interactions with Prisma UI are encapsulated in `PrismaUIManager`. Always check `IsAvailable()` and `IsViewValid()` before invoking JavaScript routines, and always include `hasSkyrimNet` and `simulateNoSkyrimNet` in settings payloads sent by `PrismaUIDataBridge`.
  - Dashboard visibility (`m_dashboardOpen`) and Modal visibility (`PrismaModalManager::GetSingleton().HasActiveModal()`) are coordinated by `PrismaUIManager::SyncViewVisibilityAndFocus()`. Never call raw `prismaUI->Hide(view)` or `prismaUI->Unfocus(view)` directly when a modal may be active.
- **Prisma UI Modals (`PrismaModalManager` — `include/ui/PrismaModalManager.h`)**:
  - **Always use `PrismaModalManager` instead of native Skyrim `RE::MessageBoxMenu` / `RE::MessageBoxData`** when building confirmation popups, retry/cancel prompts, or interactive modal dialogs.
  - Modals automatically pause the game and focus the view with `FocusMenu` enabled (`prismaUI->Focus(view, /*pauseGame=*/true, /*disableFocusMenu=*/false)`) independently of whether the main dashboard is open. Never pass `disableFocusMenu = true` (which disables PrismaUI's Scaleform cursor/input menu and freezes the cursor).
  - **Building & Showing a Modal**:
    - Construct a `ModalRequest` (`title`, `subtitle`, `message`, `tone` (`ModalTone::Default / Warning / Danger / Success / Ai`), `size` (`ModalSize::Sm / Md / Lg / Xl`), `controls`, `buttons`, `defaultCancelButtonIndex`).
    - Add declarative interactive controls via `ModalControl` builders:
      - `ModalControl::MakeCheckbox(id, label, defaultChecked, description)`
      - `ModalControl::MakeSelect(id, label, options, defaultValue, description)`
      - `ModalControl::MakeTextInput(id, label, defaultValue, placeholder, description)`
      - `ModalControl::MakeInfoBox(id, label, description, tone)`
      - `ModalControl::MakeChecklist(id, label, items, showSelectAllToggle, description)`: each `ModalChecklistItem` supports `id`, `label`, `subtitle`, `description`, `badgeText`/`badgeTone`, `checked`, and an optional per-row inline dropdown (`selectOptions`, `selectValue`, `selectLabel`).
    - Add buttons via `ModalButton::Make(id, label, ModalButtonVariant::Primary / Secondary / Danger / Outline / Ghost)`. To require at least $N$ items checked in a checklist before a button is enabled, set `btn.requireChecklistId` and `btn.minCheckedCount`.
    - Call `PrismaModalManager::GetSingleton().ShowModal(std::move(req), callback)`. The `ModalCallback` (`void(const ModalResponse&)`) always executes on the **SKSE main game thread**.
    - Read user selections from `ModalResponse`: `resp.buttonId`, `resp.buttonIndex`, `resp.aborted`, `resp.GetBool(controlId)`, `resp.GetString(controlId)`, `resp.GetCheckedItemIds(checklistId)`, `resp.GetUncheckedItemIds(checklistId)`, `resp.IsItemChecked(checklistId, itemId)`, and `resp.GetItemSelectValue(checklistId, itemId)`.
- **Mutex Locking**: Protect shared service state with internal mutexes (`std::shared_mutex` / `std::mutex`) when accessed concurrently by Papyrus VM worker threads, SkyrimNet prompt decorators, or UI data bridge builders. Never return raw `const char*` pointers into mutex-protected internal `std::unordered_map` strings after releasing the lock.

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

## 7. Unloaded Actors & Deferred Resolution

- NPCs in Skyrim may not have their 3D loaded or scripts initialized (`!actor->IsInitialized()`) when a save loads or when global background matchmaking runs.
- Never silently drop relationship or child updates when `RE::TESForm::LookupByID<RE::Actor>(formID)` is unloaded. Use `NPCRelationshipManager`'s deferred resolution queues (`pendingRelationshipsByBaseID` and `pendingChildrenByBaseID`, persisted inline in `'NPCR'`), which flush automatically on `TESInitScriptEvent` and `TESObjectLoadedEvent` (`TryResolveForActor`).

---

## 8. SKSE Co-Save Serialization (`'MARS'` v4), `Revert()` & FormID Resolution

All persistent state is serialized in `src/core/Serialization.cpp` under UID `'MARS'` (`kDataVersion = 4`):

1. **Backward Save Compatibility**: Existing player co-saves (`v1`–`v4`) **must** continue to load cleanly. When adding new fields to any of the 11 top-level chunk types (`NPCR`, `SPHR`, `AFCT`, `PHOU`, `SPAS`, `PLCH`, `CFSN`, `MKMK`, `DCSD`, `BNTR`, `CNFG`), bump `kDataVersion` (or the chunk's internal sub-version) and guard new `ReadRecordData` calls with version checks.
2. **Complete `Revert()` Parity**: Every stateful container, cache, cooldown map, timer, or `static` tracking variable in any service or UI class (`NPCRelationshipManager`, `PollingService`, `ConfirmationModal`, `PrismaUIDataBridge`, `BanterCoordinatorService`, `IntimateThreadService`, etc.) **must** be cleared in `RevertCallback` so state never bleeds from Save A into Save B.
3. **Mandatory `ResolveFormID` on All Persisted FormIDs**: Every persisted non-synthetic `RE::FormID`—including nested fields inside structs (`PairRelationshipData`, `StatusTransitionRecord`, `CheatingRecord`, `ChildRegistryEntry`, `SpouseAssetsService`, `BanterCoordinatorService`, `MatchmakingService`)—**must** pass through `a_intfc->ResolveFormID(oldID, resolvedID)` during `Load()`.
4. **Synthetic Child FormIDs (`0xFF700000`–`0xFF7FFFFF`)**:
   - Biological children registered before a physical `RE::Actor` exists use synthetic FormIDs (`NPCRelationshipManager::IsSyntheticChildId`).
   - Any C++ code resolving `RE::FormID` to `RE::Actor*` or calling `a_intfc->ResolveFormID(oldID, resolvedID)` during co-save loading **must** check `IsSyntheticChildId(id)` first and never pass synthetic IDs to `TESForm::LookupByID` or SKSE form resolution.
5. **Bounded Deserialization & `ReadRecordData` Checks**: Always check the `bool` return value of `a_intfc->ReadRecordData(...)` and enforce a sanity cap before calling `.resize(count)` on vectors or strings read from a co-save stream.
6. **Deterministic Load Order**: Keep `NPCR` (which includes relationship pairs, deferred queues, and the child registry) loading before dependent services (`SPHR`, `AFCT`, `PHOU`, `PLCH`).

---

## 9. Native Bindings, Header Parity & Input Validation

- **Papyrus Native Bindings (`PapyrusInterface.h` & `src/papyrus/Papyrus*.cpp` $\leftrightarrow$ `MARAS.psc`)**: Whenever adding, removing, or changing a native function registered in `RegisterPapyrusFunctions` (`src/papyrus/PapyrusInterface.cpp`), immediately update `include/papyrus/PapyrusInterface.h`, the corresponding domain implementation file (`PapyrusInterface.cpp`, `PapyrusRelationship.cpp`, `PapyrusHousehold.cpp`, `PapyrusChildren.cpp`, or `PapyrusSettings.cpp`), and `Scripts/Source/MARAS.psc` with the exact matching signature. Guard native entry points with `std::lock_guard lock(GetPapyrusMutex())` and never leave unimplemented function declarations in `.h` files.
- **Validate Enum Casts from Papyrus `Int` Parameters**: Always bounds-check `int32_t` arguments received from Papyrus before `static_cast`ing to `uint8_t`-backed enums (`RelationshipStatus`, `ChildStage`, `AffairStatus`, gender codes).
- **Public C DLL API (`include/MARAS_PublicAPI.h`)**: When exposing new core C++ capabilities for companion DLLs (such as `MARASUnleashed.dll`), update `MARAS_API` in `MARAS_PublicAPI.h` and `src/MARAS_PublicAPI.cpp`.


