# Advanced Configuration and Modding Guide

This guide is for mod authors, patch creators, and advanced users who want to customize M.A.R.A.S via external data files (`JSON` / `INI`), integrate custom follower or romance mods, or hook into M.A.R.A.S via Papyrus (`MARAS.psc`) and the native C++ DLL API (`MARAS_PublicAPI.h`).

> [!NOTE]
> **Looking for in-game per-save configuration?**
> Real-time per-save settings, NPC archetype editing, relationship assignment, household management, and matchmaking controls are available directly in-game via the **UI Dashboard** (see the [Player Guide](../player/PLAYER_GUIDE.md)). This document covers **permanent, external configuration files** and **modder integration APIs**.

---

## Related Modder Documentation

- [System Architecture & Technical Reference](ARCHITECTURE.md) — High-level 4-layer architecture, 13-module C++ DAG, SKSE co-save `'MARS'` v7 format, and links to all 28 C++ service & module guides.
- [API & UI Bridge Reference](API_AND_UI_BRIDGE_REFERENCE.md) — Complete reference for `MARAS_PublicAPI.h`, `MARAS.psc` native functions, SKSE ModEvents, and the UI JSON bridge.
- [Relationship Dynamics Guide](RELATIONSHIP_DYNAMICS_GUIDE.md) — Unified pair-keyed relationship engine (`Player <-> NPC` and `NPC <-> NPC`), cheating/affairs, and paternity secrets.
- [SkyrimNet & AI Integration](SKYRIMNET_AND_AI_INTEGRATION.md) — C++ prompt decorators, YAML actions/triggers, and LLM evaluation pipelines.
- [Household & Children Architecture](HOUSEHOLD_AND_CHILDREN_ARCHITECTURE.md) — Multi-home management, spouse hierarchy, store income, buffs, and dual-layer child system.
- [Marriage Difficulty Configuration Guide](MARRIAGE_DIFFICULTY_CONFIG.md) — Reference for `marriageDifficultyConfig.json` (`marriage_difficulty.json`).
- [Quest Event Tracker Documentation](QUEST_TRACKER.md) — Detailed reference for `Data/SKSE/Plugins/MARAS/questEvents/*.json`.

---

## 1. Summary of External Configuration Files

All external configuration files live under `Data/SKSE/Plugins/`:

| Path (relative to `Data/`) | Purpose | Loaded When |
| :--- | :--- | :--- |
| `SKSE/Plugins/MARAS.ini` | Default plugin settings and logging level. | Plugin startup (`kDataLoaded`) & Settings save |
| `SKSE/Plugins/PackageOverrides.ini` | AI package override rules (`Whitelist`, `AllowedPlugins`, `QuestWhitelist`, `QuestPriority`) for housed tenants (`0x6A`), pets/critters (`0xC0`), non-player children (`0x376`), and traveling adult children (`0xDD3`). | `kDataLoaded` |
| `SKSE/Plugins/MARAS/spousesTypes/*.json` | Permanent NPC archetype overrides (`social`, `skill`, `temperament`). | `kDataLoaded` (applied when an NPC is first registered) |
| `SKSE/Plugins/MARAS/questEvents/*.json` | Declarative quest start, stop, and stage-change triggers (`promoteToStatus`, `onDeath`, affection, archetype commands). | `kDataLoaded` |
| `SKSE/Plugins/MARAS/encounters/*.json` & `encounters/overrides/*.json` | Home-arrival secret letter & open-date encounter modal templates (`EncounterConfigLoader`). | `kDataLoaded` & every save load / new game |
| `SKSE/Plugins/MARAS/bonuses.json` | Numeric buff magnitudes, effect indices, perk FormKeys (`0x35`–`0x42`), and units for Social Class and Skill Type buffs. | `kDataLoaded` & every save load / new game |
| `SKSE/Plugins/MARAS/marriageDifficultyConfig.json` *(or `marriage_difficulty.json`)* | Weights, clamps, prestige targets, and guild alignment tables for marriage proposal calculations. | `kDataLoaded` & every save load / new game |
| `SKSE/Plugins/MARAS/childrenNames.json` | Race-keyed male and female name pools for newborn and NPC-couple children. | `kDataLoaded` & save load |
| `SKSE/Plugins/MARAS/critterNames.json` | Species-keyed (`fox`, `arctic_fox`, `hare`, `mudcrab`, `skeever`) name pools for adopted child critters. | `kDataLoaded` & save load |
| `SKSE/Plugins/MARAS/promptStrings.json` | Localized vanilla dialogue prompt templates keyed by topic/prompt ID (`PromptStringsLoader`). | `kDataLoaded` & save load |
| `SKSE/Plugins/MARAS/childTraining.yaml` & `childTraining/*.yaml` | Extensible child training locations (`0..21`), adult class outcomes (weighted `1..5` skills, spells, perks, outfits, items, combat styles, `costMultiplier`, `affiliation:` discounts, `requiresBeastblood`), and Daedric Champion Lineage blessings. | `kDataLoaded` & every save load / new game |
| `SKSE/Plugins/MARAS/npc_index_overrides.ini` | Manual `SpatialWorldIndex` inclusion/exclusion rules (`[Include]`, `[Exclude]`, `[ExcludeNames]`, `[ExcludeEditorIDs]`, `[ExcludePlugins]`) for custom or modded NPCs. | `kDataLoaded` (`SpatialWorldIndex::BuildIndex`) |

---

## 2. NPC Archetype Overrides (`spousesTypes/*.json`)

When an NPC is registered in M.A.R.A.S, [`TraitClassifier`](services/TraitClassifier.md) (`NPCTypeDeterminer`) automatically determines their **Social Class**, **Skill Type**, and **Temperament** using faction, rich-clothing, class, and skill heuristics.

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

- **FormID Resolution**: You can supply either the NPC's base `TESNPC` FormID or their placed `Actor` reference FormID—M.A.R.A.S resolves both via `SpatialWorldIndex`. If the target plugin is not installed in the user's load order, the entry is safely skipped.
- **Partial Overrides**: You only need to include the fields you want to override (`social`, `skill`, and/or `temperament`). Any omitted field falls back to automatic detection.

### 2.2 Valid Archetype Strings (Case-Insensitive)

| Field | Allowed Values |
| :--- | :--- |
| `social` | `poverty`, `working`, `middle`, `wealthy`, `religious`, `nobles`, `rulers`, `outcast` |
| `skill` | `warrior`, `mage`, `rogue`, `craftsman`, `ranger`, `orator` |
| `temperament` | `proud`, `humble`, `jealous`, `romantic`, `independent` |
| `comment` | Any descriptive string (ignored by parser) |

> [!TIP]
> **When Overrides Apply**: JSON overrides in `spousesTypes/` are applied when an NPC is first registered into M.A.R.A.S. If an NPC is already registered in an existing save, change their archetype live in the **UI Dashboard $\rightarrow$ NPC Detail** view (or via a `questEvents` command / Papyrus call).

---

## 3. Buff Values Configuration (`bonuses.json`)

Passive player buffs granted by married spouses ([BuffReconciler](services/BuffReconciler.md)) are configured in:
```text
Data/SKSE/Plugins/MARAS/bonuses.json
```

Because `bonuses.json` is reloaded on every save load and new game (`BonusesConfigLoader::LoadFromFile`), edits to this file take effect immediately the next time you load a save.

- Each category key (normalized to lowercase, e.g., `"poverty"`, `"working"`, `"warrior"`, `"mage"`) maps to an array of bonus entries specifying `name`, `description`, `effectIndex`, `perk` (`TT_MARAS.esp` `0x35`–`0x42`), `type` (`"multiply"`, `"add"`, or `"spell"`), `unit`, and `value`.
- `BuffReconciler` evaluates `EngineSyncPlan::ComputeBuffMultipliers` directly in C++ (no global variable multipliers) and applies scaled `BGSPerk` entry-point / ability values directly onto `PlayerCharacter`:
  - **Permanent Social-Class Multipliers**: Sums $\text{ComputeAffectionMultiplier}(\text{PermAff}) \times \text{ComputeHierarchyRankMultiplier}(\text{Rank}, N_{\text{spouses}})$ across all living married spouses (`2.0x` for a solo living spouse; `1.0x / 0.5x / 0.25x / 0.1x` for Ranks `0..3+` in polygamy, scaled by `1.25 / 1.0 / 0.25 / 0.0` affection).
  - **Follower Skill-Type Multipliers**: Sums $\text{ComputeAffectionMultiplier}(\text{PermAff})$ across living married spouses currently following the player (`PollingService::GetCurrentTeammates()`).

---

## 4. Child & Critter Name Pools (`childrenNames.json` & `critterNames.json`)

[NamePoolLoader](services/NamePoolLoader.md) loads customizable name pools from two JSON files:

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

## 4.3 Child Training & Adult Progression Configuration (`childTraining.yaml` & `childTraining/*.yaml`)

[`TrainingCatalogLoader`](../../SKSE_Source/modules/engine/include/maras/engine/TrainingCatalogLoader.h) and [`ChildGrowthAndVisuals`](services/ChildGrowthAndVisuals.md) load the adolescent training catalog and Daedric Champion Lineage blessings from:
1. **Base Catalog**: [`SKSE/Plugins/MARAS/childTraining.yaml`](../../SKSE/Plugins/MARAS/childTraining.yaml) (`Data/SKSE/Plugins/MARAS/childTraining.yaml`)
2. **Modder Extension / Override Drop-Ins**: `Data/SKSE/Plugins/MARAS/childTraining/*.yaml` (loaded in alphabetical filename order after `childTraining.yaml`).

### Override vs. Extension Behavior
- If an entry in `childTraining/*.yaml` has an `id` matching an existing training location (e.g., `warrior` or `mage`), any fields provided in the override file **patch/replace** that entry in-place while preserving its index.
- If an entry in `childTraining/*.yaml` has a new `id` (e.g., `morag_tong` or `psijic_monk`), it is **appended** as a new training path and automatically appears in the UI Dashboard's Adolescent Training modal!
- **Location Images (`image`)**: Each training location specifies an `image` path (e.g., `"Data/SKSE/Plugins/MARAS/childTraining/images/College of Winterhold.webp"`). When the image file exists on disk in `Data/SKSE/Plugins/MARAS/childTraining/images/`, C++ caches it for rendering in `ChildTrainingModal.tsx`, falling back cleanly to a styled emblem placeholder if missing.
- Every form reference (`npcClass`, `combatStyle`, `outfit`, `requirements.faction`, `requirements.quest`, `affiliation.*`, `spells[]`, `perks[]`, `items[].form`) uses standard `"__formData|<PluginName>|<0xHexFormID>"` syntax (supporting both 24-bit `.esp`/`.esm` and 12-bit light `.esl` plugins) and supports inline `#` comments to document EditorIDs and form names.
- **Skill-Gated Perk Distribution**: `ChildVisualReconciler::ApplyAdultChildPerks` evaluates only the explicit `perks:` listed on the child's training location and Daedric blessing against each `RE::BGSPerk`'s native `data.level` and `perkConditions`, deduplicates multi-rank `BGSPerk::nextPerk` chains so only the highest qualified rank is active, and upgrades ranks automatically as the Player/child scales from Lv 10 to Lv 50.

### Example Extension File (`Data/SKSE/Plugins/MARAS/childTraining/MoragTong_Patch.yaml`)
```yaml
trainingLocations:
  - id: morag_tong
    name: Morag Tong
    location: Raven Rock Sanctum
    image: "Data/SKSE/Plugins/MARAS/childTraining/images/morag_tong.webp"
    description: "Sanctioned Dunmeri executioner training in Solstheim, mastering dual-blade precision, shadow magic, and honourable writ assassination."
    costMultiplier: 2.00
    skillType: rogue
    hmsWeights: { health: 0.35, magicka: 0.25, stamina: 0.40 }
    ai:
      morality: 0      # Any Crime
      minConfidence: 3 # Brave
    affiliation:
      memberQuests:
        - "__formData|Dragonborn.esm|0x018B15" # DLC2RR02 ("Served Cold")
    requirements:
      key: inMoragTong
      label: Solstheim Patronage
      quest: "__formData|Dragonborn.esm|0x018B15" # DLC2RR02 ("Served Cold")
      minQuestStage: 200
    npcClass: "__formData|Skyrim.esm|0x01317F"    # CombatAssassin
    combatStyle: "__formData|Skyrim.esm|0x1051FA" # csAssassin
    outfit: "__formData|Dragonborn.esm|0x0376F8"  # DLC2MoragTongOutfit (Morag Tong Chitin Armor Set)
    skills:                                       # Integer weights 1..5 (5=Primary..1=Utility)
      oneHanded: 5
      sneak: 5
      lightArmor: 4
      illusion: 3
      archery: 2
    spells:
      - "__formData|Skyrim.esm|0x08F3EB" # Muffle (Apprentice Silent Movement)
      - "__formData|Skyrim.esm|0x027EB6" # Invisibility (Expert Shadow Stealth)
    perks:
      - "__formData|Skyrim.esm|0x106256" # DualFlurry30
      - "__formData|Skyrim.esm|0x106258" # DualSavagery
      - "__formData|Skyrim.esm|0x058210" # Backstab
      - "__formData|Skyrim.esm|0x058213" # AssassinsBlade
      - "__formData|Skyrim.esm|0x0581FD" # QuietCasting
    items:
      - { form: "__formData|Skyrim.esm|0x013986", count: 1 } # Steel Dagger
      - { form: "__formData|Skyrim.esm|0x013985", count: 1 } # Hunting Bow

daedricBlessings:
  - id: azura
    name: "Azura (Twilight Foresight)"
    quest: "__formData|Skyrim.esm|0x028AD6" # DA01 ("The Black Star")
    hmsBonus: { health: 0, magicka: 75, stamina: 0 }
    skills:
      illusion: 15
      restoration: 15
    spells:
      - "__formData|Skyrim.esm|0x05AD5E" # Ebonyflesh
    perks:
      - "__formData|Skyrim.esm|0x0581F7" # WardAbsorb
      - "__formData|Skyrim.esm|0x0581E4" # HypnoticGaze
```

---

## 4.4 Home-Arrival Encounter Templates (`encounters/*.json` & `encounters/overrides/*.json`)

[`EncounterConfigLoader`](../../SKSE_Source/modules/engine/include/maras/engine/EncounterConfigLoader.h) loads handwritten note / home-arrival encounter modal templates used by `AgencyWorkflowCoordinator::EvaluatePartnerHomeArrival`:
1. **Base Folder**: `Data/SKSE/Plugins/MARAS/encounters/*.json` (`cheating_talking_letter.json`, `cheating_physical_letter.json`, `open_date_away.json`), sorted alphabetically.
2. **Override Subfolder**: `Data/SKSE/Plugins/MARAS/encounters/overrides/*.json` loaded afterwards; any file sharing an existing `"id"` merges/patches fields in place over that template.

### Template Schema
| Field | Description |
| :--- | :--- |
| `schemaVersion`, `id` | Schema version (`1`) and unique template key (`"cheating_talking_letter"`, `"cheating_physical_letter"`, `"open_date_away"`). |
| `modalTone`, `modalSize`, `defaultCancelButtonIndex` | Presentation style (`"default"`, `"warning"`, `"danger"`), modal width (`"md"`), and cancel button index. |
| `allowLoverNameToken` | Whether `{lover}` substitution is revealed in text (`false` for charred/burned affair notes). |
| `modalTitle`, `modalSubtitle`, `envelopeMessage` | Top header and arrival envelope lines (supports `{partner}`, `{player}`, `{location}`, `{lover}` tokens). |
| `infoBoxId`, `infoBoxTitle`, `buttonId`, `buttonLabel` | Note parchment panel ID/title and confirmation close button label rendered via [`ModalManager`](services/ModalManager.md). |
| `defaults` | Fallback `{salutation, premise, clue, signOff}` strings. |
| `variants` | Temperament-keyed overrides (`Romantic`, `Humble`, `Independent`, `Proud`, `Jealous` — unknown temperament keys are logged and skipped). |

---

## 5. NPC Index & AI Package Override Configuration (`INI`)

### 5.1 `Data/SKSE/Plugins/MARAS/npc_index_overrides.ini`
[`SpatialWorldIndex`](services/SpatialWorldIndex.md) scans all loaded plugins at startup to map Base `TESNPC` FormIDs to Placed `Actor` Reference FormIDs. You can customize which NPCs are included or excluded in `npc_index_overrides.ini` using the following sections:
- **`[Include]`** — Force-includes specific NPCs (bypassing the `Unique` / `Persistent` requirement). Accepts `Plugin.esp|0xHexID` (supporting both standard 24-bit and ESL 12-bit light plugins), `__formData|Plugin.esp|0xHexID`, direct `0xHexID`, or EditorID.
- **`[Exclude]`** — Force-excludes specific NPCs by Base or Reference FormID (`Plugin.esp|0xHexID`, `__formData|Plugin.esp|0xHexID`, `0xHexID`) or EditorID.
- **`[ExcludeNames]`** — Excludes NPCs by exact display name or `*` / `?` wildcard pattern (case-insensitive).
- **`[ExcludeEditorIDs]`** — Excludes NPCs by exact Base/Ref EditorID or `*` / `?` wildcard pattern (case-insensitive).
- **`[ExcludePlugins]`** — Excludes all NPCs originating from specific plugin files (exact plugin name or `*` / `?` wildcard pattern; bare names without an extension automatically match `.esp`, `.esm`, and `.esl`).

### 5.2 AI Package Overrides (`Data/SKSE/Plugins/PackageOverrides.ini`)
[`PackageReconciler`](services/PackageReconciler.md) manages the home-sandbox and follow/travel AI packages for housed tenants (`0x6A`), child pets/critters (`0xC0`), dynamic non-player children (`0x376`), and traveling adult children (`0xDD3`), while [`ChildrenFleeService`](services/ChildrenFleeService.md) asserts the highest-priority `0x5DE` intimate-scene flee package. To permit packages from follower frameworks, dialogue expansions, or quest mods to run over the M.A.R.A.S base package, configure `[PackageOverrides]` in `Data/SKSE/Plugins/PackageOverrides.ini`:
```ini
[PackageOverrides]
Whitelist = SkyrimNet*, WIGames*
AllowedPlugins = nwsFollowerFramework.esp
; QuestPriority = 61
QuestWhitelist = TTM_ChildrenSandbox, TTM_DialoguesChildrenScenes, TTM_NpcsScenes
```
For full wildcard syntax and evaluation order details, see the [Package Override Configuration Guide](PACKAGE_OVERRIDE.md).

---

## 6. NPC Exclusion & Voice Keywords (SPID / KID)

`TT_MARAS.esp` defines three keywords that you can distribute via **Spell Perk Item Distributor (SPID)** or **Keyword Item Distributor (KID)** (see the included [MARAS_IgnoreNpcs_DISTR.ini](../../MARAS_IgnoreNpcs_DISTR.ini) template in the root folder):

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
2. **Add a Quest Event config** in `Data/SKSE/Plugins/MARAS/questEvents/SDA_Patch.json` (handled by [`QuestEventHandler`](services/QuestEventHandler.md)):
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

M.A.R.A.S supports both **Player $\leftrightarrow$ NPC** and **NPC $\leftrightarrow$ NPC** relationships across 9 live statuses (`Candidate` (`0`), `Engaged` (`1`), `Married` (`2`), `Divorced` (`3`), `Jilted` (`4`), `Dating` (`5`), `Lover` (`6`), `ExDating` (`7`), `ExLover` (`8`), plus `Unknown` (`255`)), while deceased partners/children are archived in `DeceasedTable` (`'DCSD'`).

For complete function signatures and payloads, consult [API_AND_UI_BRIDGE_REFERENCE.md](API_AND_UI_BRIDGE_REFERENCE.md).

### 7.1 Key Papyrus Functions (`MARAS.psc`)
- **Pair-Keyed Relationship Transitions**:
  - `PromoteNPCToStatus(Actor npc, string statusType, bool sendEvent = true)` / `PromoteNPCToStatusByEnum(Actor npc, int statusEnum, bool sendEvent = true)` — Transitions a relationship between the **Player** and `npc`. Pass `sendEvent = false` to suppress SKSE ModEvent dispatch during save migrations or batch syncs.
  - `PromoteNPCToStatusWith(Actor npc, Actor target, string statusType, bool sendEvent = true)` / `PromoteNPCToStatusWithByEnum(Actor npc, Actor target, int statusEnum, bool sendEvent = true)` — Transitions a relationship between **any two actors** (`Player <-> NPC` or `NPC <-> NPC`). Pass `sendEvent = false` to suppress event dispatch.
  - `Breakup(Actor actorA, Actor actorB)` — Ends a romantic pair's active status (recording the appropriate breakup/ex status, cooldowns, and spouse cleanup).
- **Open Relationships & Infidelity**:
  - `SetPairIsOpen(Actor actorA, Actor actorB, bool isOpen)` / `GetPairIsOpen(Actor actorA, Actor actorB)` — Configures mutual open-relationship consent for a specific couple.
  - `RecordCheating(Actor cheater, Actor victim, Actor affairPartner, bool isSecret = false)` — Records an infidelity incident and syncs cheating factions.
- **Custom Daily Affection Channels & Permanent Score**:
  - `SetPermanentAffection(Actor npc, int amount, bool sendEvent = true)` — Directly sets an actor's permanent affection score (`0..100`). Pass `sendEvent = false` to prevent firing `maras_change_affection` events (e.g. when establishing baseline 100 affection on migrated spouses).
  - `AddAffection(Actor npc, float amount, string type)` — Adds positive or negative affection to a named daily channel (e.g., `"MyModDate"`, `"CustomGift"`).
  - `SetAffectionMinMax(string type, int minVal, int maxVal)` — Configures the daily clamping range for your custom channel before midnight rollup into permanent affection (see [`AffectionRules`](services/AffectionRules.md)).
- **Children & Lineage**:
  - `RegisterChildActor(Actor child, Actor guardianA, Actor guardianB = None, Location houseLocation = None, Actor bioMother = None, Actor bioFather = None, int syntheticID = 0, int stage = 2, bool sendEvent = true)` — Registers an adopted or placed physical child actor with up to two neutral custodial guardians (`guardianA` and `guardianB` are strictly gender-neutral and interchangeable) and optional genetic parents.
  - `RegisterUnbornChild(Actor mother, Actor bioFather = None, float conceptionTime = 0.0, bool evaluateStepIn = true)` / `SetBioMother(int childID, Actor mother)` / `SetBioFather(int childID, Actor father)` — Tracks unborn/synthetic lineage and triggers automatic paternity secret evaluation when a conception involves an affair.

### 7.2 SKSE ModEvents Emitted by M.A.R.A.S
External Papyrus scripts can register via `RegisterForModEvent("<eventName>", "<HandlerName>")` to react to M.A.R.A.S state transitions (see [API_AND_UI_BRIDGE_REFERENCE.md](API_AND_UI_BRIDGE_REFERENCE.md#22-skse-modevents-emitted-by-c) for detailed payload contracts):

| ModEvent Name | `strArg` / Key Payload | When Fired |
| :--- | :--- | :--- |
| `maras_status_changed` | Lowercase status (`"married"`, `"dating"`, etc.) | When a Player $\leftrightarrow$ NPC relationship changes status. |
| `maras_status_changed_full` | JSON `{initiator, acceptor, initiatorName, acceptorName, status}` | Fired alongside `maras_status_changed` with full pair FormIDs (`RelationshipUseCases`). |
| `maras_hierarchy_changed` | `"promote"` / `"demote"` | When a player spouse's hierarchy rank changes. |
| `maras_change_affection` | Lowercase tier (`"happy"`, `"content"`, `"troubled"`, `"estranged"`) | When a partner crosses an affection tier threshold. |
| `maras_open_relationship_changed` | Open toggle notification | When a couple opens or closes their relationship. |
| `maras_affair_exposed` / `maras_affair_resolved` | Exposed / resolution notification | When an ongoing affair is exposed or resolved. |
| `maras_cheating_caught` / `maras_open_relationship_walkin` | JSON payload (`IntimateThreadService`) | When a partner catches infidelity or walks in on an open-relationship scene. |
| `maras_verify_intimate_thread` / `maras_partner_intent_changed` | Thread / intent notification | Intimate scene validation or partner intent state update. |
| `maras_partner_home_arrival_event` / `maras_banter_scene_trigger` | Event JSON payload | Home-arrival encounter dispatch or ambient couple banter trigger. |
| `maras_child_event` / `maras_spawn_synthetic_child` / `maras_paternity_secret_resolved` / `maras_child_chest_event` | Child / secret JSON or string | Child lifecycle changes, synthetic spawning, paternity secret resolution, or chest introduction/reset. |
| `maras_teammate_change` / `maras_teammate_added` / `maras_teammate_removed` | `""` (`sender` on add/remove) | When the active follower/teammate set changes. |
| `maras_day_changed` | `""` (`numArg` = game day) | Daily midnight rollover. |
| `maras_actor_died` / `maras_child_died` / `maras_child_pet_died` | `""` (`numArg` `1.0` if player killer) | When a tracked adult, registered child, or household child's pet/critter dies. |

### 7.3 Native C++ DLL API (`MARAS_PublicAPI.h`)
External SKSE C++ plugin authors can query live M.A.R.A.S relationship, household, and teammate state read-only by including [MARAS_PublicAPI.h](../../SKSE_Source/modules/bridge_c_api/include/MARAS_PublicAPI.h) (or resolving the exported `extern "C"` functions via `GetModuleHandleW(L"MARAS.dll")` + `GetProcAddress`): `MARAS_GetPermanentAffection`, `MARAS_GetDaysSinceLastAffection`, `MARAS_GetAllMarried`, `MARAS_GetAllEngaged`, `MARAS_GetTemperament`, `MARAS_GetStatusHistoryCount`, `MARAS_GetStatusHistoryEntry`, `MARAS_GetTenantHouseFormID`, `MARAS_GetPlayerHouseTenants`, and `MARAS_GetCurrentTeammates`. See [API_AND_UI_BRIDGE_REFERENCE.md#1-c-public-dll-api-maras_publich](API_AND_UI_BRIDGE_REFERENCE.md#1-c-public-dll-api-maras_publich) for buffer contracts.
