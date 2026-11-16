# `LineageUseCases`, Fertility, Child Spawn & Deceased Dumps — Architecture & Behavioral Guide

**Layers**: App (`SKSE_Source/modules/app`), Domain Lineage (`SKSE_Source/modules/domain_lineage`), Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [LineageUseCases.h](../../../SKSE_Source/modules/app/include/maras/app/LineageUseCases.h) / [LineageUseCases.cpp](../../../SKSE_Source/modules/app/src/LineageUseCases.cpp)
- [LineageRules.h](../../../SKSE_Source/modules/domain_lineage/include/maras/domain/lineage/LineageRules.h) / [LineageRules.cpp](../../../SKSE_Source/modules/domain_lineage/src/LineageRules.cpp)
- [FertilityRules.h](../../../SKSE_Source/modules/domain_lineage/include/maras/domain/lineage/FertilityRules.h) / [FertilityLifecycleRules.h](../../../SKSE_Source/modules/domain_lineage/include/maras/domain/lineage/FertilityLifecycleRules.h)
- [FertilityLifecycleCoordinator.h](../../../SKSE_Source/modules/app/include/maras/app/FertilityLifecycleCoordinator.h) / [ChildSpawnService.h](../../../SKSE_Source/modules/engine/include/maras/engine/ChildSpawnService.h) / [ChildSpawnService.cpp](../../../SKSE_Source/modules/engine/src/ChildSpawnService.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`LineageUseCases` is the Layer-4 orchestrator for pregnancy tracking, off-map and placed child growth (`Unborn` $\rightarrow$ `Newborn` $\rightarrow$ `Infant` $\rightarrow$ `Toddler` $\rightarrow$ `Child` $\rightarrow$ `Training` $\rightarrow$ `Adult`), household adoption, child pets/critters/orders/chores, child-to-adult promotion, death archival (`DeceasedTable`), and epistemic JSON dumps (`DumpDeceasedJson`, `DumpPlayerChildrenJson`). Paired with `FertilityLifecycleCoordinator` and `FertilityLifecycleRules`, it also consolidates third-party fertility mod handoffs (FMR & Beeing Female NG) and multi-fetus delivery batching (`'FRT1'`) natively in C++.

---

## 2. Core Lineage Rules & Slot Semantics

- **Strict Guardian Slot Neutrality & Married Household Unity**: `GuardianSlots` (`slotA`, `slotB`) represent custodial caretakers interchangeably—never infer mother/father roles or genetic lineage from slot order. Biological parents (`bioMother`, `bioFather`) live in separate genetic fields, and `isAdopted` marks explicit adoption events independently of guardian presence or count. Whenever the Player has at least one living married spouse, custodial birth, polygamy divorce, spouse death, and manual UI/Papyrus child guardian edits avoid leaving the Player as a single parent—linking non-adult children to the Player's highest-ranking surviving spouse (and co-housing an unhoused remaining/surviving spouse at `stayAtHome = false` when needed to keep the child housed).
- **Adoption Guard (`IsChildBlockedFromAdoption`)**: Blocks adopting Aventus Aretino (`RefId`/`BaseId`) while Dark Brotherhood quest `DB01` (*Innocence Lost*) remains active and uncompleted.
- **Beastblood Inheritance (`EvaluateChildBeastbloodInheritance`)**: Evaluates deterministically at birth/lineage sync (`hasBeastblood`, `beastbloodEvaluated`): both biological parents werewolves $\rightarrow$ `100%` inherited; one biological parent werewolf $\rightarrow$ deterministic 64-bit SplitMix hash `50%` roll; zero werewolf parents $\rightarrow$ mortal. Persisted via `'LYC1'` inside `'NPCR'`, and masked by [`PerspectiveView`](PerspectiveView.md) whenever revealing `hasBeastblood` would betray an undiscovered paternity secret.
- **Companion Bans & Cooldowns (`LineageRules`)**: Tracks per-child and global (`bGlobalBanPet` / `bGlobalBanCritter`) bans plus retry timestamps before allowing companion pet/critter adoption.

---

## 3. Child Spawning, Follow Targets & `CFSN` Graduation

- **Synthetic Nursery Pool (`0xFF700000..0xFF7FFFFF`)**: Unborn fetuses, newborns, infants, and toddlers exist purely in `LineageTable` / `'NPCR'` until Stage 2 (`Child`) spawns a real `0xFF` placed NPC via `ChildSpawnService` and promotes pending household/custody state onto the real `RefId`.
- **Non-Player Child Follow Resolution (`ChildSpawnService::ResolveNonPlayerChildTarget`)**:
  When a Stage 2 child is not assigned to a housed Player property (so [`PackageReconciler`](PackageReconciler.md) assigns `NonPlayerChildFollow`), target resolution walks a strict living-actor priority chain:
  1. If the Player (`0x14`) is a guardian, prefer the **living NPC co-guardian spouse** first (keeping unhoused Player children with their NPC parent); fall back to Player (`0x14`) if alone.
  2. Guardian slot matching living `bioMother`.
  3. Guardian slot matching living `bioFather`.
  4. Random living guardian if both guardians are alive.
  5. Any single living guardian, followed by living `bioMother` fallback.
- **Faction Preservation (`'CFSN'` `childFactionSnapshots`)**:
  - Before overriding a Stage 2 child's crime/family factions with `TT_MARAS.esp` custody factions, M.A.R.A.S. captures the child's pre-existing `crimeFactionId` and faction ranks into `HouseholdTable::childFactionSnapshots` (persisted in `'CFSN'`).
  - Upon graduation to **Stage 4 (`Adult`)**, the snapshot restores authentic native crime/home-city factions while stripping child-only MARAS custody factions, and clears the snapshot entry cleanly.
  - When a Player-guarded child enters a player home via `OnChildAddedToHousehold` with a resolved house, `LineageUseCases` delegates to `HouseholdUseCases::RelocateFamilyUnit(..., moveParents = true)` so unhoused co-guardian spouses (with `stayAtHome = false` default) and full-custody siblings are co-housed alongside the newly added child and UI details refresh for co-relocated members. If the child is not yet Player-guarded in `LineageTable`, it falls back directly to assigning the child tenant and placing their house marker.
  - Note: Child visual genetics, 22-path training classes, 12 Daedric blessings, and RaceMenu scaling are handled by [`ChildGrowthAndVisuals`](ChildGrowthAndVisuals.md), while profile creation routes through [`SkyrimNetProfileService`](SkyrimNetProfileService.md) when SkyrimNet is available.

---

## 4. Death Archival & Epistemic JSON Dumps (`DumpDeceasedJson`, `DumpPlayerChildrenJson`)

- **Death Snapshot (`RecordActorDeath`)**: Coordinates with [`RelationshipUseCases`](RelationshipUseCases.md) and [`DeceasedQueries`](DeceasedQueries.md) to archive adult and child metadata (including `'DCS1'` secret flags and Stage 4 adult `childInfo`) **before** clearing live household/relationship rows.
- **`DumpDeceasedJson(PerspectiveView)` (Strict CQS)**: Filters every `partnersAtDeath` entry and `childInfo.bioFatherBaseId` through `view`:
  - Unless `view.IsOmniscient()` is true, `p.isSecretAffair` entries substitute `p.publicStatusAtDeath` (or omit the entry entirely if `publicStatusAtDeath` is not romantic/ex).
  - `childInfo.hasUndiscoveredPaternitySecret` substitutes `ci.publicBioFatherBaseId` instead of `ci.bioFatherBaseId`, preventing deceased archives from leaking secret affairs or true paternity when Intrigue Mode is active (`!revealSecretsInUI`).
- **`DumpPlayerChildrenJson()`**: Serializes `childChestIntroduced`, active `ChildHouseholdState` entries, and deterministically sorted `'CFSN'` `childFactionSnapshots`.
