# M.A.R.A.S — Package Override Configuration (`PackageOverrides.ini`)

This document explains how [PackageOverrideService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp) manages AI packages on M.A.R.A.S-registered household actors, pets/critters, and NPC-couple children, and how to configure `Data/SKSE/Plugins/PackageOverrides.ini` to coexist cleanly with follower frameworks, quest mods, and AI dialogue systems.

---

## Related Documentation

- [PackageOverrideService Mechanics](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/PackageOverrideService.md) — Architectural deep dive into the engine hook (`RELOCATION_ID(36404, 37398) + 0x47`), registry rebuilding, and evaluation pipeline.
- [PlayerHouseService Mechanics](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/PlayerHouseService.md) — How tenant housing and the per-NPC **Stay at Home** flag work.
- [PlayerChildrenService Mechanics](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/PlayerChildrenService.md) — How household children, pets, and critters are managed.
- [Advanced Configuration and Modding](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/ADVANCED_CONFIGURATION_AND_MODDING.md) — Overview of all external configuration files in M.A.R.A.S.

---

## 1. What the Package Override Service Does

M.A.R.A.S hooks the Skyrim engine's AI package selection call site (`PackageStartHook`) to maintain a persistent base sandbox/follow package for three categories of managed actors:

1. **Housed Tenants (`0x6A` — Home Sandbox Package)**:
   - Any spouse, partner, or child assigned as a tenant in a player home via `PlayerHouseService` whose **Stay at Home** setting is enabled (`stayAtHome = true`).
   - Makes the actor eat, sleep, sit, and sandbox naturally around the home's sandbox marker (`0x6B`).
   - **Toggling Per-NPC**: Players can toggle **Stay at Home** on or off at any time in the **Prisma UI Dashboard** (`NPC Detail` or `My Household` page) or by telling the NPC through dialogue that they are free to return to their normal routine. When `Stay at Home` is disabled, the actor is unregistered from the override and resumes their vanilla/quest AI packages.
2. **Household Pets & Critters (`0xC0` — Pet/Critter Home Sandbox Package)**:
   - Keeps adopted child pets and wild critters sandboxing inside the family's assigned player home rather than wandering back to their original wild spawn cells.
3. **Dynamic Non-Player Children (`0x376` — Follow/Sandbox Around Parent Package)**:
   - Dynamically spawned (`0xFF`) Stage 2 (`Child`) biological children belonging to NPC-to-NPC couples (where neither guardian is the player) are registered with package `0x376` so they follow and sandbox around their custodial parent or home marker (`0x377`).

### Automatic Follower Exemption
Whenever a managed actor is currently following the player (`Utils::IsPlayerFollower`), `PackageOverrideService` **skips the sandbox override completely**. Follower behavior is never blocked or interrupted.

---

## 2. INI File Location

```text
Data/SKSE/Plugins/PackageOverrides.ini
```

This file is loaded once at startup (`kDataLoaded`) by `PackageOverrideService::LoadConfig()`. All settings belong under the `[PackageOverrides]` section.

---

## 3. Configuration Options

### `Whitelist`
```ini
Whitelist = SkyrimNet*, WIGames*
```
A comma-separated list of **package EditorID wildcard patterns**. Any AI package whose EditorID matches one of these patterns is **always allowed** to run over the M.A.R.A.S base package.
- Use this for specific packages or families of packages that should never be suppressed (such as `SkyrimNet*` AI conversation packages or `WIGames*` world interaction games like Hide-and-Seek / Tag).

### `AllowedPlugins`
```ini
AllowedPlugins = nwsFollowerFramework.esp, MyFollowerMod.esp
```
A comma-separated list of **plugin filename wildcard patterns**. Any AI package whose **originating plugin** (`pkg->GetFile(0)` — the `.esm`/`.esp`/`.esl` that defined the package form) matches is **always allowed** to run.
- Use this to permit all AI packages from an entire follower framework or overhaul mod without listing individual package EditorIDs.
- **Important**: Never add `Skyrim.esm`, `HearthFires.esm`, or other base game plugins to `AllowedPlugins`, as that would allow vanilla home/work packages to override M.A.R.A.S's home sandbox.

### `QuestWhitelist`
```ini
QuestWhitelist = TTM_ChildrenSandbox, TTM_DialoguesChildrenScenes, TTM_NpcsScenes, DLC1Serana*
```
A comma-separated list of **quest EditorID wildcard patterns**. When a candidate package is driven by a quest alias (`ExtraAliasInstanceArray`), M.A.R.A.S checks the owning quest's EditorID against `QuestWhitelist`. If it matches, the package is **always allowed** to run.
- Unlike earlier versions, `QuestWhitelist` is evaluated **independently and before** `QuestPriority`—it works even when `QuestPriority` is disabled (`-1`).

### `QuestPriority`
```ini
QuestPriority = 61
```
An optional integer threshold (`-1` or omitted = disabled) for **quest-alias-driven packages** that did not match `QuestWhitelist`:

| Condition | Result |
| :--- | :--- |
| Owning quest priority $\ge$ `QuestPriority` | Package is **allowed** to run |
| Owning quest priority $<$ `QuestPriority` | Package is **suppressed** $\rightarrow$ redirected to M.A.R.A.S base package |
| Package is not owned by a quest alias | Unaffected by `QuestPriority` (falls through to default redirect) |

---

## 4. Exact Hook Evaluation Order

Every time the engine evaluates which AI package should run on an actor (`PackageStartHook::thunk`), the service checks rules in this exact order (**first match wins**):

1. **Unregistered / Player Check**: If the actor is the player or is not registered in `PackageOverrideService`, pass through untouched.
2. **Follower / Teammate Exemption**: If the actor is currently following the player (`Utils::IsPlayerFollower`), pass through untouched.
3. **Null or Already-Running Base Package**: If the engine's candidate is `null`, assert the actor's M.A.R.A.S base package; if the candidate is already the actor's M.A.R.A.S base package, keep running it.
4. **`Whitelist` (Package EditorID)**: If the candidate package's EditorID matches `Whitelist`, **allow** the candidate package.
5. **`AllowedPlugins` (Originating Plugin)**: If the candidate package's source plugin matches `AllowedPlugins`, **allow** the candidate package.
6. **`QuestWhitelist` (Owning Quest EditorID)**: If the candidate package belongs to a quest alias whose quest EditorID matches `QuestWhitelist`, **allow** the candidate package.
7. **`QuestPriority` (Owning Quest Priority)**: If `QuestPriority >= 0` and the candidate package belongs to a quest alias, **allow** if `questPriority >= QuestPriority`, or **suppress** if `questPriority < QuestPriority`.
8. **Default Redirect**: Suppress the candidate package and return the actor's registered M.A.R.A.S base package (`0x6A`, `0xC0`, or `0x376`).

---

## 5. Wildcard Syntax

All patterns in `Whitelist`, `AllowedPlugins`, and `QuestWhitelist` are **case-insensitive** and support standard `*` and `?` wildcards:

| Pattern | Matches |
| :--- | :--- |
| `SkyrimNet*` | Any name starting with `SkyrimNet` |
| `*TalkToPlayer` | Any name ending with `TalkToPlayer` |
| `*Chat*` | Any name containing `Chat` |
| `DLC1_?erana*` | Single-character wildcard (`?`) plus prefix/suffix wildcard (`*`) |
| `ExactName` | Exact match only (case-insensitive) |

---

## 6. Default Shipped Configuration

[SKSE/Plugins/PackageOverrides.ini](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/PackageOverrides.ini) ships with the following defaults:

```ini
[PackageOverrides]
Whitelist=SkyrimNet*,WIGames*
AllowedPlugins=nwsFollowerFramework.esp
; QuestPriority=61
QuestWhitelist=TTM_ChildrenSandbox,TTM_DialoguesChildrenScenes,TTM_NpcsScenes
```

- **`Whitelist=SkyrimNet*,WIGames*`**: Permits all SkyrimNet AI action/conversation packages and vanilla world interaction games (such as children playing Tag or Hide-and-Seek).
- **`AllowedPlugins=nwsFollowerFramework.esp`**: Permits all packages from Nether's Follower Framework.
- **`QuestWhitelist=TTM_ChildrenSandbox,TTM_DialoguesChildrenScenes,TTM_NpcsScenes`**: Permits M.A.R.A.S's own scene and child-order quests (`Variable06` chores/games, `Variable07` forcegreets, and autonomous couple scenes) to override the default home sandbox whenever those scenes or orders are active.

---

## 7. Troubleshooting

### An NPC needs to travel somewhere for a quest
- **Quick In-Game Fix**: Open the **Prisma UI Dashboard $\rightarrow$ NPC Detail** (or `My Household`) for that NPC and toggle **Stay at Home** off (or tell them in dialogue they don't need to stay at home). Once the quest is done, toggle **Stay at Home** back on.
- **Permanent Mod Patch**: Add the quest's EditorID pattern to `QuestWhitelist` (e.g., `QuestWhitelist = TTM_ChildrenSandbox, TTM_DialoguesChildrenScenes, TTM_NpcsScenes, MyCustomQuest*`) or add the mod's `.esp` to `AllowedPlugins`.

### Inspecting Package Decisions in `MARAS.log`
Set the log level to **Debug** in the **Prisma UI Dashboard $\rightarrow$ Settings** tab. `PackageOverrideService` deduplicates log messages per actor (`CheckAndLog`) so `MARAS.log` cleanly records state transitions starting with `PackageStartHook:`, showing the exact candidate package EditorID, FormID, owning quest, and which rule allowed or suppressed it.
