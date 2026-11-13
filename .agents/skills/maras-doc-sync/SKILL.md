---
name: maras-doc-sync
description: >-
  Documentation synchronization map and authoring rules for docs/modder/ and docs/player/. Activate whenever completing a feature, altering gameplay mechanics/formulas, changing C++ services, updating MARAS.psc / MARAS_PublicAPI.h / Prisma bridge payloads / SkyrimNet prompts, or changing JSON/INI schemas.
---

# Documentation Synchronization Map & Style Guide

Whenever an implementation alters behavior, formulas, APIs, bridge payloads, SkyrimNet prompts/actions/triggers, or external JSON/INI configs, inspect and update the matching docs in the same task:

## 1. Modder & Architecture Docs (`docs/modder/`)

- **System & Layer Architecture:**
  - `docs/modder/ARCHITECTURE.md` — Layered C++/Papyrus/UI architecture, `'MARS'` v7 co-save overview, event flow.
  - `docs/modder/SKYRIMNET_AND_AI_INTEGRATION.md` — Decorators (`ttm_*`), YAML actions/triggers, Jev decisions, chat evaluations, memory service, secret isolation.
  - `docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md` — Houses, tenancy, shared beds, child lifecycle (`Unborn` $\to$ `Adult`), guardian slots, paternity secrets, training & travel.
  - `docs/modder/API_AND_PRISMA_BRIDGE_REFERENCE.md` — `MARAS.psc` natives, `MARAS_PublicAPI.h` C exports, Prisma UI JS $\leftrightarrow$ C++ actions/events.
  - `docs/modder/RELATIONSHIP_DYNAMICS_GUIDE.md` — Pair statuses, hierarchy, affection math, loneliness, infidelity, open relationships, matchmaking & partner agency.
  - `docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md` — FMR & Beeing Female NG hooks, conception, birth batching.
- **C++ Service Guides (`docs/modder/services/<ServiceName>.md`):**
  - Update or add `<ServiceName>.md` whenever a service's mechanics, formulas, edge cases, or workflows change (e.g., `PrismaModalManager.md`, `MatchmakingService.md`, `AdultChildTravelService.md`).
- **External Modder Config Guides:**
  - `docs/modder/ADVANCED_CONFIGURATION_AND_MODDING.md`, `MarriageDifficultyConfig.md`, `PACKAGE_OVERRIDE.md`, `QUEST_TRACKER.md`, `ARCHETYPES_AND_BUFFS.md`.

## 2. Player-Facing Guides (`docs/player/`)

Update when gameplay mechanics, thresholds, or Prisma UI workflows change:
- `docs/player/PLAYER_GUIDE.md`
- `docs/player/RELATIONSHIP_OVERVIEW.md`
- `docs/player/AFFECTION_SYSTEM.md`
- `docs/player/ARCHETYPES_AND_BUFFS.md`
- `docs/player/FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md`
- `docs/player/SKYRIMNET_INTEGRATION_USER_GUIDE.md` / `SKYRIMNET_INTEGRATION_GUIDE.md`
- `docs/player/UI_DASHBOARD_GUIDE.md`

## 3. Authoring Rule (No Header Copy-Pasting)

In `docs/modder/services/*.md`, document **what the service does, how its mechanics and formulas work, edge cases, and cross-service interactions**. **Never** copy-paste raw C++ method declarations, parameter lists, struct definitions, or enum tables from `.h` files.
