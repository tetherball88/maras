# `AgencyWorkflowCoordinator` & Autonomous NPC Agency — Architecture & Behavioral Guide

**Layers**: App (`SKSE_Source/modules/app`), Domain Agency (`SKSE_Source/modules/domain_agency`), Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [AgencyWorkflowCoordinator.h](../../../SKSE_Source/modules/app/include/maras/app/AgencyWorkflowCoordinator.h) / [AgencyWorkflowCoordinator.cpp](../../../SKSE_Source/modules/app/src/AgencyWorkflowCoordinator.cpp)
- [BanterSceneCoordinator.h](../../../SKSE_Source/modules/app/include/maras/app/BanterSceneCoordinator.h) / [BanterSceneCoordinator.cpp](../../../SKSE_Source/modules/app/src/BanterSceneCoordinator.cpp)
- [AsyncLlmBatchRunner.h](../../../SKSE_Source/modules/app/include/maras/app/AsyncLlmBatchRunner.h) / [AsyncLlmBatchRunner.cpp](../../../SKSE_Source/modules/app/src/AsyncLlmBatchRunner.cpp)
- [PartnerAgencyEvaluator.h](../../../SKSE_Source/modules/domain_agency/include/maras/domain/agency/PartnerAgencyEvaluator.h) / [MatchmakingRules.h](../../../SKSE_Source/modules/domain_agency/include/maras/domain/agency/MatchmakingRules.h) / [NpcMatchmaker.h](../../../SKSE_Source/modules/domain_agency/include/maras/domain/agency/NpcMatchmaker.h)
- [BanterPlanner.h](../../../SKSE_Source/modules/domain_agency/include/maras/domain/agency/BanterPlanner.h) / [BanterSceneStarter.h](../../../SKSE_Source/modules/engine/include/maras/engine/BanterSceneStarter.h)
- [EncounterTemplate.h](../../../SKSE_Source/modules/domain_agency/include/maras/domain/agency/EncounterTemplate.h) / [EncounterConfigLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/EncounterConfigLoader.h)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`AgencyWorkflowCoordinator` orchestrates M.A.R.A.S.'s autonomous living-world systems on top of four pure domain evaluators and three engine helpers:
1. **NPC-to-NPC Matchmaking (`MatchmakingRules` + `NpcMatchmaker`)**: Periodic relationship formation, progression, open/close toggles, and breakups between autonomous Skyrim NPCs, plus UI dashboard match suggestions.
2. **Player Partner Agency (`PartnerAgencyEvaluator` + `EncounterTemplate`)**: Autonomous partner temptations, secret vs. open affairs, confession/open-suggestion home-arrival intents, and immersive tavern/letter encounters (`encounters/*.json`).
3. **Ambient Couple Banter (`BanterPlanner` + `BanterSceneStarter`)**: Location-filtered, cooldown-gated ambient dialogue scenes triggered between nearby romantic couples.
4. **Resilient Batch Execution (`AsyncLlmBatchRunner`)**: Shared epoch-safe LLM batch orchestration with standalone retry modals on request/JSON failures.

---

## 2. Subsystems & Decision Flows

### 2.1 Autonomous & UI Matchmaking
- **Eligibility & Orientation (`MatchmakingRules`)**: Filters out dead, synthetic child, Player-committed (`IsMainTierStatus`), and per-NPC cooldown actors (`matchmakingNpcCooldownDays`), enforcing all seven configured `MatchmakingOrientation` pairings (`Any`, `Heterosexual`, `Homosexual`, `Gay`, `Lesbian`, `HeteroAndGay`, `HeteroAndLesbian`).
- **Dual Mode Execution (`NpcMatchmaker` & SkyrimNet)**:
  - When `IsSkyrimNetAvailable()` is true (caller-level gate), runs SkyrimNet LLM matchmaking or SkyrimNet Decisions API verification (`EvaluateMatchmakingVerify`) and optional memory previewing via [`SkyrimNetMemoryService`](SkyrimNetMemoryService.md).
  - When SkyrimNet is unavailable (or `simulateNoSkyrimNet`), executes deterministic weighted slot rolls (`RollActionSlots` bounded by configurable `diceRollChance*` weights, capped at 3 slots per action kind per cycle) against eligible pairs (`FindEligiblePairForSlot`).

### 2.2 Player Partner Agency & Letter Encounters
- **Immunity Gates (`IsPartnerAgencyCheatingAllowedByImmunities`)**: Evaluates deceased status, minimum relationship age, open/polygamy guards, shared-children stability, affection thresholds, and active follower protection before rolling partner agency.
- **Pending Partner Intents & Home Arrival**: Scheduled checks can assign `PartnerIntentType` (`ConfessTalking`, `ConfessPhysical`, `SuggestOpen`) or send the partner to a nearby inn/tavern for a letter scenario. On arrival in a Player home cell (`ProcessPartnerHomeArrival`), `AgencyWorkflowCoordinator` reads an immutable catalog snapshot (`std::shared_ptr<const EncounterTemplateCatalog>`) from `EncounterConfigLoader`:
  - `EncounterConfigLoader` parses `Data/SKSE/Plugins/MARAS/encounters/*.json` (`cheating_physical_letter.json`, `cheating_talking_letter.json`, `open_date_away.json`) along with sorted drop-in patches in `Data/SKSE/Plugins/MARAS/encounters/overrides/*.json`, and atomically publishes a `shared_ptr<const EncounterTemplateCatalog>`.
  - `RenderEncounterTemplate` selects temperament-specific copy (`salutation`, `premise`, `clue`, `signOff`) and substitutes `{partner}`, `{player}`, `{location}`, and `{lover}` (hiding `{lover}` behind `"someone"` unless `allowLoverNameToken` is set), rendering a standalone modal via [`ModalManager`](ModalManager.md) and emitting `maras_partner_home_arrival_event`.

### 2.3 Ambient Couple Banter (`BanterPlanner`, `BanterSceneStarter` & `BanterSceneCoordinator`)
- Scans loaded actors around the Player using `SpatialActorSnapshot::ForEachNearbyCandidate` (excluding children, teammates, ghosts, dead/deleted/disabled actors, actors in combat or active scenes, and deceased records).
- Validates qualifying pair statuses (`Dating`, `Lover`, `Engaged`, `Married`), `SettingsData::enableScenes_LocType*` cell location keyword toggles, and couple cooldowns (`(scenesCooldownHours * 2.0) / 24.0` days in `CooldownTable::banterCoupleCooldowns`).
- `BanterSceneStarter` uses a thread-local PRNG (`ThreadLocalEngineRng`) and starts the engine `TESScene` cleanly on eligible nearby pairs.
- When `TTM_NpcsScenes` runs a banter or forced cheating/open-date scenario, `BanterSceneCoordinator` plans the scene/variant, relocates off-cell actors via XMarkers tracked in `'BNT1'` (`movedSceneActors`) and returns them on location change, exposes discovered affairs, computes intimate fallback and caught-cheating scene indices, and manages `SkyrimNet_Filter` (`0x12DB`) mutes + stale-mute recovery on save load (`skyrimNetMutedActors` in `'BNT1'`).

---

## 3. `AsyncLlmBatchRunner` — Cycle-Free Async Orchestration & Retries

Because SkyrimNet LLM calls execute asynchronously across multiple NPC pairs, `AsyncLlmBatchRunner` prevents `std::shared_ptr` reference cycles and session-boundary races:

- **Acyclic Ownership**: Mutable per-item completion status lives inside `LlmBatchState<PayloadT>` (`std::mutex`, `std::atomic<size_t> remaining`, zero `std::function` members), while `submitTask`, `formatFailureLine`, and `onAllResolved` live separately in stateless `LlmBatchCallbacks<PayloadT>`.
- **Completion & Epoch Fence (`DispatchLlmBatchCompletion`)**: When `remaining` hits zero, `AddCurrentEpochTask` marshals completion back to the game thread, validates `IsSessionEpochCurrent(expectedEpoch)` before locking `GetPapyrusMutex()`, and either commits results (`onProceed`) or pops a standalone `ShowInGameFailedLLMModal` retry/ignore prompt via [`ModalManager`](ModalManager.md) (carrying `llmVariant` guidance from [`SkyrimNetIntegration`](SkyrimNetIntegration.md)).
