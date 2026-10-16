# 🤖🎙️ M.A.R.A.S + SkyrimNet AI Guide — When Your Spouses (and Their Exes) Have a Mind of Their Own

In standard Skyrim, romance dialogue consists of walking up to someone wearing a shiny necklace and hearing *"Is that an Amulet of Mara? I'm surprised someone like you isn't spoken for."* 😐

When you combine **M.A.R.A.S** with **SkyrimNet**, the static dialogue trees come alive! NPCs can flirt with you, judge your empty bank account when you propose, confess secret affairs out of guilt, question why their newborn baby has Khajiit ears, and remember how they fell in love with the town blacksmith while you were away in Solstheim.

Here is everything M.A.R.A.S adds to **SkyrimNet**—from a pure gameplay perspective! 🍿✨

---

> [!TIP]
> **Related Player Guides**
> - [💞 Relationship Overview](RELATIONSHIP_OVERVIEW.md) — The stages of romance, cheating, open relationships, and matchmaking.
> - [❤️ Affection System Guide](AFFECTION_SYSTEM.md) — How moods, gifts, and neglect work.
> - [👶 Kids, Adoption & Fertility Guide](FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md) — Pregnancy, children, and paternity secrets.
> - [🖥️ UI Dashboard Guide](UI_DASHBOARD_GUIDE.md) — How to manage AI matchmaking, memories, and SkyrimNet settings in the Dashboard (`Alt + B`).

---

## 1. What NPCs Know About Their Lives (Smart Prompts & Zero Metagaming!) 🧠🤫

Whenever you talk to an NPC (or two NPCs talk to each other), M.A.R.A.S feeds their live relationship story into their AI personality—**filtered strictly by what that specific person actually knows**:

### 💍 Their Love Life, Hierarchy & Sister-Wives
- **Current & Past Partners**: They know everyone they're currently **Married** to, **Engaged** to, **Dating**, or sleeping with as a casual **Lover**—plus all of their **Exes** (`Divorced`, `Jilted`, `Ex-Dating`, `Ex-Lovers`).
- **"You Stole My Spouse!" Backstory**: If an NPC left their vanilla sweetheart or spouse to be with you—or if you *stole* their partner—they remember who left whom for the Dragonborn and will bring that history (and saltiness! 🧂) into conversation!
- **Polygamy & Household Ranks**: If you have **Player Polygamy** enabled, your spouses know their **Household Rank** (`Lead Spouse`, `Second`, `Third`, etc.) and know all of your *other* spouses and partners by name!
- **Your Marriage Worthiness**: Thinking of proposing? The NPC's AI knows whether you actually meet their **Marriage Difficulty / Prestige** standards—so a snobby Noble can roast your lack of property, or a Warrior can praise your glory with the Companions!

### 👶 Pregnancy, Kids & The Kids' Own Perspective
- **Expecting Mothers**: Pregnant NPCs know they're carrying a child (including **twins or triplets**!), who the father is, and whether the pregnancy is a dangerous secret.
- **Family Roster**: Parents know all of their children (even unspawned "paper kids"!), their ages, where they're training, and even what pet dog or mudcrab each kid brought home!
- **Talking to Your Kids**: If you talk directly to a child (or a grown-up biological son/daughter), *their* AI knows who their biological parents, step-parents, adoptive guardians, and siblings are!

### 🕵️‍♂️ Strict "No Psychic Spouses" Rule (Secret Filtering!)
Nothing ruins a good soap opera faster than an AI spouse magically knowing you cheated on them in Riften while they were asleep in Whiterun. M.A.R.A.S prevents AI metagaming:
- **Secret Affairs Stay Secret**: If you (or an NPC) have a **Secret Affair**, the betrayed partner's AI prompt has **zero idea** it's happening—until they catch you, someone confesses, or the affair partner exposes it!
- **Secret Babies Stay Secret**: If a wife is hiding a **Paternity Secret**, her deceived husband genuinely believes the baby is his own biological child (calling them *"my son/daughter"*) until the truth comes out! And if she hid the pregnancy from the real biological father too, he won't know that kid is his until someone tells him!

---

## 2. Conversational AI Actions: What NPCs Can Do in Chat 🗣️⚡

With SkyrimNet, you don't have to click dialogue menus to change relationships. Based on how your conversation goes, **NPCs can decide to trigger real M.A.R.A.S actions on their own**—both with **you** and with **other nearby NPCs**!

### 💘 Starting & Advancing Romance (Two-Way Proposals!)
- **When *You* Make a Move**: Flirt with an NPC, ask them out on a date, suggest becoming casual lovers, or propose marriage in voice/text chat! The NPC runs **`ConsiderLoverProposal`**, **`ConsiderDatingProposal`**, or **`ConsiderEngagementProposal`**, weighing their feelings, personality, and your Prestige before saying yes or turning you down!
- **When *They* Make a Move on You (or Another NPC)**: If sparks are flying in conversation, an NPC can boldly ask *you* (or another NPC!) to become **Lovers** (`ProposeLover`), start **Dating** (`ProposeDating`), or get **Engaged** (`ProposeEngagement`)!

### 🔓 Opening, Closing & Ending Relationships
- **Negotiating Open Relationships**: Talk your partner into opening up your relationship (`OpenRelationship`) or returning to exclusivity (`CloseRelationship`).
- **Breakups & Divorces**: Push a partner too far in an argument—or tell them it's over—and they can end a casual fling (`BreakupLover`), dump you (`BreakupDating`), call off the wedding (`CallOffEngagement`), or **Divorce** you (`Divorce`) right in the middle of the conversation!
- **Saving an Estranged Marriage (*"Marriage: The Last Word"*)**: When a spouse's affection drops into **Estranged (`0–24`)**, they'll confront you on the brink of divorce. With SkyrimNet, you have to genuinely apologize and win back their heart in roleplay—or watch them walk out!

### 🗡️ Confessing, Exposing & Resolving Affairs
When someone is cheating, three dramatic actions can trigger in conversation:
1. **`ConfessAffair`**: A guilty partner breaks down and confesses their secret affair to their significant other.
2. **`ExposeAffair`**: The messy third-party paramour walks up to the betrayed spouse and **spills the beans** out of jealousy or spite! 😱
3. **`ResolveAffair`**: Once an affair is out in the open, the couple hashes it out in conversation—ending with the cheater **dumping the affair partner**, the couple **opening the relationship**, or an immediate **breakup/divorce**!

### 🍼 Paternity Bombshells in Conversation
- **`RevealChildSecret`**: A mother carrying a secret baby can confess the truth to her deceived partner, to the real biological father, or to **both of them at once**!
- **`DiscoverChildPaternity`**: If a father starts asking questions—especially when the child has a **noticeable racial/physical difference** from the presumed dad—he can connect the dots and uncover the paternity secret right in dialogue!

### 🛡️ The "Decline & Explain Why" Button!
Worried an over-eager NPC will propose marriage or dump you when you didn't want them to?
- In **Dashboard $\rightarrow$ Settings $\rightarrow$ Confirmations**, you can turn on confirmation pop-ups for any relationship change!
- When SkyrimNet is active, the pop-up gets a special **3rd button**: **`[Accept]`**, **`[Cancel]`**, and **`[Decline (Explain)]`**!
- Clicking **`Decline (Explain)`** stops the status change *and* immediately cues you up to explain **why** you turned them down in conversation so the NPC reacts naturally in roleplay! 🎤

---

## 3. The AI Game Master: Background Evaluations & Matchmaking 🎲🧙‍♂️

Behind the scenes, M.A.R.A.S uses SkyrimNet's Game Master AI to run **7 specialized evaluations** that keep the world evolving:

1. 🌍 **Scheduled World Matchmaking**: Every few in-game days, the AI Game Master looks at single and coupled NPCs across Skyrim and proposes new romances, engagements, weddings, breakups, open relationships, or secret affairs that fit their personalities.
2. 🎯 **Manual Per-NPC & Global Matchmaking**: When you open the **M.A.R.A.S Dashboard (`Alt + B`) $\rightarrow$ Matchmaking**, you can ask the AI for romantic matches for any specific NPC (or the whole province!) and read its exact reasoning.
3. ⚖️ **The Lore & Sanity Judge (Verification Step)**: Before *any* scheduled match is applied (even if you use the **Dice-Roll Engine** to pick pairs!), the AI verifies each couple individually—checking their bios, temperaments, and existing partners—and vetoes any pairing that makes no sense.
4. ✍️ **Dual-Perspective Memory Generation**: Whenever a new NPC couple forms, advances, or splits up via matchmaking, the AI writes **two separate first-person memories** (one for Partner A, one for Partner B) before locking in the change!
5. 🔄 **Live Relationship Progression Checks**: Evaluates whether an NPC couple in the world is ready to take the next step in their relationship.
6. 🧬 **Paternity Secret Evaluations**: When a secret baby is born (or when you click **`Evaluate Secret`** on a child's Dashboard profile!), the AI weighs the mother's personality, the fathers' personalities, and any obvious racial mismatch to decide if the secret comes out and whether the deceived partner stays or divorces her!
7. 🦸‍♂️ **Biological Father Step-In Evaluation**: If a deceived husband divorces a mother over a paternity scandal, the AI evaluates whether the **true biological father steps up** to marry her and take in his child!

> [!NOTE]
> **What If an AI Request Times Out?**
> If your local LLM or API connection hiccups during a matchmaking or evaluation batch, M.A.R.A.S won't corrupt your game or silently lose data—it pops up a clean in-game **Retry / Cancel** box telling you which pair failed so you can retry just that step!

---

## 4. Live World Events & Instant Voice Reactions 🎬📢

Whenever a big relationship or family moment happens in gameplay, M.A.R.A.S fires one of **23 live triggers** into SkyrimNet so NPCs remember the moment and **react out loud on the spot**:

- 💍 **Romance, Weddings & Polygamy**:
  - `ttm_dating_proposal` & `ttm_marital_status_change` — Reacting to new courtships, engagements, weddings, breakups, jiltings, and divorces.
  - `ttm_polygamy_blessing` — When Mara blesses your polygamous household!
  - `ttm_spouse_hierarchy_change` — When you promote or demote a spouse in your household ranks *(expect a **Proud** spouse to gloat when promoted—or fume when demoted!)*.
  - `ttm_spouse_affection_change` — When a partner's mood tier shifts (like dropping into *Troubled* or *Estranged*).
  - `ttm_action_decline` — When you turn down an NPC's proposal using **Decline (Explain)**.
- 🔥 **Open Relationships & Infidelity Caught Live**:
  - `ttm_open_relationship_change` — Opening or closing a relationship.
  - `ttm_open_relationship_walkin` — When your open-relationship partner physically walks into the room while you're getting intimate with a side lover! (They permitted it, so no breakup—though they might still make a comment!)
  - `ttm_cheating_caught` — **Bedroom Walk-Ins!** If a partner walks in on you (or an NPC) cheating in an OStim/SexLab scene, the scene stops and the betrayed partner immediately confronts the cheater! 💥
  - `ttm_affair_confessed`, `ttm_affair_exposed`, & `ttm_affair_resolved` — Immediate reactions to confessions, third-party exposures, and affair showdowns.
- 👶 **Pregnancy, Birth & Growing Up**:
  - `ttm_pregnancy_announced`, `ttm_pregnancy_lost`, & `ttm_labor_started` — Discovering a pregnancy, grieving a lost pregnancy, or going into labor!
  - `ttm_child_birth` *(supports single births plus twin/triplet batches!)* & `ttm_child_adopted`.
  - `ttm_child_grew_up`, `ttm_child_sent_to_training`, & `ttm_child_became_adult` — Celebrating your kids growing from infants into children, heading off to guild training, and becoming adult followers!
  - `ttm_child_parents_changed`, `ttm_child_inherited` *(when a new spouse becomes step-parent to your kids or vice versa)*, & `ttm_paternity_secret_resolved`.

---

## 5. First-Person Episodic Memories 🧠📖

When two NPCs get together, get married, or break up through Matchmaking while you're off exploring a ruin, they don't just get a cold `"Status: Dating"` label slapped onto them:
- M.A.R.A.S generates **two distinct, first-person episodic memories**—one written in **Partner A's voice** and one written in **Partner B's voice**—complete with **where** it happened, **how they felt**, and **emotional tags**, and injects them directly into SkyrimNet's long-term memory database!
- **What That Means in Gameplay**: You can walk into the Bannered Mare three days later, ask Ysolda *"So, how did things start between you and Mikael?"*, and she will actually tell you the story from her memory!
- **Read & Edit Memories in the Dashboard**: When running **LLM Matchmaking** in the M.A.R.A.S Dashboard (`Alt + B`), you can preview both NPCs' generated memories—and even **rewrite or edit them yourself** before clicking Apply! ✍️✨

---

## 6. Customizing SkyrimNet Features in the Dashboard ⚙️🎛️

Open the **M.A.R.A.S Dashboard (`Alt + B`) $\rightarrow$ Settings** to tune how chatty, dramatic, or autonomous your AI world is:
- **Settings $\rightarrow$ AI Dialogues**: Toggle which AI actions NPCs are allowed to initiate on their own (proposals, breakups, affair confessions/exposures, paternity reveals).
- **Settings $\rightarrow$ Confirmations**: Turn on the **Accept / Cancel / Decline (Explain)** pop-up for any relationship change involving you.
- **Settings $\rightarrow$ Matchmaking**: Toggle **AI Matchmaker (LLM Mode)**, **Verification Step**, and **Generate Memories** on or off.
- **Settings $\rightarrow$ SkyrimNet**: Adjust the proximity radius for nearby NPC actions or toggle **Simulate No SkyrimNet** if you ever want to test standard non-AI fallback mechanics! 😉

