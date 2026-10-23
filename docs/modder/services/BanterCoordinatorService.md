# BanterCoordinatorService — Feature & Mechanics Guide

**Implementation**: [BanterCoordinatorService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/BanterCoordinatorService.cpp)

`BanterCoordinatorService` coordinates ambient in-world romantic, open-relationship, and infidelity banter scenes between NPC couples near the player. It filters nearby actors, evaluates location keyword rules, runs a two-tier couple selection algorithm (supporting both co-located couples and single NPCs talking about an absent partner), and enforces both global and per-couple cooldowns.

---

## 1. Master Scene & Location Keyword Gating

Before scanning for nearby NPC couples, `BanterCoordinatorService` verifies that ambient scenes are permitted in the player's current environment:

1. **Master & Sub-Type Toggles**:
   - Requires the master `enableScenes` toggle to be enabled and at least one scene category (`Lover`, `Dating`, `Engaged`, `Married`, `OpenRelationship`, `Cheating`, `CheatingDiscovered`, or `CheatingCaught`) to be active.
2. **Location Keyword Whitelist & Veto Rules (`IsLocationAllowedForScenes`)**:
   - Inspects the `BGSLocation` of the player's current area against 15 configurable location keyword rules across three groups:
     - **Public & Civic Interiors**: `LocTypeTemple`, `LocTypeStore`, `LocTypeStewardsDwelling`, `LocTypeInn`, `LocTypeHouse`, `LocTypeBarracks`, `LocTypeGuild`
     - **Player Residences**: `LocTypePlayerHouse`, `BYOH_LocTypeHomestead`
     - **Exterior Settlements & Streets**: `LocTypeTown`, `LocTypeSettlement`, `LocTypeOrcStronghold`, `LocTypeCity`, `LocTypeLumberMill`, `LocTypeFarm`
   - **Strict Keyword Veto**: If a location carries *multiple* keywords from the list (e.g., a store inside a city) and **any** one of its matching keywords is disabled in settings, the location is rejected (`false`). Otherwise, at least one enabled keyword from the 15 categories must be present on the location (preventing romantic banter inside dungeons, caves, or wilderness camps).

---

## 2. Nearby Actor Discovery & Eligibility Filtering

To gather local candidates within `scanRadius` of the player, the service combines two engine sources:
- **High-Process Actors**: Iterates `ProcessLists::highActorHandles` for all loaded nearby actors.
- **Interior Cell Reference Sweep**: If the player is inside an interior cell, also iterates the cell's object references directly to ensure no local NPC in the room is missed.

Every candidate actor must pass strict eligibility checks:
- **Not the Player**: The player character (`0x14`) is never selected as a banter NPC.
- **Living & Valid**: Must not be dead, disabled, deleted, a ghost (`IsGhost`), or recorded in [DeceasedRegistry](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeceasedRegistry.cpp) (checked by both placed Reference FormID and Base NPC FormID).
- **Adults Only**: Excludes child actors (`IsChild`) and `0xFF` synthetic unborn/infant records.
- **No Active Player Followers**: Excludes NPCs currently following the player as an active teammate/follower (`IsPlayerFollower`) so companion combat/travel routines are not interrupted.
- **No Active Player Main Partners**: Excludes NPCs who are currently in an active main relationship (`Dating`, `Engaged`, `Married`) with the player (`IsPlayerActiveMainPartner`) so generic location-arrival banter never hijacks or competes with Tier 2 Player-Partner Agency & Home-Arrival mechanics.
- **Idle & Out of Combat**: Excludes actors currently in combat (`IsInCombat`) or already participating in another Creation Engine quest scene (`GetCurrentScene() != nullptr`).
- **3D Loaded & Within Radius**: For locally present actors, their 3D model must be loaded and within `scanRadius` units of the player.

---

## 3. Two-Tier Couple Selection Algorithm

Once eligible nearby actors are collected, `BanterCoordinatorService` searches for an active romantic pair (`Dating`, `Lover`, `Engaged`, or `Married`) that is not currently on cooldown:

### Tier 1: Co-Located Couples (Both Partners Present)
- Evaluates every pair of local actors within `scanRadius`.
- If **both** partners are physically present, loaded, and have a qualifying active relationship (`Dating`, `Lover`, `Engaged`, or `Married`) that is off cooldown, they are added to the **Tier 1** pool.
- **Tier 1 Priority**: As long as at least one co-located couple is available in Tier 1, the service selects a Tier 1 couple using prioritized selection (never-played couples first, then least-recently-played couples with random tie-breaking) and returns both actors to drive a face-to-face couple scene.

### Tier 2: Solitary Local NPC with Absent Partner (Fallback)
- If **no** co-located couples are present in Tier 1, the service falls back to **Tier 2**.
- For each local actor within `scanRadius`, it inspects all of their active `Dating`, `Lover`, `Engaged`, and `Married` partners who are **not** within the local scan radius (excluding the player and active player main partners).
- Looks up the absent partner's actor reference in the world and validates that the absent partner is alive, not a follower, not an active player main partner, not in combat, and not in another scene (skipping the 3D-loaded and distance checks).
- Selects a `(Local Actor, Absent Partner)` pair using the same prioritized selection (never-played first, then least-recently-played) and returns them so the quest scene can trigger solitary/monologue or third-party gossip dialogue referencing the absent partner.

---

## 4. Dual Cooldown System (Global & Per-Couple)

To prevent banter spam in crowded inns or towns, `BanterCoordinatorService` enforces two independent cooldown timers governed by `scenesCooldownHours` (defaulting to `24.0` in-game hours):

1. **Global Scene Cooldown (`1x`, `m_lastSceneDay`)**:
   - Recorded whenever any banter scene plays (`RecordScenePlayed`). Blocks all new banter scene searches across the entire world until `scenesCooldownHours / 24.0` in-game days have elapsed.
2. **Per-Couple Cooldown (`2x` Global Multiplier, `m_coupleCooldowns`)**:
   - Keyed by canonical `RelationshipPairKey(actorA, actorB)`.
   - Lasts **`2x` the global scene cooldown** (`2.0 * scenesCooldownHours`, e.g., `48.0` in-game hours by default) and is stamped when the couple's scene actually plays (`RecordScenePlayed`), preventing the same couple from repeating immediately as soon as the `1x` global cooldown expires.
   - Among off-cooldown candidates in a tier, couples who have **never played** a scene are prioritized first, followed by the **least-recently-played** couple(s).
- Both the global timestamp, per-couple timestamps, and configured cooldown hours are persisted in the `'BNTR'` (version 2) SKSE co-save record.
