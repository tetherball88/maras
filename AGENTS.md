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
- Relationship statuses, affection scores, hierarchy ranks, housing assignments, and child records are owned by the C++ services (`NPCRelationshipManager`, `AffectionService`, `SpouseHierarchyManager`, `PlayerHouseService`, `PlayerChildrenService`) and persisted in the `'MARS'` v4 SKSE co-save.
- `TT_MARAS.esp` factions (`0x7`, `0x111`, `0x119`, `0x6C`, `0xC23`, etc.) exist as **engine mirrors** so Creation Kit dialogue conditions, AI packages, and Story Manager nodes can evaluate state natively. Never treat faction rank alone as authoritative state when C++ APIs are available.

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

### Perspective-Bound Secret Masking (Infidelity & Paternity Secrets)
- When building prompt decorators (`ttm_npc_relationship_profile`), dialogue context, or any perspective-bound payload for a specific actor (including the Player when the Player is the deceived partner or unaware bio-father), **never leak undiscovered secrets** (`isSecretForPartner`, `isSecretForBioFather`, or undiscovered `CheatingRecord`s) when that actor is not privy to the secret (`!canActorKnow`).
- All derived lineage and role fields (such as `bio_father`, `biological_parents`, `step_parents`, `is_actor_bio_parent`, `is_actor_step_parent`) **must** reflect what the perspective actor *believes* to be true (i.e. presenting the deceived partner as the biological parent) until the secret is discovered.

### Cross-Layer Settings Synchronization
- Whenever adding, removing, or renaming a configuration setting in `Settings.h`, you **MUST** update all persistence and bridge endpoints in the same change:
  1. `Settings.cpp` (`Save`, `Load`, `Revert`, `DumpJson`, and `SaveToSkyrimNetConfig` if SkyrimNet-relevant).
  2. `PrismaUIDataBridge.cpp` (`SendOverviewData` / `SendMatchmakingSettings` **and** `ProcessUpdateSetting` / `ProcessUpdateMatchmakingSettings`).
  3. `PapyrusInterface.cpp` (`GetSetting*` and `SetSetting*`, if exposed to Papyrus).

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

