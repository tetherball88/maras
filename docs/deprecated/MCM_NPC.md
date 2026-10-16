# M.A.R.A.S – NPC Explorer MCM Page 🧾💘
*Poke at your NPCs’ souls, stats, houses, and beds. Respectfully. Or not.*

The **NPC Explorer** MCM page is your one-stop “x-ray view” for any NPC registered with M.A.R.A.S.
Here you can:

- Inspect their **status**, **affection**, **archetype**, **marriage chances**, and **buffs**
- See where they **live**, which beds they **own**, and whether you have **access**
- Manually tweak things when roleplay or debugging calls for a little “divine intervention”

It’s basically a **character sheet + relationship inspector + light cheat console** for each NPC.

---

> **Background reading**
>
> For more context on what you’re editing here:
>
> - [Affection System – Deep Dive](AFFECTION_SYSTEM.md) – what the affection slider represents and how it changes over time.
> - [Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md) – full explanation of Social Class, Skill Type, Temperament, and the buffs they drive.
> - [Player Guide](PLAYER_GUIDE.md) – how status (candidate, engaged, jilted, married, divorced) and hierarchy ranks affect quests and outcomes.
> - [Advanced Configuration & Modding](ADVANCED_CONFIGURATION_AND_MODDING.md) – permanent JSON archetype overrides and Papyrus hooks, if you want changes to apply across saves.

---

## How This Page Works

You usually reach this page **after selecting an registered NPC** from the main **Explore** tab / NPC list.

At the top you’ll see:

- A button to **return back to Explore**
- A big header like:
  `X's data`

From there, the page is split into a **left column** (data & settings) and a **right column** (homes & beds).

---

## Left Column – Who They Are To You 💑

### 🔙 Return to Explore

- A simple text option: **“Return to explore”**
- Select this to go back to the main **NPC list** / Explore view.

Great for: hopping between multiple NPCs to check their affection, rank, or housing setup.

---

### 🧬 Status & Life State

You’ll see a line like:

- **“Their status is”** → `candidate`, `engaged`, `married`, `jilted`, `divorced`, etc.

This tells you where they sit in the **M.A.R.A.S relationship pipeline**.

---

### 👑 Hierarchy Rank (Spouses Only)

If the NPC is **married to you** and **alive**, you’ll see a **Hierarchy Rank** field:

- If they are your **only spouse**:
  - Shows: **“The only one!”**

- If you have **multiple spouses**:
  - Rank can be:
    - **1st spouse** – full permanent buff
    - **2nd spouse** – 50% permanent buff
    - **3rd spouse** – 25% permanent buff
    - **4th+ spouse** – treated as “4th+” and gives 10% permanent buff
  - This appears as a **menu** you can click to adjust rank.

**Tooltip explains:**

- How much of their **permanent buff** they contribute at this rank.
- That a single spouse = **200%**, multiple spouses share the spotlight.

Use it to:

- Put your most important buff spouses higher in the hierarchy.
- Roleplay “favorite spouse energy” (and suffer the affection consequences when you shuffle ranks).

---

### 💍 Engagement Chances (Non-Spouse / Non-Fiancé)

If the NPC is **not engaged** and **not married** to you, you’ll see:

- **“Your chances to get engaged:”** → some percentage.

This is calculated by M.A.R.A.S’s full marriage difficulty logic (prestige, affection, guild alignment, etc).

**Tooltip flavor text** gives you a SkyrimNet vibe check:

- 95%+ → “accepts as though it were fated—unquestioning and unwise”
- 80%+ → eager to accept
- 60%+ → would probably accept if approached kindly
- 45%+ → on the fence, might be swayed
- 25%+ → unlikely, needs exceptional persuasion
- Below that → “almost certainly refuse, though nothing is truly impossible”

Perfect for deciding **when** to pop the question (or whether you should wait and grind affection first).

---

### ❤️ Affection Slider

- Shown as: **“Affection:”** with a slider from **0% to 100%**.
- This is the **permanent affection** used for:
  - Marriage chances
  - Buff strength
  - Relationship thresholds (happy/content/troubled/estranged)

You can:

- Adjust it directly (0–100) if you want to **cheat**, fix a bug, or story-tell something dramatic.

**Tooltip explains:**

> Higher scores unlock happier relationship events; lower scores risk estrangement.

Use responsibly. Or don’t. The gods are watching, but they’re not judging.

---

### 🧠 Archetype Sliders – Social Class, Skill Type, Temperament

You’ll see three menu options:

- **Social class:**
  Shows the NPC’s current **social class** (Outcast, Poverty, Working, Middle, Wealthy, Religious, Nobles, Rulers).

- **Skilled as:**
  Shows their **skill type** (Warrior, Mage, Rogue, Craftsman, Ranger, Orator).

- **Temperament:**
  Shows their **temperament** (Proud, Humble, Jealous, Romantic, Independent).

Each of these is:

- Clickable → opens a menu with all valid options.
- **Changing them** will directly overwrite the NPC’s archetype for M.A.R.A.S.

**Tooltips:**

- **Social class:** displays your spouse’s **permanent buff** (what they give you just by being married).
- **Skill type:** displays the **follower buff** (what they give you when they’re following).
- **Temperament:** explains it shapes affection gains/losses, jealousy, and reactions.

Use this page to:

- Fix weird auto-detections.
- Re-flavor an NPC to better match how *your* headcanon sees them.
- Turn your favorite miner into a Noble, if you’re in that mood.

---

### 🎭 Use NPC's own voice in dialogues

This adds/removes the `TTM_UseNpcVoiceInMisc` keyword to the NPC, which tells M.A.R.A.S to let them use their own voice instead of the voiceless dialogue line when they would normally be forced into it.

### 🏠 Assigned Player’s Home (Spouses Only)

If the NPC is **married** and **alive**, you’ll see:

- **“Assigned player's home:”** → either the name of your house or `unset`.

You can:

- Assign them to one of your player homes from a menu.
- Or set them to `unset` to **release** them back to their usual life.

**Tooltip warns:**

- They’ll **spend time in that house** once assigned.
- This can interfere if they **should** be somewhere else for a quest.
- You can unassign them here or through dialogue later.

Great for:

- Making sure you know where to find your spouse.
- Building a cozy “everyone lives in the same house” vibe.
- Making a quest NPC vanish from their throne room if you’re not careful. 😅

---

### 🗝️ Share Their Home With Player (Spouses Only)

If the NPC is **married**, you get options related to **their original home**:

- If they have **no home**:
  - A text line saying they don’t have a place they can call home.

- If a share-home quest is already running:
  - A text line telling you you can’t start another share-home right now.

- If they have a home and nothing is blocking it:
  - A toggle:
    **“Share their home with player:”** (for living spouses)

- If they have no home or were killed by you:
  - A message like: they have no home or you can’t use their property.

**Tooltip explains:**

- Enabling starts a quest to go **inspect their home**.
- Once done, you get **permanent access** to that place → no trespass/stealing.
- Disabling removes that access and returns it to a normal private space.
- If another share-home quest is already running or they truly have no home, toggling does nothing.

Perfect for:

- Legally unlocking noble manors or weird side houses.
- Inheriting a dead spouse’s home (assuming you didn’t kill them yourself).
- Being a classy property thief… by marrying into the deed.

---

## Right Column – Houses & Beds 🛏️🏰

The right side of the NPC Explorer page focuses on **what they own**.

### 🏡 Their Property Overview

If the NPC is **not married** to you:

- You’ll see:
  `(Name) is not married to you and can't share anything with you.`

If they **are married**:

- You’ll see a header:
  **“(Name)'s property:”**
- Then:
  - Their **original house** name.
  - Whether it is:
    - **“(shared with you)”**
    - or **“(not shared with you)”**

If they **don’t have any home** at all:

- It will say they don’t have their own home, and that’s it for this column.

---

### 🛏️ Their Beds

If they have a home, the page also lists:

- A header:
  **“(Name)'s beds:”**
- All **beds** that belong to this NPC, each with:
  - Bed name + `(shared with you)` or `(not shared with you)`, matching the home’s share state.

If they own **no personal bed**:

- You’ll see a line that they don’t have any personal bed.

This is mostly for:

- Debugging and curiosity (“Wait, who owns this bed?”).
- Confirming that your access to their home also gives you proper, non-stealing access to their bed.

---

## What This Page Is Great For ✅

- Checking **who an NPC is** to M.A.R.A.S: status, affection, archetype.
- Seeing your **engagement odds** before you risk emotional embarrassment.
- Fine-tuning **hierarchy ranks** for buff min-maxing.
- Adjusting **affection** when you:
  - Want to repair a bugged relationship
  - Or instantly turn a stranger into a soulmate for a story run
- Investigating and managing **house sharing**, **inherited property**, and **beds**.
- Re-flavoring NPCs to better match your headcanon (change their class/skills/temperament).

---

## What It Can Mess Up If You Go Wild ❌

- Changing **Social Class / Skill Type / Temperament** can alter their buffs and affection behavior.
- Assigning or moving a **quest-critical NPC** to a player home may make them skip where quests expect them.
- Forcing affection/rank combos can make relationships feel less “earned” (but hey, it’s *your* Skyrim).

Treat it like a **Divine Edit Mode** for your relationships:
incredible for storytelling and debugging, dangerous if you just mash buttons to see what happens.
Which, let’s be honest, you’re probably going to do at least once. 😉
