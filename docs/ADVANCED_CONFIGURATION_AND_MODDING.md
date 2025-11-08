# Advanced Configuration and Modding

This document is for advanced users and mod authors.

It covers:

- Permanent spouse archetype overrides via JSON in `SKSE\Plugins\MARAS\spousesTypes`
- Adjusting buff values via `SKSE\Plugins\MARAS\bonuses.json`
- Papyrus integration points (global functions and mod events) for custom relationship systems such as ORomance, OStim Romance, and similar

This is all external configuration and scripting. For in-game per-save overrides, use the MCM instead.

---

> **If you’re not a modder…**
>
> If you’re a player rather than a script/mod author, you probably want:
>
> - [Player Guide](PLAYER_GUIDE.md) – gameplay flow and behaviour.
> - [MCM Settings Guide](MCM_Settings.md) – in-game configuration per save.
> - [NPC Explorer MCM](MCM_NPC.md) – per-NPC editing inside the current save.
>
> This document is for **permanent, external configuration** and **Papyrus integration**.

---

## 1. Spouse Archetype Overrides (JSON)

By default, M.A.R.A.S automatically assigns each spouse an archetype:

- Social class
- Skill type
- Temperament

If you want full control, you can override these with JSON files. These overrides are:

- Permanent (they apply across saves and playthroughs)
- Data driven (no plugin edits required)
- Applied only when the NPC is first registered by M.A.R.A.S

If an NPC is already registered in an existing save, these JSON overrides will not retroactively change their archetype. Use the MCM in that playthrough to override them instead.

### 1.1 Folder and file layout

Create JSON override files in this folder (relative to Skyrim root):
```
SKSE\Plugins\MARAS\spousesTypes
```

You can place multiple `.json` files in this directory. Each file must contain a single JSON object at the root level.

Example of a file with a single override (Serana):
```json
{
    "__formData|Dawnguard.esm|0x2b74": {
        "comment": "Serana",
        "social": "rulers",
        "skill": "mage",
        "temperament": "independent"
    },
    // other NPC overrides can go here
}
```

You can define multiple NPC overrides in the same file by adding more key–value pairs to that root object.

### 1.2 Key syntax (JContainers-like form reference)

Each NPC entry key follows JContainers (JContainers aren't a requirement though) syntax:

`__formData|pluginNameWithExtension|FormId`

Where:

- `__formData` is the JContainers-like directive
- `pluginNameWithExtension` is the plugin that defines the NPC, for example:
  - Skyrim.esm
  - Dawnguard.esm
  - MyCustomFollowers.esp
- `FormId` is the hex form ID of the NPC, prefixed with 0x

Example: Serana from Dawnguard

`__formData|Dawnguard.esm|0x2b74`

This full string is used as the JSON key for that NPC.

### 1.3 Value object (archetype override)

Each key maps to one JSON object that can contain:

- `comment`
- `social`
- `skill`
- `temperament`

Field details:

- `comment`
  - Free text
  - Only for humans, not used by the mod
  - Helps you remember which NPC this line is for or why you changed it

- `social`
  - One of the valid social class strings (see the list below)
  - Defines the NPC’s social class for MARAS archetype logic and buffs
  - Valid values: `outcast` | `poverty` | `working` | `middle` | `wealthy` | `religious` | `nobles` | `rulers`

- `skill`
  - One of the valid skill type strings (see the list below)
  - Defines which skill archetype this spouse belongs to
  - Valid values: `warrior` | `mage` | `rogue` | `craftsman` | `ranger` | `orator`

- `temperament`
  - One of the valid temperament strings (see the list below)
  - Controls how the spouse responds in the affection system
- Valid values: `proud` | `humble` | `jealous` | `romantic` | `independent`

>If you use a value outside these lists, behavior is undefined (and the override may be ignored or cause fallback behavior).

> These string values correspond directly to the archetypes described in:
>
> - [Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md) – social classes, skill types, and temperaments and what they actually do in-game.
>
> Using values outside these lists may cause MARAS to ignore the override or fall back to auto-detection.


A more explicit example with all fields:

```json
{
    "__formData|Dawnguard.esm|0x2b74": {
        "comment": "Serana – rulers mage independent", "social": "rulers",
        "skill": "mage",
        "temperament": "independent"
    }
}
```

### 1.5 Conflict resolution between multiple JSON files

If multiple JSON files in `SKSE\Plugins\MARAS\spousesTypes` override the same NPC (same `__formData|...` key):

- The file whose name is alphabetically last wins
- Example:
  - `00_serana_base.json`
  - `zzz_serana_override.json`

  If both contain an entry for Serana, the entry from `zzz_serana_override.json` will be used.

This lets you ship “patch” files or personal edits that override other packs without editing or replacing them.

### 1.6 When overrides are read and applied

- Archetype overrides are only applied when an NPC is first registered into the MARAS system
- If the NPC is already registered in a save and has any archetype, JSON changes will not affect that existing save
- For existing spouses in an ongoing playthrough, use the MCM to adjust their archetype
- For new playthroughs or unregistered NPCs, the JSON values will be used when MARAS registers them

### 1.7 Example: “Royal” Dawnguard family overrides

For example, you might want to treat:

- Lord Harkon
- Valerica
- Serana

as rulers in terms of social class, instead of letting auto detection possibly assign them a lower class.

You could create a file such as:

`SKSE\Plugins\MARAS\spousesTypes\Dawnguard_Royal_Family.json`

With content similar to:

```json
{
    "__formData|Dawnguard.esm|0x3BA8": {
        "comment": "Lord Harkon – rulers",
        "social": "rulers"
    },
    "__formData|Dawnguard.esm|0x3B8B": {
        "comment": "Valerica – rulers",
        "social": "rulers"
    },
    "__formData|Dawnguard.esm|0x2b74":  {
        "comment": "Serana – rulers",
        "social": "rulers",
        "skill": "mage",
        "temperament": "independent"
    }
}
```

Only fields you specify are overridden. If you set only `social`, MARAS can still auto-detect or preserve the existing `skill` and `temperament`, depending on when the NPC is registered.

You can distribute such JSON files separately, for example:

- A small patch that adjusts social classes for various vanilla or DLC NPCs
- Support packs that set archetypes for custom follower mods
- Personal override packs for your preferred lore or balance

---

## 2. Buff Values Configuration (bonuses.json)

MARAS also exposes buff values in a JSON file so you can tweak how powerful each archetype’s bonuses are.

Path:
```
SKSE\Plugins\MARAS\bonuses.json
```

This file stores the numeric values used for:

- Spouse buffs based on social class
- Spouse buffs based on skill type
- Any derived buffs that spouses grant while married or following
- Temporary follower buffs where applicable

You can:

- Increase values if you want a more “heroically powerful” marital system
- Decrease values if you want buffs to be more subtle and less game changing
- Adjust specific combinations if you feel some archetypes are underpowered or too strong

Changes take effect when:

- The game loads a save (on load)
- A spouse is added or removed
- Follower status changes for buffs that depend on the spouse currently following you

Note: Always keep a backup of the original bonuses.json before editing, in case you want to restore default balance.

---

## 3. Papyrus Integration for Custom Relationship Mods

For mod authors and advanced users who want to integrate their own relationship systems (ORomance, OStim Romance, etc.) with MARAS, there are several global functions and events exposed in `MARAS.psc`.

The main relationship status flow in MARAS is:
```
candidate -> engaged -> jilted or married -> divorced
```

Core promotion and state changes are handled by SKSE native functions defined in `MARAS.psc`.

### 3.1 Promotion functions

These are the primary APIs to move an NPC through MARAS relationship statuses. (Names here are descriptive; refer to `MARAS.psc` for exact signatures.)

- `PromoteNPCToStatus`
  - Promote or demote an NPC by passing the status name as string
  - Example statuses: candidate, engaged, jilted, married, divorced (check `MARAS.psc` for full list of allowed values)

- `PromoteNPCToStatusByEnum`
  - Same idea as above, but uses an integer enum instead of a string
  - Useful if you prefer enums in native code or want to avoid typos in string names

When an NPC changes status through these functions, MARAS fires a Papyrus event for listeners.

### 3.2 Status change event

Mod event name:

`maras_status_changed`

Papyrus signature:
```papyrus
Event YourEventHandleName(String EventName, String status, Float statusEnum, Form npc)
EndEvent
```

Arguments:

- EventName
  - The mod event name (`maras_status_changed`)
- status
  - Status name as string (for example: `married`, `engaged`, etc.)
- statusEnum
  - Status as a float-encoded enum value
- npc
  - The NPC whose status just changed (Form, usually castable to Actor)

Use this event if your mod wants to react when someone becomes a candidate, gets engaged, is jilted, married, or divorced inside MARAS.

### 3.3 Hierarchy change event (spouse ranks)

MARAS can track hierarchy ranks between multiple spouses (for example, "first spouse", "second spouse", etc.). When a spouse’s hierarchy rank changes, MARAS fires:

Mod event name:

`maras_hierarchy_changed`

Papyrus signature:
```papyrus
Event YourEventHandleName(String EventName, String promotDemote, Float rankDiff, Form spouse)
EndEvent
```

Arguments:

- EventName
  - The mod event name (`maras_hierarchy_changed`)
- promotDemote
  - A string describing whether this was effectively a promotion or demotion
- rankDiff
  - How much the rank changed
- spouse
  - The spouse whose rank was adjusted

You can use this to:

- Adjust jealousy or approval in your own system
- Drive custom reactions when a spouse is “demoted” for a newer spouse
- Integrate with mod-added scenes or dialogue

### 3.4 Affection level change event

When a spouse’s affection crosses thresholds between:
```
happy -> content -> troubled -> estranged
```

MARAS fires:

Mod event name:

`maras_change_affection`

Papyrus signature:

```papyrus
Event YourEventHandleName(String EventName, String level, Float affectionDiff, Form spouse)
EndEvent
```

Arguments:

- EventName
  - The mod event name (`maras_change_affection`)
- level
  - The new affection band name (example: `happy`, `content`, `troubled`, `estranged`)
- affectionDiff
  - The difference in affection value that triggered the change, positive - improving, negative - worsening
- spouse
  - The spouse whose affection band changed

This is useful if your mod wants to:

- Trigger arguments or romantic scenes when the relationship gets worse or better
- Apply extra buffs or debuffs based on how happy the spouse is
- Drive custom story events tied to specific affection levels

### 3.5 Teammate / follower state change event

When a spouse starts or stops following the player, MARAS fires:

Mod event name:

`maras_teammate_change`

Papyrus signature:

```papyrus
Event YourEventHandleName(String EventName, String strArg, Float fltArg, Form sender)
EndEvent
```

Typical usage:

- eventName
  - The mod event name (`maras_teammate_change`)
- strArg
  - not used
- fltArg
  - not used
- sender
  - not used

You can use this to:

- Sync your own follower-related systems
- Enable or disable buffs, tracking, or UI when the spouse becomes a follower
- Fire custom scenes when a spouse joins you on an adventure

---

## 4. Custom Affection Types (Native Functions)

MARAS supports custom affection “channels” so other mods can add their own reasons for affection changes without fighting over a single value.

These are exposed as native functions in `MARAS.psc`.

### 4.1 AddAffection

```papyrus
Function AddAffection(Actor npc, float amount, string type)
```

- npc
  - The NPC whose affection you are modifying
- amount
  - Positive values add affection, negative values reduce it
- type
  - A string key that identifies your affection source

Notes:

- Do not reuse type strings already used internally by MARAS
  - Always pick your own unique type name (for example, "ORomanceDate", "OStimScene", "MyModGift", etc.)
- MARAS will treat this as one daily affection source and will include it when evaluating total affection

### 4.2 SetAffectionMinMax

```papyrus
Function SetAffectionMinMax(string type, int minVal, int maxVal)
```

- type
  - The same string key you used in AddAffection
- minVal, maxVal
  - Clamp range for this affection type

Use this to:

- Ensure your custom affection channel cannot explode to absurd values
- Limit negative affection so it does not soft-lock the relationship forever
- Keep your systems balanced with MARAS’s own affection logic

### 4.3 Daily affection evaluation

Each in-game day, when MARAS evaluates daily affection:

- All internal MARAS affection types are updated
- All custom affection types (added via AddAffection) are included
- Affection min–max caps defined with SetAffectionMinMax are respected

As a result, your mod’s contributions are fully integrated into:

- Overall affection values
- Affection band transitions (happy, content, troubled, estranged)
- Any MARAS logic that depends on affection level


> For a full explanation of how daily affection roll-ups work and how custom affection feeds into relationship states (happy/content/troubled/estranged), see the [Affection System – Deep Dive](AFFECTION_SYSTEM.md).


---

## 5. More Functions in MARAS.psc

This document only covers the major integration points most mods will need:

- Promotion functions
- Status, hierarchy, affection, and teammate change events
- Custom affection type functions

MARAS.psc` contains additional helpers and native functions that may be useful depending on how deep you want to integrate. You can open MARAS.psc directly in your Papyrus editor to:

- See full function signatures
- Discover additional global helpers
- Understand available enums and constants

Always test integrations in a clean environment and keep backups of your JSON configs (spousesTypes and bonuses.json) before making large changes.
