# FactionService — Feature & Mechanics Guide

**Implementation**: [FactionService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/FactionService.cpp)

`FactionService` hooks the Creation Engine's native `Actor::AddToFaction` and `Actor::RemoveFromFaction` functions via inline x86-64 prologue detours. It enables M.A.R.A.S to automatically detect vanilla or third-party marriage/divorce events (`PlayerMarriedFaction`), enforce player polygamy by blocking vanilla single-marriage lockouts on the player character, and broadcast real-time faction change notifications to internal listeners.

---

## 1. Inline Prologue Detour Hooks

At startup, `FactionService` decodes the instruction prologue of both `Actor::AddToFaction` (`RELOCATION_ID(36678, 37686)`) and `Actor::RemoveFromFaction` (`RELOCATION_ID(36680, 37688)`), copies the original instructions into an SKSE trampoline bridge, and installs 5-byte jump detours so **every** faction change in the game—whether triggered by Papyrus scripts, dialogue topic fragments, console commands (`addfac`), or other SKSE plugins—passes through `FactionService`.

---

## 2. Automatic Vanilla & Third-Party Marriage Synchronization

By intercepting changes to `PlayerMarriedFaction` (`0x000C6472`) on NPCs, M.A.R.A.S seamlessly integrates with vanilla Temple of Mara weddings, console commands, and custom follower marriage quests:

1. **External Marriage Detection (`a_rank >= 0`)**:
   - When an external script or quest adds an NPC to `PlayerMarriedFaction` and that NPC is not yet marked as `Married` to the player in [NPCRelationshipManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp):
     - Checks `CanFormRelationship(npc, Player, Married)` to verify that no hard family/incest or exclusivity block prevents the marriage.
     - Automatically promotes the NPC to **Married** with the player in `NPCRelationshipManager` (`PromoteToMarried`), triggering full M.A.R.A.S spouse onboarding (hierarchy slot assignment, buff application, child custody sync, and faction mirroring).
     - Immediately sanitizes `PlayerMarriedFaction` off the Player character if polygamy is enabled.
2. **External Divorce Detection (`RemoveFromFaction` or `a_rank < 0`)**:
   - When an external script or console command removes `PlayerMarriedFaction` from an NPC (or sets their rank `< 0`) while they are currently `Married` to the player in M.A.R.A.S:
     - Automatically transitions the pair to **Divorced** in `NPCRelationshipManager` (`PromoteToDivorced`), triggering full M.A.R.A.S divorce cleanup (hierarchy compaction, buff removal, and house/child custody updates).

---

## 3. Player Polygamy Protection (`SanitizePlayerMarriedFaction`)

In vanilla Skyrim, the `RelationshipMarriage` quest adds the **Player character (`0x14`)** to `PlayerMarriedFaction` upon their first wedding, and vanilla courtship dialogue checks whether the Player is in `PlayerMarriedFaction` to block all future Amulet of Mara proposals.

- **Real-Time Hook Blocking**:
  - Whenever `enablePolygamy` is active in M.A.R.A.S settings and any quest or script attempts to add `PlayerMarriedFaction` (`rank >= 0`) to the **Player** (`IsPlayerRef()`), `Hook_AddToFaction` intercepts and drops the call before it ever reaches the engine.
- **Active Sanitization**:
  - `SanitizePlayerMarriedFaction` also actively strips `PlayerMarriedFaction` from the Player if it was already present (for example, when enabling polygamy mid-playthrough on a save with an existing vanilla marriage), ensuring the player can continue courting and proposing to additional partners.

---

## 4. Re-Entrancy Prevention (`InternalFactionScope`)

Because `NPCRelationshipManager` itself modifies `PlayerMarriedFaction` and M.A.R.A.S factions when promoting or demoting NPCs, `FactionService` uses a thread-local scope guard (`InternalFactionScope`):
- While M.A.R.A.S is performing its own internal faction synchronization, `IsInternalFactionUpdate()` returns `true`.
- When `true`, `Hook_AddToFaction` and `Hook_RemoveFromFaction` bypass the external auto-promote/auto-demote marriage triggers (preventing infinite recursion) while still notifying registered faction listeners and forwarding the call to the original engine function.

