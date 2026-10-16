# Marriage Difficulty Configuration Guide

This document explains all parameters in `Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json`, which governs the non-AI marriage proposal difficulty calculation in [MarriageDifficulty.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MarriageDifficulty.cpp).

> [!NOTE]
> **SkyrimNet vs. Standard Dialogue Proposals**
> This configuration applies when proposing marriage through standard M.A.R.A.S dialogue options (or when SkyrimNet is not used). When proposing organically through **SkyrimNet** AI conversation, the NPC's LLM persona and prompt context (`SkyrimNetIntegration`) determine whether they accept or decline.
> You can also bypass the difficulty roll entirely per save by enabling **Always Succeed Proposals** in the **Prisma UI Dashboard $\rightarrow$ Settings** tab.

---

## Related Documentation

- [MarriageDifficulty Service Mechanics](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/MarriageDifficulty.md) — Architectural breakdown of the proposal complexity and prestige pipeline.
- [AffectionService Mechanics](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/AffectionService.md) — How permanent affection is accumulated and affects proposal difficulty.
- [Archetypes & Buffs](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/ARCHETYPES_AND_BUFFS.md) — Social class definitions referenced in this config.
- [Advanced Configuration and Modding](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/ADVANCED_CONFIGURATION_AND_MODDING.md) — Overview of all external JSON/INI files (`bonuses.json`, `spousesTypes/`, `questEvents/`).
- [User Guide](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/player/USER_GUIDE.md) — Player-facing guide to courting, proposals, and the Prisma UI Dashboard.

---

## Overview

When the player proposes to an NPC, M.A.R.A.S computes a **Proposal Complexity** score starting from `initialComplexity`. Positive modifiers make the proposal harder; negative modifiers (bonuses) make the proposal easier. The final clamped difficulty is converted into a `[0.0, 1.0]` acceptance probability using a logistic sigmoid curve.

`Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json` is automatically reloaded **every time you load a save or start a new game** (`kPostLoadGame` / `kNewGame`), so you can tweak values and reload your save without restarting Skyrim.

---

## Configuration Sections

### 1. Difficulty Calculation (`difficultyCalculation`)

Controls how the final complexity score is clamped and converted to a success probability (`0.0` to `1.0`) using a sigmoid curve:
$$\text{SuccessChance} = \frac{1}{1 + e^{\text{sigmoidSteepness} \times (\text{difficulty} - \text{sigmoidCenter})}}$$

| Parameter | Default | Description |
| :--- | :---: | :--- |
| `sigmoidSteepness` | `0.10` | Steepness of the difficulty-to-probability S-curve. Higher values (`0.12`) create a sharper cutoff around the center; lower values (`0.08`) create a more gradual transition. |
| `sigmoidCenter` | `50.0` | The difficulty score that yields an exact **50%** acceptance chance (inflection point). |
| `difficultyClampMin` | `0.0` | Minimum clamped difficulty before sigmoid evaluation. |
| `difficultyClampMax` | `100.0` | Maximum clamped difficulty before sigmoid evaluation. |

---

### 2. Base Complexity & Level Clamping (`complexity`)

| Parameter | Default | Description |
| :--- | :---: | :--- |
| `initialComplexity` | `50.0` | Starting complexity score before any modifiers are applied. |
| `levelDiffClampMin` | `-10.0` | Minimum complexity contribution from `(npcLevel - playerLevel) * levelDiffMultiplier` (caps the bonus from out-leveling an NPC). |
| `levelDiffClampMax` | `10.0` | Maximum complexity contribution from `(npcLevel - playerLevel) * levelDiffMultiplier` (caps the penalty when proposing to a much higher-level NPC). |

---

### 3. Player Prestige vs. Social Class Target (`prestige`)

Higher-status NPCs expect higher personal renown and wealth from the player.

#### 3.1 Player Prestige Sources
Player prestige is summed from four categories and clamped to `[prestigeClampMin, prestigeClampMax]`:

| Parameter | Default | Description |
| :--- | :---: | :--- |
| `dragonbornBonus` | `25.0` | Flat prestige bonus once main quest `MQ104` reaches stage $\ge 90$ (recognized as Dragonborn). |
| `thaneHoldValue` | `4.0` | Prestige points per hold where the player is Thane (quest stage $\ge 25$ across all 9 hold favor quests). |
| `maxThaneHolds` | `9.0` | Maximum number of holds counted for Thane prestige. |
| `guildLeaderBonus` | `8.0` | Flat prestige bonus if the player is leader of the Companions (`C06` $\ge 200$), College of Winterhold (`MG08` $\ge 200$), or Thieves Guild (`TGLeadership` $\ge 40$). |
| `mostGoldDivisor` | `5000.0` | Divides the engine's *"Most Gold Carried"* misc stat to compute wealth prestige (`mostGold / mostGoldDivisor`). |
| `mostGoldClampMax` | `15.0` | Maximum prestige points granted from peak gold carried. |
| `houseUnitMultiplier` | `3.0` | Prestige points per house owned (*"Houses Owned"* misc stat). |
| `horseUnitMultiplier` | `1.0` | Prestige points per horse owned (*"Horses Owned"* misc stat). |
| `houseHorseClampMax` | `15.0` | Maximum combined prestige points from houses and horses. |
| `questsMultiplier` | `0.2` | Prestige points per quest completed. |
| `dungeonsMultiplier` | `0.1` | Prestige points per dungeon cleared. |
| `soulsMultiplier` | `0.5` | Prestige points per dragon soul collected. |
| `renownClampMax` | `25.0` | Maximum combined prestige points from quests, dungeons, and dragon souls. |
| `prestigeClampMin` | `0.0` | Minimum total player prestige score. |
| `prestigeClampMax` | `100.0` | Maximum total player prestige score. |

#### 3.2 Social Class Target & Prestige Delta
Each NPC has a `SocialClass` index (`0` = `Outcast`, `1` = `Poverty`, `2` = `Working`, `3` = `Middle`, `4` = `Wealthy`, `5` = `Religious`, `6` = `Nobles`, `7` = `Rulers`):
$$\text{TargetPrestige} = \text{targetBase} + (\text{targetPerSocialIndexMultiplier} \times \text{SocialClassIndex})$$
$$\text{PrestigeComplexityMod} = (\text{TargetPrestige} - \text{PlayerPrestige}) \times \text{deltaMultiplier}$$

| Parameter | Default | Description |
| :--- | :---: | :--- |
| `targetBase` | `10.0` | Base prestige expected even by index `0` (`Outcast`). |
| `targetPerSocialIndexMultiplier` | `10.0` | Additional prestige expected per social class index (`0..7`). |
| `deltaMultiplier` | `0.30` | Multiplier applied to `(TargetPrestige - PlayerPrestige)`. Exceeding the target reduces complexity (bonus); falling short increases complexity (penalty). |

---

### 4. Courting Bonuses & History Penalties (`penalties`)

#### 4.1 Courting Progression Bonuses (Built-In)
Building a relationship through M.A.R.A.S romantic stages before proposing grants major complexity reductions:
- **Currently Dating (`IsDating`)**: Reduces complexity by **`-15.0`** (`datingBonus`).
- **Currently Lovers (`IsLover`)**: Reduces complexity by **`-30.0`** (`loverBonus`).
- **Intimacy Adjustment**: Any intimacy bonus passed from recent intimate scenes is also applied directly to complexity.

#### 4.2 Negative History Penalties (`penalties` JSON object)

| Parameter | Default | Description |
| :--- | :---: | :--- |
| `jiltedPenalty` | `10.0` | Complexity increase if you previously broke off an engagement with this specific NPC. |
| `divorcedPenalty` | `20.0` | Complexity increase if you previously divorced this specific NPC. |
| `playerKillerPenalty` | `50.0` | Complexity increase if you have murdered a romantic partner (`playerKiller`). |

---

### 5. Scaling Multipliers (`multipliers`)

| Parameter | Default | Description |
| :--- | :---: | :--- |
| `marriedCountMultiplier` | `2.0` | Complexity added per currently married spouse (`CountMarried() * marriedCountMultiplier`). |
| `divorcedCountMultiplier` | `1.0` | Complexity added per total divorce in your history (`CountDivorced() * divorcedCountMultiplier`). |
| `levelDiffMultiplier` | `0.50` | Multiplier for `(npcLevel - playerLevel)`, clamped to `[levelDiffClampMin, levelDiffClampMax]`. |
| `speechcraftMultiplier` | `-0.1` | Multiplier for player Speech skill (at 100 Speech: `-10.0` complexity). |
| `relationshipRankMultiplier` | `-3.0` | Multiplier for vanilla `BGSRelationship` rank (`-4` to `+4`). |
| `affectionMultiplier` | `0.50` | Scales permanent affection deviation from the neutral baseline of `50`: `complexity -= (permanentAffection - 50.0) * affectionMultiplier`. |

---

### 6. Guild Alignment (`guilds`)

Evaluates five factions: **The Companions** (`companions`), **Thieves Guild** (`thieves`), **Dark Brotherhood** (`brotherhood`), **College of Winterhold** (`college`), and **Bards College** (`bards`, detected via `MS05` stage $\ge 300$).

#### How Multi-Guild Membership is Evaluated
1. **Shared Guild Bonus (`sameGuildBonus`, default `-4.0`)**: Applied for **every** guild that both the player and the target NPC belong to.
2. **Social Class Perception**: For every guild the player belongs to, M.A.R.A.S looks up the modifier for the NPC's social class. If the player belongs to multiple guilds, M.A.R.A.S tracks the single strongest positive modifier (hardest penalty) and the single strongest negative modifier (best bonus) across those guilds, and applies whichever has the **larger absolute magnitude**.

#### Default Guild Social Class Table
*(Negative values reduce complexity = easier proposal; positive values increase complexity = harder proposal.)*

| Social Class | `companions` | `thieves` | `brotherhood` | `college` | `bards` |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`outcast`** (`0`) | `0.0` | `-2.0` | `-3.0` | `+1.0` | `+1.0` |
| **`poverty`** (`1`) | `0.0` | `-1.0` | `-2.0` | `0.0` | `0.0` |
| **`working`** (`2`) | `-2.0` | `0.0` | `+1.0` | `+1.0` | `0.0` |
| **`middle`** (`3`) | `-1.0` | `+1.0` | `+2.0` | `0.0` | `-1.0` |
| **`wealthy`** (`4`) | `0.0` | `+2.0` | `+3.0` | `+1.0` | `-1.0` |
| **`religious`** (`5`) | `+1.0` | `+2.0` | `+4.0` | `-2.0` | `0.0` |
| **`nobles`** (`6`) | `-1.0` | `+2.0` | `+3.0` | `0.0` | `-2.0` |
| **`rulers`** (`7`) | `-2.0` | `+3.0` | `+4.0` | `-3.0` | `-1.0` |

---

## Example Calculation

**Scenario:** Player proposes to a **Nobles** (`index = 6`) NPC while **Dating** them:
- **Player Level**: `30`, **NPC Level**: `25`
- **Speech**: `60`, **Vanilla Relationship Rank**: `+1` (`Friend`)
- **Player Prestige**: `45`, **Permanent Affection**: `65`
- **Relationship Status**: `Dating` (`-15.0` bonus)

**Step-by-Step Complexity:**
1. **Base Complexity**: `50.0`
2. **Prestige Delta**: Target is $10 + (10 \times 6) = 70$. Gap is $(70 - 45) \times 0.30 = +7.5$
3. **Dating Bonus**: `-15.0`
4. **Level Difference**: $(25 - 30) \times 0.50 = -2.5$
5. **Speechcraft**: $60 \times (-0.1) = -6.0$
6. **Vanilla Relationship Rank**: $1 \times (-3.0) = -3.0$
7. **Permanent Affection**: $-((65 - 50) \times 0.50) = -7.5$

**Total Difficulty:** $50.0 + 7.5 - 15.0 - 2.5 - 6.0 - 3.0 - 7.5 = 23.5$
**Success Chance:** $\frac{1}{1 + e^{0.10 \times (23.5 - 50.0)}} \approx \mathbf{93.4\%}$

---

## 7. Papyrus Dialogue Globals & Tier Multipliers (`TTM_Data.SetGameNoAIChancesGlobals`)

When standard (non-SkyrimNet) dialogue opens with a tracked NPC who is not yet `engaged` or `married`, [`TTM_Data.SetGameNoAIChancesGlobals`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Data.psc#L111-L156) takes the C++ `CalculateMarriageSuccessChance` output (scaled to `0–100`), applies relationship-state modifiers, and writes the resulting values to three `TT_MARAS.esp` `GlobalVariable` records used by Creation Kit dialogue conditions:

- **`GetLoverChanceGV()` (`0x5D1`)**: Lover proposal chance global.
- **`GetDatingChanceGV()` (`0x5D0`)**: Dating proposal chance global.
- **`GetMarriageChanceGV()` (`0x15C`)**: Engagement/marriage proposal chance global.

### Post-Calculation Modifiers Applied in Papyrus
1. **Cheating Check (`0.6x`)**: If either the NPC or the Player has `Rank 0` in `TTM_CanFormNonCheatingMain` (`0x5D2`, indicating a closed main-tier relationship where a new romance requires cheating), base `chance` is multiplied by `0.6`.
2. **Prior Breakup Check (`0.75x`)**: If the NPC is currently an `exlover`, `exdating`, `jilted`, or `divorced`, `chance` is multiplied by `0.75`.
3. **Base Tier Multipliers**:
   - `loverChance = chance * 0.7`
   - `datingChance = chance * 0.85`
   - `marriageChance = chance`
4. **Current Romantic Stage Multipliers**:
   - If the NPC is already a **`lover`**: `datingChance *= 1.5` (`lover` $\rightarrow$ `dating`) and `marriageChance *= 1.2` (`lover` $\rightarrow$ `engaged`).
   - If the NPC is already **`dating`**: `marriageChance *= 1.5` (`dating` $\rightarrow$ `engaged`).

---

## Troubleshooting & Logging

- **Live Reloading**: `marriageDifficultyConfig.json` is reloaded on every save load and new game (`MarriageDifficulty::LoadConfig()`). You do not need to exit to desktop after editing the file—simply reload your save.
- **Detailed Breakdown in Logs**: Set `logLevel` to `Debug` (`1`) or `Trace` (`0`) in the Prisma UI Dashboard Settings (or `MARAS.ini`) to see the exact microsecond breakdown of every factor (`Target prestige`, `Player prestige`, `Married count`, `Speechcraft`, `Guild alignment`, `Permanent affection`, `Final complexity`, and `chance`) in `MARAS.log`.

