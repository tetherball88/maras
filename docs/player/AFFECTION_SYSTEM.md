# ❤️ The Affection System — Keeping the Spark Alive (Or Speed-Running a Divorce)

In vanilla Skyrim, marriage is simple: wear a shiny necklace, chop some firewood once, get married, and then leave your spouse staring at a wall in Breezehome for the next 400 hours while you fight dragons. 🐉

**M.A.R.A.S** says: *"Yeah, no."*

Under the hood, M.A.R.A.S runs a living **Affection System (0–100)** for everyone in your romantic life. Treat your partners well and they'll shower you with stronger combat buffs, more store gold, and warm greetings. Ignore them for weeks, demote them in your household hierarchy, or get caught having an affair with the local bard, and you'll come home to cold shoulders—or a courier handing you divorce papers. 📜💔

---

> **📖 Other Handy Player Guides**
> - [💍 Player Guide](PLAYER_GUIDE.md) — The complete walkthrough of courting, marriage, households, and shortcuts.
> - [🖥️ UI Dashboard Guide](UI_DASHBOARD_GUIDE.md) — How to manage everything from the in-game Dashboard (`Alt + B` by default).
> - [💞 Relationship Overview](RELATIONSHIP_OVERVIEW.md) — Dating, lovers, open relationships, cheating drama, and NPC matchmaking.
> - [🎭 Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md) — How your partner's Social Class, Skill Type, and Temperament power up your character.
> - [👶 Kids, Adoption & Fertility Guide](FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md) — Pregnancies, babies, and family life.
> - [🤖 SkyrimNet AI Guide](SKYRIMNET_INTEGRATION_GUIDE.md) — Voice/text AI romance, actions, and memories.

---

## 1. What Does Affection Actually Do? 🤔

Every NPC you've registered in M.A.R.A.S has an **Affection score from `0` to `100`** (starting around `50`—neutral *"you seem alright, I guess"* territory). You can check anyone's current score and mood at any time by pressing **`Alt + B`** (default) to open the **M.A.R.A.S Dashboard** (`Prisma UI`).

Your affection score directly controls:
- 💍 **Proposal Odds**: Asking someone to marry you at `20` Affection is basically asking for public humiliation. Asking at `85` Affection makes them much more likely to say *"Yes!"*
- 💪 **Spouse Buff Strength**: Happy spouses literally make you stronger (`1.25x` buff power!). Neglected spouses nerf your buffs down to `0.25x` or shut them off completely (`0x`).
- 💰 **Store Gold & Home Perks**: Happy spouses generate **25% more daily gold** from their businesses and are eager to cook meals and share their homes with you.
- 🗣️ **SkyrimNet AI Attitude**: If you use SkyrimNet, your partner's exact affection mood is fed directly into their AI personality—so an Estranged spouse will *sound* fed up with your excuses.

---

## 2. Today's Vibes vs. True Love (Why You Can't Bribe Your Way to 100 in 5 Minutes) ⏳

M.A.R.A.S splits affection into two layers so you can't just dump 50,000 gold on someone at 2:00 PM and marry them at 2:03 PM:

1. **🌤️ Daily Affection ("What have you done for me lately?")**:
   - Everything you do during a single day—chatting, giving gifts, sleeping in the same house, getting intimate—goes into **daily buckets**.
   - Each bucket has a **daily cap**. Once you hit the cap for gifts today, giving them a 17th diamond necklace won't add any more points until tomorrow.
2. **💖 Permanent Affection ("How do I actually feel about you?")**:
   - When the clock strikes midnight and a **new in-game day** begins, all of yesterday's daily buckets are added to your **Permanent Affection (`0–100`)**, and the daily buckets reset to zero.
   - Major life events (like having a baby together or getting caught cheating!) skip the daily buckets and hit your **Permanent Affection** immediately.

---

## 3. Daily Ways to Win (or Lose) Their Heart 📅

Here is how your everyday actions stack up, and how your partner's **Temperament** (personality) changes the math:

| Daily Activity | Daily Cap | How It Works |
| :--- | :---: | :--- |
| 🗣️ **Checking In (Dialogue)** | **`0` to `+5`** | Just starting a conversation gives **`+1`** per chat (up to 5 times a day). Easiest way to say *"Hey, I'm still alive!"* |
| 🎁 **Giving Gifts** | **`0` to `+15`** | Based on the item's gold value and their **Social Class**. **Romantic ❤️** partners get **`1.5x`** bonus points from gifts! |
| 🛏️ **Sleeping Nearby** | **`0` to `+8`** | Sleeping in the same home/area gives **`+3`** by default (**`+5`** for **Romantic ❤️**, **`+4`** for **Jealous 👀**, **`+1`** for **Independent 🐺**). |
| 🔥 **Intimate Moments** | **`-22` to `+22`** | Intimate scenes (OStim) scale with personality: **Romantic ❤️** & **Jealous 👀** get **`1.5x`**, while **Independent 🐺** gets **`0.5x`**. |
| 👑 **Hierarchy Promotions / Demotions** | **`-24` to `+24`** | Moving a spouse up or down in your Spouse Hierarchy gives **`+8` / `-8`** per rank step (**`+12` / `-12`** for **Proud 👑**, **`+4` / `-4`** for **Humble 🌱**). |
| 🧭 **Adventuring vs. Loneliness** | **`-16` to `+6`** | Traveling together as a follower gives **`+6`/day**! Leaving them behind for more than 2 days causes **Loneliness Decay** (`-1` to `-4`/day base, modified by stage, open status, or unresolved cheating). |

### 💎 A Quick Note on Gift Snobbery
Your partner's **Social Class** determines how hard they are to impress with shiny things. Handing a **1,000 gold** gem to an **Outcast** or **Poverty** partner will blow their mind (**`+15` points**, maxing out the daily gift cap in one go!). Handing that same gem to a **Noble** or **Ruler** gets you a polite nod (**`~6` points**)—*"Oh, how quaint, another ruby for the pile."*

---

## 4. Loneliness, Polyamory & The Follower Hack 🏕️

Once you're in a committed relationship, your partners actually expect to see you occasionally. Shocking, right?

### The 2-Day Grace Period
If you've interacted with a partner within the last **2 in-game days**, they lose **zero** affection. Go clear that dungeon!

### What Happens After 2 Days of Radio Silence?
If you vanish for 3+ days without visiting, daily **Loneliness Decay** kicks in based on several factors:

1. **Their Personality (Temperament)**:
   - 🐺 **Independent (`-1`/day)**: *"Oh, you're back? Cool. I was busy doing my own thing anyway."*
   - 🌱 **Humble (`-2`/day)**: *"I missed you, but I know saving Skyrim takes time."*
   - 👑 **Proud / 👀 Jealous (`-3`/day)**: *"Three days without a word? Who were you with?!"*
   - ❤️ **Romantic (`-4`/day)**: *"I stared out the window in the rain for 72 hours wondering if you still loved me!"*
2. **How Serious You Two Are**:
   - 💍 **Married Spouses** feel **`100%`** of the loneliness penalty.
   - 💎 **Fiancés / Fiancées** feel **`75%`** of the penalty.
   - 🌹 **Dating Partners** feel **`50%`** of the penalty.
   - 🔥 **Casual Lovers** only feel **`25%`** of the penalty.
   - 👀 **Candidates** (not dating yet) feel **`0%`** decay—they'll wait forever!
3. **Open Relationship Status (By Temperament)**:
   - If your committed relationship (`Dating`, `Engaged`, or `Married`) is **Open**, how they handle time apart shifts with their personality:
     - 🐺 **Independent (`0.50x` decay)**: Happily enjoying their freedom!
     - 🌱 **Humble (`0.75x` decay)** & 👑 **Proud (`0.90x` decay)**: More relaxed about time apart.
     - ❤️ **Romantic (`1.15x` decay)** & 👀 **Jealous (`1.35x` decay)**: Even though they agreed to an open relationship, being left alone makes them overthink and lose affection faster!
4. **Known Ongoing Affairs (By Temperament)**:
   - If either you or your partner has an **active, discovered affair** that hasn't been resolved yet, leaving things festering while you're away accelerates daily decay:
     - 🐺 **Independent (`1.25x`)** | 🌱 **Humble (`1.35x`)** | ❤️ **Romantic (`1.50x`)** | 👑 **Proud (`1.75x`)** | 👀 **Jealous (`2.00x`)**

### 🛡️ The "Take Them With You" Hack (`+6`/day!)
If a partner is actively **following you** as a companion, they **never** get lonely. In fact, spending the day adventuring together gives them **`+6` Affection every single day** (plus a **`1.5x` boost** to their spouse buffs!).

### 🏘️ Big Household Relief (Polygamy Mercy Rule)
Running a massive household with 8 spouses across 3 castles? M.A.R.A.S won't punish you for not having a time machine. Loneliness decay is automatically softened the more partners you have:
- **1–2 Partners**: Normal decay (`100%`)
- **3–4 Partners**: Decay reduced to **`75%`**
- **5–6 Partners**: Decay reduced to **`60%`**
- **7+ Partners**: Decay cut in **half (`50%`)**!

> [!TIP]
> **Want to turn off decay completely (or make it twice as brutal)?**
> Open the **[M.A.R.A.S Dashboard](UI_DASHBOARD_GUIDE.md) (`Alt + B`) $\rightarrow$ Settings** tab and adjust the **Affection Decay Multiplier** slider anywhere from **`0.0` (Zero Decay — Purely Cozy Mode)** to **`2.0` (Hardcore Relationship Simulator)**.

---

## 5. Big Life Events: Babies, Open Relationships & Cheating Drama 🎭

Some moments are too huge for daily caps and hit your partner's **Permanent Affection** right away (further amplified by their personality!):

### 👶 Family & Children Milestones
> [!NOTE]
> **Mod Requirement**: By default in base Skyrim + M.A.R.A.S, only **Vanilla Adoption** is available. Pregnancy, childbirth, and children growing up require either **Fertility Mode Reloaded (FMR)** or **BeeingFemale** installed!

- **Adopting a Child Together**: **`+20` Affection** *(works out of the box with Vanilla/Hearthfire Adoption)*.
- **Conceiving a Baby**: **`+20` Affection** *(requires Fertility Mode Reloaded or BeeingFemale)*.
- **Giving Birth**: **`+25` Affection** (plus **`+10`** for each additional twin or triplet!).
- **Child Growing Up**: **`+10` to `+15` Affection** as your child reaches childhood and adulthood milestones.

### 🔓 Open Relationships (Know Your Partner First!)
Asking a partner to open up your relationship has wildly different results depending on who you're asking:
- 🐺 **Independent**: **`+15` Affection** — *"Finally! Freedom for both of us!"*
- 🌱 **Humble / 👑 Proud**: **`-10` Affection** — Not thrilled, but they'll hear you out.
- ❤️ **Romantic**: **`-20` Affection** — You just stepped on their daydream of exclusive true love.
- 👀 **Jealous**: **`-35` Affection** — Congratulations, you just pulled the pin on an emotional grenade. (And even if they agree, if a Jealous partner physically walks in on you with someone else, expect another **`-10`** hit!)

### 🗡️ Infidelity, Affairs & Secret Paternity Bombshells
- **Caught Cheating Red-Handed**: **`-35` Affection** immediately.
- **Confessing to an Affair**: **`-30` Affection** (honesty hurts, but slightly less than getting exposed).
- **Having Your Secret Affair Exposed**: **`-50` Affection**—half their love bar vanishes on the spot!
- **Secret Paternity Revealed**: If your partner discovers that the baby in the cradle secretly belongs to someone else, expect a massive **`-30` to `-40` Affection** drop (whereas a biological father learning he has a child gains **`+15`**).

---

## 6. The Four Mood Tiers & *"Marriage: The Last Word"* ⚖️

Your partner's Permanent Affection (`0–100`) places them into one of four emotional moods, which directly multiplies the **Spouse Buffs** and **Daily Store Gold** they give you:

| Mood Tier | Affection Score | Buff & Gold Multiplier | How Things Are Going |
| :--- | :---: | :---: | :--- |
| 😀 **Happy** | **`75 – 100`** | **`1.25x` (+25% Bonus!)** | Peak marital bliss! Extra strong buffs, extra store gold, and glowing greetings. |
| 🙂 **Content** | **`50 – 74`** | **`1.00x` (Normal)** | Solid, comfortable relationship. Everything works at full baseline strength. |
| 😕 **Troubled** | **`25 – 49`** | **`0.25x` (-75% Penalty)** | Uh-oh. They feel neglected or hurt. Your buffs and store income drop by three-quarters! |
| 💔 **Estranged** | **`0 – 24`** | **`0.00x` (Shut Off!)** | **Crisis Mode!** Zero buffs, zero gold, and the marriage is about to implode. |

### 🗣️ How Affection Shapes Vanilla Dialogue Negotiations & Confrontations
Even before a marriage hits rock bottom, your partner's Affection directly tips the scales whenever you navigate tense relationship talks using **standard (vanilla) dialogue options**:
- **Confronting Suspected Cheating**: When you ask a partner if they've been faithful (to uncover whether they are secretly having an affair), or **demand that they end an affair**, your standing with them matters!
- **Opening or Closing the Relationship**: Proposing to open up your relationship—or asking to close it back down—also checks how much they currently love and trust you.
- **Dice Rolls & Speech Checks**: Depending on the situation, these dialogue branches use either **behind-the-scenes dice rolls** (where high affection stacks the odds in your favor and low affection works against you) or **Persuasion checks** (where a `Happy` partner requires a much lower **Speech** skill to convince, while a `Troubled` or `Estranged` partner demands silver-tongued mastery).

> [!NOTE]
> **Vanilla Dialogues vs. SkyrimNet AI Chat**: These dice-roll and Speech-threshold modifiers apply specifically to **built-in vanilla dialogue choices**. If you use **SkyrimNet**, those conversations play out organically through AI roleplay instead!

### 🚨 What Happens at *Estranged*? ("Marriage: The Last Word")
When a spouse's affection drops into **Estranged (`0–24`)**, the quest **"Marriage: The Last Word"** begins:
1. You have a limited window to track down your spouse and **have the big talk**.
2. You can try to **save the marriage** (via Persuasion/Intimidation or by genuinely talking things through in SkyrimNet AI dialogue and bringing their affection back up).
3. If you ignore the ultimatum—or fail to win them back—they will **divorce you**.
4. **Watch Your Wallet**: When a marriage ends in divorce, your ex-spouse may file for a **Divorce Settlement**—delivered by courier (or slapped onto your Hold Bounty if you dodge the courier for 3 days!) scaled to your wealth and properties. 💸

---

## 7. Quick Survival Cheat Sheet 📝

- **Check the Dashboard**: Open the **M.A.R.A.S Dashboard** (`Overview` or `NPC Detail`) whenever you want to see everyone's affection bar and how many days it's been since you last visited.
- **Bring Your Favorites Along**: Making a spouse your active follower turns loneliness decay into a free **`+6`/day** affection generator *and* gives you a **`1.5x`** buff boost.
- **Don't Demote a Proud Spouse Without a Plan**: Dropping a **Proud** spouse from Lead (`Rank 1`) down to `Rank 3` costs `-24` Affection in a single day—have some shiny gifts ready!
- **Tune It Your Way**: If relationship upkeep ever feels too stressful (or too easy!), slide the **Affection Decay Multiplier** in **Settings** until it fits your playthrough. 😉
