# `SkyrimNetIntegration` (`ApiLoader`, `Decisions`, `Decorators`, `EventService`) — Architecture & Behavioral Guide

**Layer**: Bridge SkyrimNet (`SKSE_Source/modules/bridge_skyrimnet`) & SkyrimNet Bundle (`SKSE/Plugins/SkyrimNet/external/tetherball88.maras`)  
**Sources**:
- [SkyrimNetApiLoader.h](../../../SKSE_Source/modules/bridge_skyrimnet/include/maras/bridge_skyrimnet/SkyrimNetApiLoader.h) / [SkyrimNetApiLoader.cpp](../../../SKSE_Source/modules/bridge_skyrimnet/src/SkyrimNetApiLoader.cpp)
- [SkyrimNetEventPayloads.h](../../../SKSE_Source/modules/bridge_skyrimnet/include/maras/bridge_skyrimnet/SkyrimNetEventPayloads.h) / [SkyrimNetEventPayloads.cpp](../../../SKSE_Source/modules/bridge_skyrimnet/src/SkyrimNetEventPayloads.cpp)
- [SkyrimNetEventService.h](../../../SKSE_Source/modules/bridge_skyrimnet/include/maras/bridge_skyrimnet/SkyrimNetEventService.h) / [SkyrimNetEventService.cpp](../../../SKSE_Source/modules/bridge_skyrimnet/src/SkyrimNetEventService.cpp)
- [EventDispatchPlan.h](../../../SKSE_Source/modules/projection/include/maras/projection/EventDispatchPlan.h) / [EventDispatchPlan.cpp](../../../SKSE_Source/modules/projection/src/EventDispatchPlan.cpp)
- [SkyrimNetDecisions.h](../../../SKSE_Source/modules/bridge_skyrimnet/include/maras/bridge_skyrimnet/SkyrimNetDecisions.h) / [SkyrimNetDecisions.cpp](../../../SKSE_Source/modules/bridge_skyrimnet/src/SkyrimNetDecisions.cpp)
- [SkyrimNetDecorators.h](../../../SKSE_Source/modules/bridge_skyrimnet/include/maras/bridge_skyrimnet/SkyrimNetDecorators.h) / [SkyrimNetDecorators.cpp](../../../SKSE_Source/modules/bridge_skyrimnet/src/SkyrimNetDecorators.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

The `bridge_skyrimnet` module cleanly isolates optional runtime linking to `SkyrimNet.dll` (`GetProcAddress` / exported plugin C APIs) behind `ISkyrimNetGateway`. M.A.R.A.S. never takes a load-time DLL import on SkyrimNet; all features check `IsSkyrimNetAvailable()` at caller sites before running SkyrimNet-only rolls, prompt construction, or event narration.

---

## 2. Runtime API Loader & Caller-Level Gating

- **Dynamic Discovery (`SkyrimNetApiLoader`)**: Includes SkyrimNet's MIT-licensed SDK header ([PublicAPI.h](../../../SKSE_Source/external/skyrimnet/PublicAPI.h)) privately inside `SkyrimNetApiLoader.cpp` and calls `FindFunctions()` once via `std::call_once` when `SkyrimNet.dll` is loaded, resolving version-checked exports (`PublicRegisterEvent`, `PublicRegisterDecorator`, `PublicSendCustomPromptToLLM`, `PublicSendCustomDecisionToLLM`, `PublicFormIDToUUID`, `PublicRecordActorSuccession` / `PublicRecordActorSuccessionByUUID` [v15+], `PublicAddMemory`, `PublicRequestProfileUpdate` [v14+], `PublicGetBioTemplateName`, `PublicGetSaveUniqueID`, `PublicGetMemoriesForActor`, `PublicGetPluginConfigValue`).
- **Testing Simulation Gate**: `IsSkyrimNetAvailable()` returns false when `SkyrimNet.dll` is missing **or** when `SettingsData::simulateNoSkyrimNet` is enabled—letting developers and players exercise non-SkyrimNet deterministic/dice-roll paths without uninstalling SkyrimNet.
- **UUID Resolution & Dynamic `0xFF` Isolation**: `GetActorUUID(formID)` gates on `IsSkyrimNetAvailable()` and calls `PublicFormIDToUUID(formID)` directly on placed actor `RefId`s (never resolving shared `TESNPC` `BaseId` templates, so dynamically spawned `0xFF` Stage 2/4 children never collide on template bases). `RecordActorSuccession` / `RecordActorSuccessionByUUID` (`IsActorSuccessionAvailable()`) link childhood and adulthood UUIDs on SkyrimNet Public API v15+, while `SkyrimNetDecorators` preserves `ChildRecord::skyrimNetUuid` on despawned Stage 3 (`Training`) children in parent prompts.
- **Decisions API (`SkyrimNetDecisions`)**:
  - Prefers `PublicSendCustomDecisionToLLM` (SkyrimNet API v12+) for structured boolean/categorical judgments (`EvaluateStepIn`, `EvaluatePaternity`, `EvaluatePartnerAgency`, `EvaluateMatchmakingVerify`) when `ShouldUseDecisions()` (`skyrimnetUseDecisions && IsDecisionsAvailable()`) is true.
  - **Automatic Chat Fallback**: If a decision request returns `error == "no_decisions_route"` (`ShouldFallbackToChatOnFailure`), callers automatically fall back to `SendCustomPromptToLLM` on the standard Chat route and parse strict JSON objects (`is_object()`, typed getters with defaults).
  - **Prompt Injection Defense**: `SanitizeSingleLine` collapses CR/LF/tabs/repeated spaces and trims bounds, while `SanitizeKey` restricts criterion keys to `[A-Za-z0-9_-]`.
- **Event Planning, Serialization & Suppression (`EventDispatchPlan`, `SkyrimNetEventPayloads`, `SkyrimNetEventService`)**:
  - Domain use cases, fertility hooks, and Papyrus actions route all `ttm_*` events and `direct_narration` through `ISkyrimNetGateway` $\to$ `SkyrimNetEventService` instead of Papyrus JSON string concatenation (`TTM_ServiceSkyrimNet.psc` is retired; `TTM_MainController.psc` retains only startup `SkyrimnetApi.RegisterEventSchema("ttm_event", ...)` registration).
  - `EventDispatchPlan` computes audience isolation (`exactAudience` lists vs. `Nearby`), `DispatchMaskMode` legs, speaker fallback guards (`0x14` Player never selected as speaker for a hidden biological child), and cascade suppression cleanly on immutable snapshots.
  - `SkyrimNetEventPayloads` serializes UTF-8-safe `nlohmann::json` (`error_handler_t::replace`), `SkyrimNetEventService` executes legs with proximity gating and a `std::chrono::steady_clock` action-narration suppression timer (cleared on `Revert()`), and pregnancy/birth events feed [`SkyrimNetMemoryService`](SkyrimNetMemoryService.md) directly in C++.

---

## 3. Epistemic Prompt Decorators (23 `ttm_*` Exports)

At `kPostLoad`, `RegisterSkyrimNetDecorators()` registers **23 `ttm_*` prompt decorators** (evaluated read-only via [`PerspectiveView`](PerspectiveView.md), never mutating state):

| Decorator Group | Registered Names (`23` total) | Perspective & Purpose |
| :--- | :--- | :--- |
| **Actor Profile & Nearby Context** | `ttm_is_registered`, `ttm_npc_relationship_profile`, `ttm_get_nearby_actors`, `ttm_get_available_actions` | `ActorSelf` (or `dialogue_target` public bio) schema-v2 JSON, nearby context, and eligible action list. |
| **Relationship Proposals & Transitions** | `ttm_get_propose_dating_candidates`, `ttm_get_propose_lover_candidates`, `ttm_get_propose_engagement_candidates`, `ttm_get_breakup_dating_candidates`, `ttm_get_breakup_lover_candidates`, `ttm_get_breakup_engagement_candidates`, `ttm_get_divorce_candidates`, `ttm_get_open_relationship_candidates`, `ttm_get_close_relationship_candidates`, `ttm_get_exclusive_main_partners`, `ttm_get_lovers_becoming_ex`, `ttm_get_dating_partner`, `ttm_get_open_main_partners`, `ttm_get_ex_partner` | Filtered through `PerspectiveView::ForActor` for actor-consistent action/target suggestions. |
| **Secrets, Paternity & Affairs** | `ttm_get_reveal_child_secret_candidates`, `ttm_get_discover_child_paternity_candidates`, `ttm_get_confess_affair_candidates`, `ttm_get_expose_affair_candidates`, `ttm_get_resolve_affair_candidates` | Enforces secret ownership: actors only see secrets they personally know (`bioMother`, true father if told, or `ForDashboard` candidate validation where appropriate). |

- **Self vs. Target Masking (`ttm_npc_relationship_profile`)**: Uses `PerspectiveView::ForActor` (`render_mode != "dialogue_target"`) vs. `PerspectiveView::ForDialogueTarget` (`render_mode == "dialogue_target"`). Dialogue target renders expose public relationships (`siblings_public`, public bio parents) and strip undiscovered affairs, secret paternity flags, secret sibling links, and `has_beastblood`—even if the target NPC personally knows them.

---

## 4. SkyrimNet Bundle Integration (`tetherball88.maras`)

Located under [SKSE/Plugins/SkyrimNet/external/tetherball88.maras](../../../SKSE/Plugins/SkyrimNet/external/tetherball88.maras):
- **25 YAML Triggers** (`triggers/*.yaml`): React to MARAS lifecycle and relationship events (proposals, marriage/engagement/lovers, infidelity caught/confessed/exposed/resolved, open relationships & walk-ins, polygamy blessing, pregnancy/labor/birth/loss, child growth/adulthood/training/adoption/inheritance, paternity resolution, affection/hierarchy shifts, and action declines).
- **18 YAML Actions** (`actions/*.yaml`): Expose conversational SkyrimNet actions for proposals, breakups, divorce, opening/closing relationships, confessing/exposing/resolving affairs, revealing/discovering child paternity, and managing relationships. Thin 1-line `TTM_Actions.psc` entrypoints delegate execution natively to C++ `SkyrimNetActionCoordinator` (`Execute*Action` and `GetDivorceResolutionQuestStage`), which handles C++ confirmation modals, post-modal state revalidation, delayed NPC–NPC proposal narrations (`TickPendingNarrations`), `-15.0f` rejection affection penalties, and paternity directive parsing (`PaternityActionRules`).
- **Companion Services**: Memory narration runs through [`SkyrimNetMemoryService`](SkyrimNetMemoryService.md), and placed child bio creation runs through [`SkyrimNetProfileService`](SkyrimNetProfileService.md).
