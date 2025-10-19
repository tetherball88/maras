# 💍 M.A.R.A.S – Marry Anyone, Rule All Skyrim

> ⚠️ **Note**: While this mod mentions OStim integration, it contains no NSFW content.

## 📚 Documentation
- [User Guide](docs/USER-GUIDE.md) - Complete guide on how to use the mod
- [MCM Configuration](docs/MCM.md) - Detailed MCM menu options
- [JSON Configuration](docs/JSON-CONFIG.md) - Advanced configuration for mod authors
- [SkyrimNet Integration](docs/SKYRIMNET.md) - Features and AI/chat-driven behaviors when using SkyrimNet

Ever wanted to marry *literally anyone* in Skyrim? The Jarl? The blacksmith? That one suspicious guy in the woods?
With **M.A.R.A.S**, you can — and that's just the start! 🎉

This mod doesn't just unlock marriage. It reimagines it with:
- ✨ Personality-based spouse perks
- ❤️ A living affection + temperament system
- 🏠 Property sharing
- 💔 Divorce & remarriage
- 👥 Multiple spouses (yes, polyamory is now lore-friendly!)
- 🤖 Optional SkyrimNet AI Framework integration for NPCs who have a mind of their own (and might reject you 😏)

## 📦 Requirements

### Required
- [JContainers](https://www.nexusmods.com/skyrimspecialedition/mods/16495) - Core dependency, the whole mod is built around it
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) - Required for MCM functionality
- [Papyrus MessageBox - SKSE NG](https://www.nexusmods.com/skyrimspecialedition/mods/83578) - Required for dynamic message boxes
- [UIExtensions](https://www.nexusmods.com/skyrimspecialedition/mods/17561) - Required for player home selection interface
- [PapyrusUtil SE](https://www.nexusmods.com/skyrimspecialedition/mods/13048) - Required modders scripting utility functions

### Optional
- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) - Required only if you want to use AI features
- [Lover's Ledger](https://www.nexusmods.com/skyrimspecialedition/mods/158983) - Optional, enhances engagement chance calculations when using OStim. Don't install if you don't use SkyrimNet
- [Relations Finder](https://www.nexusmods.com/skyrimspecialedition/mods/158980) - Optional, adds complexity for NPCs who are in existing game relationships. Don't install if you don't use SkyrimNet

## ✨ Core Features

### 💘 Marry Anyone
Yes, technically anyone. (But do you really want to marry a random guard? Or a Forsworn? Maybe... we don't judge 😅)

### 📜 Native Skyrim Marriage Support
- Want to keep it traditional? Wear Mara's Amulet → propose → wedding ceremony → profit! You still get all M.A.R.A.S benefits after the wedding.
- Prefer the direct route? Pop the question through this mod and enjoy the same ceremony sequence.

### 🎭 Custom NPC Compatibility
- If a custom NPC works with vanilla marriage, no extra setup is needed
- If they have a custom questline, you can add them (and their quest) to the config for full support

### 🧠 Personality-Based Bonuses
Your spouse isn't just a pretty face — they come with Profession + Social Class perks that give you:
- **Permanent buffs**: Always active (even if spouse is off doing their own thing)
- **Follower buffs**: Active when your spouse follows you (requires separate follower mod)
- Fully customizable in `SKSE\Plugins\MARAS\bonuses.json`
- View your current buffs anytime in the MCM

### ❤️ Affection & Temperaments
- Every tracked NPC now rolls one of five temperaments (Proud, Humble, Jealous, Romantic, Independent) that colors how they react to you
- Spend time together, bring gifts, sleep in the same location, and keep promises to push affection toward happy tiers with stronger buffs
- Neglect them and affection fades—hierarchy perks weaken and, at estranged levels, divorce events can fire
- MCM pages surface temperament, current affection tier, and hierarchy rank so you always know where things stand

### 👥 Multiple Spouses (Optional)
Unlock through a special quest! Each spouse adds their bonuses — but with diminishing returns for balance:
- Single spouse = 200% bonus strength
- Multiple spouses
    - 1st spouse = 100%
    - 2nd spouse = 50%
    - 3rd spouse = 25%
    - 4th+ spouse = 10% each

Yes, you'd need 5-6 spouses to match one true love at full strength 😉

### 🏠 Property Sharing
- Ask spouse for access to their home (yes, you can technically own the Blue Palace this way)
- Ask them to move into your home — just step inside each house once for detection
- Reset their home anytime if you break their schedule or quest routine

### 💔 Divorce System
- Divorce any spouse easily
- Lose access to their home (unless another spouse lives there)
- Shouldn't break with most marriage mods (but treats only native marriages as "real" spouses)

## 🤖 SkyrimNet AI Integration

Want to make Skyrim marriages more unpredictable? SkyrimNet integration brings NPCs to life by letting them decide if they'll marry you — based on numerous factors!

### 🎯 Marriage Logic
- **Without SkyrimNet**: Marry anyone, no questions asked
- **With SkyrimNet**: NPCs consider multiple factors:

#### Acceptance Factors
- **Prestige**: Dragon souls, Thane status, guild leadership, wealth, quest completion
- **Guild Alignment**: Your affiliations affect acceptance chances
- **Relationship**: Quest completion, family help, opinion levels
- **Intimacy**: Integration with OStim and Lover's Ledger for relationship depth
- **History**: Current/past marriages affect future proposals
- **Skills**: Level and Speech skill influence success
- **Recent Interactions**: Current behavior impacts acceptance

### 🗣️ AI-Generated Dialogue
With SkyrimNet enabled:
- Unique proposal responses
- Dynamic home-sharing dialogue
- Personalized polyamory quest conversations
- Toggle AI dialogue in MCM (falls back to vanilla/generic responses)

This combination turns M.A.R.A.S + SkyrimNet into a dynamic relationship system where NPCs feel alive and responsive to your actions and status in Skyrim.

## 📱 MCM Features
- View all current spouses
- Monitor active buffs
- Configure AI options
- Manage marriage settings

---

*M.A.R.A.S - Making Skyrim's relationships as dynamic as its dragons!* 🐉💕

*M.A.R.A.S - Making Skyrim's relationships as dynamic as its dragons!* 🐉💕

## What's next

- Flesh out SM Event quests for the new affection tiers so happy/content spouses trigger bespoke errands instead of placeholder notifications.
- Verify and improve vanilla adoption compatibility: make sure the vanilla adoption system isn't broken and consider making it work the same way as vanilla marriage so adoptions behave consistently with existing marriage mechanics.
- Author temperament-aware AI dialogue variants so SkyrimNet actors comment on their emotional state and relationship tier.

```
