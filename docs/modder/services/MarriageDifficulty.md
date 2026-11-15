# `MarriageDifficultyRules` & `MarriageDifficultyLoader` — Architecture & Behavioral Guide

**Layers**: Domain Relationships (`SKSE_Source/modules/domain_relationships`) & Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [MarriageDifficultyRules.h](../../../SKSE_Source/modules/domain_relationships/include/maras/domain/relationships/MarriageDifficultyRules.h)
- [MarriageDifficultyRules.cpp](../../../SKSE_Source/modules/domain_relationships/src/MarriageDifficultyRules.cpp)
- [MarriageDifficultyLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/MarriageDifficultyLoader.h)
- [MarriageDifficultyLoader.cpp](../../../SKSE_Source/modules/engine/src/MarriageDifficultyLoader.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

Marriage proposal difficulty is split strictly between an engine-free pure evaluator (`MARAS::Domain::Relationships::MarriageDifficultyRules`) and a JSON config loader (`MARAS::Engine::MarriageDifficultyLoader`). Together they compute Player social prestige, NPC social-tier expectations, relationship history modifiers, guild alignment, and a smooth `[0.0, 1.0]` logistic marriage acceptance probability.

---

## 2. Configuration Loading & Fallback Order

`MarriageDifficultyLoader` initializes at `kDataLoaded` and reloads cleanly on `kPostLoadGame` and `kNewGame`.

- **Search Order**:
  1. `Data/SKSE/Plugins/MARAS/marriage_difficulty.json`
  2. `Data/SKSE/Plugins/MARAS/marriageDifficultyConfig.json`
  3. `SKSE/Plugins/MARAS/marriage_difficulty.json`
  4. `SKSE/Plugins/MARAS/marriageDifficultyConfig.json`
- **Resilience**: Starts from `MarriageDifficultyConfig::CreateDefault()` so missing files or omitted JSON keys cleanly inherit compiled defaults.
- **Section Precedence**: Scalar keys are mapped from JSON sections (`difficultyCalculation`, `complexity`, `prestige`, `penalties`, `multipliers`, `bonuses`, `guilds`). Later sections override earlier fallback keys (for example, `bonuses.datingBonus` overrides `multipliers.datingBonus` and `penalties.datingBonus`). Guild-by-social-class matrices (`companions`, `thieves`, `brotherhood`, `college`, `bards` $\times$ `outcast`..`rulers`) expand into keyed map parameters.

---

## 3. Calculation Pipeline & Formulas

`CalculateMarriageSuccessChance(config, inputs)` executes deterministically from pure inputs gathered by the caller (zero TES/RE lookups inside the rule module):

### Step 1: Player Prestige (`[0, 100]` clamped)
Sums the Player's standing from configurable weights:
- **Main-Quest & Titles**: `dragonbornBonus` (`+25`), `thaneHoldValue * min(thaneHolds, maxThaneHolds)` (`4 * up to 9`), `guildLeaderBonus * guildLeaderCount` (`+8` each).
- **Wealth & Holdings**: `min(mostGold / 5000, 15)` + `min(houses * 3 + horses * 1, 15)`.
- **Renown**: `min(questsCompleted * 0.2 + dungeonsCleared * 0.1 + dragonSouls * 0.5, 25)`.
- Final `playerPrestige` is clamped to `[prestigePrestigeClamp_min, prestigePrestigeClamp_max]` (`[0, 100]`).

### Step 2: Target Prestige & Base Complexity
- **Target Social Expectation**: `targetPrestige = targetBase + targetPerSocialIndexMultiplier * socialClassIndex` (`10 + 10 * index`, scaling from `10` for `Outcast` up to `80` for `Rulers`).
- **Base Complexity**: Starts at `initialComplexity` (`50`), adding `(targetPrestige - playerPrestige) * 0.30`.

### Step 3: Relationship, Trait & Guild Modifiers
- **Prior Bonds**: Applies `datingBonus` (`-15`, non-zero fallback guarded) or `loverBonus` (`-30`), plus `jiltedPenalty` (`+10`), `divorcedPenalty` (`+20`), and `playerKillerPenalty` (`+50`).
- **Household & Stats**: Adds `marriedCount * 2`, `divorcedCount * 1`, clamped `(npcLevel - playerLevel) * 0.5` (`[-10, 10]`), `playerSpeechcraft * -0.1`, vanilla `relationshipRank * -3.0`, caller `intimacyAdjustment`, and subtracts `(permanentAffection - 50) * 0.5`.
- **Guild Alignment (`CalculateGuildAlignment`)**: Inspects all five major Skyrim guilds where the Player is a member: tracks the single strongest positive or negative social-class reaction (`-bestNegative > bestPositive ? bestNegative : bestPositive`), plus an additive `sameGuildBonus` (`-4.0`) for each guild shared with the NPC.

### Step 4: Clamped Difficulty & Sigmoid Probability
1. Clamps raw `complexity` into `difficulty` within `[difficultyClampMin, difficultyClampMax]` (`[0, 100]`).
2. Computes acceptance probability via the inverted logistic curve:
   $$\text{chance} = \frac{1}{1 + \exp\big(\text{sigmoidSteepness} \cdot (\text{difficulty} - \text{sigmoidCenter})\big)}$$
   With default parameters (`sigmoidSteepness = 0.10`, `sigmoidCenter = 50.0`), difficulty `50` yields `50%` chance, lower difficulty yields higher probability, and higher difficulty smoothly approaches `0%`.

---

## 4. Native No-AI Dialogue Chances, Stats Cache & `TT_LoversLedger` Bridge

When dialogue opens, [`DialogueEventSink`](../ARCHITECTURE.md) notifies `RelationshipInteractionCoordinator::OnDialogueOpened`:

- **Pure Intimacy Adjustment (`ComputeIntimacyAdjustment` & `ComputeLoversLedgerIntimacyBonus`)**: Reduces proposal difficulty based on existing player intimacy status (`25` married spouse, `30` married spouse also lover, `15` engaged partner, `20` engaged fiancé also lover, `10` active lover) minus any optional `TT_LoversLedger` bonus (`10` exclusive sex or `6` group sex, plus `2 * (3 - playerTopLoverRank)` for ranks `0`, `1`, or `2`).
- **Cached Player Stats (`PlayerMiscStatsTracker`)**: Subscribes to `RE::TESTrackedStatsEvent` and seeds initial totals asynchronously via `Game.QueryStat` on load/new game (`mostGold`, `housesOwned`, `horsesOwned`, `questsCompleted`, `dungeonsCleared`, `dragonSoulsCollected`) so dialogue-open and divorce calculations run without synchronous Papyrus VM queries.
- **No-AI Chance Scaling (`ComputeNoAiDialogueChances` $\rightarrow$ `RelationshipEngineReconciler`)**: Converts `chance` (`0..1`) to `[0, 100]` (`100` across all three when `alwaysSuccessMarriage` is enabled), applies `0.6x` when either actor holds a closed main pair (`canFormNonCheatingMainRank == 0`) and `0.75x` for prior breakup statuses, applies stage-progression boosts (`dating * 1.15`, `lover * 1.3`, plus `marriage * 1.2` / `dating * 1.5` when `Lover`, and `marriage * 1.5` when `Dating`), clamps to `[0, 100]`, and writes `0x5D0` (`TTM_DatingChance`), `0x5D1` (`TTM_LoverChance`), and `0x15C` (`TTM_MarriageChance`).
- **Optional `TT_LoversLedger.esp` Refresh**: If `TT_LoversLedger.esp` is loaded and `alwaysSuccessMarriage` is disabled, `OnDialogueOpened` dispatches `TTM_ServiceIntimateMoments.RefreshDialogueLoversLedger(npc)` post-lock, which calls back `ApplyDialogueLoversLedgerBonus` to recompute and overwrite the three chance globals with live ledger metrics.
