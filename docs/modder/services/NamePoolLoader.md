# `NamePool` & `NamePoolLoader` — Architecture & Behavioral Guide

**Layers**: Domain Lineage (`SKSE_Source/modules/domain_lineage`) & Engine (`SKSE_Source/modules/engine`)  
**Sources**:
- [NamePool.h](../../../SKSE_Source/modules/domain_lineage/include/maras/domain/lineage/NamePool.h) / [NamePool.cpp](../../../SKSE_Source/modules/domain_lineage/src/NamePool.cpp)
- [NamePoolLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/NamePoolLoader.h) / [NamePoolLoader.cpp](../../../SKSE_Source/modules/engine/src/NamePoolLoader.cpp)
- [childrenNames.json](../../../SKSE/Plugins/MARAS/childrenNames.json) & [critterNames.json](../../../SKSE/Plugins/MARAS/critterNames.json)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`NamePool` (pure, engine-free storage and normalization) and `NamePoolLoader` (JSON file ingestion and actor race extraction) supply lore-appropriate, collision-avoidant names for both newly born M.A.R.A.S. children (`childrenNames.json`) and child-adopted critters (`critterNames.json`).

---

## 2. Config Loading & Format (`childrenNames.json`, `critterNames.json`)

Initialized at `kDataLoaded` (`plugin.cpp`):
- Searches `Data/SKSE/Plugins/MARAS/<file>.json` then `SKSE/Plugins/MARAS/<file>.json`.
- Unwraps an optional top-level `"names"` object if present (`UnwrapNamesObject`), parses string arrays per race/gender category with trimming (`JsonOverrideLoader` comment-tolerant JSON parser), and ingests them into `NamePool`.
- Applies optional per-language name overlays via `ApplyLocalizationOverlay` whenever `LocalizationService` loads or reloads an active language pack containing `Data/SKSE/Plugins/MARAS/localization/<lang>/names.json` (`childrenNames` and/or `critterNames`), replacing only the races/species defined in that language's overlay while preserving base English pools for any omitted categories.

---

## 3. Race Normalization & Selection Rules

- **Humanoid / Beast Race Normalization (`NormalizeRaceKey`)**:
  - Strips trailing `"racechild"`, `"racevampire"`, and `"race"` suffixes before matching substring aliases (`nord`, `imperial`, `breton`, `redguard`, `altmer`, `bosmer`, `dunmer`, `orsimer`, `khajiit`, `argonian`).
  - Mirrors `"orsimer"` $\leftrightarrow$ `"orc"` automatically so configs authored under either spelling match cleanly.
- **Critter Race Normalization (`NormalizeCritterRaceKey`)**:
  - Classifies Skyrim critter editor IDs/names into `"arctic_fox"` (checked before `"fox"`), `"fox"`, `"hare"` (or `"rabbit"`), `"mudcrab"` (or `"crab"`), and `"skeever"` (or standalone `"rat"`).
- **Collision Avoidance**: When choosing a child name (`PickChildName` / `PickCritterName` using `MtRngAdapter` backed by `std::mt19937_64`), checks existing names in the active [`WorldStore::Snapshot()`](WorldStoreAndCoSave.md) so siblings or household critters avoid duplicate names whenever alternatives exist in the pool.
