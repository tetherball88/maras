# AGENTS.md — Papyrus Scripts (`Scripts/Source/`)

Applies to all Papyrus scripts (`.psc`) in `Scripts/Source/` (compiled via `skyrimse.ppj` / VS Code `pyro: Compile Project (skyrimse.ppj)` task). Supplements root `AGENTS.md`.

---

## 1. Naming, Null/Array Safety & Logging

- **Naming**: `TTM_` prefix on all scripts except `MARAS.psc`; `PascalCase` for scripts, functions, and properties.
- **Null & Array Checks**: Guard actor args (`if (!akActor)`) before formatting/factions/events. **Never** compare Papyrus arrays to `None` (`actors != None` fails compilation)—always check `actors.Length > 0` or `actors.Length == 0`.
- **Logging**: Use `TTM_Debug.trace / debug / info / warn / err` per root logging tiers.

---

## 2. `MARAS.psc` Parity & Event-Driven Flow (`TTM_MainController.psc`)

- **`MARAS.psc` $\leftrightarrow$ C++ Parity**: Every `global native` signature in `Scripts/Source/MARAS.psc` must match `SKSE_Source/include/papyrus/PapyrusInterface.h` and `RegisterPapyrusFunctions` (`src/papyrus/PapyrusInterface.cpp`) with zero duplicate alias wrappers.
- **Event-Driven (No Polling)**: Do not add `RegisterForUpdate` / `OnUpdate` loops (background polling belongs in C++ `PollingService`). Route SKSE `ModCallbackEvent`s (`maras_status_changed`, `maras_affection_changed`, `maras_hierarchy_changed`, `maras_house_assigned`, `maras_caught_cheating`, `maras_action_declined`, `maras_child_updated`) via `TTM_MainController.psc`.
- **Unloaded Actor Sender Fallback**: When an NPC's `RE::Actor*` is unloaded, C++ sends `maras_status_changed` with `PlayerRef` as `akSender`—always resolve the NPC via the passed FormID parameter rather than `akSender`. (`OnRelationshipChangedFull` already triggers `TTM_ServiceBuff.Maintenance()` when `sendEvent == true`.)

---

## 3. SkyrimNet Gating & Audience Isolation (`TTM_ServiceSkyrimNet.psc`)

- **Caller-Level Gate**: Check `TTM_Data.GetHasSkyrimNet()` at call sites and at entry to `TTM_ServiceSkyrimNet` functions (except YAML action executor callbacks in `TTM_Actions.psc`).
- **C++ Event Bridge (`MARAS.RegisterSkyrimNetEvent`)**: Never call `SkyrimNetApi.RegisterEvent` or `DirectNarration` directly from Papyrus (they broadcast `Nearby` unconditionally). Always format payloads via `TTM_ServiceSkyrimNet.BuildJson(...)` and dispatch through `MARAS.RegisterSkyrimNetEvent(eventType, content, originator, target, exactAudience, audience)` / `TTM_ServiceSkyrimNet.DirectNarration(...)`:
  - Default `exactAudience = false` (`Nearby`) for public/interpersonal events.
  - Set `exactAudience = true` (`Exact`) **only** for private internal events (`RegisterPregnancyAnnounced` $\to$ `[mother]`) or events carrying an undiscovered paternity secret (`RegisterPaternitySecretEvent`, `RegisterChildBirth*`, `RegisterChildGrewUp`, `RegisterChildSentToTraining`, `RegisterChildBecameAdult`), splitting into a secret-aware event for privy actors (`mother` + aware `bioFather`) and a `maskSecrets = true` event for `[presumedFather]`.

---

## 4. Children ActorValues, CK Fragments & Integrations

- **Child AI Package ActorValues (`TTM_ServiceChildrenLogic.psc`)**:
  - **`Variable06` (Orders/Games)**: `0` = schedule, `1` = inside, `2` = bed, `3` = outside, `4` = chores, `100` = Tag / Hide-and-Seek.
  - **`Variable07` (ForceGreet Queue)**: `0` = none, `1` = allowance, `2` = gift, `3` = chest, `4` = critter, `5` = stray pet, `6` = game invite, `10` = scared of pet.
  - Always call `akChild.EvaluatePackage()` immediately after changing `Variable06` or `Variable07`.
- **CK Fragment Safety**: In CK-generated scripts (`TTM_DialogueManager*.psc`, `TTM_Quest*.psc`, `TTM_ChildrenPackages.psc`), never alter `;BEGIN FRAGMENT` / `;END FRAGMENT`, `;NEXT FRAGMENT INDEX`, or `;BEGIN AUTOCAST`; edit strictly inside `;BEGIN CODE` ... `;END CODE`.
- **MCM & Fertility**: MCM (`TTM_MCM_SettingsPage.psc`) is **deprecated** (Prisma UI only for new settings). Keep **both** **Fertility Mode Reloaded** (`TTM_ServiceFertilityFMR.psc`) and **Beeing Female NG** (`TTM_ServiceFertilityBF.psc`) synchronized via `TTM_ServiceFertility.psc`.
