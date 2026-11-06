# AdultChildProgression — Per-Actor Runtime `TESNPC` Cloning & Progression Architecture

## 1. The Problem

When spawning a Stage 4 (`Adult`) child actor (for example, `Kjell`, Nord son of Player `Bjorn` and `Serana`, trained at the **College of Winterhold** as an `Arcane Evoker`, `classIdx = 0`), three bugs occurred simultaneously:

1. **Wrong Race (`DarkElfRace` instead of `NordRace`)**:
   - In [`NPCRelationshipManager::ResolveActorFMRRaceIndex`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L1934-L1948), `Nord` was mapped to index `2` instead of `6`.
   - Fertility Mode Reloaded (`_JSW_BB_Storage.psc` / `_JSW_BB_Utility.psc`) indexes the 10 playable races in strict alphabetical order (`0=Argonian, 1=Breton, 2=Dark Elf, 3=High Elf, 4=Imperial, 5=Khajiit, 6=Nord, 7=Orc, 8=Redguard, 9=Wood Elf`). Passing `savedRaceIndex = 2` caused FMR to return a **Dark Elf** `ActorBase`.

2. **Wrong Class (`Sons of Skyrim Soldiers`) & Wrong Combat Style (`csWEBerserker`) in Console**:
   - For maximum face diversity, [`TTM_ServiceChildrenLogic.SpawnAdultChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc#L1591-L1630) pools all 11 FMR adult face slots (`fmrSlot = Utility.RandomInt(0, 10)`) along with Beeing Female adult bases.
   - Each of FMR's 11 slots is a static `TESNPC` (`NPC_`) record in `Fertility Mode.esm` with hardcoded template fields (`Slot 5 = Stormcloak` with `CNAM = SoldierSonsSkyrimNotGuard` (`"Sons of Skyrim Soldiers"`), `ZNAM = csWEBerserker`, `DOFT = StormcloakOutfit`, `CNTO = Steel Battleaxe`).
   - Although M.A.R.A.S attached `RE::ExtraCombatStyle` to the spawned `Actor`'s `extraList`, **More Informative Console** (`MoreInformativeConsole.dll`) and several engine systems do **not** read `actor->extraList` when inspecting an actor's Class or Combat Style. Instead, they read the actor's base pointer directly from `[actor + 0x40]` (`actor->data.objectReference`, returned by `actor->GetActorBase()`):
     - `Class` is read from `actor->GetActorBase()->npcClass` (`[base + 0x1C0]`).
     - `CombatStyle` is read from `actor->GetActorBase()->combatStyle` (`[base + 0x1D8]`).

3. **Wrong Armor Equipped (`Stormcloak` Armor Overwriting Mage Robes)**:
   - `MARAS.RegisterChildActor` runs on Frame 0 immediately after `PlaceAtMe`, while `actor->Is3DLoaded()` is still `false`.
   - A few frames later, when Skyrim finishes `Character::Load3D`, the engine reads `actor->GetActorBase()->defaultOutfit` (`[base + 0x218]`, which on FMR Slot 5 is `StormcloakOutfit`) and `actor->GetActorBase()->containerObjects` (`Steel Battleaxe`), instantiating and equipping the Stormcloak gear on top of the training outfit.

---

## 2. Considerations & Constraints

To solve this properly, several architectural constraints had to be satisfied simultaneously:

1. **Never Mutate the Shared Static `ActorBase` (`RE::TESNPC`) in `Fertility Mode.esm` or `BeeingFemale.esm`**:
   - FMR and Beeing Female provide a finite set of static `TESNPC` base records per `race + sex`.
   - If two children of the same race and sex (e.g., two Nord sons) happen to roll the same FMR/BF `ActorBase` and are sent to different training paths (e.g., Son #1 to **College of Winterhold** and Son #2 to **Jorrvaskr**), mutating `origNpc->npcClass`, `origNpc->combatStyle`, `origNpc->defaultOutfit`, `origNpc->playerSkills`, or `origNpc->perks` on the shared `TESNPC` for Son #2 would immediately overwrite Son #1.
   - Furthermore, calling engine setters on `origNpc` sets `ChangeFlags` (`kClass`, `kDefaultOutfit`, `kNPCSkills`, `kAIData`) in `.ess` savegames under `origNpc->formID`, permanently polluting the shared base record across saves.

2. **Never Create Hundreds of Static ESP `ActorBase` Permutations**:
   - Creating `10 races × 2 sexes × 22 training classes = 440+` static `NPC_` records in `TT_MARAS.esp` (plus custom YAML training classes) is unmaintainable, bloats the plugin, and loses the facegen diversity provided by pooling FMR and Beeing Female adult bases.

3. **Strict Config Validation (No Guessing by Name)**:
   - Training definitions in [`childTraining.yaml`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/MARAS/childTraining.yaml) (and any addon `childTraining/*.yaml` files) must explicitly define their critical fields (`id`, `name`, `location`, `npcClass`, `combatStyle`, `outfit`).
   - If a training config entry is missing critical fields or references an invalid form, the loader must log an `ERROR` and reject it rather than guessing fallback IDs by name.

---

## 3. Decisions & Solution Architecture

### 3.1 Per-Actor Isolated Runtime `TESNPC` Clone (`EnsurePerActorRuntimeBase`)
Instead of mutating the shared static `TESNPC*` (`origNpc`) or creating static ESP records, [`AdultChildProgression::EnsurePerActorRuntimeBase`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AdultChildProgression.cpp#L1424-L1651) gives each spawned Stage 4 adult `Actor` (`0xFF......`) its own heap-allocated runtime `RE::TESNPC*` (`cloneNpc`):

1. **Preserving the Original Base**:
   - The original static `RE::TESNPC*` from `Fertility Mode.esm` / `BeeingFemale.esm` is stored untouched in `originalAdultBases_[childID]`.
2. **Heap-Cloning `RE::TESNPC`**:
   - Allocates `cloneNpc = RE::calloc<RE::TESNPC>(1)` and copies `origNpc` via `std::memcpy(cloneNpc, origNpc, sizeof(RE::TESNPC))` so the clone retains the exact vtable, `formID`, `race`, `headParts`, `faceNPC`, `faceData`, `tintLayers`, and `voiceType` required for 3D facegen rendering.
   - Deep-clones `cloneNpc->perks` and `cloneNpc->headRelatedData` into separate heap buffers so per-child perks and hair colors never touch `origNpc`.
3. **Stripping Template Bleed on the Clone**:
   - Sets `cloneNpc->containerObjects = nullptr`, `cloneNpc->numContainerObjects = 0`, `cloneNpc->actorEffects = nullptr`, and `cloneNpc->sleepOutfit = nullptr`, and clears `kAutoCalcStats`.
   - This prevents FMR template weapons (`CNTO`, e.g. `Steel Battleaxe`) and template spells (`SPLO`) from ever being added by the engine when 3D loads.
4. **Assigning Training Class, Combat Style & Default Outfit on the Clone**:
   - `cloneNpc->npcClass = GetTrainingClass(trainIdx)`
   - `cloneNpc->combatStyle = GetCombatStyle(trainIdx)`
   - `cloneNpc->defaultOutfit = GetClassOutfit(trainIdx)`
5. **Populating Skills, HMS & AI Data Directly on the Clone**:
   - Computes the 3-pillar skills (race + bio parents + guardians + training class + Daedric blessing), Health/Magicka/Stamina (`playerSkills`), and Confidence/Morality/Aggression/Assistance (`aiData`) and writes them directly into `cloneNpc->playerSkills` and `cloneNpc->aiData` without calling `AddChange`.
6. **Repointing the Actor Instance**:
   - Sets `actor->data.objectReference = cloneNpc`.
   - Every subsequent call to `actor->GetActorBase()` — by Skyrim's `Character::Load3D`, AI combat evaluation, `ApplyPerksFromBase`, or **More Informative Console** — now reads `cloneNpc`.

### 3.2 Runtime `RE::TESClass` Per Training Path (`GetTrainingClass`)
- Each training entry in [`childTraining.yaml`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/MARAS/childTraining.yaml) specifies its `npcClass` (`__formData|Skyrim.esm|0x......`) and `skills:` weights (`1..5`: `5 = Primary`, `4 = Secondary`, `3 = Tertiary`, `2 = Minor`, `1 = Utility/Flavor`).
- [`AdultChildProgression::GetTrainingClass`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AdultChildProgression.cpp) resolves that `RE::TESClass`, allocates a cached runtime clone (`spec.cachedRuntimeClass`), sets `runtimeClass->fullName` to the training path's `className` (e.g. `"Arcane Evoker"`, `"Shield-Brother Vanguard"`), and populates `attributeWeights` and `skillWeights` (`1..5`) from the YAML config.

### 3.3 Three-Stage Progression Lifecycle & Dynamic Follower Scaling (`'CHD2'` Co-Save Extension)
To ensure adult children feel like capable, cohesive followers without spawning as overpowered Level 50+ demi-gods when the Player is early- or mid-level, progression is split across three distinct lifecycle stages:

1. **Stage 1 — Birth Snapshot (`EvaluateChildBioLineageAtBirth`)**:
   - Evaluated once when a child is born (`Stage 1: Infant`) or first registered, and persisted in `entry.bioSkillBonuses[18]` (`entry.bioLineageEvaluated = true`).
   - Each biological parent (`bioMotherID`, `bioFatherID`) grants **`+2` (at Parent Level 1) $\rightarrow$ `+8` (at Parent Level 50+)** (`round(2 + 6 * clamp((parentLevel - 1) / 49, 0, 1))`) to their 4 signature skills (or the **Player's actual top 4 highest base skills** when the parent is the Player).
   - Also evaluates hereditary Lycanthropy (`EvaluateChildBeastbloodInheritance`) and hair color inheritance at birth.

2. **Stage 2 — Adult Graduation Snapshot (`EvaluateChildGuardianLineageAtGraduation`)**:
   - Evaluated once when the child graduates from training into `Stage 4: Adult`, and persisted in `entry.guardianSkillBonuses[18]` (`entry.guardianLineageEvaluated = true`) and `entry.daedricBlessingId`.
   - Each custodial guardian (`entry.GetGuardians()`, strictly slot-neutral) grants **`+2` (at Guardian Level 1) $\rightarrow$ `+7` (at Guardian Level 50+)** (`round(2 + 5 * clamp((guardianLevel - 1) / 49, 0, 1))`) to their 4 signature skills (or the **Player's actual top 4 highest base skills** when the guardian is the Player).
   - Combined family lineage bonus is naturally capped at **`+15` max** for a single Level 50+ parent acting as both Bio-Parent and Guardian (`+8` Bio + `+7` Guardian) and **`+30` max** if two Level 50+ parents of the same archetype are both Bio-Parents and Guardians (`+16` Bio + `+14` Guardian).
   - Snapshots the child's Daedric Champion patron (`entry.daedricBlessingId`) once at graduation based on quests completed at that moment.

3. **Stage 3 — Dynamic Player-Level Scaling (`EffectiveLevel = clamp(PlayerLevel, 10, 50)`)**:
   - Evaluated at graduation, on save/3D load, and automatically whenever the Player levels up (`PollingService::CheckPlayerLevelChanged` $\rightarrow$ `ReassertAllChildVisualsOnLoad`).
   - Sets `cloneNpc->actorData.level = EffectiveLevel` (with `kPCLevelMult` cleared).
   - **Level Factor**: `levelFactor = clamp((EffectiveLevel - 10) / 40, 0.0, 1.0)` (`0.0` at Lv 10 $\rightarrow$ `1.0` at Lv 50).
   - **Class Skill Weights (`1..5`)**: Each weight unit grants `5.0 + 11.0 * levelFactor` skill points (`+5/weight` at Lv 10 $\rightarrow$ `+16/weight` at Lv 50):
     - Weight `5` (Primary): **`+25` at Lv 10 $\rightarrow$ `+80` at Lv 50**
     - Weight `4` (Secondary): **`+20` at Lv 10 $\rightarrow$ `+64` at Lv 50**
     - Weight `3` (Tertiary): **`+15` at Lv 10 $\rightarrow$ `+48` at Lv 50**
     - Weight `2` (Minor): **`+10` at Lv 10 $\rightarrow$ `+32` at Lv 50**
     - Weight `1` (Utility): **`+5` at Lv 10 $\rightarrow$ `+16` at Lv 50**
   - **Final Skill Formula**:
     $$\text{Skill} = \text{clamp}\bigl(15 + \text{RaceBoost} + \text{BioSnapshot} + \text{GuardianSnapshot} + \text{DaedricBonus} + \text{ClassWeight} \times (5 + 11 \times \text{levelFactor}),\; 15,\; 100\bigr)$$
   - **HMS Pool Scaling**: Base `100 / 100 / 100` + `(80 + 320 * levelFactor)` (`+80` pool at Lv 10 $\rightarrow$ `+400` pool at Lv 50) distributed by `85% Class HMS Weights + 15% Bio Parent HMS Weights`, plus Daedric and Beastblood (`+25 HP / +50 Stamina`) bonuses. Updates both `cloneNpc->playerSkills` and `actor->AsActorValueOwner()->SetBaseActorValue(...)` whenever `entry.lastAppliedLevel != EffectiveLevel`.

### 3.4 Skill-Gated Class Perks & Multi-Rank Upgrades (`childTraining.yaml` $\leftrightarrow$ `ApplyAdultChildPerks`)
- All `spells:` and `perks:` granted to an adult child come strictly from the training path's `spells:` and `perks:` lists and the snapshotted `daedricBlessings:` entry in [`childTraining.yaml`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/MARAS/childTraining.yaml) (and any addon `childTraining/*.yaml` files).
- For training class perks (`trainingLocations[i].perks`), [`AdultChildProgression::ApplyAdultChildPerks`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AdultChildProgression.cpp) reads each `RE::BGSPerk`'s character-level (`perk->data.level`) and skill-level requirements (`perk->perkConditions` `GetBaseActorValue` / `GetActorValue` / `GetPermanentActorValue` / `GetLevel` conditions) directly from the loaded perk form:
  - Only perks whose skill/level requirement the child **currently meets** are unlocked.
  - For multi-rank perk chains linked via `BGSPerk::nextPerk` (e.g., `Armsman00` $\rightarrow$ `Armsman20` $\rightarrow$ `Armsman40` $\rightarrow$ `Armsman60` $\rightarrow$ `Armsman80`, `Juggernaut00`..`80`, `Overdraw00`..`80`, `AgileDefender00`..`80`, `ShieldWall00`..`60`, `Recovery30` $\rightarrow$ `Recovery50`, `Augmented*` $\rightarrow$ `Augmented*60`, `MageArmor30`..`70`, `MagicResistance30`..`70`), only the **highest qualified rank** is kept active so multiple ranks never stack on top of each other.
  - Whenever the Player levels up (`PollingService::CheckPlayerLevelChanged`) and the child's scaled skills cross a higher perk threshold, newly qualified perks are added and lower ranks in a `nextPerk` chain are upgraded in-place (cleanly removing the previous rank's `BGSPerkEntry` effects before applying `Actor::ApplyPerksFromBase()`).

### 3.5 Dual-Phase Outfit & Equipment Enforcement (Spawn + `kLoad3D`)
- Because `PlaceAtMe` initializes the actor before `MARAS.RegisterChildActor` is called from Papyrus, the actor's initial inventory may already contain items from `origNpc->defaultOutfit` or `origNpc->containerObjects`.
- Both [`InitializeAndApplyChildVisualsAndRPG`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AdultChildProgression.cpp#L1942-L1980) (on initial registration) and [`ApplyChildVisualsToActor`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AdultChildProgression.cpp#L2069-L2126) (on `TESObjectLoadedEvent` / `kPostLoadGame`) strip any `origNpc->defaultOutfit` items, foreign `ExtraOutfitItem` gear, and `origNpc->containerObjects` weapons, and ensure `classOutfit` and `GetClassEquipment(trainIdx)` are worn.

### 3.4 Savegame & Revert Safety (`RevertRuntimeBases`)
- Because `cloneNpc->formID` equals `origNpc->formID`, `.ess` savegames serialize the valid FMR/BF `FormID` for the placed actor reference without needing custom ESP records.
- Because `AddChange` is never called on `cloneNpc` or `origNpc`, `origNpc` is never dirtied in the `.ess` savegame.
- On save revert/reload, [`NPCRelationshipManager::Clear`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Serialization.cpp#L15-L29) calls [`AdultChildProgression::RevertRuntimeBases`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AdultChildProgression.cpp#L1399-L1422) to restore `actor->data.objectReference = origBase` and free all heap-allocated `clonedAdultBases_`, and `ReassertAllChildVisualsOnLoad` re-creates each child's runtime clone from the `'MARS'` SKSE co-save.

---

## 4. Summary of Modified Files

| File | Changes |
| :--- | :--- |
| [`SKSE/Plugins/MARAS/childTraining.yaml`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE/Plugins/MARAS/childTraining.yaml) | Added explicit `npcClass: __formData\|Skyrim.esm\|0x......` to all 22 training locations. |
| [`SKSE_Source/include/core/AdultChildProgression.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/AdultChildProgression.h) | Added `classKeys` and `cachedRuntimeClass` to `TrainingClassRuntimeSpec`; declared `GetTrainingClass`, `EnsurePerActorRuntimeBase`, `GetOriginalAdultBase`, `RevertRuntimeBases`, `originalAdultBases_`, and `clonedAdultBases_`. |
| [`SKSE_Source/src/core/AdultChildProgression.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/AdultChildProgression.cpp) | Enforced strict config validation in `ParseTrainingLocationObject`; implemented `GetTrainingClass`, `EnsurePerActorRuntimeBase`, `GetOriginalAdultBase`, and `RevertRuntimeBases`; updated `InitializeAndApplyChildVisualsAndRPG`, `ApplyChildVisualsToActor`, and `ApplyAdultChildPerks` to operate on `cloneNpc`. |
| [`SKSE_Source/src/core/NPCRelationshipManager_Serialization.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Serialization.cpp) | Called `AdultChildProgression::GetSingleton().RevertRuntimeBases()` in `NPCRelationshipManager::Clear()`. |
| [`SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp) | Fixed `ResolveActorFMRRaceIndex` to match FMR's `0..9` alphabetical race order (`6 = Nord`, `2 = Dark Elf`). |
| [`Scripts/Source/TTM_ServiceChildrenLogic.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc) | Updated `SpawnSyntheticChild` and `SpawnAdultChild` to query `fmrUtility.GetRaceIndex(childRace)` first and fall back to `MARAS.GetChildSavedRaceIndex(syntheticID)`; compiled to `Scripts/TTM_ServiceChildrenLogic.pex`. |

---

## 5. Final Result

1. **Exact Lineage Race**: A child of Nord parents (`NordRace` / `NordRaceVampire`) resolves `raceIdx = 6` and spawns as a **Nord**.
2. **Exact Per-Actor Class, Combat Style & Outfit**: Regardless of which of the 11 FMR or Beeing Female face bases is randomly chosen for visual variety, the spawned adult child's `cloneNpc` has the exact `Class` (e.g. `Arcane Evoker`), `Combat Style` (e.g. `csWinterholdCollege`), `Outfit` (e.g. `CollegeRobes`), weapons, spells, skills, and perks from its training path.
3. **Zero Shared `ActorBase` Mutation**: Multiple adult children spawned from the exact same FMR/BF `ActorBase` and sent to different training paths each have their own independent `cloneNpc` in memory and never overwrite one another.
