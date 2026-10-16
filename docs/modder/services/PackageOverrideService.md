# PackageOverrideService — Feature & Mechanics Guide

**Implementation**: [PackageOverrideService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp)

`PackageOverrideService` intercepts the Creation Engine's AI package evaluation loop at the machine-code level (`RELOCATION_ID(36404, 37398) + 0x47`) to keep housed spouses, children, pets/critters, and dynamically spawned NPC children (`0xFF` Stage 2) anchored to their assigned M.A.R.A.S sandbox packages without requiring fixed quest aliases. At the same time, it dynamically steps aside whenever an actor is recruited as a follower or needs to run an important quest, scene, or whitelisted mod package.

---

## 1. Three Categories of Managed Sandbox Packages

When `PackageOverrideService` loads its configuration, it resolves three core AI packages from `TT_MARAS.esp` and assigns them to registered actors based on their household role:

1. **Housed Spouses & Player Children (`0x6A` — Base Home Sandbox Package)**:
   - Assigned to all player-house tenants whose `stayAtHome` toggle is enabled in [PlayerHouseService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp).
   - Directs the actor to sandbox around their personal home `XMarker` (`MARAS_HomeSandboxKeyword`).
2. **Household Pets & Woodland Critters (`0xC0` — Pet/Critter Home Sandbox Package)**:
   - Assigned to pets and critters owned by player children in [PlayerChildrenService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp).
   - Directs the animal to sandbox around its owner child inside the home.
3. **Non-Player Dynamic `0xFF` Children (`0x376` — Non-Player Child Follow Parent Package)**:
   - Assigned to dynamically spawned (`0xFF`) Stage 2 children raised by NPC-to-NPC families (or Honorhall Orphanage).
   - Directs the child to follow/sandbox around their NPC parent or orphanage caretaker (`0x377` linked reference).

---

## 2. Engine Hook Decision Pipeline (`PackageStartHook`)

Every time the Creation Engine resolves which AI package an actor should run (`TESPackage*(ExtraDataList*, Actor*)`), `PackageStartHook` intercepts the winning `candidate` package. If the actor is registered in `PackageOverrideService`, it evaluates the candidate through a strict priority cascade:

```mermaid
flowchart TD
    Start["Engine Evaluates Winning Candidate Package"] --> Reg{"Actor in\nOverride Registry?"}
    Reg -- "No" --> Pass["Run Engine Candidate"]
    Reg -- "Yes" --> Follower{"Is Active Player\nFollower?"}
    Follower -- "Yes" --> Pass
    Follower -- "No" --> NullOrSame{"Candidate is Null\nor Already Sandbox?"}
    NullOrSame -- "Yes" --> Sandbox["Enforce MARAS Sandbox Package"]
    NullOrSame -- "No" --> PkgWL{"Matches Package\nEditorID Whitelist?"}
    PkgWL -- "Yes" --> Pass
    PkgWL -- "No" --> PlugWL{"Matches Allowed\nPlugins Whitelist?"}
    PlugWL -- "Yes" --> Pass
    PlugWL -- "No" --> QuestWL{"Owning Quest Matches\nQuest Whitelist?"}
    QuestWL -- "Yes" --> Pass
    QuestWL -- "No" --> PriCheck{"Owning Quest Priority\n>= QuestPriority Threshold?"}
    PriCheck -- "Yes" --> Pass
    PriCheck -- "No / Vanilla NPC Package" --> Sandbox
```

### Step-by-Step Evaluation Rules
0. **Intimate-Scene Flee Override (children, runs before the registry check)**:
   - If `ChildrenFleeService` currently flags the actor as a child fleeing a threat (an active OStim/SexLab scene or a manual flee request), the hook returns `TTM_ChildrenFleeIntimateScene` (`0x5DE`) immediately. This applies to **any** loaded child (registered or not) and beats every rule below, including whitelists and the follower exemption. See [ChildrenFleeService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/ChildrenFleeService.md).
1. **Active Follower Bypass**:
   - If the actor is currently following the player (`IsPlayerFollower`), the hook immediately yields and lets the follower package run unimpeded. When the player dismisses the follower later, the sandbox override automatically resumes.
2. **Null Candidate or Already Running Sandbox**:
   - If the engine returns `nullptr` (falling back to the NPC's vanilla town package stack) or is already running the M.A.R.A.S sandbox package, the hook returns the M.A.R.A.S sandbox package.
3. **Package EditorID Wildcard Whitelist (`Whitelist`)**:
   - Checks if the candidate package's EditorID matches any wildcard pattern (`*`, `?`) in the `Whitelist` INI setting. If matched, the candidate package is allowed to run.
4. **Originating Plugin Whitelist (`AllowedPlugins`)**:
   - Checks the source plugin file (`GetFile(0)->fileName`) of the candidate package against wildcard patterns in `AllowedPlugins`. Useful for allowing animation frameworks or companion mods to run their packages freely.
5. **Owning Quest EditorID Whitelist (`QuestWhitelist`)**:
   - Inspects the actor's `ExtraAliasInstanceArray` to find which `TESQuest` injected the candidate alias package. If the owning quest's EditorID matches `QuestWhitelist`, the package is allowed.
6. **Quest Priority Threshold (`QuestPriority`)**:
   - If `QuestPriority` is configured (`>= 0`) and the candidate package comes from a quest alias, compares `quest->data.priority` against the threshold:
     - **Priority `>=` Threshold**: Allowed (story/radiant quests with high priority can temporarily pull a spouse or child into a quest behavior).
     - **Priority `<` Threshold**: Suppressed and redirected to the M.A.R.A.S sandbox package.
7. **Default Suppression (Vanilla Routine Override)**:
   - Any non-alias vanilla NPC package (priority `-1`) or low-priority alias package that did not match any whitelist is suppressed and replaced with the actor's M.A.R.A.S sandbox package, keeping the spouse/child/pet at their assigned home.

### Log Deduplication Cache
- Because the engine evaluates AI packages frequently, `PackageStartHook` caches the last `(candidateID, resultID, PackageDecision)` tuple per actor (`s_lastEvals`) and only writes debug log lines when an actor's evaluation state actually changes.

---

## 3. Immediate Injection & Save-Load Registry Rebuild

- **Immediate Run-Once Injection**:
  - Whenever an actor is newly registered (and is not currently a player follower), `ReassertBasePackage` immediately calls `currentProcess->SetRunOncePackage(basePkg, actor)` followed by `EvaluatePackage` so the actor transitions to their home sandbox immediately without waiting for their current vanilla package timer to expire.
- **Stateless Save-Load Rebuild (`RebuildFromTenants`)**:
  - `PackageOverrideService` does not need its own co-save record. Instead, on every save load, it reconstructs its entire override registry deterministically from three authoritative sources:
    1. All player-house tenants with `stayAtHome == true` in `PlayerHouseService` (`0x6A`).
    2. All active pets and critters in `PlayerChildrenService` (`0xC0`).
    3. All non-player `0xFF` Stage 2 children in `NPCRelationshipManager` (`0x376`), while simultaneously re-asserting their `0x377` parent/orphanage linked reference so they immediately follow their NPC parent or caretaker after loading a save.

