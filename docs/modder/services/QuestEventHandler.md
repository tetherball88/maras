# QuestEventHandler

**Sources**: [QuestEventHandler.h](../../../SKSE_Source/modules/engine/include/maras/engine/QuestEventHandler.h) · [QuestEventHandler.cpp](../../../SKSE_Source/modules/engine/src/QuestEventHandler.cpp) · [QuestEventConfigLoader.h](../../../SKSE_Source/modules/engine/include/maras/engine/QuestEventConfigLoader.h) · [QuestEventConfigLoader.cpp](../../../SKSE_Source/modules/engine/src/QuestEventConfigLoader.cpp) · [RelationshipUseCases.cpp](../../../SKSE_Source/modules/app/src/RelationshipUseCases.cpp)  
**Cross-References**: [RelationshipUseCases.md](RelationshipUseCases.md) · [WorldStoreAndCoSave.md](WorldStoreAndCoSave.md) · [DeceasedQueries.md](DeceasedQueries.md) · [ARCHITECTURE.md](../ARCHITECTURE.md)

## Overview

`QuestEventManager` and `QuestEventConfigLoader` (`modules/engine`) allow third-party mod patches and quest integrations to react automatically to Skyrim quest start, stop, and stage-change events without writing or recompiling custom Papyrus fragments (replaces legacy `QuestEventManager.md`).

---

## JSON Configuration Loading (`QuestEventConfigLoader`)

`QuestEventManager::LoadConfigFromFolder` reads sorted `*.json` configuration files from `Data/SKSE/Plugins/MARAS/questEvents` at `kDataLoaded`:

- **Root Containers**: Supports top-level `"__formData|Plugin.esp|0xLocalID"` quest keys or an explicit `"quests": { ... }` object. Unresolved quests (missing mods in the active load order) are gracefully skipped and counted in `LoadStatistics`.
- **Event Trigger Keys**:
  - `onStart`: Array of commands run when `TESQuestStartStopEvent` fires with `started == true`.
  - `onStop`: Array of commands run when `TESQuestStartStopEvent` fires with `started == false`.
  - `onStageChange:<stage>` (primary) or nested `"onStageChange": { "<stage>": [...] }` (optional): Arrays of commands mapped to `std::uint16_t` quest stages.
- **Command Schemas**:
  - **Legacy Colon String**: `"<commandType>:<npcSpecifier>:<argument>"` (split on the first two colons so arguments containing colons like `quest:15` stay intact).
  - **Structured Object**: `{ "type" | "action": "...", "npc": "...", "arg": ... }` or `"args": [npc, arg]`.
  - **Safe UTF-8 Argument Normalization**: Non-scalar JSON arguments (`JsonArgToString`) serialize via `arg.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)` so malformed/non-UTF-8 plugin bytes replace cleanly rather than throwing `nlohmann::json::type_error`.

---

## Event Sinks & Main-Thread Epoch Dispatch

`QuestStartStopEventSink` and `QuestStageEventSink` subscribe to Skyrim's quest event sources:

1. **Fast Pre-Filter**: Inside `ProcessEvent`, the sink checks `GetConfigForQuest(event->formID)` and verifies non-empty handlers for that specific start/stop flag or stage number before allocating or queuing any work.
2. **Epoch-Fenced Main-Thread Dispatch**: Captures `GetSessionEpoch()` and enqueues an SKSE task via `AddEpochTask(expectedEpoch, ...)`.
3. **Locked Execution**: On the main thread, the task acquires `GetPapyrusMutex()`, verifies `IsSessionEpochCurrent(expectedEpoch)` and non-deleted `RE::TESQuest*`, and executes matched commands sequentially.

---

## NPC Resolution & Supported Commands

### 3-Tier NPC Specifier Resolution (`ResolveNPC`)

| Priority | Specifier Format | Resolution Path |
| :--- | :--- | :--- |
| **1. FormKey** | Starts with `__formData|` | Resolves local FormID against plugin, then upgrades base FormID to placed `RefID` via `SpatialWorldIndex::GetRefID`. |
| **2. Quest Alias** | Alias name string | Walks `quest->aliases`, checks `RE::BGSRefAlias::GetReference()`, and extracts `RE::Actor*`. |
| **3. Name Lookup** | Actor display name | Searches loaded/indexed actors via `SpatialWorldIndex::FindActorByName` -> `FindByName` fallback. |

### Command Routing (`IEngineEventHooks` -> `RelationshipUseCases::ExecuteQuestCommand`)

| `commandType` (Case-Insensitive) | `argument` Behavior | Target Handler |
| :--- | :--- | :--- |
| `onDeath` | Ignored (kills/archives resolved NPC) | `EngineEventHooks::OnActorDeath(npcFormID, 0, false)` |
| `promoteToStatus` | Relationship status string (e.g., `dating`, `lover`, `engaged`, `married`, `divorced`, etc.), or `"deceased"` special case routed to `OnActorDeath`. | `RelationshipUseCases::PromoteToStatus` (`breakLovers = IsMainTier`) or `OnActorDeath` |
| `setAffection` | Integer permanent affection value | `RelationshipUseCases::SetPermanentAffection` |
| `changeAffection` | Signed integer affection delta | `RelationshipUseCases::ChangeAffection` |
| `addDailyAffection` | `"<float>"` (defaults type `"quest"`) or `"<type>:<float>"` | `RelationshipUseCases::AddAffection` |
| `setSocialClass` | Social class name string | `RelationshipUseCases::SetSocialClass` |
| `setSkillType` | Skill archetype name string | `RelationshipUseCases::SetSkillType` |
| `setTemperament` | Temperament name string | `RelationshipUseCases::SetTemperament` |
