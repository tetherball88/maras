# Marriage Difficulty Configuration Guide

This document explains all parameters in the `marriageDifficultyConfig.json` file, which controls the marriage proposal difficulty calculation system.

**File Location:** `Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json`

---

> **Related Documentation**
>
> - [Player Guide](PLAYER_GUIDE.md) – See section 3 "Proposing & Engagement" to understand how marriage chances work in gameplay
> - [Affection System – Deep Dive](AFFECTION_SYSTEM.md) – Learn how affection affects marriage difficulty (section 7)
> - [Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md) – Social class definitions referenced in this config
> - [MCM Settings Guide](MCM_Settings.md) – For in-game difficulty adjustments and cheats
> - [Advanced Configuration and Modding](ADVANCED_CONFIGURATION_AND_MODDING.md) – Other JSON configuration options (bonuses.json, spouse types)

## Overview

The marriage difficulty system calculates the success chance of a marriage proposal based on various factors including player prestige, NPC social class, guild affiliations, relationship status, and more. All values can be customized without recompiling the mod.

---

## Configuration Sections

### 1. Difficulty Calculation

Controls how the final difficulty score is converted to a success chance using a sigmoid curve.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `sigmoidSteepness` | 0.10 | Controls the steepness of the difficulty-to-chance conversion curve. Higher values (0.08-0.12) create steeper transitions. Lower = more gradual, Higher = sharper cutoff |
| `sigmoidCenter` | 50.0 | The difficulty value where success chance is exactly 50%. This is the inflection point of the sigmoid curve |
| `difficultyClampMin` | 0.0 | Minimum difficulty value (prevents negative difficulty) |
| `difficultyClampMax` | 100.0 | Maximum difficulty value (caps difficulty at this value) |

**How it works:** The sigmoid curve formula `1 / (1 + e^(steepness * (difficulty - center)))` converts difficulty to success chance, creating smooth transitions instead of harsh breakpoints.

---

### 2. Complexity

Base parameters for the difficulty calculation.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `initialComplexity` | 50.0 | Starting difficulty score before any modifiers are applied. This is the baseline difficulty |
| `levelDiffClampMin` | -10.0 | Minimum contribution from level difference (prevents excessive bonus from being higher level) |
| `levelDiffClampMax` | 10.0 | Maximum contribution from level difference (prevents excessive penalty from being lower level) |

---

### 3. Prestige

Player prestige affects how NPCs perceive your social standing. Higher social class NPCs require more prestige.

#### Prestige Calculation Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `dragonbornBonus` | 25.0 | Flat prestige bonus for completing the main quest (stage 90+) |
| `thaneHoldValue` | 4.0 | Prestige points per hold where you are Thane |
| `maxThaneHolds` | 9.0 | Maximum number of holds with Thane status (reference value) |
| `guildLeaderBonus` | 8.0 | Flat prestige bonus for being leader of Companions, College, or Thieves Guild |
| `prestigeClampMin` | 0.0 | Minimum total prestige value |
| `prestigeClampMax` | 100.0 | Maximum total prestige value |

#### Wealth Prestige

| Parameter | Default | Description |
|-----------|---------|-------------|
| `mostGoldDivisor` | 5000.0 | Gold amount divided by this to get prestige points (e.g., 10,000 gold / 5000 = 2 points) |
| `mostGoldClampMax` | 15.0 | Maximum prestige points from wealth alone |

#### Property Prestige

| Parameter | Default | Description |
|-----------|---------|-------------|
| `houseUnitMultiplier` | 3.0 | Prestige points per house owned |
| `horseUnitMultiplier` | 1.0 | Prestige points per horse owned |
| `houseHorseClampMax` | 15.0 | Maximum prestige points from houses and horses combined |

#### Achievement Prestige

| Parameter | Default | Description |
|-----------|---------|-------------|
| `questsMultiplier` | 0.2 | Prestige points per quest completed |
| `dungeonsMultiplier` | 0.1 | Prestige points per dungeon cleared |
| `soulsMultiplier` | 0.5 | Prestige points per dragon soul collected |
| `renownClampMax` | 25.0 | Maximum prestige points from all achievements combined |

#### Prestige Target Calculation

| Parameter | Default | Description |
|-----------|---------|-------------|
| `targetBase` | 10.0 | Base prestige requirement |
| `targetPerSocialIndexMultiplier` | 10.0 | Additional prestige required per social class level (0=outcast, 7=rulers) |
| `deltaMultiplier` | 0.30 | How much the prestige gap affects difficulty (target - player prestige) × this value |

**Example:** A noble (index 6) requires: 10 + (10 × 6) = 70 prestige. If you have 50 prestige, the gap is 20, contributing (20 × 0.30) = 6 to difficulty.

> For social class definitions (outcast, poverty, working, middle, wealthy, religious, nobles, rulers), see [Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md).

---

### 4. Penalties

Fixed difficulty increases for specific negative conditions.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `jiltedPenalty` | 10.0 | Difficulty increase if you previously jilted this NPC |
| `divorcedPenalty` | 20.0 | Difficulty increase if you previously divorced this NPC |
| `playerKillerPenalty` | 50.0 | Difficulty increase if you have killed spouse or fiance NPCs

---

### 5. Multipliers

These scale various factors that contribute to difficulty.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `marriedCountMultiplier` | 2.0 | Difficulty increase per existing spouse (polygamy penalty) |
| `divorcedCountMultiplier` | 1.0 | Difficulty increase per total divorces in your history |
| `levelDiffMultiplier` | 0.50 | Multiplier for NPC level - player level (clamped by levelDiffClampMin/Max) |
| `speechcraftMultiplier` | -0.1 | Multiplier for Speech skill (negative = easier with higher speech). At 100 Speech: -10 difficulty |
| `relationshipRankMultiplier` | -3.0 | Multiplier for relationship rank (-4 to +4). Friend (+1) gives -3 difficulty |
| `affectionMultiplier` | 0.50 | Multiplier for affection deviation from baseline (50). 70 affection: (70-50) × 0.5 = -10 difficulty |

> For details on how affection works, see [Affection System – Deep Dive](AFFECTION_SYSTEM.md).

---

### 6. Guilds

Guild membership affects difficulty based on NPC social class. Negative values make it easier (reduce difficulty).

#### Same Guild Bonus

| Parameter | Default | Description |
|-----------|---------|-------------|
| `sameGuildBonus` | -4.0 | Applied when both player and NPC are in the same guild (makes it easier) |

#### Guild-Specific Modifiers by Social Class

Each guild has modifiers for each social class. **Negative values reduce difficulty (easier), positive values increase difficulty (harder).**

**Companions Guild**
- Favor working class and higher class

| Social Class | Modifier | Interpretation |
|--------------|----------|----------------|
| `outcast` | 0.0 | Neutral |
| `poverty` | 0.0 | Neutral |
| `working` | -2.0 | Easier (working class appreciates warriors) |
| `middle` | -1.0 | Slightly easier |
| `wealthy` | 0.0 | Neutral |
| `religious` | 1.0 | Harder (religious distrust mercenaries) |
| `nobles` | -1.0 | Slightly easier (respect strength) |
| `rulers` | -2.0 | Easier (value military prowess) |

**Thieves Guild**
- Penalty with upper classes, favor lower classes
- Strongly disliked by religious NPCs

| Social Class | Modifier | Interpretation |
|--------------|----------|----------------|
| `outcast` | -2.0 | Easier (outcasts understand thieves) |
| `poverty` | -1.0 | Slightly easier |
| `middle` | 1.0 | Harder (respectable folk distrust thieves) |
| `working` | 0.0 | Neutral |
| `wealthy` | 2.0 | Harder (wealthy fear thieves) |
| `religious` | 2.0 | Harder (religious condemn thievery) |
| `nobles` | 2.0 | Harder (nobles look down on criminals) |
| `rulers` | 3.0 | Much harder (rulers despise criminals) |

**Dark Brotherhood**
- Strong penalties across most classes
- Extremely negative for religious and nobles

| Social Class | Modifier | Interpretation |
|--------------|----------|----------------|
| `outcast` | -3.0 | Easier (outcasts respect assassins) |
| `poverty` | -2.0 | Easier |
| `working` | 1.0 | Harder (working folk fear assassins) |
| `middle` | 2.0 | Harder |
| `wealthy` | 3.0 | Much harder |
| `religious` | 4.0 | Very much harder (religious abhor murder) |
| `nobles` | 3.0 | Much harder |
| `rulers` | 4.0 | Very much harder (rulers see assassins as threats) |

**College of Winterhold**
- Small penalties/bonuses overall
- Disliked by religious NPCs

| Social Class | Modifier | Interpretation |
|--------------|----------|----------------|
| `outcast` | 1.0 | Harder (outcasts distrust mages) |
| `poverty` | 0.0 | Neutral |
| `working` | 1.0 | Harder (working folk distrust magic) |
| `middle` | 0.0 | Neutral |
| `wealthy` | 1.0 | Harder |
| `religious` | -2.0 | Easier (some religious value magical knowledge) |
| `nobles` | 0.0 | Neutral |
| `rulers` | -3.0 | Easier (rulers value magical power) |

**Bards College**
- Generally minor effects
- Slight favor with middle class and nobles

| Social Class | Modifier | Interpretation |
|--------------|----------|----------------|
| `outcast` | 1.0 | Harder |
| `poverty` | 0.0 | Neutral |
| `working` | 0.0 | Neutral |
| `middle` | -1.0 | Slightly easier (middle class appreciates culture) |
| `wealthy` | -1.0 | Slightly easier |
| `religious` | 0.0 | Neutral |
| `nobles` | -2.0 | Easier (nobles value artistic pursuits) |
| `rulers` | -1.0 | Slightly easier |

---

## Tuning Tips

### Making Marriage Easier Overall
- Decrease `initialComplexity` (try 40.0)
- Decrease `sigmoidSteepness` (try 0.08)
- Decrease guild penalties (make negative values more negative)
- Decrease prestige `deltaMultiplier` (try 0.20)

### Making Marriage Harder Overall
- Increase `initialComplexity` (try 60.0)
- Increase `sigmoidSteepness` (try 0.12)
- Increase guild penalties (make positive values more positive)
- Increase prestige `deltaMultiplier` (try 0.40)

### Balancing Specific Factors

**To make Speech more powerful:**
- Increase magnitude of `speechcraftMultiplier` (try -0.15)

**To reduce polygamy penalty:**
- Decrease `marriedCountMultiplier` (try 1.0)

**To make affection more impactful:**
- Increase `affectionMultiplier` (try 0.75)

**To reduce prestige requirements:**
- Decrease `targetPerSocialIndexMultiplier` (try 7.0)
- Decrease `deltaMultiplier` (try 0.20)

---

## Example Calculation

**Scenario:** Player proposes to a noble NPC
- Player Level: 30
- NPC Level: 25
- Speech: 60
- Relationship Rank: 1 (Friend)
- Prestige: 45
- Affection: 65

**Calculation:**
1. Base: 50.0
2. Prestige target: 10 + (10 × 6) = 70
3. Prestige delta: (70 - 45) × 0.30 = 7.5
4. Level diff: (25 - 30) × 0.50 = -2.5 (clamped to -10, 10)
5. Speech: 60 × -0.1 = -6.0
6. Relationship: 1 × -3.0 = -3.0
7. Affection: (65 - 50) × 0.50 = -7.5
8. Guild effects: (varies)

**Total Difficulty:** 50 + 7.5 - 2.5 - 6.0 - 3.0 - 7.5 = 38.5
**Success Chance:** ~68% (via sigmoid curve)

---

## Notes

- The configuration is loaded once when the first marriage calculation occurs
- Changes to the JSON file require restarting the game to take effect
- Invalid JSON will cause the mod to log errors and fail to load configuration
- Missing parameters will be logged as warnings and default to 0.0
- All calculations are logged in detail when log level is set to DEBUG

> For other configuration files (bonuses.json, spouse type overrides), see [Advanced Configuration and Modding](ADVANCED_CONFIGURATION_AND_MODDING.md).

---

## Troubleshooting

**Config not loading?**
- Check the file path: `Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json`
- Validate JSON syntax using a JSON validator
- Check SKSE logs for error messages

**Values not affecting gameplay?**
- Restart the game after editing the config
- Ensure the parameter names match exactly (case-sensitive)
- Check that numerical values don't have quotes around them

**Want to reset to defaults?**
- Delete or rename the JSON file
- The mod will log errors but continue using the values shown in this document
