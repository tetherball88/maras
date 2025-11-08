# Changelog

## 0.0.3.dev - 2025-10-12
- Rebuilt proposal acceptance logic with the new `marriageComplexityVariables.json` tuning file, expanded `TTM_ServiceMarriageDifficulty` scoring, and extra MCM switches so candidates must finish social/skill tagging before engagements proceed.
- Broadened SkyrimNet integration by renaming proposal decorators, tightening action eligibility checks, surfacing co-spouse/ex-partner context, and emitting spouse promotion/demotion events for AI awareness.
- Introduced spouse hierarchy tracking that syncs faction ranks, exposes rank menus in the MCM, and applies demotion cooldown spells plus SkyrimNet alerts when positions shift.
- Added a documentation pass across README and new guides (`docs/JSON-CONFIG.md`, `docs/MCM.md`, `docs/SKYRIMNET.md`, `docs/USER-GUIDE.md`) to explain configuration knobs and AI workflows.
- Consolidated cooldown handling into `TTM_CooldownSpell` and refreshed data (initialData.json, prompts, quests) to support the new hierarchy and marriage flow hooks.

## 0.0.2.dev - 2025-10-09
- Added JSON-driven spouse type overrides and maintenance routines so social class/skill assignments can be patched (includes Volkihar family defaults and clothing-based wealth detection).
- Expanded the spouse MCM page with status-aware displays, configurable social/skill classes, home sharing toggles, and marriage chance readouts for candidates.
- Hooked additional mod events for SkyrimNet and OStim to keep sandbox packages in sync after AI-driven scenes and ensure housing packages are restored.
- Authored a comprehensive README covering requirements, features, and optional integrations.
- Recompiled Papyrus artifacts to align with the updated sources and new quest script rename.

## 0.0.1.dev - 2025-10-07
- Initial development release providing the core marry-anyone quest flow, JSON-backed data services, MCM configuration menus, and baseline SkyrimNet integration scaffolding.
