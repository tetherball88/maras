# Archetypes & Buffs – Who You Married *Matters* 🧠💖

Every spouse in M.A.R.A.S isn’t just “some NPC with a ring.” Under the hood, each one gets a **3-part archetype**:

- **Social Class** – where they sit in Skyrim’s social ladder
  *(Outcast, Poverty, Working, Middle, Wealthy, Religious, Nobles, Rulers)*
- **Skill Type** – what they’re actually good at
  *(Warrior, Mage, Rogue, Craftsman, Ranger, Orator)*
- **Temperament** – how they emotionally react to you
  *(Proud, Humble, Jealous, Romantic, Independent)*

These drive **buffs** (social class + skill type) and **affection behavior** (temperament).

---

> **Related docs**
>
> If you want to see how these archetypes interact with affection and daily behavior:
>
> - [Affection System – Deep Dive](AFFECTION_SYSTEM.md) – how temperaments modify gains and losses from gifts, sleeping, promotions, etc.
> - [Player Guide](PLAYER_GUIDE.md) – how hierarchy ranks, polygamy, and divorces play out in actual quests and dialogue.
> - [Current Buffs MCM Page](MCM_BUFF.md) – live UI showing the final multipliers for each social class and skill type.
> - [NPC Explorer MCM Page](MCM_NPC.md) – where you can change an NPC’s social class, skill type, and temperament per-save.

---

## Social Class – Who They Are in the World 🏰🧹

Social Class is mostly determined by what the NPC *is* in Skyrim:

- **Outcast** – bandits, fugitives, people on the margins.
- **Poverty** – beggars, desperate laborers, folks scraping by.
- **Working** – farmers, guards, dock workers, miners, basic town workers.
- **Middle** – shopkeepers, skilled professionals, comfortable but not rich.
- **Wealthy** – successful merchants, rich landowners, high-status non-nobles.
- **Religious** – priests, priestesses, temple servants, devout clerics.
- **Nobles** – noble families, court officials, high-born hangers-on.
- **Rulers** – jarls, thanes, major power-holders and leadership figures.

This class controls which **permanent buff** you get when you marry them.

---

## Skill Type – What They Actually *Do* in Practice ⚔️📚🗡️

Skill Type is determined in two passes:

1. **First pass – their in-game combat/character class**
   If Skyrim already says “this person is a battlemage / ranger / rogue / etc.”, M.A.R.A.S uses that.

2. **Second pass – their strongest skills**
   If the class isn’t clear enough, M.A.R.A.S looks at which skill group they’re best at:
   - High combat skills like one-handed, two-handed, heavy armor, block or similar → **Warrior**
   - Archery-focused builds (especially when ranged skills clearly dominate) → **Ranger**
   - Sneak, pickpocket, lockpicking, and thief-adjacent builds → **Rogue**
   - Magic school dominance (Destruction, Restoration, Conjuration, Illusion, Alteration, Enchanting, etc.) → **Mage**
   - Speech being their standout talent → **Orator**
   - Craft skills like Smithing, Alchemy and utility trades → **Craftsman**
   - Special case: if **Light Armor** is their top skill, the system checks whether they lean more toward archery (Ranger) or thief skills (Rogue) and decides accordingly.

This Skill Type controls which **temporary follower buff** you get when they accompany you.

---

## Temperament – Personality From Their Role 😇😈

Temperament isn’t random: it’s derived from the **intersection** of Social Class and Skill Type.

| Social Class \ Skill Type | Warrior     | Mage        | Rogue       | Craftsman   | Ranger      | Orator      |
| ------------------------- | ----------- | ----------- | ----------- | ----------- | ----------- | ----------- |
| **Outcast**               | Independent | Jealous     | Jealous     | Humble      | Independent | Romantic    |
| **Poverty**               | Humble      | Romantic    | Jealous     | Independent | Proud       | Romantic    |
| **Working**               | Proud       | Humble      | Romantic    | Independent | Independent | Proud       |
| **Middle**                | Proud       | Romantic    | Independent | Romantic    | Humble      | Jealous     |
| **Wealthy**               | Proud       | Jealous     | Romantic    | Romantic    | Independent | Jealous     |
| **Religious**             | Independent | Humble      | Romantic    | Jealous     | Humble      | Proud       |
| **Nobles**                | Jealous     | Romantic    | Independent | Proud       | Humble      | Proud       |
| **Rulers**                | Proud       | Independent | Humble      | Romantic    | Jealous     | Independent |

Every single social-class/skill-type combo maps to one of:

- **Proud**
- **Humble**
- **Jealous**
- **Romantic**
- **Independent**

### What Temperament Actually Does

Temperament **doesn’t** change the buff list or gameplay rules directly or SkyrimNet's personality context.
It **only modifies how much affection they gain or lose** from certain actions:

- **Romantic** – bigger gains from gifts, intimacy, and sleeping nearby; more sensitive to being ignored.
- **Jealous** – strong reaction to intimacy and presence; doesn’t love feeling second-best.
- **Proud** – big emotional swings from promotion/demotion in your household rank.
- **Humble** – softer responses to rank changes, more forgiving overall.
- **Independent** – gets less from mushy closeness, loses less from distance; emotionally low-maintenance.

So archetypes = flavor + math: **who they are** decides *how* they react to your behavior.

---

## Permanent Buffs – Social Class, Always On (If They Care Enough) ♾️

Once you’re married, each spouse’s **Social Class** gives you one **permanent buff** just for being wed to them:

- **Outcast – "Kiss of the Knife"**
  > A life on the fringe teaches you to strike unseen, increasing sneak attack damage by 10%

- **Poverty – "Enduring Hearts"**
  > Enduring hardship together fosters resilience, allowing stamina to regenerate faster 10%

- **Working – "Labor's Legacy"**
  > Hard-working roots carry their weight, increasing your carry capacity 30 points

- **Middle – "Steady Kinship"**
  > Your stable domestic life encourages learning, increasing skill experience gained by 2%

- **Wealthy – "Merchant's Dowry"**
  > Sharing wealth brings prosperity, increasing your sell prices by 5%

- **Religious – "Blessing of Union"**
  > Living under divine guidance shields you from harm, increasing your magic resistance by 3%

- **Nobles – "Aristocratic Favor"**
  > Your noble ties ease every transaction, reducing store prices by 5%

- **Rulers – "Speech of Sovereigns"**
  > Being wed to a figure of influence sharpens your rhetoric, increasing your Speech skill by 5 points

Those percentages are **base values**. The real in-game effect is then scaled by:

1. How many spouses you have and what rank this spouse holds.
2. How much they currently like you (affection threshold).

---

## Temporary Follower Buffs – Skill Type, Only When They’re With You 🚶‍♀️⚔️

If your spouse is **actively following you**, their **Skill Type** grants a **temporary follower buff**:

- **Warrior – "Bond of Steel"**
  >With your spouse at your side in battle, your melee attack damage is increased by 5%

- **Mage – "Arcane Union"**
  >Fighting beside a practiced spellcaster renews your focus, allowing magicka to regenerate faster by 10%

- **Ranger – "Wildsight Bond"**
  >With a seasoned ranger by your side, your instincts sharpen and your aim steadies, increasing ranged damage by 5%

- **Rogue – "Shared Shadows"**
  >Your partner's silent footsteps teach you subtlety, increasing your sneak effectiveness by 5%

- **Orator – "Shared Breath"**
  >Your bond with your orator spouse sharpens your mastery of breath, decreasing the time between Shouts by 5%.

- **Craftsman – "Tailored Defense"**
  >Your spouse's mastery in the forge ensures your armor fits true and holds strong, reducing damage taken by 3%"

Again, these are **base values** that get multiplied by affection-based scaling.

---

## Buff Scaling – How Strong Are These Bonuses Really? 📈

Every buff is not created equal every day. Their actual strength depends on:

1. **How much that spouse loves you (permanent affection).**
2. **How many spouses you have and what rank they hold (for permanent buffs only).**

### 1. Affection Threshold Multiplier

Permanent affection (0–100) is turned into a **buff multiplier**:

- **75–100** affection → **1.25×** buff (really happy with you)
- **50–74** affection → **1.0×** buff (content / normal strength)
- **25–49** affection → **0.25×** buff (they’re not thrilled, but there’s still something there)
- **0–24** affection → **0.0×** buff (estranged; no benefit at all)

This affection multiplier is applied to:

- Their **permanent social-class buff**
- Their **follower skill-type buff** (when following)

Happy spouse = stronger buffs. Estranged spouse = “sleeping on the metaphorical couch” buff level (none).

---

### 2. Rank & Spouse Count – Permanent Buffs Only

Permanent buffs also care about **how many spouses you have** and which **household rank** each one holds.

- If you have **only one spouse**:
  - Their permanent buff gets a **2.0× “single spouse” multiplier** before affection is applied.
  - In practice: monogamy = stronger single bond buff.

- If you have **multiple spouses**, the game uses **household rank**:
  - **Lead spouse (rank 1st)** → **1.0×**
  - **Second** → **0.5×**
  - **Third** → **0.25×**
  - **Everyone else (4th, 5th, 10th, etc.)** → **0.1×**

So for permanent buffs, each spouse’s final contribution is roughly:

> **Permanent buff strength = Rank multiplier × Affection multiplier**

Examples:
- A **Happy (≥75)** lead Noble spouse:
  - Rank: **1.0×**, Affection: **1.25×** → Nice strong discount buff.
- A **Troubled (30 affection)** Ruler in 5th place:
  - Rank: **0.1×**, Affection: **0.25×** → Their buff is a faint whisper of what it could be.
- Your only spouse, **Happy Working-class**:
  - Single spouse: **2.0×**, Affection: **1.25×** → Their carry capacity buff really pulls its weight.

---

### 3. Follower Buffs – No Rank, Just Feelings

For **temporary follower buffs**, rank and spouse count don’t matter:

- Only the **affection multiplier** is used.
- Whether they’re your first spouse or your eleventh, if they’re following and they love you, you get the stronger version of their follower buff.

So:

- Happy Warrior spouse following you → “Bond of Steel” at **1.25×** its base value.
- Estranged Mage spouse following you → “Arcane Union” at **0×**; they’re physically there, emotionally checked out.

---

## TL;DR – Building Your Dream Roster 🧩

- **Choose Social Classes** for your always-on bonuses (sneak, stamina, XP, prices, magic resist, Speech…).
- **Choose Skill Types** for your on-the-road follower buffs (melee, magic, ranged, sneaking, shouts, tankiness).
- **Temperaments** decide how dramatic the affection swings are when you:
  - Give gifts
  - Sleep near them
  - Promote/demote them
  - Spend (or don’t spend) time with them

If you want to min-max:

- Keep your **favorite buff spouses** in higher ranks.
- Keep their **affection high** (≥75) for the best numbers.
- Rotate follower spouses based on what you’re doing: dungeon crawling with a Warrior, sneaking with a Rogue, shouting with an Orator, etc.

If you want to roleplay:

- Ignore the spreadsheets.
- Marry the vibes.
- Just know that the game is quietly rewarding you for being a decent partner… and quietly nerfing you when you aren’t. 😉
