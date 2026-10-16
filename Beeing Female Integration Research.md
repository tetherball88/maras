# Beeing Female NG ↔ M.A.R.A.S Integration & Save Migration Investigation

> [!WARNING]
> **Correction:** Parts 1–4 below were written against an **outdated copy** of Beeing Female (`E:\Skyrim\development\mods\Beeing-Female-NG`). The real source is `D:\Modlists\SEF\mods\Beeing Female NG\Source\Scripts`, which already emits per-child `BeeingFemaleBirth(mother, father, baby, name, sex)`, `BeeingFemaleChildSpawned(mother, father, child, name)`, `BeeingFemaleAdultChildSpawned(mother, father, adult, child, name)`, `BeeingFemaleAbort(mother, father, abortusState, reason)` and `BeeingFemaleStillbirth(mother, father)`. Beeing Female-side findings in Part 2 and the proposals in Part 3 are therefore **superseded**; the implemented integration follows the real contract (see `docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md`, section 7). Part 5 (MARAS owns child behavior) is unaffected.

## Executive Summary

An audit of M.A.R.A.S's current Beeing Female NG integration ([`TTM_ServiceFertilityBF.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc), [`TTM_MainController.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_MainController.psc), [`TTM_ServiceChildrenLogic.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc), and C++ child registry methods) alongside the **Beeing Female NG** source code (`E:\Skyrim\development\mods\Beeing-Female-NG`) revealed:

1. **Multiple critical bugs in M.A.R.A.S's current BF integration** — including a Papyrus `ModEvent` signature mismatch that causes `OnBeeingFemaleBirth` to **never fire**, a `StorageUtil.SetIntValue` typo in `Sync()` that **halts child growth in Beeing Female NG on every game load**, broken multi-child birth promotion/deduplication, and the complete absence of a one-time save migration (`Migrate()`).
2. **Architectural differences between FMR and Beeing Female NG** — FMR treats born children as inventory items that transition into *ephemeral/synthetic* Stage 2 children until explicitly spawned, whereas Beeing Female NG's default mode (`BabySpawn = 1`, Actor mode) immediately spawns a physical, scale-growing `FWChildActor` at birth and chains `SendBirthEvent` directly into `SendChildSpawnedEvent`.
3. **Gaps in Beeing Female NG's event & persistence layer** — missing abort/stillbirth/unimpregnate events, `akFather = none` on abortions/miscarriages, silent father replacement during Baby NTR, lack of name/sex/ref persistence on `FW.BornChild*`, and no events on child rename, maturity (when adult graduation is disabled), or child dismissal/death.

---

## Part 1: What Is Broken in M.A.R.A.S's Current BF Integration

### 1.1 `BeeingFemaleBirth` Signature Mismatch (Complete Silent Failure)
- **In Beeing Female NG** ([`FWUtility.psc:933-940`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWUtility.psc#L933-L940)):
  `SendBirthEvent` pushes **5** arguments:
  `(Form akMother, Form akFather, Form akBaby, string bName, int bSex)`
- **In M.A.R.A.S** ([`TTM_MainController.psc:324`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_MainController.psc#L324), [`TTM_ServiceFertility.psc:134`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertility.psc#L134), [`TTM_ServiceFertilityBF.psc:132`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L132)):
  `OnBeeingFemaleBirth` declares **7** arguments:
  `(Form akMother, Form akFather, Form akBaby, string asBabyName, int aiBabySex, int aiChildIndex, int aiTotalChildren)`
- **Impact**: Papyrus `ModEvent` dispatch requires an exact parameter count and type match. Because 5 arguments are pushed and 7 are expected, **Papyrus silently drops every `BeeingFemaleBirth` event**. `OnBirth` in `TTM_ServiceFertilityBF` has never executed.

### 1.2 `Sync()` Corrupts Beeing Female NG's Child Growth State on Every Load
- **In M.A.R.A.S** ([`TTM_ServiceFertilityBF.psc:782`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L782)):
  ```papyrus
  bool finishedGrowing = StorageUtil.SetIntValue(childActor, "FW.AddOn.StartGrowing", 0) == 0
  ```
- **Impact**: It calls `StorageUtil.SetIntValue(..., 0)` instead of `StorageUtil.GetIntValue(..., 0)`! On every game load (`Maintenance()` -> `Sync()`), M.A.R.A.S overwrites `FW.AddOn.StartGrowing` to `0` on every child actor in `FW.Babys`.
  - In Beeing Female NG ([`FWChildActor.psc:859`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWChildActor.psc#L859)), `UpdateSize()` only scales/grows the child while `StorageUtil.GetIntValue(self, "FW.AddOn.StartGrowing", 0) == 1`. Setting it to `0` permanently freezes the baby at `0.3` scale and prevents it from ever growing into a child or adult!

### 1.3 Double-Execution on Direct Actor Births (`SendBirthEvent` + `SendChildSpawnedEvent`)
- In Beeing Female NG, `BabySpawn = 1` (**"Actor"** mode) is the **default** for both Player and NPCs ([`FWChildSettings.psc:54-55`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWChildSettings.psc#L54-L55)), and even in `BabySpawn = 2` ("Item/Actor" mode), creature babies spawn directly as Actors at birth ([`FWSystem.psc:2885-2887`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L2885-L2887)).
- Inside [`FWUtility.SendBirthEvent`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWUtility.psc#L943-L945):
  ```papyrus
  If (akBaby as Actor)
      SendChildSpawnedEvent(akBaby as Actor, akMother, akFather, bName)
  EndIf
  ```
- In [`TTM_ServiceFertilityBF.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L149-L204), `OnBirth` promotes the unborn child to Stage 1 (`Infant`) and then, if `akBaby as Actor`, immediately calls `AdoptChild` / `RegisterChildActor` at Stage 2 (`Child`). Moments later, `OnChildSpawned` ([`TTM_ServiceFertilityBF.psc:551`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L551)) fires for the exact same actor and runs the registration/adoption logic a second time.

### 1.4 Multi-Child & Multi-Father (Superfecundation) Failures Across Papyrus & C++
Unlike FMR (where a woman only ever carries **one** child from **one** father per pregnancy), Beeing Female NG allows a single woman to conceive and give birth to **up to 12 children from multiple different biological fathers in a single pregnancy** (via multi-donor sperm pools in `ActiveSpermImpregnation*`, `ImpregnateA`, or mid-pregnancy `AllowNTRbaby`). Currently, this breaks at **five separate points** in M.A.R.A.S:

1. **`OnConception` Only Reads 3 Fathers & Repeats `akFather0` for Baby 4+**:
   - In [`TTM_ServiceFertilityBF.OnConception`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L47-L68), the `BeeingFemaleConception` event only passes `akFather0`, `akFather1`, `akFather2`. For any 4th+ baby (`idx >= 3`), M.A.R.A.S falls back to `f = akFather0` instead of reading `StorageUtil.FormListGet(akMother, "FW.ChildFather", idx) as Actor` directly.
2. **Premature `EvaluateBioFatherStepIn` Inside `RegisterUnbornChild` (`NPCRelationshipManager_Fertility.cpp:526-535`)**:
   - When an unmarried mother conceives twins from two different unmarried fathers (`FatherA` and `FatherB`), [`NPCRelationshipManager::RegisterUnbornChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L526-L535) triggers `EvaluateBioFatherStepIn(motherID, 0)` immediately inside the first `RegisterUnbornChild` call (`idx = 0`), *before* `idx = 1` (`FatherB`) has even been registered in `childrenByID`. Thus `FatherB` is never included in the step-in candidate pool.
3. **Birth Loop Runs in Reverse Order (`NumChilds - 1` down to `0`) & Matches Wrong Father's Unborn Record**:
   - At conception, `OnConception` registers `unbornIDs[0 .. N-1]` in forward order (`0 .. N-1`).
   - At birth, both [`FWController.GiveBirth`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L1124-L1178) and [`FWSystem.InstantBornChilds`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L2358-L2370) loop **backwards** (`while NumChilds > 0` -> `NumChilds -= 1` -> `SpawnChild(Mother, ChildFather[NumChilds], ...)`).
   - If Baby #0 was fathered by the Husband (`isSecret = false`) and Baby #1 was fathered by a Paramour (`isSecret = true`), BF NG spawns the **Paramour's baby first** and the **Husband's baby second**! Indexing `unbornIDs[aiChildIndex]` without matching `bioFather` swaps their records and paternity secrets!
4. **`MARAS.PromoteUnbornToInfant` and `MARAS.RegisterFertilityChild` Are Hardcoded for Single-Child FMR Births**:
   - When Baby #0 is born, [`NPCRelationshipManager::PromoteUnbornToInfant`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L540-L589) promotes **all** `unbornIDs` of `mother` to `ChildStage::Infant` (`1`) at once, naming them `"<FirstBabyName> #1"`, `"<FirstBabyName> #2"`.
   - Five seconds later in `GiveBirth`, when the next baby (from Father #2) is born, `MARAS.GetUnbornChildren(mother)` returns **`[]` (empty)** because all fetuses are already at Stage 1 (`Infant`).
   - `OnBirth` then falls into the `else` branch and calls `MARAS.RegisterFertilityChild(0, mother, partner, akFather, ...)`.
   - Inside [`NPCRelationshipManager::RegisterFertilityChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L1194-L1206), C++ has a single-infant deduplication guard:
     ```cpp
     // Deduplication check: if mother already has an active Infant (stage 1), return existing infant
     ```
     It finds Baby #0 (fathered by Father #1), logs *"Mother already has an active infant. Skipping duplicate birth registration"*, and **returns Baby #0's ID**! Baby #1 (fathered by Father #2) then overwrites Baby #0's name and sex, while the actual second infant record is left orphaned with the wrong name, wrong sex, or wrong father!
5. **`BuildChildBirthJson` Only Receives the Last Baby Instead of the Full Multi-Father Delivery Batch**:
   - Even though C++ [`BuildChildBirthInternal`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L1052-L1129) has a dedicated `Case B: Multiple Biological Fathers (Superfecundation)` narrative generator, [`TTM_ServiceFertilityBF.OnBirth`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L207-L211) only passes `bornIDs = [childID]` (a 1-element array containing only the last baby) to `MARAS.BuildChildBirthJson(akMother, bornIDs)`. Because `bornIDs` only has 1 child, `Case B (Superfecundation)` is **never** reached for BF births!

### 1.5 True One-Time Save Migration (`Migrate()`) Is Missing
- Compare [`TTM_ServiceFertilityFMR.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityFMR.psc#L269-L511) with [`TTM_ServiceFertilityBF.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L728-L814):
  - FMR has a dedicated one-time `Migrate()` (guarded by `IsMigrated()` / `MarkMigrated()`) that scans all tracked mothers and reconstructs active pregnancies (Stage 0), carried baby items (Stage 1), and past born children (Stage 2), plus a lightweight `Sync()` on every load.
  - `TTM_ServiceFertilityBF` has **no `Migrate()` at all**! Its `Sync()` only:
    - Checks mothers in `FW.SavedNPCs` who have `FW.BabyItemArmor` and updates the name if `MARAS.FindSyntheticChild(mother, "")` *already* exists (it never registers missing carried baby items!).
    - Loops `FW.Babys` for `Actor` references, but only registers/promotes them if `finishedGrowing` is true (which was broken by the `SetIntValue` bug) and never migrates active pregnancies (`FW.CurrentState` 4..7 in `FW.SavedNPCs`) or historical births (`FW.BornChildFather` / `FW.BornChildTime`)!

### 1.6 Broken `BeeingFemale` State Change Listener & Race Conditions
- [`TTM_MainController.psc:90-91`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_MainController.psc#L90-L91) registers for `"BeeingFemaleStateChange"` (an event Beeing Female NG never sends) and `"BeeingFemale"`.
- Beeing Female NG sends `"BeeingFemale"` in two different ways:
  1. From `FWSystem`: `SendModEvent("BeeingFemale", EventName, base.ActorRef.GetFormID())` where `EventName` is `"stateChanged"`, `"Update"`, `"Belly"`, `"CheckAbortus"`, etc., and `numArg` is the **FormID** (`sender` is the `FWSystem` quest, NOT the Actor!).
  2. From `FWController.ChangeState`: `FemaleActor.SendModEvent("BeeingFemale", "ChangeState", newState)` where `sender` is `FemaleActor` and `numArg` is `newState`.
- [`TTM_ServiceFertilityBF.OnStateChange`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L261-L299) only runs when `strArg == "Update"` or `"CheckAbortus"`, completely ignoring `"stateChanged"` and `"ChangeState"`. Worse, because `"Update"` fires during impregnation right as `OnConception` is running, `OnStateChange` can race with `OnConception`.

### 1.7 Missing `SpawnSyntheticChild` ActorBase Resolution for Beeing Female
- In [`TTM_ServiceChildrenLogic.psc:1303`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc#L1303):
  ```papyrus
  ; 2. TODO try t oresolve with Being Female fertility mod
  ```
- If a player or system attempts to spawn a synthetic child in M.A.R.A.S when Beeing Female NG is used without FMR, `SpawnSyntheticChild` fails with `"Failed to resolve childBase"`.

### 1.8 Household / AI Package Conflict with `FWChildActor` & Adult Promotion (`Stage 4`) Bug
1. **`FWChildActor` vs. MARAS Household Adoption**:
   - In Beeing Female NG, `FWChildActor` is a scaled-down (`0.3` -> `1.0`) adult-race actor driven by [`FWChildActor.psc`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWChildActor.psc) with its own AI packages controlled by the `JumpingBonus` ActorValue (`0` = Follow Mother, `1` = Follow Father, `2` = Wait, `3` = Go Inside, `4` = Play), its own skill training menu, and its own 50-day growth loop.
   - When `TTM_ServiceFertilityBF` calls `childrenLogicQst.AdoptChild(akChild, ...)`, [`PlayerChildrenService::OnChildAddedToHousehold`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/PlayerChildrenService.cpp#L141-L207) calls `ClearChildExternalFactions(childID)` (stripping BF factions) and overrides its AI package with MARAS's Hearthfire child packages!
2. **Stage 4 (`Adult`) Promotion Bug in C++ `RegisterChildActor`**:
   - When a child grows into an adult (`OnAdultSpawned` in BF or `OnAdultChildSpawned` in FMR), `MARAS.RegisterChildActor(akAdultActor, ..., existingID, 4)` is called.
   - In [`NPCRelationshipManager::RegisterChildActor`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Children.cpp#L290-L355):
     - It erases `synthFormID` (`existingID`) from `childrenByID` **without** calling `PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(synthFormID)`, leaving the deleted `FWChildActor` as a ghost entry in `PlayerChildrenService` and `PlayerHouseService`!
     - It then calls `SyncChildHouseholdState(akAdultActor, ...)` unconditionally — even when `stage == ChildStage::Adult` (`4`)! That calls `OnChildAddedToHousehold(akAdultActor, houseID)`, putting the **grown Adult NPC** into `PlayerChildFaction`, `BYOHRelationshipAdoptionChildOwnedFaction`, and assigning **child sandbox/bed AI packages** to an adult!

---

## Part 2: What Is Missing or Problematic in Beeing Female NG

Below is everything found in `E:\Skyrim\development\mods\Beeing-Female-NG` that hinders accurate lifecycle tracking and save migration in M.A.R.A.S:

### 2.1 Incomplete Abort / Miscarriage / Stillbirth / Unimpregnate Event Coverage
1. **Stillbirths during birth emit NO event**:
   - In [`FWController.GiveBirth`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L1181) and [`FWSystem.InstantBornChilds`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L2371), if `UnbornHealth <= HealthRnd`, the baby dies during birth (`"Your baby died during birth"`), `NumChilds` is cleared, and no `SendBirthEvent` or `SendAbortEvent` is fired. M.A.R.A.S's `Unborn` child record remains stuck in Stage 0 forever!
2. **`Unimpregnate` / `UnimpregnateState` / Console `bf:unimpregnate` / `FWSaveLoad.Delete` emit NO event**:
   - Calling [`FWController.UnimpregnateState`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L819-L850) or resetting an NPC via [`FWSaveLoad.Delete`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSaveLoad.psc#L221) clears pregnancy storage keys without emitting `BeeingFemaleAbort`.
3. **`SendAbortEvent` always receives `akFather = none`**:
   - In all 5 places where `FWUtility.SendAbortEvent` is called ([`FWController.psc:1680, 1732, 1791, 1844`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L1680) and [`FWAbilityBeeingFemale.psc:1326`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWAbilityBeeingFemale.psc#L1326)), `ClearChildFathers(Mother)` is called *before* `SendAbortEvent(Mother, none, reason)`, so `akFather` is always `none`.

### 2.2 `SendBirthEvent` Conflates Actor Birth with Item Hatching & Lacks Multi-Child Context
1. **Auto-chaining `SendChildSpawnedEvent` inside `SendBirthEvent`**:
   - In [`FWUtility.SendBirthEvent`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWUtility.psc#L943-L945), if `akBaby as Actor`, it immediately calls `SendChildSpawnedEvent`. That conflates "birth in Actor mode (`BabySpawn == 1`)" with "a carried baby item hatched into a child actor (`ProcessBabyItemTransitionToChild` in `BabySpawn == 2`)".
2. **Missing `childIndex` and `totalChildren` in `SendBirthEvent`**:
   - In [`FWController.GiveBirth`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L1168-L1196), babies are born in a `while currentChild < NumChilds` loop with `Utility.Wait(SecondsBetweenBabySpawn)` (5 seconds) between each baby. Without `childIndex` and `totalChildren` in the `BeeingFemaleBirth` event, M.A.R.A.S cannot know when a multi-child birth batch is complete (to emit a single consolidated `ttm_child_birth` SkyrimNet narrator event for twins/triplets rather than spamming one per twin) or which unborn slot (`0..totalChildren-1`) corresponds to which newborn.
3. **Missing Name & Sex when `BabySpawn == 0` (None) or `3` (BabyGem)**:
   - In [`FWSystem.SpawnChild`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L2880-L2983), when `spawnSetting == 0` or `3`, `Baby` is `None`. `SendBirthEvent` defaults to `bName = "Newborn"` and `bSex = 0`, discarding the `isFemale` roll that `SpawnChild` already computed!

### 2.3 Baby NTR (`AllowNTRbaby`) Replaces Father Silently & Desyncs Parallel Lists
- In [`FWAbilityBeeingFemale.psc:2867`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWAbilityBeeingFemale.psc#L2867) (`PregnancySecond_State`) and [`2950`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWAbilityBeeingFemale.psc#L2950) (`PregnancyThird_State`), when `cfg.AllowNTRbaby` triggers:
  ```papyrus
  StorageUtil.FormListSet(ActorRef, "FW.ChildFather", num_babies, a)
  ```
  - **Bug in BF NG**: It updates `FW.ChildFather`, but does **not** update the parallel lists `FW.ChildFatherStr` (father name) or `FW.ChildFatherRace` (father race)!
  - **Missing Event**: It emits no ModEvent (`BeeingFemaleFatherChanged` or similar), so M.A.R.A.S never learns that the unborn child's biological father changed mid-pregnancy.

### 2.4 NPC-NPC Baby Items Never Hatch in `BabySpawn == 2` (Item/Actor Mode)
- In [`FWSystem.OnGameLoad`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L768-L777):
  ```papyrus
  ; Drop only player-fathered NPC baby items whose mother lost track;
  ; NPC mothers' baby items never hatch, so dropping their entries here is by design.
  ```
- And [`FWAbilityBeeingFemale.ProcessBabyItemTransitionToChild`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWAbilityBeeingFemale.psc#L1063-L1086) only runs on loaded actors with the active magic effect, and only checks `PlayerRef` or `StorageUtil.GetFormValue(Mother, "FW.BabyItemFather") == PlayerRef` during `OnGameLoad`.
- Consequently, if the player uses `BabySpawn == 2` ("Item/Actor"), NPC-NPC couples who give birth receive a `BabyItemArmor` that is **purged from `FW.Babys` on the next game load** and never transitions into a child!

### 2.5 Missing Metadata in `FW.BornChild*` History Lists for Save Migration
- In [`FWSystem.SpawnChild`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L2977-L2978):
  ```papyrus
  StorageUtil.FormListAdd(Mother, "FW.BornChildFather", Father, true)
  StorageUtil.FloatListAdd(Mother, "FW.BornChildTime", CurrentTime, true)
  ```
- It records the father and birth timestamp on `Mother`, but does **not** record:
  - `FW.BornChildName` (string list)
  - `FW.BornChildSex` (int list: `0` = Male, `1` = Female)
  - `FW.BornChildRef` (form list: the spawned `Actor` or `Armor`, or `None`)
- Storing those parallel lists on `Mother` makes existing-save migration 100% deterministic even when `BabySpawn == 0` (None) or when an NPC baby item was unequipped/lost.

### 2.6 Missing Events for Child Maturity, Rename, and Dismissal/Death
1. **Child Finishes Growing (`StartGrowing` 1 -> 0 when `ActorGrowUpToAdult` is false)**:
   - In [`FWChildActor.UpdateSize`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWChildActor.psc#L876-L907) and [`FWDefaultCustomChildEffect.FinalizeMature`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWDefaultCustomChildEffect.psc#L137-L153), when a child reaches full child size (`Age >= modifiedSizeDuration`) and `ActorGrowUpToAdult` is `false` (the default!), `FW.AddOn.StartGrowing` is set to `0`, but **no ModEvent is emitted**.
2. **Child Renamed (`bf:childname` / `FWChildActor.SetName`)**:
   - Renaming a child via [`FWSystem.psc:4496-4520`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L4496-L4520) or `FWChildActor.SetName` does not emit a ModEvent, so M.A.R.A.S's child registry keeps the old name.
3. **Child Deleted / Dismissed (`DeleteChild`) or Killed (`OnDeath`)**:
   - In [`FWChildActor.DeleteChild`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWChildActor.psc#L1217-L1254) (triggered by the *"Leave me alone!"* dialogue) and [`FWChildActor.OnDeath`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWChildActor.psc#L1014-L1023), the child is deleted from `FW.Babys`, but no ModEvent is emitted for M.A.R.A.S to remove the child record.

### 2.7 Multi-Father (Superfecundation) Desynchronization Inside Beeing Female NG
1. **`ActiveSpermImpregnationTimed` & `ActiveSpermImpregnationNoContraceptionTimed` Write `Fathers[]` in Reverse Order of `FW.ChildFather`**:
   - In [`FWController.psc:476-494`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L476-L494) and [`591-601`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L591-L601), the loop counts `numChild` down from `N-1` to `0`. `FWUtility.AddChildFather(Mother, a[j])` appends to `FW.ChildFather` in forward order (`0 .. N-1`), while `Fathers[numChild] = a[j]` writes into `Fathers` in **reverse order** (`N-1 .. 0`).
2. **`ImpregnateA` Passes Input Donor Pool Instead of Selected Fathers to `SendConceptionEvent`**:
   - In [`FWController.ImpregnateA`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L348-L357), each fetus picks a random father `tFather = Fathers[Utility.RandomInt(0, cSperm - 1)]` into `FW.ChildFather`, but `SendConceptionEvent(Mother, Fathers)` passes the original `Fathers` array instead of the actual selected `FW.ChildFather` entries.
3. **`GiveBirth` and `InstantBornChilds` Spawn Children in Reverse Order (`NumChilds - 1` down to `0`)**:
   - Both [`FWController.GiveBirth`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L1124-L1178) and [`FWSystem.InstantBornChilds`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L2358-L2370) iterate `NumChilds` downward (`NumChilds - 1` down to `0`), spawning the last fetus in `FW.ChildFather` first.

---

## Part 3: What Can Be Done in Beeing Female NG to Help Integrate into M.A.R.A.S

Since we have full access to `E:\Skyrim\development\mods\Beeing-Female-NG`, we can make clean, backward-compatible improvements directly to Beeing Female NG:

### 3.1 Fix Multi-Father Conception & Birth Ordering (`FWController`, `FWSystem`, `FWUtility`)
1. **Ensure `SendConceptionEvent` Reads Directly from `FW.ChildFather`**:
   - In [`FWController.SendConceptionEvent`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWController.psc#L361-L389), read `Father0`, `Father1`, `Father2` directly from `StorageUtil.FormListGet(Mother, "FW.ChildFather", 0..2)` so the event arguments always match `FW.ChildFather` regardless of whether `ImpregnateA` or `ActiveSpermImpregnation*` was used.
2. **Update `FWUtility.SendBirthEvent` Signature & Birth Call Sites**:
   ```papyrus
   Function SendBirthEvent(Actor akMother, Actor akFather, Form akBaby, int aiChildIndex = 0, int aiTotalChildren = 1, int aiFallbackSex = -1, string asFallbackName = "") Global
   ```
   - Push all 7 parameters to `BeeingFemaleBirth`: `(akMother, akFather, akBaby, bName, bSex, aiChildIndex, aiTotalChildren)`.
   - If `akBaby == None` (e.g. `BabySpawn == 0` or `3`), use `aiFallbackSex` and `asFallbackName` so `bName` and `bSex` are always valid even when no physical baby is spawned.
   - Stop `SendBirthEvent` from auto-firing `SendChildSpawnedEvent` (`BeeingFemaleChildSpawned` should only fire when a carried baby item transitions into a child actor in `ProcessBabyItemTransitionToChild`).
   - In `FWController.GiveBirth` and `FWSystem.InstantBornChilds`, pass the 0-based birth index `aiChildIndex` (`0 .. totalChildren - 1`) and `aiTotalChildren` (`totalChildren`) to `SpawnChild` -> `SendBirthEvent`.

### 3.2 Complete `SendAbortEvent` Coverage (Including Per-Fetus Stillbirths in Multi-Father Births)
1. **Read `akFather` before clearing `FW.ChildFather`**:
   - In `FWController.AbortusBaby*`, `AbortusState*`, and `FWAbilityBeeingFemale.castAbortus`, pass the father from `FW.ChildFather` before calling `FWUtility.ClearChildFathers(Mother)`.
2. **Fire `SendAbortEvent` on Individual Stillbirths, `UnimpregnateState`, and `FWSaveLoad.Delete`**:
   - In `FWController.GiveBirth` (line 1181) and `FWSystem.InstantBornChilds` (line 2371), when a specific fetus dies during birth (`UnbornHealth <= HealthRnd`), emit `FWUtility.SendAbortEvent(Mother, ChildFather[NumChilds], "stillbirth")` for that specific father's fetus! That way, if a woman is carrying twins from two different fathers and one twin is stillborn while the other survives, M.A.R.A.S removes **only** the stillborn father's unborn child record and promotes the surviving father's child!
   - Also emit `SendAbortEvent` in `FWController.UnimpregnateState` (`"unimpregnate"`) and `FWSaveLoad.Delete` (`"reset"`).

### 3.3 Fix Baby NTR (`AllowNTRbaby`) & Emit `BeeingFemaleFatherChanged`
- In [`FWAbilityBeeingFemale.psc`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWAbilityBeeingFemale.psc#L2865-L2870) (both Trimester 2 and Trimester 3 NTR blocks):
  - Update `FW.ChildFatherStr` and `FW.ChildFatherRace` at index `num_babies` alongside `FW.ChildFather`.
  - Call `FWUtility.SendFatherChangedEvent(ActorRef, oldFather, a, num_babies)` (`"BeeingFemaleFatherChanged"`) so M.A.R.A.S updates that specific unborn child's `bioFatherID` and recalculates its `PaternitySecret`.

### 3.4 Persist `FW.BornChildName`, `FW.BornChildSex`, and `FW.BornChildRef` on `Mother`
- In [`FWSystem.SpawnChild`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L2977-L2978), alongside `FW.BornChildFather` and `FW.BornChildTime`, record:
  ```papyrus
  StorageUtil.StringListAdd(Mother, "FW.BornChildName", bName, true)
  StorageUtil.IntListAdd(Mother, "FW.BornChildSex", bSex, true)
  StorageUtil.FormListAdd(Mother, "FW.BornChildRef", Baby, true)
  ```
- Update `FW.BornChildRef` when a baby item transitions into a child actor (`ProcessBabyItemTransitionToChild`) and when a child actor graduates into an adult (`GrowChildToAdult`).

### 3.5 Support NPC-NPC Baby Item Hatching in `BabySpawn == 2`
- In [`FWSystem.OnGameLoad`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSystem.psc#L768-L777) and [`FWSaveLoad.UpdatePerDay`](file:///E:/Skyrim/development/mods/Beeing-Female-NG/dist/Core/source/scripts/FWSaveLoad.psc#L120-L195):
  - Keep NPC mothers' baby items in `FW.Babys` and allow `FWAbilityBeeingFemale` / `FWSaveLoad.UpdatePerDay` to hatch expired baby items (`CurrentTime - FW.BabyItemDOB >= TimeBabyItemConvert`) for NPC-NPC couples as well, using each item's stored `FW.BabyItemFather[idx]`.

### 3.6 Add Lifecycle Events for Child Maturity, Rename, and Removal/Death
1. **`SendChildMaturedEvent(Actor akChild, Form akMother, Form akFather, string asChildName)` (`"BeeingFemaleChildMatured"`)**:
   - Emit in `FWChildActor.UpdateSize()` and `FWDefaultCustomChildEffect.FinalizeMature()` when `FW.AddOn.StartGrowing` transitions from `1` to `0`.
2. **`SendChildRenamedEvent(Actor akChild, string asNewName)` (`"BeeingFemaleChildRenamed"`)**:
   - Emit in `FWChildActor.SetName()` and in `FWSystem.psc` console command `bf:childname`.
3. **`SendChildRemovedEvent(Actor akChild, Form akMother, Form akFather, string asReason)` (`"BeeingFemaleChildRemoved"`)**:
   - Emit in `FWChildActor.DeleteChild()` (`asReason = "dismissed"`) and `FWChildActor.OnDeath()` (`asReason = "death"`).

---

## Part 4: Proposed M.A.R.A.S Integration & Migration Design

### 4.1 Lifecycle Stage Mapping in M.A.R.A.S for Beeing Female NG
| MARAS Stage | Beeing Female NG State / Representation | How MARAS Tracks It |
| :--- | :--- | :--- |
| **Stage 0 (`Unborn`)** | `FW.CurrentState` 4..7 (`FW.NumChilds > 0`, `FW.ChildFather[0..N-1]`) | Synthetic ID (`0x80000001+`) **per fetus**, each storing its own `bioFatherID` and `PaternitySecret`. Created on `BeeingFemaleConception` by reading `FW.ChildFather[0..NumChilds-1]`. Updated per-fetus if `BeeingFemaleFatherChanged` (NTR) fires. |
| **Stage 1 (`Infant`)** |<ul><li>**Mode 2 (`Item/Actor`)**: Carried `FW.BabyItemArmor` (`FW.BabyItemFather[idx]`)</li><li>**Mode 1 (`Actor`) while growing** (`StartGrowing == 1`)</li><li>**Mode 0/3 (`None`/`BabyGem`)**: Synthetic Infant</li></ul> | On `BeeingFemaleBirth`, promotes **only the single matching Stage 0 `Unborn` record whose `bioFather` matches `akFather`** to Stage 1 (`Infant`), preserving each twin/triplet's distinct biological father and paternity secret. |
| **Stage 2 (`Child`)** |<ul><li>**Mode 2 (`Item/Actor`)**: Hatched `FWChildActor` / custom child actor (`BeeingFemaleChildSpawned`)</li><li>**Mode 1 (`Actor`)**: Spawned `FWChildActor` / custom child actor</li><li>**Mode 0/3**: Synthetic Child after `TimeBabyItemConvert` days</li></ul> | Keyed by physical `Actor.GetFormID()` (if physical actor exists) or synthetic ID (if Mode 0/3), matched to its Stage 1 `Infant` record by `(mother, bioFather, childName)`. |
| **Stage 4 (`Adult`)** | Graduated adult `Actor` from `GrowChildToAdult` (`BeeingFemaleAdultChildSpawned`) | Promoted from Stage 2 `existingID` to `akAdultActor.GetFormID()` with `stage = 4`. Removed from child household aliases/packages. |

### 4.2 Fixing C++ Multi-Child / Multi-Father Promotion & Household Logic (`NPCRelationshipManager_Children.cpp` & `NPCRelationshipManager_Fertility.cpp`)
1. **Per-Child Unborn Promotion by Biological Father (`PromoteUnbornToInfant`)**:
   - In [`NPCRelationshipManager::PromoteUnbornToInfant`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L540-L589), instead of promoting **all** unborn children of `motherID` at once, promote a single unborn child (or add an optional `bioFatherID` / `unbornID` match so it promotes the first `ChildStage::Unborn` of `motherID` whose `bioFatherID` matches `akFather`, returning the promoted `childID`).
   - Remove the single-infant deduplication guard in [`NPCRelationshipManager::RegisterFertilityChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L1194-L1206) so a mother who gives birth to twins/triplets (from the same or different fathers) can have multiple Stage 1 `Infant` records simultaneously!
2. **Defer `EvaluateBioFatherStepIn` Until All Fetuses Are Registered**:
   - Do not trigger `EvaluateBioFatherStepIn` on fetus #0 before fetus #1..N (which may have different biological fathers!) are registered. Trigger it once after the entire conception batch is registered so all unmarried biological fathers in a superfecundation pregnancy are evaluated as step-in candidates.
3. **Accumulate All Newborn IDs Across a Multi-Child Delivery for `BuildChildBirthJson`**:
   - In `TTM_ServiceFertilityBF.OnBirth`, collect all `childID`s born during the current delivery batch and pass the complete `bornIDs` array on `aiChildIndex == aiTotalChildren - 1` into `MARAS.BuildChildBirthJson(akMother, bornIDs)`. This activates C++ [`BuildChildBirthInternal` Case B (Superfecundation)](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L1052-L1129) when twins/triplets have different biological fathers!
4. **Fix Stage 4 (`Adult`) Promotion in `RegisterChildActor`**:
   - When `syntheticID != 0` and `synthFormID` was a physical actor in `PlayerChildrenService`, call `PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(synthFormID)` and `RemoveChildSandboxAroundParentLinkedRef(synthFormID)` before erasing `synthIt`.
   - In `SyncChildHouseholdState`, if `entry.stage == ChildStage::Adult`, call `PlayerChildrenService::GetSingleton().OnChildRemovedFromHousehold(childID)` and `UnregisterActor(childID)` instead of `OnChildAddedToHousehold` so grown adults are never placed into `PlayerChildFaction` or given child sandbox/bed packages.

### 4.3 One-Time Save Migration (`TTM_ServiceFertilityBF.Migrate()`)
Modeled after `TTM_ServiceFertilityFMR.Migrate()`, executed once per save and supplemented by `Sync()` on subsequent loads:
1. **Pass 1 — Scan `FW.SavedNPCs` (plus `PlayerRef`) for Active Pregnancies (`Stage 0 Unborn`)**:
   - Check `FW.CurrentState` (states 4..7) and `FW.NumChilds > 0`.
   - Read **every fetus's individual father** from `StorageUtil.FormListGet(mother, "FW.ChildFather", i) as Actor` for `i = 0 .. NumChilds - 1` and `FW.LastConception`, registering each unborn child with its own biological father and paternity secret edges.
2. **Pass 2 — Scan `FW.SavedNPCs` (plus `PlayerRef`) for Carried Baby Items (`Stage 1 Infant`)**:
   - Loop `i = 0 .. StorageUtil.FormListCount(mother, "FW.BabyItemArmor") - 1`.
   - Read each baby item's individual `FW.BabyItemName[i]`, `FW.BabyItemSex[i]`, `FW.BabyItemFather[i]`, and `FW.BabyItemDOB[i]`, registering a distinct Stage 1 `Infant` record per baby item with its own biological father.
3. **Pass 3 — Scan `FW.Babys` for Physical Child/Adult Actors (`Stage 2 Child` / `Stage 4 Adult`)**:
   - For each `Actor` in `FW.Babys`, read `FW.Child.Mother`, `FW.Child.Father`, `FW.Child.Name`, `FW.Child.DOB`, `FW.Child.GrownUp`, and `FW.AddOn.StartGrowing` (using **`GetIntValue`**, never `SetIntValue`!).
   - Match to any existing synthetic record by `(mother, bioFather, childName)` or register directly with full lineage and paternity secret edges.
4. **Pass 4 — Scan `FW.SavedNPCs` for `FW.BornChildFather` / `FW.BornChildTime` History**:
   - Reconstruct any historical births not already covered by Passes 2–3 using the parallel lists `FW.BornChildFather[i]`, `FW.BornChildTime[i]`, `FW.BornChildName[i]`, and `FW.BornChildSex[i]`.

### 4.4 Resolving `SpawnSyntheticChild` ActorBase with Beeing Female NG
- In [`TTM_ServiceChildrenLogic.SpawnSyntheticChild`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc#L1303), resolve `FWBabyItemList` from `BeeingFemale.esm` when BF NG is active.

---

## Part 5: Decision — MARAS Takes Over Beeing Female Child Behavior (No Changes to BF Child AI)

**Decision:** Beeing Female NG keeps doing what it does best (scaling, growth, stats, skills, adult graduation). **M.A.R.A.S forcefully owns all child behavior** (packages, factions, teammate/follower state, dialogue). **No changes are made to Beeing Female's child AI handling or dialogue**; everything below is done on the MARAS side only. (Part 3 changes to BF's *events/persistence* are a separate matter from child AI.)

### 5.1 How Beeing Female Controls Children (Findings)
- BF creates child actors from shared base `ActorBase` records. Per-child state is the **`JumpingBonus` actor value** (`FWChildSettings.OrderAV()`), set by `FWChildActor.RefreshAI()` from `_order` (0 wait, 1 wait+play, 10 follow other parent, 11 go home, 12 meet place, 30/31 follow player, 99 delete). `RefreshAI()` also sets `WaitingForPlayer`, `SetPlayerTeammate(...)`, then `EnableAI(false)` / `EvaluatePackage()` / `EnableAI(true)`.
- The base NPCs carry **package formlists whose packages are conditioned on `JumpingBonus`**. Packages are therefore base-record packages, not alias-assigned and not script-forced.
- Shared destinations are aliases on `ChildDialogueQuest` (`OtherParent`, `MeetPoint`, `PlayerHome`, `PlayerHomeLoc`). Those only feed BF's own packages and are irrelevant once MARAS owns behavior.
- BF dialogue is gated by the custom classes `_BFChildPlayer` / `BFChildNpc` and lives in exactly two dialogue quests (both *Start Game Enabled* + *Run Once*, no other BF quests contain dialogue):

| Quest | FormID (as seen in xEdit) | Children covered |
| :--- | :--- | :--- |
| `BF_ChildDialNPCChildren` | `02023EAD` | NPC-parent children |
| `BF_ChildDialPlayerChildren` | `0201ACB8` | Player-parent children |

> [!NOTE]
> The leading `02` is the load-order index in the user's load order. Scripts must resolve forms with `Game.GetFormFromFile(<local FormID>, "BeeingFemale.esm")` using the local FormID (`0x023EAD`, `0x01ACB8`), never the load-order-prefixed value.

### 5.2 Why Packages Need No BF Changes
`PackageOverrideService` hooks the engine's winning-package selection (`PackageStartHook`). For every **registered** actor it replaces the winning package with the MARAS package (or re-asserts it when the candidate is null). BF's `JumpingBonus`-conditioned packages are just losing candidates, and BF's own `EvaluatePackage()` calls re-run the hook, which wins again. Only registered actors that are **player followers** (`Utils::IsPlayerFollower`) or **whitelisted** bypass the hook.

### 5.3 Required MARAS-Side Behavior

1. **Stop BF child dialogue quests during migration (when Beeing Female is detected)**
   - Runs in `TTM_ServiceFertilityBF` **migration** (`Migrate()`, the one-time pass) whenever `Game.GetModByName("BeeingFemale.esm") != 255`, and is re-checked in `Sync()` on later loads (cheap `IsRunning()` guard) in case a BF update or reset restarts them.
   - For both quests (`0x023EAD` NPC, `0x01ACB8` Player): `Quest q = Game.GetFormFromFile(<local id>, "BeeingFemale.esm") as Quest`; `if q && q.IsRunning()` → `q.Stop()`. **Do not `Reset()`**.
   - Safety: BF never calls `Start()`/`Stop()`/`Reset()` on these quests. Its alias writes (`ForceRefTo`, `ForceRefIfEmpty`, `ForceLocationTo`) on a stopped quest are no-ops that only log Papyrus warnings; `GetAliasByName` still works and `SetMeetPoint` takes its null-safe branch. Aliases are only needed by BF's own packages, which MARAS overrides.
   - Start-game-enabled quests do not auto-restart after `Stop()`, and a stopped state is saved with the game. This is unconditional while BF is detected (no user setting).

2. **Player-teammate and follower faction cleanup**
   - Cause: BF's `RefreshAI()` calls `SetPlayerTeammate(true)` for orders ≥ 30 and the default order is 31, so BF children follow the player and satisfy `IsPlayerFollower`, which makes `PackageStartHook` skip the sandbox override.
   - On adoption/registration of any BF-created child (`FWChildActor` or BF custom child actor) and on every `Sync()` for children MARAS owns: call `SetPlayerTeammate(false)` and remove `CurrentFollowerFaction` (Skyrim `0x5C84E`) from the child.
   - Optional hygiene (MARAS-side only): set `JumpingBonus` to `11` (Go Home) at adoption. This is not required, since BF rewrites it from `_order` anyway.
   - BF's `OnUpdateGameTime` (every 5 game hours) re-runs `RefreshFactions()` and re-adds transferred parent factions; MARAS's periodic sync must re-clear external factions (`ClearChildExternalFactions`) for owned children.

3. **Do not check the PackageOverride whitelist for Beeing Female**
   - Explicitly **no whitelist verification or ini changes** for `BeeingFemale.esm`. No plugin/quest/package whitelist entries are added or audited for BF. (Dialogue quests are stopped and packages are overridden regardless, so no exemption is needed.)

### 5.4 Updated Resolution of the Original Design Questions
1. **`FWChildActor` vs. MARAS household AI:** **Resolved.** BF keeps scaling/growth; MARAS forcefully owns packages, factions, teammate/follower state, and dialogue (5.3). Adopt `FWChildActor`s into the MARAS household immediately at Stage 2 via the normal `AdoptChild` / `RegisterChildActor` path.
2. **Changes in `Beeing-Female-NG`:** BF **child AI handling and dialogue are not modified**. Event/persistence improvements in Part 3 remain separate and are still pending approval.

### 5.5 Implementation Checklist (MARAS side)
- [ ] `TTM_ServiceFertilityBF.Migrate()` (new): include the dialogue-quest stop when BF is detected; keep a lightweight re-check in `Sync()`.
- [ ] `TTM_ServiceFertilityBF` adoption/registration + `Sync()`: `SetPlayerTeammate(false)` and remove `CurrentFollowerFaction` for owned BF children.
- [ ] `TTM_ServiceFertilityBF.Sync()`: fix `SetIntValue` → `GetIntValue` for `FW.AddOn.StartGrowing`.
- [ ] Fix `BeeingFemaleBirth` signature mismatch (5 args vs 7) across `TTM_MainController`, `TTM_ServiceFertility`, `TTM_ServiceFertilityBF`.
- [ ] Fix stage-4 promotion and multi-child/multi-father handling in C++ (Part 4.2).
- [ ] Update docs per project rules (`docs/modder/FERTILITY_MODE_RELOADED_INTEGRATION.md`, `SKYRIMNET_AND_AI_INTEGRATION.md`/BF-related guides, `docs/modder/services/*` as applicable).
