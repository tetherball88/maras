# M.A.R.A.S – Package Override Configuration

This document covers how M.A.R.A.S manages AI packages on spouse-tenant actors and how you can configure it to coexist with other mods that give those same NPCs custom AI behaviour.

---

> **Who this is for**
>
> - Players who have installed follower frameworks, dialogue mods, or romance mods that control NPC AI on spouses tracked by M.A.R.A.S — and want them to work together cleanly.
>
> If your spouses are working fine with other mods already, you do not need to change anything here.

---

## What the Package Override Service Does

When an NPC moves into a player home (becomes a "tenant"), M.A.R.A.S assigns them a **base sandbox package** — a home-sandboxed AI package that makes them behave naturally inside their assigned house: eating, sleeping, sitting, wandering indoors, etc.

To keep this package active, M.A.R.A.S hooks the engine's AI package selection at the point where the game decides which package runs next. Every time any package tries to start on a managed actor, M.A.R.A.S evaluates it against a set of rules:

- **Allow** — let the other package run normally (the NPC leaves home, follows a quest, talks, etc.)
- **Redirect** — send the actor back to the MARAS sandbox package instead

This keeps spouses from wandering off or getting stuck in packages they picked up from quests, locations, or follower frameworks — while still allowing intentional, high-priority AI to work correctly.

**Quest alias/Scenes packages** - are disabled by default. If you need npc somewhere during quest:
- **Simple/preferred way:** simply talk to them and tell them they can come back to their home. This will make them abandond M.A.R.A.S home sandbox and allow them to be used in quests.
- **More complex way:** whitelist manually packages by `Whitelist` or `AllowedPlugins`, or rescued by `QuestWhitelist` if `QuestPriority` is active, any package owned by a quest alias with priority below the threshold is redirected to the home sandbox. This prevents most quest-driven behaviour from interfering with the spouse's home routine, while still allowing important quests (like marriage quests) to function if configured.


**Automatic exemption — follower/teammate state:** If the NPC is currently a player follower (in the follower faction, or actively following the player), the override is skipped entirely. Follower behaviour is never blocked.

---

## INI File Location

```
Data\SKSE\Plugins\PackageOverrides.ini
```

This file ships with M.A.R.A.S and contains commented-out examples of every option. You can edit it freely; changes take effect on the next game load.

---

## Configuration Options

All options go under the `[PackageOverrides]` section.

---

### `Whitelist`

```ini
Whitelist = SkyrimNet*, *TalkToPlayer
```

A comma-separated list of **package editor ID patterns**. Any package whose editor ID matches one of these patterns is always allowed to run, regardless of any other setting.

Use this for specific packages or families of packages that should never be blocked — for example, dialogue packages from NPC dialogue-expansion mods.

---

### `AllowedPlugins`

```ini
AllowedPlugins = nwsFollowerFramework.esp, MyFollowerMod.esp
```

A comma-separated list of **plugin name patterns**. Any package whose **originating plugin** (the `.esp`/`.esm`/`.esl` that created the package form — not patches that edit it) matches is always allowed to run.

Use this to permit every package from a whole mod without listing each editor ID individually. This is the recommended approach for follower framework compatibility.

---

### `QuestPriority`

```ini
QuestPriority = 61
```

An integer threshold for **quest-alias-driven packages**. When a candidate package is owned by a quest alias, M.A.R.A.S reads the owning quest's priority value and compares it against this threshold:

| Condition | Result |
|---|---|
| Quest priority ≥ `QuestPriority` | Package is **allowed** |
| Quest priority < `QuestPriority` | Package is **suppressed** → sandbox (unless rescued by `QuestWhitelist`) |
| No associated quest | This setting has **no effect** on the package |

Set to `-1` (or omit the line entirely) to disable quest-priority filtering. Typical Skyrim quest priorities range from 0 (lowest) to 100+ (highest). Most player-facing marriage/romance quests use priorities in the 50–80 range.

---

### `QuestWhitelist`

```ini
QuestWhitelist = MQ*, DLC1Serana*
```

A comma-separated list of **quest editor ID patterns**. When `QuestPriority` is active and a candidate package would otherwise be suppressed (its quest priority is below the threshold), M.A.R.A.S checks the owning quest's editor ID against these patterns. A match **exempts** the package from suppression and lets it run.

Has no effect if `QuestPriority` is not set.

---

## Evaluation Order

Rules are checked in this order; **the first match wins**:

1. **Follower/teammate state** — if the NPC is currently following the player, skip all overrides entirely.
2. **`Whitelist`** — allow by package editor ID pattern.
3. **`AllowedPlugins`** — allow by originating plugin name pattern.
4. **`QuestPriority`** — allow or suppress based on the owning quest's priority, with `QuestWhitelist` as a rescue.
5. **Default** — redirect to the MARAS home sandbox package.

---

## Wildcard Syntax

Patterns in `Whitelist`, `AllowedPlugins`, and `QuestWhitelist` all support the same simple wildcard rules (matching is **case-insensitive**):

| Pattern | Matches |
|---|---|
| `SkyrimNet*` | Anything starting with `SkyrimNet` |
| `*TalkToPlayer` | Anything ending with `TalkToPlayer` |
| `*Chat*` | Anything containing `Chat` |
| `ExactName` | Exact match only |

Multiple patterns are separated by commas. Whitespace around commas is ignored.

---

## Default Shipped Configuration

The file that ships with M.A.R.A.S contains these active settings:

```ini
[PackageOverrides]
Whitelist = SkyrimNet*
AllowedPlugins = nwsFollowerFramework.esp
; QuestPriority = 61
; QuestWhitelist =
```

- **`SkyrimNet*`** allows all SkyrimNet NPC dialogue packages (covers SkyrimNet AI dialogue companions).
- **`nwsFollowerFramework.esp`** allows all packages from Nether's Follower Framework, preventing any home-sandbox conflict when NFF manages a spouse.
- `QuestPriority` and `QuestWhitelist` are commented out by default (disabled).

---

## Common Compatibility Scenarios

### Follower framework (NFF, EFF, AFT, etc.)

Add the framework's plugin name to `AllowedPlugins`:

```ini
AllowedPlugins = nwsFollowerFramework.esp, EFFCore.esm, AFT.esp
```

### NPC dialogue expansion mods (SkyrimNet, Interesting NPCs, etc.)

Add the package editor ID prefix to `Whitelist`:

```ini
Whitelist = SkyrimNet*, 3DNPCFollower*
```

Alternatively, add the plugin to `AllowedPlugins` if you know the plugin filename:

```ini
AllowedPlugins = 3DNPC.esp
```

### OStim / romance quest mods

Most OStim-driven packages are scene-based and do not need to be listed. If you find a specific romance quest breaking, add the quest editor ID to `QuestWhitelist` with `QuestPriority` active:

```ini
QuestPriority = 61
QuestWhitelist = C00,MG04
```

---

## Troubleshooting

**Spouse is stuck sandboxing at home and won't follow me**

This should never happen — follower state is checked first and overrides are skipped automatically. If this occurs, check that the actor is actually being added to the follower/teammate faction by your framework. Try to releive npc from player's house.

**A mod's dialogue or scene is getting interrupted on a managed spouse**

Identify the package editor ID or its source plugin via `TES5Edit` or the console (`cgf "Debug.GetFormString" pkgRef`). Add the editor ID to `Whitelist` or the plugin file to `AllowedPlugins`(don't add vanilla plugins, it will make spouses to use vanilla packages instead of M.A.R.A.S).

**Enable debug logging to see what is being blocked**

Set the log level to debug in the MCM (Settings page → Log Level → Debug), then load the game and trigger the behaviour. Check `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\MARAS.log` for lines starting with `PackageStartHook:` — they will show which packages are being allowed or suppressed and why.
