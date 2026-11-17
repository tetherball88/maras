# `BuffReconciler`, `SpouseBuffRules` & `BonusesConfigLoader` — Architecture & Behavioral Guide

**Layers**: Engine (`SKSE_Source/modules/engine`), Domain Household (`SKSE_Source/modules/domain_household`), State (`SKSE_Source/modules/state`)  
**Sources**:
- [BuffReconciler.h](../../../SKSE_Source/modules/engine/include/maras/engine/BuffReconciler.h) / [BuffReconciler.cpp](../../../SKSE_Source/modules/engine/src/BuffReconciler.cpp)
- [SpouseBuffRules.h](../../../SKSE_Source/modules/domain_household/include/maras/domain/household/SpouseBuffRules.h) / [SpouseBuffRules.cpp](../../../SKSE_Source/modules/domain_household/src/SpouseBuffRules.cpp)
- [BonusesConfigLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/BonusesConfigLoader.h) / [BonusesConfigLoader.cpp](../../../SKSE_Source/modules/engine/src/BonusesConfigLoader.cpp)
- [EngineSyncPlan.cpp](../../../SKSE_Source/modules/state/src/EngineSyncPlan.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

M.A.R.A.S. grants dynamic passive gameplay bonuses from married spouses based on **Social Class** (permanent household buffs active anywhere in Skyrim) and **Skill Type** (follower combat buffs active while married spouses travel as teammates). Pure multiplier math lives in `EngineSyncPlan` and `SpouseBuffRules`, JSON category definitions load through `BonusesConfigLoader`, and idempotent Skyrim perk/spell application executes inside `BuffReconciler`.

---

## 2. Config Loading (`BonusesConfigLoader` & `bonuses.json`)

- **Path Resolution**: Searches `Data/SKSE/Plugins/MARAS/bonuses.json` then `SKSE/Plugins/MARAS/bonuses.json`, initializing at `kDataLoaded` and reloading on save load / new game.
- **Categories (`14` Total)**: Indexes all `8` Social Classes (`outcast`, `poverty`, `working`, `middle`, `wealthy`, `religious`, `nobles`, `rulers`) and `6` Skill Types (`warrior`, `mage`, `rogue`, `craftsman`, `ranger`, `orator`).
- **Entry Types**: Each entry targets a `perk` (`__formData|Plugin|Hex`), `effectIndex`, `type` (`"spell"`, `"multiply"`, or `"add"`), display `name`/`description`/`unit`, and base `value`. Reloading invalidates `BuffReconciler`'s resolved `BGSPerk*` cache.

---

## 3. Multiplier Formulas (`EngineSyncPlan` & `SpouseBuffRules`)

Only **living, non-deceased married spouses** contribute; dead spouses in `DeceasedTable` are excluded.

### Affection Multiplier (`ComputeAffectionMultiplier`)
- `>= 75` (*Happy*): `1.25x`
- `50 .. 74` (*Content*): `1.00x`
- `25 .. 49` (*Troubled*): `0.25x`
- `< 25` (*Estranged*): `0.00x` (zero contribution regardless of rank or follower status)

### Hierarchy Rank Multiplier (`ComputeHierarchyRankMultiplier`)
Applies to **Permanent Social Class Buffs**:
- **Monogamy Devotion (`livingMarriedCount == 1`)**: `2.00x` (yields up to `2.50x` when combined with *Happy* `1.25x`).
- **Polygamy (`livingMarriedCount >= 2`)**: Rank `0` (Lead Spouse) $\rightarrow$ `1.00x`, Rank `1` $\rightarrow$ `0.50x`, Rank `2` $\rightarrow$ `0.25x`, Rank `3+` $\rightarrow$ `0.10x`.
- **Follower Skill Type Buffs**: Exempt from hierarchy rank decay—every active married teammate contributes `1.00x * affectionMultiplier` (`0.00x .. 1.25x` per teammate).
- **Additive Bucket Stacking**: Spouses sharing the same Social Class or active married teammates sharing the same Skill Type sum additively into that category's multiplier bucket.

### Engine Target Value (`ComputeBonusTargetValue`)
- `"multiply"`: Scaled delta = `(baseValue / 100.0) * multiplier`; engine entry-point target = `1.0 + delta` (inactive fallback `1.0`).
- `"spell"`: Positive magnitude `abs(baseValue) * multiplier` written directly to `BGSAbilityPerkEntry`'s `ability->effects[0]`.
- `"add"`: Signed magnitude `baseValue * multiplier` written to `BGSEntryPointFunctionDataOneValue`.

---

## 4. Idempotent Engine Reconciliation (`BuffReconciler`)

- **Main-Thread Marshalling**: `ReconcilePlayerBuffs()` checks `DeferredActorResolver::CanSyncEngineInline()`. Off-thread callers coalesce via `pendingReconcile_` atomic exchange into a single `SessionEpoch`-guarded SKSE main-thread task acquiring `GetPapyrusMutex()`.
- **Grouped Perk Updates**: `RebuildCacheIfNeeded()` groups multiple bonus entries referencing the same `RE::BGSPerk*`. When no entry in a perk group is active, `BuffReconciler` removes the perk; when magnitudes drift beyond `kMagnitudeEpsilon = 1e-5` (or `forceRefreshOnLoad_` is set post-load) on an already-active perk, it patches spell/entry-point magnitudes and refreshes each `BGSPerkEntry` in-place via `RemovePerkEntry` $\rightarrow$ `ApplyPerkEntry` (avoiding `PlayerCharacter::RemovePerk` + `AddPerk` back-to-back, since `RemovePerk` queues a deferred end-of-frame task that would strip the perk after synchronous `AddPerk` no-ops).
- **Dashboard Reporting (`EvaluateHouseholdBuffs`)**: [`HouseholdBuffsJsonBuilder`](UIDataBridge.md) evaluates the identical `SpouseBuffRules` snapshot purely read-only to display live bonuses, active contributors, and at-home potential follower contributors.

---

## 5. Sleep Rested Abilities & Partner Sleep Affection (`SleepEventSink`)

When `RE::TESSleepStopEvent` fires, [`SleepEventSink`](../../../SKSE_Source/modules/engine/include/maras/engine/SleepEventSink.h) collects active high actors sharing the Player's parent cell plus household tenants physically present at the Player's current `BGSLocation` and dispatches into `AppLifecycleCoordinator::OnPlayerSlept`:

- **Slept Affection**: `EvaluateSleepPaidAffectionPartners` selects deduplicated same-cell living partners (`Married`, `Engaged`, `Dating`, or `Lover`) and applies `AffectionEventCoordinator::AddSleptAffection`.
- **Rested Buff Selection (`EvaluateSleepRestedPlan`)**:
  - When a `Lover`, `Dating`, or `Engaged` tenant is present in the Player's house, selects `LoversRadiance` (`0xDA`); otherwise, when a `Married` spouse tenant is present, selects `MarriageSleep` (`0xCDA1D`, vanilla *Lover's Comfort*). Non-spouse lovers intentionally take precedence over spouses if both are present.
  - Independently, when a custodied non-adult child is present in the Player's house, selects `BYOHAdoptionSleepAbilityMale` (`0x2F54`) or `BYOHAdoptionSleepAbilityFemale` (`0x4292`) according to Player sex.
- **Vanilla Synchronization**: Before adding the selected ability, `SleepEventSink` invokes `PlayerSleepQuestScript.RemoveRested()` / `RemoveAdoptionRested()` and waits for the `SessionEpoch`-guarded VM stack callback before running `BuffReconciler::ApplySleepRestedPlan` on the main game thread (falling back directly if the vanilla quest VM handle is unavailable).
