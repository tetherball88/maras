# NPCIndex — Feature & Mechanics Guide

**Implementation**: [NPCIndex.cpp](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/src/core/NPCIndex.cpp)

`NPCIndex` is the centralized, thread-safe world directory of all eligible humanoid NPCs (both adults and children) across the entire load order. It bridges the gap between Base NPC records (`TESNPC`) and placed in-world Actor references (`TESObjectREFR` / `Actor`), enforces candidate eligibility rules and user INI overrides, provides O(1) bidirectional Base/Ref FormID lookups, and powers fuzzy/proximity name resolution for UI search and AI action execution.

---

## 1. Multi-Phase World Indexing Pipeline

When `BuildIndex()` runs after game data loads, it constructs the NPC directory in four phases:

### Phase 1: Base NPC (`TESNPC`) Scan
- Iterates every `TESNPC` record in `TESDataHandler` and evaluates each base record against `IsValidNPCCandidate`.
- Temporarily keys valid unique (or explicitly INI-included) base NPCs by their Base FormID and records their display name, EditorID, race, sex, child status (`kChild` race flag), and originating plugin file.

### Phase 1.5 & 1.6: Persistent Worldspace & Interior Cell Reference Upgrade
- Scans the `persistentCell` of every `TESWorldSpace` and every interior cell in `TESDataHandler->interiorCells`.
- For each placed NPC reference that passes `IsValidNPCCandidate`:
  - **Upgrading Unique Base Forms**: Replaces the entry's temporary Base FormID key with its actual placed **Reference FormID** (`refFormID`), prioritizing persistent references (`IsPersistent()`) if multiple exist.
  - **Promoting Non-Unique Persistent NPCs**: If a non-unique NPC (such as a named or persistent town resident whose `IsUnique` flag was omitted by a mod author) has a **persistent placed reference** AND an EditorID, it is promoted into the index as a valid candidate.
- **Unplaced Base Form Pruning**: Any base `TESNPC` that was never upgraded to an actual placed `ActorCharacter`/`Reference` in the game world is pruned from both `m_entriesByRef` and `m_baseToRefMap` so M.A.R.A.S never attempts to pair or teleport unspawned template records.
- When debug logging (`logLevel <= 1`) is enabled, writes a diagnostic audit log of all indexed, upgraded, and excluded NPCs to `Data/SKSE/Plugins/MARAS/dumps/npc_index_promotions.txt`.

### Phase 2: Live Process List Sweep & Dynamic Registration
- Scans all 4 Creation Engine process list tiers (`ForEachLoadedActor`) to cache live `RE::Actor*` pointers keyed by Reference FormID (`m_loadedActors`), record each loaded actor's current cell name, and dynamically index any valid newly loaded actors.
- As the player travels through Skyrim, `RegisterLoadedActor` and `RefreshIndex` keep the live actor cache updated and evict any pointers to actors that have unloaded, died, or been deleted.

---

## 2. Authoritative NPC Candidate Filtering (`IsValidNPCCandidate`)

To prevent leveled bandits, corpses, ghosts, animals, or test clones from polluting romance and household systems, every NPC must pass a strict sequence of checks:

1. **Player & Synthetic ID Exclusion**: Rejects `0x0`, the Player (`0x7` Base / `0x14` Ref), and `0xFF` dynamically spawned references.
2. **Reference State & Leveled Spawns**:
   - Rejects deleted references (`IsDeleted`) and dead actors (`IsDead`).
   - Rejects actors spawned from leveled character lists (`ExtraDataType::kLeveledCreature`) or placed references whose base object is a `LeveledNPC`.
3. **INI `[Exclude]` FormID Check**: Immediately rejects any Base or Reference FormID listed in `[Exclude]`.
4. **Ghost, Preset & Humanoid Checks**:
   - Rejects ghosts (`IsGhost`) and Racemenu/Chargen presets (`IsPreset`).
   - Requires the `ActorTypeNPC` keyword on either the `TESNPC` base form or its `TESRace` (`IsHumanoidActor`).
5. **Display Name & EditorID Exclusions**:
   - Requires a non-empty display name that is not `"None"`.
   - Automatically excludes any NPC whose Base or Reference EditorID contains `"clone"`, `"dead"`, or `"corpse"`.
   - Checks user-configured exact and wildcard (`*`, `?`) exclusions for names (`[ExcludeNames]`), EditorIDs (`[ExcludeEditorIDs]`), and source plugins (`[ExcludePlugins]`).
6. **Uniqueness vs. Persistence Rule**:
   - Unless explicitly whitelisted in `[Include]`, an NPC must **either** have the `Unique` flag (`base->IsUnique()`) **or** have a **Persistent** placed reference with a non-empty EditorID.

---

## 3. User INI Overrides (`npc_index_overrides.ini`)

Modders and players can customize which NPCs are indexed via `Data/SKSE/Plugins/MARAS/npc_index_overrides.ini`:
- **`[Include]`**: Force-includes specific NPCs (bypassing the Unique/Persistent requirement), accepting `Plugin.esp|0xHexID` (supporting both standard 24-bit and ESL 12-bit light plugins), `__formData|Plugin|0xID`, direct `0xHexID`, or EditorID.
- **`[Exclude]`**: Force-excludes specific NPCs by FormID or EditorID.
- **`[ExcludeNames]`, `[ExcludeEditorIDs]`, `[ExcludePlugins]`**: Supports both exact strings and `*` / `?` wildcard patterns (automatically expanding plugin names without extensions to `.esp`, `.esm`, and `.esl`).

---

## 4. Bidirectional FormID Resolution & Fuzzy Name Search

- **Seamless Base <-> Ref Translation (`GetRefID` / `GetBaseID` / `GetActorFromFormId`)**:
  - External systems (such as vanilla `BGSRelationship` records or `ExtraOwnership` data) often store Base `TESNPC` FormIDs, whereas M.A.R.A.S runtime systems operate on placed Reference FormIDs. `NPCIndex` translates between Base FormIDs, Reference FormIDs, and live `RE::Actor*` pointers in O(1) time (safely guarding against synthetic `0xFF700000–0xFF7FFFFF` child FormIDs), falling back to a live process-list scan if needed.
- **4-Stage Name Resolution (`FindByName`)**:
  - Used by SkyrimNet action executors and console/UI tools when resolving an NPC name string (with optional `All`, `AdultOnly`, or `ChildOnly` filtering), returning a thread-safe `std::optional<NPCEntry>` value copy:
    1. **Exact Name Lookup**: O(1) hash lookup in `m_nameToRefMap`.
    2. **Fuzzy Levenshtein Match**: Searches all indexed names within a Levenshtein edit distance of **`<= 3`** to tolerate minor LLM spelling variations.
    3. **Deterministic Substring Match**: Matches if the search query is a substring of the indexed name, ranking prefix matches first, then shortest display name, then alphabetical order.
    4. **Live Process List Fallback**: Scans currently loaded actors in memory using exact, substring, and length-pruned Levenshtein matching.
- **Proximity-Weighted Name Disambiguation (`FindActorByNameNear`)**:
  - When multiple loaded NPCs share similar names during an in-world scene or AI action, scores candidates by **Match Quality first** (`0 = exact`, `1 = substring`, `2+ = Levenshtein distance`) and breaks ties by **3D Euclidean distance squared (`distSq`)** to the speaker.

