# M.A.R.A.S Player Guide 💍🐉
*How to love, lose, and (maybe) rule all Skyrim without soft-locking your heart or your save.*

This guide walks you through what **you can do** and **can’t do** with M.A.R.A.S in actual gameplay: how to start relationships, how to keep them alive, how to blow them up (oops), and how polygamy & hierarchy really work.

---

> **Related docs**
>
> If you want the raw numbers behind affection and buffs, check:
>
> - [Affection System – Deep Dive](AFFECTION_SYSTEM.md) – how affection is calculated, capped, and rolled into long-term scores.
> - [Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md) – how social class, skill type, and temperament turn into actual bonuses.
>
> For UI help (MCM), see:
>
> - [MCM Settings Guide](MCM_Settings.md)
> - [NPC Explorer MCM Page](MCM_NPC.md)
> - [Current Buffs MCM Page](MCM_BUFF.md)
---

## 1. Getting Started – Turning an NPC Into a Candidate 💬

### Step 1: Register the NPC

After installing M.A.R.A.S, walk up to any NPC you’re interested in and choose:

> **`(M) Ever think we could be something more?`**

This tells M.A.R.A.S: *“Track this one. They’re special (or I will make bad choices with them).”*
From this moment, that NPC enters the **Candidate** state.

### What actually happens

- M.A.R.A.S reads your **vanilla relationship rank** with that NPC (from **-4 to +4**) and converts it into their **starting affection**.
- This gives you a **baseline**:
  - If they already like you (quest ally, friend, etc.) → you start ahead.
  - If they hate you (bandit, victim, “that guy you stole from”) → expect extra work.

### You can:
- Register **any NPC** as a Candidate with that line.
- Check in later with them to build affection and eventually propose.

### You can’t:
- Expect miracles if your base relationship rank is deep in the negative. You’re basically starting at “we hate each other, but sure, let’s talk.”

---

## 2. Candidate Stage – Building Affection ❤️

After registering them, talk to the NPC again. You’ll see:

> **`(M) May I have a word with you?`**

This opens two main branches if you’re **not** using SkyrimNet:
- **`I found something I thought you might like. (Gift)`**
- **`Will you marry me?`**

If you're **using SkyrimNet**, the marriage acceptance goes through AI chat instead of the simple "Will you marry me?" check, but the affection system still matters.

> **SkyrimNet Action Confirmations:** When using SkyrimNet, the AI can sometimes trigger major relationship changes (accepting proposals, breaking engagements, divorces) during conversations. You can control whether these require your confirmation in [MCM Settings](MCM_Settings.md) under "Actions Confirmation."

### How to increase affection as a Candidate

There are **three** ways to raise affection at this stage:

1. **Gifting**
   - Use: `I found something I thought you might like. (Gift)`
   - Higher value items → more affection.
   - The NPC’s **Social Class** affects how impressed they are.
     - A noble might shrug at cheap junk.
     - Someone from poverty/working class might be thrilled with modest gifts.

2. **Initiating the M.A.R.A.S dialogue**
   - Starting any dialogue gives a **small affection bump**.
   - Don’t expect miracles, but consistent check-ins help.

3. **Intimate moments via Lover's Ledger**
   - If you use Lover's Ledger, intimacy can also increase affection (especially once you’re in a closer relationship).

### Important mechanics

- **Affection does NOT decay** during Candidate stage.
- There is a **cap** to how much affection you can gain daily — you can’t brute-force the entire bar in one gifting binge.
- You’re meant to increase affection **over multiple in-game days**, not in a single “here’s 200 rings” session.

### You can:
- Slowly build trust and affection with gifts, chats, and (optionally) intimacy.
- Take your time before proposing; there’s no rush timer here.

### You can’t:
- Buy infinite affection. There’s a maximum, and it’s meant to be incremental.

---

## 3. Proposing & Engagement – Taking the Leap 💍

When you feel ready, use:

> **`Will you marry me?`** (no SkyrimNet)
> or
> Propose through **SkyrimNet chat** (if using SkyrimNet).

### The result

- M.A.R.A.S rolls your **marriage chance**, based on:
  - Affection
  - Stats/traits (wealth, prestige, etc.)
  - Guild memberships
  - Social class differences
  - Your relationship history
- You get **Yes** or **No**:

> For the complete technical breakdown of how marriage difficulty is calculated, see [Marriage Difficulty Configuration Guide](MarriageDifficultyConfig.md).

#### If you get **No**:
- You can **try again after 24 in-game hours**.
- Use the time to increase affection more (gifts, talk, Lover's Ledger if applicable).

#### If you get **Yes**:
- The NPC moves to **Engaged** (fiancé/fiancée).
- The **vanilla Wedding quest** starts normally.
- If you went through wedding once, you’ll see a popup asking whether to:
  - Play the wedding scene, or
  - Skip it (and you can auto-skip via MCM if you already know the drill).

---

## 4. Engagement – Waiting for the Big Day 💒

Now that they’re your fiancé(e):

- They’re in **Engaged** state.
- **Affection now decays over time** if you ignore them. (Welcome to semi-realistic commitment.)

You still have the same ways to **increase affection** as during Candidate:
- Gifting
- Talking via `(M) May I have a word with you?`
- Lover's Ledger intimacy (if present)

### Missing or Ruining the Wedding

If you:
- **Miss the wedding**,
- Or otherwise cancel it (e.g., attacking guests, or ending it via SkyrimNet scenarios, or selecting "(M) We need to talk about... us. (Breakup)"),

Then the NPC moves to **Jilted**.

In **Jilted** state:
- They refuse to talk to you for **1 in-game day**.
- After that, you can approach again and try to fix things.

#### Without SkyrimNet

After 1 day, you can use dialogue:

> **`(M) I made mistakes, and I regret them. I still care - can we start over?`**

If they forgive you, you can work your way back toward engagement and marriage.

#### With SkyrimNet

- After 1 day, you start with the same kind of “we need to talk” prompt, but:
  - The actual forgiveness and second chance comes from **SkyrimNet AI chat**, not a fixed outcome.

---

## 5. Marriage – Managing Your Life Together 🏡

If the wedding goes well (or you skip and confirm), the NPC moves to **Married** state.

You now unlock a new root dialogue:

> **`(M) Can we talk about our life together?`**

This is your main **marriage management hub**, with multiple branches.

---

### 5.1 Cooking 🍲

> **`Would you mind cooking something for me?`**

- Spouse makes you a **homemade dish**.
- Once every **24 in-game hours**.
- They need to be in **your house** to cook.

**Tip:** Great for roleplay and a little extra immersion buff; just don’t spam them like a walking stove.

---

### 5.2 Hierarchy Management (Polygamy Only) 👑

> **`Let's talk about your standing in our home.`**

- Available only if you have **more than one spouse**.
- Lets you assign this spouse to one of the **hierarchy slots**:
  - 1st
  - 2nd
  - 3rd
  - “Everyone else”

This affects **buff multipliers** and **affection**:

- **Promoting** a spouse → affection boost.
- **Demoting** a spouse → affection loss (they do *not* love being “downgraded”).

---

### 5.3 Housing Management – Where You Live & What You Access 🗝️

> **`I want to talk about where we're living.`**

This opens several important options:

#### 1. Accessing Their Home

> **`Is your home open to me as well?`**

You can only ask this if:

- They **haven’t** already given you access.
- Their **affection > 50**.
- They actually **have** a home location.
- You are **not already** in the middle of asking another spouse to share their home.

If conditions are met:

- A mini quest starts: you must **visit the spouse’s home**.
- Once you enter that location, the quest completes and:
  - You gain permanent access: no trespassing, no stealing flags.
  - You can then ask **another spouse** to share their home too (you keep old access).

**Tip:** This is how you “discover” where certain NPCs actually live and legally unlock powerful spaces (like nobles’ homes).

---

#### 2. Giving Up Access

> **`I won't be relying on your house for a while.`**

- You **forfeit access** to that home.
- It becomes a normal private cell again:
  - Items revert to **stealable**.
  - You’re no longer treated as a resident.

If **multiple spouses** share the same location and all gave you access:

- You must forfeit access **with each spouse** before the location fully returns to its original ownership/stealing state.

---

#### 3. Moving Into Your House

> **`I'd like us to move somewhere else.`**

Requirements:

- You own at least **one player home**.
- Spouse affection is **> 25**.

Result:

- You can choose one of your houses.
- The spouse gets an **AI package** to sandbox there:
  - They actually live in your house, wander around, sit, eat, etc.

**Tip:** This is great for keeping track of them… but risky if they’re also important for a quest. See below.

---

#### 4. Letting Them Go Home

> **`You can head back to your own home whenever you'd like.`**

- Removes the override AI package.
- Spouse returns to their **original home/routine**, including quest behavior.

Use this if:
- You moved a jarl, shopkeeper, or quest NPC into your house and now regret your life choices.

---

#### 5. Store Income & Merchant Services 💰

> **`Has the store made any money?`**

Requirements:

- The spouse is assigned to **one of your houses**.
- Their affection is **> 50**.

Effect:

- Once per day, you can collect income from their "small store."
- Income depends on the spouse's **Social Class**:
  - Nobles / wealthy types earn more than beggars.
  - Yes, even a Jarl or East Empire Company figure is technically running a little side hustle for you.

**Merchant Services:**

When a spouse is assigned to one of your houses, they also become a **merchant** (like vanilla spouse merchants), allowing you to buy and sell items with them.

- M.A.R.A.S supports **multiple merchant types** based on the spouse's vanilla merchant faction (if they have one):
  - **Apothecary** – Potions, ingredients, alchemical supplies
  - **Blacksmith** – Weapons, armor, smithing materials
  - **Fletcher** – Bows, arrows, ranged equipment
  - **Hunter** – Pelts, hunting supplies, basic provisions
  - **Innkeeper** – Food, drinks, basic items
  - **Jeweler** – Jewelry, gems, precious metals
  - **Tailor** – Clothing, fabrics, accessories
  - **Spell Merchant** – Spell tomes, scrolls, magical items
  - **Misc Merchant** – General goods (default for spouses without specialized merchant type)

**Shared Merchant Inventory:**

- Each **merchant type shares the same merchant chest** with other spouses of that type.
- Example: If you have 3 Blacksmith spouses assigned to different houses, they all share the same inventory.
- This means selling items to one Blacksmith spouse makes those items available when trading with your other Blacksmith spouses.

**Follower Behavior:**

- If a spouse assigned to your house becomes your **follower**, they will **drop their house sandboxing** and join you.
- When you **release them from following**, they will **return to their assigned house** and resume sandboxing/merchant services.
- This check happens every **15 seconds** for performance reasons, so there may be a 0-15 second delay when asking them to follow or releasing them.

---

#### 6. Gifting (Again)

> **`I found something I thought you might like. (Gift)`**

- Same gifting menu as earlier stages.
- Still increases affection, scaled by value & social class.

---

## 6. Affection in Marriage – Keeping the Flame Lit 🔥

Once married, affection:

- **Decays faster** than during engagement.
- Still gains from:
  - Gifting.
  - Talking via M.A.R.A.S dialogue.
  - OStim intimacy (if used).

Additional affection sources:

- **Sleeping in the same location** as your spouse.
- **Promoting** them in the hierarchy (polygamy).

Additional affection losses:

- **Demoting** them to a lower hierarchy slot.

### You can:
- Maintain marriages by checking in regularly, sharing a bed, and not treating them like stat sticks.

### You can’t:
- Expect a spouse to stay happy if you vanish for weeks and demote them from 1st to “everyone else” like they’re an old sword.

---

## 7. Divorce & “Marriage: The Last Word” 💔

There are multiple ways to end a marriage.

### 7.1 Direct Breakup Dialogue

Use:

> **`(M) We need to talk about... us. (Breakup)`**
> → **`It's over. I'm sorry, but this is the end of us.`**

This triggers a **straightforward divorce**.

---

### 7.2 SkyrimNet-Driven Divorce

If you’re using **SkyrimNet**, your spouse can decide to end it during AI-driven chats.
- M.A.R.A.S will recognize that and move the relationship into **divorced**.

---

### 7.3 Affection Collapse – “Marriage: The Last Word” Quest

If affection drops to **“estranged”**(<25) level:

- The quest **`Marriage: The Last Word`** starts.
- You must go talk to your spouse, beginning with:

> **`(M) I came as soon as I heard. What's going on?`**

From here, outcome depends on whether you use SkyrimNet.

---

#### Without SkyrimNet – Dialogue Outcomes

You get three choices with skill checks:

1. **Persuade – Save the relationship**
   > `"You're right. But I still care about you more than anything. (Persuade)"`
   - **Success**: Affection rises above “estranged.” Marriage survives, but you still need to do work to rebuild it.
   - **Failure**: Immediate divorce.

2. **Intimidate**
   > `"You're overreacting. You'll regret walking away from me. (Intimidate)"`
   - **Success**: Affection goes above “estranged” (though honestly, that’s a… spicy relationship).
   - **Failure**: Divorce.

3. **Accept the end**
   > `"If that's how you feel... maybe there's nothing left to say."`
   - No check, just a **straight, final divorce**.

---

#### With SkyrimNet – AI Negotiation

- After you choose the initial “I came as soon as I heard” line:
  - You must **talk it out via SkyrimNet AI**.
  - You can try to convince them to stay… or agree to part ways.

---

#### Passive Outcomes

There are two non-dialogue ways the quest can resolve:

1. **Repair affection**
   - If you somehow raise affection back above “estranged,” the quest **auto-completes** with the marriage saved.

2. **Ignore them**
   - If you do **nothing for 3 in-game days**, the quest auto-completes with a **divorce**.

---

### 7.4 Divorce settlements & crime fee 💰

Ending a marriage in M.A.R.A.S isn’t just emotional – it can hit your wallet too.

Whenever a marriage actually ends in a **divorce** (doesn’t matter whether *you* ended it, your spouse did, or it came from “Marriage: The Last Word” auto-resolving), you may owe your ex a **divorce settlement**.

#### How it’s delivered

- After the divorce, a **courier** will try to find you with a letter explaining that you owe your ex-spouse money.
- If you dodge or ignore that courier for **3 in-game days**, the settlement is applied **automatically** anyway – no letter required.

Once the letter is delivered (or those 3 days pass), the settlement is converted into a **crime bounty** in your ex-spouse’s hold:

- The bounty is added to the **crime faction for your ex’s home hold**.
- If they don’t belong to a specific hold crime faction, the fee is assigned to **Haafingar** by default.

From that point on:

- Any **guard in that hold** will treat it like a normal bounty:
  - You can **pay it off on the spot**, or
  - Go to **jail** if you can’t afford it (or refuse to pay).
- Guards in other holds don’t care; it’s strictly a local problem.

#### How the fee is calculated

The settlement scales with how established your Dragonborn is. Roughly speaking, it’s:

> **Fee = 100 base gold**
> + **up to 1000 gold** based on **30% of your current gold**
> + **700 gold per house you own**
> + **500 gold per horse you own**
> + **up to 1000 gold** from **quests completed**
> + **up to 1000 gold** from **dungeons cleared**

In detail:

- 30% of your current carried gold is taken and **clamped** between **0 and 1000**.
- Each **house owned** adds **700**.
- Each **horse owned** adds **500**.
- Each **quest completed** adds **100**, but this contribution is capped at **1000** total.
- Each **dungeon cleared** adds **100**, also capped at **1000** total.
- Then **100 gold** is added as a flat base on top of all that.

So wealthy, well-traveled characters will feel the sting a lot more than a fresh adventurer with one shack and a single quest under their belt.

#### Cooldown between divorce fees

To keep things from getting ridiculous if you chain divorces:

- A **divorce settlement can only trigger once every 7 in-game days**.
- If you divorce someone and get a fee, then divorce someone else within **7 days**, **no second fee** is generated.
- After 7 days have passed since the last settlement was applied, the system is ready to charge you again on the next divorce.

In short: divorce is still an option, but it’s no longer a completely **free reset button** for messy marriages.

## 8. Death, Mourning & Inheritance ⚰️

If an NPC tracked by M.A.R.A.S dies, you may get different results depending on their state:

- **Candidate** – No notification.
- **Fiancé(e)** – You receive a **mourning letter**.
- **Spouse** – You receive:
  - A **mourning letter**.
  - An **inheritance**, scaled by their **Social Class**.
- **Jilted or Divorced** – You receive a **mourning letter** as well.

If **you killed them**:

- M.A.R.A.S gives you **nothing**.
- Not even a scolding. Just cold silence. (You know what you did.)

---

## 9. Polygamy & Hierarchy – “The Flame Divides Not” 🔥💞

### Unlocking Polygamy

**Standard Way (Quest):**

Requirements:

1. Be **married**.
2. Consume at least **one dragon soul** (be the actual Dragonborn).
3. **Sleep** anywhere.

This triggers the quest:

> **`The Flame Divides Not`**

This quest is your in-lore explanation for how the Nord gods (and everyone else) tolerate you having multiple spouses.

**Alternative Way (MCM Cheat):**

If you prefer to skip the quest, you can enable polygamy directly via the MCM:

- Go to **MCM > M.A.R.A.S > Settings**.
- Under **Cheats**, toggle **"Enable polygamy (bypasses quest)"** to **ON**.

This instantly unlocks polygamy without needing to complete the quest or meet the requirements above.

**Once Unlocked (Either Way):**

- You can go to as many NPCs as you want, use:
  - `(M) Ever think we could be something more?`
  - And eventually propose as usual.

All the same rules apply, just… multiplied. 😅

---

### Life With Multiple Spouses

When you have more than one spouse:

- Every spouse has their own **affection bar**.
- Every fiancé(e) also still has their own affection.
- You now manage a **small emotional kingdom**.

### Hierarchy System

Using:

> **`Let's talk about your standing in our home.`**

You can place each spouse in:

- **1st**
- **2nd**
- **3rd**
- **Everything else**

These slots determine buff **multipliers** (from the main mod design) and also drive affection changes:

- **Promote** → affection up.
- **Demote** → affection down (proportionally salty).

Expect:
- More power from your top slots.
- More drama if you constantly reshuffle the rankings like it’s a reality show.

---

## Quick Do/Don’t Recap ✅❌

### You can:
- Turn any NPC into a Candidate and woo them over time.
- Propose, get engaged, and enjoy (or skip) the vanilla wedding.
- Manage homes, access spouse houses, and pull in store income.
- Unlock polygamy and build a whole marriage hierarchy.
- Save a failing marriage with persuasion, intimidation, or heartfelt AI chats (SkyrimNet).

### You can’t:
- Grind infinite affection in one sitting.
- Ignore engaged or married partners forever without consequences.
- Keep all spouses in top hierarchy slots at once; someone is always “less favored.”
- Kill your tracked NPC and expect inheritance or letters of sympathy.
- Move quest-critical NPCs into your house and assume all quests will be totally fine. (Use “You can head back to your own home whenever you'd like” if things get weird.)

Go forth, Dragonborn. Date responsibly. Or irresponsibly—M.A.R.A.S will just track the consequences. 😉
