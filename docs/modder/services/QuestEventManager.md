# QuestEventManager — Data-Driven Quest Reaction Engine

[QuestEventHandler.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventHandler.cpp) and [QuestEventConfigLoader.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/QuestEventConfigLoader.cpp) allow vanilla and modded quests to automatically influence M.A.R.A.S relationships, affection scores, and NPC archetypes without requiring Creation Kit edits or custom Papyrus scripts on those quests.

By placing JSON files inside `Data/SKSE/Plugins/MARAS/questEvents/`, modders can bind declarative M.A.R.A.S commands to any quest's start, stop, or specific stage transitions.

---

## 1. JSON Configuration Discovery & Loading

At `kDataLoaded`, the loader scans every `.json` file in `Data/SKSE/Plugins/MARAS/questEvents/`:

1. **Form Key Resolution**: Top-level JSON keys formatted as `__formData|<PluginName>|<HexFormID>` are parsed and resolved against the active load order.
   - If the plugin is not installed or the FormID does not resolve to a valid quest in the current load order, the entry is safely skipped without erroring out the rest of the file.
   - Keys starting with `__metaInfo` are ignored as documentation/metadata blocks.
2. **Lifecycle Hook Binding**: Under each resolved quest key, the loader looks for three trigger categories:
   - `onStart`: Commands executed immediately when the quest starts (`TESQuestStartStopEvent` with `started == true`).
   - `onStop`: Commands executed when the quest stops (`TESQuestStartStopEvent` with `started == false`).
   - `onStageChange:<StageNumber>`: Commands executed when the quest advances to that exact stage (`TESQuestStageEvent`). Multiple stage keys can be defined per quest.
3. **Command Formats**: Each trigger array accepts commands written either as compact colon-delimited strings (`"commandType:npcSpecifier:argument"`) or as structured JSON objects (`{"type": "...", "npc": "...", "arg": ...}` where `"arg"` can be a string, number, or boolean). Command types are matched case-insensitively, and entries missing any of `type`, `npc`, or `arg` are rejected at load time.
4. **Fast Runtime Event Filtering**: When the engine dispatches `TESQuestStartStopEvent` or `TESQuestStageEvent`, the event sinks first verify that the quest FormID exists in `questConfigs_` and has non-empty commands for that specific trigger/stage before looking up the `TESQuest` form, avoiding unnecessary engine form lookups on unconfigured quests.

---

## 2. Three-Tier NPC Target Resolution

Because quests reference NPCs in different ways—some use static actors while radiant quests populate dynamic aliases at runtime—the engine resolves the `npcSpecifier` in each command to an `RE::FormID` using a three-step fallback hierarchy:

1. **Direct Form Key (`__formData|Plugin|HexID`)**:
   - If the specifier starts with `__formData|`, it resolves the FormID and queries [NPCIndex](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCIndex.md) (`GetRefID`, falling back to the resolved FormID) to translate either a base `TESNPC` or placed `Actor` FormID into the canonical actor FormID without requiring the actor's 3D reference to be currently loaded.
2. **Active Quest Reference Alias (`aliasName`)**:
   - If not a form key, the engine iterates through the triggering quest's aliases looking for a `BGSRefAlias` whose name matches `npcSpecifier`.
   - When found, it extracts the alias's current runtime `RE::Actor` FormID. This allows radiant favor quests (e.g., rescuing a random citizen or delivering an item for an alias target) to automatically reward or register whichever NPC filled that alias.
3. **Fallback Name Lookup**:
   - If no matching quest alias is found, the specifier is passed to `NPCIndex` (`FindActorByName` and `FindByName`) for a direct actor name search across both loaded actors and the persistent index.

---

## 3. Supported Quest Reaction Commands

When a monitored quest starts, stops, or hits a configured stage, the manager executes each bound command sequentially:

### Relationship Progression (`promoteToStatus`)
Transitions the resolved NPC's relationship with the player in [NPCRelationshipManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCRelationshipManager.md):
- **`candidate`**: Registers the NPC as a romantic candidate (e.g., upon completing their personal favor quest).
- **`dating` / `lover` / `engaged` / `married` / `divorced` / `jilted` / `exdating` / `exlover`**: Promotes or transitions the NPC's player-relationship status directly.
- **`deceased`**: Special keyword that archives the NPC in [DeceasedRegistry](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/DeceasedRegistry.md) (`RecordDeceasedAdult`) and invokes `NPCRelationshipManager::HandleNPCDeath` (or `EvaluateChildCustodyOnDeath` if unregistered) when a quest scripts their narrative death.

### Affection Adjustments
Directly modifies the NPC's standing in [AffectionService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/AffectionService.md):
- **`setAffection`**: Overrides the NPC's permanent affection score to an exact integer value.
- **`changeAffection`**: Adds a signed integer delta (`+` or `-`) directly to the NPC's permanent affection score.
- **`addDailyAffection`**: Contributes a floating-point amount to today's daily affection accumulator under the `"quest"` interaction category (subject to daily caps and midnight rollup).

### Archetype & Personality Overrides
Updates the NPC's core attributes in `NPCRelationshipManager` (which automatically recalculates their household buffs and updates their `TT_MARAS.esp` archetype factions):
- **`setSocialClass`**: Changes the NPC's social standing (e.g., an NPC rising to `nobles` or `rulers` after a civil war or guild storyline, or falling to `outcast` / `poverty`).
- **`setSkillType`**: Reassigns the NPC's combat/craft skill archetype (`warrior`, `mage`, `rogue`, `craftsman`, `ranger`, `orator`).
- **`setTemperament`**: Updates the NPC's romantic temperament (`proud`, `humble`, `jealous`, `romantic`, `independent`) in response to character-arc quests.

