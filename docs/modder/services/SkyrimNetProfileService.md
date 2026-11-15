# `SkyrimNetProfileService` — Architecture & Behavioral Guide

**Layer**: Bridge SkyrimNet (`SKSE_Source/modules/bridge_skyrimnet`)  
**Sources**:
- [SkyrimNetProfileService.h](../../../SKSE_Source/modules/bridge_skyrimnet/include/maras/bridge_skyrimnet/SkyrimNetProfileService.h)
- [SkyrimNetProfileService.cpp](../../../SKSE_Source/modules/bridge_skyrimnet/src/SkyrimNetProfileService.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`SkyrimNetProfileService` generates and updates dynamic SkyrimNet AI character profiles whenever M.A.R.A.S. spawns a physically placed child actor at **Stage 2 (`Child`)** or graduates/promotes them at **Stage 4 (`Adult`)**. Because newly spawned runtime `0xFF` child NPCs lack pre-authored SkyrimNet static bios, the service builds rich biological heritage, guardian family, trait, and recent-memory context prompts and routes profile generation asynchronously to SkyrimNet without stalling the game thread.

---

## 2. Eligibility & Session Deduplication

- **Strict Placed-Actor Gate**: `RequestChildProfileUpdate(childID, stage)` drops calls when SkyrimNet is unavailable, `IsSuppressed()` (or `SkyrimNetMemoryService::IsSuppressed()`) is active (e.g., co-save load replay), `childID` is null/`0x14`/synthetic (`0xFF700000..0xFF7FFFFF`), or `stage` is not `Child` (Stage 2) or `Adult` (Stage 4).
- **Per-Save Stage Deduplication**: `updatedActorStages_` tracks `(actorFormID << 8) | stage` keys under `mutex_` so repeated 3D reloads or world reconciliations never spam duplicate LLM profile rebuilds for the same stage within a save session. `AppLifecycleCoordinator::FinalizeCoSaveLoad` seeds already-placed Stage 2/4 children via `MarkActorStageUpdated`, and `Revert()` clears both `pendingQueue` and `updatedActorStages_`.

---

## 3. Context Rendering & Strict HTTP JSON Validation

Background workers resolve involved adult family members (bio parents and guardians) safely and build context without leaking unmasked secrets into public bios:

- **Primary Local Preview Route**: When SkyrimNet's local web server is reachable, the worker issues HTTP `POST /prompts?api=render-template-preview` (5000 ms timeout) embedding `render_character_profile("ttm_gm", adult.uuid)` blocks and recent family events, keeping the prompt decorator `ttm_npc_relationship_profile` strictly perspective-masked. Template curly braces in dynamic names are sanitized (`{`/`}` $\rightarrow$ `(`/`)`) before template rendering.
- **Strict Response Type Checks**: Parses responses non-throwing (`nlohmann::json::parse(..., false)`) and validates `parsed.is_object()`, `parsed["success"].is_boolean() == true`, and `parsed["renderedContent"].is_string()` before accepting rendered output.

---

## 4. Bounded Worker Queue & Safe Teardown

To prevent unbounded thread growth or use-after-free/loader-lock hangs on process exit:

- **Shared Queue State (`WorkerQueueState`)**: Owned via `std::shared_ptr<WorkerQueueState>` shared with `workerThread_` (`std::jthread`), holding `queueMutex`, `pendingQueue`, `workerActive`, and `std::atomic<bool> shuttingDown`.
- **Bounded Queue (`kMaxPendingTasks = 32`)**: If `pendingQueue` reaches `32` pending requests, the oldest entry is evicted cleanly and its `(childID, stage)` deduplication key is removed from `updatedActorStages_` so it remains eligible if triggered later.
- **Stale-Epoch Protection**: Each `ChildProfileContextData` captures `SessionEpoch`; `EnqueueProfileTask` and `WorkerLoop` drop tasks cleanly whenever `!IsSessionEpochCurrent(taskData->sessionEpoch)` or `stopToken.stop_requested()`.
- **Worker Reuse & Destructor Shutdown**:
  - When `workerActive` flips from false back to true, `EnqueueProfileTask` joins the previous finished `std::jthread` outside locks before launching the next worker.
  - `~SkyrimNetProfileService()` sets `shuttingDown.store(true, release)`, clears `pendingQueue`, signals `workerThread_.request_stop()`, and **detaches** a live `workerThread_` rather than blocking inside Windows CRT static destruction (preventing loader-lock deadlock if an HTTP socket call is mid-flight, while `shared_ptr<WorkerQueueState>` keeps all state valid until the thread exits).
