# 🤝 M.A.R.A.S + SkyrimNet — Features & Behavior

This document explains what changes (and what extra features) appear when you run M.A.R.A.S with SkyrimNet enabled. It also describes how engagement difficulty is calculated (see `Scripts/Source/TTM_MarriageDifficulty.psc`) and how chat-driven interactions (proposals, breakups, divorces) work.

See also:
- General usage: [User Guide](USER-GUIDE.md)
- In-game options: [MCM Guide](MCM.md)
- Advanced JSON config: [JSON Configuration](JSON-CONFIG.md)

---

## 🚀 What SkyrimNet adds

When SkyrimNet is installed and enabled, M.A.R.A.S can:

- 🎤 Accept typed proposals via SkyrimNet chat instead of fixed menu options
- 🤖 Use AI (if configured) to dynamically generate dialogue lines (voiced if your AI provider supports audio)
- 🧠 Evaluate proposal success using a complex engagement difficulty algorithm (see section below)
- 🗨️ Allow NPCs to initiate divorce/breakup through chat-driven interactions
- 🧾 Present richer context about NPC marital relationships (spouse/co-spouses/courting/lovers) to AI and the player
- ⚙️ Respect MCM toggles for AI usage:
  - "Use AI generated misc dialogues" (misc chat lines)
  - "Use AI generated quest dialogues" (quest text)

Notes:
- If SkyrimNet is missing, M.A.R.A.S falls back to vanilla dialogs and pre-written text lines. See [User Guide](USER-GUIDE.md).
- Toggle AI behaviors in the MCM: see [MCM Guide](MCM.md).

---

## 🎯 Engagement difficulty (how proposals are scored)

When you attempt to propose using SkyrimNet, M.A.R.A.S calls into `TTM_MarriageDifficulty.calcMarriageSuccessChance(npc)` to compute a success probability. Below is a human-readable breakdown of that logic (exact behavior is implemented in `Scripts/Source/TTM_MarriageDifficulty.psc`).

High-level steps:
1. Start with a baseline complexity of 50.0
2. Add a prestige delta based on NPC social class vs your prestige
3. Apply penalty/bonuses for jilted/divorced history, player-killed spouse flag, spouse counts, divorces count
4. Adjust by actor level difference, player Speech skill and relationship rank
5. Add a guild-alignment modifier (positive or negative)
6. Add an intimacy adjustment calculated from Lover's Ledger state
7. Clamp complexity to [0,100]
8. Convert to a chance: chance = clamp(1 - complexity / 100, 0, 1)

Detailed components (names taken from script):

- Baseline
  - complexity = 50.0

- Prestige delta (pDelta)
  - Target = 10 + 10 * socialClassIndex (so target is 10..70 depending on NPC social class)
  - pDelta = (target - calcPlayerPrestige()) * 0.30
  - Player prestige is computed by `calcPlayerPrestige()` (see below)

- Flags & history
  - +10 if NPC is currently jilted (previously rejected you)
  - +20 if NPC is divorced from you previously
  - +50 if player has killed a spouse/fiancé (a hard red flag)

- Counts
  - +2 * (number of currently married spouses you have)
  - +1 * (number of previous divorces)

- Level difference
  - +clamp((npcLevel - playerLevel) * 0.10, -10, 10)

- Player skills & relationship
  - -0.1 * player Speechcraft (so higher Speech helps)
  - -3 * npc.GetRelationshipRank(player) (higher relationship rank lowers complexity)

- Guild alignment
  - Added value from `calcGuildAlignmentMod(npc)` which checks the player's guild memberships (Companions, Thieves Guild, Dark Brotherhood, College, Bards) and compares them to NPC social class and NPC faction membership. If NPC is in the same guild as player it applies a -4 multiplier (makes it harder). The function picks the single strongest positive or negative modifier for the player's guilds and applies it.

- Intimacy adjustment
  - `intimacyAdjustment(npc)` uses M.A.R.A.S internal store (and Lovers Ledger if present) to determine how intimate the NPC already is with the player or others.
  - Examples:
    - If NPC has an existing spouse recorded, +25..+30 to complexity (more if that spouse is also recorded as a lover)
    - If NPC is courting, +15..+20
    - If NPC has at least one lover recorded, +10
    - Then subtracts the NPC's recorded intimacy value with the player (from Lovers Ledger) to reduce complexity when intimacy is high

- Clamp & final chance
  - complexity is clamped to 0..100
  - chance = clamp(1 - complexity / 100, 0, 1)


### 🧾 Player prestige (calcPlayerPrestige)

`calcPlayerPrestige()` compiles a 0..100 score from several sources:
- Dragonborn flag (adds +25 if player is Dragonborn)
- Thane holds: each Thane adds +4 (up to 9 holds considered)
- Guild leader: +8 if player leads a guild
- Wealth (Most Gold Carried / 5000), clamped up to +15
- Owned houses & horses: each counted with multiplier then clamped up to +15
- Heroic renown: quests, dungeons, dragon souls combined and clamped up to +25

All the above are clamped and summed to 0..100.

---

## 🗨️ Chat-driven divorces & breakups

SkyrimNet enables bi-directional chat interactions. With it:

- NPCs can initiate breakups/divorces through chat messages generated by AI or scripted prompts.
- Players can also type messages that trigger breakup/divorce sequences (e.g., aggressive or insulting text that the AI interprets as relationship-ending).
- The mod listens for certain signals and maps them to the same state transitions as vanilla dialogue choices (engaged → jilted, engaged → married, married → divorced).

Behavioral notes:
- AI-driven or chat-driven breakups follow the same save/flag updates as normal dialogues (so they persist across saves).
- If AI generation fails, M.A.R.A.S falls back to predefined text and continues the flow.
- MCM toggles allow you to disable AI generation for misc dialogues or quests individually (see [MCM Guide](MCM.md)).

---
