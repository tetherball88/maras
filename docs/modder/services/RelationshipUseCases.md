# `RelationshipUseCases` & Domain Relationship Rules — Architecture & Behavioral Guide

**Layers**: App (`SKSE_Source/modules/app`), Domain Relationships (`SKSE_Source/modules/domain_relationships`), Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [RelationshipUseCases.h](../../../SKSE_Source/modules/app/include/maras/app/RelationshipUseCases.h) / [RelationshipUseCases.cpp](../../../SKSE_Source/modules/app/src/RelationshipUseCases.cpp)
- [RelationshipRules.h](../../../SKSE_Source/modules/domain_relationships/include/maras/domain/relationships/RelationshipRules.h) / [InfidelityRules.h](../../../SKSE_Source/modules/domain_relationships/include/maras/domain/relationships/InfidelityRules.h) / [SpouseHierarchyRules.h](../../../SKSE_Source/modules/domain_relationships/include/maras/domain/relationships/SpouseHierarchyRules.h)
- [DialogueOutcomeCoordinator.h](../../../SKSE_Source/modules/app/include/maras/app/DialogueOutcomeCoordinator.h) / [PromptStringsLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/PromptStringsLoader.h) / [JsonOverrideLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/JsonOverrideLoader.h)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`RelationshipUseCases` is the Layer-4 application coordinator governing M.A.R.A.S.'s romantic, marital, hierarchy, infidelity, affection mutation, and NPC trait classification workflows. It bridges pure domain validators (`RelationshipRules`, `InfidelityRules`, `AffectionRules`, `SpouseHierarchyRules`) with [`WorldStore`](WorldStoreAndCoSave.md) transactions, precomputes engine metadata safely outside locks, and triggers downstream engine reconciliation and cross-layer notifications. Vanilla topic-info prompt lookups and conversational dialogue outcomes are coordinated alongside it by `DialogueOutcomeCoordinator` using `PromptStringsLoader` (`SKSE/Plugins/MARAS/promptStrings.json`).

---

## 2. Core Workflows & Domain Rules

- **Symmetric Pair Relationships (`RelationshipRules`)**:
  - Manages canonical pairs `(min(A, B), max(A, B))` across candidate, active (`Dating`, `Lover`, `Engaged`, `Married`), and historical (`ExDating`, `ExLover`, `Jilted`, `Divorced`) tiers, enforcing incest/family barriers (`AreFamilyMembers` normalized across `BaseId`/`RefId`), breakup cascades, open-relationship eligibility (`CanOpenRelationship`), offline affection floor synchronization (`100` Married/Engaged, `80` Dating floor, `70` Lover floor, `0` breakup/ex, `ComputeInitialCandidateAffection` on initial Candidate), breakup cooldown timestamps (`Divorced`, `Jilted`, or cheating ex), and marriage acceptance evaluation via [`MarriageDifficulty`](MarriageDifficulty.md).
- **Dialogue-Open Chances, Income Sharing & Divorce Settlements (`RelationshipInteractionCoordinator` & `RelationshipEngineReconciler`)**:
  - Computes No-AI dialogue chance globals (`0x5D0`, `0x5D1`, `0x15C`) and optional `TT_LoversLedger` intimacy bonuses (`MARAS.ApplyDialogueLoversLedgerBonus`), pays daily spouse income shares (`ComputeSpouseIncomeShare`: `100` gold/day clamped `[0, 500]`), and executes `ComputeDivorceSettlement` (`100` court fee + `30%` gold capped at `1000` + owned houses/horses/quests/dungeons assessment on a `7`-day cooldown) through `ModalManager`.
  - `RelationshipEngineReconciler` applies vanilla relationship rank updates (`SetRelationshipRank`), breakup/demoted/pacify spells (`0x102`, `0x10E`, `0x167`), and `$TTM_MSG_*` HUD notifications safely post-lock.
- **Spouse Hierarchy (`SpouseHierarchyRules`)**:
  - Maintains up to `4` ranked spouse slots (`ApplySetHierarchyRank`), auto-promoting and compacting slots cleanly on marriage, divorce, or spouse death while emitting rank-shift events (`maras_hierarchy_changed`) and recalculating player spouse buffs ([`BuffReconciler`](BuffReconciler.md)).
- **Directional Infidelity & Open Consent (`InfidelityRules`)**:
  - Evaluates asymmetric `AffairRecord` ledgers (`IsActorCheating`, `IsRelationshipCheating`), respecting per-pair `isOpen` consent, `enablePlayerPolygamy`, and Player co-spouse exemptions while separating secret vs. discovered vs. confronted affairs.
- **Pending Relationship Imports (`TryResolvePendingRelationships`)**:
  - Resolves vanilla/mod pre-existing relationships queued by `BaseId` during startup scans as soon as both `RE::Actor` references initialize/load (`OnActorLoaded`), setting historical baseline entries (`timestamp = -1.0f`, `fromStatus = Unknown`).
- **Quest Command Execution (`ExecuteQuestCommand`)**:
  - Executes scriptless quest stage actions dispatched by [`QuestEventHandler`](QuestEventHandler.md) (`promoteToStatus`, `setAffection`, `changeAffection`, `addDailyAffection`, `setSocialClass`, `setSkillType`, `setTemperament`, `onDeath`).
- **Death & Unregistration Cleanup**:
  - Before tearing down live relationship/household rows on `HandleNPCDeath`, captures actor display names and metadata via `CachedActorMetadataProvider`, archives the full record + secret flags into `DeceasedTable`, cleans up third-party affair/intent references, evicts home shares, reconciles surviving partners/children, and refreshes [`BuffReconciler`](BuffReconciler.md).

---

## 3. Concurrency, Override Mutex & Precomputation Invariants

To uphold zero engine contention under `WorldStore::writeMutex_`:

1. **Engine Lookups Before `Modify()`**: All `RE::TESForm::LookupByID` calls (actor base resolution, keyword lookups, `SpatialWorldIndex` lookups, `ActorSvc::GetName`, `RE::Calendar` timestamps, and `CachedActorMetadataProvider::CaptureActors`) are gathered into stack variables **before** entering `WorldStore::GetSingleton().Modify(...)`.
2. **Override Table (`overridesMutex_`) Discipline**: `LoadOverridesFromFolder` loads JSON files via `JsonOverrideLoader::LoadOverridesFromFolder` without holding locks, then swaps `npcOverrides_` under a brief `std::unique_lock lock(overridesMutex_)`. Query helpers (`GetSocialClassOverride`, `GetSkillTypeOverride`, `GetTemperamentOverride`) compute `baseId` via [`SpatialWorldIndex`](SpatialWorldIndex.md) **before** taking `std::shared_lock lock(overridesMutex_)`—preventing lock-order inversions.
3. **Post-Commit Engine Reconciliation**: Only after `Modify()` publishes a new `WorldStateSnapshot` does `RelationshipUseCases` invoke [`FactionReconciler`](FactionReconciler.md), [`PackageReconciler`](PackageReconciler.md), `GlobalVarReconciler`, [`BuffReconciler`](BuffReconciler.md), [`SkyrimNetMemoryService`](SkyrimNetMemoryService.md) (caller-gated via `IsSkyrimNetAvailable()`), and [`UIDataBridge`](UIDataBridge.md).

---

## 4. Strict CQS Diagnostics (`DumpJson`, `DumpHierarchyJson`, `DumpAffectionJson`)

All JSON dumps are `const` snapshot readers taking [`PerspectiveView`](PerspectiveView.md): `DumpJson(view)` filters cheating/affair visibility and status history through `view` (`ForDashboard` in UI dumps) while leaving zero side effects on live tables.
