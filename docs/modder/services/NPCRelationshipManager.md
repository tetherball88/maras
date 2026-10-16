# `NPCRelationshipManager` — Features & Behavioral Guide

**Source**:
- [`NPCRelationshipManager.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp) (Core relationship state machine, candidate registration, family checks, status promotions, faction mirrors & vanilla `BGSRelationship` scanner)
- [`NPCRelationshipManager_Serialization.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Serialization.cpp) (SKSE co-save serialization, global count recalculation & JSON state dump)
- [`NPCRelationshipManager_Infidelity.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Infidelity.cpp) (Open relationships, directional cheating records, affair discovery & resolution)
- [`NPCRelationshipManager_Children.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Children.cpp) (Child registry, marriage/divorce/death custody evaluation & non-player child AI package overrides)
- [`NPCRelationshipManager_Fertility.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp) (Pregnancy/birth lifecycle, racial phenotype classification, bio-father step-in & dual-edge paternity secrets)

---

## 1. What This Service Does

`NPCRelationshipManager` is the central brain of M.A.R.A.S. Rather than treating romance as a player-only flag, it models **every romantic relationship, affair, pregnancy, and parent-child bond in Skyrim** as a unified social network where both the Player and NPCs follow the same relationship rules.

---

## 2. Core Features & Mechanics

### 2.1 Automatic NPC Onboarding & Archetype Assignment
- **Internal Profile Registration vs. Player Candidate Registration**:
  - Any NPC who interacts with M.A.R.A.S systems internally (matchmaking, existing vanilla relationships, affairs, or child guardianship) is automatically registered with an internal profile (`EnsureRegistered`) without being added to `TTM_TrackedNPC` (`0x7`) or `PotentialMarriageFaction`.
  - `TTM_TrackedNPC` at `Candidate` rank (`0`) and `PotentialMarriageFaction` are applied only when the Player explicitly registers an NPC as a marriage candidate (`RegisterAsCandidate`, e.g., via dialogue) or forms a player-involved relationship with them.
- **Personality & Social Profiling**: Upon registration, the service determines three permanent traits for the NPC:
  - **Social Class** (*Outcast, Poverty, Working, Middle, Wealthy, Religious, Nobles, Rulers*)
  - **Skill Type** (*Warrior, Mage, Rogue, Craftsman, Ranger, Orator*)
  - **Temperament** (*Proud, Humble, Jealous, Romantic, Independent*)
- **JSON Override Priority**: If an NPC has a custom preset in `SKSE/Plugins/MARAS/npcs/*.json`, those traits override the automatic calculation.
- **Engine Faction Sync**: Assigned traits and player-relative relationship statuses are automatically mirrored onto `TT_MARAS.esp` factions so Creation Kit dialogue conditions and AI packages react immediately without script polling—including `TTM_CanFormNonCheatingMain` (`0x5D2`), which marks the Player and every registered NPC as `Rank 0` if they are in any closed main-tier relationship and `Rank 1` otherwise (polygamous Player, open main-tier, lover, or single/candidate/ex).

---

### 2.2 Vanilla World Importing & Deferred Actor Resolution
- **Importing Pre-Existing Skyrim Couples & Families**: On a new game or save load, the service scans all vanilla and mod-added `BGSRelationship` records across the load order:
  - Vanilla **Spouses** become `Married`.
  - Vanilla **Courting** pairs become `Dating`.
  - Vanilla **Lovers** become `Lover`.
  - Vanilla **Parent-Child** pairs are registered in the child lineage system (automatically pairing the parent's current partner as the second co-guardian and assigning `bioMotherID` / `bioFatherID` based on actor sex).
- **Conflict Resolution with Player Romance**: If an NPC in a vanilla couple has already entered a relationship with the Player (or another partner via M.A.R.A.S), their pre-existing vanilla marriage or courtship is automatically converted to `Divorced` or `Ex-Dating` so they don't remain simultaneously married to their vanilla spouse.
- **Deferred Loading for Unspawned NPCs**: Many NPCs in Skyrim only exist as Base records until a quest spawns their physical Actor Reference in the world. If either partner (or child) hasn't spawned yet during the initial world scan, the relationship is queued in a **Pending Deferred List** and automatically activates the moment that NPC loads into the game world.

---

### 2.3 Relationship Progression & Exclusivity Rules
- **Two Relationship Tiers**:
  - **Committed (Main-Tier) Relationships**: `Dating`, `Engaged`, and `Married`.
  - **Casual / Side Relationships**: `Lover`.
  - **Former / Breakup States**: `Ex-Dating`, `Ex-Lover`, `Jilted` (broken engagement), and `Divorced`.
- **Exclusivity vs. Polygamy**:
  - NPCs can only hold **one** committed Main-Tier relationship at a time. To date or marry someone new, they must first break up with or divorce their current partner.
  - The **Player** follows the same single-partner rule *unless* **Polygamy** is enabled in settings.
- **Automatic Lover Cleanup on Commitment**: When an actor enters a new committed relationship (`Dating`, `Engaged`, or `Married`), all of their active casual `Lover` connections are automatically broken off (`Ex-Lover`)—unless they are in an **Open Relationship** or are a polygamous Player.
- **5-Day NPC Auto-Marriage**: When two NPCs become `Engaged` to each other, the service tracks the exact game day of their engagement and **automatically marries them 5 in-game days later** (as long as both remain alive and engaged).
- **Incest & Family Protection**: Before any two actors can form a romantic relationship, the service checks both M.A.R.A.S child registries and vanilla `BGSRelationship` family links (parents, children, siblings, shared parents, aunts/uncles, cousins, grandparents) and blocks any romance between blood or adoptive relatives.
- **Isolating NPC Marriages from Vanilla Player Factions**: When two NPCs marry each other, the service actively strips vanilla player-marriage factions (`PlayerMarriedFaction`, `PlayerBedOwnership`, etc.) from both NPCs so they never accidentally speak spouse lines to the Player.

---

### 2.4 Open Relationships, Infidelity & Affair Resolution
- **Open Relationship Consent**:
  - Any committed couple (`Dating`, `Engaged`, `Married`) can agree to an **Open Relationship**.
  - **Forward-Only Auto-Open**: If an actor already has an Open Relationship with one partner, any *new* committed relationship they enter is automatically marked Open as well. Breaking up resets the Open flag.
  - Closing an Open Relationship automatically breaks off all active outside `Lover` connections.
- **Directional Infidelity Tracking**:
  - Whenever an actor in a closed committed relationship has an intimate encounter (`Casual`) or enters a side romance (`Dating` or `Lover`) with a third party, the service logs a **directional cheating record** (who cheated, which committed partner was betrayed, and who the affair partner was).
  - *Polygamy Exemption*: If Player Polygamy is enabled, the Player is never marked as cheating.
  - Affairs start as **Secret** unless caught in the act, and automatically upgrade their severity if a `Casual` fling turns into `Dating` or `Lover`.
- **Discovery & Resolution Outcomes**:
  - Affairs can be discovered in three ways: **Confessed** by the cheater, **Exposed** by the affair partner/witness, or **Caught** in the act.
  - Once discovered, an affair resolves into one of three outcomes:
    1. **Confronted (Stay Together)**: The betrayed partner forgives the cheater on the condition that the affair ends—the service automatically breaks up the cheater and the affair partner.
    2. **Allowed (Open Relationship)**: The betrayed partner accepts the situation and the couple's relationship converts to an **Open Relationship**.
    3. **Breakup / Divorce**: The betrayed partner ends the relationship (`Divorced`, `Jilted`, or `Ex-Dating`).

---

### 2.5 Child Lineage, Custody Automation & Orphanage Routing
- **Lifecycle Stages & Synthetic Children**:
  - Children progress through five stages: `Unborn` (active pregnancy), `Infant` (carried baby item), `Child` (Hearthfire/spawned child actor), `Training` (away from home), and `Adult` (grown follower).
  - Unborn and infant children who do not yet have a physical 3D Actor in Skyrim are assigned a temporary **Synthetic ID** (`0xFF700001+`). When they grow into a physical child actor, their entire history, lineage, and secrets seamlessly transfer to the real Actor.
- **Automatic Custody Transfers**:
  - **On Marriage**: If a single parent marries (either Player + NPC or NPC + NPC), their new spouse is automatically added as the child's second custodial guardian. If the Player marries an unhoused NPC and already has a child living in a player home, the new spouse is automatically moved into that child's home.
  - **On Divorce**:
    - Any active partner-side paternity secrets (`isSecretForPartner`) for the couple's born or unborn children are immediately resolved (`maras_paternity_secret_resolved`), while preserving `isSecretForBioFather` if the biological father is still unaware.
    - Sole custody is awarded based on a priority hierarchy:
      1. **Biological Parent Priority** (if enabled in settings and only one spouse is the genetic parent).
      2. **Player Custody Policy** (*Always With Player* or *Never With Player*, if configured).
      3. **Gender Custody Policy** (*Stay With Mother* or *Stay With Father* for opposite-sex couples, or a 50/50 coin flip for same-sex couples).
  - **On Parent Death**:
    - If a player's spouse dies, custody of their children is automatically inherited by the **Player and the Player's highest-ranking surviving spouse**, relocating the children to that spouse's home.
    - If an NPC child loses both guardians and has no surviving biological parents, dynamically spawned (`0xFF`) children are **automatically sent to Honorhall Orphanage** in Riften and added to the adoptable faction so the Player or another couple can adopt them!
- **Dynamic NPC Child Following**: Spawned (`0xFF`) children living with NPC parents outside the player's household are dynamically linked to follow and sandbox around their living custodial parent (prioritizing their biological mother, then biological father, then adoptive guardian).

---

### 2.6 Pregnancy, Phenotype Mismatch & Paternity Intrigue
- **Dual-Edge Paternity Secrets**:
  - When a mother conceives a child with someone other than her committed partner, the service creates a **two-sided secret**:
    1. **Secret from Partner**: Her committed partner believes the baby is theirs.
    2. **Secret from Biological Father**: The true father does not know he fathered the child.
  - Each side can be discovered or confessed independently!
- **Racial Phenotype Mismatch Detection**:
  - The service categorizes races into visual phenotype groups (*Pale Human, Redguard, Mer/Elven, Orc, Argonian, Khajiit*).
  - If the biological father and presumed father belong to noticeably different racial groups (e.g., an Orc or Khajiit baby born to two Nord parents), the child is flagged with a **Phenotype Mismatch**:
    - Triggers a 40% immediate discovery roll at the moment of birth.
    - Accelerates the daily suspicion/discovery chance as the child grows older.
    - Generates tailored visual descriptions (e.g., pointing out scales, fur, tusks, elven ears, or skin tone contrast) for SkyrimNet AI prompts so the betrayed partner reacts specifically to how the child looks.
- **Biological Father "Step-In" Marriages**:
  - When a pregnant or single mother is unmarried, widowed, or newly divorced, the service checks if the child's biological father is also single and eligible to marry her.
  - Using either a **SkyrimNet LLM evaluation** (`maras_step_in_evaluation`) or a configurable dice roll, the biological father can "step up" and marry the mother to raise his child together.
- **Multi-Father Birth & Pregnancy Narratives**:
  - Supports twins, triplets, and **superfecundation** (carrying twins fathered by two different men at the same time), generating natural-language announcements that distinguish which child belongs to the partner vs. a secret lover.
