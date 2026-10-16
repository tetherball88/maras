# ChildrenFleeService - Feature & Mechanics Guide

**Implementation**: [ChildrenFleeService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/ChildrenFleeService.cpp)

`ChildrenFleeService` makes children run away from a cell that contains a **threat**. A threat is any danger point grouped by cell. Two sources feed it:

- **Intimate scenes**: `IntimateThreadService` registers a provider that reports the positions of all active scene participants. Children flee automatically while a scene is running in their cell (see [IntimateThreadService](IntimateThreadService.md)).
- **Manual request**: `MARAS.FleeChildrenFromPlayerCell()` (Papyrus) or `ChildrenFleeService::FleeFromPlayerCell()` (C++, e.g. from the Prisma UI bridge) registers the player's current cell and position as a threat until `MARAS.StopChildrenFleeing()` / `StopFleeing()` is called **or the player leaves that cell** (checked every refresh, so it ends within about 1.5 seconds of a cell change).

The feature has no SkyrimNet dependency and no user setting.

## 1. Who Flees

A refresh runs on the game thread about every 1.5 seconds while there is anything to monitor, and immediately (queued task) whenever a source requests it:

1. **Threatened cells**: the cells reported by the sources, read live each refresh.
2. **Fleeing children**: every loaded, living, 3D-loaded child located in a threatened cell (child race via `IsChild()` **or** a child in the MARAS child registry, which covers fertility-mod-spawned children) who is **not sleeping** (sleeping or waiting for the sleep animation are exempt).
3. **Origin cell**: each child is registered together with the cell it flees from and the time it started. It stays registered for as long as a threat remains in that cell, even if the flee package carries it out of the cell (through a door or out of the loaded area), so it does not flip between fleeing and not fleeing. It is released when it falls asleep, dies, or no threat remains in its origin cell. A child standing in a different threatened cell flees from that cell with a fresh timer.
4. **Application**: the set is published to `PackageStartHook`, which returns `TTM_ChildrenFleeIntimateScene` (`0x5DE`, resolved via `FormCache`) for those actors, and `EvaluatePackage` is called on actors whose state changed. On release the actor falls back to its normal stack (including the MARAS sandbox/follow packages if registered).

## 2. Blocked Path

Actors in the scene (or the player) can block a child's way out. A child that is still in its origin cell and within 800 units of a threat 12 seconds after it started fleeing is teleported away:

- **Indoors**: through the load door closest to the child, to the other side of that door. The load doors of each interior cell are scanned once and cached until the next session reset.
- **Outdoors**: to a walkable navmesh spot about 2000 units directly away from the threat (the center of the navmesh triangle at least 1000 units from the threat that is closest to that target), so the child never lands inside geometry.

The child **keeps fleeing** (it stays in the flee set, so it does not walk back through the door) until no threat remains in its origin cell. Its timer restarts after the teleport, so a child that is still blocked is retried every 12 seconds. If an interior has no usable load door, or an exterior cell has no usable navmesh spot, the child is left alone (warning logged).

## 3. Lifecycle

- The service owns a small worker thread that posts a refresh task while there are threats or fleeing children; it idles otherwise.
- Session reset (`kPreLoadGame` and co-save revert) silently clears all flee state, manual requests and the door cache. No co-save state is involved, so a manual flee request does not survive a load.
