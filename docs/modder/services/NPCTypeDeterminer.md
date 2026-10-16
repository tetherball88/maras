# NPCTypeDeterminer — Automatic Archetype & Personality Classification

[NPCTypeDeterminer.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCTypeDeterminer.cpp) automatically classifies any vanilla or custom-modded NPC into three core M.A.R.A.S archetypes when they are first registered:
1. **Social Class** (`Poverty`, `Working`, `Middle`, `Wealthy`, `Religious`, `Nobles`, `Rulers`, `Outcast`)
2. **Skill Type** (`Warrior`, `Mage`, `Rogue`, `Craftsman`, `Ranger`, `Orator`)
3. **Temperament** (`Proud`, `Humble`, `Jealous`, `Romantic`, `Independent`)

These three attributes govern proposal difficulty expectations, passive player buffs ([SpouseBuffService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/SpouseBuffService.md)), daily home-share store income ([SpouseAssetsService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/SpouseAssetsService.md)), housed spouse vendor inventory ([PlayerHouseService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/PlayerHouseService.md)), affection decay multipliers ([AffectionService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/AffectionService.md)), and SkyrimNet persona context.

---

## 1. JSON Override Precedence

For all three attributes, the determiner first checks whether an explicit override exists in `Data/SKSE/Plugins/MARAS/npcOverrides/` (via `JsonOverrideLoader`). If a modder or user has defined a specific social class, skill type, or temperament for that NPC's FormID, the override is used immediately and heuristic detection is bypassed.

---

## 2. Social Class Heuristics

When no JSON override is present, Social Class is determined by inspecting the NPC's base and runtime factions, followed by equipped attire:

1. **Base & Runtime Faction FormList Evaluation**:
   - Inspects all active factions on the NPC's `TESNPC` base record (`npcBase->factions`) **and**, when a live `RE::Actor*` is available, any runtime faction additions or removals stored in `ExtraFactionChanges` (`actor->extraList`), evaluating them against seven curated FormLists cached in `FormCache` (`RulerFactions`, `NobleFactions`, `ReligiousFactions`, `WealthyFactions`, `MiddleFactions`, `PovertyFactions`, `OutcastFactions`).
   - Works even if the NPC's placed `RE::Actor*` reference is unloaded or not yet resolved, as long as `TESNPC` is available.
   - If an NPC belongs to multiple matching factions, the **highest-status social class index** wins.
2. **Rich Clothing Inspection**:
   - If a live `RE::Actor*` is available and the faction scan results in a class below `Wealthy` (or no match at all), the service scans 29 biped equipment slots on the live actor (`Head`, `Body`, `Hands`, `Amulet`, `Ring`, `Circlet`, `Ears`, and modular armor slots).
   - If any equipped item carries the vanilla `ClothingRich` keyword, the NPC is automatically promoted to **Wealthy**.
3. **Default Fallback**:
   - If neither factions nor rich clothing match, the NPC defaults to **Working** class.

---

## 3. Skill Type Heuristics

When no JSON override is present, Skill Type is resolved through a two-stage pipeline:

### Stage 1: NPC Class (`TESClass`) FormList Lookup
The NPC's `TESClass` record is checked against six class FormLists in `FormCache` in the following priority order:
1. **Orator** (`OratorClasses` — e.g., Bards, Merchants, Citizen speech classes)
2. **Ranger** (`RangerClasses` — e.g., Scouts, Archers, Hunters)
3. **Rogue** (`RogueClasses` — e.g., Thieves, Assassins, Nightblades)
4. **Craftsman** (`CraftsmanClasses` — e.g., Blacksmiths, Alchemists, Miners, Lumberjacks)
5. **Mage** (`MageClasses` — e.g., Wizards, Priests, Spellswords, Witchblades)
6. **Warrior** (`WarriorClasses` — e.g., Soldiers, Housecarls, Mercenaries, Bandits)

### Stage 2: Highest ActorValue Skill Fallback
If the NPC uses a custom modded class not present in any of the six FormLists, the determiner reads all 18 combat, magic, stealth, and crafting `ActorValue` skills on the NPC and finds the single highest skill:
- **Warrior**: Highest skill is One-Handed, Two-Handed, Block, or Heavy Armor.
- **Ranger**: Highest skill is Archery.
- **Rogue**: Highest skill is Pickpocket, Lockpicking, or Sneak.
- **Mage**: Highest skill is Alteration, Conjuration, Destruction, Illusion, or Restoration.
- **Orator**: Highest skill is Speech.
- **Craftsman**: Highest skill is Smithing, Alchemy, or Enchanting.
- **Light Armor Tie-Breaker**: If the NPC's highest skill is Light Armor, the determiner compares their Archery against Pickpocket and Lockpicking—classifying them as a **Ranger** if Archery is higher, or a **Rogue** otherwise.

---

## 4. Deterministic Temperament Matrix

When no JSON override is present, an NPC's **Temperament** is derived deterministically from the $8 \times 6$ intersection of their resolved **Social Class** (`Outcast = 0` .. `Rulers = 7`) and **Skill Type** (`Warrior = 0` .. `Orator = 5`) so every archetype combination produces a consistent personality:

| Social Class | Warrior (`0`) | Mage (`1`) | Rogue (`2`) | Craftsman (`3`) | Ranger (`4`) | Orator (`5`) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Outcast** (`0`) | Independent | Jealous | Jealous | Humble | Independent | Romantic |
| **Poverty** (`1`) | Humble | Romantic | Jealous | Independent | Proud | Romantic |
| **Working** (`2`) | Proud | Humble | Romantic | Independent | Independent | Proud |
| **Middle** (`3`) | Proud | Romantic | Independent | Romantic | Humble | Jealous |
| **Wealthy** (`4`) | Proud | Jealous | Romantic | Romantic | Independent | Jealous |
| **Religious** (`5`) | Independent | Humble | Romantic | Jealous | Humble | Proud |
| **Nobles** (`6`) | Jealous | Romantic | Independent | Proud | Humble | Proud |
| **Rulers** (`7`) | Proud | Independent | Humble | Romantic | Jealous | Independent |

