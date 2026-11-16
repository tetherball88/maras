# `ChildGrowthAndVisuals`, `TrainingCatalog` & RaceMenu Genetics — Architecture & Behavioral Guide

**Layers**: Domain Lineage (`SKSE_Source/modules/domain_lineage`) & Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [ChildGrowthRules.h](../../../SKSE_Source/modules/domain_lineage/include/maras/domain/lineage/ChildGrowthRules.h) / [ChildGrowthRules.cpp](../../../SKSE_Source/modules/domain_lineage/src/ChildGrowthRules.cpp)
- [ChildVisualReconciler.h](../../../SKSE_Source/modules/engine/include/maras/engine/ChildVisualReconciler.h) / [ChildVisualReconciler.cpp](../../../SKSE_Source/modules/engine/src/ChildVisualReconciler.cpp)
- [TrainingCatalog.h](../../../SKSE_Source/modules/domain_lineage/include/maras/domain/lineage/TrainingCatalog.h)
- [TrainingCatalogLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/TrainingCatalogLoader.h) / [TrainingCatalogLoader.cpp](../../../SKSE_Source/modules/engine/src/TrainingCatalogLoader.cpp)
- [childTraining.yaml](../../../SKSE/Plugins/MARAS/childTraining.yaml)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

This subsystem turns biological parentage, custodial upbringing, and adolescent training into visible genetics, RPG stats, combat perks, and Daedric blessings as children grow from **Stage 2 (`Child`)** through **Stage 3 (`Training`)** into **Stage 4 (`Adult`)**. Pure skill/blessing math sits in `ChildGrowthRules` + `TrainingCatalog`, YAML/override parsing lives in `TrainingCatalogLoader`, and Skyrim engine / RaceMenu application lives in `ChildVisualReconciler`.

---

## 2. Training Catalog & Daedric Blessings (`TrainingCatalogLoader`)

At `kDataLoaded` and save load / new game, `TrainingCatalogLoader` loads `SKSE/Plugins/MARAS/childTraining.yaml` and alphabetically sorted `.yaml` drop-in overrides from `SKSE/Plugins/MARAS/childTraining/`:

- **22 Training Paths (`trainingLocations:`)**: Each path defines an `id`, display `name`, `location`, dashboard `image`, UI `description`, concise `skyrimnetContext` (used in `ttm_child_sent_to_training` events and Stage 4 adult biography generation), `costMultiplier` (spanning `1.00x` up to `3.25x` across all 22 locations—e.g., Stormcloak `1.00x` through Shadowblade `3.25x`), `skillType`, Health/Magicka/Stamina weights (`hmsWeights`), AI `morality` (`-1` inherits from custodial guardians) / `minConfidence`, `affiliation` rules, `npcClass`, `combatStyle`, `outfit`, weighted `skills` (`1..5`), `spells`, and `perks`.
- **12 Daedric Blessings (`daedricBlessings:`)**: Defines passive champion lineage blessings (`meridia`, `azura`, `boethiah`, `nocturnal`, `molag_bal`, `mehrunes_dagon`, `sanguine`, `hermaeus_mora`, `hircine`, `malacath`, `mephala`, `peryite`) gated by completed Daedric quest keys.
- **Parent Affiliation Tuition Discounts**: Evaluates custodial parents/guardians via `EngineParentAffiliationPort`—matching `memberFactions`/`memberQuests`/`memberWerewolf` grants a `10%` tuition discount, while matching `leaderFactions`/`leaderFactionRank`/`leaderQuests`/`leaderNpcs` grants a `25%` discount.

---

## 3. Pure RPG Skill & Lineage Math (`ChildGrowthRules`)

Two immutable skill bonus snapshots (`'CHD2'` in `'NPCR'`) lock heritage before runtime mutations can alter parents:
- **Biological Parent Snapshot (`EvaluateChildBioLineageAtBirth`)**: Captured once at birth (`bioLineageEvaluated = true`). Each biological parent contributes `round(2 + 6 * clamp((level - 1) / 49, 0, 1))` (`2..8` pts) across their top `4` signature skills (`ResolveSignatureSkillsFromArray` / `GetSignatureSkillsForSkillType`), capped at `32` per skill.
- **Guardian Mentorship Snapshot (`EvaluateChildGuardianLineageAtGraduation`)**: Captured once at graduation (`guardianLineageEvaluated = true`). Each living guardian contributes `round(2 + 5 * clamp((level - 1) / 49, 0, 1))` (`2..7` pts) across their signature skills, capped at `30` per skill.
- **Deterministic Daedric Blessing**: If the Player is a biological parent or custodial guardian and any configured Daedric quests are completed, selects one blessing deterministically using a seed hashed from `(childId, bioMother, bioFather, name)`.

---

## 4. `ChildVisualReconciler` — Visual Morphs, Level Scaling & Lifecycle

- **Visual Genetics (`'CHD1'`) & RaceMenu**: Inherits skin tone, hair color, eye color, morph presets, and height/head/scale interpolations cleanly onto placed Stage 2 and Stage 4 actors whenever 3D loads (`ApplyChildVisualsOn3DLoad`) or post-load reconciliation runs (`ReassertAllChildVisualsOnLoad`).
- **Player-Level Reactive Scaling**: Whenever [`PollingService::CheckPlayerLevelChanged`](PollingService.md) detects a Player level-up, `OnPollingRefreshChildVisuals` recalculates Stage 4 adult children's stats, HMS pools, skill weights (`+5..+25` at Player Lv 10 scaling up to `+16..+80` at Lv 50+), and dynamically unlocks `BGSPerk` ranks (`nextPerk`) whose Creation Kit skill/level conditions are now satisfied.
- **Retired Form Cleanup & Revert**: Tracks retired/replaced temporary actor forms (`FlushRetiredForms`) and resets transient visual reconciliation state in `Revert()` on save load/revert.
