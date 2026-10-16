# PlayerChildrenService — Feature & Mechanics Guide

**Implementation**: [PlayerChildrenService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp)

`PlayerChildrenService` replaces vanilla Hearthfire's fixed two-child alias limit (`BYOHRelationshipAdoption`) with an unlimited C++ household manager for player-guarded children. It handles external faction snapshotting and restoration, per-child home sandbox markers, timed behavioral orders, household forcegreet selection, follower pets and woodland critters, sibling interaction scenes, and neglect-weighted event selection.

---

## 1. Household Onboarding & Reversible Faction Snapshotting

When a child enters the player's household (through adoption, marriage into a step-family, or a biological child aging up):

1. **External Faction & Crime Faction Snapshot**:
   - Before modifying the child's factions, the service takes a complete snapshot of every non-household faction and effective rank on the child (combining base `TESNPC` factions with runtime `ExtraFactionChanges`), along with their original crime faction.
2. **Stripping Conflicting Town/Orphanage Factions**:
   - Removes all snapshotted external factions from the child so their old town, beggar, or Honorhall Orphanage AI packages and dialogue conditions cannot override their new player-family behavior.
   - Re-applies the child's original crime faction so guards in their native hold still react appropriately if the child is attacked.
3. **Player Household Faction Enrollment**:
   - Enrolls the child in the core player-family and Hearthfire compatibility factions:
     - `TTM_PlayerChildFaction` (`0xC23`)
     - `BYOHRelationshipAdoptionChildOwnedFaction`
     - `BYOHRelationshipAdoptionFaction`
     - `PlayerFaction`
     - `BYOHRelationshipAdoptableFaction` at **Rank `25`** (signaling adopted status to vanilla dialogue checks)
     - Synchronizes `TTM_PlayerChildBothParents` (`0xCF0`) based on whether the child currently has two guardians or a single parent.
4. **Reversible Removal (Divorce / Orphanage Routing)**:
   - If the child ever leaves the player's household (for example, when the player divorces their sole biological parent or sends a child away), the service removes any assigned pet/critter, strips all 6 player-family factions, **restores every original faction and crime faction from the snapshot**, and re-evaluates their AI packages so the child seamlessly resumes their pre-adoption life.

---

## 2. Personal Home Sandbox Markers & Relocation

Unlike vanilla Hearthfire, which binds children to fixed quest aliases for a single house:
- Every child in the player's household is dynamically given their own persistent `XMarker` (`GetOrCreateChildHomeMarker`) linked to the child via the `MARAS_HomeSandboxKeyword` (`ExtraLinkedRef`).
- When a child is assigned to a player house:
  - Registers the child as a tenant in [PlayerHouseService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerHouseService.cpp).
  - Teleports the child's personal home marker to the target house's interior marker.
  - Optionally teleports the child actor directly to the marker and re-evaluates their AI package.

---

## 3. Timed Behavioral Orders & ActorValue Synchronization

M.A.R.A.S drives child AI behavior and Hearthfire-style parent orders through two ActorValues on each child:
- **`Variable06` (Current Order)**: Encodes the child's active behavioral state or parent command (e.g., chores, go outside, go to bed, or gift-triggered play animations).
- **`Variable07` (Forcegreet Pending)**: Set to `1.0` when the child has a queued household greeting for the player, and `0.0` once completed or reset.

### Timed Order Expiration & Same-Order Lock
- Orders can be issued with a duration in game hours. `PlayerChildrenService` calculates the exact expiration game day (`nowDay + durationHours / 24.0`) and automatically resets `Variable06` back to `0.0` during the periodic game-day tick once the timer expires.
- **Same-Order Lock (Dummy / Doll Conflict Prevention)**:
  - When a gift triggers a special play order (`1.1` for practice dummy or `1.2` for doll play), the service checks whether another child **in the same house** is already running that exact order.
  - If another sibling in that house is already using the dummy or playing with a doll, the duplicate order is voided so two children do not collide on the same furniture marker.

---

## 4. Pets & Woodland Critters

Each child can independently own **one Follower Pet** (e.g., a dog brought home by the child) and **one Woodland Critter** (e.g., fox, rabbit, mudcrab, skeever):

- **AI Package & Faction Binding**:
  - Assigned pets and critters are added to `TTM_PlayerChildPetFaction` (`0xCF1`) and `PlayerFaction`.
  - Their `MARAS_HomeSandboxKeyword` linked reference is pointed **at the child actor**, and they are registered in [PackageOverrideService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PackageOverrideService.cpp) with the `PetCritterHomeSandboxPackage` so the animal sandboxes around its owner child inside the home.
  - Replacing or removing a pet/critter cleanly strips its factions and unregisters its package override (as long as no other child in the house is still linked to that same animal).
- **3-Tier Pet & Critter Ban / Cooldown Rules**:
  - A child is blocked from bringing home a new pet or critter if **any** of three conditions apply:
    1. **Global MCM/Settings Ban**: Pets or critters are disabled globally in M.A.R.A.S settings.
    2. **Per-Child Permanent Ban**: The player told that specific child they are never allowed to have a pet/critter (`petBanned` / `critterBanned`).
    3. **15-Day Rejection / Death Cooldown**: Whenever the player rejects a pet/critter request (or one dies), a retry timestamp is recorded for that child. The child cannot ask for another pet/critter until **15 in-game days** have elapsed.
- **Automatic Death Cleanup ([`DeathEventHandler`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/DeathEventHandler.cpp))**:
  - When a child's pet or critter dies (`TESDeathEvent` with `dead == true`), `DeathEventHandler` automatically removes the pet/critter assignment (`RemovePet` / `RemoveCritter`), records the 15-day retry cooldown (`RecordPetRejected` / `RecordCritterRejected`), issues the 24-hour `10.0f` ("Never Speak to You Again") order on the child if the player was the killer, and dispatches the `maras_child_pet_died` ModEvent.

---

## 5. Household Forcegreets, Sibling Scenes & Neglect-Weighted Selection

When the player returns home, quest scripts query `PlayerChildrenService` to decide which child should greet the player or trigger a household event:

- **Per-House Forcegreet Roll**:
  - Groups all real (non-`0xFF` dynamic) children by their assigned house and randomly picks **one child per house** who does not already have a pending forcegreet, setting their `Variable07` to `1.0`.
- **Neglect-Weighted Child Selection**:
  - Whenever the system needs to pick a child in a house (for a random greeting, a newly adopted welcome, a pet discovery event, or a critter discovery event), it weights each eligible candidate by **how many in-game days have elapsed since the player last interacted with that child** (minimum weight `0.1`, defaulting to `30.0` days if never interacted).
  - Children who haven't received attention recently are significantly more likely to approach the player than children the player just spoke to.
- **Newly Adopted Welcome & Household Chest Introduction**:
  - Tracks `isNewlyAdopted` per child (with an option to clear the flag across all children in the same house once the welcome scene plays) and a global `childChestIntroduced` flag so the family chest tutorial only happens once per playthrough.
- **Two-Child Sibling Scenes**:
  - Tracks whether each child has participated in **Scene 1** and **Scene 2** (sibling arguments/interactions). When querying a house for a sibling scene, the service filters for real children in that house who haven't played that scene yet and returns a random pair only if at least **2** eligible children live in that house.

---

## 6. Post-Load LinkedRef & Package Reassertion

Because Skyrim's engine does not persist runtime `ExtraLinkedRef` pointers or dynamic package overrides across save loads on its own, `PlayerChildrenService::ReassertAllOnLoad()` runs after every save load to:
- Re-sync `Variable06` and `Variable07` ActorValues on every child.
- Re-bind each child's `MARAS_HomeSandboxKeyword` linked reference to their personal home marker.
- Re-bind every active pet and critter's linked reference and sandbox package to their child owner and ensure their pet/player factions remain intact.

