# NameService — Culturally & Species-Appropriate Name Generation

[NameService.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NameService.cpp) provides lore-friendly name pools and random name generation for biological/adopted children and household critters (pets adopted by children).

---

## 1. JSON Name Pool Loading

The service loads two independent JSON dictionaries from `Data/SKSE/Plugins/MARAS/` (either at startup or lazily on first request):

1. **Child Names (`childrenNames.json`)**:
   - Organized by race key, with separate `male` and `female` name arrays per race.
   - Automatically aliases `orc` and `orsimer` so either naming convention in JSON or modded race EditorIDs resolves to the same pool.
2. **Critter Names (`critterNames.json`)**:
   - Organized by species category (`fox`, `arctic_fox`, `hare`, `mudcrab`, `skeever`, plus any custom categories added by modders).

---

## 2. Race Normalization & Modded Race Matching

Because Skyrim and custom race mods use varied EditorIDs and display names (such as `NordRaceChild`, `BretonRaceVampire`, `HighElf`, or lore names like `Saxhleel` and `Yokudan`), the service normalizes raw race strings before querying the name dictionary:

1. **Suffix Stripping**: Strips engine suffixes (`racechild`, `racevampire`, `race`) and trims whitespace.
2. **Lore & Common Substring Matching**: Maps both common and lore race identifiers to the ten canonical Tamrielic races:
   - `argo` / `saxh` $\rightarrow$ `argonian`
   - `khaj` $\rightarrow$ `khajiit`
   - `orsi` / `orc` $\rightarrow$ `orsimer`
   - `altm` / `high` $\rightarrow$ `altmer`
   - `bosm` / `wood` $\rightarrow$ `bosmer`
   - `dunm` / `dark` $\rightarrow$ `dunmer`
   - `redg` / `yoku` $\rightarrow$ `redguard`
   - `bret` $\rightarrow$ `breton`
   - `imper` / `cyrod` $\rightarrow$ `imperial`
   - `nord` / `atmor` $\rightarrow$ `nord`
3. **Wildcard Queries**: Passing an empty race string, `"any"`, `"all"`, or `"none"` aggregates names across all loaded races for the requested gender.

---

## 3. Child Name Generation & Duplicate Prevention

When generating a name for a newborn or adopted child (such as during Fertility Mode Reloaded births or when naming an NPC-couple baby):

- **Direct Actor or Race Inspection**: Can inspect an `RE::Actor*`, `RE::TESNPC*`, or `RE::TESRace*` directly to extract the child's sex and race EditorID/FullName automatically.
- **Uniqueness Filtering**: When unique name selection is enabled (the default), the service queries [NPCRelationshipManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCRelationshipManager.md) for the names of all existing registered children (ignoring placeholder `"Newborn"` entries) and removes any already-used names from the candidate pool before rolling a random name.

---

## 4. Critter Species Detection & Fallback

When a household child brings home a wild critter (`PlayerChildrenService` / `TTM_ChildrenSandbox`), `NameService` picks a fitting pet name:

- **Species Classification**: Normalizes the critter actor's race EditorID or display name into canonical species buckets:
  - Checks `arctic_fox` (`FoxArctic`, `Arctic Fox`) **before** general `fox` so snow foxes receive appropriate names.
  - Maps `hare`, `rabbit`, and `bunny` to `hare`.
  - Maps `mudcrab` and `crab` to `mudcrab`.
  - Maps `skeever`, standalone/CamelCase `rat`, and `mouse` to `skeever`.
- **Universal Critter Fallback**: If a child adopts a modded creature whose race does not match any known category, the service automatically pools all critter names across every category and selects one at random so the pet still receives a name.

