# AGENTS.md — Papyrus Scripts (`Scripts/Source/`)

Applies to all Papyrus scripts (`.psc`) in `Scripts/Source/` (compiled via `skyrimse.ppj` / VS Code `pyro: Compile Project (skyrimse.ppj)` task). Supplements root `AGENTS.md`.

---

## 1. Naming, Null/Array Safety & Logging

- **Naming**: `TTM_` prefix on all scripts except `MARAS.psc`; `PascalCase` for scripts, functions, and properties.
- **Null & Array Checks**: Guard actor args (`if (!akActor)`) before formatting/factions/events. **Never** compare Papyrus arrays to `None` (`actors != None` fails compilation)—always check `actors.Length > 0` or `actors.Length == 0`.
- **Logging**: Use `TTM_Debug.trace / debug / info / warn / err` per root logging tiers.

---

## 2. `MARAS.psc` Parity & Event-Driven Flow (`TTM_MainController.psc`)

- **`MARAS.psc` $\leftrightarrow$ C++ Parity**: Every `global native` signature in `Scripts/Source/MARAS.psc` must match [PapyrusInterface.h](../../SKSE_Source/modules/bridge_papyrus/include/maras/bridge_papyrus/PapyrusInterface.h) and `RegisterPapyrusFunctions` ([PapyrusInterface.cpp](../../SKSE_Source/modules/bridge_papyrus/src/PapyrusInterface.cpp) + sibling `Papyrus*.cpp` adapters under `SKSE_Source/modules/bridge_papyrus/src/`) with zero duplicate alias wrappers. Read queries—including `MARAS.GetTrackedNpcHomeMarker` and `MARAS.GetChildHomeMarker` (`PapyrusHousehold.cpp`)—must remain strictly read-only CQS snapshots.
- **Event-Driven (No Polling)**: Do not add `RegisterForUpdate` / `OnUpdate` loops (background polling belongs in C++ `PollingService`). Route active quest/scene SKSE `ModCallbackEvent`s (`maras_status_changed_full`, `maras_change_affection`, `maras_actor_died`, `maras_spawn_synthetic_child`, `maras_cheating_caught`, `maras_open_relationship_walkin`, `maras_partner_home_arrival_event`, `maras_stop_polygamy_quest`) via `TTM_MainController.psc` (`SkyrimNet` `ttm_*` events emit natively in C++).
- **Unloaded Actor Sender Fallback**: When an NPC's `RE::Actor*` is unloaded, C++ sends `maras_status_changed` (external listener compatibility) with `PlayerRef` as `akSender`—always resolve the NPC via passed FormID/JSON parameters (as in `maras_status_changed_full`) rather than `akSender`. Player spouse/follower buff reconciliation is handled natively by C++ `BuffReconciler` (`TTM_ServiceBuff.psc` and the `OnTeammateChange` listener are retired).

---

## 3. SkyrimNet Gating & Native Event Bridge (`PapyrusSkyrimNet.cpp`)

- **Caller-Level Gate**: `TTM_ServiceSkyrimNet.psc` is **retired** (only `TTM_MainController.RegisterSkyrimNetEventSchema()` registers the `"ttm_event"` event schema via `SkyrimnetApi.RegisterEventSchema` on startup/load). Always check `TTM_Data.GetHasSkyrimNet()` at Papyrus call sites before invoking `MARAS.SuppressNextActionNarration`, `MARAS.DirectNarration`, `MARAS.Register*` SkyrimNet functions, or `MARAS.RegisterSkyrimNetEvent` (except YAML action executor callbacks in `TTM_Actions.psc`).
- **Native C++ Event Dispatcher (`SkyrimNetEventService` / `EventDispatchPlan`)**: Never call `SkyrimnetApi.RegisterEvent` or `SkyrimnetApi.DirectNarration` directly from Papyrus (they broadcast `Nearby` unconditionally without C++ proximity checks or secret isolation). Delegate Papyrus-originated SkyrimNet events through the `MARAS` SkyrimNet natives (`PapyrusSkyrimNet.cpp`), which route through `Projection::EventDispatchPlan` (`PerspectiveView`-based `exactAudience` splits, `MaskForPartner` / `MaskForChildOnly` redaction, `0x14` Player speaker-fallback suppression, and `steady_clock` action-narration suppression).

---

## 4. Children ActorValues, CK Fragments & Integrations

- **Child AI Package ActorValues (`TTM_ServiceChildrenLogic.psc`)**:
  - **`Variable06` (Orders/Games)**: `0` = schedule, `1` = inside, `2` = bed, `3` = outside, `4` = chores, `100` = Tag / Hide-and-Seek.
  - **`Variable07` (ForceGreet Queue)**: `0` = none, `1` = allowance, `2` = gift, `3` = chest, `4` = critter, `5` = stray pet, `6` = game invite, `10` = scared of pet.
  - Always call `akChild.EvaluatePackage()` immediately after changing `Variable06` or `Variable07`.
- **CK Fragment Safety**: In CK-generated scripts (`TTM_DialogueManager*.psc`, `TTM_Quest*.psc`, `TTM_ChildrenPackages.psc`), never alter `;BEGIN FRAGMENT` / `;END FRAGMENT`, `;NEXT FRAGMENT INDEX`, or `;BEGIN AUTOCAST`; edit strictly inside `;BEGIN CODE` ... `;END CODE`.
- **MCM, Modals & Fertility**: Legacy SkyUI MCM pages (`TTM_MCM_*.psc`) and `TTM_ServiceBuff.psc` are **retired** (all settings live in the Prisma UI Dashboard; only the self-gating `TTM_MCM.psc` ongoing-save Dashboard launcher stub remains on Quest `000800` for `.ess` save safety). Interactive confirmations triggered from Papyrus use standalone C++ `PrismaModalManager` modals (`PauseGame = true`, `disableFocusMenu = false`, never `RE::MessageBoxMenu` / vanilla Debug message boxes). Keep **both** **Fertility Mode Reloaded** (`TTM_ServiceFertilityFMR.psc`) and **Beeing Female NG** (`TTM_ServiceFertilityBF.psc`) synchronized via `TTM_ServiceFertility.psc`.
