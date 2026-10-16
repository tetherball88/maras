# MarriageDifficulty — Proposal Success & Social Prestige Mechanics

[MarriageDifficulty.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MarriageDifficulty.cpp) calculates the exact probability (`0.0` to `1.0`) that an NPC will accept a marriage proposal from the player. Rather than relying solely on whether the player wears an Amulet of Mara and completed a single favor quest, the service evaluates social class expectations, player prestige, romantic history, existing polygamous commitments, guild reputation, and current affection.

All tuning weights and thresholds are loaded from `Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json` (with built-in defaults if the file is missing or partially specified) and can be adjusted live from the Prisma UI Settings tab.

---

## 1. Core Complexity-to-Probability Pipeline

When the player proposes to an NPC, the service computes an aggregate **Proposal Complexity** score and converts it into a final acceptance probability:

1. **Base Complexity**: Starts at a configurable baseline (`initialComplexity`, default `50.0`).Higher complexity makes acceptance harder; negative modifiers (bonuses) reduce complexity and make acceptance easier.
2. **Factor Accumulation**: Complexity is adjusted by:
   - Social Class Prestige Gap
   - Romantic & Marital History Modifiers
   - Permanent Affection & Relationship Rank
   - Player Speechcraft & Level Disparity
   - Guild Alignment & Shared Faction Membership
3. **Difficulty Clamping**: Total complexity is clamped to `[difficultyClampMin, difficultyClampMax]` (default `[-100.0, 200.0]`).
4. **Sigmoid Probability Conversion**: The clamped difficulty is mapped onto a smooth `[0.0, 1.0]` probability curve using a logistic sigmoid function:
   $$\text{SuccessChance} = \frac{1}{1 + e^{\text{steepness} \times (\text{difficulty} - \text{center})}}$$
   With default parameters (`center = 50.0`, `steepness = 0.08`), a difficulty score of `50` yields a **50% chance**, lower complexity rapidly approaches **100%**, and high complexity approaches **0%**.

---

## 2. Social Class Expectations vs. Player Prestige

Every NPC has a resolved **Social Class** (`Outcast`, `Poverty`, `Working`, `Middle`, `Wealthy`, `Religious`, `Nobles`, `Rulers`), indexed `0` through `7`. Higher-status NPCs expect a higher degree of personal renown and wealth from a prospective spouse.

### Social Class Target Score
The NPC's expected prestige target scales linearly with their social class index:
$$\text{TargetPrestige} = \text{prestigeTargetBase} + (\text{prestigeTargetPerSocialIndexMultiplier} \times \text{SocialClassIndex})$$

### Player Prestige Calculation
The player's actual prestige score is summed from six in-game achievements and misc stats, then clamped to `[prestigeClampMin, prestigeClampMax]` (default `[0.0, 100.0]`):

1. **Dragonborn Recognition**: Grants `dragonbornBonus` (default `+25`) once the main quest `MQ104` (*Bleak Falls Barrow / Dragon Rising* progression, stage $\ge 90$) confirms the player as Dragonborn.
2. **Thane Titles**: Checks quest stages ($\ge 25$) across all nine hold favor quests (Eastmarch, Falkreath, Haafingar, Hjaalmarch, The Pale, The Reach, The Rift, Whiterun, Winterhold). Each hold where the player is Thane adds `thanePerHoldBonus` (default `+5`).
3. **Guild Leadership**: Grants `guildLeaderBonus` (default `+15`) if the player has become the leader of the Companions (`C06` $\ge 200$), the College of Winterhold (`MG08` $\ge 200$), or the Thieves Guild (`TGLeadership` $\ge 40$).
4. **Peak Wealth**: Reads the engine's *"Most Gold Carried"* misc stat, divides by `mostGoldDivisor` (default `5000`), and clamps the contribution to `mostGoldMaxBonus` (default `+20`).
5. **Property & Mounts**: Reads *"Houses Owned"* ($\times$ `houseBonusPerUnit`) plus *"Horses Owned"* ($\times$ `horseBonusPerUnit`), clamped to `propertyBonusClampMax` (default `+15`).
6. **Heroic Renown**: Combines *"Quests Completed"*, *"Dungeons Cleared"*, and *"Dragon Souls Collected"* multiplied by their respective weights and clamped to `heroicClampMax` (default `+25`).

### Prestige Gap Impact
The difference between what the NPC expects and what the player has achieved adjusts proposal complexity:
$$\text{Complexity} \mathrel{+}= (\text{TargetPrestige} - \text{PlayerPrestige}) \times \text{prestigeDeltaMultiplier}$$
- If the player's prestige **exceeds** the NPC's social class expectations, complexity **decreases** (making the proposal easier).
- If an unaccomplished player proposes to a Noble or Ruler, the large positive gap **increases** complexity significantly.

---

## 3. Relationship History & Affection Modifiers

### Prior Courting Progression Bonuses
Proposing after building a relationship through M.A.R.A.S stages rewards the player with substantial complexity reductions:
- **Currently Dating**: Reduces complexity by `datingBonus` (default `-15.0`).
- **Currently Lovers**: Reduces complexity by `loverBonus` (default `-30.0`).
- **Intimacy History**: Applies any custom `intimacyAdjustment` passed by the caller.

### Negative History & Polygamy Penalties
- **Previously Jilted**: If the player previously broke off an engagement with this NPC, adds `jiltedPenalty` (default `+40.0`).
- **Previously Divorced**: If the player previously divorced this NPC, adds `divorcedPenalty` (default `+50.0`).
- **Partner Murderer**: If the player killed a previous romantic partner (`playerKiller` flag), adds `killerPenalty` (default `+60.0`).
- **Existing Spouses (Polygamy Scaling)**: Every currently married spouse the player already has increases complexity by `marriedCountMultiplier` (default `+20.0` per spouse).
- **Prior Divorces (Reputation Penalty)**: Every total divorce on the player's record adds `divorcedCountMultiplier` (default `+15.0` per divorce).

### Permanent Affection & Vanilla Relationship Rank
- **Permanent Affection**: Centered around the neutral baseline of `50`:
  $$\text{Complexity} \mathrel{-}= (\text{PermanentAffection} - 50.0) \times \text{affectionMultiplier}$$
  High permanent affection reduces complexity; neglectful or low affection below `50` increases complexity.
- **Vanilla Relationship Rank**: Multiplies the engine's `BGSRelationship` rank (`-4` Arch-nemesis to `+4` Lover) by `relationshipRankMultiplier` (default `-5.0` per positive rank) and adds it to complexity.

---

## 4. Character Stats & Guild Alignment

### Speechcraft & Level Disparity
- **Player Speechcraft**: The player's current Speech skill reduces complexity by $\text{Speechcraft} \times \text{speechcraftMultiplier}$ (default `-0.3` per point, up to `-30` at 100 Speech).
- **Level Disparity**: Compares the NPC's level to the player's level:
  $$\Delta\text{Level} = \text{clamp}(\text{NPCLevel} - \text{PlayerLevel},\ \text{levelDiffClampMin},\ \text{levelDiffClampMax})$$
  Multiplied by `levelDiffMultiplier` (default `+1.0`). High-level NPCs are harder for a low-level player to impress, while out-leveling the NPC provides a modest advantage.

### Guild Alignment & Social Class Perception
Different social strata view Skyrim's major factions differently (e.g., Nobles and Rulers look down on the Thieves Guild and Dark Brotherhood, whereas Working or Outcast NPCs may respect or fear them).

The service evaluates five major guilds:
1. **The Companions** (`0x00048362`)
2. **Thieves Guild** (`0x00029DA9`)
3. **Dark Brotherhood** (`0x0001BDB3`)
4. **College of Winterhold** (`0x0001F259`)
5. **Bards College** (detected via quest `MS05` stage $\ge 300$)

For every guild the player belongs to:
- **Social Class Perception**: Looks up how the NPC's specific social class views that guild (`guildSocialMods` table in config). Across all guilds the player belongs to, the service tracks the single strongest positive impression and the single strongest negative impression, then applies whichever has the larger absolute magnitude.
- **Shared Guild Bonus**: If both the player and the target NPC belong to the **same** guild (including both being Bards), complexity is reduced by `sameGuildBonus` (default `-15.0`) for each shared guild.

---

## 5. Non-AI Dialogue Proposal Globals & Tier Modifiers (`TTM_Data.SetGameNoAIChancesGlobals`)

When the player opens dialogue with a tracked NPC while SkyrimNet is inactive (`!TTM_Data.GetHasSkyrimNet()`) and the NPC is neither `married` nor `engaged`, [`TTM_MainController.OnDialogueOpened`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_MainController.psc#L515-L529) calls [`TTM_Data.SetGameNoAIChancesGlobals`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Data.psc#L111-L156) to populate three `TT_MARAS.esp` `GlobalVariable` records evaluated by Creation Kit dialogue conditions:

| Global Variable Getter | FormID (`TT_MARAS.esp`) | Purpose |
| :--- | :---: | :--- |
| `TTM_Data.GetLoverChanceGV()` | `0x5D1` | Threshold (`0–100`) for accepting a **Lover** proposal in standard dialogue. |
| `TTM_Data.GetDatingChanceGV()` | `0x5D0` | Threshold (`0–100`) for accepting a **Dating** proposal in standard dialogue. |
| `TTM_Data.GetMarriageChanceGV()` | `0x15C` | Threshold (`0–100`) for accepting an **Engagement / Marriage** proposal in standard dialogue. |

### Calculation & Stage Scaling in `SetGameNoAIChancesGlobals`
1. **Always Succeed Override (`alwaysSuccessMarriage`)**:
   - If the `alwaysSuccessMarriage` setting is enabled, `loverGV`, `datingGV`, and `marriageGV` are set to `100.0` and the function returns immediately.
2. **Base Percentage Conversion**:
   - Calls [`TTM_ServiceMarriageDifficulty.calcMarriageSuccessChance(npc)`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceMarriageDifficulty.psc#L61-L72) (which passes [`intimacyAdjustment(npc)`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceMarriageDifficulty.psc#L3-L58) and player misc stats into `MARAS.CalculateMarriageSuccessChance` and scales the result to a `0–100` base `chance`).
3. **Cheating & Ex-Partner Multipliers**:
   - **Cheating Penalty (`0.6x`)**: Checks faction rank in `TTM_CanFormNonCheatingMain` (`0x5D2`) on both the NPC and the Player. If either actor has `Rank 0` (meaning they are in a closed main-tier relationship and cannot form a new committed relationship without cheating), `chance` is multiplied by `0.6`.
   - **Ex-Partner Penalty (`0.75x`)**: If the NPC has status `exlover`, `exdating`, `jilted`, or `divorced`, `chance` is multiplied by `0.75` (`exMult`).
4. **Romance Tier Scaling**:
   - **Lover (`loverChance`)**: `chance * 0.7` (easiest tier threshold).
   - **Dating (`datingChance`)**: `chance * 0.85` (easier than engagement).
   - **Marriage (`marriageChance`)**: `chance`.
5. **Existing Relationship Stage Adjustments**:
   - If the NPC is currently a **`lover`**: `datingChance` is multiplied by `1.5` (`lover` $\rightarrow$ `dating`) and `marriageChance` is multiplied by `1.2` (`lover` $\rightarrow$ `engaged`).
   - If the NPC is currently **`dating`**: `marriageChance` is multiplied by `1.5` (`dating` $\rightarrow$ `engaged`).
6. **Global Assignment**:
   - Writes the final `loverChance`, `datingChance`, and `marriageChance` values to `0x5D1`, `0x5D0`, and `0x15C` respectively.

