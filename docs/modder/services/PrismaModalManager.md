# PrismaModalManager — Standalone & Blocking Prisma UI Modal System

[PrismaModalManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/PrismaModalManager.cpp), [useModalStore.ts](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/store/useModalStore.ts), and [PrismaModalHost.tsx](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/modals/PrismaModalHost.tsx) provide a generic, queue-driven modal popup system rendered through `PrismaUI.dll` as a replacement for native Skyrim message boxes.

---

## 1. Architectural Overview & Independent Rendering

### Single Web View, Dual Independent Roots
To keep VRAM and GPU usage minimal, M.A.R.A.S uses a single `MARAS/index.html` `PrismaView` managed by [PrismaUIManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/PrismaUIManager.cpp), while the React application (`App.tsx`) maintains two completely independent top-level UI roots:
1. **Main Dashboard (`<DashboardLayout />`)**: Driven by `useMarasStore(s => s.isOpen)`.
2. **Modal Host (`<PrismaModalHost />`)**: Driven by `useModalStore(s => s.activeModal !== null)`.

Because these roots mount and unmount independently:
- **Standalone Gameplay Modals**: When a modal is triggered during gameplay while the dashboard is closed (`!isOpen`), `<DashboardLayout />` remains unmounted (`0` dashboard DOM nodes), and only `<PrismaModalHost />` renders centered over the game with a flat dim overlay (`--overlay-bg`). C++ does not serialize or push dashboard overview payloads when opening the view solely for a modal.
- **Modals Over Open Dashboard**: If a modal is triggered while the player already has the dashboard open, `<PrismaModalHost />` renders above `<DashboardLayout />` at `--z-modal` (`50`), while dropdowns (`--z-dropdown: 60`) and tooltips (`--z-tooltip: 70`) inside the modal remain layered above it.
- **Zero Idle Cost**: When both `isOpen` and `activeModal` are inactive, `App.tsx` returns `null` and `PrismaUIManager` hides and unfocuses the view.

```mermaid
sequenceDiagram
    participant Caller as C++ Service / Latent Papyrus
    participant ModalMgr as PrismaModalManager (C++)
    participant UIMgr as PrismaUIManager (C++)
    participant Bridge as PrismaUIDataBridge (C++)
    participant Store as useModalStore (React/Zustand)
    participant Host as PrismaModalHost (React)

    Caller->>ModalMgr: ShowModal(request, callback)
    ModalMgr->>ModalMgr: Assign modalId & push to FIFO queue
    ModalMgr->>UIMgr: EnsureViewCreated() & SyncViewVisibilityAndFocus()
    UIMgr->>UIMgr: Show(view) & Focus(view, pauseGame=true, disableFocusMenu=false)
    ModalMgr->>Store: InteropCall("showMARASModal", modalJson)
    Store->>Host: Initialize controlValues & mount modal
    Note over UIMgr,Host: Game is paused; Esc, backdrop clicks & dashboard hotkey are blocked
    Host->>Store: Player interacts via Mouse or Keyboard (WASD/Arrows/Tab + Enter/Space/E)
    Store->>Bridge: sendMarasAction("modalResponse", { modalId, buttonId, buttonIndex, values })
    Bridge->>ModalMgr: HandleModalResponse(payload) on SKSE Game Thread
    alt Queue has more modals
        ModalMgr->>Store: InteropCall("showMARASModal", nextModalJson)
    else Queue is empty
        ModalMgr->>Store: InteropCall("clearMARASModal", "")
        ModalMgr->>UIMgr: SyncViewVisibilityAndFocus() (Unpause & hide or restore dashboard focus)
    end
    ModalMgr->>Caller: Invoke callback(ModalResponse) on Game Thread
```

---

## 2. Game Pause, Cursor Ownership & Keyboard Navigation

Every Prisma UI modal is strictly modal and blocking until the player selects one of its action buttons:

1. **Native PrismaUI Pause & `FocusMenu` Cursor Ownership**:
   - `PrismaUIManager::SyncViewVisibilityAndFocus()` calls `prismaUI->Focus(view, /*pauseGame=*/true, /*disableFocusMenu=*/false)`.
   - Keeping `disableFocusMenu = false` is essential: PrismaUI's `FocusMenu` is the native Scaleform menu that owns the mouse cursor and routes mouse movement and keyboard events to the web view. Passing `disableFocusMenu = true` prevents `FocusMenu` from opening and leaves the cursor frozen on screen.
2. **Input & Menu Event Guards (C++)**:
   - While `PrismaModalManager::HasActiveModal()` is `true`, `PrismaUIManager::ProcessEvent` and `PrismaUIManager::Toggle()` ignore the dashboard toggle hotkey (`Alt+B` / `toggleUIKey`) and restore focus on button press if lost.
   - If a native Skyrim menu opens or closes (`RE::MenuOpenCloseEvent` in `plugin.cpp`) while a modal is active, a game-thread task immediately re-runs `SyncViewVisibilityAndFocus()` so focus cannot be stolen.
3. **Web View Guards & Keyboard Navigation (React)**:
   - `<PrismaModalHost />` renders no close (`X`) icon, ignores backdrop clicks, and intercepts `Escape` keydown events in the capture phase (`preventDefault` + `stopPropagation`, while still allowing `Escape` to close an open `<Select>` dropdown inside the modal).
   - When a modal opens, `<PrismaModalHost />` automatically focuses the first enabled footer action button and provides full keyboard navigation:
     - **`A` / `D` or `Left` / `Right` Arrow**: Cycles horizontally between footer buttons (or across modal controls when focused inside the modal body).
     - **`W` / `S` or `Up` / `Down` Arrow**: Cycles through all interactive controls and footer buttons in the modal, automatically scrolling checklists into view.
     - **`Tab` / `Shift + Tab`**: Cycles forward/backward through all focusable controls and buttons inside the modal.
     - **`Enter` / `Space` / `E`**: Activates the currently focused button, toggles the focused checkbox, or opens the focused dropdown.

---

## 3. Hybrid Content Model: Declarative Schema & Custom Templates

The modal system supports two rendering modes controlled by `modalType`:

### 3.1 Declarative Generic Modals (`modalType = "generic"`)
Callers build a modal from composable building blocks without writing new React components:
- **Header & Framing**:
  - `title` and optional `subtitle`.
  - `tone` (`default`, `warning`, `danger`, `success`, `ai`), which selects the header badge icon and accent styling.
  - `size` (`sm`, `md`, `lg`, `xl`), scaling the modal width in `rem`.
  - `message`: Multi-paragraph body text (split on `\n`).
- **Interactive Controls (`controls`)**:
  - **InfoBox (`infobox`)**: Highlighted callout banner with a title, description, and semantic tone.
  - **Checkbox (`checkbox`)**: Single boolean toggle row with label, optional description, and default checked state. Clicking anywhere on the row toggles the checkbox.
  - **Select (`select`)**: Labeled dropdown menu using `<SelectInput />` with option labels, values, disabled states, and tooltips.
  - **Text Input (`text`)**: Labeled text field with initial value and placeholder.
  - **Interactive Checklist (`checklist`)**: Scrollable list of selectable records designed for bulk confirmation workflows (such as reviewing 3rd-party plugin relationships on initial scan or reviewing matchmaking suggestions):
    - Displays a header counter (`"X / Y Selected"`) and an optional **Select All / Deselect All** button.
    - Automatically displays a search filter input when the list exceeds 8 items.
    - Each row (`ModalChecklistRow`) supports a checkbox (`checked`, `checkboxDisabled`), primary `label`, `subtitle`, `description`, a status/rank badge (`badgeText`, `badgeTone`), and an optional **inline per-row dropdown** (`selectOptions`, `selectValue`, `selectLabel`) whose click events are isolated from toggling the row checkbox.
- **Control Buttons (`buttons`)**:
  - Configurable `id`, `label`, `variant` (`primary`/`default`, `secondary`, `outline`, `ghost`, `danger`), `disabled`, and `tooltip`.
  - **Checklist Selection Gating**: Any button can specify `requireChecklistId` and `minCheckedCount` to automatically disable itself until the player has checked at least `minCheckedCount` items in that checklist.

### 3.2 Custom React Modal Templates (`modalRegistry.ts`)
When a future feature requires a specialized layout beyond the declarative controls, a React component accepting `{ modal, values, onSubmit }` can be registered in [modalRegistry.ts](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/prisma-src/src/components/modals/modalRegistry.ts) via `registerModalTemplate(modalType, Component)`. Setting `request.modalType` and `request.customData` in C++ routes rendering to that custom component while preserving all C++ queueing, game pausing, focus locking, and response callbacks.

---

## 4. Queueing, Response Resolution & Lifecycle Edge Cases

- **FIFO Serialization & Re-Entrancy Safety**:
  - Multiple `ShowModal` calls from any thread are queued in order and displayed one at a time.
  - In `HandleModalResponse`, `PrismaModalManager` pops the completed modal from the queue and releases its internal mutex *before* invoking the caller's callback. This allows a modal callback (such as a `"Retry"` handler on a failed LLM request) to immediately enqueue a new modal without deadlocking.
- **Typed Response Extraction**:
  - `ModalResponse` provides direct access to `buttonId`, `buttonIndex`, and `aborted`, plus typed extractors for scalar controls (`GetBool`, `GetString`, `GetInt`) and checklist controls (`GetCheckedItemIds`, `GetUncheckedItemIds`, `IsItemChecked`, and `GetItemSelectValue` for reading per-row inline dropdown selections).
- **DOM Readiness Handshake**:
  - If a modal is requested before `MARAS/index.html` has ever been created (e.g., on `kPostLoadGame`), `PrismaModalManager` calls `PrismaUIManager::EnsureViewCreated()`. As soon as the view finishes mounting (`OnDomReady` / `"uiReady"`), `PrismaUIManager::OnUIReady()` dispatches `SendActiveModalToUI()` automatically.
- **Character Creation (`RaceSex Menu`) & Blocking Native Menu Deferral**:
  - On a new game (`kNewGame`), `PrismaUIManager::OnNewGameStarted` defers both modal display and the initial `ScanExistingRelationships(true)` audit scan until character creation (`RE::RaceSexMenu::MENU_NAME`) closes, ensuring the player finishes creating their character before the detected relationships audit modal is presented.
  - Whenever `RaceSex Menu`, `Loading Menu`, or `Main Menu` is open (`PrismaUIManager::IsModalDisplayBlocked()`), `PrismaModalManager` holds queued modals without showing or focusing the Prisma UI view over the native menu, and automatically dispatches and focuses the active modal on the game thread as soon as those menus close.
- **Missing PrismaUI & Save Revert Safety**:
  - If `PrismaUI.dll` is not installed, `ProcessQueueOnGameThread()` logs an error and resolves queued callbacks on the game thread with `aborted = true` and `buttonIndex = defaultCancelButtonIndex`.
  - `PrismaModalManager::Revert()` is invoked during `Serialization::RevertCallback` and `kPreLoadGame` to clear any pending modals when switching saves.

---

## 5. Built-In Modal Workflows Across Layers

1. **C++ Gameplay & AI Modals**:
   - **High-Ranking Spouse Breakup/Divorce Confirmation** (`ConfirmationModal.cpp`): Triggered when transitioning a Rank 1–3 spouse to `Divorced` or `Jilted` from outside the Prisma UI dashboard.
   - **Estranged Divorce Ultimatum Confirmation** (`PapyrusInterface.cpp`): Triggered before `TTM_LowAffectionQuest` kicks off when `confirmDivorceDuringLowAffectionQuest` is enabled, allowing the player to immediately grant a divorce or launch the estranged quest.
   - **SkyrimNet LLM Retry Modals** (`ConfirmationModal::ShowLLMRetryModal` / `MARAS.ShowInGameFailedLLMModal`): Triggered when any background SkyrimNet LLM request fails (single-NPC or global matchmaking, match memory generation, paternity secret evaluation, fertility discovery, hierarchy assignment, or NPC-NPC reaction evaluation).
   - **Scheduled & Global Matchmaking Review Modal** (`SkyrimNetMatchmakingService.cpp`): Displays a checklist of proposed pairings/updates with per-row target status selectors when `reviewScheduledMatchmakingResults` is enabled or a non-auto-applied global scan finishes while the dashboard is closed.
2. **Latent Papyrus Modals (`PapyrusInterface.cpp`)**:
   - `MARAS.ShowSkipWeddingModal` (`TTM_WeddingQuestScript.psc`)
   - `MARAS.ShowDivorceSettlementModal` (`TTM_DivorcePaper.psc`)
   - `MARAS.ShowChooseHomeModal` (`TTM_ServiceSpouseAssets.psc`)
   - `MARAS.ShowChooseAdoptionSpouseModal` (`TTM_ServiceChildrenLogic.psc`)
   - `MARAS.ShowDebugStatusModal` (`TTM_Debug_ToggleSpouseEffect.psc`)
   - `MARAS.ShowFirstLoadRelationshipAuditModal` (`TTM_DataService.psc`)
   - `MARAS.ShowChildMigrationAuditModal` (`TTM_ServiceChildrenLogic.psc`)
3. **Frontend Dashboard Confirmation Modals (`useModalStore.ts`)**:
   - `useModalStore.showModal(modal, onResolve?)` supports an optional local `onResolve` callback for frontend-initiated modals that resolve directly in React without a C++ `PrismaModalManager` queue round-trip.
   - `confirmRemoveRelationship` and `confirmRemoveChild` present compact (`size: 'sm'`, `tone: 'danger'`) confirmation modals before executing `removeRelationship` or `removeChild` from any dashboard list, partner drawer, or child profile card.


