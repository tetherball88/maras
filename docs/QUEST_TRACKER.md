# MARAS Quest Event System

This folder contains JSON configuration files that define automated actions to execute when specific quest events occur.

## Overview

The quest event system allows you to automatically trigger MARAS commands (like promoting NPCs to relationship statuses or changing affection) when:
- A quest starts
- A quest stops/completes
- A quest reaches a specific stage

## File Format

JSON files in this folder should follow this structure:

```json
{
  "__formData|PluginName.esp|0xFormID": {
    "comment": "Optional description",
    "onStart": [
      "commandType:npcSpecifier:argument",
      "commandType:npcSpecifier:argument"
    ],
    "onStop": [
      "commandType:npcSpecifier:argument"
    ],
    "onStageChange:10": [
      "commandType:npcSpecifier:argument"
    ],
    "onStageChange:20": [
      "commandType:npcSpecifier:argument"
    ]
  }
}
```

### Quest Identification

Quests are identified using the `__formData|PluginName|0xFormID` format:
- `PluginName`: The plugin file containing the quest (e.g., `Skyrim.esm`, `YourMod.esp`)
- `0xFormID`: The local form ID in hexadecimal (e.g., `0x00021382`)

**How to find a quest's FormID:**
1. Open the Creation Kit or xEdit
2. Find your quest
3. Note the FormID (example: `00021382` for Book of Love quest in Skyrim.esm)
4. Use format: `__formData|Skyrim.esm|0x00021382`

## Available Events

### `onStart`
Executes commands when the quest starts (first time).

```json
"onStart": [
  "promoteToStatus:LoveInterest:Candidate"
]
```

### `onStop`
Executes commands when the quest stops or completes.

```json
"onStop": [
  "changeAffection:Spouse:10"
]
```

### `onStageChange:X`
Executes commands when the quest reaches stage X (replace X with the stage number).

```json
"onStageChange:50": [
  "promoteToStatus:LoveInterest:Engaged",
  "setAffection:LoveInterest:75"
]
```

## Command Format

All commands follow the format: `commandType:npcSpecifier:argument`

- **commandType**: The command to execute (see below)
- **npcSpecifier**: How to identify the target NPC (see "NPC Specification" section)
- **argument**: The command-specific argument

**Alternative Object Format:**
```json
{
  "type": "commandType",
  "npc": "npcSpecifier",
  "arg": "argument"
}
```

## NPC Specification

The `npcSpecifier` can be either:

1. **Alias Name**: The name of a quest alias (e.g., `LoveInterest`, `Spouse`, `Target`)
2. **Form Key**: Direct NPC reference using `__formData|PluginName|0xFormID` format

**Examples:**
```json
"promoteToStatus:LoveInterest:Married"
"setAffection:__formData|Skyrim.esm|0x00013BAB:100"
```

## Available Commands

### Command Summary

**Relationship & Affection:**
- `promoteToStatus` - Change NPC relationship status
- `setAffection` - Set permanent affection to absolute value
- `changeAffection` - Modify affection by delta
- `addDailyAffection` - Add to daily affection accumulator

**NPC Attributes:**
- `setSocialClass` - Set NPC's social class
- `setSkillType` - Set NPC's skill type/profession
- `setTemperament` - Set NPC's temperament/personality

---

### `promoteToStatus:npcSpecifier:statusKeyword`
Promotes the specified NPC to a specific relationship status.

**Available statuses:**
- `Candidate` - Potential romantic interest
- `Engaged` - Engaged to player
- `Married` - Married to player
- `Divorced` - Divorced from player
- `Jilted` - Rejected by player

**Examples:**
```json
"onStageChange:100": [
  "promoteToStatus:LoveInterest:Married"
]

"onStart": [
  "promoteToStatus:__formData|MyMod.esp|0x001234:Candidate"
]
```

### `setAffection:npcSpecifier:value`
Sets the NPC's permanent affection to a specific value (0-100).

**Examples:**
```json
"onStart": [
  "setAffection:LoveInterest:50"
]

"onStageChange:20": [
  "setAffection:Spouse:100"
]
```

### `changeAffection:npcSpecifier:delta`
Changes the NPC's affection by a delta amount (positive or negative).

**Examples:**
```json
"onStageChange:10": [
  "changeAffection:LoveInterest:15"
]

"onStageChange:20": [
  "changeAffection:LoveInterest:-10"
]
```

### `addDailyAffection:npcSpecifier:amount`
Adds to the NPC's daily affection accumulator. Daily affection is automatically applied to permanent affection at day boundaries, providing gradual bonding over time.

**Examples:**
```json
"onStageChange:10": [
  "addDailyAffection:LoveInterest:5"
]

"onStart": [
  "addDailyAffection:Spouse:10"
]
```

### `setSocialClass:npcSpecifier:className`
Sets the NPC's social class, which affects marriage compatibility and gameplay mechanics.

**Available classes:**
- `Outcast` - Social outcasts
- `Poverty` - Poor/destitute
- `Working` - Working class
- `Middle` - Middle class
- `Wealthy` - Wealthy citizens
- `Religious` - Religious figures
- `Nobles` - Nobility
- `Rulers` - Rulers/leaders

**Examples:**
```json
"onStart": [
  "setSocialClass:LoveInterest:Wealthy"
]

"onStageChange:50": [
  "setSocialClass:Target:Nobles"
]
```

### `setSkillType:npcSpecifier:skillName`
Sets the NPC's skill type/profession, which affects compatibility and dialogue options.

**Available skill types:**
- `Warrior` - Combat-focused
- `Mage` - Magic-focused
- `Rogue` - Stealth/subterfuge
- `Craftsman` - Crafting/smithing
- `Ranger` - Hunting/archery
- `Orator` - Speech/persuasion

**Examples:**
```json
"onStart": [
  "setSkillType:LoveInterest:Mage"
]

"onStageChange:20": [
  "setSkillType:Spouse:Craftsman"
]
```

### `setTemperament:npcSpecifier:temperamentName`
Sets the NPC's temperament/personality, which affects relationship dynamics.

**Available temperaments:**
- `Proud` - Prideful/arrogant
- `Humble` - Modest/humble
- `Jealous` - Jealous/possessive
- `Romantic` - Romantic/affectionate
- `Independent` - Independent/self-reliant

**Examples:**
```json
"onStart": [
  "setTemperament:LoveInterest:Romantic"
]

"onStageChange:50": [
  "setTemperament:Spouse:Proud"
]
```

## NPC Resolution

The system resolves NPCs based on the `npcSpecifier` parameter:

### By Alias Name
If the specifier doesn't start with `__formData|`, it's treated as a quest alias name:
- The system searches the quest's aliases for a matching name
- The alias must be a Reference Alias pointing to an Actor

**Common alias names:** `LoveInterest`, `Spouse`, `Target`, `NPC`, `Actor`

### By Form Key
If the specifier starts with `__formData|`, it's parsed as a direct form reference:
- Format: `__formData|PluginName|0xFormID`
- The system resolves the form and verifies it's an Actor
- This allows targeting specific NPCs regardless of quest aliases

**Supports both reference IDs and base actor IDs:**
- **Reference ID**: Direct actor instance in the world (e.g., a placed NPC)
- **Base Actor ID**: Actor template (TESNPC) - system will find an active reference of this actor in the world

**Examples:**
```json
"promoteToStatus:__formData|Skyrim.esm|0x00013BAB:Married"
"setAffection:__formData|MyMod.esp|0x000D62:75"
```

**How it works:**
1. If the FormID points to an Actor reference → uses it directly
2. If the FormID points to a base actor (TESNPC) → searches all process lists for an active reference:
   - Searches high-priority actors first (nearby, important NPCs)
   - Then middle-high priority actors
   - Then middle-low priority actors
   - Finally low-priority actors
3. If no active reference is found → logs an error

**Recommendation**: Use alias names for flexibility, form keys for specific NPCs. Base actor IDs work well for unique NPCs, but the NPC must be loaded in the world when the quest event fires.

## Example Configurations

### Simple Courtship Quest
```json
"__formData|MyMod.esp|0x001000": {
  "comment": "Courtship quest for Lydia",
  "onStart": [
    "promoteToStatus:LoveInterest:Candidate",
    "setAffection:LoveInterest:25",
    "setSocialClass:LoveInterest:Middle",
    "setSkillType:LoveInterest:Warrior",
    "setTemperament:LoveInterest:Proud"
  ],
  "onStageChange:50": [
    "changeAffection:LoveInterest:25",
    "promoteToStatus:LoveInterest:Engaged"
  ],
  "onStop": [
    "promoteToStatus:LoveInterest:Married",
    "setAffection:LoveInterest:100"
  ]
}
```

### Multi-Stage Romance with Gradual Bonding
```json
"__formData|MyMod.esp|0x002000": {
  "comment": "Complex romance quest with gradual affection growth",
  "onStart": [
    "promoteToStatus:LoveInterest:Candidate",
    "setAffection:LoveInterest:10",
    "setSocialClass:LoveInterest:Wealthy",
    "setSkillType:LoveInterest:Mage",
    "setTemperament:LoveInterest:Romantic"
  ],
  "onStageChange:10": [
    "addDailyAffection:LoveInterest:5"
  ],
  "onStageChange:20": [
    "addDailyAffection:LoveInterest:10",
    "changeAffection:LoveInterest:15"
  ],
  "onStageChange:30": [
    "promoteToStatus:LoveInterest:Engaged",
    "addDailyAffection:LoveInterest:15"
  ],
  "onStageChange:40": [
    "promoteToStatus:LoveInterest:Married",
    "setAffection:LoveInterest:100"
  ]
}
```

### Failed Romance (Rejection)
```json
"__formData|MyMod.esp|0x003000": {
  "comment": "Handles rejection scenario",
  "onStageChange:50": [
    "promoteToStatus:Target:Jilted",
    "setAffection:Target:0"
  ]
}
```

### Using Form Keys (Direct NPC Reference)
```json
"__formData|MyMod.esp|0x004000": {
  "comment": "Quest affecting a specific NPC by reference FormID",
  "onStart": [
    "promoteToStatus:__formData|Skyrim.esm|0x00013BAB:Candidate"
  ],
  "onStageChange:100": [
    "setAffection:__formData|Skyrim.esm|0x00013BAB:75"
  ]
}
```

### Using Base Actor IDs
```json
"__formData|MyMod.esp|0x004100": {
  "comment": "Quest affecting NPC by base actor ID (works for unique NPCs)",
  "onStart": [
    "promoteToStatus:__formData|Skyrim.esm|0x00013BAB:Candidate",
    "setSocialClass:__formData|Skyrim.esm|0x00013BAB:Nobles"
  ],
  "onStageChange:50": [
    "changeAffection:__formData|Skyrim.esm|0x00013BAB:25"
  ],
  "onStop": [
    "promoteToStatus:__formData|Skyrim.esm|0x00013BAB:Married",
    "setAffection:__formData|Skyrim.esm|0x00013BAB:100"
  ]
}
```

### Mixed Approach (Multiple NPCs in One Quest)
```json
"__formData|MyMod.esp|0x005000": {
  "comment": "Quest with multiple romance interests",
  "onStart": [
    "promoteToStatus:LoveInterest1:Candidate",
    "promoteToStatus:LoveInterest2:Candidate"
  ],
  "onStageChange:50": [
    "changeAffection:LoveInterest1:25"
  ],
  "onStageChange:60": [
    "changeAffection:LoveInterest2:25"
  ],
  "onStageChange:100": [
    "promoteToStatus:LoveInterest1:Married",
    "promoteToStatus:LoveInterest2:Jilted"
  ]
}
```

### Advanced Quest with Full NPC Configuration
```json
"__formData|MyMod.esp|0x006000": {
  "comment": "Comprehensive courtship quest demonstrating all attribute management",
  "onStart": [
    "promoteToStatus:LoveInterest:Candidate",
    "setSocialClass:LoveInterest:Nobles",
    "setSkillType:LoveInterest:Orator",
    "setTemperament:LoveInterest:Romantic",
    "setAffection:LoveInterest:15"
  ],
  "onStageChange:10": [
    "addDailyAffection:LoveInterest:5"
  ],
  "onStageChange:25": [
    "changeAffection:LoveInterest:10",
    "addDailyAffection:LoveInterest:8"
  ],
  "onStageChange:50": [
    "promoteToStatus:LoveInterest:Engaged",
    "setTemperament:LoveInterest:Proud",
    "changeAffection:LoveInterest:20"
  ],
  "onStageChange:75": [
    "addDailyAffection:LoveInterest:10",
    "changeAffection:LoveInterest:15"
  ],
  "onStageChange:100": [
    "promoteToStatus:LoveInterest:Married",
    "setAffection:LoveInterest:100",
    "setTemperament:LoveInterest:Humble"
  ]
}
```

## Troubleshooting

### Commands not executing?
1. Check the MARAS log file (`Documents/My Games/Skyrim Special Edition/SKSE/MARAS.log`)
2. Verify the quest FormID is correct
3. Verify the command format: `commandType:npcSpecifier:argument`
4. Check that commands are spelled correctly (case-sensitive!)
5. Ensure the NPC specifier is correct (alias name or form key)

### Quest not found?
- Make sure the plugin name exactly matches (including `.esp` or `.esm`)
- Verify the FormID is in hexadecimal with `0x` prefix
- The mod containing the quest must be loaded in your game

### NPC not found?
When using alias names:
- Ensure your quest has an alias with the exact name you specified
- Check that the alias is a Reference Alias (not Location Alias)
- Verify the alias points to an Actor

When using form keys:
- Verify the plugin name and FormID are correct
- Ensure the form is an Actor reference or a base actor (TESNPC)
- Check that the plugin is loaded in your game
- **For base actor IDs**: The NPC must have an active reference in the world (must be loaded/spawned)
  - Unique NPCs like followers usually work fine
  - Generic NPCs might not be loaded when the quest event fires
  - Check MARAS.log for "no active reference found" errors

## Best Practices

1. **Use Comments**: Add `"comment"` fields to document what each quest does
2. **Alias Names Over Form Keys**: Use alias names for flexibility; they work regardless of load order
3. **Consistent Naming**: Use standard alias names (`LoveInterest`, `Spouse`) for consistency
4. **Form Keys for Specifics**: Use form keys when you need to target a specific NPC regardless of quest state
5. **Test Incrementally**: Start with simple commands, test, then add complexity
6. **Backup Configs**: Keep backups of working configurations
7. **Check Logs**: Always review MARAS.log after testing
8. **Meaningful Stages**: Align your stage changes with meaningful quest moments
9. **Validate Format**: Ensure all commands follow `commandType:npcSpecifier:argument` format

## Technical Notes

- Configuration files are loaded when the game starts (after data loaded event)
- Changes to JSON files require restarting the game to take effect
- All files with `.json` extension in this folder are automatically loaded
- Invalid configurations are logged but won't crash the game
- Commands execute in the order they appear in the array
