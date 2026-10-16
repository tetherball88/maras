# Advanced Configuration and Modding Guide

This guide is for mod authors, patch creators, and advanced users who want to customize M.A.R.A.S via external data files (`JSON` / `INI`), integrate custom follower or romance mods, or hook into M.A.R.A.S via Papyrus (`MARAS.psc`) and the native C++ DLL API (`MARAS_PublicAPI.h`).

> [!NOTE]
> **Looking for in-game per-save configuration?**
> Real-time per-save settings, NPC archetype editing, relationship assignment, household management, and matchmaking controls are available directly in-game via the **Prisma UI Dashboard** (see the [User Guide](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/player/USER_GUIDE.md)). This document covers **permanent, external configuration files** and **modder integration APIs**.

---

## Related Modder Documentation

- [System Architecture & Technical Reference](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/ARCHITECTURE.md) — High-level 4-layer architecture, SKSE co-save `'MARS'` v4 binary format, and links to all 22 C++ service guides.
- [API & Prisma Bridge Reference](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/API_AND_PRISMA_BRIDGE_REFERENCE.md) — Complete reference for `MARAS_PublicAPI.h`, `MARAS.psc` native functions, SKSE ModEvents, and the Prisma UI JSON bridge.
- [Relationship Dynamics Guide](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/RELATIONSHIP_DYNAMICS_GUIDE.md) — Unified pair-keyed relationship engine (`Player <-> NPC` and `NPC <-> NPC`), cheating/affairs, and paternity secrets.
- [SkyrimNet & AI Integration](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/SKYRIMNET_AND_AI_INTEGRATION.md) — C++ prompt decorators, YAML actions/triggers, and LLM evaluation pipelines.
- [Household & Children Architecture](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md) — Multi-home management, spouse hierarchy, store income, buffs, and 6-slot child system.
- [Marriage Difficulty Configuration Guide](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/MARRIAGE_DIFFICULTY_CONFIG.md) — Reference for `marriageDifficultyConfig.json`.
- [Quest Event Tracker Documentation](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/QUEST_TRACKER.md) — Detailed reference for `Data/SKSE/Plugins/MARAS/questEvents/*.json`.

---

## 1. Summary of External Configuration Files

All external configuration files live under `Data/SKSE/Plugins/`:

| Path (relative to `Data/`) | Purpose | Loaded When |
| :--- | :--- | :--- |
| `SKSE/Plugins/MARAS/MARAS.ini` | Default plugin settings, logging level, and hotkey defaults. | Plugin startup (`kDataLoaded`) |
| `SKSE/Plugins/PackageOverrides.ini` | AI package override rules (`Whitelist`, `AllowedPlugins`, `QuestWhitelist`, `QuestPriority`) for housed tenants, pets/critters, and NPC-couple children. | `kDataLoaded` |
| `SKSE/Plugins/MARAS/spousesTypes/*.json` | Permanent NPC archetype overrides (`social`, `skill`, `temperament`). | `kDataLoaded` (applied when an NPC is first registered) |
| `SKSE/Plugins/MARAS/questEvents/*.json` | Declarative quest start, stop, and stage-change triggers that promote statuses, adjust affection, or set archetypes. | `kDataLoaded` |
| `SKSE/Plugins/MARAS/bonuses.json` | Numeric buff magnitudes, effect indices, and units for Social Class and Skill Type perks/spells. | Every save load & new game (`kPostLoadGame` / `kNewGame`) |
| `SKSE/Plugins/MARAS/marriageDifficultyConfig.json` | Weights, clamps, prestige targets, and guild alignment tables for marriage proposal calculations. | Every save load & new game |
| `SKSE/Plugins/MARAS/childrenNames.json` | Race-keyed male and female name pools for newborn and NPC-couple children. | `kDataLoaded` & save load |
| `SKSE/Plugins/MARAS/critterNames.json` | Species-keyed (`fox`, `arctic_fox`, `hare`, `mudcrab`, `skeever`) name pools for adopted child critters. | `kDataLoaded` & save load |
| `SKSE/Plugins/MARAS/npc_index_overrides.ini` | Manual `NPCIndex` inclusion/exclusion rules (`[Include]`, `[Exclude]`, `[ExcludeNames]`, `[ExcludeEditorIDs]`, `[ExcludePlugins]`) for custom or modded NPCs. | `kDataLoaded` (`NPCIndex::BuildIndex`) |

---

## 2. NPC Archetype Overrides (`spousesTypes/*.json`)

When an NPC is registered in M.A.R.A.S, [NPCTypeDeterminer](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCTypeDeterminer.md) automatically determines their **Social Class**, **Skill Type**, and **Temperament** using faction, rich-clothing, class, and skill heuristics.

You can override any or all of those three attributes permanently across playthroughs by placing `.json` files in:
```text
Data/SKSE/Plugins/MARAS/spousesTypes/
```

### 2.1 File Structure & Form Key Syntax
Each `.json` file contains a root JSON object mapping `__formData|<PluginName>|<HexFormID>` keys to override objects (keys starting with `__metaInfo` are ignored as comments/metadata):

```json
{
    "__metaInfo": "Custom archetype overrides for Dawnguard Royal Family",
    "__formData|Dawnguard.esm|0x2B74": {
        "comment": "Serana",
        "social": "rulers",
        "skill": "mage",
        "temperament": "independent"
    },
    "__formData|Dawnguard.esm|0x3BA8": {
        "comment": "Lord Harkon (social class only; skill & temperament auto-detected)",
        "social": "rulers"
    }
}
```

- **FormID Resolution**: You can supply either the NPC's base `TESNPC` FormID or their placed `Actor` reference FormID—M.A.R.A.S resolves both via `NPCIndex`. If the target plugin is not installed in the user's load order, the entry is safely skipped.
- **Partial Overrides**: You only need to include the fields you want to override (`social`, `skill`, and/or `temperament`). Any omitted field falls back to automatic detection.

### 2.2 Valid Archetype Strings (Case-Insensitive)

| Field | Allowed Values |
| :--- | :--- |
| `social` | `poverty`, `working`, `middle`, `wealthy`, `religious`, `nobles`, `rulers`, `outcast` |
| `skill` | `warrior`, `mage`, `rogue`, `craftsman`, `ranger`, `orator` |
| `temperament` | `proud`, `humble`, `jealous`, `romantic`, `independent` |
| `comment` | Any descriptive string (ignored by parser) |

> [!TIP]
> **When Overrides Apply**: JSON overrides in `spousesTypes/` are applied when an NPC is first registered into M.A.R.A.S. If an NPC is already registered in an existing save, change their archetype live in the **Prisma UI Dashboard $\rightarrow$ NPC Detail** view (or via a `questEvents` command / Papyrus call).

---

## 3. Buff Values Configuration (`bonuses.json`)

Passive player buffs granted by married spouses ([SpouseBuffService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/SpouseBuffService.md)) are configured in:
```text
Data/SKSE/Plugins/MARAS/bonuses.json
```

Because `bonuses.json` is reloaded on every save load and new game (`BonusesService::LoadFromFile`), edits to this file take effect immediately the next time you load a save.

- Each category key (normalized to lowercase, e.g., `"poverty"`, `"working"`, `"warrior"`, `"mage"`) maps to an array of bonus entries specifying `name`, `description`, `effectIndex`, `perk`, `type`, `unit`, and `value`.
- `SpouseBuffService` multiplies these base values by each spouse's **Hierarchy Rank Weight** (`Lead = 1.0`, `Second = 0.75`, `Third = 0.5`, `Fourth+ = 0.25`) and **Follower Multiplier** (`1.5x` when actively following the player) before writing the totals into the engine's `TESGlobal` variables (`0x6F`–`0x79`, `0x16B`).

---

## 4. Child & Critter Name Pools (`childrenNames.json` & `critterNames.json`)

[NameService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NameService.md) loads customizable name pools from two JSON files:

### 4.1 `Data/SKSE/Plugins/MARAS/childrenNames.json`
Contains race-keyed `male` and `female` arrays (`argonian`, `khajiit`, `orsimer`/`orc`, `altmer`, `bosmer`, `dunmer`, `redguard`, `breton`, `imperial`, `nord`):
```json
{
    "nord": {
        "male": ["Bjorn", "Torsten", "Halvar"],
        "female": ["Freja", "Astrid", "Ingrid"]
    }
}
```
Custom races are automatically matched to canonical pools by stripping `Race` / `RaceChild` / `RaceVampire` suffixes and matching lore substrings, or you can add new race keys directly.

### 4.2 `Data/SKSE/Plugins/MARAS/critterNames.json`
Contains species-keyed name arrays (`fox`, `arctic_fox`, `hare`, `mudcrab`, `skeever`) used when a household child brings home a wild critter.

---

## 5. NPC Index & AI Package Override Configuration (`INI`)

### 5.1 `Data/SKSE/Plugins/MARAS/npc_index_overrides.ini`
[NPCIndex](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/NPCIndex.md) scans all loaded plugins at startup to map Base `TESNPC` FormIDs to Placed `Actor` Reference FormIDs. You can customize which NPCs are included or excluded in `npc_index_overrides.ini` using the following sections:
- **`[Include]`** — Force-includes specific NPCs (bypassing the `Unique` / `Persistent` requirement). Accepts `Plugin.esp|0xHexID` (supporting both standard 24-bit and ESL 12-bit light plugins), `__formData|Plugin.esp|0xHexID`, direct `0xHexID`, or EditorID.
- **`[Exclude]`** — Force-excludes specific NPCs by Base or Reference FormID (`Plugin.esp|0xHexID`, `__formData|Plugin.esp|0xHexID`, `0xHexID`) or EditorID.
- **`[ExcludeNames]`** — Excludes NPCs by exact display name or `*` / `?` wildcard pattern (case-insensitive).
- **`[ExcludeEditorIDs]`** — Excludes NPCs by exact Base/Ref EditorID or `*` / `?` wildcard pattern (case-insensitive).
- **`[ExcludePlugins]`** — Excludes all NPCs originating from specific plugin files (exact plugin name or `*` / `?` wildcard pattern; bare names without an extension automatically match `.esp`, `.esm`, and `.esl`).

### 5.2 AI Package Overrides (`Data/SKSE/Plugins/PackageOverrides.ini`)
[PackageOverrideService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/PackageOverrideService.md) manages the home-sandbox and follow AI packages for housed tenants (`0x6A`), child pets/critters (`0xC0`), and dynamic non-player children (`0x376`). To permit packages from follower frameworks, dialogue expansions, or quest mods to run over the M.A.R.A.S base package, configure `[PackageOverrides]` in `Data/SKSE/Plugins/PackageOverrides.ini`:
```ini
[PackageOverrides]
Whitelist = SkyrimNet*, WIGames*
AllowedPlugins = nwsFollowerFramework.esp
; QuestPriority = 61
QuestWhitelist = TTM_ChildrenSandbox, TTM_DialoguesChildrenScenes, TTM_NpcsScenes
```
For full wildcard syntax and evaluation order details, see the [Package Override Configuration Guide](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/PACKAGE_OVERRIDE.md).

---

## 6. NPC Exclusion & Voice Keywords (SPID / KID)

`TT_MARAS.esp` defines three keywords that you can distribute via **Spell Perk Item Distributor (SPID)** or **Keyword Item Distributor (KID)** (see the included [MARAS_IgnoreNpcs_DISTR.ini](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/MARAS_IgnoreNpcs_DISTR.ini) template in the root folder):

| EditorID | FormID (`TT_MARAS.esp`) | Behavior |
| :--- | :--- | :--- |
| `TTM_IgnoreInitialDialogue` | `0xC0A` | Hides the initial courting dialogue option (*"(M) Ever think we could be something more?"*). The NPC can still be registered via quests, matchmaking, or API calls. |
| `TTM_IgnoreProposal` | `0xC0B` | Allows the NPC to be registered and courted in M.A.R.A.S, but blocks M.A.R.A.S's marriage proposal dialogue so a custom follower mod's own marriage quest can handle proposals instead. |
| `TTM_UseNpcVoiceInMisc` | `0xC20` | Instructs M.A.R.A.S to let the NPC use their own custom voice lines for post-marriage greetings/goodbyes instead of M.A.R.A.S lines when their voice type is non-vanilla. |

### Example: Integrating a Custom Follower Marriage Quest (e.g., Serana Dialogue Addon)
1. **Distribute `TTM_IgnoreProposal` via SPID** (`*_DISTR.ini`):
   ```ini
   Keyword = TTM_IgnoreProposal|Serana
   ```
2. **Add a Quest Event config** in `Data/SKSE/Plugins/MARAS/questEvents/SDA_Patch.json` (handled by [QuestEventManager](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/QuestEventManager.md)):
   ```json
   {
     "__formData|SDA.esp|0x123456": {
       "onStageChange:100": [
         "promoteToStatus:__formData|Dawnguard.esm|0x002B74:Married"
       ]
     }
   }
   ```
When the custom marriage quest reaches stage `100`, M.A.R.A.S automatically promotes the NPC to `Married` with the player, granting full spouse hierarchy, buff, affection, and household support without interfering with the follower mod's proposal quest.

---

## 7. Papyrus & Native C++ Integration Points

M.A.R.A.S supports both **Player $\leftrightarrow$ NPC** and **NPC $\leftrightarrow$ NPC** relationships across 10 statuses:
`Candidate` (`0`), `Engaged` (`1`), `Married` (`2`), `Divorced` (`3`), `Jilted` (`4`), `Lover` (`5`), `Dating` (`6`), `ExLover` (`7`), `ExDating` (`8`), and `Deceased` (`9`).

For complete function signatures and payloads, consult [API_AND_PRISMA_BRIDGE_REFERENCE.md](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/API_AND_PRISMA_BRIDGE_REFERENCE.md).

### 7.1 Key Papyrus Functions (`MARAS.psc`)
- **Pair-Keyed Relationship Transitions**:
  - `PromoteNPCToStatus(Actor npc, String statusName, Bool sendEvent = true)` / `PromoteNPCToStatusByEnum(Actor npc, Int statusEnum, Bool sendEvent = true)` — Transitions a relationship between the **Player** and `npc`. Pass `sendEvent = false` to suppress SKSE ModEvent dispatch during save migrations or batch syncs.
  - `PromoteNPCToStatusWith(Actor actorA, Actor actorB, String statusName, Bool sendEvent = true)` / `PromoteNPCToStatusWithByEnum(Actor actorA, Actor actorB, Int statusEnum, Bool sendEvent = true)` — Transitions a relationship between **any two actors** (`Player <-> NPC` or `NPC <-> NPC`). Pass `sendEvent = false` to suppress event dispatch.
  - `DemoteNPCToCandidateWith(Actor actorA, Actor actorB)` — Resets a pair's romantic relationship back to `Candidate`.
- **Open Relationships & Infidelity**:
  - `SetPairIsOpen(Actor actorA, Actor actorB, Bool isOpen)` / `GetPairIsOpen(Actor actorA, Actor actorB)` — Configures mutual open-relationship consent for a specific couple.
  - `RecordCheating(Actor cheater, Actor victim, Actor affairPartner, Bool isSecret, Int affairType)` — Records an infidelity incident (`affairType`: `0` = OneNightStand, `1` = OngoingAffair, `2` = CommittedSecret) and syncs cheating factions.
- **Custom Daily Affection Channels & Permanent Score**:
  - `SetPermanentAffection(Actor npc, Float value, Bool sendEvent = true)` — Directly sets an actor's permanent affection score (`0..100`). Pass `sendEvent = false` to prevent firing `maras_change_affection` events (e.g. when establishing baseline 100 affection on migrated spouses).
  - `AddAffection(Actor npc, Float amount, String type)` — Adds positive or negative affection to a named daily channel (e.g., `"MyModDate"`, `"CustomGift"`).
  - `SetAffectionMinMax(String type, Float minVal, Float maxVal)` — Configures the daily clamping range for your custom channel before midnight rollup into permanent affection (see [AffectionService](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/docs/modder/services/AffectionService.md)).
- **Children & Lineage**:
  - `RegisterChild(Actor child, Actor guardianA, Actor guardianB, Form houseLocation)` — Registers an adopted or spawned child with up to two neutral custodial guardians (`guardianA` and `guardianB` are strictly gender-neutral and interchangeable).
  - `RegisterChildActor(Actor childActor, Actor guardianA, Actor guardianB, Form houseLocation, Bool sendEvent = true)` — Registers a physical child actor with optional ModEvent emission suppression (`sendEvent = false`).
  - `RegisterBiologicalChild(...)` / `SetBioParents(Int childId, Actor mother, Actor father)` — Tracks genetic parentage and triggers automatic paternity secret detection when a birth results from an affair.

### 7.2 SKSE ModEvents Emitted by M.A.R.A.S
External Papyrus scripts can register via `RegisterForModEvent("<eventName>", "<HandlerName>")` to react to M.A.R.A.S state transitions:

| ModEvent Name | `strArg` | `numArg` (`Float`) | `sender` (`Form`) | When Fired |
| :--- | :--- | :--- | :--- | :--- |
| `maras_status_changed` | Status name (`"married"`, `"dating"`, etc.) | Status enum (`0..9`) | `Actor` (NPC) | When a Player $\leftrightarrow$ NPC relationship changes status. |
| `maras_pair_status_changed` | `"<HexID_A>\|<HexID_B>\|<statusName>"` | Status enum (`0..9`) | `Actor` (`actorA`) | When any pair (including NPC $\leftrightarrow$ NPC) changes relationship status. |
| `maras_hierarchy_changed` | `"promote"` or `"demote"` | Rank delta | `Actor` (Spouse) | When a player spouse's hierarchy rank changes. |
| `maras_change_affection` | New tier (`"happy"`, `"content"`, `"unhappy"`, `"estranged"`) | Affection delta | `Actor` (Partner) | When a partner crosses an affection tier threshold. |
| `maras_affair_changed` | `"<HexCheater>\|<HexVictim>\|<HexParamour>"` | Affair status | `Actor` (Cheater) | When a cheating record is created, discovered, or resolved. |
| `maras_teammate_change` | `""` | `0.0` | `None` | Summary notification when the active follower set changes. |
| `maras_teammate_added` | `""` | `0.0` | `Actor` (Follower) | Fired for each registered NPC who starts following the player. |
| `maras_teammate_removed` | `""` | `0.0` | `Actor` (Follower) | Fired for each registered NPC who stops following the player. |
| `maras_day_changed` | `""` | Current game day | `None` | Fired once at midnight each in-game day rollover. |
| `maras_dialogue_opened` | `""` | `0.0` | `Actor` (Speaker) | Fired when the player opens `Dialogue Menu` with an NPC. |
| `maras_actor_died` | `""` | `1.0` if player killer, else `0.0` | `Actor` (Victim) | Fired when a tracked adult NPC dies (`DeathEventHandler`). |
| `maras_child_died` | `""` | `1.0` if player killer, else `0.0` | `Actor` (Child) | Fired when a registered child dies. |
| `maras_child_pet_died` | `""` | `1.0` if player killer, else `0.0` | `Actor` (Animal) | Fired when a household child's pet or critter dies. |
| `maras_intimate_scene_ended` | Participant names (`"Name1, Name2"`) | Participant count | `Actor` (First NPC) | Fired when an OStim Standalone intimate thread finishes. |
| `maras_intimate_threads_idle` | `""` | `0.0` | `None` | Fired when all active OStim threads have ended. |

### 7.3 Native C++ DLL API (`MARAS_PublicAPI.h`)
SKSE C++ plugin authors can query and mutate M.A.R.A.S state directly in C++ without going through Papyrus by including [MARAS_PublicAPI.h](file:///e:/Skyrim/development/mods/M.A.R.A.S%20Dev/SKSE_Source/include/MARAS_PublicAPI.h) and calling `MARAS_API::RequestPluginAPI(MARAS_API::InterfaceVersion::V1)` after `MARAS.dll` is loaded.
