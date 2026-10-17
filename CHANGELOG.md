# Changelog

## 2.0.1 - 2026-10-17
- Fixed new game initialization importing plugin-baked Player-NPC `BGSRelationship` records (e.g., custom followers with pre-set Lover rank 4) — on a fresh new game, the player now starts with a clean relationship slate while NPC-NPC relationships are still scanned normally.
- Fixed floating-point numbers displaying excessive decimal places (e.g., `1.7000000476837158x` for Affection Decay Multiplier) across Prisma UI sliders, number inputs, and settings descriptions.

## 2.0 - 2026-XX-XX
Major release. See [CHANGELOG_v2.0.md](CHANGELOG_v2.0.md) for the full detailed changelog.
- Added Prisma UI dashboard — in-game web UI replacing MCM as the primary interface for relationship management, matchmaking, adoption, NPC detail views, and all mod settings. MCM is deprecated.
- Added Dating and Lover relationship statuses between Candidate and Engaged, with corresponding Ex-Dating and Ex-Lover statuses for ended relationships.
- Added full NPC-NPC relationship support across all statuses. Polygamy remains player-exclusive; NPC-NPC pairs are tracked on paper and integrated into SkyrimNet biography context.
- Added cheating and affairs system with affair types (Casual, Dating, Lover), affair statuses (Ongoing, Ended breakup/confronted/victim split, Allowed), secrecy tracking, and open relationship flags with auto-propagation.
- Added unlimited adoption system mirroring vanilla Hearthfire: polygamy-aware child-to-spouse assignment, vanilla NPC children adoption on marriage, force greeting, home sandbox, pets and critters, child orders, gift system, and configurable child custody policy on divorce.
- Added relationship status history tracking per pair (initiator, previous status, timestamp), viewable in Prisma UI.
- Added confirmation modals for relationship actions with configurable cooldowns; SkyrimNet adds a "Decline (Explain)" AI narration option.
- Added vanilla relationship scanning on first load — existing marriages and parent-child bonds are automatically imported into MARAS tracking.
- Added matchmaking tools in Prisma UI with LLM-assisted evaluation via SkyrimNet.
- Reworked SkyrimNet integration: migrated from Papyrus to native C++ decorators and event registration. Added relationship context decorators (household, co-spouses, infidelity, ex-partners, polygamy), structured event dispatching, and LLM-based relationship status evaluation with retry/ignore modals.
- Improved home sandbox: reworked package override logic to avoid follower framework conflicts, added quest whitelist, improved merchant spouse schedule handling.
- Fixed PlayerMarriedFaction being re-added by third-party mods when polygamy is enabled; added periodic faction sanitizer.
- Migrated major subsystems (relationship tracking, affection, children, package overrides, SkyrimNet dispatching) from Papyrus to native SKSE C++ for improved performance.
- Migrated SkyrimNet assets to Beta 25+ external plugin bundle format (`tetherball88.maras`) with `manifest.json`, standardized action filenames matching in-file names, and modular prompt directory structure.

## 0.0.4.dev - 2025-12-22
- Added MCM setting to adjust daily affection decay multiplier (0.0 = disabled, 1.0 = default, 2.0 = double decay). Set to 0 to disable decay entirely, or increase to make relationship maintenance more challenging.
- Added follower-aware home sandboxing: spouses assigned to player houses who become followers will temporarily drop their house sandboxing package and join you. When released from following, they return to their assigned house (15-second polling cycle for performance).
- Added merchant services for housed spouses: spouses assigned to player houses now become merchants like in vanilla, with support for specialized merchant types (Apothecary, Blacksmith, Fletcher, Jeweler, Hunter, Innkeeper, Misc, Spells, Tailor). Each merchant type shares the same merchant chest with other spouses of that type.
- Fixed PO3 Papyrus Extender linked reference persistence bug: replaced PO3's SetLinkedRef with native SKSE ExtraLinkedRef management to ensure home sandbox markers persist correctly across game loads.

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
