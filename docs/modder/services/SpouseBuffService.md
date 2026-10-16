# `SpouseBuffService` & `BonusesService` — Features & Behavioral Guide

**Sources**:
- [`SpouseBuffService.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SpouseBuffService.cpp)
- [`BonusesService.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BonusesService.cpp)

---

## 1. What These Services Do

`BonusesService` loads the customizable buff definitions from `SKSE/Plugins/MARAS/bonuses.json`, while `SpouseBuffService` calculates the live strength of every passive **Social Class** and **Skill Type** bonus granted to the player by their spouses, scaling them dynamically by monogamy vs. polygamy rank, partner affection, and follower presence.

---

## 2. Core Features & Mechanics

### 2.1 Two Distinct Categories of Spouse Buffs
M.A.R.A.S divides spouse bonuses into two gameplay loops:
1. **Permanent Household Buffs (Based on Social Class)**:
   - Granted by **all married spouses**, regardless of whether they are waiting at home or traveling with you.
   - Reflects the socioeconomic benefits of your household (*Outcast, Poverty, Working, Middle, Wealthy, Religious, Nobles, Rulers*—such as better store prices, carry weight, XP gain, or magic resistance).
2. **Follower Combat Buffs (Based on Skill Type)**:
   - Granted **only by married spouses currently accompanying you as active followers**.
   - Reflects fighting side-by-side with a specialist (*Warrior, Mage, Rogue, Craftsman, Ranger, Orator*—such as melee damage, magicka regen, sneak effectiveness, armor defense, ranged damage, or shout cooldown reduction).

---

### 2.2 Monogamy Bonus vs. Polygamy Hierarchy Scaling
For **Permanent Household Buffs** (and daily store income), a spouse's contribution depends on whether you are monogamous or polygamous:
- **Monogamy Devotion Bonus (`2.0x`)**: If you are married to **only one spouse**, that single spouse's Social Class buff is **doubled (`2.0x`)**! This rewards monogamous roleplay so a player with one devoted spouse can rival a polygamous household.
- **Polygamy Hierarchy Scaling (`2+` Spouses)**: As soon as you have two or more spouses, each spouse's contribution is weighted by their rank in `SpouseHierarchyManager`:
  - **Lead Spouse**: `1.00x`
  - **Second Spouse**: `0.50x`
  - **Third Spouse**: `0.25x`
  - **Fourth+ Spouses**: `0.10x` each
- **Follower Combat Buff Scaling**: Follower buffs do *not* penalize lower-ranked spouses—any spouse fighting beside you contributes their full `1.0x` base Skill Type bonus multiplied by their affection.

---

### 2.3 Emotional Multiplier & Stacking
- **Affection Scaling**: Both Permanent and Follower buffs are multiplied by each spouse's current **Affection Multiplier** (`1.25x` when *Happy*, `1.00x` when *Content*, `0.25x` when *Troubled*, and `0.00x` when *Estranged*). An estranged spouse provides zero buffs even if they are your Lead Spouse or active follower.
- **Same-Archetype Stacking**: If you marry multiple spouses who share the same Social Class or bring multiple spouse followers who share the same Skill Type, their multipliers **stack additively** in that archetype's bucket.
- **Live Dashboard Breakdown**: Generates a real-time breakdown for the Prisma UI **My Household $\rightarrow$ Household Buffs** view, showing the exact effective percentage/point bonus for all 14 buff categories, which spouses are actively contributing, and which at-home spouses *could* contribute to a Follower Buff if recruited into your party.

