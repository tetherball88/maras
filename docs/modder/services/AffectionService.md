# `AffectionService` — Features & Behavioral Guide

**Source**: [`AffectionService.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AffectionService.cpp)

---

## 1. What This Service Does

`AffectionService` tracks how happy or neglected each of the player's romantic partners (`Married`, `Engaged`, `Dating`, and `Lover`) feels on a `0` to `100` scale. It governs daily emotional upkeep, prevents players from spamming a single action to max out affection in one day, scales household buffs and store income, and drives neglect-based breakups and divorces.

---

## 2. Core Features & Mechanics

### 2.1 Daily Accumulation vs. Permanent Affection (Anti-Spam System)
- **Intraday Buffering**: When the player performs positive or negative actions during the day (giving gifts, talking, intimate scenes, completing quests, or getting caught cheating), the points are not added directly to the partner's permanent score. Instead, they accumulate in **daily category buckets** (e.g., `"gift"`, `"visit"`, `"intimacy"`, `"loneliness"`).
- **Per-Category Daily Caps**: Each category can have a minimum and maximum daily cap. Even if a player gives 20 gifts in a single afternoon, the `"gift"` bucket is clamped to its daily ceiling when the day ends.
- **End-of-Day Commit**: At the start of each new in-game day, all clamped daily buckets are summed together and committed to the partner's **Permanent Affection** (`0–100`), and the daily buckets reset for the new day.

---

### 2.2 Emotional Tiers & Gameplay Impact
A partner's permanent affection score places them into one of four emotional states, which directly scales their **Spouse Buffs** and **Store Gold Income**:
1. **Happy (`75–100`) — `1.25x` Multiplier**: Peak relationship satisfaction; boosts passive buffs and daily gold by 25%.
2. **Content (`50–74`) — `1.00x` Multiplier**: Normal, healthy baseline relationship.
3. **Troubled (`25–49`) — `0.25x` Multiplier**: The partner feels neglected or hurt; buffs and income drop by 75%.
4. **Estranged (`0–24`) — `0.00x` Multiplier**: The relationship is on the verge of collapse. All buffs and income shut off completely, and crossing into `Estranged` triggers the **Estranged Divorce/Breakup Countdown** quest in Papyrus (`maras_change_affection` event), giving the player a limited window to win back their partner's heart before being served divorce papers.

- **Vanilla Plugin Dialogue Conditions (`TT_MARAS.esp`)**: Beyond buff/gold multipliers, `TT_MARAS.esp` dialogue branches evaluate the partner's mirrored affection rank (`TTM_AffectionFaction`) during sensitive vanilla relationship conversations—such as asking a partner if they have been faithful (to uncover cheating), demanding that they end an affair, or proposing to open/close the relationship. Depending on the topic, higher or lower affection either shifts the odds of behind-the-scenes **dice rolls** or lowers/raises the **Speech skill threshold** required for persuasion checks (applies to vanilla dialogue trees; SkyrimNet AI conversations resolve through LLM roleplay).

---

### 2.3 Loneliness Decay & Adventuring Together
Every day, the service checks how many days have passed since each player partner last received any affection interaction:
- **2-Day Grace Period**: If you interacted with a partner within the last **2 in-game days**, they suffer zero loneliness decay.
- **Adventuring Bonus (`+6` Affection/Day)**: If a partner is currently traveling with you as an active **Follower**, they never suffer loneliness decay—instead, they *gain* `+6` daily affection from spending time by your side!
- **Personalized Neglect Decay**: If a partner is left behind for more than 2 days, their daily affection loss is tailored to six factors:
  1. **Personality (Temperament)**:
     - *Independent* partners barely mind being left alone (`-1`/day).
     - *Humble* partners are patient (`-2`/day).
     - *Proud* and *Jealous* partners expect regular attention (`-3`/day).
     - *Romantic* partners crave constant closeness and hurt the most when ignored (`-4`/day).
  2. **Relationship Stage**:
     - *Spouses* feel the full weight of neglect (`1.0x`).
     - *Fiancés/Fiancées* decay slower (`0.75x`).
     - *Dating* partners expect less daily presence (`0.50x`).
     - *Casual Lovers* barely decay (`0.25x`).
  3. **Open Relationship Status (Temperament-Scaled)**:
     - When a main-tier relationship (`Dating`, `Engaged`, or `Married`) is marked **Open**, loneliness decay is multiplied based on how comfortable the partner's personality is with non-exclusivity while apart:
       - *Independent* (`0.50x`), *Humble* (`0.75x`), *Proud* (`0.90x`), *Romantic* (`1.15x`), *Jealous* (`1.35x`).
  4. **Known Ongoing Cheating (Temperament-Scaled)**:
     - If either the NPC or the Player has an active, discovered affair (`AffairStatus::Ongoing` and `!isSecret`) on the relationship pair, unresolved betrayal accelerates daily loneliness decay by temperament:
       - *Independent* (`1.25x`), *Humble* (`1.35x`), *Romantic* (`1.50x`), *Proud* (`1.75x`), *Jealous* (`2.00x`).
  5. **Large Household Dampening (Polyamory Relief)**:
     - So players with many partners aren't overwhelmed by travel upkeep, total decay is automatically reduced when you have **3–4 partners** (`0.75x`), **5–6 partners** (`0.60x`), or **7+ partners** (`0.50x`).
  6. **User Difficulty Setting**: Scaled by the player's **Affection Decay Multiplier** slider (`0.0x` to disable decay completely, up to `2.0x`), with the daily `"loneliness"` bucket clamped to `[-16, +6]`.
