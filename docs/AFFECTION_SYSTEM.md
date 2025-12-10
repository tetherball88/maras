## Affection System – Deep Dive ❤️📊

M.A.R.A.S doesn’t just flip a “likes you / doesn’t like you” switch – it runs a **full affection engine** under the hood. This section explains exactly what that means, what affects it, and how to avoid accidentally speed-running your way into estranged territory.

---

> **Related docs**
>
> If you’re looking for how this feels in actual gameplay rather than formulas, see:
>
> - [Player Guide](PLAYER_GUIDE.md) – step-by-step flow from candidate to engaged, married, jilted, and divorced.
> - [Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md) – how affection levels and temperaments change buff strength.
> - [NPC Explorer MCM](MCM_NPC.md) – how to inspect and edit affection, archetypes, and hierarchy in-game.
---

## 1. What Affection Is (and Where It Applies)

- Affection is a **0–100 score** per NPC.
- It only exists for **NPCs registered in M.A.R.A.S**
  (i.e., those you’ve spoken to with `(M) Ever think we could be something more?`).
- **Decay over time** only affects:
  - **Fiancés/fiancées**
  - **Spouses**
- **Candidates** don’t lose affection over time; they’re just quietly judging your gifts.

Affection feeds into:

- 💍 **Marriage chances** (higher affection = easier “Yes”)
- 💫 **Buff strength** (your emotional support spouse is literally a buff)
- 🗣️ **SkyrimNet personality** (extra context for how they feel about you)
- 🏡 **Marriage features**:
  - Cooking for you
  - Sharing store income
  - Sharing their home
  - Choosing to live in your home

---

## 2. Two Layers: Short-Term vs Long-Term 💡

M.A.R.A.S uses a **two-level affection system**:

1. **Short-Term Affection**
   - Tracks all gains & losses during a **single in-game day**.
   - Split into categories like **Gift**, **Intimacy**, **Slept**, etc.
   - Has **per-day caps per category** so you can’t jump from 0 → 100 in one afternoon.
   - On its own, it doesn’t do anything permanent.

2. **Long-Term Affection**
   - Once per in-game day, the short-term values are **rolled into** long-term affection.
   - This long-term value (0–100) is what actually:
     - Changes marriage odds
     - Affects buffs
     - Controls whether you get “happy/content/troubled/estranged.”
   - Then short-term is reset for the next day.

Think of it like this:

> **Short-term** = “What you did today”
> **Long-term** = “How they feel about you overall”

You can’t brute-force the relationship in one day; you need **consistent effort over time**.

---

## 3. Short-Term Categories & Daily Caps 📆

Short-term affection is split into named **categories**. Each category has its own **daily minimum and maximum**, so you can’t abuse one source forever.

### Daily Caps Per Category

| Category       | Daily Range | What it Represents                          |
| -------------- | ----------- | ------------------------------------------- |
| **Intimacy**   | -22 → +22   | OStim-style intimacy, with temperament mods |
| **Gift**       | 0 → +15     | All gifts combined for that day             |
| **Dialogue**   | 0 → +5      | Starting M.A.R.A.S conversations            |
| **Slept**      | 0 → +8      | Sleeping near them                          |
| **Promotion**  | -24 → +24   | Hierarchy promotions/demotions              |
| **Loneliness** | -4 → +6     | Being ignored vs. actually spending time    |

> ⚠️ Example: Even if you throw 50,000 gold at your spouse in one day, Gift is still capped at +15 for that day. No “infinite bribe to instant soulmate” exploit.

## 4. How Each Source Works (With Temperament Spice)

### 4.1 Intimacy (Lover's Ledger)

- Base intimacy amount is passed in from the OStim-related logic.
- Temperament multipliers:
    - Romantic ❤️ → ×1.5
    - Jealous 👀 → ×1.5
    - Independent 🧍 → ×0.5
    - Proud / Humble (or others) → ×1.0 (no change)
- Clamped daily to -22 → +22.

> **Player takeaway:** Romantic and Jealous spouses care a LOT about physical closeness. Independent ones… not so much.

### 4.2 Gifts (Gold-Backed Affection)

Formula:

- **Base scaling:** `giftAffection = value / (spouseSocialClassFactor * 100.0)` where `spouseSocialClassFactor = 0.667 + socialClass/7`
- Lower social class → **more impressed** by the same value.
- Example given in comments for a **1000 gold** value gift:

| Social Class | Example (0 = Outcast, 7 = Ruler) | Approx Affection |
| -----------: | -------------------------------- | ---------------- |
|            0 | Outcast                          | ≈ **15** points  |
|            3 | Middle                           | ≈ **9.1** points |
|            7 | Ruler                            | ≈ **6** points   |

- Temperament:
    - Romantic → ×1.5 to the gift affection.
    - Others → unchanged.
    - Daily category cap: 0 → +15.

> **Player takeaway:**
> - Big gifts are more effective on poorer spouses.
> - Romantics absolutely melt over thoughtful (or expensive) presents.
> - You still hit a +15/day wall from gifts, no matter how rich you are.

### 4.3 Talking – Just Checking In 🗣️

- Every time you start a in-game dialogues, you get a little +1 nudge.
- Clamped per day: 0 → +5 from dialogue.

> **Player tip:** If you’re busy, at least say hi. Five quick check-ins over the day can help maintain affection with minimal effort.

### 4.4 Promotions & Demotions (Hierarchy Drama) 👑

Base affection per rank step:
- For promotion points are positive for demotions negative.
- All temperaments → **8 points** per step
    - Proud → **12 points** per step
    - Humble → **4 points** per step
- Daily cap: **-24 → +24** for Promotion category.

**Examples:**
- Promote a **Proud** spouse by 1 rank → +12 affection.
- Demote them by 1 rank → -12 affection.
- Promoting them 3 ranks in one day? Still capped at +24 total from Promotion.

> **Player takeaway:** Proud spouses are very invested in their “ranking.” Demoting them is like emotional nuclear warfare.

### 4.5 Sleeping Together (Literally) 🛏️

Base affection for sleeping in the same location:
- Default → **+3**
- Romantic → **+5**
- Jealous → **+4**
- Independent → **+1**
- Daily cap for Slept category: **0 → +8**.
>**Player tip:** A simple “sleep nearby” is one of the easiest ways to keep Romantic/Jealous spouses happy. Independent ones… just appreciate that you didn’t snore too much.

### 4.6 Loneliness – The Silent Killer 😶

For **fiancés and spouses** only, affection starts to decay if you don’t visit them:

- If you haven’t given them **any affection-related interactions** for **more than 2 in-game days**, the system starts applying **Loneliness** penalties.
- The decay:
    - Comes from the loneliness category (daily clamped to -4 → +6).
    - **Default decay -3**
    - Temperament adjustments:
        - **Romantic → -4** - `“You didn’t come by for two days and I noticed.”`
        - **Humble → -2** - `They’re a bit more forgiving about distance.`
        - **Independent → -1** `You do you. Just maybe send a letter sometimes.`
    - For spouse following player decay turns into **"together time"**:
        - Each day spent together **together time** adds +6 affection points.

On top of that, to keep polygamy remotely manageable:

#### Spouse Count Decay Adjustments

If you have:
- 1 spouse → full decay: ×1.0
- ≥3 spouses → decay × 0.75
- ≥5 spouses → decay × 0.60
- ≥7 spouses → decay × 0.50

So the more spouses you have, the more the game quietly goes:
```
“Okay, you’re busy, let’s not delete your entire harem because you spent two in-game days in Blackreach.”
```

> **Player takeaway:**
> - You still need to check in occasionally, especially with favorites.
> - But if you’re juggling a dozen spouses, the decay is slower on purpose.

## 5. Daily Roll-Up: From Short-Term to Long-Term 📈

At the start of each new in-game day:
1. M.A.R.A.S looks at your short-term affection categories.
2. Each category is already clamped to its own daily min/max.
3. The combined result is added to long-term affection (0–100).
4. Short-term counters are reset for the next day.

**Example:**
- You:
  - Gift modestly → +10
  - Talk a bit → +3
  - Sleep near them → +5
  - No promotions/demotions → 0
  - No loneliness decay because you were present
- Total daily gain: +18 affection.
- Do that for a few days and you’ll climb from “meh” to “marriage material” pretty fast.

## 6. Affection Thresholds & “Marriage: The Last Word” ⚖️

Long-term affection isn’t just a number; it’s grouped into **states**:
- Happy 😀
- Content 🙂
- Troubled 😕
- Estranged 💔

Only **“Estranged”** currently drives a big scripted event:

### Marriage: The Last Word

When affection for a spouse drops into estranged:
- The quest **“Marriage: The Last Word”** starts.
- It’s the “we need to talk” moment for your marriage:
    - You’re called to confront the situation.
    - You can try to **save the relationship** (Persuade/Intimidate or SkyrimNet chat).
    - Or accept that things are over and let the marriage end in a divorce.

> The key point here is: **Estranged = crisis mode**. You either fix it or lose them.

> 💸 **Note on divorce consequences:**
> If “Marriage: The Last Word” ends in a **divorce** instead of a reconciliation, the breakup can also trigger a **divorce settlement** – a crime bounty in your ex-spouse’s home hold, based on your wealth, property, and achievements. This fee is delivered via courier (or auto-applied after a few days if you avoid them) and can only be charged once every **7 in-game days**. For the full breakdown, see the [Player Guide – Divorce & “Marriage: The Last Word”](PLAYER_GUIDE.md) (section 7).


## 7. How Affection Feeds Into Marriage Chance

- Affection is treated as 0–100 with 50 as "neutral".
- Values above 50 reduce internal "difficulty," boosting your chance.
- Values below 50 increase difficulty, lowering your odds.

In practical terms:
- Low affection: you're asking a near-stranger (or ex) to marry you. Expect more "Nope."
- High affection: chances shift noticeably in your favor. RNG can still be mean, but the curve is on your side.

> For the complete marriage difficulty calculation formula and all factors involved (prestige, guilds, social class, penalties, etc.), see [Marriage Difficulty Configuration Guide](MarriageDifficultyConfig.md).

## 8. Practical Tips 🎯

- **Don’t try to max in one day.** Use gifts, talking, and sleeping over several days to grow affection steadily.
- **Match actions to temperament:**
    - **Romantic** → loves gifts, intimacy, and sleeping nearby.
    - **Jealous** → also big on intimacy & proximity, less thrilled by being ignored.
    - **Independent** → don’t care much about closeness spam; steady, respectful contact is enough.
    - **Proud** → promotions hit hard; demotions hit very hard. Handle rank changes carefully.
    - **Humble** → they appreciate promotions, but don’t explode over them.
- **If you’re running a huge spouse roster:**
    - Remember decay multipliers help you.
    - Rotate check-ins, share a bed sometimes, and don’t demote half your roster in one day.
- **Watch for “troubled” mood before it becomes “estranged.”**
It’s easier to repair affection early than to pull a marriage back from the edge of “The Last Word.”

In short: the affection system is designed so your relationships feel **earned, maintainable, and occasionally messy** — just like real life, if real life had dragons and 17 spouses in the same farmhouse. 😉
