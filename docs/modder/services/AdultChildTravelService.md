# AdultChildTravelService — Independent Adult Offspring Travel & Inn/Family Visits

`AdultChildTravelService` (`SKSE_Source/include/core/AdultChildTravelService.h`, `SKSE_Source/src/core/AdultChildTravelService.cpp`) governs the autonomous world behavior of physically spawned Stage 4 (`ChildStage::Adult`) offspring. Rather than remaining permanently parked as domestic tenants in a player house, grown-up offspring act as **independent traveling adventurers** who journey between Skyrim's inns and cities, occasionally stop by to visit their parents' or the player's homes, and remain available to be recruited as followers.

---

## 1. Design Philosophy & Behavior Model

When a child completes adolescent training (`ChildStage::Training` $\rightarrow$ `ChildStage::Adult`) and is spawned into the world via `TTM_ServiceChildrenLogic.SpawnAdultChild`, they are no longer managed by `PlayerChildrenService` or registered as permanent house tenants in `PlayerHouseService`.

Instead, `AdultChildTravelService` drives their AI schedule by combining:
1. **Dynamic Destination Index**: All interior inns/taverns across Skyrim (`LocTypeInn`, `0x0001CB87`) plus the child's family homes (owned Player Houses for player-related offspring, and custodial/biological parents' homes or live parent actors).
2. **Engine Travel & Sandbox Package (`000DD3:TT_MARAS.esp`)**: Uses the dedicated `TTM_AdultChildTravelSandbox` (`0xDD3`, templated on pure `Sandbox` `01C254:Skyrim.esm`) paired with the `TTM_ChildrenSandboxAroundParent` linked-reference keyword (`0x377`). Because package `0xDD3` uses `LocationKeyword(0x377, Radius = 3000)` with `UnlockDoorsAtPackageStart`/`End`, `PreferredSpeed = Walk`, and enables eating, sleeping, sitting, conversation, wandering, and idle markers (without any `KeepAnEyeOn` head-tracking target), dynamically updating the `0x377` linked reference causes Skyrim's pathfinding engine to walk the adult child along the road network between cities, enter the target inn or family house, and sandbox naturally inside.
3. **Follower & Scene Yielding (`PackageOverrideService`)**: Whenever the player recruits the adult child as a follower (`IsPlayerTeammate()` / `CurrentFollowerFaction`), or when the actor is in combat or an active quest scene, `PackageOverrideService`'s 7-step safety gate and `AdultChildTravelService`'s rotation checks automatically yield so follower packages and combat AI take full priority.

---

## 2. Destination Indexing (`BuildDestinationIndex`)

At `kDataLoaded` (right after `HomeCellService::BuildIndex()`), `BuildDestinationIndex()` scans `RE::TESDataHandler::GetSingleton()->interiorCells` to build the province-wide inn/tavern destination table:

- **Eligibility**: An interior cell qualifies if it has a valid `RE::BGSLocation` carrying the `LocTypeInn` keyword (`0x0001CB87`) and is not marked as a forbidden/test cell.
- **Target Marker Resolution**:
  1. Prefers the location's `LocationCenterMarker` (`0x0001BDF1`) or `HomeCellService::GetDoorMarkerForCell(cellFormID)`.
  2. Falls back to the first valid persistent reference inside the interior cell (`cell->GetRuntimeData().references`).
- **Display Naming**: Uses the `BGSLocation` display name (or the interior cell's name) so the Prisma UI dashboard can display where the adult child is currently traveling or staying (e.g., `"The Bannered Mare"`, `"The Bee and Barb"`, `"Candlehearth Hall"`).

---

## 3. Weighted Destination Selection & Rotation

### 3.1 Family Visit vs. Inn/Tavern Weighting (`PickNextDestination`)
Whenever an adult child needs a new destination, `AdultChildTravelService` builds a per-actor list of **Family Destinations**:
- **Player-Related Offspring** (where the Player is a guardian, biological mother, or known biological/presumed father): All currently owned/available player houses from `PlayerHouseService::GetSingleton().GetAllPlayerHouses()`, resolved to each house's interior home marker (`GetHouseMarkerFormID`).
- **NPC Parents / Co-Guardians**: For each living non-player guardian (`guardianAID`, `guardianBID`), biological mother (`bioMotherID`), and eligible father (`visitBioFatherID` / `visitPresumedFatherID`), resolves their assigned player house marker (if housed in a player home), their original home center marker (`HomeCellService`), or the parent actor reference itself.
- **Paternity Secret Rules for Father Visits**:
  - **Bio knows, Presumed doesn't** (`!isSecretForBioFather && isSecretForPartner`): Child believes the presumed father is their father; visits only `presumedFatherID` (never `bioFatherID`).
  - **Bio knows, Presumed knows** (`!isSecretForBioFather && !isSecretForPartner`): Child can visit `bioFatherID`, and if `presumedFatherID` is still in an active relationship with the mother (or is a guardian), child can visit **both** `bioFatherID` and `presumedFatherID`.
  - **Bio doesn't know, Presumed doesn't know** (`isSecretForBioFather && isSecretForPartner`): Child believes the presumed father is their father; visits only `presumedFatherID` (never `bioFatherID`).
  - **Bio doesn't know, Presumed knows** (`isSecretForBioFather && !isSecretForPartner`): Child visits only `presumedFatherID` (if still with the mother or a guardian), never `bioFatherID`.

Destination selection uses a **30% / 70% weighted roll**:
- **30% Chance — Family Visit**: Picks one of the adult child's family destinations (excluding the current destination when alternatives exist) so grown children periodically visit their parents or stop by the player's manors without living there permanently.
- **70% Chance — Inn / Tavern Travel**: Picks a random inn across Skyrim from the `LocTypeInn` index (avoiding the current marker so they actively travel to a new hold or town).

### 3.2 Arrival Detection & 2–4 Day Stay Cycle (`Tick` & `HasActorReachedDestination`)
Rather than rotating on a naive fixed timer from the moment a destination is picked (which could cause an adult child to turn around mid-journey while walking across Skyrim), destination rotation is **gated on physical arrival**:

1. **In-Transit Phase (`arrivedGameDay < 0.0f`)**:
   - When `AssignNewDestination` selects a target, it records `assignedGameDay = currentGameDay`, sets `arrivedGameDay = -1.0f` (in transit), and rolls a random `stayDurationDays` in **`[2.0, 4.0]` in-game days** (`kMinStayDurationDays`..`kMaxStayDurationDays`).
   - Every 5 seconds (`PollingService::Update` $\rightarrow$ `AdultChildTravelService::Tick(currentGameDay)`), `HasActorReachedDestination` checks whether the adult child has arrived:
     - **Interior / Cell Match**: `actorCell == targetCell` or `actorCell->GetFormID() == state.targetCellID`.
     - **Location Match**: `actor->GetCurrentLocation()` matches `state.targetLocationID` (or is a child location of it).
     - **Exterior Proximity**: Both actor and target are in the same exterior cell within `2500.0f` units (`kArrivalDistanceUnits`).
2. **Staying / Visiting Phase (`arrivedGameDay >= 0.0f`)**:
   - The moment arrival is detected, `state.arrivedGameDay` latches to `currentGameDay`.
   - The adult child remains at the inn or family home—sandboxing, eating, sitting, sleeping, and conversing—until `(currentGameDay - state.arrivedGameDay) >= state.stayDurationDays` (2 to 4 full in-game days **after arrival**), and only then picks a new destination.
3. **Unreachable Fallback (`kMaxTravelTimeoutDays = 10.0f`)**:
   - If an adult child remains in transit for more than 10 in-game days without reaching the target (e.g., blocked by a locked modded cell), `Tick` automatically rotates to a new destination.
4. **Active Follower / Combat Guard**:
   - If the adult child is currently a player teammate (`IsPlayerTeammate()`), in combat (`IsInCombat()`), or participating in an active scene (`GetCurrentScene() != nullptr`), `Tick` skips arrival and rotation checks so their active gameplay state is undisturbed.

---

## 4. Save/Load Persistence & Prisma UI Integration

- **Co-Save Persistence (`'ACTR'` Chunk)**:
  - `'ACTR'` co-save serialization runs through `State::SaveWorldState` / `State::LoadWorldState` (`CoSaveCodec` driven by [`Serialization.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/Serialization.cpp)), persisting each adult child's active `targetMarkerID`, `targetLocationID`, `targetCellID`, `destinationName`, `assignedGameDay`, `arrivedGameDay`, `stayDurationDays`, and `isFamilyVisit` in `WorldStore` (`household.adultChildTravelDestinations`).
  - When a save is loaded, `AdultChildTravelService::ReassertAllOnLoad()` runs at `kPostLoadGame` (after all co-save chunks have loaded), reads the restored `targetMarkerID` from `WorldStore::Snapshot()`, and re-binds the `0x377` linked reference and `0xDD3` package to that exact destination without picking a new random location—so the adult child continues traveling to (or staying at) the exact destination they had before saving, with their arrival state and 2–4 day stay timer intact.
- **Prisma UI Status Readout**:
  - `PrismaUIDataBridge::BuildNPCProfileJson` queries `AdultChildTravelService::GetSingleton().GetCurrentDestinationName(formID)` for spawned Stage 4 offspring and populates `currentHome` with `"Traveling to <Destination>"` while in transit, `"Staying at <Inn>"` once arrived at an inn, or `"Visiting <Home>"` once arrived at a family home.
  - In `ChildProfileResidenceSection.tsx`, spawned Stage 4 offspring display an **Independent Traveling Adventurer** status card showing their current travel or stay status rather than a static house assignment dropdown.
