# IntimateThreadService — Feature & Mechanics Guide

**Implementation**: [IntimateThreadService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/IntimateThreadService.cpp)

`IntimateThreadService` monitors active adult animation threads (from **OStim Standalone** and **SexLab**) in real time to detect infidelity, register secret affairs, and trigger "caught in the act" or "open-relationship walk-in" events when a committed partner witnesses the scene or walks into the room mid-animation.

---

## 1. Thread Registration & Relationship Pre-Evaluation

When an OStim or SexLab animation thread starts (or changes participants), Papyrus bridge scripts register the thread (`framework` + `threadID` + participant list) with `IntimateThreadService`.

Before any sexual stage even begins, the service evaluates every participant against all active primary relationships (`Dating`, `Engaged`, `Married`) in [NPCRelationshipManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp):

1. **Group Scene Exemption**:
   - If an actor's committed partner is **also a participant in the same animation thread** (e.g., a threesome or group scene), that partner is not considered betrayed.
2. **Co-Spouse Intimacy Exemption**:
   - When two or more active main partners of the Player (`IsPlayerActiveMainPartner`) participate in intimacy with each other, it **never** counts as cheating on the Player (and does not trigger cheating or open-relationship walk-in events for the Player).
3. **Open Relationship Watcher Classification**:
   - If the relationship pair has `isOpen == true`, the absent partner is not a victim of cheating. Instead, they are cached as an **Open Relationship Watcher** so the system can still fire a consensual open-relationship witness reaction if they walk in.
3. **Player Polygamy Exemption**:
   - If the participant is the Player (`0x14`) and `enablePolygamy` is enabled in M.A.R.A.S settings, the player's absent partners do not classify the player as a cheater.
4. **Potential Cheater & Victim Caching**:
   - Any remaining absent committed primary partner (`isOpen == false`) is cached as a **potential victim of infidelity** for that participant.
- **Zero Overhead for Non-Drama Scenes**: If no participant in the thread has an absent committed partner (neither closed nor open), the background monitoring worker stays completely asleep on its condition variable (`cv_.wait`).

---

## 2. Sexual Stage Reporting & Instant Exposure Check

Merely starting an animation thread (such as an undressing, kissing, or idle hugging stage) does **not** count as infidelity. Cheating is only recorded when the animation framework reports an actual **sexual interaction pair** (`ReportIntimateInteraction` between Actor A and Actor B):

1. **Per-Thread Pair Deduplication**:
   - Once `(Actor A, Actor B)` has been recorded as a sexual pair in a given thread, subsequent stage changes between the same pair in that same thread session are ignored so the affair's `incidentCount` is only incremented **once per intimate session**.
2. **Dynamic Participant & Live Relationship Refresh**:
   - If an actor joins the scene dynamically before `RegisterIntimateThread` is re-invoked, `ReportIntimateInteraction` automatically adds them to the thread's participant list and refreshes the potential cheater and open-relationship watcher rosters against live `NPCRelationshipManager` state. Both `ReportIntimateInteraction` and `PollActiveThreads` re-verify that each `(cheater, victim)` or `(partner, witness)` pair is still in an active primary relationship (closed or open, respectively) and that the victim/witness is not the current sexual partner or a scene participant.
3. **Immediate Exposure Check at Stage Start**:
   - For each potential victim of Actor A or Actor B, the service immediately checks whether the victim is already witnessing the act (`CheckVictimExposure`):
     - **Victim Present at Start (`isWalkIn = false`)**:
       - Records the cheating incident (`RecordCheating` with `isSecret = false`, `AffairType::Casual`, `AffairStatus::Ongoing`).
       - Immediately transitions discovery state to `AffairDiscovery::Caught` (`SetCheatingDiscovered`).
       - If the victim is the Player, displays an on-screen HUD notification: `"You caught <Cheater> with <Paramour>!"`.
       - Fires the `maras_cheating_caught` SKSE ModCallbackEvent (`isWalkIn = false`).
     - **Victim Absent at Start**:
       - Registers or increments a **Secret Affair** (`RecordCheating` with `isSecret = true`, `AffairType::Casual`, `AffairStatus::Ongoing`).
4. **Immediate Open-Relationship Witness Check**:
   - If an actor's open-relationship partner is already watching when sexual activity begins, fires the `maras_open_relationship_walkin` SKSE ModCallbackEvent (`isWalkIn = false`).

---

## 3. Continuous Walk-In Monitoring (Background Worker & LOS Polling)

While at least one thread is active, a background worker thread wakes up on a periodic poll interval (`kPollInterval`) and dispatches a main-thread task (`PollActiveThreads`) whenever the game is unpaused. Each tick sends the missed-end-event verification (§5) and, for threads with recorded sexual activity and potential cheaters or open-relationship watchers, evaluates walk-ins:

### How Victim / Witness Exposure Is Determined (`CheckVictimExposure`)
A committed partner (victim or open-relationship witness) is considered to have **caught/witnessed** the scene if **all** of the following are true:
1. Both the witness/victim and the participating partner are alive, 3D-loaded, and in the **exact same cell** (`GetParentCell()`).
2. **Sleeping Exemption**: The witness/victim is **not** sleeping, trying to sleep, or waiting for a sleep animation (`SIT_SLEEP_STATE`). A spouse asleep in the next bed will not "catch" an affair happening quietly unless woken up.
3. **Distance & Line-of-Sight Thresholds**:
   - **Close Proximity (`<= kCloseProximityDistance`)**: The witness is standing right next to the couple (caught automatically regardless of camera/facing angle), **OR**
   - **Visual Distance + Engine Line-of-Sight (`<= kVisualDistance` AND `HasLineOfSight`)**: The witness is within visual range and has unobstructed engine line-of-sight to the cheater.

### Walk-In Discovery Resolution (`isWalkIn = true`)
- When an absent victim walks into the cell/room mid-scene and satisfies `CheckVictimExposure` while an **ongoing secret affair** is active in the thread:
  - Upgrades the secret affair to `AffairDiscovery::Caught` (`SetCheatingDiscovered`).
  - Deduplicates the event per `(cheater, victim, affairPartner)` within the thread so the reaction only fires once per session.
  - Shows `"You caught <Cheater> with <Paramour>!"` on the HUD if the player is the victim.
  - Broadcasts the `maras_cheating_caught` SKSE ModCallbackEvent with `isWalkIn = true` and the victim actor as the event sender.
- Similarly, if an open-relationship partner walks in mid-scene, broadcasts `maras_open_relationship_walkin` with `isWalkIn = true` (deduplicated once per thread session).

---

## 4. Children Flee From Intimate Scenes

While **any** OStim/SexLab thread is registered (cheating-relevant or not), `IntimateThreadService` reports the positions of the loaded participants, grouped by cell, to the [ChildrenFleeService](ChildrenFleeService.md) (via its threat provider) and queues a refresh whenever a thread is registered or cleared. Everything about which children flee, how they keep fleeing, and the blocked-path teleport lives in that service.

## 5. Missed End-Event Safety Net

Framework end events (`ostim_thread_end`, `HookAnimationEnd`) are the normal way a thread is cleared. As a safety net, every poll tick `PollActiveThreads` sends a `maras_verify_intimate_thread` mod event (framework as `strArg`, thread ID as `numArg`) for each tracked thread. `TTM_ServiceIntimateMoments.OnVerifyIntimateThread` checks the framework (`OThread.IsRunning` for OStim, an empty participant list for SexLab) and, if the thread is gone, runs the same cleanup as the end event (clears the thread, tracking flags and cheater factions). This prevents a lost end event from leaving a phantom thread that keeps children fleeing.
