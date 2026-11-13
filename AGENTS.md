# M.A.R.A.S — Cross-Layer Architecture & Project Rules

This root `AGENTS.md` contains **only project-wide, cross-cutting rules** that apply across all layers of the **M.A.R.A.S** mod. Environment-specific coding rules are cascaded into each layer's own directory:

1. **SKSE C++ Plugin**: [SKSE_Source/AGENTS.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/AGENTS.md)
2. **Prisma UI Dashboard**: [prisma-src/AGENTS.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/AGENTS.md)
3. **Papyrus Scripts**: [Scripts/Source/AGENTS.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/AGENTS.md)
4. **SkyrimNet External Bundle**: [SKSE/Plugins/SkyrimNet/external/tetherball88.maras/AGENTS.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/SkyrimNet/external/tetherball88.maras/AGENTS.md)

For detailed system architecture, consult:
- [docs/modder/ARCHITECTURE.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/ARCHITECTURE.md)
- [docs/modder/SKYRIMNET_AND_AI_INTEGRATION.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/SKYRIMNET_AND_AI_INTEGRATION.md)
- [docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md)
- [docs/modder/API_AND_PRISMA_BRIDGE_REFERENCE.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/API_AND_PRISMA_BRIDGE_REFERENCE.md)
- [docs/modder/RELATIONSHIP_DYNAMICS_GUIDE.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/RELATIONSHIP_DYNAMICS_GUIDE.md)
- [docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md)

---

## ⚠️ STRICT BUILD & TOOLING PROHIBITION

- **NEVER SEARCH FOR CMAKE OR TRY TO BUILD C++**: You are **NEVER** supposed to search for `cmake`, look for `cmake.exe`, run CMake commands, invoke MSBuild, or attempt to compile the C++ plugin (`MARAS.dll`). C++ compilation is handled strictly and exclusively by the user.

---

## 1. Core Cross-Layer Domain Rules

### Single Source of Truth (C++ Owns State, Factions Mirror State)
- Relationship statuses, affection scores, hierarchy ranks, housing assignments, and child records are owned by the C++ services (`NPCRelationshipManager`, `AffectionService`, `SpouseHierarchyManager`, `PlayerHouseService`, `PlayerChildrenService`) and persisted in the `'MARS'` v7 SKSE co-save.
- `TT_MARAS.esp` factions (`0x7`, `0x111`, `0x119`, `0x6C`, `0xC23`, etc.) exist as **engine mirrors** so Creation Kit dialogue conditions, AI packages, and Story Manager nodes can evaluate state natively. Never treat faction rank alone as authoritative state when C++ APIs are available.
- **Strict Command-Query Separation (CQS)**: Query snapshots, UI bridge payloads (`SendOverviewData`, `SendNPCList`, `SendNPCDetail`), JSON/message builders (`Build*Json`, `DumpJson`), and SkyrimNet prompt decorators (`ttm_*`) **must** be strictly read-only—never mutate domain state, custody, actor registrations, or record memories as a side effect of reading or formatting data.

### Pair-Keyed Relationship Symmetry vs. Asymmetry
- Romantic statuses (`Dating`, `Lover`, `Engaged`, `Married`, `ExDating`, `ExLover`, `Jilted`, `Divorced`) are stored on a canonical `PairKey(actorA, actorB)` where `min(A, B)` is first.
- However, **cheating/infidelity flags, open-relationship consent, and paternity secrets are directional/role-specific** (e.g., partner A can be cheating on partner B with paramour C while B is faithful; paternity secrets track `trueFatherID` vs. `deceivedPartnerID`). Never assume infidelity or open-relationship flags are symmetric across a pair.

### Guardian, Parental & Adoption Semantics
- **Strict Slot Neutrality**: Variables, properties, struct members, and parameters named `guardianA/B`, `guardianAID/BID`, `parentA/B`, `parentAID/BID` are strictly neutral slots.
  - Never infer gender or maternal/paternal roles from slot position (A vs. B are completely interchangeable).
  - Inspect explicit fields (`bioMotherID`, `bioFatherID`) or query actor sex directly.
- **Guardians (`guardianA/B`) vs. Bio Parents (`bioMother/bioFather`)**:
  - **Guardians**: Official custodial caretakers (can be biological parents, adoptive parents, single parent, or none). Both can be passed simultaneously.
  - **Bio Parents**: Genetic lineage only. Never conflate with guardians.
- **Adoption (`isAdopted`)**: Represents an explicit adoption process into a household. Never infer `isAdopted` from presence, absence, or count of guardians.

### Secret Masking Semantics (Perspective-Bound AI vs. World-Wide Intrigue Mode)
- **SkyrimNet Prompts & AI Memories (Perspective-Bound + `dialogue_target` Public Isolation)**:
  - **Self-Perspective (`render_mode != "dialogue_target"`)**: When building `ttm_npc_relationship_profile`, action candidate lists, or episode memory contexts (`SkyrimNetMemoryService`) for a specific actor (including the Player when deceived or unaware), **never leak undiscovered secrets** (`isSecretForPartner`, `isPregnancySecretForPartner`, `isSecretForBioFather`, secret werewolf `hasBeastblood`, or undiscovered `CheatingRecord`s) when `!canActorKnow`. All derived lineage/role fields (`bio_father`, `biological_parents`, `step_parents`, `siblings`, `is_actor_bio_parent`, `is_actor_step_parent`) must reflect what that actor *believes* to be true.
  - **Target Bio (`render_mode == "dialogue_target"`)**: Represents **public knowledge** about the target actor when rendered into *another* speaker's prompt. Never expose active undiscovered affairs (`is_secret_affair` in current or past relationships), secret paternity (`has_undiscovered_paternity_secret`, `is_paternity_secret`, `is_other_parent_secret`), secret `siblings` (use `siblings_public`), or `has_beastblood` in `dialogue_target` mode—even when the target actor personally knows the secret.
- **Prisma UI Dashboard & Diagnostic Dumps — Intrigue Mode (`!Settings::revealSecretsInUI`)**:
  - Intrigue Mode enforces **uniform world-wide secret masking** across the entire Dashboard and JSON data dumps: *no* undiscovered affair, secret paternity, secret werewolf inheritance (`hasBeastblood` without a visible werewolf parent), or unannounced NPC pregnancy is magically revealed in the UI—including secrets involving the Player (`0x14`)—until discovered in-game or `revealSecretsInUI` is toggled on. Do **not** add `0x14` Player exemptions to UI masking.

### Cross-Layer Settings Synchronization
- Whenever adding, removing, or renaming a configuration setting in `Settings.h`, you **MUST** update all persistence and bridge endpoints in the same change:
  1. `Settings.cpp` (`Save`, `Load`, `Revert`, `DumpJson`, and `SaveToSkyrimNetConfig` if SkyrimNet-relevant).
  2. `PrismaUIDataBridge.cpp` (`SendOverviewData` / `SendMatchmakingSettings` **and** `ProcessUpdateSetting` / `ProcessUpdateMatchmakingSettings`).
  3. `PapyrusInterface.cpp` (`GetSetting*` and `SetSetting*`, if exposed to Papyrus).

### Standalone Prisma UI Modals (`PrismaModalManager` & `PrismaModalHost`)
- All confirmation popups, retry/cancel prompts, and interactive modal dialogs **MUST** use the Prisma UI modal infrastructure (`PrismaModalManager` in C++ $\leftrightarrow$ `useModalStore` / `<PrismaModalHost />` in React) rather than native Skyrim `RE::MessageBoxMenu`.
- Modals mount independently of the main dashboard (`<DashboardLayout>`), pause the game on show, keep PrismaUI's `FocusMenu` enabled (`Focus(view, /*pauseGame=*/true, /*disableFocusMenu=*/false)`) so mouse cursor movement and keyboard navigation (`WASD`, Arrow keys, `Tab`, `Enter`/`Space`/`E`) work seamlessly, and cannot be dismissed except by selecting one of the modal's control buttons. See [docs/modder/services/PrismaModalManager.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/PrismaModalManager.md).

---

## 2. SkyrimNet Cross-Layer Gating Policy

### Mandatory Runtime Gating & Caller-Level Check
- Any feature, UI element, or background logic intended for SkyrimNet **MUST** be gated by runtime checks verifying that SkyrimNet is installed, loaded, and enabled.
- SkyrimNet-dependent functions must be gated **at the call site**, not only inside the callee. Preparatory work (dice rolls, data gathering, state mutations) **MUST** check availability **before** doing that work so no computation is wasted and no game state is left inconsistent when SkyrimNet is absent.

### Non-SkyrimNet Fallback Requirement
When implementing any new SkyrimNet-dependent feature, the agent **MUST** explicitly consider what happens when SkyrimNet is not installed. If the feature has no non-SkyrimNet fallback path, the agent **MUST ask the user** whether:
1. The feature should be silently skipped (no-op) when SkyrimNet is absent.
2. A simplified non-AI fallback should be implemented (e.g., dice-roll, default behavior, static outcome).
3. The feature should be entirely hidden/disabled in the UI when SkyrimNet is absent.

Do **NOT** silently assume "skip if no SkyrimNet" without confirming with the user.

### Exemptions: When Explicit Gating is NOT Required
1. **SkyrimNet Decorators**: C++ callbacks registered with `PublicRegisterDecorator` (called exclusively by SkyrimNet's internal prompt engine).
2. **Action Executor Callbacks**: Functions invoked *only* via SkyrimNet action executor YAML files (`external/tetherball88.maras/actions/*.yaml`).

---

## 3. Refactoring, Fixes & Code Clean-Up Rules

- **Clean Old Logic**: When working on refactoring, fixing, or replacing features or systems, actively clean out and remove old, superseded logic. Do **NOT** merely leave it around deprecated or dead, and do **NOT** attempt to awkwardly incorporate or graft outdated logic into the new approach.
- **Save Game Compatibility Required**: Always preserve **save-game (savegame / SKSE co-save) backward compatibility**. Existing player saves and serialized data must continue to load cleanly without crashes, data corruption, or lost state. Provide safe upgrade or fallback migration paths for serialized data structures when altering them.
- **No Code/API Backward Compatibility**: Code-level and API backward compatibility is **NOT** required. Do not keep obsolete functions, vestigial wrapper stubs, deprecated Papyrus hooks, or dead parameters around purely to preserve code backward compatibility. Make a clean cut and delete them.
- **No Unsolicited API Aliases or Synonyms**: **NEVER** create duplicate alias functions, alternative naming wrappers, or synonymous function names (e.g., adding both `IsRelationshipCheating` and `IsPairCheating`) unless the user explicitly and specifically asks for an alias. Choose the single most canonical, accurate function name and implement only that one.
- **Commit Fully to the New Approach**: Do not create compromised hybrid solutions trying to reconcile legacy patterns with new architecture; fully transition to the clean, updated implementation.

---

## 4. Documentation Synchronization & Maintenance Rules

- **Mandatory Documentation Check & Update on Implementation**: Whenever you implement a new feature, alter gameplay mechanics or formulas, change C++ service behavior, modify APIs (`MARAS.psc`, `MARAS_PublicAPI.h`, Prisma UI bridge actions/events, SkyrimNet decorators/YAML actions/triggers), or update external config formats (`JSON` / `INI`), you **MUST** check the relevant documentation and update it in the same task if needed:
  - **Architecture & Cross-Layer Guides**: `docs/modder/ARCHITECTURE.md`, `docs/modder/SKYRIMNET_AND_AI_INTEGRATION.md`, `docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md`, `docs/modder/API_AND_PRISMA_BRIDGE_REFERENCE.md`, `docs/modder/RELATIONSHIP_DYNAMICS_GUIDE.md`, `docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md`.
  - **C++ Service Guides**: `docs/modder/services/<ServiceName>.md` for any C++ service whose mechanics, rules, or workflows were added or modified.
  - **Modder Configuration Guides**: `docs/modder/ADVANCED_CONFIGURATION_AND_MODDING.md`, `docs/modder/MarriageDifficultyConfig.md`, `docs/modder/PACKAGE_OVERRIDE.md`, `docs/modder/QUEST_TRACKER.md`, `docs/modder/ARCHETYPES_AND_BUFFS.md`.
  - **Player Guides**: `docs/player/PLAYER_GUIDE.md`, `docs/player/RELATIONSHIP_OVERVIEW.md`, `docs/player/AFFECTION_SYSTEM.md`, `docs/player/ARCHETYPES_AND_BUFFS.md`, `docs/player/FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md`, `docs/player/SKYRIMNET_INTEGRATION_GUIDE.md`, `docs/player/UI_DASHBOARD_GUIDE.md` when player-facing mechanics or UI workflows change.
- **Feature & Behavior Focus (No Header Copy-Pasting)**: When updating or creating service documentation in `docs/modder/services/`, document **what the service does, how its features and mechanics work, formulas, edge cases, and interactions**. Do **NOT** copy-paste raw C++ function declarations, method signatures, argument lists, struct definitions, or enum tables from `.h` files.

---

## 5. Logging & Diagnostic Discipline (`MARAS.log`)

- **Log Levels & Signal-to-Noise Balance**: Always use the appropriate log level so `MARAS.log` has **just enough structured context to diagnose issues quickly without log spam**:
  - **`ERROR` (`MARAS_LOG_ERROR` / `TTM_Debug.err`)**: Broken invariants, missing critical config fields (reject the config entry rather than guessing by name or silently substituting fake defaults), failed form/file lookups that break a feature, serialization failures, or caught exceptions.
  - **`WARN` (`MARAS_LOG_WARN` / `TTM_Debug.warn`)**: Recoverable anomalies, unexpected null actors/forms, rejected invalid parameters, or skipped optional integrations.
  - **`INFO` (`MARAS_LOG_INFO` / `TTM_Debug.info`)**: High-level state transitions and one-shot lifecycle events only (plugin init, config/save load summary, relationship status change, child birth/training/graduation summary, house assignment). **Never** log per-frame, per-tick, or per-inventory-item loops at `INFO`.
  - **`DEBUG` (`MARAS_LOG_DEBUG` / `TTM_Debug.debug`)**: Decision inputs and resolved values needed to debug why a specific outcome happened (e.g., resolved `ActorBase` FormIDs, race/slot indices, dice rolls vs. thresholds, 3D load visual/equipment application, eligibility checks).
  - **`TRACE` (`MARAS_LOG_TRACE` / `TTM_Debug.trace`)**: Verbose inner-loop dumps (e.g., per-item inventory enumeration, polling tick details, raw payload dumps).
- **Include Concrete Identifiers**: Every `INFO`/`DEBUG`/`WARN`/`ERROR` log for an actor, child, or form **must** include concrete identifiers (hex FormID `{:08X}`, actor/child name, and relevant enum/index values) so issues can be pinpointed from a single log read without guessing.
- **Add Targeted Logs Instead of Guessing**: When investigating a bug where the root cause is not immediately proven by existing logs or code inspection, add targeted `DEBUG`/`INFO` logs at the exact boundary in question and ask the user to reproduce rather than guessing or spinning on unverified theories.


