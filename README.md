# 💍🐉 M.A.R.A.S — Marriage, Affection, Romance & Adoption in Skyrim

## 1. General Overview ✨🏰

Ever looked at a Jarl, a grumpy blacksmith, a chaotic necromancer, or that one shopkeeper who keeps ripping you off and thought: *"Yeah, I can fix them"*?

**M.A.R.A.S** is a full-scale, **SKSE-powered** and **ESL-flagged** romance, marriage, family, and social simulation overhaul for Skyrim. It doesn't just let you **romance and marry literally any adult NPC in the province**—it turns all of Skyrim into a living, breathing soap opera where **both you and NPCs** can date, hook up, propose, marry, build a polygamous dynasty, share houses, adopt unlimited kids, have biological babies, cheat Behind Closed Doors™, get caught red-handed, and suffer financially ruinous divorces. 😇🔥

Whether you want a wholesome monogamous life with a doubled spouse buff, a carefully ranked harem of twelve spouses funding your adventures, or to play Cupid (or homewrecker) for the townsfolk of Whiterun from an in-game **Prisma UI Dashboard (`Alt + B`)**, M.A.R.A.S hands you the rings and lets you live with the consequences.

> [!NOTE]
> **SFW Core with Optional Adult & AI Hooks:** M.A.R.A.S itself is completely **SFW** out of the box, and its `TT_MARAS.esp` plugin is **ESL-flagged** (so it takes zero full load-order slots). If you *choose* to pair it with **SkyrimNet**, **Fertility Mode Reloaded / Beeing Female**, or intimacy frameworks (**OStim / SexLab**), M.A.R.A.S seamlessly hooks into them for AI-voiced romance, pregnancies, and intimacy tracking!

### ⚙️ Requirements & Integrations

**Hard Requirements**
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [Prisma UI](https://www.nexusmods.com/skyrimspecialedition/mods/148718) *(powers the interactive in-game `Alt + B` Dashboard)*
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604)
- [UIExtensions](https://www.nexusmods.com/skyrimspecialedition/mods/17561)
- [PapyrusUtil SE](https://www.nexusmods.com/skyrimspecialedition/mods/13048)
- [powerofthree's Papyrus Extender](https://www.nexusmods.com/skyrimspecialedition/mods/22854)

**Optional Integrations (Highly Recommended, Totally Optional)**
- 🤖 **[SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin/tree/main)** — Turns romance into real AI-powered conversations! Flirt, propose, negotiate open relationships, confess affairs, or let the AI play matchmaker for the entire province with custom voice acting.
- 🤰 **Fertility Mode Reloaded (FMR) / Beeing Female NG** — Adds full biological pregnancy, childbirth, racial phenotype inheritance, and secret paternity drama to both Player and NPC relationships.
  - ⚠️ **Fertility Mode Reloaded**: **v1.0.7+ (CRITICAL)**
  - ⚠️ **Beeing Female NG**: **v3.6.4+ (CRITICAL)**
- 💋 **OStim Standalone / SexLab** — Automatically rewards affection for intimate moments with partners—and triggers secret affairs or bedroom walk-in confrontations if you stray!

> [!WARNING]
> **CRITICAL FERTILITY FRAMEWORK VERSION REQUIREMENTS:**
> If you choose to use an optional fertility framework with M.A.R.A.S, you **MUST** ensure you are running the latest versions:
> - **Fertility Mode Reloaded (FMR)**: **v1.0.7 or higher** is strictly required.
> - **Beeing Female NG**: **v3.6.4 or higher** is strictly required.
> 
> Older versions of either mod lack necessary lifecycle ModEvents, pass incomplete parameters, or contain incompatible data structures, which will break pregnancy detection, birth progression, and child actor spawning!

### 🧩 Compatibility at a Glance
- **Non-Destructive by Design**: M.A.R.A.S does **not** edit vanilla NPC records or overwrite vanilla marriage quests. It layers its C++ relationship engine and AI package overrides cleanly at runtime.
- **Freedom to Make Questionable Choices**: The game still runs its original quests! If you marry **Vittoria Vici**, the Dark Brotherhood still has a contract on her. If you move a Jarl into Breezehome and forget to uncheck *"Stay at Home"*, don't be surprised when they're sipping mead in your kitchen instead of sitting on their throne. 😉

---

## 2. Player-Friendly Features & Gameplay Guide 🎉💘

Want the complete shortcut cheat sheet and step-by-step walkthrough? Start here:
👉 **[The Ultimate M.A.R.A.S Player Guide](docs/player/PLAYER_GUIDE.md)**

---

### 🖥️ The In-Game Dashboard (`Alt + B`)
Forget digging through ten layers of slow menus! Press **`Alt + B`** anytime during gameplay to pause the world and open the **M.A.R.A.S Dashboard**:
- Inspect your **Household**, **Spouse Hierarchy**, **Homes**, **Children**, and live **Household Buffs**.
- Browse a searchable directory of **every NPC in Skyrim**, view their relationship tree, check their mood, or teleport straight to a lost spouse or kid.
- Play Cupid in the **Matchmaking** tab—pair up any two NPCs manually or let the AI suggest matches!
- 📖 **Read more:** **[UI Dashboard Guide](docs/player/UI_DASHBOARD_GUIDE.md)**

---

### 💞 Romance Anyone, Any Way You Want (Plus NPC-to-NPC Couples!)
If they're an adult NPC in Skyrim and not a sabre cat, they're fair game:
- **Start with a Spark**: No NPC is cluttered with romance menus until you break the ice with:
  > `(M) Ever think we could be something more?`
- **Flexible Relationship Ladder**: Take things at your own pace! From **Candidate**, you can hook up as **Casual Lovers** *(friends with benefits!)*, go steady with **Exclusive Dating**, or skip the courtship and propose straight from **Candidate** $\rightarrow$ **Engaged** $\rightarrow$ **Married**—or crash and burn into **Ex-Lover**, **Ex-Dating**, **Jilted** *(left at the altar!)*, or **Divorced**.
- **Proposals That Actually Care Who You Are**: Gone are the days of "wear amulet, get spouse." NPCs evaluate your **Affection**, **Speech**, **Wealth**, **Owned Homes**, **Quest Fame**, and **Guild Alignment** against their own social standing before saying yes.
- **A Living World of NPC Couples**: Existing vanilla couples are automatically imported, and background **Matchmaking** lets single NPCs across Skyrim date, get engaged, and marry each other on their own!
- 📖 **Read more:** **[Relationship Overview & Matchmaking Guide](docs/player/RELATIONSHIP_OVERVIEW.md)**

---

### ❤️ Affection, Open Relationships, Cheating & Alimony Drama
Getting the ring is only half the battle—keeping it is where the fun begins:
- **Affection & Daily Moods (`0–100`)**: Earn love by talking daily, giving gifts tailored to their tastes, traveling together as followers, or sharing a bed. Ignore your spouse for weeks while dungeon-delving, and their mood will slide from *Happy* to *Unhappy* to *Estranged*.
- **"Marriage: The Last Word" & Divorce Settlements**: Let a spouse's affection drop below `25` and you have **3 in-game days** to save your marriage before they serve you divorce papers—complete with a **Divorce Settlement bill** based on your gold, houses, horses, and cleared dungeons (backed by Hold guards if you refuse to pay!). 🚓💸
- **Open Relationships & Secret Affairs**: Negotiate an **Open Relationship** if your partner's temperament allows it—or risk a **Secret Affair**! Just watch out: if your partner walks into the room mid-act, or if your side lover exposes you, you'll face a full confrontation.
- 📖 **Read more:** **[Affection System Guide](docs/player/AFFECTION_SYSTEM.md)**

---

### 👑 Polygamy, Spouse Hierarchy & 14 Unique Buffs
Why stop at one spouse when Mara herself can bless your ambition?
- **"The Flame Divides Not"**: Unlock **Polygamy** through an immersive mini-quest (be married, absorb a Dragon Soul, and sleep) or with a single click in the Dashboard settings.
- **Rank Your Household (`1st` to `4th+`)**: Promote your favorite spouses to the top of your **Spouse Hierarchy** for maximum buff power and store income—or stay **Monogamous** with a single spouse to enjoy a massive **`2.0x` Permanent Buff bonus**!
- **Personality Archetypes That Matter**: Every NPC has a **Social Class** (*Outcast* to *Rulers*), **Skill Type** (*Warrior, Mage, Rogue, Craftsman, Ranger, Orator*), and **Temperament** (*Proud, Humble, Jealous, Romantic, Independent*). Together, they grant **8 Permanent Household Buffs** and **6 Follower Combat Buffs**!
- 📖 **Read more:** **[Archetypes, Hierarchy & Buffs Guide](docs/player/ARCHETYPES_AND_BUFFS.md)**

---

### 🏡 Homes, Spouse Stores & "I Married Into the Blue Palace"
- **Move Spouses Into Your Homes**: Assign spouses to any vanilla or modded player home. Toggle **"Stay at Home"** off anytime for Jarls, merchants, or quest NPCs so they keep their daytime jobs without breaking quests!
- **9 Specialized Home Merchants**: Housed spouses open their own shop right at home—acting as an **Apothecary, Blacksmith, Fletcher, Hunter, Innkeeper, Jeweler, Tailor, Spell Merchant,** or **Misc Merchant** while generating daily passive gold based on their Social Class.
- **Live in *Their* House Legally**: Ask a happy spouse (`Affection > 50`) to share their home and gain legal ownership access to their bed, doors, and containers—yes, even if they live in a palace! 👑

---

### 👶 Unlimited Adoptions, Pregnancies & Secret Paternity
Build a true Skyrim dynasty—or a daytime talk-show scandal:
- **Unlimited Child Adoptions**: Bypass the vanilla 2-child cap! Adopt orphans into your household—or assign orphans to **NPC couples** across Skyrim via the Dashboard.
- **Kids, Critters & Pets**: Manage your children's allowances, chores, pets, and wild critters right from the Dashboard.
- **Fertility Mode Reloaded (FMR) & Beeing Female Integration**: Track pregnancies from conception to birth, watch children grow from **Infant $\rightarrow$ Child $\rightarrow$ Youth Training $\rightarrow$ Adult Follower**, and navigate **Secret Paternity** drama! If a baby is born from an affair with a noticeably different racial phenotype (like an Orc baby born to two Nord spouses 😬), the presumed father might start asking very uncomfortable questions!
- 📖 **Read more:** **[Kids, Adoption & Fertility Guide](docs/player/FERTILITY_MODE_RELOADED_INTEGRATION_USER_GUIDE.md)**

---

### 🤖 SkyrimNet AI Integration: Voice, Memory & Autonomous Drama
Pair M.A.R.A.S with **SkyrimNet** to bring every conversation to life:
- **Real-Time TTS Voice Acting for All Unvoiced Dialogue**: With SkyrimNet installed, every unvoiced M.A.R.A.S dialogue line is **voiced in-game in real time** using your selected TTS engine! *(Even if you don't want AI chat or autonomous LLM behavior, you can disable all other SkyrimNet functionality and keep **only TTS** enabled just to have every NPC fully voiced!)* 🎙️🗣️
- **Natural AI Proposals & Breakups**: Charm, persuade, or argue with NPCs in real time. NPCs can accept proposals, demand an open relationship, confess to cheating, or dump you mid-conversation!
- **Rich Relationship Memory**: NPCs remember who they're dating, who their step-kids are, who cheated on whom, and which secrets they *aren't* supposed to know yet.
- 📖 **Read more:** **[SkyrimNet AI Integration Guide](docs/player/SKYRIMNET_INTEGRATION_GUIDE.md)**

---

## 3. Modder & Advanced User Reference 🛠️📐

This section provides technical documentation for mod authors, patch creators, and advanced users who wish to customize M.A.R.A.S via JSON configurations, Papyrus scripts, native C++ APIs, or SkyrimNet prompt decorators.

### 3.1 Configuration & Modding Guides
- **[Advanced Configuration & Modding](docs/modder/ADVANCED_CONFIGURATION_AND_MODDING.md)** — Overview of JSON configuration files (`SKSE/Plugins/MARAS/`), NPC archetype overrides, and external mod integration points.
- **[Marriage Difficulty Configuration](docs/modder/MarriageDifficultyConfig.md)** — Technical specification for `marriageDifficultyConfig.json` formulas, weights, social class multipliers, and acceptance probability curves.
- **[Archetypes & Buffs Technical Reference](docs/modder/ARCHETYPES_AND_BUFFS.md)** — Deterministic archetype classification rules, `BuffsConfig.json` schema, and spell/perk effect magnitudes.
- **[AI Package Override System](docs/modder/PACKAGE_OVERRIDE.md)** — JSON schema and condition evaluation rules for dynamic AI package injection (`PackageOverrideService`).
- **[Quest Event Tracker Configuration](docs/modder/QUEST_TRACKER.md)** — Declarative JSON quest stage hooks (`QuestEventHandler`) for triggering relationship transitions or affection changes on quest events.

### 3.2 System Architecture & Cross-Layer References
- **[System Architecture Overview](docs/modder/ARCHITECTURE.md)** — High-level architecture across the SKSE C++23 plugin (`MARAS.dll`), `TT_MARAS.esp` engine mirrors, Papyrus event layer, and Prisma UI frontend, including `'MARS'` v4 SKSE co-save serialization.
- **[Public API & Prisma UI Bridge Reference](docs/modder/API_AND_PRISMA_BRIDGE_REFERENCE.md)** — Complete reference for the `MARAS.psc` Papyrus native interface, `MARAS_PublicAPI.h` C/C++ interop header, SKSE `ModCallbackEvent` payloads, and Prisma UI JS-to-C++ bridge endpoints.
- **[Relationship Dynamics Technical Guide](docs/modder/RELATIONSHIP_DYNAMICS_GUIDE.md)** — Canonical `PairKey` state machine, exclusivity/polygamy rules, directional `CheatingRecord` tracking, and autonomous NPC matchmaking pipelines.
- **[Household & Children Architecture](docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md)** — Player house registration, tenant sandboxing, guardian vs. biological lineage slot neutrality, and marriage/divorce/death custody resolution.
- **[Fertility Mode Reloaded Integration Architecture](docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md)** — FMR StorageUtil polling, synthetic child ID lifecycle (`0xFF700001+`), racial phenotype tier classification, and dual-edge paternity secrecy (`isSecretForPartner` / `isSecretForBioFather`).
- **[SkyrimNet & AI Integration Reference](docs/modder/SKYRIMNET_AND_AI_INTEGRATION.md)** — Registered prompt decorators (`ttm_*`), perspective-bound secret masking rules, YAML custom actions, and event triggers.

### 3.3 C++ Core Services Reference (`docs/modder/services/`)
- **[NPCRelationshipManager](docs/modder/services/NPCRelationshipManager.md)** — Pair-keyed relationship state machine, profile/candidate registration, vanilla `BGSRelationship` scanning, infidelity records, and child registry.
- **[AffectionService](docs/modder/services/AffectionService.md)** — Permanent affection storage (`0–100`), daily interaction caps, status-scaled decay, and estrangement thresholds.
- **[MatchmakingService](docs/modder/services/MatchmakingService.md)** — Autonomous background NPC-to-NPC pairing, compatibility scoring, 5-day engagement auto-marriage, and SkyrimNet LLM batch matchmaking.
- **[MarriageDifficulty](docs/modder/services/MarriageDifficulty.md)** — Proposal acceptance probability engine and modifier breakdown calculation.
- **[SpouseHierarchyManager](docs/modder/services/SpouseHierarchyManager.md)** — Polygamous spouse ranking (`1st`–`4th+`), rank multiplier calculation, and promotion/demotion affection adjustments.
- **[SpouseBuffService](docs/modder/services/SpouseBuffService.md)** — Calculation and application of permanent Social Class buffs and follower Skill Type buffs.
- **[BonusesService](docs/modder/services/BonusesService.md)** — Low-level effect and ActorValue modifier definitions loaded from `BuffsConfig.json`.
- **[PlayerHouseService](docs/modder/services/PlayerHouseService.md)** — Player home discovery, cell registration, and spouse tenant assignment.
- **[SpouseAssetsService](docs/modder/services/SpouseAssetsService.md)** — Spouse home sharing, cell/door ownership unlocking, and bed ownership transfer.
- **[PlayerChildrenService](docs/modder/services/PlayerChildrenService.md)** — Child home assignments, pet/critter management, and adult child follower transitions.
- **[DeceasedRegistry](docs/modder/services/DeceasedRegistry.md)** — Archival of deceased partners and children, widower/orphan resolution, and inheritance eligibility.
- **[FactionService](docs/modder/services/FactionService.md)** — Engine faction synchronization and external faction-change interception.
- **[PackageOverrideService](docs/modder/services/PackageOverrideService.md)** — Runtime AI package override evaluation and injection for housed spouses and children.
- **[PollingService](docs/modder/services/PollingService.md)** — Periodic background tick coordinator for follower state, FMR pregnancy diffs, and daily maintenance.
- **[QuestEventHandler](docs/modder/services/QuestEventHandler.md)** — Quest start/stop/stage listener and declarative command executor.
- **[SkyrimNetIntegration](docs/modder/services/SkyrimNetIntegration.md)** — SkyrimNet C++ decorator registration, perspective-masked JSON profile builders, and AI action handlers.
- **[NPCIndex](docs/modder/services/NPCIndex.md)** — Fast lookup index mapping `TESNPC` Base FormIDs to placed `Actor` Reference FormIDs.
- **[NPCTypeDeterminer](docs/modder/services/NPCTypeDeterminer.md)** — Heuristic classifier for Social Class, Skill Type, and Temperament.

---

## 4. License & Permissions 📜⚖️

**Copyright © 2025–2026 tetherball88. All Rights Reserved** *(Source-Available — see [LICENSE](LICENSE) for full terms).*

- ✅ **Contributions Welcome:** You are welcome and encouraged to fork this repository on GitHub, inspect the code, and submit **Pull Requests** or bug reports.
- ✅ **Patches & Third-Party Integrations:** You may freely create and publish compatibility patches, JSON configs, SkyrimNet extensions, and add-on mods that require the official M.A.R.A.S release as a dependency. The public interop files (`SKSE_Source/include/MARAS_PublicAPI.h` and `Scripts/Source/MARAS.psc`) are explicitly provided under the **MIT License** so third-party mods can compile against them freely.
- ✅ **Translations:** Community translations may be published as standalone translation patches that require the original M.A.R.A.S mod and credit the original page.
- ❌ **No Standalone Redistribution or Re-uploading:** You may **not** copy, repackage, re-upload, or publish M.A.R.A.S (or modified standalone forks of its DLL, ESP, scripts, or Prisma UI) as your own mod on Nexus Mods or any other platform without explicit written permission from the author.

