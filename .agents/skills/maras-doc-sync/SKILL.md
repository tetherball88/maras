---
name: maras-doc-sync
description: >-
  Documentation synchronization map and authoring rules for docs/modder/ and docs/player/. Activate whenever completing a feature, altering gameplay mechanics/formulas, changing C++ services, updating MARAS.psc / MARAS_PublicAPI.h / UI bridge payloads / SkyrimNet prompts, or changing JSON/INI schemas.
---

# Documentation Synchronization Map & Style Guide

Whenever an implementation alters behavior, formulas, APIs, bridge payloads, SkyrimNet prompts/actions/triggers, or external JSON/INI configs, inspect and update the matching docs in the same task:

## 1. Modder & Architecture Docs (`docs/modder/`)

- **System & Layer Architecture:**
  - `docs/modder/ARCHITECTURE.md` — Layered C++/Papyrus/UI architecture, `'MARS'` v7 co-save overview, event flow.
  - `docs/modder/SKYRIMNET_AND_AI_INTEGRATION.md` — Decorators (`ttm_*`), YAML actions/triggers, Jev decisions, chat evaluations, memory service, secret isolation.
  - `docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md` — Houses, tenancy, shared beds, child lifecycle (`Unborn` $\to$ `Adult`), guardian slots, paternity secrets, training & travel.
  - `docs/modder/API_AND_UI_BRIDGE_REFERENCE.md` — `MARAS.psc` natives, `MARAS_PublicAPI.h` C exports, UI JS $\leftrightarrow$ C++ actions/events.
  - `docs/modder/RELATIONSHIP_DYNAMICS_GUIDE.md` — Pair statuses, hierarchy, affection math, loneliness, infidelity, open relationships, matchmaking & partner agency.
  - `docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md` — FMR & Beeing Female NG hooks, conception, birth batching.
  - [INGAME_SANITY_TEST_SUITE.md](../../../docs/modder/INGAME_SANITY_TEST_SUITE.md) — `TTM_SanitySuite.psc`, console `.txt` batch scripts, `MARAS.DumpStoredData(tag)`, and `verify-maras-sanity.ps1`.
- **C++ Service Guides (Canonical 28-File `docs/modder/services/` Roster):**
  - Update or add `<ServiceName>.md` whenever a service's mechanics, formulas, edge cases, or workflows change. Use **only** the canonical 28-file roster below (retiring pre-modular service filenames):
    - **Kept (9):** `ChildrenFleeService.md`, `IntimateThreadService.md`, `MarriageDifficulty.md`, `ModalManager.md`, `PollingService.md`, `SkyrimNetIntegration.md`, `SkyrimNetMemoryService.md`, `SkyrimNetProfileService.md`, `UIDataBridge.md`.
    - **Canonical New / Renamed (19):** `WorldStoreAndCoSave.md`, `PerspectiveView.md`, `RelationshipUseCases.md`, `AffectionRules.md`, `TraitClassifier.md`, `BuffReconciler.md`, `AgencyWorkflowCoordinator.md`, `LineageUseCases.md`, `PaternityWorkflowCoordinator.md`, `ChildGrowthAndVisuals.md`, `NamePoolLoader.md`, `HouseholdUseCases.md`, `TravelRules.md`, `DeceasedQueries.md`, `SpatialWorldIndex.md`, `FactionReconciler.md`, `PackageReconciler.md`, `HomeCellDetector.md`, `QuestEventHandler.md`.
- **External Modder Config Guides & Data Schemas:**
  - Guides in `docs/modder/`: [ADVANCED_CONFIGURATION_AND_MODDING.md](../../../docs/modder/ADVANCED_CONFIGURATION_AND_MODDING.md), [LOCALIZATION_GUIDE.md](../../../docs/modder/LOCALIZATION_GUIDE.md), [MARRIAGE_DIFFICULTY_CONFIG.md](../../../docs/modder/MARRIAGE_DIFFICULTY_CONFIG.md), [PACKAGE_OVERRIDE.md](../../../docs/modder/PACKAGE_OVERRIDE.md), [QUEST_TRACKER.md](../../../docs/modder/QUEST_TRACKER.md), [LOCATION_IMAGES_PROMPTS.md](../../../docs/modder/LOCATION_IMAGES_PROMPTS.md).
  - External config paths loaded under `(Data/)SKSE/Plugins/`:
    - `MARAS/localization/<lang>/*.json` — Multi-language HUD, modal, catalog, Dashboard UI, & optional name overlays (`LocalizationService`, `LocaleJsonBuilder`, `NamePoolLoader`).
    - `MARAS/encounters/*.json` (+ optional `MARAS/encounters/overrides/*.json`) — Encounter templates (`EncounterConfigLoader`).
    - `MARAS/spousesTypes/*.json` — NPC trait overrides (`JsonOverrideLoader`).
    - `MARAS/questEvents/*.json` — Quest trigger event configs (`QuestEventConfigLoader` via `plugin.cpp`).
    - `MARAS/childTraining.yaml` + `MARAS/childTraining/*.yaml` — Child training catalog & Daedric blessings (`TrainingCatalogLoader`).
    - `MARAS/childrenNames.json` & `MARAS/critterNames.json` — Child & critter name pools (`NamePoolLoader`).
    - `MARAS/marriageDifficultyConfig.json` (fallback `MARAS/marriage_difficulty.json`) — Marriage difficulty params (`MarriageDifficultyLoader`).
    - `MARAS/bonuses.json` — Spouse archetype bonuses (`BonusesConfigLoader`).
    - `MARAS/npc_index_overrides.ini` — NPC spatial index include/exclude overrides (`SpatialWorldIndex`).
    - `MARAS/promptStrings.json` — Canonical English dialogue/LLM prompt strings (`PromptStringsLoader`).
    - `PackageOverrides.ini` & `MARAS.ini` — AI package whitelist (`PackageReconciler`) and hotkey/log-level/language baseline (`SettingsCoordinator`).

## 2. Player-Facing Guides (`docs/player/`)

Update when gameplay mechanics, thresholds, or UI Dashboard workflows change:
- [PLAYER_GUIDE.md](../../../docs/player/PLAYER_GUIDE.md)
- [RELATIONSHIP_OVERVIEW.md](../../../docs/player/RELATIONSHIP_OVERVIEW.md)
- [AFFECTION_SYSTEM.md](../../../docs/player/AFFECTION_SYSTEM.md)
- [ARCHETYPES_AND_BUFFS.md](../../../docs/player/ARCHETYPES_AND_BUFFS.md)
- [FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md](../../../docs/player/FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md)
- [SKYRIMNET_INTEGRATION_GUIDE.md](../../../docs/player/SKYRIMNET_INTEGRATION_GUIDE.md)
- [UI_DASHBOARD_GUIDE.md](../../../docs/player/UI_DASHBOARD_GUIDE.md)

## 3. Authoring Rule (No Header Copy-Pasting)

In `docs/modder/services/*.md`, document **what the service does, how its mechanics and formulas work, edge cases, and cross-service interactions**. **Never** copy-paste raw C++ method declarations, parameter lists, struct definitions, or enum tables from `.h` files.
