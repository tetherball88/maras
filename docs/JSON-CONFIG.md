# 📝 M.A.R.A.S JSON Configuration Guide

This guide explains how to configure and extend M.A.R.A.S using JSON files located in `SKSE\Plugins\MARAS` directory. For general usage, see the [User Guide](USER-GUIDE.md). For in-game configuration options, see the [MCM Guide](MCM.md). For SkyrimNet-specific integration and how AI/chat can use JSON data, see [SkyrimNet Integration](SKYRIMNET.md).

## 📂 Directory Structure
```
SKSE/
└── Plugins/
    └── MARAS/
        ├── quests/            # Custom marriage quest integration
        │   └── original.json  # Example file
        ├── spousesTypes/      # NPC type overrides
        │   └── overrides.json # Example file
        ├── bonuses.json       # Buff values configuration
        └── initialData.json   # Core mod data (do not modify)
```

## 🎯 Custom Quest Integration (`quests` folder)

Create JSON files here to integrate custom-modded NPCs with their own marriage quests. Follow the structure of `original.json`.

### Quest Integration Structure

Each quest is defined by its Form ID from a specific plugin using the format `__formData|PluginName.esm|0xFormID`.

### Available Actions

1. **📜 weddingFinish**
   - Triggers when wedding ceremony completes successfully (stage 100 in vanilla)
   - M.A.R.A.S uses this to officially register the NPC as a spouse
   - Hook this action to your quest stage for post-wedding customization

2. **❌ weddingCancel**
   - Triggers when wedding ceremony is canceled (stage 200 in vanilla)
   - M.A.R.A.S will mark the NPC as "jilted"
   - Use this to handle failed marriage scenarios

3. **🔄 marriageFinStart**
   - Called at the start of vanilla post-marriage quest
   - Internal M.A.R.A.S functionality for quest flow control
   - Typically used with the "start" stage identifier

4. **🛑 marriageMainStop**
   - Triggers when main marriage quest stops
   - M.A.R.A.S uses this to check for pending fiancés
   - Typically used with the "stop" stage identifier

### Example Structure:
```json
{
    "__formData|MyMod.esp|0xABC123": {
        "stages": {
            "100": {
                "action": "weddingFinish"
            },
            "200": {
                "action": "weddingCancel"
            }
        }
    },
    "__formData|MyMod.esp|0xABC124": {
        "stages": {
            "stop": {
                "action": "marriageMainStop"
            }
        }
    }
}
```

The structure maps quest stages to specific actions:
- Use numerical stages (like "100", "200") for regular quest stages
- Use "start" and "stop" for quest start/stop events
- Each quest can handle multiple stages and actions

## 👥 Spouse Types Configuration (`spousesTypes` folder)

Override default NPC social class and skill type assignments. Create new JSON files following the structure of `overrides.json`.

### FormID Key Format
- Format: `__formData|PluginName.esp|0xFormID`
- Example: `__formData|Skyrim.esm|0x00013BA`

### Available Values

#### 👑 Social Classes:
- `outcast`: Outcasts, criminals
- `poverty`: Poor, beggars
- `working`: Workers, farmers
- `middle`: Merchants, skilled workers
- `wealthy`: Rich merchants, property owners
- `religious`: Priests, temple workers
- `nobles`: Nobility
- `rulers`: Jarls, leaders

#### ⚔️ Skill Types:
- `warrior`: Combat specialists
- `mage`: Magic users
- `rogue`: Thieves, assassins
- `craftsman`: Blacksmiths, alchemists
- `ranger`: Hunters, scouts
- `orator`: Merchants, bards

### Example Structure:
```json
{
    "__metaInfo": {
        "typeName": "JFormMap"
    },
    "__formData|MyMod.esp|0x000ABC": {
        "comment": "Custom NPC Name",
        "social": "nobles",
        "skill": "mage"
    }
}
```

## ⚡ Buff Configuration (`bonuses.json`)

Configure spouse buff values while maintaining predefined perk effects for each social/skill type combination. For a list of default buff values and effects, see the [Spouse Bonuses section in the User Guide](USER-GUIDE.md#-spouse-bonuses).

### Value Types:
- `spell`: Modifies magic effect magnitude
- `multiply`: Multiplies perk entry value
- `add`: Adds to perk entry value

### Example Structure:
```json
{
  "nobles": {
    "permanent": {
      "speechcraft": {
        "type": "multiply",
        "value": 1.5
      }
    }
  },
  "warrior": {
    "follower": {
      "combat": {
        "type": "spell",
        "value": 50
      }
    }
  }
}
```

## ⚠️ Core Data (`initialData.json`)

This file contains essential mod data and FormIDs. **Do not modify** unless you absolutely know what you're doing.

## 💡 Tips for JSON Configuration

1. **Backup First**: Always backup your JSON files before making changes
2. **Test Thoroughly**: Test changes in a new save before applying to your main game
3. **Syntax Matters**: Ensure your JSON is valid - use a JSON validator if needed
4. **FormIDs**: Always use the correct format for FormIDs, including leading zeros
5. **Compatibility**: Consider load order when referencing FormIDs from different plugins

## 🔍 Default Logic Override

The mod's default logic for NPC classification:

### Social Class Detection:
- Checks NPC factions
- Evaluates clothing/equipment quality
- Considers NPC location and role

### Skill Type Detection:
- Examines NPC class
- Evaluates skill levels
- Considers combat style and spells

You can override these automatic assignments using the `spousesTypes` JSON files.
