# `ModalManager` & `ConfirmationModal` — Architecture & Behavioral Guide

**Layer**: Bridge UI (`SKSE_Source/modules/bridge_ui`)  
**Sources**:
- [ModalManager.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/ModalManager.h)
- [ModalManager.cpp](../../../SKSE_Source/modules/bridge_ui/src/ModalManager.cpp)
- [ConfirmationModal.h](../../../SKSE_Source/modules/bridge_ui/include/maras/bridge_ui/ConfirmationModal.h)
- [ConfirmationModal.cpp](../../../SKSE_Source/modules/bridge_ui/src/ConfirmationModal.cpp)
- [UIManager.cpp](../../../SKSE_Source/modules/bridge_ui/src/UIManager.cpp)
- [Architecture Overview](../ARCHITECTURE.md)

---

## 1. Purpose & Responsibility

`ModalManager` provides M.A.R.A.S.'s standalone, game-pausing interactive dialog system (`<ModalHost />` backed by `useModalStore`), replacing native `RE::MessageBoxMenu` prompts across LLM failure retries, save-migration failure protection (`MigrationDiagnosticsCoordinator`), encounter letters, paternity/adoption prompts, and action confirmations. All C++-assembled modal titles, subtitles, messages, control labels, badges, and button captions resolve through `LocalizationService` (`localization/<lang>/hud_and_modals.json` and `catalogs.json`) at request creation time, while generic `<ModalHost />` chrome (checklist counters, Select All toggles, and keyboard navigation hints) resolves through `useLocaleStore`. `ConfirmationModal` exposes Papyrus-facing non-blocking latent confirmation prompts and pair cooldown tracking on top of `ModalManager`.

---

## 2. Standalone Modal Queue & Focus Rules

```mermaid
flowchart TD
    CALLER["App / Engine / Papyrus caller\nShowModal() or ShowRawJsonModal()"] --> ENQ["Enqueue under mutex_\nAttach current SessionEpoch"]
    ENQ --> TASK["AddCurrentEpochTask\nProcessQueueOnGameThread()"]
    TASK --> CHK{"IsModalDisplayBlocked()\n(Main/Loading/Console/etc.)?"}
    CHK -- "No" --> PUSH["SendActiveModalToUI()\nshowMARASModal"]
    PUSH --> FOCUS["UIManager::SyncViewVisibilityAndFocus()\nShowView(view, true) + RequestUIMode(view, kUIModeFlagPause)"]
    FOCUS --> RESP["Frontend modalResponse -> HandleModalResponse()"]
    RESP --> CB["Clear UI + Promote next\nIf epoch current -> invoke callback"]
```

- **Thread-Safe FIFO Queue**: `ShowModal` and `ShowRawJsonModal` can be called from any thread (including background LLM callbacks). When a modal is already active, subsequent requests enter `queue_` under `mutex_` and promote automatically as each active modal resolves.
- **View Independence & Focus Policy**: `<ModalHost />` renders outside `<DashboardLayout>`, so modals appear cleanly even while the M.A.R.A.S. dashboard itself is closed. Inside `UIManager::SyncViewVisibilityAndFocus()`, an active unblocked modal calls `ShowView(view_, true)`, raises the view, keeps Escape capture active (`SetEscapeCapture(view_, true)`), and acquires Magelight UI mode with `kUIModeFlagPause` (`RequestUIMode`) so Skyrim gameplay pauses cleanly and Escape/toggle shortcuts cannot dismiss the modal unintentionally—modals resolve exclusively through their rendered control buttons (`submitModalResponse` $\rightarrow$ `HandleModalResponse`).
- **Blocked Menus & Resend**: `ProcessEvent` (`MenuOpenCloseEvent`), `UIManager::OnUIReady()`, and Magelight's `ViewDomReady` event trigger resends (`SendActiveModalToUI`) whenever blocking menus close or the DOM becomes ready while a modal waits in queue.
- **Save Load / Revert Safety**: Every queued `ModalEntry` records `expectedEpoch`. `Revert()` cancels the active modal and drains `queue_` with `aborted = true`, emits `clearMARASModal`, and resynchronizes view focus. Late callbacks across a bumped epoch are dropped cleanly.

---

## 3. `ConfirmationModal` — Latent Papyrus Confirmations & Cooldowns

`ConfirmationModal` adapts `ModalManager` to non-blocking Papyrus latent calls and C++ callback-based confirmation flows (`ShowWithCallback` via `IModalGateway`, used by `SkyrimNetActionCoordinator`) for dialogue and intimate-action prompts (such as proposing, breaking up, opening/closing relationships, adopting, or declining with SkyrimNet roleplay explanations):

- **Cooldown Management (`CheckAndSet` / `IsOnCooldown`)**: Normalizes action keys and sorts actor `FormID` sets cleanly under `ConfirmationCooldownState::mutex`, returning `0` (`kCooldownEligible`) when eligible or `4` (`kCooldownBlocked`) when inside `SettingsData::actionsCooldown` or `actionsDeclineCooldown` within the current `SessionEpoch`.
- **Latent Result Codes (`ConfirmationModal::Show`)**:
  - `0`: Accepted (proceed with action).
  - `1`: Cancelled / Declined silently.
  - `2`: Declined with roleplay explanation (when SkyrimNet is available, lets the user explain why and emits `ttm_action_decline` directly via `ISkyrimNetGateway::EmitActionDecline`).
  - `3`: Blocked (on cooldown, invalid actors, or relationship state changed while waiting).
  - `4`: Confirmation modals disabled by settings (auto-accept).
  - `5`: UI host unavailable / not initialized (auto-accept).
