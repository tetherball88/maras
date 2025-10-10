# 📖 M.A.R.A.S MCM Guide

The M.A.R.A.S (Marry Anyone, Rule All Skyrim) mod comes with a comprehensive MCM (Mod Configuration Menu) that allows you to manage all aspects of your marriages and relationships in Skyrim. For general usage instructions, see the [User Guide](USER-GUIDE.md). For SkyrimNet-specific behavior and chat-driven features, see [SkyrimNet Integration](SKYRIMNET.md).

The menu is divided into three main sections:

## 🔍 Explore Page

The Explore page is your main hub for managing relationships with NPCs. It provides powerful search and filtering capabilities to help you find and manage your relationships.

### 🔎 Search Features
- **🔤 Search Box**: Search for NPCs by name (supports partial name matches)
- **⚡ Filter Options**:
  - **👥 Show all** (toggleable): Displays all NPCs regardless of relationship status
  - **💝 Show candidates** (toggleable): Shows NPCs who can potentially be married
  - **💍 Show fiancés** (toggleable): Displays NPCs you are currently engaged to
  - **👰 Show spouses** (toggleable): Lists your current spouses
  - **💔 Show jilted** (toggleable): Shows NPCs who have rejected your marriage proposal
  - **📜 Show divorced** (toggleable): Displays NPCs you were previously married to
  - **🧬 Show alive/deceased** (dropdown menu):
    - 🌟 All: Show both living and dead NPCs
    - 💫 Alive: Show only living NPCs
    - 🪦 Deceased: Show only deceased NPCs (former spouses who died)

Note: When "Show all" is enabled, other filters are automatically disabled. Disable "Show all" to use specific filters.

### NPC List
- Displays all NPCs registered in the mod that match your search criteria
- Click any NPC name to view their detailed page

### NPC Details Page
When you select an NPC, you'll see different options based on their relationship status. Each option is either editable or read-only:

- **Status** (read-only): Shows current relationship as `candidate`/`fiance`/`spouse`/`jilted`/`divorced`
- **Engagement Chance** (read-only, only for `candidate`/`jilted`/`divorced`): Displays your percentage chance of successful proposal
- **Social Class** (editable, for `all statuses`): Manually select their social class, affecting permanent buffs. It will override their default and/or social class from json overrides.
- **Skill Type** (editable, for `all statuses`): Manually choose their skill specialization, affecting follower buffs. It will override their default and/or skill type from json overrides.
- **Marriage Rank** (read-only, only for `spouse`): Shows spouse order (single, 1st, 2nd, etc.)
- **Player Home** (editable, only for `spouse`): Allows manually assign them to one of your houses
- **Share Their Home** (toggleable, `spouse`): Request access to their property. Will start quest to visit spouse's home.
    - It is available if spouse died and left you inheritence.
    - It isn't available if such quest is already running.
    - It isn't available if it turned out spouse doesn't have in-game home.
    - It isn't available if your killed your spouse.

## ⚙️ Settings Page

The Settings page allows you to customize how the mod behaves.

### 💒 Wedding Settings
- **⏭️ Skip Wedding Ceremonies**: Toggle to skip all wedding ceremonies after your first one

### 🗣️ AI Dialogue Settings
- **Gender Preferences**: Choose which genders to show start dialogue to
  - Options: opposite/same/all
- **AI-Generated Misc Dialogues**: Toggle AI-generated dialogue for miscellaneous topics
  - When enabled: Uses dynamically generated dialogue
  - When disabled: Uses predefined, unvoiced dialogue
- **AI-Generated Quest Dialogues**: Toggle AI-generated dialogue for quest conversations
  - When enabled: Uses dynamic AI dialogue
  - When disabled: Uses predefined dialogue without voice
- **ChatGPT Integration**: Toggle integration with ChatGPT for advanced AI interactions
  - When enabled: Access enhanced dialogue and quest features
  - When disabled: Use standard dialogue options

### ⚙️ Technical Settings
- **📊 Log Level**: Choose how detailed the mod's logging should be
  - Options: trace/debug/warning/error/none
  - Higher levels (trace, debug) show more technical details
  - Lower levels (warning, error) show only important issues
- **📁 Log Destination**: Choose where logs are output
  - Options: file/console/floating

### 💾 Data Management
- **Export Data**: Export all mod data to `Documents\My Games\Skyrim Special Edition\JCUser\MARAS\store.json`
- **Import Data**: Import mod data from the same location. You can export -> make some changes in json(in save part) -> import here -> save game. In this way your manual updates to json file will be stored in game save.
- **Clear Data**: Reset all mod data (with confirmation prompt)

> 💡 For advanced JSON configuration options, see the [JSON Configuration Guide](JSON-CONFIG.md)

## 💪 Spouse Buff Page

This page shows all active buffs you're receiving from your spouse(s).

### 🎯 Buffs Overview

- 📈 Shows current multiplier for each buff type:
  - 0.5x: Half value of buff
  - 1.0x: Full value
  - 2.0x: Double value
- ℹ️ Hover over each buff to see detailed effects:
    - 🎯 Bonus value
    - 📝 Detailed description
    - 📊 Unit of measurement
    - ✨ Current calculated value with multipliers

#### 🏃‍♂️ Follower Buffs
- 📋 Lists all potential follower-based buffs
- ⚔️ Buffs only active when spouse is following you
- 🎯 Based on spouse's `Skill Type`

### 🌟 Permanent Buffs
- 📋 Lists all permanent buffs from spouses
- ⭐ These buffs are always active
- 👑 Based on spouse's `Social Class`

📈 Buff Strength Scaling:
- 💑 Single spouse: 200% bonus strength
- 👥 Multiple spouses:
 - 👑 1st spouse: 200% bonus strength
 - 💫 2nd spouse: 50% bonus strength
 - ✨ 3rd spouse: 25% bonus strength
 - ⭐ 4th+ spouse: 10% bonus strength each
