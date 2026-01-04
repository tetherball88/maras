# M.A.R.A.S – MCM Settings Guide 🎚️💍
*Where you fine-tune your love life, your logs, and your shameless cheating.*

This page explains what each **MCM option** does in M.A.R.A.S so you know exactly which sliders you’re messing with (and how much chaos you’re inviting).

---

> **Before you dig into every toggle…**
>
> If you’re looking for:
>
> - **How to actually play** with these systems → see the [Player Guide](PLAYER_GUIDE.md).
> - **How affection and buffs work under the hood** → see [Affection System – Deep Dive](AFFECTION_SYSTEM.md) and [Archetypes & Buffs](ARCHETYPES_AND_BUFFS.md).
> - **Other MCM pages**:
>   - [NPC Explorer MCM](MCM_NPC.md) – per-NPC editing and inspection.
>   - [Current Buffs MCM](MCM_BUFF.md) – read-only summary of all active buffs.
>
> This page is only about **global MCM switches** (dialogue, AI, logs, cheats), not per-NPC tweaking.

---

## Left Column – Relationship & Dialogue Settings 💬

### 🧷 Settings

#### **Skip wedding ceremonies after first?**

**What it does:**
If enabled, you only have to sit through the **full vanilla wedding ceremony once**.
After that, future marriages will **skip the Wedding Ceremony quest** and just handle the "we're married now" part.

- **Enabled:**
  - First marriage → full scene.
  - All later marriages → no ceremony, just results.
- **Disabled:**
  - Every marriage runs the vanilla wedding quest and ceremony.

**Good for you if:**
- You've seen the wedding scene 47 times already.
- You're running a serial spouse dynasty and don't want to camp in Riften forever.

---

#### **Daily affection decay multiplier**

**What it does:**
Controls how fast (or if) affection decays for **fiancés and spouses** when you haven't interacted with them for more than 2 in-game days.

- **Range:** 0.0 to 2.0
- **Default:** 1.0

**Values:**
- **0.0** – Decay **completely disabled**. Affection never decreases from loneliness, only from active negative actions (demotions, etc.).
- **1.0** – **Default decay rate**. Standard relationship maintenance as designed.
- **2.0** – **Double decay**. Relationships deteriorate twice as fast, turning M.A.R.A.S into a Sims-like relationship management challenge.

**How it affects gameplay:**
- At **0.0**: You can ignore your spouses indefinitely without affection loss. Relationships are purely gain-based.
- At **1.0**: Standard experience. You need to check in on spouses periodically (gifts, talking, sleeping together) to maintain affection.
- At **2.0**: High-maintenance mode. Managing multiple spouses becomes significantly more challenging as they lose affection faster.

**Good for you if:**
- **0.0** – You want relationships without the "neglect penalty," or you're doing a playthrough where you're frequently away for long stretches.
- **1.0** – You want the intended balanced experience.
- **2.0** – You want a challenging relationship simulator where maintaining a large harem requires serious dedication.

> **Note:** This multiplier applies to all loneliness decay calculations. The base decay values are still influenced by temperament (Romantic, Humble, Independent, etc.) and spouse count, but the final result is multiplied by this setting.

---

### 🧠 Dialogues

#### **Show start dialogue to genders:**

Controls which NPCs get the special **M.A.R.A.S starter line**
`(M) Ever think we could be something more?`

Options:
- **opposite** – Only NPCs of the **opposite gender** (from your character’s perspective).
- **same** – Only NPCs of the **same gender**.
- **all** – Everyone. If it breathes and walks, it’s a potential Candidate.

**Use this to:**
- Restrict romance prompts to your preferred playstyle.
- Open things up to “chaos mode” with **all**.

---



### 🗣️ AI Dialogue Settings

> These options only available if **SkyrimNet** is installed. Without it,  toggles will be disabled and do nothing fancy.

- **Use vanilla voiced dialogues if possible**
  Some M.A.R.A.S lines reuse **existing vanilla voice files** (for NPCs who already had marriage/relationship lines in the base game).
  - When this option is **enabled**, the mod will *always try* to use those vanilla voiced lines first.
  - If no vanilla audio exists for that line, it falls back to either **AI-generated audio** (if allowed) or a **silent subtitle**.

- **Use AI generated responses in dialogues**
  This controls whether the mod is allowed to **ask SkyrimNet to generate responses** for certain dialogues.
  - When **enabled**, applicable lines can use **AI-generated responses**.
  - When **disabled**, those lines will always use **silent fallback dialogues** (no AI improv, just the written line).

- **Min relationship rank for dialogue**
  Sets the minimum relationship rank required for NPCs to show the initial romance dialogue `(M) Ever think we could be something more?`.
  - Relationship ranks range from **-4 (arch enemy)** to **4 (lover)**.
  - Default: **-4**
  - Use this to restrict romance prompts to NPCs you have a positive relationship with, or open it up to enemies for dramatic roleplay.

> These toggles only truly **come alive together** when you use them with the **SkyrimNet** mod.

---

### ✅ Actions Confirmation

> These options only available if **SkyrimNet** is installed. Without it, the toggles will be disabled.

When using SkyrimNet, the AI can sometimes suggest or trigger major relationship actions during conversations. These toggles control whether the game asks you to confirm before those actions actually happen.

- **Confirm Accept Marriage Proposal action**
  - When **enabled**: If the AI conversation leads to the NPC accepting your marriage proposal, you'll get a confirmation prompt before the engagement actually occurs.
  - When **disabled**: The AI can directly accept proposals and move the NPC to engaged status.
  - Default: **Enabled**

- **Confirm Breakup Engagement action**
  - When **enabled**: If the AI conversation leads to breaking off an engagement, you'll be asked to confirm before it happens.
  - When **disabled**: The AI can directly break engagements during conversations.
  - Default: **Disabled**

- **Confirm Divorce action**
  - When **enabled**: If the AI conversation leads to a divorce, you'll get a confirmation prompt before the marriage ends.
  - When **disabled**: The AI can directly divorce you during conversations.
  - Default: **Disabled**

- **Confirm Divorce action when affection is low**
  - When **enabled**: During the "Marriage: The Last Word" quest (when affection drops to estranged), you'll be asked to confirm before the AI finalizes a divorce.
  - When **disabled**: The AI can complete the divorce without confirmation during low affection scenarios.
  - Default: **Disabled**

**Use these if:**
- You want to stay in control of major relationship decisions, even when using AI conversations.
- You want a safety net to prevent accidental relationship changes during roleplay.

**Disable these if:**
- You prefer fully AI-driven relationship outcomes without interruptions.
- You trust the AI to make dramatic decisions on your behalf for maximum immersion.

---

### 🎛️ How the Combinations Behave

| Use Vanilla voiced | Use AI generated | Result                                                                                                 | Notes                                                                                                                                      |
|--------------------|------------------|--------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------|
| Enabled            | Enabled          | Uses **vanilla voiced lines** whenever they exist (fast but static). Otherwise, uses **AI responses** (dynamic, but slower). | A good “hybrid” mode: classic when possible, AI when needed.                                                                               |
| Enabled            | Disabled         | Uses **vanilla voiced lines** when possible (fast but static). Otherwise, uses **silent fallback** (fast but static and silent*). | **Default**. If SkyrimNet is used with [DVBO](https://www.nexusmods.com/skyrimspecialedition/mods/84329), the silent fallback can be voiced by TTS. |
| Disabled           | Enabled          | **Always** uses AI-generated responses where supported (dynamic, but slower).                         | Full “AI takeover” mode – best if you want every line to feel fresh and custom.                                                            |
| no SkyrimNet       | no SkyrimNet     | Uses **vanilla voiced lines** when possible (fast but static). Otherwise, uses **silent fallback** (fast, static, and silent*). | Here the fallback is **truly silent**, since there’s no SkyrimNet/AI to fill the gap.                                                     |

\* “Silent fallback” means the dialogue shows up as text/subtitles, but there’s **no voice audio** unless another mod (like DVBO) steps in to synth it.

In short:
- Want classic Skyrim with a bit of extra logic? → **Vanilla: Enabled, AI: Disabled**
- Want vanilla where possible, AI when it can help? → **Both Enabled**
- Want maximum AI personality everywhere? → **Vanilla: Disabled, AI: Enabled**


---

## Right Column – Logs & Cheats 🛠️

### 📜 Logging

These options are for people who like to **debug**, **mod**, or just want to see what the marriage engine is thinking behind the scenes.

#### **Log level**

Controls how much detail M.A.R.A.S writes into its logs.

Options:
- **trace** – Maximum detail, every tiny step. Great for debugging, noisy for normal play.
- **debug** – Still very detailed, but slightly less spammy than trace.
- **warning** – Only logs warnings and above. This is the **default**.
- **error** – Only logs serious issues.
- **none** – No logging at all; M.A.R.A.S goes silent.

**Recommendation:**
- **Normal players:** leave it on **warning**.
- **Modders / bug hunters:** use **debug** or **trace** when you’re hunting weird behavior.

---

#### **Log destination**

Where the logs actually **show up**.

Options:
- **file** – Writes logs to files (standard mod debugging style). Good for long-term diagnostics.
- **console** – Sends logs to the in-game console/output (depending on your setup).
- **floating** – Shows logs as **floating on-screen messages**.

**Use cases:**
- **file:** long sessions, bug reports, mod conflict hunting.
- **console:** quick testing.
- **floating:** “I want to see what’s happening *right now* without tabbing out.”

---

### 🧨 Cheats

#### **Enable always success for engagement**

This is the **"I am the Dragonborn, I do not roll dice for love"** button.

**What it does:**
- When enabled, **engagement attempts always(almost for SkyrimNet) succeed** regardless of:
  - Affection
  - Prestige
  - Guild alignment
  - Jilted/divorce history
  - All that careful balance logic from the main system

If you ask **"Will you marry me?"** – the answer is effectively "Yes, of course," every time.

> For a detailed breakdown of the marriage difficulty calculation that this cheat bypasses, see [Marriage Difficulty Configuration Guide](MarriageDifficultyConfig.md).

**Perfect if:**
- You just want to test downstream features (weddings, buffs, polygamy) quickly.
- You're running a sandbox save where everyone is contractually obligated to be your fiancé(e).

**Not recommended if:**
- You want any semblance of challenge or roleplay.
- You enjoy the feeling of *earning* that "Yes."

---

#### **Enable debug spell**

**What it does:**
- Grants (when enabled) — or removes (when disabled) — a debug spell to the player. This spell, when cast on an NPC, will **cycle that NPC's MARAS status** in the following order: **unknown → candidate → engaged → married → divorced**.

**Use cases:**
- Quickly test the full relationship flow without needing to play through all intervening steps.
- Test wedding/engagement events, NPC behaviors, and related quests at specific relationship states.

**Warnings:**
- This is **strictly a debugging tool**. Running it on NPCs in a live playthrough can disrupt your intended roleplay and may cause unintended side effects (quests, factions, or asset sharing may be altered). Use on test saves where possible.

---

#### **Enable polygamy (bypasses quest)**

**What it does:**
- When enabled, allows **multiple marriages** (polygamy) without requiring the player to complete the in-game polygamy quest.
- This bypasses the need to obtain and translate the ancient scroll, collect dragon souls, or follow the quest steps.
- Once toggled on, the player can marry multiple NPCs immediately, as if the polygamy feature had been unlocked through the quest.

**Perfect if:**
- You want to enable polygamy quickly for testing or roleplay purposes.
- You're playing a save where you've already experienced the quest and want to skip it in future playthroughs.
- You prefer not to engage with the polygamy quest mechanics.

**Not recommended if:**
- You want to experience the full questline and story progression for unlocking polygamy.
- You're aiming for a more immersive, quest-driven experience.

**Notes:**
- This is a cheat option intended for advanced users or modders.
- Enabling this toggle will prevent the polygamy quest from starting automatically (e.g., via dragon soul collection during sleep).
- The toggle reflects the quest's completion status but can be set independently.

## Defaults & Quick Recommendations ✅

- **Skip wedding ceremonies after first?**
  - Recommended: **Enabled** if you’ve seen the wedding before.
- **Show start dialogue to genders:**
  - Default **opposite**, but **all** is the “maximum chaos” setting.
- **Use vanilla voiced dialogues if possible:**
  - With SkyrimNet: pick based on whether you value **stability** (enabled) or **full AI voicing** (disabled).
- **Use AI generated responses in dialogues:**
  - Recommended: **Enabled** if you’re using SkyrimNet and like dynamic storytelling.
- **Min relationship rank for dialogue:**
  - Default: **-4** – allows romance with enemies and positive relationships.
- **Action Confirmations** (all four options):
  - Default: **Enabled** – recommended for most players to maintain control over major relationship decisions.
  - Disable if you prefer full AI autonomy.
- **Log level:**
  - Default: **warning** – good for most players.
- **Log destination:**
  - Default: **file** – safe and standard.
- **Enable always success for engagement:**
  - Default: **Disabled**, unless you're testing or embracing full Dragonborn narcissism.
- **Enable polygamy (bypasses quest):**
  - Default: **Disabled**, unless you want to skip the polygamy quest and enable multiple marriages immediately.

Tweak these until your game feels like *your* version of Skyrim romance: chill, chaotic, or somewhere between “Hallmark movie” and “political power harem.” 😉
