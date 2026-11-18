# M.A.R.A.S Translation Style, Grammar Traps & DSD Reference

Read this guide before translating M.A.R.A.S strings into any non-English language.

---

## 1. M.A.R.A.S String Concatenation & Grammatical Substitution Traps

### 1.1 `catalogs.json` $\to$ `bonuses.<category>.0.description` (Trailing Value Concatenation)
In `ui-src/src/components/partners/BuffCard.tsx`, spouse archetype buff descriptions render inline immediately before the bold numeric value:
```tsx
{buff.description}{' '}<strong>{valueDisplay}</strong>
```
- **English source**: `"With your spouse at your side in battle, your melee attack damage is increased by"` $\to$ renders `... increased by 15%` or `... increased by 20 points`.
- **Rule**: **Never end `bonuses.*.0.description` with a period (`.` / `。`).** Always phrase the clause so the trailing value (`15%` or localized `unitPoints`) naturally terminates the sentence:
  - **German (`de`)**: `"... erhöht sich Euer Nahkampfschaden um"` ($\to$ `... um 15%`)
  - **Russian (`ru`)**: `"... увеличивает ваш урон в ближнем бою на"` ($\to$ `... на 15%`)
  - **French (`fr`)**: `"... augmente vos dégâts de mêlée de"` ($\to$ `... de 15%`)
  - **Japanese / Korean / Chinese (`ja`, `ko`, `zh-CN`, `zh-TW`)**: End with a noun phrase + colon or particle that precedes a value, e.g. `"…近接攻撃ダメージ増加量:"` / `"…近战攻击伤害提高"` ($\to$ `…提高 15%`).
- Keep buff titles consistent between `catalogs.json` (`bonuses.<cat>.0.name`) and DSD (`SPEL FULL` / `MGEF FULL` / `PERK FULL`) so the Dashboard **Household Buffs** tab matches Skyrim's **Active Effects** menu.

### 1.2 `hud_and_modals.json` $\to$ `modal.confirm.label_*` + `msg_enter` / `msg_breakup`
In `ConfirmationModal.cpp`:
- `label_dating`, `label_engaged`, `label_lover`, `label_married` are injected as `{action}` into `modal.confirm.msg_enter` (`"Allow {actorA} and {actorB} to enter {action}?"`).
- `label_exdating`, `label_jilted`, `label_exlover`, `label_divorced` are injected as `{action}` into `modal.confirm.msg_breakup` (`"Allow {actorA} to proceed with {action} involving {actorB}?"`).
- `label_low_affection_divorce`, `label_open_relationship`, and `label_close_relationship` use dedicated `msg_*` keys (`msg_low_affection_divorce`, `msg_open_relationship`, `msg_close_relationship`), though `title_fallback` (`"Confirm Action: {action}"`) can also display an action label.
- **Rule for inflected languages (`de`, `ru`, `pl`, `uk`, `cs`, `fr`, `es`, `it`, `pt-BR`)**: Either:
  1. Keep `label_*` in nominative/infinitive form and frame `msg_enter` / `msg_breakup` with a colon or guillemets/quotes (e.g. Russian: `"Разрешить {actorA} и {actorB} перейти к статусу «{action}»?"` and `"Разрешить {actorA} выполнить действие «{action}» с {actorB}?"`), **or**
  2. Inflect the four `msg_enter` labels for the exact case governed by `msg_enter`'s verb/preposition and the four `msg_breakup` labels for `msg_breakup`'s verb/preposition.

### 1.3 Non-CLDR Keys Ending in `_one`, `_two`, or `_other`
Do **not** treat these four keys in `hud_and_modals.json` as CLDR `{count}` plural stems—they are standalone semantic keys:
- `hud.spouse_shared_home_stopped_other` (paired with `_stopped_divorce`, `_stopped_deceased`, `_stopped_affection`)
- `modal.child_migration.sub_guardian_one` (`{guardian}` — single adoptive guardian)
- `modal.child_migration.sub_guardians_two` (`{guardianA}` & `{guardianB}` — two adoptive guardians)
- `modal.child_migration.sub_parents_two` (`{mother}` & `{father}`)

---

## 2. CLDR Plural Rules by Language Family

Both C++ `LocalizationCatalog::SelectPluralCategory` and React `Intl.PluralRules` resolve `<stem>_<category>` and bind `{count}` automatically:

| Language Codes | Required Suffixes on True Plural Stems | Cardinal Selection Rule |
| :--- | :--- | :--- |
| `en`, `de`, `es`, `it` | `_one`, `_other` | `1` $\to$ `_one`; `0, 2+` $\to$ `_other` |
| `fr`, `pt-BR` | `_one`, `_other` | **`0` and `1`** $\to$ `_one`; `2+` $\to$ `_other` *(keep `{count}` in `_one` whenever `0` is possible!)* |
| `ru`, `uk` | `_one`, `_few`, `_many`, `_other` | `mod10==1 && mod100!=11` $\to$ `_one`; `mod10 in 2..4 && mod100 not in 12..14` $\to$ `_few`; else $\to$ `_many` (`_other` for fractions/fallback) |
| `pl` | `_one`, `_few`, `_many`, `_other` | `1` $\to$ `_one`; `mod10 in 2..4 && mod100 not in 12..14` $\to$ `_few`; else $\to$ `_many` (`_other` fallback) |
| `cs` | `_one`, `_few`, `_other` | `1` $\to$ `_one`; `2..4` $\to$ `_few`; `0, 5+` $\to$ `_other` |
| `ja`, `zh-CN`, `zh-TW`, `ko`, `tr` | `_other` only | All counts select `_other` (`_one` is unnecessary) |

---

## 3. `TT_MARAS.esp` Dynamic String Distributor (DSD) Subrecord Guide

DSD entries in `SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json` cover `1,258` records across these subrecord types:

| DSD `type` | In-Game Surface | Translation & Formatting Rules |
| :--- | :--- | :--- |
| `DIAL FULL` / `INFO RNAM` | Player dialogue menu choice | Keep concise and natural from the Dragonborn's perspective (first-person intent). |
| `INFO NAM1` | Spoken NPC line (subtitles) | Match relationship stage (stranger, courting, spouse, estranged, child) and Skyrim's localized 2nd-person register. |
| `QUST FULL` | Quest journal title | Short title-case / localized quest title (`TTM_EnablePolygamy`, `TTM_CheckSpouseHome`, etc.). |
| `QUST NNAM` | HUD / Journal objective | Actionable concise objective (`<Alias=...>` preserved). |
| `QUST CNAM` | Quest journal log entry | Written in the Dragonborn's first-person reflective journal voice (`"Maramal recognized that my soul..."`). |
| `BOOK FULL` / `BOOK DESC` | Letters, notices & scrolls | Preserve Scaleform HTML tags (`<font size="19">`, `</font>`) and `\n` line breaks verbatim. **Exception (`$DragonFont`)**: see Section 3.1 below! |
| `MESG DESC` / `MESG ITXT` | Engine message box & buttons | Preserve `%.0f` float tokens; keep `MESG ITXT` button labels short (`Yes`, `No`, `Don't show it again`). |
| `SPEL FULL` / `MGEF FULL` / `PERK FULL` | Magic / Active Effects titles | Align terminology with `catalogs.json` `bonuses.*.0.name`. |
| `MGEF DESC` / `PERK DESC` | Active Effects descriptions | Preserve every `<mag>` token (note: `0000D0` `TTM_LoversRadianceEffect` contains **two** `<mag>` tags!). |

### 3.1 Critical `$DragonFont` Rule (`00004D|TT_MARAS.esp` — `TTM_ScrollofAlessiasGrace`)
- `00004D|TT_MARAS.esp` (`TTM_ScrollofAlessiasGrace` `BOOK DESC`) uses `<font face="$DragonFont" size="19">` to render **untranslated ancient Dragon runes** in-game before the player has the scroll translated into `00004C|TT_MARAS.esp` (`TTM_ScrollofAlessiasGraceTranslated`).
- Skyrim's `$DragonFont` glyph table supports **uppercase ASCII `A–Z` only**. Any Cyrillic, CJK, Korean, or accented Latin characters (`ä`, `ö`, `ü`, `ß`, `é`, `ą`, `č`, etc.) inside `$DragonFont` render as ugly missing-glyph boxes (`□□□`)!
- **Rule**: Keep `00004D|TT_MARAS.esp` `BOOK DESC` in its original uppercase ASCII `A–Z` text verbatim. Translate `00004D` `BOOK FULL` (the item name) and both `BOOK FULL` + `BOOK DESC` of `00004C` (`TTM_ScrollofAlessiasGraceTranslated`, which uses standard `<font size="19">`).

### 3.2 Creation Kit Tag Preservation
Never translate or alter ASCII identifiers inside `<...>`:
- `<Alias=Player>`, `<Alias=Spouse>`, `<Alias=Partner>`, `<Alias=Lover>`, `<Alias=Victim>`, `<Alias=TavernLocation>`
- `<Alias.Pronoun=Spouse>`, `<Alias.PronounObj=Spouse>`, `<Alias.Pronoun=Partner>`
- `<mag>`, `<dur>`, `%.0f`

---

## 4. Elder Scrolls Lore, Register & UI Length Guidelines

1. **Official *Skyrim Special Edition* Proper Nouns**:
   - Always use the official localized *Skyrim SE* names for Divines (*Mara, Dibella, Kynareth, Stendarr, Julianos, Akatosh*), Daedric Princes (*Azura, Boethiah, Meridia, Molag Bal, Nocturnal, Hircine, Hermaeus Mora, Mehrunes Dagon, Sanguine, Malacath, Mephala, Peryite*), cities/holds (*Whiterun, Solitude, Windhelm, Riften, Markarth,Solstheim, Ragnvald*), and factions/locations (*Jorrvaskr, Companions, The Circle, Underforge, Skyforge, College of Winterhold, Arcanaeum, Thieves Guild, Ratway Cistern, Dark Brotherhood, Bards College, Palace of the Kings, Castle Dour, Sky Haven Temple, The Blades, Fort Dawnguard, College of Whispers, Stendarr's Beacon, Temple of Kynareth, House Telvanni, School of Julianos, Synod, Fighters Guild, Morag Tong, Nightingale Hall, Evergloam, Coldharbour, Frostmoon Crag*).
2. **Two Distinct Registers**:
   - **Dashboard & Settings UI (`ui_dashboard.json`, `ui_settings.json`, system/LLM modals)**: Crisp, scannable RPG interface style. Establish consistent terms across all files for M.A.R.A.S core concepts: *Affection*, *Hierarchy* (*Primary / Lead, Secondary, Tertiary, Subordinate*), *Temperament* (*Romantic, Humble, Independent, Proud, Jealous*), *Social Class*, *Open Relationship*, *Intrigue Mode*, *Matchmaking*, *Partner Agency*, *Adolescent Training*.
   - **In-World Dialogue, Letters & Lore (`DSD`, `hud.*`, `catalogs.json` `training.*` & `encounters.*`)**: Authentic Tamrielic in-world tone. Respect formal vs. intimate 2nd-person pronouns (`Sie`/`Ihr` vs. `du` in German depending on Skyrim SE convention, `Вы` vs. `ты` in Russian, `vous` vs. `tu` in French, polite vs. intimate speech in Japanese/Korean) based on whether the speaker is a court clerk/priestess, a courting acquaintance, a devoted spouse, or the player's child.
3. **UI Badge & Tab Compactness**:
   - Magelight UI scales proportionally via `rem`. Keep top navigation tabs, status pills (`enums.relationshipStatus.*`, `badge_*`), table column headers, and slider unit suffixes (`unitDaysShort`, `unitHours`, `unitPercent`, `unitMultiplier`) compact so cards and badges do not clip or wrap awkwardly.
4. **Optional `names.json` Policy**:
   - Omit `SKSE/Plugins/MARAS/localization/<lang>/names.json` for Latin-alphabet languages unless explicitly requested, preserving lore-friendly Tamrielic names (*Thorvik, Yrsil, Aubrin*).
   - Include `names.json` (root keys `"childrenNames"` mapping race $\to$ `{"male": [...], "female": [...]}` and `"critterNames"` mapping species $\to$ `[...]`) for non-Latin scripts (`ru`, `uk`, `ja`, `ko`, `zh-CN`, `zh-TW`) so newborn/critter names match vanilla NPC script rendering.
