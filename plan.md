# Phase 2 Verification & Remaining Work

### ✅ Task 1: Group 2 — The 3 Letter / Away Scenarios (`open_date_away`, `cheating_talking_letter`, `cheating_physical_letter`) — COMPLETED

Implemented via Quest `TTM_PartnerAway` (`000DC4:TT_MARAS.esp`) + Package `TTM_PartnerAwayTavernSandbox` (`000DC5:TT_MARAS.esp`):
1. **[`MatchmakingService::EvaluatePartnerHomeArrival`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp)**:
   - Added `FindNearbyTavernForRendezvous` to resolve the best nearby inn (`LocTypeInn` `0x0001CB87`) and its interior `LocationCenterMarker` (`0x0001BDF1`), prioritizing the player home's settlement $\rightarrow$ lover's settlement $\rightarrow$ home's hold $\rightarrow$ lover's hold $\rightarrow$ any valid inn.
   - Included `targetLocationFormId`, `targetMarkerFormId`, and `targetLocationName` in `maras_partner_home_arrival_event` and the 4-part parchment modal clue.
2. **[`JsonService.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/JsonService.h), [`PapyrusInterface.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/papyrus/PapyrusInterface.h), [`PapyrusInterface.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp), [`MARAS.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/MARAS.psc)**:
   - Added `MARAS.GetJsonForm(string json, string path)` so Papyrus can resolve `Location` and `ObjectReference` directly from JSON payloads.
3. **[`TTM_QuestPartnerAway.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_QuestPartnerAway.psc), [`TTM_MainController.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_MainController.psc), [`TTM_PlayerScript.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_PlayerScript.psc), [`TTM_NpcsScenes.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_NpcsScenes.psc)**:
   - `StartRendezvous` fills `Partner`, `Lover`, `TavernMarker`, and `TavernLocation` aliases, moves only `Partner` to the tavern (snapped to navmesh with `TTM_PartnerAwayTavernSandbox`), and displays Stage 10 / Objective 10 (`open_date_away`) or Stage 20 / Objective 20 (`cheating_talking_letter` / `cheating_physical_letter`).
   - When the player enters the tavern (`CheckPlayerArrival`), the quest completes and calls `TTM_NpcsScenes.SetupSceneOnLocationChange(partner, lover, scenario)`, which moves `lover` to the tavern (recording their original location for automatic restoration when the player leaves), exposes cheating (`MARAS.SetCheatingDiscovered(npc1, npc2, "exposed")`) for cheating scenarios, and runs the corresponding scene (`SetupQuickScene` or `CheatingScene` / `OpenRelationshipScene`).

---

### Task 3: Decide How `TTM_PartnerResentment` and `TTM_PartnerUpset` Are Used

In [`TTM_DialogueManagerMisc.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_DialogueManagerMisc.psc#L211-L312), you added:
- `StorageUtil.SetIntValue(akSpeaker, "TTM_PartnerResentment", 1)` (lines 211, 240, 282, 312 — when the player intimidates the partner into dropping an open/close request)
- `StorageUtil.SetIntValue(akSpeaker, "TTM_PartnerUpset", 1)` (line 304 — when scolding fails persuasion on a close request)

Currently nothing reads or clears these two `StorageUtil` keys. Depending on your design intent, we should either:
1. Hook them into **SkyrimNet / Dialogue / Affection** (e.g., emit a SkyrimNet event or use them as conditions on greeting/dialogue topics, then unset them), **or**
2. Remove them if they were temporary placeholders.

---

### Task 4: Optional SkyrimNet Event Logging for Non-State-Changing Intent Outcomes

When an intent dialogue results in `MARAS.SetPairIsOpen(...)` or `MARAS.SetCheatingDiscovered(...)`, C++ automatically fires `maras_open_relationship_changed` or `maras_affair_exposed` / `maras_affair_resolved`, which [`TTM_MainController.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_MainController.psc#L211-L278) forwards to [`TTM_ServiceSkyrimNet.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceSkyrimNet.psc).

However, when the outcome **keeps the current relationship state unchanged**—specifically:
- `maras_partner_suggest_open_promise_agree` / `persuade_success` / `intimidate_success` / `scold_agree` / `scold_persuade_success` (partner asked to open the relationship, player convinced/forced them to stay closed)
- `maras_partner_suggest_close_refuse_agree` / `persuade_success` / `intimidate_success` / `scold_agree` / `scold_persuade_success` (partner asked to close the relationship, player convinced/forced them to stay open)

No C++ status change event fires. If you want SkyrimNet's memory system to record that the partner brought this up and the player promised/convinced/intimidated them into staying closed/open, we can add a helper in [`TTM_ServiceSkyrimNet.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceSkyrimNet.psc) and call it from those branches in [`TTM_DialogueManagerMisc.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_DialogueManagerMisc.psc).
