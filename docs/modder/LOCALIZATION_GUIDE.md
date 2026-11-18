# M.A.R.A.S — Localization & Translator Guide

M.A.R.A.S is designed to be **100% localizable without editing or overriding `TT_MARAS.esp`** and without binary `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS` tables. Users playing Skyrim in Russian, German, Japanese, Simplified Chinese, French, Polish, or any other language never see `LOOKUP FAILED!` strings when a translation pack is missing or partial—untranslated entries fall back seamlessly to English key-by-key.

---

## 1. Two-Part Translation Architecture

A complete M.A.R.A.S translation mod (shipped as a standalone FOMOD/MO2/Vortex archive with zero `.esp` plugins) deploys into two folders under `Data/SKSE/Plugins/` (and is authored inside `localization/packs/<lang>/` in this repository):

| Layer | Installed Path in `Data/` | Repo Source Path | Covers | Required Mod Dependency |
| :--- | :--- | :--- | :--- | :--- |
| **1. MARAS Localization Pack** | `SKSE/Plugins/MARAS/localization/<lang>/*.json` | `SKSE/Plugins/MARAS/localization/en/` (base `en`), `localization/packs/<lang>/` (output packs) | HUD notifications, Papyrus messages, standalone confirmation/choice modals, spouse buff names/descriptions, adolescent training paths, Daedric blessings, encounter letters, optional newborn/critter names, and the entire Magelight UI Dashboard & Settings. | Built natively into `MARAS.dll` |
| **2. Dynamic String Distributor (DSD)** | `SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json` | `localization/packs/<lang>/dsd/TT_MARAS.esp/<lang>.json` | `TT_MARAS.esp` player/NPC dialogue topics & responses (`DIAL` / `INFO`), quests (`QUST`), courier letters & scrolls (`BOOK`), spells (`SPEL`), magic effects (`MGEF`), perks (`PERK`), and engine messages (`MESG`). | [Dynamic String Distributor (DSD)](https://www.nexusmods.com/skyrimspecialedition/mods/107676) |

**Official Release Packaging & Dev Separation (English Main Archive vs. Translations FOMOD)**: Because Dynamic String Distributor unconditionally loads every `.json` file inside `Data/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/`, keeping non-English packs and DSD JSONs under `localization/packs/<lang>/` ensures development mod setups stay cleanly in English. Official GitHub Actions releases keep `MARAS_<version>.zip` English-only (`SKSE/Plugins/MARAS/localization/en/` only) and bundle all non-English packs from `localization/packs/` into a separate `SelectExactlyOne` FOMOD archive (`MARAS_Translations_<version>.zip`) that maps each chosen pack into `Data/SKSE/Plugins/`.

> [!NOTE]
> **Why SkyrimNet AI Prompts Are Not Translated**: Files under `SKSE/Plugins/SkyrimNet/external/tetherball88.maras/` and `SKSE/Plugins/MARAS/promptStrings.json` remain in canonical English intentionally. Large Language Models reason accurately over English context blocks while generating spoken NPC dialogue in whatever output language you configure inside SkyrimNet.

---

## 2. Pack Structure (Installed `Data/` vs. Repository `localization/packs/`)

Base English files live in [`SKSE/Plugins/MARAS/localization/en/`](../../SKSE/Plugins/MARAS/localization/en/) and serve as the authoritative reference schema, while non-English FOMOD output packs live in [`localization/packs/<lang>/`](../../localization/packs/) in the repository and install to `Data/SKSE/Plugins/`:

```text
Data/SKSE/Plugins/MARAS/localization/
├── en/                       # Built-in base fallback (always loaded first)
│   ├── _meta.json            # Language code, display names, optional fontFamily
│   ├── hud_and_modals.json   # HUD notifications, MCM stub, & standalone modals
│   ├── catalogs.json         # Buffs, 22 child training paths, blessings, encounter notes
│   ├── ui_dashboard.json     # Dashboard tabs, overview, household, NPC detail, matchmaking
│   ├── ui_settings.json      # All 10 Settings sub-pages
│   └── names.json            # Optional race child names & species critter names
└── <lang>/                   # Your translation pack (e.g. ru/, de/, ja/, zh-CN/, fr/, pl/)
    ├── _meta.json
    ├── hud_and_modals.json
    ├── catalogs.json
    ├── ui_dashboard.json
    ├── ui_settings.json
    └── names.json            # Optional — omit to keep lore-friendly English/Tamrielic names
```

### 2.1 `_meta.json` Schema & Custom Fonts
Every `localization/<lang>/` folder **must** contain `_meta.json` so `LocalizationService` discovers it and lists it in **Dashboard $\rightarrow$ Settings $\rightarrow$ General $\rightarrow$ Language**:

```json
{
  "code": "ru",
  "name": "Russian",
  "nativeName": "Русский",
  "skyrimLanguages": ["RUSSIAN"],
  "fontFamily": ""
}
```

- **`code`**: Folder name and BCP-47/ISO code (`de`, `fr`, `es`, `it`, `ru`, `pl`, `uk`, `cs`, `tr`, `pt-BR`, `ja`, `ko`, `zh-CN`, `zh-TW`, etc.).
- **`name`** / **`nativeName`**: Shown in the Dashboard Language selector as `Русский (Russian)`.
- **`fontFamily`** *(optional)*: Overrides `--font-ui` when this language is active. Leave `""` to use the built-in multi-script font stack (`'MARAS', 'Bahnschrift', 'Segoe UI', 'Yu Gothic UI', 'Microsoft YaHei UI', 'Malgun Gothic', sans-serif`), which automatically covers Latin, Cyrillic, Japanese, Simplified/Traditional Chinese, and Korean glyphs on Windows.

### 2.2 Language Selection & Auto-Detection (`MARAS.ini`)
Language selection is global across saves and stored in `Data/SKSE/Plugins/MARAS.ini`:
```ini
[Localization]
Language = auto
```
When `Language = auto` (default), M.A.R.A.S reads `sLanguage:General` from `Skyrim.ini` (`ENGLISH` $\rightarrow$ `en`, `GERMAN` $\rightarrow$ `de`, `FRENCH` $\rightarrow$ `fr`, `SPANISH` $\rightarrow$ `es`, `ITALIAN` $\rightarrow$ `it`, `RUSSIAN` $\rightarrow$ `ru`, `POLISH` $\rightarrow$ `pl`, `JAPANESE` $\rightarrow$ `ja`, `CHINESE` $\rightarrow$ `zh-CN`, `TRADITIONAL CHINESE` $\rightarrow$ `zh-TW`, `KOREAN` $\rightarrow$ `ko`, `CZECH` $\rightarrow$ `cs`, `PORTUGUESE`/`BRAZILIAN` $\rightarrow$ `pt-BR`, `UKRAINIAN` $\rightarrow$ `uk`, `TURKISH` $\rightarrow$ `tr`). If no matching folder exists, M.A.R.A.S falls back cleanly to `en`.

Players and translators can override the language at any time in **Dashboard $\rightarrow$ Settings $\rightarrow$ General $\rightarrow$ Language**, and click **Reload Localization** to hot-reload edited `.json` files live in-game without restarting Skyrim!

---

## 3. Placeholders & Pluralization Rules

### 3.1 Named Placeholders (`{token}`)
Dynamic values always use named `{token}` placeholders so translators can reorder words freely:
- **English**: `"{child} has been sent to {location} to train as a {className}."`
- **Japanese**: `"{child}は{className}の修行のため、{location}へ旅立ちました。"`

**Safety Guarantees**:
- Never rename placeholder identifiers inside `{...}` (keep `{actor}`, `{spouse}`, `{child}`, `{count}`, etc. in exact ASCII spelling).
- Runtime substitutions for `{token}` and `<Alias=...>` are always injected in **dictionary / Nominative form** without grammatical case inflection—in case-inflected languages, place actor tokens in grammatical subject position or precede location/object tokens with an inflected generic classifier noun + quotes or parentheses (e.g., `at the inn "<Alias=TavernLocation>"`).
- Literal braces can be escaped as `{{` and `}}`.
- Malformed braces or missing tokens **never crash** C++ or React—unknown tokens render verbatim and log a startup validation warning in `MARAS.log`.

### 3.2 CLDR Plural Suffixes (`_one`, `_few`, `_many`, `_other`)
Count-sensitive strings use CLDR plural suffixes on the key stem (`<stem>_zero`, `<stem>_one`, `<stem>_two`, `<stem>_few`, `<stem>_many`, `<stem>_other`). `{count}` is bound automatically.

- **English (`en`)** uses `_one` and `_other`:
  ```json
  "house_option_one": "{house} ({count} resident)",
  "house_option_other": "{house} ({count} residents)"
  ```
- **Russian / Ukrainian / Polish (`ru`, `uk`, `pl`)** can add `_few` and `_many` alongside `_one` and `_other`:
  ```json
  "house_option_one": "{house} ({count} житель)",
  "house_option_few": "{house} ({count} жителя)",
  "house_option_many": "{house} ({count} жителей)",
  "house_option_other": "{house} ({count} жителей)"
  ```
- **Japanese / Chinese / Korean (`ja`, `zh-CN`, `zh-TW`, `ko`)** only need `_other` (`SelectPluralCategory` always selects `other`):
  ```json
  "house_option_other": "{house}（居住者 {count} 名）"
  ```

---

## 4. Translator Workflow & Sync CLI (`localization/scripts/`)

All helper scripts in [`localization/scripts/`](../../localization/scripts/) use **only the Python 3.10+ standard library** (zero `pip install` packages required).

### 4.1 Starting or Updating Any Language Pack (Unified JSON + DSD Flow)
`sync_localization.py` audits and synchronizes **both** `localization/packs/<lang>/*.json` **and** `localization/packs/<lang>/dsd/TT_MARAS.esp/<lang>.json` in one command:

1. Create `localization/packs/de/_meta.json`:
   ```json
   {
     "code": "de",
     "name": "German",
     "nativeName": "Deutsch"
   }
   ```
2. **Generate `_todo.json` automatically**:
   ```powershell
   # Audits both JSON + DSD and automatically writes localization/packs/de/_todo.json
   # (preserves any in-progress translations already typed inside _todo.json!)
   python localization/scripts/sync_localization.py --lang de

   # Optional: when updating across M.A.R.A.S versions, pass --prev-en to also flag changed English JSON strings
   # (changed TT_MARAS.esp DSD strings are detected automatically via each DSD entry's "original" field!)
   python localization/scripts/sync_localization.py --lang de --prev-en C:/old_maras/localization/en
   ```
   *(Pass `--check` for a read-only status check without writing `_todo.json`, `--no-dsd` to skip DSD, or `--dsd-only` for DSD alone.)*
3. **Translate inside `localization/packs/de/_todo.json`** (in small batches or all at once):
   Fill in `"translation"` for any entries under `"files"` (MARAS JSON packs) and/or `"dsd"` (`TT_MARAS.esp` records).
4. **Apply translated entries**:
   ```powershell
   python localization/scripts/sync_localization.py --lang de --apply-todo --prune-obsolete
   ```
   - Merges completed `"files"` entries into `localization/packs/de/*.json` (preserving `en/` key order).
   - Merges completed `"dsd"` entries into `localization/packs/de/dsd/TT_MARAS.esp/de.json`.
   - Automatically removes applied items from `_todo.json` (and deletes `_todo.json` once 0 pending items remain).
5. **Test live in-game & verify before publishing**:
   Copy the built pack (or install the generated FOMOD/zip into a separate MO2 profile), open the M.A.R.A.S Dashboard (`Alt+B`), select **Deutsch (German)** in **Settings $\rightarrow$ General**, and click **Reload Localization** to preview JSON edits without restarting Skyrim. Run a strict final check:
   ```powershell
   python localization/scripts/sync_localization.py --lang de --strict
   ```
   To preview or bundle all completed non-English packs into the official `SelectExactlyOne` FOMOD archive (`MARAS_Translations_<version>.zip`)—mirroring what `.github/workflows/main-release.yml` and `translations-release.yml` produce—run:
   ```powershell
   python localization/scripts/package_translations_fomod.py --version v2.6.0 --output dist/MARAS_Translations_v2.6.0.zip
   ```

---

## 5. `TT_MARAS.esp` Dynamic String Distributor (DSD) Details

`TT_MARAS.esp` keeps inline English strings (`Small` ESL flag only). In the repository, DSD translations live at `localization/packs/<lang>/dsd/TT_MARAS.esp/<lang>.json`, and the FOMOD installer deploys them to:
```text
Data/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json
```
You can either manage `<lang>.json` directly through `sync_localization.py` (`"dsd"` section in `_todo.json`) or copy [`localization/scripts/TT_MARAS_dsd_template.json`](../../localization/scripts/TT_MARAS_dsd_template.json) directly:
```json
{
  "form_id": "000162|TT_MARAS.esp",
  "editor_id": "TTM_DivorceLetter",
  "type": "BOOK FULL",
  "original": "Divorce Settlement",
  "string": "Scheidungsvergleich"
}
```
> [!IMPORTANT]
> - Preserve Creation Kit text-replacement tags such as `<Alias=Spouse>`, `<Alias=Player>`, `<Alias.Pronoun=Spouse>`, `<Alias.PronounObj=Spouse>`, `<mag>`, `<dur>`, and `%.0f` inside DSD `"string"` values (keep both `<mag>` tags in `0000D0` `TTM_LoversRadianceEffect`).
> - **`INFO NAM1` vs. `INFO RNAM` indexing**: Spoken NPC lines (`INFO NAM1`) **must** use 1-based `ResponseNumber` (`"index": 1, 2...`, never `0`), because `index: 0` is reserved for player prompt overrides (`INFO RNAM`) in DSD.
> - **Magic Effect descriptions**: DSD requires `"type": "MGEF DNAM"` (not `"MGEF DESC"`).
> - **`$DragonFont` runes (`00004D|TT_MARAS.esp` `BOOK DESC`)**: Uses Skyrim's uppercase-ASCII-only Dragon rune font—keep `00004D` `BOOK DESC` in its original uppercase English ASCII text verbatim so runes render instead of missing-glyph boxes, and translate `00004D` `BOOK FULL` + `00004C` (*Scroll of Alessia's Grace (Translated)*) instead.
> - **Skyrim Scaleform-safe punctuation (`DSD`, `names.json`, `hud.*`/`pet.*`/`common.*`/`mcm.*`)**: Never leave typographic Unicode punctuation (`—`, `–`, `…`, `«»`, `“”`, `’`, `ʼ`, NBSP, zero-width spaces) in Skyrim-rendered text—it renders as `□` boxes in `.swf` fonts. Use plain ASCII `-`, `...`, `"`, and `'` (or pass `--sanitize-existing` to normalize existing packs).
> (`sync_localization.py` automatically enforces all of these rules!)

- **For M.A.R.A.S Developers**: Whenever records in `.spriggit/TT_MARAS.esp/` are added or edited, regenerate the DSD template via:
  ```powershell
  python localization/scripts/export_dsd_template.py
  ```

---

## 6. How `catalogs.json` Works with Gameplay YAML/JSON Files & Add-On Mods

Unlike hardcoded C++ or React strings, M.A.R.A.S's gameplay data catalogs—**Spouse Bonuses** ([`bonuses.json`](../../SKSE/Plugins/MARAS/bonuses.json)), **Adolescent Child Training & Daedric Blessings** ([`childTraining.yaml`](../../SKSE/Plugins/MARAS/childTraining.yaml) + `childTraining/*.yaml`), and **Home-Arrival Encounter Notes** ([`encounters/*.json`](../../SKSE/Plugins/MARAS/encounters/) + `encounters/overrides/*.json`)—combine gameplay numbers/FormIDs with player-facing text.

[`catalogs.json`](../../SKSE/Plugins/MARAS/localization/en/catalogs.json) decouples **translation** from **gameplay balance/mechanics** so translators never overwrite gameplay YAML/JSON files and gameplay modders never overwrite full translation packs.

### 6.1 Mapping Between Gameplay Files and `catalogs.json` Keys

| Gameplay Source File(s) | Entry Identifier | Localization Keys in `catalogs.json` (or `zz_<mod>.json`) | Not Localized (Keep in Gameplay File) |
| :--- | :--- | :--- | :--- |
| `SKSE/Plugins/MARAS/bonuses.json` | `<category>` (e.g. `warrior`, `nobles`) + array index (`0`) | `bonuses.<category>.<effectIndex>.name`<br>`bonuses.<category>.<effectIndex>.description` *(renders immediately before `15%` inline — do not end with a period)* | `perk`, `type`, `unit`, `value` |
| `SKSE/Plugins/MARAS/childTraining.yaml` & `childTraining/*.yaml` | `trainingLocations[].id` (e.g. `mage`, `morag_tong`) | `training.<id>.name`<br>`training.<id>.location`<br>`training.<id>.description`<br>`training.<id>.factionLabel` *(optional)* | `costMultiplier`, `skillType`, `skills`, `spells`, `perks`, `items`, `outfit`, `requirements`, `skyrimnetContext` *(stays English for LLM)* |
| `SKSE/Plugins/MARAS/childTraining.yaml` & `childTraining/*.yaml` | `daedricBlessings[].id` (e.g. `azura`, `nocturnal`) | `blessing.<id>.name` | `quest`, `hmsBonus`, `skills`, `spells`, `perks` |
| `SKSE/Plugins/MARAS/encounters/*.json` & `encounters/overrides/*.json` | `id` (e.g. `open_date_away`, `cheating_talking_letter`) | `encounters.<id>.modalTitle`, `.modalSubtitle`, `.envelopeMessage`, `.infoBoxTitle`, `.buttonLabel`<br>`encounters.<id>.defaults.{salutation,premise,clue,signOff}`<br>`encounters.<id>.variants.<Temperament>.{salutation,premise,clue,signOff}` | `modalTone`, `modalSize`, `allowLoverNameToken`, `defaultCancelButtonIndex` |

> [!TIP]
> **Spouse Bonus Engine Descriptions (`TT_MARAS.esp`)**: `bonuses.<category>.0.*` localizes the **Dashboard Household Buffs** cards. If a modder also changes the Active Effects menu description on the underlying `MGEF` / `PERK` record in `.esp`, provide a small DSD file under `SKSE/Plugins/DynamicStringDistributor/<Plugin.esp>/` for those `MGEF` / `PERK` subrecords.

### 6.2 Runtime Resolution Priority (Why English Mods Work Out of the Box)

For every catalog string above, `LocalizationService` resolves text in this exact order:
1. **Active Language Overlay (`localization/<lang>/*.json` when `<lang> != "en"`, plus any `localization/en/zz_*.json` add-on drop-ins)** — loaded in **alphabetical filename order** inside that language folder (files later in the alphabet win key-by-key).
2. **Live Gameplay YAML/JSON Text** — the `name` / `description` / `location` loaded from `bonuses.json`, `childTraining.yaml` + `childTraining/*.yaml`, or `encounters/*.json` + `encounters/overrides/*.json` (including any modder overrides!).
3. **Stock `localization/en/catalogs.json`** — used as the base reference dictionary for `sync_localization.py` and only consulted at runtime if a gameplay file left a text field blank.

Because Step 2 sits **above** stock `en/catalogs.json`, **editing `bonuses.json`, dropping a `childTraining/*.yaml` patch, or adding `encounters/overrides/*.json` immediately updates English text in-game without touching `localization/en/catalogs.json`!**

---

### 6.3 Modder Guide: Adding or Modifying Content & Shipping Partial Translations Only

If you are creating an add-on or overhaul mod (for example, adding a **Morag Tong** training path, tweaking the **Warrior** training path, or customizing an **Encounter Letter**), **never copy or overwrite `catalogs.json`, `ui_dashboard.json`, or `hud_and_modals.json`**. Instead, ship standalone drop-in files containing **only your mod's changes**:

#### Case A — Adding a Brand-New Training Path, Blessing, or Encounter
1. **Create your gameplay drop-in file with English text**:
   - `Data/SKSE/Plugins/MARAS/childTraining/MoragTong_Addon.yaml` (with `id: morag_tong`, English `name`, `location`, `description`, `requirements.label`, and English `skyrimnetContext`).
   - Players on English (or any language you haven't translated yet) immediately see your English text from `MoragTong_Addon.yaml`.
2. **Ship tiny per-language JSON drop-ins containing ONLY your new keys**:
   Because `LocalizationService` merges **all** `*.json` files in `localization/<lang>/` alphabetically (ignoring `_*` files and `names.json`), name your file `zz_<your_mod>.json`:
   ```text
   Data/SKSE/Plugins/MARAS/
   ├── childTraining/
   │   └── MoragTong_Addon.yaml
   └── localization/
       ├── de/
       │   └── zz_morag_tong_addon.json
       └── ru/
           └── zz_morag_tong_addon.json
   ```
   Example `Data/SKSE/Plugins/MARAS/localization/de/zz_morag_tong_addon.json`:
   ```json
   {
     "training": {
       "morag_tong": {
         "name": "Morag-Tong-Vollstrecker",
         "location": "Zuflucht von Rabenfels",
         "description": "Sanktionierte Ausbildung der Dunmer auf Solstheim in Doppelklingen-Präzision, Schattenmagie und ehrenhaften Erlassen.",
         "factionLabel": "Patronat von Solstheim"
       }
     },
     "blessing": {
       "azura": {
         "name": "Azura (Dämmerungs-Voraussicht)"
       }
     }
   }
   ```

#### Case B — Modifying an Existing Built-In Training Path, Encounter, or Bonus
Suppose your mod changes the `warrior` training description (via `childTraining/WarriorOverhaul.yaml`), rewrites `open_date_away` (via `encounters/overrides/open_date_away.json`), or rebalances `bonuses.json`:
1. **English players**: Put your updated English text directly in `childTraining/WarriorOverhaul.yaml`, `encounters/overrides/open_date_away.json`, or `bonuses.json`. No `localization/en/` file is needed! *(Optional: if you want a pure-text English replacer without touching `bonuses.json`, you can even drop `localization/en/zz_my_text_tweak.json`.)*
2. **Translated languages (`de`, `ru`, etc.)**: Because a full German pack already defines `training.warrior.description` inside `localization/de/catalogs.json`, prefixing your mod's localization file with `zz_` ensures it loads **after** `catalogs.json` (`"catalogs.json" < "zz_warrior_overhaul.json"`) and cleanly overrides **only** the keys you changed—with **zero file conflicts** in MO2 or Vortex:
   ```json
   {
     "bonuses": {
       "warrior": {
         "0": {
           "description": "An der Seite Eures Ehepartners erhöht sich Euer Nahkampfschaden und Eure Blockwirkung um"
         }
       }
     },
     "training": {
       "warrior": {
         "description": "Überarbeitete deutsche Beschreibung nur für den Krieger-Pfad."
       }
     },
     "encounters": {
       "open_date_away": {
         "envelopeMessage": "Eine kurze Notiz von {partner} liegt auf dem Tisch und erwähnt {location}.",
         "defaults": {
           "premise": "Ich bin heute Abend mit {lover} im {location} unterwegs."
         }
       }
     }
   }
   ```
   Any sub-fields you omit (such as `training.warrior.name` or other `encounters.open_date_away.variants.*` temperaments) remain untouched from the user's installed translation pack!

