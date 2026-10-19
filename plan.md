# Prisma UI Modal Migration & Expansion Plan

This plan covers:
1. **Part 1 — Migrating Existing Native Modals & Menus**: Replacing all native `RE::MessageBoxMenu`, `BGSMessage.Show()`, and `UIExtensions` (`UIListMenu`) popups across C++ and Papyrus with [`PrismaModalManager`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/PrismaModalManager.h).
2. **Part 2 — New Modal Opportunities**: High-value interactive modals enabled by the new Prisma UI modal capabilities (checklists, per-row dropdowns, checkboxes, and info boxes).

---

## Confirmed Design Decisions

1. **Papyrus `UIExtensions` (`UIListMenu`) & `BGSMessage` Migrations**: **Approved** — All Papyrus-driven native messages and `UIListMenu` popups (`SkipWeddingMsg`, `DivorceSettlementMsg`, `ChooseHomeForSpouseMsg`, `ChooseSpouseToAdoptWithMsg`, `TTM_Debug_ToggleSpouseEffect.psc`) will be migrated to Prisma UI modals, eliminating runtime reliance on `UIExtensions` and `StorageUtil` menu-paging caches.
2. **`ConfirmationModal` Decline Explanation Input**: **Confirmed** — Do **not** add an inline text input to the modal. Keep the `[Decline (Explain)]` button returning `2` to [`TTM_Actions.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Actions.psc), which invokes `SkyrimNetApi.TriggerTextInput()` so the explanation goes through SkyrimNet's native message input.
3. **First-Time Relationship Audit Scope (`ScanExistingRelationships`)**: **Confirmed** — Run the interactive checklist audit modal **only for Player-NPC** romantic `BGSRelationship` records (`Dating`, `Lover`, `Married`) on both New Games and existing saves (handling both spawned `RE::Actor` references and unspawned `RE::TESNPC` base forms). All NPC-NPC `BGSRelationship` records continue to import automatically in the background.

---

## Part 1: Migration of Existing Native Modals & Menus

### 1.1 C++ `RE::MessageBoxMenu` Modals (Direct Migrations)

#### 1. Relationship & Open-Relationship Confirmation Modal (`ConfirmationModal`)
- **Current Implementation**:
  - [`ConfirmationModal.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/ConfirmationModal.h) / [`ConfirmationModal.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/ConfirmationModal.cpp#L392-L433)
  - Exposed as latent Papyrus native [`MARAS.ShowConfirmationModal`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp#L146-L157) and called from [`TTM_Actions.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Actions.psc#L27-L193) (`ProposeRelationshipChange`, `ConsiderRelationshipChange`, `BreakupRelationship`, `SetOpenRelationshipChange`).
  - Uses `RE::MessageBoxData` + `RE::MessageBoxMenu::QueueMessage` with `[Accept]`, `[Cancel]`, and (when `MARAS::IsSkyrimNetAvailable()` is true) `[Decline (Explain)]`.
- **Migration Plan**:
  - Remove `RE::MessageBoxData` creation and `RE::IMessageBoxCallback` (`ConfirmationModal::ModalCallback`) from [`ConfirmationModal.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/ConfirmationModal.cpp#L9-L56).
  - Build a [`ModalRequest`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/PrismaModalManager.h#L79-L95) and dispatch via [`PrismaModalManager::GetSingleton().ShowModal`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/ui/PrismaModalManager.h#L106-L108):
    - **Dynamic Title, Subtitle & Tone by `ModalAction`**:
      - `Dating`, `Engaged`, `Lover`, `Married` $\rightarrow$ `ModalTone::Default` (or `ModalTone::Ai` when NPC-initiated via SkyrimNet) with titles `"Dating Proposal"`, `"Engagement Proposal"`, `"Lover Proposal"`, `"Marriage Proposal"`.
      - `ExDating`, `Jilted`, `ExLover`, `Divorced` $\rightarrow$ `ModalTone::Danger` with titles `"Breakup Request"` / `"Divorce Request"`.
      - `OpenRelationship`, `CloseRelationship` $\rightarrow$ `ModalTone::Warning` with titles `"Open Relationship Request"` / `"Close Relationship Request"`.
      - `LowAffectionDivorce` (new — see Section 2.4) $\rightarrow$ `ModalTone::Danger` with title `"Estranged Spouse Divorce"`.
    - **Buttons & SkyrimNet Integration**:
      - Button `0`: `ModalButton::Make("accept", "Accept", ModalButtonVariant::Primary)` $\rightarrow$ returns `0`.
      - Button `1`: `ModalButton::Make("cancel", "Cancel", ModalButtonVariant::Secondary)` $\rightarrow$ returns `1`.
      - Button `2` (only when `MARAS::IsSkyrimNetAvailable()` is true): `ModalButton::Make("decline_explain", "Decline (Explain)", ModalButtonVariant::Ghost)` $\rightarrow$ returns `2` to [`TTM_Actions.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Actions.psc), which calls `SkyrimNetApi.TriggerTextInput()` and sends the `maras_decline` mod event.
    - **Latent Stack Safety**:
      - Set `req.defaultCancelButtonIndex = 1`. In the `ModalCallback`, always call `vm->ReturnLatentResult(stackID, res.buttonIndex >= 0 ? res.buttonIndex : 1)` so even if a modal is aborted on save-load/revert (`res.aborted == true`), the suspended Papyrus stack is cleanly unblocked with `1` (`Cancel`).

---

#### 2. SkyrimNet Relationship Status Evaluation Retry Modal (`ShowLLMRetryModal`)
- **Current Implementation**:
  - [`SkyrimNetIntegration.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp#L2044-L2095), called from [`EvaluateRelationshipStatusChange`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp#L2159-L2174) when an HTTP/LLM error or JSON parse failure occurs.
  - Uses `LLMRetryCallback : RE::IMessageBoxCallback` and `RE::MessageBoxMenu::QueueMessage` with `[Retry]` and `[Ignore]`.
- **Migration Plan**:
  - Delete `LLMRetryCallback` and replace [`ShowLLMRetryModal`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp#L2055-L2095) with `PrismaModalManager::GetSingleton().ShowModal`:
    - `title = "LLM Evaluation Failed"`, `subtitle = evaluationType`, `tone = ModalTone::Warning`, `size = ModalSize::Md`.
    - Add `ModalElement::MakeInfoBox("error_details", "Failure Details", description, ModalTone::Warning)` to display the evaluation context and error reason.
    - Buttons: `ModalButton::Make("retry", "Retry Request", ModalButtonVariant::Primary)` (`0`) and `ModalButton::Make("ignore", "Ignore", ModalButtonVariant::Secondary)` (`1`, `defaultCancelButtonIndex = 1`).
    - Declare `ShowLLMRetryModal` in [`SkyrimNetIntegration.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/SkyrimNetIntegration.h) so other SkyrimNet LLM callers (`EvaluatePaternitySecret`, `EvaluateBioFatherStepIn`) can reuse it.

---

#### 3. Matchmaking Batch & Scheduled Scan LLM Failure Modal (`ShowInGameFailedLLMModal`)
- **Current Implementation**:
  - [`MatchmakingService.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/core/MatchmakingService.h#L106-L110) / [`MatchmakingService.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1501-L1569).
  - Called from [`HandleBatchCompletion`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L168-L180) (Tier 2 Verification Batch and Memory Generation Batch) and [`RunLLMScheduledMatchmaking`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L624-L652).
  - Uses `MatchmakingRetryCallback : RE::IMessageBoxCallback` and `RE::MessageBoxMenu::QueueMessage` with `[Retry]` and `[Cancel]`.
- **Migration Plan**:
  - Delete `MatchmakingRetryCallback` and replace [`MatchmakingService::ShowInGameFailedLLMModal`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1515-L1569) with `PrismaModalManager::GetSingleton().ShowModal`:
    - `title = title`, `subtitle = "SkyrimNet Matchmaking Pipeline"`, `tone = ModalTone::Warning`, `size = ModalSize::Md`.
    - Display the failed task descriptions inside a `ModalElement::MakeInfoBox("failed_tasks", "Failed Requests", joinedDescriptions, ModalTone::Warning)`.
    - Buttons: `ModalButton::Make("retry", "Retry Failed", ModalButtonVariant::Primary)` (`0`) and `ModalButton::Make("cancel", "Cancel", ModalButtonVariant::Secondary)` (`1`, `defaultCancelButtonIndex = 1`).

---

### 1.2 Papyrus `BGSMessage.Show()` & `UIExtensions` (`UIListMenu`) Modals

#### 4. Skip Wedding Ceremony Prompt (`TTM_Data.GetSkipWeddingMsg().Show()`)
- **Current Implementation**:
  - [`TTM_ServiceRelationships.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceRelationships.psc#L48-L61) (`MakeNpcEngaged`).
  - When `TTM_Data.GetPlayerHadWedding()` is true and `!alwaysSkipWeddings`, shows a native `BGSMessage` with 3 buttons:
    - `0` = Skip wedding and marry immediately
    - `1` = Proceed with normal wedding ceremony
    - `2` = Don't ask again (sets `StorageUtil` `"TTM_DontShowSkipWeddingMessage"` to `1`)
- **Migration Plan**:
  - Add a latent Papyrus native function `int Function ShowSkipWeddingModal(Actor fiance) global native` in [`MARAS.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/MARAS.psc) and [`PapyrusInterface.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp):
    - `title = "Wedding Ceremony"`, `subtitle = ActorSvc::GetName(fiance)`, `tone = ModalTone::Default`, `size = ModalSize::Sm`.
    - `description`: Ask whether to skip the formal Temple of Mara wedding ceremony with `{fianceName}` and marry immediately, or hold a traditional ceremony.
    - Include `ModalElement::MakeCheckbox("dont_ask_again", "Remember my choice for future engagements", false)`:
      - If checked + `"Skip Ceremony"` (`0`) $\rightarrow$ C++ sets `Settings::GetSingleton().skipWedding = true` and saves settings, returning `0`.
      - If checked + `"Hold Ceremony"` (`1`) $\rightarrow$ returns `2` so `TTM_ServiceRelationships.psc` sets `StorageUtil.SetIntValue(none, "TTM_DontShowSkipWeddingMessage", 1)`.
    - Buttons: `Marry Immediately (Skip Ceremony)` (`Primary`, index `0`) and `Hold Wedding Ceremony` (`Secondary`, index `1`, `defaultCancelButtonIndex = 1`).

---

#### 5. Divorce Settlement / Crime Bounty Popup (`TTM_Data.GetDivorceSettlementMsg().Show(fee)`)
- **Current Implementation**:
  - [`TTM_ServiceRelationships.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceRelationships.psc#L224-L256) (`AddDivorceFee`), triggered by [`TTM_QuestDivorce.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_QuestDivorce.psc#L38) or reading the divorce letter.
  - Calculates `fee` (base 100 + 30% player gold + houses + horses + quests + dungeons) and adds it to `crimeFaction.SetCrimeGold(fee)`, then shows a native `BGSMessage`.
- **Migration Plan**:
  - Add a latent Papyrus native function `int Function ShowDivorceSettlementModal(Actor exSpouse, int totalFee, int goldShare, int assetsShare, bool canPayDirectly) global native` in [`MARAS.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/MARAS.psc) and [`PapyrusInterface.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp):
    - `title = "Divorce Settlement Notice"`, `subtitle = exSpouse ? ActorSvc::GetName(exSpouse) : "Hold Court"`, `tone = ModalTone::Danger`, `size = ModalSize::Md`.
    - Show an itemized breakdown of the settlement fee in a `ModalElement::MakeInfoBox("fee_breakdown", "Settlement Breakdown", breakdownText, ModalTone::Warning)`.
    - If `canPayDirectly` (`player.GetGoldAmount() >= totalFee`), offer two buttons:
      1. `Pay Settlement Now ({totalFee} Gold)` (`Primary`, returns `0` $\rightarrow$ deducts gold directly via `player.RemoveItem(Gold001, fee)` without adding criminal bounty).
      2. `Refuse / Add to Hold Bounty` (`Danger`, returns `1` $\rightarrow$ applies `crimeFaction.SetCrimeGold(fee)`).
    - If `!canPayDirectly`, show a single button `Accept Hold Bounty ({totalFee} Gold)` (`Danger`, returns `1`).

---

#### 6. Spouse & Child Residence + Schedule Selector (`ChooseHomeForSpouseMsg`)
- **Current Implementation**:
  - [`TTM_ServicePlayerHouse.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServicePlayerHouse.psc#L45-L118), called from [`TTM_DialogueManagerSpouseManagement.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_DialogueManagerSpouseManagement.psc#L15-L53), [`TTM_DialogueManagerChildren.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_DialogueManagerChildren.psc#L59), and [`TTM_ServiceChildrenLogic.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc#L216).
  - Opens a paginated `UIListMenu` (10 homes per page via `StorageUtil` `SpouseHomeChoiceCache`) to select a `Location`, and then for spouses opens a *second* `UIListMenu` (`"Stay at Home"` vs. `"Keep Original Schedule"`).
- **Migration Plan**:
  - Add a latent Papyrus native function `Location Function ShowChooseHomeModal(Actor targetActor, bool isChild) global native` in [`MARAS.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/MARAS.psc) and [`PapyrusInterface.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp):
    - Queries `PlayerHouseService::GetSingleton().GetAllPlayerHouses()` directly in C++.
    - Combines both steps into a **single Prisma UI modal** (`ModalSize::Md`, `ModalTone::Default`):
      1. `ModalElement::MakeSelect("house_id", "Select Player Residence", houseOptions, defaultHouseId)` — lists all registered player houses (with current tenant count in the label).
      2. When `!isChild`: `ModalElement::MakeSelect("schedule_mode", "Spouse Schedule", {{"stay", "Stay at Home (Sandbox in Residence)"}, {"original", "Keep Original AI Schedule"}}, "stay")`.
    - Buttons: `Move to Residence` (`Primary`, index `0`) and `Cancel` (`Secondary`, index `1`, `defaultCancelButtonIndex = 1`).
    - When confirmed (`buttonIndex == 0`), C++ assigns the house via `PlayerHouseService` (or returns the selected `RE::BGSLocation*` and schedule choice so `ChooseHomeForSpouseMsg` calls `MoveSpouseToHouse` / `MoveChildToHouse`), completely removing `UIExtensions` and `SpouseHomeChoiceCache` from `TTM_ServicePlayerHouse.psc`.

---

#### 7. Co-Parent Spouse Selection on Child Adoption (`ChooseSpouseToAdoptWithMsg`)
- **Current Implementation**:
  - [`TTM_Utils.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Utils.psc#L160-L214), called from [`TTM_ServiceChildrenLogic.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc#L202) (`AdoptChild`) when `spousesCount > 1`.
  - Opens a paginated `UIListMenu` (`SpouseToAdoptWithChoiceCache`) to pick a co-parent spouse.
- **Migration Plan**:
  - Add a latent Papyrus native function `Actor Function ShowChooseAdoptionSpouseModal(Actor child = none) global native` in [`MARAS.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/MARAS.psc) and [`PapyrusInterface.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp):
    - Queries all married spouses (`NPCRelationshipManager::GetSingleton().GetNPCsByStatus(RelationshipStatus::Married)`).
    - Builds a `ModalRequest` (`title = "Adopt Child — Select Co-Parent"`, `subtitle = child ? ActorSvc::GetName(child) : ""`, `size = ModalSize::Md`):
      - `ModalElement::MakeSelect("spouse_id", "Co-Parent Spouse", spouseOptions, defaultSpouseId)` (including an option `"0"` for `"None (Adopt as Single Parent)"`, plus each spouse's current residence in their option label).
    - Buttons: `Confirm Co-Parent` (`Primary`, index `0`) and `Adopt as Single Parent` (`Secondary`, index `1`, `defaultCancelButtonIndex = 1`).
    - Returns the chosen `RE::Actor*` (or `nullptr` if single parent is selected), replacing the `UIListMenu` loop and `SpouseToAdoptWithChoiceCache` in [`TTM_Utils.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Utils.psc#L160-L214).

---

#### 8. Debug Status Spell Menu (`TTM_Debug_ToggleSpouseEffect.psc`) & Legacy Dead Code Cleanup
- **Current Implementation**:
  - [`TTM_Debug_ToggleSpouseEffect.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Debug_ToggleSpouseEffect.psc#L11-L36) uses `UIListMenu` to pick a status (`candidate`, `engaged`, `married`, `divorced`, `jilted`, `unregister`).
  - [`TTM_Utils.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Utils.psc#L97-L158) contains commented-out `ShowAIConfirmationMessage` (`SkyMessage.ShowArray`), unused `OpenMisclickedMenu` (`UIListMenu`), and unused `ActionConfirmationCooldownPassed`.
- **Migration Plan**:
  - Add a latent Papyrus native function `string Function ShowDebugStatusModal(Actor target) global native` in [`MARAS.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/MARAS.psc) and [`PapyrusInterface.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/papyrus/PapyrusInterface.cpp) that opens a Prisma UI modal with a `select` dropdown containing **all** MARAS statuses (`candidate`, `dating`, `lover`, `engaged`, `married`, `exdating`, `exlover`, `jilted`, `divorced`, `unregister`).
  - Update [`TTM_Debug_ToggleSpouseEffect.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Debug_ToggleSpouseEffect.psc) to call `MARAS.ShowDebugStatusModal(akTarget)`.
  - Cleanly delete dead functions `OpenMisclickedMenu`, `ActionConfirmationCooldownPassed`, and commented-out `ShowAIConfirmationMessage` from [`TTM_Utils.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Utils.psc#L97-L158).

---

## Part 2: New Prisma UI Modal Opportunities

### 2.1 First-Time Load / Player-NPC Relationship Audit Modal
- **Target Location**: [`NPCRelationshipManager::ScanExistingRelationships(bool isNewGame)`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager.cpp#L2535-L2749) (triggered from [`plugin.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/plugin.cpp#L236-L240) when `!Settings::GetSingleton().initialRelationshipScanDone`).
- **Why It's Needed**:
  - Many 3rd-party follower and quest mods bake `BGSRelationship` records (`kLover` rank 4, `CourtingAssocType`, or `SpouseAssocType`) between their NPCs and the Player directly into their `.esp` files.
  - Previously, `ScanExistingRelationships` skipped all player-NPC relationships on a new game and blindly imported all of them on an existing save, causing players to discover unexpected lovers or spouses from mods they haven't interacted with yet.
- **Confirmed Design & Behavior**:
  - **Strictly Player-NPC Only**:
    - The modal only collects and displays `BGSRelationship` records where one participant is the Player (`isPlayerForm(baseID1) || isPlayerForm(baseID2)`).
    - All NPC-NPC `BGSRelationship` records continue to import automatically in the background without appearing in the modal.
  - **Runs on Both New Games and Existing Saves**:
    - Whenever `ScanExistingRelationships` detects one or more Player-NPC romantic `BGSRelationship` records (`Dating`, `Lover`, `Married`), it collects them into a `PlayerRelationshipAuditEntry` list instead of immediately importing or discarding them.
    - If no Player-NPC romantic `BGSRelationship` records are found, no modal is shown.
  - **Handles Both Spawned Refs and Deferred/Unspawned Base NPCs**:
    - For each detected Player-NPC `BGSRelationship`, the NPC's `baseID`, `refID` (if already placed in the world), `vanillaStatus`, `targetMarasStatus`, and source plugin name (`rel->GetFile(0)->GetFilename()`) are recorded.
    - Display names are resolved via `ActorSvc::GetName(refID)` or `npcBase->GetName()` so even unspawned modded NPCs display accurately.
  - **Checklist Modal (`ModalSize::Lg`, `ModalTone::Default`)**:
    - **Header**: `"Detected Player Relationships"` / Subtitle: `"First-Time M.A.R.A.S Relationship Scan"`
    - **Body**: Explains that existing or plugin-defined romantic relationships with the Player were found, and asks the player to select which ones are genuinely active in this playthrough.
    - **Control (`ModalElement::MakeChecklist("detected_player_rels", ...)`)**:
      - Each item shows:
        - `id`: `"rel_{index}"`
        - `label`: `"{npcName}"`
        - `subtitle`: Source plugin file name (e.g., `SomeFollower.esp`)
        - `badgeText` / `badgeTone`: Detected relationship type (`"Lover"`, `"Courting"`, `"Spouse"`)
        - `checked`:
          - On a **New Game** (`isNewGame == true`): Defaults to `false` (unchecked).
          - On an **Existing Save** (`isNewGame == false`): Defaults to `true` if backed by active vanilla marriage/courting state, or `false` if only a raw `.esp` `BGSRelationship` record.
        - **Per-row inline dropdown (`selectOptions`)**: `Dating`, `Lover`, `Engaged`, `Married` (defaulting to the detected `targetMarasStatus`).
    - **Buttons**: `Import Selected` (`Primary`, index `0`) and `Skip All` (`Secondary`, index `1`, `defaultCancelButtonIndex = 1`).
  - **Callback Execution**:
    - If `buttonIndex == 0` (`Import Selected`), iterates over each entry:
      - If `res.IsChecklistItemChecked("detected_player_rels", itemId)` is `true`, reads the selected status (`res.GetChecklistSelectValue("detected_player_rels", itemId)`):
        - If the NPC is already spawned (`isRef`), calls `EnsureRegistered`, `SetOriginalRelationshipStatus`, and `ApplyImportedVanillaStatus(kPlayerRefID, npcRefID, vanillaStatus, chosenStatus)`.
        - If the NPC is not yet spawned (`!isRef`), registers it via `AddPendingRelationship(npcBaseID, PendingRelationshipData(kPlayerBaseID, kPlayerRefID, chosenStatus, vanillaStatus))` so it automatically applies when the NPC spawns.
    - Unchecked entries (or all entries if `Skip All` is clicked) are not imported into MARAS.

---

### 2.2 Scheduled & Global Matchmaking Results Review Modal
- **Target Locations**:
  - [`MatchmakingService::FinishVerificationBatch`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1671-L1695)
  - [`MatchmakingService::RunDiceRollMatchmaking`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1105-L1127)
  - [`MatchmakingService::RunGlobalMatchmaking`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1170-L1209) (when `autoApply == true`)
- **Why It's Needed**:
  - Scheduled matchmaking (both AI and Dice-Roll) currently applies all generated relationship transitions (`new`, `advance`, `breakup`, `open`, `close`) silently in the background, and in the AI pipeline `StartMemoryBatch` generates LLM memories for *all* verified pairs before any user review.
- **Proposed Modal Design**:
  - Add a setting in [`Settings.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Settings.h) (synchronized across `Settings.cpp`, `PrismaUIDataBridge.cpp`, `PapyrusInterface.cpp`, and the Prisma UI Settings tab): `matchmakingConfirmResults` (**"Review Matchmaking Results in Modal"**).
  - After candidate generation/verification (and *before* `StartMemoryBatch` so LLM memory calls are only made for approved pairs!), show a **consolidated Prisma UI Checklist Modal** (`ModalSize::Lg` / `Xl`, `ModalTone::Ai`):
    - **Header**: `"Matchmaking Suggestions"` / Subtitle: `"{N} Proposed NPC Relationship Changes"`
    - **Control (`ModalElement::MakeChecklist("matches", ...)`)**:
      - Each row represents one `MatchmakingResult`:
        - `label`: `"{actorAName} & {actorBName}"`
        - `subtitle`: Current status $\rightarrow$ Suggested action/status
        - `badgeText` / `badgeTone`: `"New"`, `"Advance"`, `"Breakup"` (`danger`), `"Cheating"` (`warning`), `"Open"` / `"Close"`
        - `description`: LLM verification reason or compatibility summary
        - `checked`: `true` by default
        - **Per-row inline dropdown (`selectOptions`)**: Allows the player to override the target status on the fly (e.g., for `"new"` / `"advance"`, choose between `Dating`, `Lover`, `Engaged`, `Married`; for `"breakup"`, choose breakup status).
    - **Buttons**: `Apply Selected` (`Primary`, `requireChecklistId = "matches"`, `minCheckedCount = 1`) and `Discard All` (`Secondary`).
  - Only the checked pairs (with any overridden status from `res.GetChecklistSelectValue`) proceed to `StartMemoryBatch` and `ApplyMatch`.

---

### 2.3 Missing SkyrimNet LLM Failure Retry Modals
- **Target Locations**:
  1. **[`EvaluatePaternitySecret`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/SkyrimNetIntegration.cpp#L2355-L2378)** (`SkyrimNetIntegration.cpp`): Currently logs a warning and silently aborts when the LLM call fails or returns unparseable JSON.
  2. **[`EvaluateBioFatherStepIn`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCRelationshipManager_Fertility.cpp#L168-L186)** (`NPCRelationshipManager_Fertility.cpp`): Immediately falls back to a dice roll when the LLM request fails, without giving the player a chance to retry the LLM request.
  3. **Manual & Global Matchmaking / Single-Pair Memory Generation** ([`RunManualMatchmaking`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1151-L1158), [`RunGlobalMatchmaking`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1186-L1193), [`RequestMemoriesAsync`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/MatchmakingService.cpp#L1371-L1386)): Currently, `HandleBatchCompletion` only shows `ShowInGameFailedLLMModal` when `batch->isScheduledScan == true`, and manual/global dashboard scans silently clear the loading spinner with empty results `{}` on failure.
- **Proposed Modal Design**:
  - Since `<PrismaModalHost>` renders at `z-[200]` above `<DashboardLayout>`, `ShowLLMRetryModal` works seamlessly both in-game and while the dashboard is open:
    - **`EvaluatePaternitySecret`**: Show `ShowLLMRetryModal("Paternity Secret Evaluation", ...)` with `[Retry]` and `[Ignore]`.
    - **`EvaluateBioFatherStepIn`**: Show a 3-button modal on failure: `[Retry LLM Request]` (`Primary`), `[Use Dice Roll Fallback]` (`Secondary`), and `[Skip]` (`Ghost`).
    - **Manual / Global Matchmaking & Memory Requests**: Show the Prisma retry/cancel modal on HTTP or JSON parse failure so the user knows why the scan failed and can retry immediately.

---

### 2.4 Wire Up `confirmDivorceDuringLowAffectionQuest` (Estranged Divorce Ultimatum Modal)
- **Target Locations**:
  - [`Settings.h`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/utils/Settings.h#L44) (`confirmDivorceDuringLowAffectionQuest`), [`ConfirmationModal.cpp`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/ConfirmationModal.cpp#L488-L503), [`TTM_QuestAffectionEstrangedDivorce.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_QuestAffectionEstrangedDivorce.psc#L11-L21), and [`TTM_Actions.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_Actions.psc#L196-L203) (`QuestDivorceResolutionActionExecute`).
- **Why It's Needed**:
  - `confirmDivorceDuringLowAffectionQuest` is persisted in `Settings`, shown in the Prisma UI Settings tab ([`ConfirmationSettings.tsx`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/settings/ConfirmationSettings.tsx#L177-L187)), and shown in MCM, **but is never actually checked or triggered** when the low-affection estranged divorce quest (`TTM_QuestAffectionEstrangedDivorce`) divorces the player at Stage 200.
- **Proposed Modal Design**:
  - Add `ModalAction::LowAffectionDivorce` (`"low_affection_divorce"`) to `ConfirmationModal` mapped to `s.confirmDivorceDuringLowAffectionQuest`.
  - Trigger `MARAS.ShowConfirmationModal("low_affection_divorce", ...)` before Stage 200 finalizes the divorce in `TTM_QuestAffectionEstrangedDivorce.psc` / `QuestDivorceResolutionActionExecute`:
    - If accepted $\rightarrow$ proceeds with Stage 200 (`PromoteNPCToStatus(spouse, "divorced")`).
    - If declined $\rightarrow$ transitions the estranged divorce quest to Stage 150 (saving the marriage with a partial affection restoration).

---

### 2.5 First-Time 3rd-Party Fertility & Child Migration Audit Modal (Optional)
- **Target Locations**:
  - [`TTM_ServiceChildrenLogic.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceChildrenLogic.psc#L1128-L1225) (`MigrateAllChildren`), [`TTM_ServiceFertilityFMR.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityFMR.psc#L386-L503), [`TTM_ServiceFertilityBF.psc`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/Scripts/Source/TTM_ServiceFertilityBF.psc#L607-L645).
- **Proposed Modal Design**:
  - When `MigrateAllChildren` detects existing 3rd-party pregnancies/children on first-time migration, present a checklist modal allowing the player to review detected pregnancies/children (`motherName`, `fatherName`, `stage`) and uncheck any stale/glitched records before they are registered in `PlayerChildrenService`.

---

### 2.6 Destructive Dashboard Action Confirmation Modals
- **Target Locations**:
  - **Remove / Unregister Relationship Pair**: [`TrackedRelationshipsSection.tsx`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/npcDetail/sections/TrackedRelationshipsSection.tsx) and [`NpcRelationshipDetails.tsx`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/npcRelations/NpcRelationshipDetails.tsx) (`removeRelationship`).
  - **Delete / Unregister Child Record**: [`ChildrenList.tsx`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/partners/ChildrenList.tsx) and [`PlayerChildDetailBody.tsx`](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/npcDetail/PlayerChildDetailBody.tsx) (`removeChild`).
- **Proposed Modal Design**:
  - Trigger a `ModalTone::Danger`, `ModalSize::Sm` confirmation modal via `useModalStore` before dispatching `removeRelationship` or `removeChild` to C++.

---

## Implementation Phasing

| Phase | Scope | Key Files |
| :--- | :--- | :--- |
| **Phase 1: Core C++ Native Modal Migrations** | Migrate `ConfirmationModal` (keeping `SkyrimNetApi.TriggerTextInput()` on button `2`), `ShowLLMRetryModal` (`SkyrimNetIntegration`), and `ShowInGameFailedLLMModal` (`MatchmakingService`) from `RE::MessageBoxMenu` to `PrismaModalManager`. | `ConfirmationModal.h/.cpp`, `SkyrimNetIntegration.h/.cpp`, `MatchmakingService.h/.cpp` |
| **Phase 2: Papyrus Native & `UIListMenu` Migrations + Low-Affection Divorce Fix** | Add latent C++ modal functions (`ShowSkipWeddingModal`, `ShowDivorceSettlementModal`, `ShowChooseHomeModal`, `ShowChooseAdoptionSpouseModal`, `ShowDebugStatusModal`) and update Papyrus scripts; wire up `confirmDivorceDuringLowAffectionQuest`; clean up dead `TTM_Utils.psc` / `UIListMenu` code. | `PapyrusInterface.h/.cpp`, `MARAS.psc`, `TTM_ServiceRelationships.psc`, `TTM_ServicePlayerHouse.psc`, `TTM_Utils.psc`, `TTM_QuestAffectionEstrangedDivorce.psc`, `TTM_Debug_ToggleSpouseEffect.psc` |
| **Phase 3: First-Time Player-NPC Relationship Audit Modal** | Add checklist + per-row status dropdown modal to `ScanExistingRelationships` on both New Games and existing saves strictly for Player-NPC `BGSRelationship` records (handling both spawned refs and deferred unspawned base NPCs). | `NPCRelationshipManager.h/.cpp`, `plugin.cpp` |
| **Phase 4: Matchmaking Results Review & Missing LLM Retry Modals** | Add interactive checklist + status override modal for scheduled/global matchmaking results (`matchmakingConfirmResults`); add LLM retry modals to `EvaluatePaternitySecret`, `EvaluateBioFatherStepIn`, and manual/global matchmaking. | `MatchmakingService.h/.cpp`, `SkyrimNetIntegration.cpp`, `NPCRelationshipManager_Fertility.cpp`, `Settings.h/.cpp`, `PrismaUIDataBridge.cpp`, `PapyrusInterface.cpp` |
| **Phase 5: Dashboard Destructive Action Confirmations** | Add confirmation modals before deleting relationship pairs or child records in the dashboard. | `TrackedRelationshipsSection.tsx`, `NpcRelationshipDetails.tsx`, `ChildrenList.tsx`, `PlayerChildDetailBody.tsx` |
