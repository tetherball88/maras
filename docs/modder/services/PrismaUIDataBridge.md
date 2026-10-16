# PrismaUIManager & PrismaUIDataBridge — Web Dashboard Lifecycle & Bidirectional Data Bridge

[PrismaUIManager.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/PrismaUIManager.cpp) and [PrismaUIDataBridge.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/ui/PrismaUIDataBridge.cpp) connect the native C++ M.A.R.A.S engine to the embedded React/TypeScript web dashboard (`MARAS/index.html` rendered via `PrismaUI.dll`).

---

## 1. View Lifecycle, Focus & Hotkey Management (`PrismaUIManager`)

### Dynamic API Attachment & Graceful Fallback
At startup, `PrismaUIManager` requests the `PrismaUI` V1 plugin interface dynamically. If `PrismaUI.dll` is not installed, M.A.R.A.S continues running all gameplay, Papyrus, and SkyrimNet systems normally without the web dashboard.

### Keyboard Hotkey Listener
The manager registers as an `RE::BSInputDeviceManager` input event sink and listens for the user's configured dashboard toggle key (`toggleUIKey`, defaulting to `Alt+B`) combined with exact modifier state verification (`Ctrl`, `Shift`, `Alt` via Win32 `GetAsyncKeyState`). When matched, it queues a game-thread task to toggle the dashboard open or closed.

### Lazy View Creation & Game Pause Control
- **On-Demand View Creation**: The `MARAS/index.html` browser view is created lazily the first time the player opens the dashboard (`Show()`), registering four JavaScript-to-C++ bridge listeners (`closeMARAS`, `log`, `refocusView`, and `marasAction`).
- **DOM Ready Handshake**: When the web view finishes loading (`OnDomReady`), the manager focuses the view, pushes the plugin version status (`window.receiveMARASStatus`), triggers `window.openMARAS()`, and dispatches a complete initial state snapshot via `PrismaUIDataBridge::SendOverviewData()`.
- **Safe Pause Counter Management**: Tracks whether M.A.R.A.S itself incremented `RE::UI::GetSingleton()->numPausesGame` (`m_pausedGame`) so hiding or destroying the view always decrements the engine's pause counter cleanly without desynchronizing other menus.

---

## 2. Outbound State Aggregation (`PrismaUIDataBridge`)

Rather than forcing the React frontend to make dozens of granular queries, `PrismaUIDataBridge` builds comprehensive, normalized JSON snapshots and pushes them to `window.*` receivers in the web view.

### Overview Snapshot (`SendOverviewData`)
Before assembling the overview payload, the bridge triggers `SyncAllMarriedCouplesChildCustody()` so any recent marriages or divorces reflect up-to-date child guardianship. It then packages:
1. **Player Relationship Counts**: Totals for married, engaged, lover, dating, divorced, jilted, and candidate NPCs.
2. **Spouse Hierarchy Top 3**: Profiles and permanent affection scores for the player's `Lead`, `Second`, and `Third` ranked spouses.
3. **Complete NPC Profile Directory**:
   - Includes the Player (`0x14`), all registered M.A.R.A.S NPCs, all loaded child actors in Skyrim (discovered directly from loaded forms without polluting the marriage candidate registry), all biological/adoptive parents in the children registry, and any third-party paramours referenced in cheating records.
   - Each profile includes resolved race, gender, source plugin, social class, skill type, temperament, current AI package name/FormID, current vs. original home names, stay-at-home flag, and (for children) life stage (`unborn`, `infant`, `child`, `training`, `adult`), ephemeral vs. physical actor status, active pet/critter details, and per-child pet/critter ban flags.
4. **Child-Centric Registry**: Every `ChildRegistryEntry` with biological parents, custodial guardians, conception/birth timestamps, life stage, and directional paternity secret state (`isSecretForBioFather`, `isSecretForPartner`, phenotype mismatch, and discovery flags).
5. **Global Relationship Graph & History**: Every player-NPC and NPC-NPC relationship pair, open-relationship consent flag, and full chronological status transition history (`fromStatus`, `toStatus`, `timestamp`, `initiatorFormId`).
6. **First-Class Cheating Records**: All active and historical affairs (`cheaterId`, `victimId`, `affairPartnerId`, incident count, secret status, affair type, and affair status).
7. **Affection & Settings Snapshot**: Permanent affection scores, days since last interaction, registered player homes, and all live configuration toggles (including `hasSkyrimNet` and `simulateNoSkyrimNet` so the UI can adapt SkyrimNet-specific controls).

### Targeted & Real-Time Event Pushes
In addition to full overview refreshes, C++ services push lightweight incremental updates to the UI whenever state changes during gameplay:
- **Live Mutations**: `OnAffectionChanged`, `OnStatusChanged`, `OnHierarchyChanged`, `OnChildUpdated`, `OnChildRemoved`, and `OnChildPromoted` (when a synthetic child ID is replaced by a spawned actor FormID).
- **Household Buff Breakdown**: `SendHouseholdBuffs` serializes the complete [SpouseBuffService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/SpouseBuffService.md) calculation breakdown (`window.receiveMARASBuffs`).
- **Matchmaking & Memory Streams**: Pushes autonomous/manual matchmaking suggestions (`window.receiveMatchmakingResults`) and LLM-generated partner memories (`window.receiveGeneratedMemories`).
- **Full World NPC Directory & Search**: Streams all indexed Skyrim NPCs (`SendAllSkyrimNPCs`) or top-50 substring matches (`ProcessSearchNPCs`) from [NPCIndex](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCIndex.md) for the dashboard's relationship assignment and matchmaking pickers.

---

## 3. Inbound Action Dispatcher (`HandleAction`)

All user interactions in the React dashboard are sent as JSON `{ "action": "...", "payload": { ... } }` messages to `OnMarasAction`. `PrismaUIDataBridge` parses the request and schedules execution on the **Skyrim main game thread** via `SKSE::GetTaskInterface()->AddTask`:

### Relationship & Hierarchy Controls
- **Relationship Assignment & Removal**: Auto-resolves any base `TESNPC` FormIDs to placed `Actor` reference FormIDs via `ActorSvc` / `NPCIndex`. If `PromoteToStatusWith` fails validation (e.g., child actor, deceased actor, or invalid state), it queries `CanFormRelationship` for the exact failure reason and fires `window.onActionRejected` back to the UI. If dual LLM memories are attached to a matchmaking assignment, it ingests them via [MatchmakingService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/MatchmakingService.md) and records NPC matchmaking cooldowns.
- **Open Relationship & Hierarchy**: Toggles pair open-relationship consent (`setPairIsOpen`) and updates spouse hierarchy ranks (`setHierarchyRank`).
- **Attribute & Affection Overrides**: Allows direct editing of an NPC's permanent affection, social class, skill type, and temperament from the NPC Detail page.

### Household, Children & Paternity Management
- **Home Relocation & Stay-At-Home**: Moves an NPC and their family unit to a selected player home (`setNPCCurrentHome`, `relocateFamilyUnit`), unassigns tenants (checking `CanRemoveTenant` family guards first), and toggles whether a housed spouse stays at the player home (`setNPCStayAtHome`).
- **Child Adoption & Spawning**:
  - Adding a child to the **player's** family dispatches a native VM call to `TTM_ServiceChildrenLogic.AdoptChildGlobal` so full Hearthfire/MARAS slot setup runs.
  - Adding a child to an **NPC-to-NPC** couple registers the child directly in C++ and relocates the family unit.
  - Triggering `spawnChild` on an abstract/synthetic biological child dispatches the `maras_spawn_synthetic_child` SKSE ModEvent to spawn their physical actor in the world.
- **Pets & Critters**: Toggles per-child or global pet/critter bans and allows dismissing a child's current critter (disabling and deleting the spawned wild critter) or pet (unlinking the follower pet while leaving the animal alive in the world).
- **Paternity Secrets**: Toggles secret visibility edges (`setPaternitySecretEdges`) and triggers LLM paternity reveal evaluations (`evaluateChildSecret`).

### World Utilities & Diagnostic Dumps
- **Teleportation**: Teleports the player to an NPC (`teleportPlayerToNpc`) or summons an NPC to the player (`teleportNpcToPlayer`) on the main game thread.
- **Children Flee**: The persistent header button sends `fleeChildrenFromCell`, which calls `ChildrenFleeService::FleeFromPlayerCell()` (see [ChildrenFleeService](ChildrenFleeService.md)). The button is disabled in the UI when the store has no child records.
- **JSON Diagnostic Dumps**: Exports complete human-readable JSON snapshots of either all 10 C++ service states (`dumpData` $\rightarrow$ `Data/SKSE/Plugins/MARAS/dumps/maras_dump_<timestamp>.json`) or the entire resolved `NPCIndex` database (`dumpNPCIndex` $\rightarrow$ `maras_npc_index_<timestamp>.json`), printing the output path to the in-game console and notifying the web UI upon completion.

