# `IntimateThreadService` — Architecture & Behavioral Guide

**Layer**: Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [IntimateThreadService.h](../../../SKSE_Source/modules/engine/include/maras/engine/IntimateThreadService.h)
- [IntimateThreadService.cpp](../../../SKSE_Source/modules/engine/src/IntimateThreadService.cpp)
- [AppLifecycleCoordinator.cpp](../../../SKSE_Source/modules/app/src/AppLifecycleCoordinator.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`IntimateThreadService` bridges third-party intimacy frameworks (e.g., OStim, SexLab) and M.A.R.A.S.'s infidelity, open-relationship, child-flee, and SkyrimNet narration systems. It tracks running scene threads, identifies potential cheaters and open-relationship participants against [`WorldStore`](WorldStoreAndCoSave.md), detects bystander walk-ins by cheated or open partners, records cheating incidents, and supplies active threat coordinates to [`ChildrenFleeService`](ChildrenFleeService.md).

---

## 2. Participant Evaluation & Exemptions

When `RegisterIntimateThread(framework, threadID, actors)` deduplicates valid participant `FormID`s, `EvaluateParticipants` reads an atomic `WorldStore::Snapshot()` (read-only CQS) and inspects every main-tier relationship (`Dating`, `Lover`, `Engaged`, `Married`) held by each participant:

1. **Group Scene Exemption**: If the committed partner is already a participant in the same thread, it is neither cheating nor an uninvolved bystander scenario.
2. **Co-Spouse Intimacy Exemption**: If the absent partner is the Player (`0x14`) and **every** other adult participant in the thread is also an active Player main-tier partner (no third-party outsider present), intimacy between co-partners does not constitute cheating on the Player.
3. **Open Relationship (`isOpen`)**: Absent partners whose edge has `isOpen = true` are routed into `OpenRelationshipWatcher` lists instead of `PotentialCheater` victims.
4. **Player Polygamy Exemption**: When `SettingsData::enablePlayerPolygamy` is enabled, the Player (`0x14`) is never flagged as cheating during participant evaluation.
5. **Absent Closed Partners**: Remaining absent main-tier partners become cheating victims (`PotentialCheater`).

---

## 3. Walk-In Bystander Detection & Polling

- On registration, any thread with cheating or open-relationship potential starts a `Tick()` monitor (`kPollInterval = 1000 ms` driven by [`PollingService`](PollingService.md)).
- Each tick inspects active threads under `mutex_` and checks loaded nearby actors (line-of-sight / distance / cell proximity) for absent victims or open partners walking in:
  - **Victim Walk-In**: Triggers immediate cheating discovery (`AppLifecycleCoordinator::OnIntimateCheatingCaught`), which acquires `GetPapyrusMutex()`, invokes [`RelationshipUseCases`](RelationshipUseCases.md) to expose/record the incident, emits [`SkyrimNetMemoryService`](SkyrimNetMemoryService.md) infidelity memories + `ttm_cheating_caught` trigger (`SkyrimNet` availability gated at caller level), and pushes refreshes to [`PrismaUIDataBridge`](PrismaUIDataBridge.md) if the dashboard is open.
  - **Open Partner Walk-In**: Fires the open-relationship walk-in notification without punishing the couple as cheaters (`ttm_open_relationship_walkin` when SkyrimNet is available).
- **Direct Infidelity Recording & Pending Intent Upgrade**: When a thread finishes or direct recording runs outside walk-in confrontation (`RecordCheatingIncidentDirect`), `WorldStore::Modify` appends the `AffairRecord` (`AffairType::Casual`, `AffairStatus::Ongoing`, `AffairStage::Physical`). If the cheater previously held a `ConfessTalking` pending partner intent, `WorldState::RecordCheatingIncident` upgrades it to `ConfessPhysical` and emits `maras_partner_intent_changed`, followed by [`FactionReconciler`](FactionReconciler.md) updates if the Player was the victim.

---

## 4. Integration & Lifecycle Invariants

- **Child Protection Hook**: `IntimateThreadService::Initialize()` registers `CollectSceneThreats` as [`ChildrenFleeService`](ChildrenFleeService.md)'s threat provider so active threads automatically push children out of the scene cell.
- **Player Intimacy & End-of-Thread Affection (`GiftAndIntimacyRules`)**: `RegisterIntimateThread` invokes pure `EvaluateIntimateThreadParticipants` to record deduplicated tracked non-player participants when the Player is present. OStim scene changes and SexLab stage events call `HasIntimateThreadPlayerParticipants` and `MarkIntimateThreadSexual` when sexual actions occur. When `ClearIntimateThread` completes the thread, `AppLifecycleCoordinator::OnIntimateThreadEnded` runs `EvaluateIntimateThreadCompletion` and awards `15.0` base intimacy affection if sexual actions occurred (or `8.0` for non-sexual/foreplay threads).
- **Mid-Act Confronted Faction (`0x5C4`) Tracking**: For active threads with cheating/open potential (or upon in-scene exposure), `IntimateThreadService` manages `CheatingNpcCaughtFaction` (`0x5C4`) additions and overlap-safe removals directly on the main thread when all threads involving that actor end.
- **Session Reset**: `ClearAll()` clears active threads under `mutex_` on `kPreLoadGame` / revert and notifies [`ChildrenFleeService`](ChildrenFleeService.md).
- **Locking Discipline**: Thread maps are protected by an internal `std::mutex`; callers copy snapshots out before invoking Papyrus/SKSE mod events, `WorldStore::Modify`, faction changes, or `ChildrenFleeService::QueueRefresh()`.
