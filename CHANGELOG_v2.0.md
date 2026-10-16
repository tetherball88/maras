# M.A.R.A.S. v2.0 — Changelog

**The biggest update yet.** Version 2.0 is a ground-up rework of M.A.R.A.S., bringing a brand-new in-game dashboard, expanded relationship stages, NPC-NPC relationship tracking, an adoption system with no child limit, a full cheating & affairs framework, and deep SkyrimNet AI integration — all backed by a native C++ core for dramatically better performance.

---

## ✨ New Features

### 🎯 Prisma UI Dashboard
The centerpiece of 2.0. A fully interactive in-game web dashboard that replaces MCM as your one-stop shop for managing everything in M.A.R.A.S.:
- **Overview page** — at-a-glance stats, hierarchy display, and a live activity feed of relationship events
- **My Household page** — browse all your partners, their affection levels, children assignments, and relationship details
- **NPC Detail panels** — drill into any NPC to see their full relationship profile, status history, affection bar, traits, and partner connections
- **Matchmaking tools** — assign relationship statuses between NPCs directly from the dashboard, with a dedicated NPC picker and status assignment panel
- **LLM-assisted matchmaking** (SkyrimNet) — let the AI evaluate and suggest relationship changes between NPCs
- **Adoption management** — adopt, reassign, and manage children directly from the dashboard
- **NPC-NPC Relationships viewer** — browse and manage all NPC pairs and their relationship status
- **Settings page** — all mod settings in one place (General, Children, AI/Dialogue, SkyrimNet, Cheats). MCM is officially deprecated — it still works, but no new options will be added there

### 💕 New Relationship Statuses: Dating & Lover
Two new relationship stages sit between "Candidate" and "Engaged":
- **Dating** — a casual romantic relationship, lighter commitment than engagement
- **Lover** — an intimate but non-committed bond, distinct from dating

When a dating or lover relationship ends, NPCs transition to the corresponding **Ex-Dating** or **Ex-Lover** status, preserving their romantic history.

### 💍 NPC-NPC Relationships
NPCs can now have relationships with *each other* — not just with the player. All relationship statuses are available for NPC-NPC pairs: dating, lover, engaged, married, divorced, and everything in between. The main difference from player-NPC relationships is that polygamy remains player-exclusive, and NPC-NPC partners don't get home sandbox behavior. However, with SkyrimNet, these relationships become part of the NPC's biography — meaning NPCs will treat their partners as partners in conversation, not as strangers.

### 🔥 Cheating & Affairs System
A full infidelity framework:
- **Affair types**: Casual(not MARAS status, designed for when caught in act with someone else but no MARAS status like lover or dating), Dating, or Lover — reflecting the nature of the affair
- **Affair statuses**: Ongoing, Ended (breakup), Ended (victim split), Ended (confronted), or Allowed by partner
- **Secrecy**: Affairs can be secret (the victim doesn't know) or discovered
- **Open relationships**: Any main-tier relationship (dating, engaged, married) can be flagged as "open," which prevents new connections from being classified as cheating
- **Auto-propagation**: If one partner in a pair is already in an open relationship, new relationships involving them are automatically treated as open
- All of this feeds into SkyrimNet's narrative context — NPCs are aware of who's cheating on whom, whether the affair is secret, and how it ended

### 👶 Adoption System
A complete adoption system mirroring vanilla Hearthfire behavior, but without the 2-child limit:
- **Unlimited adoption slots** — adopt as many children as you want(if you don't have enough beds its on you)
- **Polygamy-aware**: Assign children to different spouses in your household
- **Vanilla children integration**: When you marry an NPC who has in-game vanilla children, you can adopt those children
- **Force greeting**: Children will randomly initiate conversations with the player (just like vanilla)
- **Home sandbox**: Children sandbox in their assigned home with proper package overrides
- **Pets & critters**: Children can adopt pets and critters, with dedicated sandbox behavior, faction management, and home assignment. In vanilla you could have one critter and pet per "family". In MARAS each child can have critter and/or pet. So your home can become zoo :)
- **Child orders**: Issue orders to children (go to room, play games, etc.) with configurable durations
- **Gift system**: Children give gifts to the player based on configurable gift tiers
- **Child custody policy**: Configurable rules for what happens to children on divorce — stay with mother, father, always with player, or never with player

### 📖 Relationship Status History
Every relationship pair now tracks a full history of status changes — who initiated the change, what the previous status was, and when it happened. This history is viewable in the Prisma UI NPC detail panels.

### ✅ Confirmation Modals
New in-game confirmation dialogs for important relationship actions (accept proposal, break engagement, divorce, etc.):
- Configurable per-action with cooldown timers to prevent accidental spam
- With SkyrimNet: adds a third "Decline (Explain)" button that lets explain why did you decline.

### 🔍 Vanilla Relationship Scanning
On first load, M.A.R.A.S. now scans all existing vanilla relationships (marriages, courting, lovers, parent-child bonds) and imports them into its tracking system. No more starting from scratch if you install M.A.R.A.S. on an existing save — your existing relationships are automatically recognized.

### 🏠 Improved Home Sandbox
Reworked spouse home sandbox logic:
- Better handling to avoid conflicting with follower framework mechanics
- Quest whitelist for package overrides — whitelisted quest packages (like follower quests) are no longer blocked by M.A.R.A.S. sandbox overrides
- Merchant spouses properly handle their vendor schedules while still sandboxing at home

---

## 🤖 SkyrimNet Integration (Reworked)

The SkyrimNet integration has been **completely reworked**, migrating from slow Papyrus-side dispatching to native C++ decorator and event registration:
- **Relationship context decorators**: NPCs' SkyrimNet prompts now include detailed relationship context — household connections, co-spouses, dating partners, lovers, ex-partners, infidelity details, polygamy context, and more
- **Event registration**: All major relationship events (status changes, affection changes, hierarchy changes, proposals, gifts, polygamy blessings) are now dispatched as structured SkyrimNet events
- **LLM relationship evaluation**: Before committing a relationship status change, SkyrimNet can evaluate whether it makes narrative sense — with retry/ignore modals if the LLM request fails
- **Available actions decorator**: NPCs expose their available relationship actions to SkyrimNet's AI context

---

## 🐛 Bug Fixes

- **Fixed PlayerMarriedFaction being re-added by 3rd party mods** when the player has polygamy enabled. M.A.R.A.S. now actively monitors and strips the faction via a periodic sanitizer, ensuring polygamy isn't broken by mods that re-add the vanilla married faction.

---

## ⚙️ Technical Notes

- Major portions of the codebase have been migrated from Papyrus to native SKSE C++, resulting in significantly better performance for relationship tracking, affection calculations, children management, package overrides, and SkyrimNet event dispatching.
