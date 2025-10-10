# 💑 M.A.R.A.S User Guide

> For chat-driven features and AI behavior with SkyrimNet, see [SkyrimNet Integration](SKYRIMNET.md).

## 📚 Glossary

- **💬 "chat"/"chatting"**: Using SkyrimNet's hotkey to open text input
- **🗣️ "talk"**: Using vanilla Skyrim dialogue system
- **🎤 "native voice lines"**: Vanilla Skyrim voiced marriage dialogue

### 🤖 SkyrimNet vs Non-SkyrimNet Users
- **Without SkyrimNet**: NPCs use original voices if available, otherwise text-only dialogue
- **With SkyrimNet**:
    - NPCs keep original voices if available
    - Others get AI-generated dialogue (voiced if your AI provider supports it)
    - Enable/disable in MCM: "Use AI generated misc dialogues"

## 🌟 Getting Started

When you install M.A.R.A.S, any NPC dialogue will include:
> "Ever think we could be something more?"

Selecting this registers the NPC with the mod (but doesn't marry you yet).

⚙️ **Gender Preference**: By default, this appears only for opposite gender NPCs
- Configurable in MCM: "Show start dialogue to genders" → Opposite / Same / All
- See [MCM Configuration](MCM.md) for all available settings

## 💍 Marriage Proposals

### 🎮 Without SkyrimNet
1. Talk to registered NPC
2. Select "Will you marry me?"
3. Instant acceptance (No requirements, no fuss.)

### 🤖 With SkyrimNet
1. Use SkyrimNet chat hotkey
2. Type your proposal naturally
3. AI evaluates:
   - 💕 Your relationship
   - 👑 Prestige & reputation
   - ⚔️ Guild affiliations
   - 📜 Past marriages/divorces

## 💝 Engagement Period

### Status Options
- 💔 Break off engagement: "We need to talk about... us. (Breakup)"
- 💒 Schedule wedding: Visit Maramal (vanilla quest)
- ⚠️ Wedding can be canceled if you:
  - 🏃‍♂️ Miss the ceremony
  - ⚔️ Start a fight
  - Other vanilla cancellation reasons
  - chat via SkyrimNet and your fiance decides to cancel it

### Jilted Status
- Triggered by:
  - Breaking engagement
  - Failed wedding
- Affects future marriage chances with SkyrimNet
- Locks npc's vanilla dialogue for 1 day

## 👰 Married Life

### Marriage Dialogue Options
> "Can we talk about our life together?"

#### 🍳 Food Request
> "Would you mind cooking something for me?"
- Uses **native voice lines**
- Gives you food (vanilla function)

#### 🏠 Housing Management
> "I want to talk about where we're living."

Options:
1. > "I'd like us to move somewhere else."
   - Choose any registered home
   - Visit once to register
   - Uses **native voice lines**
   - Custom sandbox package applies to this spouse. Attention: might break spouse's behavior required by quests or other functionality.

2. > "Is your home open to me as well?"
   - Starts home-sharing quest, allowing player to use their home(can give access for player to some places which player shouldn't get until some progress in playthorugh), it makes home's location public for player(can use containers, take items, etc...)
   - Grants bed ownership (if personally owned by spouse)

3. > "I won't be relying on your house for a while."
   - Removes access to spouse's home, so player won't have public access anymore


#### 💰 Store Income
> "Has the store made any money?"
- Once per day per spouse
- Uses **native voice lines**

#### 💔 Divorce Options
If using Skyrimnet, can be triggered by **chatting** with npc.

> "We need to talk about... us. (Breakup)"

Choices:
1. > "Forget it. That wasn't serious."
   - Cancels divorce

2. > "It's over. I'm sorry, but this is the end of us."
   - Confirms divorce
   - Uses **native voice lines**
   - 24-hour cooldown before new interaction

3. > "I made mistakes, and I regret them..."
   - Available for jilted status only
   - Not available after divorce
   - Uses **native voice lines**

## 👥 Unlocking Polygamy

### Requirements
- 🐉 Absorb at least one dragon soul
- 💑 Be married to one spouse

### Quest: "The Flame Divides Not"
1. 💤 Sleep in any bed to trigger
2. 🙏 Visit Maramal
3. Complete quest errands

### Dialogue System
- **Without SkyrimNet**: Pre-written text, without voice
- **With SkyrimNet**:
  - AI-generated dialogue
  - Optional voice (provider dependent)
  - 1-2 second generation delay
  - Toggle in MCM: "Use AI generated quest dialogues"

## ⚡ Spouse Bonuses
See [JSON config](./JSON-CONFIG.md#-buff-configuration-bonusesjson) for all available settings
### 💪 Bonus Types
1. **Profession Bonus**
   - Active when spouse is following
2. **Social Class Bonus**
   - Always active
   - Persists until divorce

### 📊 Multiple Spouse Scaling
- Single Spouse: 200% bonus strength
- Multiple Spouses:
  - 1st: 200% strength
  - 2nd: 50% strength
  - 3rd: 25% strength
  - 4th+: 10% strength each

### 👑 Social Class Bonuses (Permanent)
| Class     | Bonus                    |
|-----------|--------------------------|
| Rulers    | +5% Speech              |
| Nobles    | Store prices -5%        |
| Wealthy   | Sell prices +5%         |
| Religious | +3% Magic Resistance    |
| Working   | +30 Carry Weight        |
| Middle    | +2% Skill XP gain       |
| Poverty   | +10% Stamina Regen      |
| Outcast   | +10% Sneak Attack Damage|

### ⚔️ Profession Bonuses (Follower Only)
| Profession | Bonus                  |
|-----------|------------------------|
| Craftsman | -5% Damage Taken      |
| Mage      | +10% Magicka Regen    |
| Orator    | -5% Shout Cooldown    |
| Ranger    | +5% Ranged Damage     |
| Rogue     | +5% Sneak             |
| Warrior   | +5% Melee Damage      |
