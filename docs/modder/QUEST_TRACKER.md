# M.A.R.A.S — Quest Event Tracker Configuration (`questEvents/*.json`)

This document explains how to create declarative JSON configuration files in `Data/SKSE/Plugins/MARAS/questEvents/` so vanilla or modded quests automatically trigger M.A.R.A.S relationship promotions, affection changes, or archetype updates without modifying the quests in the Creation Kit or recompiling Papyrus scripts.

---

## Related Documentation

- [QuestEventManager Mechanics](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/QuestEventManager.md) — Architectural overview of `QuestEventHandler.cpp` and `QuestEventConfigLoader.cpp`.
- [NPCIndex Mechanics](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCIndex.md) — How Base `TESNPC` FormIDs, Placed Reference FormIDs, and NPC names are resolved.
- [Advanced Configuration and Modding](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/ADVANCED_CONFIGURATION_AND_MODDING.md) — Guide to combining `TTM_IgnoreProposal` SPID keywords with Quest Event Tracker configs for custom follower marriage mods.

---

## 1. File Location & Structure

Place one or more `.json` files in:
```text
Data/SKSE/Plugins/MARAS/questEvents/
```
All `.json` files in this folder are scanned and loaded automatically at `kDataLoaded` (`QuestEventManager::LoadConfigFromFolder`).

```json
{
  "__metaInfo": "Optional file-level description (any key starting with __metaInfo is ignored)",
  "__formData|PluginName.esp|0xFormID": {
    "comment": "Optional human-readable note",
    "onStart": [
      "commandType:npcSpecifier:argument"
    ],
    "onStop": [
      "commandType:npcSpecifier:argument"
    ],
    "onStageChange:10": [
      "commandType:npcSpecifier:argument"
    ],
    "onStageChange:100": [
      {
        "type": "commandType",
        "npc": "npcSpecifier",
        "arg": "argument"
      }
    ]
  }
}
```

### Quest Identification (`__formData|PluginName|0xFormID`)
- **`PluginName`**: The `.esm`, `.esp`, or `.esl` file that defines the quest (e.g., `Skyrim.esm`, `Dawnguard.esm`, `SDA.esp`). If the plugin is not installed in the user's load order, the quest entry is safely skipped.
- **`0xFormID`**: The local hexadecimal FormID of the `TESQuest` record (e.g., `0x00021382` for *The Book of Love* in `Skyrim.esm`).
- **`__metaInfo*` Keys**: Top-level keys starting with `__metaInfo` are ignored by the parser and can be used for file metadata.

---

## 2. Quest Lifecycle Triggers

Each quest object can define any combination of three trigger types:

1. **`onStart`**: Array of commands executed when the quest starts (`RE::TESQuestStartStopEvent` with `started == true`).
2. **`onStop`**: Array of commands executed when the quest stops or completes (`RE::TESQuestStartStopEvent` with `started == false`).
3. **`onStageChange:<StageNumber>`**: Array of commands executed when the quest advances to stage `<StageNumber>` (`RE::TESQuestStageEvent`). You can define as many `onStageChange:<N>` keys per quest as needed.

Commands within an array are executed sequentially in the order listed.

---

## 3. Command Syntax

Commands can be written in either **compact colon-delimited string format** or **JSON object format**:

### String Format
```json
"commandType:npcSpecifier:argument"
```
> [!IMPORTANT]
> Because the parser splits on the **first two colons (`:`)**, if your `npcSpecifier` is a `__formData|Plugin|0xHexID` key (which uses pipe `|` characters, not colons), both formats work identically.

### Object Format
```json
{
  "type": "commandType",
  "npc": "npcSpecifier",
  "arg": "argument"
}
```
- `type` is matched **case-insensitively** (`"promoteToStatus"`, `"setAffection"`, `"changeAffection"`, `"addDailyAffection"`, `"setSocialClass"`, `"setSkillType"`, `"setTemperament"`).
- In object format, `"arg"` can be a string (`"50"`), number (`50`), or boolean (`true`), and is validated as non-empty at load time.

---

## 4. Three-Tier NPC Target Resolution (`npcSpecifier`)

When a quest event fires, `QuestEventManager::ResolveNPC` resolves `npcSpecifier` to an `RE::FormID` using three strategies in order:

### Strategy 1: Form Key (`__formData|PluginName|0xFormID`)
If `npcSpecifier` starts with `__formData|`, M.A.R.A.S resolves the FormID against the active load order and queries [NPCIndex](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCIndex.md) (`GetRefID`, falling back to the resolved FormID):
- Works with **either** the NPC's placed `Actor` Reference FormID **or** their base `TESNPC` FormID (`NPCIndex` automatically translates Base IDs to their canonical placed world reference), even if the NPC's 3D reference is not currently loaded in memory.
- **Example**: `"promoteToStatus:__formData|Dawnguard.esm|0x002B74:Married"`

### Strategy 2: Quest Reference Alias Name (`aliasName`)
If `npcSpecifier` does not start with `__formData|`, M.A.R.A.S searches the triggering quest's `aliases` array for a `BGSRefAlias` whose alias name matches `npcSpecifier` (case-sensitive) and extracts the alias's current `RE::Actor` FormID.
- Ideal for radiant favor quests or multi-NPC quests where the target actor is dynamically bound to a quest alias.
- **Example**: `"changeAffection:LoveInterest:15"`

### Strategy 3: Fallback Actor Name Search (`NPCIndex::FindActorByName` / `FindByName`)
If no quest alias matches `npcSpecifier`, M.A.R.A.S falls back to searching `NPCIndex` by the NPC's display name (checking both loaded actors and the persistent index).
- **Example**: `"setAffection:Serana:80"`

---

## 5. Supported Commands Reference

### 5.1 Relationship Status (`promoteToStatus`)
Transitions the resolved NPC's relationship with the player via [NPCRelationshipManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCRelationshipManager.md).

**Syntax**: `"promoteToStatus:<npcSpecifier>:<statusKeyword>"`

| `statusKeyword` (Case-Insensitive) | Behavior |
| :--- | :--- |
| `Candidate` | Registers the NPC as a romantic candidate (`RegisterAsCandidate`). |
| `Dating` | Promotes the NPC to `Dating` with the player (`PromoteToDating`). |
| `Lover` | Promotes the NPC to `Lover` with the player (`PromoteToLover`). |
| `Engaged` | Promotes the NPC to `Engaged` with the player (`PromoteToEngaged`). |
| `Married` | Promotes the NPC to `Married` with the player (`PromoteToMarried`). |
| `Divorced` | Transitions the NPC to `Divorced` from the player (`PromoteToDivorced`). |
| `Jilted` | Transitions the NPC to `Jilted` (`PromoteToJilted`). |
| `ExDating` | Transitions the NPC to `ExDating` (`PromoteToExDating`). |
| `ExLover` | Transitions the NPC to `ExLover` (`PromoteToExLover`). |
| `Deceased` | Special keyword that archives the NPC in [DeceasedRegistry](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/DeceasedRegistry.md) (`RecordDeceasedAdult`) and runs full death cleanup (`HandleNPCDeath` / `EvaluateChildCustodyOnDeath`) when a quest scripts their death. |

### 5.2 Affection Commands (`setAffection`, `changeAffection`, `addDailyAffection`)
Modifies the NPC's standing in [AffectionService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/AffectionService.md):

| Command | Argument | Behavior |
| :--- | :--- | :--- |
| `setAffection` | Integer (`0..100`) | Sets the NPC's permanent affection score to an exact value (`SetPermanentAffection`). |
| `changeAffection` | Signed Integer (`+N` / `-N`) | Adds a signed delta directly to the NPC's permanent affection score. |
| `addDailyAffection` | Float (`+N.N` / `-N.N`) | Adds to today's `"quest"` daily affection channel (`AddAffection(id, amount, "quest")`), which rolls up into permanent affection at midnight. |

### 5.3 Archetype Overrides (`setSocialClass`, `setSkillType`, `setTemperament`)
Updates the NPC's archetype in `NPCRelationshipManager` (and recalculates player buffs and `TT_MARAS.esp` archetype factions):

| Command | Valid Arguments (Case-Insensitive) |
| :--- | :--- |
| `setSocialClass` | `Outcast`, `Poverty`, `Working`, `Middle`, `Wealthy`, `Religious`, `Nobles`, `Rulers` |
| `setSkillType` | `Warrior`, `Mage`, `Rogue`, `Craftsman`, `Ranger`, `Orator` |
| `setTemperament` | `Proud`, `Humble`, `Jealous`, `Romantic`, `Independent` |

---

## 6. Practical Examples

### Example 1: Custom Follower Marriage Quest Integration
When paired with `Keyword = TTM_IgnoreProposal|Serana` via SPID, this config registers Serana as a candidate when her romance quest starts and promotes her to `Married` when the custom wedding stage completes:

```json
{
  "__metaInfo": "Serana Dialogue Addon romance & marriage integration",
  "__formData|SDA.esp|0x123456": {
    "comment": "SDA Romance Quest",
    "onStart": [
      "promoteToStatus:__formData|Dawnguard.esm|0x002B74:Candidate",
      "setSocialClass:__formData|Dawnguard.esm|0x002B74:Rulers",
      "setSkillType:__formData|Dawnguard.esm|0x002B74:Mage",
      "setTemperament:__formData|Dawnguard.esm|0x002B74:Independent"
    ],
    "onStageChange:50": [
      "promoteToStatus:__formData|Dawnguard.esm|0x002B74:Engaged",
      "changeAffection:__formData|Dawnguard.esm|0x002B74:20"
    ],
    "onStageChange:100": [
      "promoteToStatus:__formData|Dawnguard.esm|0x002B74:Married",
      "setAffection:__formData|Dawnguard.esm|0x002B74:100"
    ]
  }
}
```

### Example 2: Radiant Favor Quest Using Quest Aliases
Rewards the NPC filling the quest's `FavorTarget` reference alias with daily affection and registers them as a candidate upon quest completion:

```json
{
  "__formData|MyFavorPack.esp|0x000800": {
    "comment": "Radiant citizen delivery favor",
    "onStageChange:20": [
      "promoteToStatus:FavorTarget:Candidate",
      "addDailyAffection:FavorTarget:10"
    ],
    "onStop": [
      "changeAffection:FavorTarget:15"
    ]
  }
}
```

---

## 7. Troubleshooting

- **Check Startup Load Statistics in `MARAS.log`**:
  At `kDataLoaded`, `QuestEventConfigLoader` logs a summary line:
  `Quest event config loading complete. Files: X/Y, Quests: A/B, Invalid keys: C, Unresolved: D`
- **Command Execution Logs**:
  Whenever a tracked quest starts, stops, or changes stage, `QuestEventManager` logs:
  `Quest stage changed: <QuestName> (0x...) stage <N>, executing <M> commands`
  Set the log level to **Debug** in the **Prisma UI Dashboard $\rightarrow$ Settings** tab to see individual command and NPC resolution trace lines.
