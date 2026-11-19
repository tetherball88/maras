# FactionReconciler

**Sources**: [FactionReconciler.h](../../../SKSE_Source/modules/engine/include/maras/engine/FactionReconciler.h) · [FactionReconciler.cpp](../../../SKSE_Source/modules/engine/src/FactionReconciler.cpp) · [DeferredActorResolver.h](../../../SKSE_Source/modules/engine/include/maras/engine/DeferredActorResolver.h) · [DeferredActorResolver.cpp](../../../SKSE_Source/modules/engine/src/DeferredActorResolver.cpp) · [EngineSyncPlan.h](../../../SKSE_Source/modules/state/include/maras/state/EngineSyncPlan.h) · [EngineSyncPlan.cpp](../../../SKSE_Source/modules/state/src/EngineSyncPlan.cpp)  
**Cross-References**: [WorldStoreAndCoSave.md](WorldStoreAndCoSave.md) · [PackageReconciler.md](PackageReconciler.md) · [LineageUseCases.md](LineageUseCases.md) · [ARCHITECTURE.md](../ARCHITECTURE.md)

## Overview

In M.A.R.A.S, `TT_MARAS.esp`, vanilla, and Hearthfire factions are strictly **non-authoritative engine mirrors** computed from immutable `WorldStore` snapshots (`EngineSyncPlan`) and applied idempotently on Skyrim's main thread by `FactionReconciler` (`modules/engine`). Unloaded or off-thread requests coalesce through `DeferredActorResolver`.

---

## Pure Sync Plans & Convergence

`EngineSyncPlan` (`modules/state`, `pch_core.h`) computes pure diff targets without engine dependencies, and `FactionReconciler` applies only changed ranks (`SyncActorFaction`, `ApplyMembershipOp`):

| Target Category | Plan Producer | Mirrored Factions & Edge Rules |
| :--- | :--- | :--- |
| **Player (`0x14`)** | `ComputePlayerFactionPlan` | Synchronizes `PlayerMarriedFaction` (`0x000C6472`, cleared when polygamy is enabled or `spousesCount == 0`) and `CanFormNonCheatingMainFaction` (`1` if polygamy enabled or no closed main relationship exists, `0` otherwise). |
| **Registered Adults & Graduated Stage 4** | `ComputeAdultActorFactionPlan` | Converges MARAS ranked factions (tracked status rank, social class, skill type, temperament, hierarchy, affection, spouse children count, cheating/open flags, non-cheating main rank, housed rank), vanilla marriage/follower factions, and clears young-child factions + optionally restores follower/werewolf factions on graduated Stage 4 adults. For MARAS or vanilla `BGSRelationship` Player family members, assigns `TTM_PlayerFamilyBlocked` (`0xDD4` rank `0`), suppresses Player-romance factions (`trackedRank`, hierarchy, affection, cheating/open, shared house) while preserving archetype/non-cheating-main ranks for NPC-NPC matchmaking, and force-strips `PotentialMarriageFaction` (`0x19809`) even when inherited from a `TESNPC` base record (`forceStripMarriagePotential`). Otherwise, `IsNativeBaseFaction` (`preserveIfNativeOnBase`) prevents stripping native `TESNPC` base factions when removing MARAS flags. |
| **Young Children (`Stage 0–3`)** | `ComputeChildFactionPlan` | Converges `PlayerChildFaction`, `BYOHRelationshipAdoptionChildOwnedFaction`, `BYOHRelationshipAdoptable` (rank `25` in household, rank `0` at Honorhall orphanage marker), `PlayerFaction`, and dual-parent marriage indicator rank (`2` if other spouse is male, `1` otherwise), plus saved crime faction restoration (`ClearChildHouseholdFactions` strips these when leaving custody). |
| **Child Pets & Critters** | `ComputeCompanionFactionPlan` | Synchronizes `PlayerChildPetFaction` and `PlayerFaction` when assigned to a tracked living child. |

Unregistered/removed actors reaching step 4 of `ReconcileLoadedActor` first check whether `ExtraFactionChanges` or any static `TT_MARAS.esp` base faction is present (`HasCandidateMarasFactionsOnUnknownActor`), skipping the 25-faction scrub completely on untouched creatures/NPCs while still stripping lingering companion and MARAS factions safely via `ClearUnknownActorMarasFactions` when present.

---

## Engine Faction Hooks (`Hook_AddToFaction` / `Hook_RemoveFromFaction`)

`FactionReconciler::InstallHooks()` detours Skyrim's native `AddToFaction` (`RELOCATION_ID(36678, 37686)`) and `RemoveFromFaction` (`RELOCATION_ID(36680, 37688)`) functions at plugin startup:

- **Instruction-Safe x64 Detour**: `InstallDetourHook` handles already-hooked targets (`E9` chaining) or decodes x64 prologue instruction boundaries (`DecodePrologueSize`) so instructions (including RIP-relative instructions) are never sliced; if unrecognized, it aborts hooking cleanly and logs an `ERROR`. Allocates a 14-byte absolute jump (`WriteAbsoluteJump`) bridge on SKSE's trampoline.
- **Zero-Allocation Fast-Exit for Non-Marriage Factions**: When `a_faction->GetFormID()` is not `PlayerMarriedFaction` (`0x000C6472`) or vanilla courtship/potential factions (`PotentialMarriageFaction` `0x00019809`, `MarriageAskedFaction` `0x000FF7F3`, `CourtingFaction` `0x0007431A`) and no external faction listeners are registered (`!HasListeners()`), both hooks jump straight to `_originalAddToFaction` / `_originalRemoveFromFaction` without copying `WorldStore::Snapshot()` or scheduling main-thread SKSE tasks on off-thread Papyrus/AI calls.
- **Re-Entrancy Suppression (`InternalFactionScope`)**: Sets `thread_local int s_internalFactionDepth` while MARAS performs its own `AddToFaction`/`RemoveFromFaction` writes so external auto-promotion/demotion hooks are skipped while still dispatching `NotifyFactionAdded` / `NotifyFactionRemoved` callbacks to registered listeners (such as adoption tracking).
- **Polygamy, Player Family & Vanilla Marriage Intercept**: When an external mod or vanilla quest adds `PlayerMarriedFaction` (`0x000C6472`) to the Player while `enablePlayerPolygamy` is enabled, the hook blocks the addition so vanilla marriage quest conditions remain compatible with multiple spouses. External additions of `PotentialMarriageFaction` (`0x00019809`), `MarriageAskedFaction` (`0x000FF7F3`), or `CourtingFaction` (`0x0007431A`) on Player family members are blocked inline on the main thread or reconciled away when dispatched off-thread. External `PlayerMarriedFaction` additions/removals on NPCs marshal via `AddCurrentEpochTask` under `GetPapyrusMutex()` into `OnExternalMarriageFactionAdded` (if `CanAcceptExternalMarriage` permits, otherwise reverted) or `OnExternalMarriageFactionRemoved`.

---

## `DeferredActorResolver` (`PendingEngineSyncSet` & Thread Gate)

```mermaid
sequenceDiagram
    participant Caller as Off-Thread or Save-Load Caller
    participant DAR as DeferredActorResolver
    participant Sink as ActorInitLoadedEventSink
    participant Sync as FactionReconciler / PackageReconciler

    Caller->>DAR: RequestSync(actorId, SyncFlags::Factions | Package)
    alt Main thread (!init wait) AND actor loaded
        Sync->>Sync: ReconcileLoadedActor(actor) inline
    else Off main thread
        DAR->>DAR: Coalesce SyncFlags into pendingSync_ under leaf mutex_
        DAR-->>Caller: Schedule single SessionEpoch-fenced DrainPending task
    else Actor nullptr (unloaded)
        DAR->>DAR: MarkUnloaded(actorId, flags) in unloadedActors_
    end
    Sink->>DAR: OnActorLoaded(actor) on main thread
    DAR->>Sync: Drain unloadedActors_ / pendingSync_ for actor
```

- **Main-Thread Identity Rule**: `CaptureMainThread()` is called strictly during `SKSEPluginLoad` / `kInputLoaded` on Skyrim's WinMain thread — **never** during `kDataLoaded` (`InitTESThread`) and **never** treating `BSTaskPool`/`BSJobs` worker threads as the main thread inside `DeferredActorResolver`.
- **Leaf Mutex Discipline**: `DeferredActorResolver::mutex_` protects only `pendingSync_`, `unloadedActors_`, `drainScheduled_`, and `queuedEpoch_`; queued flags are moved out before calling engine/reconciler/`WorldStore` APIs.
- **Save Boundaries**: `ReconcileAllOnLoad` converges Player + all registered adults, children, pets/critters, and tenants after co-save load (recording unloaded ones in `DeferredActorResolver`), while `Revert()` clears `pendingAllOnLoad_` and both `DeferredActorResolver` maps cleanly.
