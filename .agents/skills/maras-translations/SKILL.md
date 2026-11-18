---
name: maras-translations
description: >-
  End-to-end localization and translation runbook for M.A.R.A.S (JSON language packs + TT_MARAS.esp Dynamic String Distributor). Activate whenever translating, updating, auditing, or packaging a language pack (de, ru, fr, es, it, pl, uk, cs, tr, pt-BR, ja, ko, zh-CN, zh-TW, etc.), or whenever adding, modifying, or removing player-facing text in C++, Papyrus, ui-src, gameplay catalogs, or TT_MARAS.esp.
---

# M.A.R.A.S Localization & Translation Runbook

M.A.R.A.S uses a **100% ESP-override-free two-layer localization architecture** with key-by-key English fallback:
1. **MARAS JSON Pack** — Base English lives at [`SKSE/Plugins/MARAS/localization/en/`](../../../SKSE/Plugins/MARAS/localization/en/) (shipped in the main archive). Non-English output packs live in [`localization/packs/<lang>/`](../../../localization/packs/) (bundled into the Translations FOMOD, installing to `Data/SKSE/Plugins/MARAS/localization/<lang>/`): `_meta.json`, `hud_and_modals.json`, `catalogs.json`, `ui_dashboard.json`, `ui_settings.json`, optional `names.json`, and optional `zz_<addon>.json` overlays (`LocalizationService` + `useLocaleStore`).
2. **Dynamic String Distributor (`TT_MARAS.esp`)** — `DIAL`, `INFO`, `QUST`, `BOOK`, `MESG`, `SPEL`, `MGEF`, and `PERK` text synced against [TT_MARAS_dsd_template.json](../../../localization/scripts/TT_MARAS_dsd_template.json). Non-English output files live in `localization/packs/<lang>/dsd/TT_MARAS.esp/<lang>.json` (installing via FOMOD to `Data/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json` so dev MO2 setups stay English).

---

## 1. Non-Negotiable Localization Invariants

- **Keep `TT_MARAS.esp` Non-Localized**: `TT_MARAS.esp` retains inline English strings (`Small` ESL flag only; never enable the `Localized` header flag or generate `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS` tables).
- **Canonical English Exclusions (Never Translate)**:
  - `SKSE/Plugins/SkyrimNet/external/tetherball88.maras/**` (prompts, actions, triggers)
  - `SKSE/Plugins/MARAS/promptStrings.json` (`PromptStringsLoader`)
  - `skyrimnetContext` fields inside `SKSE/Plugins/MARAS/childTraining.yaml` and `childTraining/*.yaml`
  - Internal JSON/YAML keys, enum identifiers, FormKeys, `TTM_*` technical EditorIDs, co-save tags, and `MARAS.log` diagnostics.
- **Placeholder, Tag & Scaleform Punctuation Integrity**:
  - Never rename `{named}` placeholder tokens in JSON packs (`{{` and `}}` escape literal braces).
  - Never alter Creation Kit / Scaleform tags (`<Alias=...>`, `<Alias.Pronoun=...>`, `<mag>`, `<dur>`, `%.0f`, `<font ...>`, `</font>`) in DSD files, and never put non-ASCII characters inside `$DragonFont` (`00004D|TT_MARAS.esp` `BOOK DESC`).
  - **Skyrim Scaleform Safe Punctuation (`DSD`, `names.json`, and `hud.*` / `pet.*` / `common.*` / `mcm.*`)**: Never emit LLM typography (`—`, `–`, `…`, `«»`, `“”`, `’`, `ʼ`, `•`, `→`, NBSP, zero-width spaces) in Skyrim-rendered text—it renders as missing-glyph squares (`□`) in `.swf` fonts. Always use plain ASCII `-`, `...`, `"`, and `'` (enforced automatically by `sync_localization.py` and `translation_batches.py`).

---

## 2. Developer Workflow — Adding, Editing, or Removing English Strings

Whenever adding, renaming, or removing player-facing text in code or data files, update the matching English source in the same task:

1. **C++ HUD, Modals & Common Labels (`SKSE_Source/`)**:
   - Add canonical English keys to [hud_and_modals.json](../../../SKSE/Plugins/MARAS/localization/en/hud_and_modals.json) (`hud.*`, `modal.*`, `common.*`, `mcm.*`, `pet.*`).
   - Look up via `MARAS::Tr`, `MARAS::TrOr`, `MARAS::TrFmt`, or `MARAS::TrPlural` ([LocalizationService.h](../../../SKSE_Source/modules/engine/include/maras/engine/LocalizationService.h)).
   - For count-sensitive strings, always define **both** `<stem>_one` and `<stem>_other` in `en/`.

2. **Papyrus Scripts (`Scripts/Source/*.psc`)**:
   - Use `MARAS.NotifyLocalized(key, arg1, arg2)` or `MARAS.TranslateKey(key, arg1, arg2)`.
   - `FormatPapyrusTranslation` ([PapyrusSettings.cpp](../../../SKSE_Source/modules/bridge_papyrus/src/PapyrusSettings.cpp)) binds `arg1` $\to$ `{arg1}`, `{actor}`, `{spouse}`, `{child}`, `{mods}`, `{total}` and `arg2` $\to$ `{arg2}`, `{home}`, `{migrated}`. Use those token names (or extend `FormatPapyrusTranslation` if introducing a new semantic token).

3. **Magelight UI Dashboard & Settings (`ui-src/`)**:
   - Add keys (without a top-level `"ui."` wrapper) to [ui_dashboard.json](../../../SKSE/Plugins/MARAS/localization/en/ui_dashboard.json) or [ui_settings.json](../../../SKSE/Plugins/MARAS/localization/en/ui_settings.json) and call `const { t, tp } = useTranslation()` ([ui-src/src/i18n/index.ts](../../../ui-src/src/i18n/index.ts)).
   - *(Note: `LocalizationService` prepends `"ui."` internally on `ui_*.json` files and strips it for `UiEntries()`, so C++ lookups of UI keys use `"ui.<key>"` while TypeScript uses `"<key>"`.)*
   - **Mandatory Frontend Rebuild**: [useLocaleStore.ts](../../../ui-src/src/store/useLocaleStore.ts) bundles `@locales-en/ui_dashboard.json` and `@locales-en/ui_settings.json` into `BASE_EN_STRINGS`. Always rebuild after editing either file:
     ```powershell
     Set-Location ui-src; npm run build
     ```

4. **Gameplay Data Catalogs (`bonuses.json`, `childTraining.yaml`, `encounters/*.json`)**:
   - Mirror stock English display text changes into [catalogs.json](../../../SKSE/Plugins/MARAS/localization/en/catalogs.json) (`bonuses.<category>.0.{name,description}`, `training.<id>.{name,location,description,factionLabel}`, `blessing.<id>.name`, `encounters.<id>.*`) so translators receive deltas.
   - At runtime, `LocalizationService` resolves: active `<lang>` overlay (or `en/zz_*.json` add-on) $\to$ live YAML/JSON text $\to$ stock `en/catalogs.json`.

5. **`TT_MARAS.esp` Records (`.spriggit/TT_MARAS.esp/`)**:
   - After modifying any player-facing `DIAL`, `INFO`, `QUST`, `BOOK`, `MESG`, `SPEL`, `MGEF`, or `PERK` text in `.spriggit/TT_MARAS.esp/`, refresh the DSD template:
     ```powershell
     python localization/scripts/export_dsd_template.py
     ```

6. **Developer Verification Gate**:
   ```powershell
   python localization/scripts/export_dsd_template.py --check
   python localization/scripts/sync_localization.py --lang en --strict
   ```

---

## 3. Translator / AI Workflow — Authoring or Updating `<lang>` Packs

Before translating strings into `<lang>`, **read [translation-style-and-gotchas.md](./references/translation-style-and-gotchas.md)** for concatenation traps (`bonuses.*.0.description`, `modal.confirm.label_*`), CLDR plural suffixes per language family, `$DragonFont` rules, and Elder Scrolls terminology.

> [!IMPORTANT]
> A full translation pack spans **~2,870 entries** (`~1,613` JSON keys + `~1,258` DSD entries). **Never** read or edit the entire `localization/packs/<lang>/_todo.json` in a single tool call—always split into batches using [translation_batches.py](../../../localization/scripts/translation_batches.py).

### Step-by-Step Batch Pipeline

1. **Initialize `_meta.json` & Split Pending (or Full-Pack Review) Entries into Batches**:
   ```powershell
   # Creates localization/packs/<lang>/_meta.json (if missing), runs sync_localization.py,
   # scaffolds language-specific CLDR plural siblings (_few/_many for Slavic, _other-only for CJK),
   # auto-protects $DragonFont runes, orders DSD records chronologically via .spriggit/TT_MARAS.esp
   # (Quest/Branch LinkTo chains & Scene variants + speaker hints), and writes ~100-entry batch_*.json files:
   python localization/scripts/translation_batches.py split --lang <lang> --out-dir "<scratch_dir>/i18n_<lang>" --init-meta

   # Full-pack LQA / editorial review mode (pre-fills all entries with existing <lang> strings even at 100% coverage):
   python localization/scripts/translation_batches.py split --lang <lang> --all --out-dir "<scratch_dir>/i18n_<lang>"
   ```
   *(Optional flags: `--batch-size 80`, `--prev-en <old_en_dir>` to flag changed English JSON strings across versions, `--no-dsd`, `--dsd-only`, or `--all` for full LQA passes.)*

2. **Translate or Review `batch_*.json` Files (Sequential or Parallel Subagents)**:
   - Fill or refine `"translation"` for each item in `<scratch_dir>/i18n_<lang>/batch_*.json`.
   - Follow inline `"placeholders"`, `"plural_rule"`, `"required_tags"`, `"context"` (quest, branch, `prompt_en`, `speaker_hints`, scene `variant`/`phase`), and `"note"` hints attached to batch items.
   - Maintain multi-turn conversation flow, consistent T-V pronoun register per speaker/relationship role, Nominative placeholder case-framing, and natural idiomatic phrasing (avoiding word-for-word calques and stiff gender-dodge passives).
   - When fanning out across parallel subagents or `run_workflow`, assign **disjoint `batch_*.json` files** to each worker so zero file write collisions occur, and instruct each worker to read [translation-style-and-gotchas.md](./references/translation-style-and-gotchas.md) first.
   - Omit `names.json` for Latin-script languages unless requested; for Cyrillic/CJK/Korean packs (`ru`, `uk`, `ja`, `ko`, `zh-CN`, `zh-TW`), optionally localize `localization/packs/<lang>/names.json` using root keys `"childrenNames"` and `"critterNames"`.

3. **Validate & Apply Completed Batches**:
   ```powershell
   # Check placeholders, CK <Alias=...>/<mag>/%.0f/<font> tags, $DragonFont ASCII, and JSON syntax:
   python localization/scripts/translation_batches.py validate --lang <lang> --batch-dir "<scratch_dir>/i18n_<lang>" --require-complete

   # Atomically merge all validated batches into localization/packs/<lang>/*.json and
   # localization/packs/<lang>/dsd/TT_MARAS.esp/<lang>.json:
   python localization/scripts/translation_batches.py apply --lang <lang> --batch-dir "<scratch_dir>/i18n_<lang>" --prune-obsolete
   ```

4. **Strict Audit & Optional MO2/Vortex Archive Packaging**:
   ```powershell
   # Verify 100% coverage and 0 placeholder/CK-tag warnings or errors:
   python localization/scripts/sync_localization.py --lang <lang> --strict

   # Build standalone zero-ESP archive for a single language (maps to Data/SKSE/Plugins/...):
   python localization/scripts/translation_batches.py package --lang <lang> --output "dist/MARAS_Translation_<lang>.zip"

   # Build the multi-language MO2/Vortex FOMOD installer (auto-discovers all complete/valid packs in localization/packs/; SelectExactlyOne):
   python localization/scripts/package_translations_fomod.py --version <ver> --output "dist/MARAS_Translations_<ver>.zip"
   ```
   *(In-game live preview: open Dashboard $\to$ **Settings $\to$ General $\to$ Language**, select `<lang>`, and click **Reload Localization** without restarting Skyrim.)*

---

## 4. Add-On & Patch Mod Partial Translations (`zz_<mod>.json`)

When authoring an add-on or rebalance mod (e.g. `childTraining/MoragTong_Addon.yaml` or `encounters/overrides/*.json`), **never copy or overwrite full `catalogs.json`, `ui_dashboard.json`, or `hud_and_modals.json`**. Place minimal delta keys inside `SKSE/Plugins/MARAS/localization/<lang>/zz_<mod_slug>.json`—`LocalizationService` loads `*.json` files alphabetically so `zz_*` cleanly overrides only the keys your mod touches. See Section 6 of [LOCALIZATION_GUIDE.md](../../../docs/modder/LOCALIZATION_GUIDE.md) for schema examples.
