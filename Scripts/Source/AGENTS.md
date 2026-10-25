# AGENTS.md — Papyrus Scripts (`Scripts/Source/`)

These rules apply to all Papyrus scripts (`.psc`) in `Scripts/Source/`. They supplement the root `AGENTS.md`.

---

## 1. Naming, Structure & Compiler Rules

- **Prefix & Case**:
  - Script names and global helper scripts must use the `TTM_` prefix (e.g., `TTM_ServiceSkyrimNet`, `TTM_Data`, `TTM_Utils`), except for `MARAS.psc`.
  - Use `PascalCase` for script names, function names, and properties.
- **Null Safety**: Validate actor arguments (`if (!akActor)`) before calling name formatting, faction mutations, or event dispatches.
- **No Array Comparison with `None`**:
  - Never compare an array variable to `None` or `none` (e.g., `if (actors != none)` or `if (actors == none)`). In Papyrus, comparing arrays directly to `None` produces a compiler type mismatch error.
  - Instead, check the array's length: `if (actors.Length > 0)` or `if (actors.Length == 0)`. Querying `.Length` on an empty or uninitialized array in Papyrus safely evaluates to `0`.

---

## 2. `MARAS.psc` $\leftrightarrow$ C++ Native Binding Parity

- `Scripts/Source/MARAS.psc` is the native binding interface for `MARAS.dll`.
- Every `global native` function in `MARAS.psc` must match the exact parameter types and return type declared in `SKSE_Source/include/papyrus/PapyrusInterface.h` and registered in `MARAS::PapyrusInterface::RegisterPapyrusFunctions` (`SKSE_Source/src/papyrus/PapyrusInterface.cpp`).
- Do **not** add wrapper aliases or duplicate function names in `MARAS.psc` (per Root Rule 3: No Unsolicited API Aliases).

---

## 3. SkyrimNet Gating & Event Dispatch (`TTM_ServiceSkyrimNet.psc`)

- **Caller-Level Gating (`TTM_Data.GetHasSkyrimNet()`)**:
  - Check `TTM_Data.GetHasSkyrimNet()` at the call site before doing preparatory string formatting or dice rolls, and at the entry of `TTM_ServiceSkyrimNet` functions (except functions invoked exclusively as SkyrimNet YAML Action Executor callbacks in `TTM_Actions.psc`):
    ```papyrus
    Function RegisterAffectionGift(string msg, Actor receiver) global
        if(!TTM_Data.GetHasSkyrimNet())
            return
        endif
        Actor player = TTM_Data.GetPlayer()
        string json = BuildJson("ttm_affection_gift", msg)
        SkyrimNetApi.RegisterEvent("ttm_event", json, player, receiver)
    EndFunction
    ```
- **JSON Formatting**: Always use `TTM_ServiceSkyrimNet.BuildJson(...)` when dispatching JSON payloads to `SkyrimNetApi`.

---

## 4. Event-Driven Architecture via `TTM_MainController.psc`

- Avoid creating new `RegisterForUpdate` / `OnUpdate` polling loops in Papyrus scripts. Periodic background polling belongs in C++ (`PollingService`).
- Subscribe to SKSE `ModCallbackEvent` notifications in `TTM_MainController.psc` (`MARAS_StatusChanged`, `MARAS_AffectionChanged`, `MARAS_HierarchyChanged`, `MARAS_HouseAssigned`, `MARAS_CaughtCheating`, `MARAS_ActionDeclined`, `MARAS_ChildUpdated`) and route to the appropriate `TTM_Service*` script.

---

## 5. Children System & ActorValue Conventions (`TTM_ServiceChildrenLogic.psc`)

Household children assigned to `ChildLogic` (`0xC25`) and `ChildrenSandbox` (`0x2BC`) use two dedicated ActorValues to drive Creation Kit AI package conditions in `TT_MARAS.esp`:

- **`Variable06` (Child Orders & Active Games)**:
  - `0` = Default schedule (sandbox inside/outside by `GameHour`).
  - `1` = Ordered inside (`order_inside_accept`).
  - `2` = Ordered to bed (`order_bed_accept` / `game_bedtime`).
  - `3` = Ordered outside (`order_outside_accept`).
  - `4` = Ordered to do chores (`order_chores_accept`).
  - `100` = Playing Tag / Hide-and-Seek (`StartGame`).
- **`Variable07` (ForceGreet Topic Queue)**:
  - `0` = No forcegreet active.
  - `1` = Allowance request (`allowance_topic`).
  - `2` = Gift for player (`gift_topic`).
  - `3` = Children's chest update (`chest`).
  - `4` = Found critter request (`critter_topic`).
  - `5` = Stray pet request (`pet_topic`).
  - `6` = Game invitation (`game_ask`).
  - `10` = Scared of pet / bailout (`pet_bailout`).
- **Mandatory Package Refresh**: Whenever modifying `Variable06` or `Variable07` on a child actor, immediately call `akChild.EvaluatePackage()` so the engine transitions the child's AI package without delay.

---

## 6. Creation Kit Fragment Safety (`TTM_DialogueManager*.psc`, `TTM_Quest*.psc`)

- In Creation Kit generated fragment scripts (`TTM_DialogueManager.psc`, `TTM_DialogueManagerChildren.psc`, `TTM_QuestDivorce.psc`, `TTM_QuestTrackKilledSpouses.psc`, `TTM_QuestTrackNewAdoption.psc`, `TTM_ChildrenPackages.psc`), **never** alter the `;BEGIN FRAGMENT` / `;END FRAGMENT` headers, `;NEXT FRAGMENT INDEX`, or `;BEGIN AUTOCAST` blocks, and keep fragment logic strictly inside `;BEGIN CODE` and `;END CODE`.

---

## 7. MCM Deprecation & Fertility Mod Status

- **MCM Deprecated**: Mod Configuration Menu (MCM) settings (`TTM_MCM_SettingsPage.psc`) are **deprecated** and retained solely for save backward compatibility. Do **NOT** add new MCM settings or UI fields; all new settings must be integrated into the Prisma UI dashboard instead.
- **Fertility Integration**:
  - **Fertility Mode Reloaded (FMR)** (`TTM_ServiceFertility.psc`) is the active, supported fertility integration.
  - **Fertility Mode Reloaded (FMR)** (`TTM_ServiceFertilityFMR.psc`) and **Beeing Female NG** (`TTM_ServiceFertilityBF.psc`) are both active, supported fertility integrations routed through `TTM_ServiceFertility.psc`. Keep both integrations synchronized whenever modifying pregnancy, birth, custody, or child-spawning logic.
