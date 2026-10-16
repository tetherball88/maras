# PollingService — Periodic Heartbeat & Daily Rollover Scheduler

[PollingService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PollingService.cpp) acts as M.A.R.A.S's lightweight C++ heartbeat scheduler. Instead of running continuous Papyrus `OnUpdate` polling loops for follower tracking, child order timers, or midnight calendar transitions, `PollingService` runs throttled wall-clock checks while the game is unpaused and dispatches native C++ callbacks and SKSE ModEvents only when actual state transitions occur.

---

## 1. Initialization & Save-Load Baseline

When a save loads or a new game begins:
1. **Baseline Capture**: Captures the current set of active followers (`previousTeammates_`) and the current integer game day (`previousGameDay_`) immediately so that loading into a save does not falsely fire "follower added" or "new day" events.
2. **Player Faction Sanitization**: Runs an immediate `SanitizePlayerMarriedFaction()` pass via [FactionService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/FactionService.md) to ensure the player actor never retains the vanilla `PlayerMarriedFaction` by mistake.
3. **Catch-Up Engagement Check**: Schedules a game-thread task to run `CheckAutoMarriageForEngagedNPCs` in [NPCRelationshipManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCRelationshipManager.md) so any NPC-to-NPC engagements that matured past their 5-day threshold while away are promoted cleanly.

---

## 2. Throttled Real-Time Polling Loops

Whenever `Update()` is invoked (skipped automatically while any pause menu is open), the service evaluates three independent real-time timers:

### 15-Second Interval: Follower Detection & Player Faction Guard
Every **15 seconds** of unpaused real time:
- **Active Follower Scan**: Iterates through the engine's `highActorHandles` (loaded actors near the player), filters for M.A.R.A.S-registered NPCs, and checks whether each actor is currently following the player (`Utils::IsPlayerFollower`).
- **Delta Detection & ModEvent Dispatch**: Compares the current follower set against the previous snapshot. If any registered NPC joined or left the player's party:
  - Fires a summary `maras_teammate_change` SKSE ModEvent.
  - Fires individual `maras_teammate_added` (with the joining `Actor` as sender) and `maras_teammate_removed` (with the departing `Actor` as sender) ModEvents. These drive [SpouseBuffService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/SpouseBuffService.md) (toggling Lover's Radiance and follower multiplier buffs) and [AffectionService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/AffectionService.md) (awarding daily adventure affection).
- **Periodic Player Faction Sanitization**: Re-verifies that the player has not been added to `PlayerMarriedFaction` by vanilla scripts or third-party mods.

### 5-Second Interval: Child Order Expiration Tick
Every **5 seconds** of unpaused real time:
- Calls `PlayerChildrenService::GetSingleton().Tick(currentGameDay)` so timed child orders (`Variable06` chores, outdoor play, and hide-and-seek/tag sessions) expire promptly once their 3-in-game-hour window elapses.

### 5-Second Interval: In-Game Day Rollover Detection
Every **5 seconds** of unpaused real time, the service compares the integer part of `Calendar::GetDaysPassed()` against `previousGameDay_`.

---

## 3. Midnight / New-Day Rollover Pipeline

When the integer in-game day increments (whether through natural time passage, sleeping, waiting, or fast travel), `PollingService` orchestrates the daily household and relationship maintenance pipeline in deterministic order:

1. **Affection Daily Rollup & Decay**: Calls `AffectionService::OnDayChanged()` to commit yesterday's accumulated daily interactions into permanent affection, apply loneliness/jealousy decay, re-evaluate affection tiers, and decrement estranged divorce countdowns.
2. **Household Child Forcegreet Roll**: Calls `PlayerChildrenService::RollForcegreetForHousehold()` to roll whether one housed child will greet the player with a gift request, allowance question, or found critter today.
3. **Autonomous Matchmaking Check**: Calls `MatchmakingService::TryScheduledMatchmaking(currentDay)` to run background NPC-to-NPC pairing if the configured interval has elapsed.
4. **Secret Paternity Discovery Roll**: Calls `NPCRelationshipManager::CheckPaternitySecretsDailyTick(currentDay)` to roll the daily discovery chance for any active unrevealed paternity secrets when SkyrimNet is active.
5. **NPC Couple Auto-Marriage**: Calls `NPCRelationshipManager::CheckAutoMarriageForEngagedNPCs(currentDay)` to automatically wed NPC-to-NPC couples who have been engaged for at least 5 in-game days.
6. **Papyrus Broadcast**: Dispatches the `maras_day_changed` SKSE ModEvent (passing the new game day as `numArg`) so Papyrus services can run any script-side daily tasks.

