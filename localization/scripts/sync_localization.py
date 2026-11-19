#!/usr/bin/env python3
"""
M.A.R.A.S Localization Sync, Audit & Delta Workflow Tool (JSON Packs + DSD).

Audits and synchronizes BOTH localization layers of M.A.R.A.S:
  1. C++ / Papyrus / Catalogs / Magelight UI JSON packs:
       `SKSE/Plugins/MARAS/localization/en/*.json`  <->  `localization/packs/<lang>/SKSE/Plugins/MARAS/localization/<lang>/*.json`
  2. `TT_MARAS.esp` Dynamic String Distributor (DSD) packs:
       `localization/scripts/TT_MARAS_dsd_template.json`  <->  `localization/packs/<lang>/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json`

Features:
  - Running `python localization/scripts/sync_localization.py --lang <lang>` audits both layers
    AND automatically writes `localization/packs/<lang>/_todo.json` whenever any
    missing or source-changed strings exist (pass `--check` or `--strict` for read-only mode).
  - Understands CLDR plural sibling keys (`_zero`, `_one`, `_two`, `_few`, `_many`, `_other`)
    so Slavic/Arabic/etc. plural forms (`_few`, `_many`) validate against `<stem>_other`.
  - Validates `{named}` placeholders in JSON packs AND Creation Kit `<Alias=...>`, `<mag>`,
    `<dur>`, and `%.0f` tags in DSD records.
  - Detects changed English `.esp` strings automatically via DSD `"original"` fields, and
    detects changed English JSON strings when `--prev-en <dir>` is supplied.
  - `--apply-todo [path]` merges translated entries from `_todo.json` back into both
    `localization/packs/<lang>/SKSE/Plugins/MARAS/localization/<lang>/*.json` and
    `localization/packs/<lang>/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json`.

Usage examples:
  python localization/scripts/sync_localization.py --lang de
  python localization/scripts/sync_localization.py --lang de --apply-todo
  python localization/scripts/sync_localization.py --lang de --check
  python localization/scripts/sync_localization.py --lang en --strict

Zero third-party dependencies (Python 3.10+ standard library only).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


PLURAL_SUFFIXES = ("_zero", "_one", "_two", "_few", "_many", "_other")
SPECIAL_FILES = {"_meta.json", "names.json"}
TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
CK_TAG_RE = re.compile(r"<[A-Za-z0-9_=.]+>")
HTML_BOOK_TAG_RE = re.compile(r"</?(?:font|p|br|img|div)\b[^>]*>|\[pagebreak\]", re.IGNORECASE)
PRINTF_TAG_RE = re.compile(r"%(?:\.\d+)?[fdgs]")
DRAGON_FONT_RE = re.compile(r'face\s*=\s*["\']?\$DragonFont["\']?', re.IGNORECASE)

VALID_DSD_TYPES = {
    "ACTI FULL", "ALCH FULL", "AMMO FULL", "APPA FULL", "ARMO FULL", "AVIF FULL",
    "BOOK FULL", "CELL FULL", "CONT FULL", "CLAS FULL", "DOOR FULL", "ENCH FULL",
    "EXPL FULL", "FLOR FULL", "FURN FULL", "HAZD FULL", "INGR FULL", "KEYM FULL",
    "LCTN FULL", "LIGH FULL", "MESG FULL", "MGEF FULL", "MISC FULL", "PERK FULL",
    "PROJ FULL", "QUST FULL", "RACE FULL", "SCRL FULL", "SHOU FULL", "SLGM FULL",
    "SPEL FULL", "TACT FULL", "TREE FULL", "WATR FULL", "WEAP FULL", "WOOP FULL",
    "WRLD FULL", "NPC_ FULL",
    "LSCR DESC", "MGEF DNAM", "NPC_ SHRT", "REGN RDMP", "WOOP TNAM",
    "MESG ITXT", "PERK EPF2", "QUST NNAM", "PERK EPFD",
    "ACTI RNAM", "FLOR RNAM",
    "DIAL FULL", "AMMO DESC", "ARMO DESC", "AVIF DESC", "BOOK DESC", "MESG DESC",
    "PERK DESC", "RACE DESC", "SCRL DESC", "SHOU DESC", "SPEL DESC", "WEAP DESC",
    "GMST DATA", "REFR FULL", "QUST CNAM", "BOOK CNAM", "INFO RNAM", "INFO NAM1",
}
DSD_ZERO_BASED_INDEX_TYPES = {"MESG ITXT", "PERK EPF2", "QUST NNAM", "PERK EPFD"}

# Common LLM-emitted Unicode symbols that render as missing-glyph boxes (`□`) in Skyrim Scaleform `.swf` fonts
SCALEFORM_SYMBOL_MAP: Dict[str, str] = {
    # Long / non-ASCII dashes & hyphens
    "\u2010": "-",
    "\u2011": "-",
    "\u2012": "-",
    "\u2013": "-",  # en dash (–)
    "\u2014": "-",  # em dash (—)
    "\u2015": "-",  # horizontal bar (―)
    "\u2212": "-",  # minus sign (−)
    "\uFE58": "-",
    "\uFE63": "-",
    "\uFF0D": "-",
    # Single-glyph ellipses
    "\u2026": "...",  # horizontal ellipsis (…)
    "\u22EF": "...",
    "\uFE19": "...",
    # Smart / angle double quotation marks
    "\u00AB": '"',  # «
    "\u00BB": '"',  # »
    "\u201C": '"',  # “
    "\u201D": '"',  # ”
    "\u201E": '"',  # „
    "\u201F": '"',  # ‟
    "\u2033": '"',  # ″
    "\u2036": '"',
    "\u301D": '"',
    "\u301E": '"',
    "\uFF02": '"',
    # Smart / modifier single quotes & apostrophes
    "\u2018": "'",  # ‘
    "\u2019": "'",  # ’
    "\u201A": "'",  # ‚
    "\u201B": "'",  # ‛
    "\u02BC": "'",  # modifier letter apostrophe (ʼ)
    "\u02B9": "'",  # modifier letter prime (ʹ)
    "\u2032": "'",  # ′
    "\u2035": "'",
    "\uFF07": "'",
    # Non-breaking & typographic spaces
    "\u00A0": " ",
    "\u202F": " ",
    "\u2000": " ",
    "\u2001": " ",
    "\u2002": " ",
    "\u2003": " ",
    "\u2004": " ",
    "\u2005": " ",
    "\u2006": " ",
    "\u2007": " ",
    "\u2008": " ",
    "\u2009": " ",
    "\u200A": " ",
    "\u205F": " ",
    # Zero-width invisible chars
    "\u200B": "",
    "\u200C": "",
    "\u200D": "",
    "\u2060": "",
    "\uFEFF": "",
    # Bullets, arrows, math & numero
    "\u2022": "-",  # •
    "\u2023": "-",
    "\u25E6": "-",
    "\u2043": "-",
    "\u00B7": "-",  # ·
    "\u2192": "->",  # →
    "\u2794": "->",
    "\u279C": "->",
    "\u2190": "<-",  # ←
    "\u2194": "<->",  # ↔
    "\u00D7": "x",  # ×
    "\u2248": "~",  # ≈
    "\u2264": "<=",  # ≤
    "\u2265": ">=",  # ≥
    "\u2260": "!=",  # ≠
    "\u2116": "#",  # №
}
UNIVERSAL_LLM_ARTIFACT_CHARS = (
    "\u200B",
    "\u200C",
    "\u200D",
    "\u2060",
    "\uFEFF",
    "\u00A0",
    "\u202F",
    "\u2026",
    "\u2018",
    "\u2019",
    "\u02BC",
)
UI_EXTRA_SANITIZE_CHARS: Dict[str, str] = {
    "\u2010": "-",
    "\u2011": "-",
    "\u2012": "-",
    "\u2013": "-",
    "\u2014": "-",
    "\u2015": "-",
    "\u2212": "-",
    "\u2026": "...",
    "\u2116": "#",
    "\u2018": "'",
    "\u2019": "'",
    "\u02BC": "'",
    "\u02B9": "'",
    "\u00A0": " ",
    "\u202F": " ",
    "\u200B": "",
    "\u200C": "",
    "\u200D": "",
    "\u2060": "",
    "\uFEFF": "",
}


def is_scaleform_json_key(fname: str, key: str) -> bool:
    return fname == "hud_and_modals.json" and key.startswith(("hud.", "pet.", "common.", "mcm."))


def _format_bad_char(ch: str) -> str:
    if ch.isprintable() and not ch.isspace():
        return f"U+{ord(ch):04X} ('{ch}')"
    return f"U+{ord(ch):04X}"


def find_scaleform_unsafe_chars(text: str) -> List[str]:
    seen: Set[str] = set()
    found: List[str] = []
    for ch in text:
        if ch in SCALEFORM_SYMBOL_MAP or ord(ch) > 0xFFFF:
            if ch not in seen:
                seen.add(ch)
                found.append(_format_bad_char(ch))
    return found


def find_universal_llm_artifacts(text: str, en_ref: str) -> List[str]:
    seen: Set[str] = set()
    found: List[str] = []
    for ch in text:
        if ch in UNIVERSAL_LLM_ARTIFACT_CHARS and ch not in en_ref and ch not in seen:
            seen.add(ch)
            found.append(_format_bad_char(ch))
    return found


def sanitize_scaleform_text(text: str) -> str:
    if not text:
        return text
    out: List[str] = []
    for ch in text:
        if ch in SCALEFORM_SYMBOL_MAP:
            out.append(SCALEFORM_SYMBOL_MAP[ch])
        elif ord(ch) <= 0xFFFF:
            out.append(ch)
    return "".join(out)


def sanitize_ui_text(text: str) -> str:
    if not text:
        return text
    return "".join(UI_EXTRA_SANITIZE_CHARS.get(ch, ch) for ch in text)


def extract_dsd_tags(text: str) -> Tuple[List[str], List[str], List[str]]:
    """Extract Creation Kit `<Alias=...>` / `<mag>`, Scaleform `<font>` / `[pagebreak]`, and printf tokens."""
    ck_tags = CK_TAG_RE.findall(text)
    book_tags = HTML_BOOK_TAG_RE.findall(text)
    printf_tags = PRINTF_TAG_RE.findall(text)
    return ck_tags, book_tags, printf_tags


def validate_dsd_schema_fields(
    disp_key: str,
    dtype: str,
    index_val: Any,
    source_label: str,
) -> Optional[str]:
    """Validate DSD `type` and `index` against SSE-Dynamic-String-Distributor runtime rules."""
    norm_type = dtype.strip().upper()
    if norm_type not in VALID_DSD_TYPES:
        hint = " (use 'MGEF DNAM' for Magic Effect descriptions)" if norm_type == "MGEF DESC" else ""
        return f"{source_label} '{disp_key}': unsupported DSD type '{norm_type}'{hint}"

    has_index = index_val is not None
    parsed_idx: Optional[int] = None
    if has_index:
        if isinstance(index_val, bool) or not isinstance(index_val, int):
            return f"{source_label} '{disp_key}': 'index' must be an integer (got {index_val!r})"
        parsed_idx = index_val

    if norm_type == "INFO NAM1":
        if parsed_idx is None or parsed_idx < 1:
            return (
                f"{source_label} '{disp_key}': 'INFO NAM1' requires 1-based ResponseNumber 'index' >= 1 "
                f"(got {index_val!r}; index 0 collides with 'INFO RNAM' player prompts in DSD)"
            )
    elif norm_type in ("INFO RNAM", "BOOK CNAM"):
        if parsed_idx is not None and parsed_idx != 0:
            return f"{source_label} '{disp_key}': '{norm_type}' must not set non-zero index (got {parsed_idx})"
    elif norm_type in DSD_ZERO_BASED_INDEX_TYPES:
        if parsed_idx is None or parsed_idx < 0:
            return f"{source_label} '{disp_key}': '{norm_type}' requires non-negative integer 'index' (got {index_val!r})"
    elif norm_type == "QUST CNAM" and parsed_idx is not None and parsed_idx < 0:
        return f"{source_label} '{disp_key}': 'QUST CNAM' index must be >= 0 (got {parsed_idx})"

    return None


def validate_dsd_entry_schema(entry: Dict[str, Any], source_label: str) -> Optional[str]:
    disp_key = make_dsd_key(entry, canonical_display=True)
    dtype = str(entry.get("type", ""))
    return validate_dsd_schema_fields(disp_key, dtype, entry.get("index"), source_label)


def extract_placeholders(template: str) -> Set[str]:
    """Extract `{named}` placeholder tokens matching C++ `LocalizationCatalog::ExtractPlaceholders`."""
    tokens: Set[str] = set()
    i = 0
    n = len(template)
    while i < n:
        ch = template[i]
        if ch == "{":
            if i + 1 < n and template[i + 1] == "{":
                i += 2
                continue
            close = template.find("}", i + 1)
            if close > i + 1:
                candidate = template[i + 1 : close]
                if TOKEN_RE.match(candidate):
                    tokens.add(candidate)
                    i = close + 1
                    continue
            i += 1
        elif ch == "}" and i + 1 < n and template[i + 1] == "}":
            i += 2
        else:
            i += 1
    return tokens


def normalize_dsd_form_id(raw_form_id: str) -> str:
    parts = raw_form_id.strip().split("|", 1)
    if len(parts) != 2:
        return raw_form_id.strip().lower()
    hex_part = parts[0].strip()
    if hex_part.lower().startswith("0x"):
        hex_part = hex_part[2:]
    try:
        num = int(hex_part, 16)
        norm_hex = f"{num:06X}"
    except ValueError:
        norm_hex = hex_part.upper()
    return f"{norm_hex}|{parts[1].strip().lower()}"


def make_dsd_key(entry: Dict[str, Any], canonical_display: bool = False) -> str:
    raw_fid = str(entry.get("form_id", "")).strip()
    fid = raw_fid if canonical_display else normalize_dsd_form_id(raw_fid)
    dtype = str(entry.get("type", "")).strip().upper()
    if "index" in entry and entry["index"] is not None:
        return f"{fid}|{dtype}|{int(entry['index'])}"
    return f"{fid}|{dtype}"


def flatten_json(node: Any, prefix: str = "", out: Optional[Dict[str, str]] = None) -> Dict[str, str]:
    if out is None:
        out = {}
    if not isinstance(node, dict):
        return out
    for k, v in node.items():
        if not k or k.startswith("$comment") or k.startswith("_comment"):
            continue
        full = f"{prefix}.{k}" if prefix else k
        if isinstance(v, str):
            out[full] = v
        elif isinstance(v, dict):
            flatten_json(v, full, out)
    return out


def unflatten_aligned_to_base(
    flat_values: Dict[str, str],
    base_tree: Optional[Dict[str, Any]] = None,
    prefix: str = "",
) -> Dict[str, Any]:
    """
    Reconstruct a nested JSON object from `flat_values`, preserving key ordering from `base_tree`
    and inserting CLDR plural sibling keys (`_few`, `_many`, etc.) right next to their plural stem.
    """
    result: Dict[str, Any] = {}
    consumed: Set[str] = set()

    if isinstance(base_tree, dict):
        for k, v in base_tree.items():
            if k.startswith("$comment") or k.startswith("_comment"):
                continue
            full = f"{prefix}.{k}" if prefix else k
            if isinstance(v, dict):
                sub = unflatten_aligned_to_base(flat_values, v, full)
                if sub:
                    result[k] = sub
                sub_prefix = f"{full}."
                for fk in flat_values:
                    if fk.startswith(sub_prefix):
                        consumed.add(fk)
            elif isinstance(v, str):
                if full in flat_values:
                    result[k] = flat_values[full]
                    consumed.add(full)
                if k.endswith("_other"):
                    leaf_stem = k[: -len("_other")]
                    full_stem = full[: -len("_other")]
                    for suf in PLURAL_SUFFIXES:
                        sib_full = f"{full_stem}{suf}"
                        sib_leaf = f"{leaf_stem}{suf}"
                        if sib_full in flat_values and sib_full not in consumed:
                            result[sib_leaf] = flat_values[sib_full]
                            consumed.add(sib_full)

    rem_prefix = f"{prefix}." if prefix else ""
    for fk in sorted(flat_values.keys()):
        if fk in consumed:
            continue
        if prefix and not fk.startswith(rem_prefix):
            continue
        rel = fk[len(rem_prefix) :] if prefix else fk
        parts = rel.split(".")
        cur = result
        for part in parts[:-1]:
            if part not in cur or not isinstance(cur[part], dict):
                cur[part] = {}
            cur = cur[part]
        cur[parts[-1]] = flat_values[fk]
        consumed.add(fk)

    return result


def split_plural_stem(key: str) -> Optional[Tuple[str, str]]:
    for suf in PLURAL_SUFFIXES:
        if key.endswith(suf) and len(key) > len(suf):
            return key[: -len(suf)], suf
    return None


def list_domain_files(folder: Path) -> List[str]:
    if not folder.is_dir():
        return []
    out: List[str] = []
    for p in sorted(folder.glob("*.json")):
        if p.name in SPECIAL_FILES or p.name.endswith("_todo.json") or p.name.startswith("_"):
            continue
        out.append(p.name)
    return out


def validate_names_overlay(names_path: Path) -> List[str]:
    issues: List[str] = []
    try:
        data = json.loads(names_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return [f"names.json: invalid JSON ({exc})"]
    if not isinstance(data, dict):
        return ["names.json: root must be a JSON object"]
    if "childNames" in data and "childrenNames" not in data:
        issues.append("names.json: use key 'childrenNames' (not 'childNames') to match NamePoolLoader")
    if "childrenNames" in data:
        cn = data["childrenNames"]
        if not isinstance(cn, dict):
            issues.append("names.json: 'childrenNames' must be an object mapping race -> {male, female}")
        else:
            for race, genders in cn.items():
                if not isinstance(genders, dict):
                    issues.append(f"names.json: childrenNames.{race} must be an object")
                    continue
                for g_key in ("male", "female"):
                    if g_key in genders:
                        arr = genders[g_key]
                        if not isinstance(arr, list) or not all(isinstance(x, str) and x.strip() for x in arr):
                            issues.append(f"names.json: childrenNames.{race}.{g_key} must be a non-empty string list")
                        else:
                            for idx, name_val in enumerate(arr):
                                bad = find_scaleform_unsafe_chars(name_val)
                                if bad:
                                    issues.append(
                                        f"names.json: childrenNames.{race}.{g_key}[{idx}] ({name_val!r}) "
                                        f"contains unrenderable Scaleform symbol(s) {', '.join(bad)}"
                                    )
    if "critterNames" in data:
        cr = data["critterNames"]
        if not isinstance(cr, dict):
            issues.append("names.json: 'critterNames' must be an object mapping species -> string[]")
        else:
            for species, arr in cr.items():
                if not isinstance(arr, list) or not all(isinstance(x, str) and x.strip() for x in arr):
                    issues.append(f"names.json: critterNames.{species} must be a non-empty string list")
                else:
                    for idx, name_val in enumerate(arr):
                        bad = find_scaleform_unsafe_chars(name_val)
                        if bad:
                            issues.append(
                                f"names.json: critterNames.{species}[{idx}] ({name_val!r}) "
                                f"contains unrenderable Scaleform symbol(s) {', '.join(bad)}"
                            )
    return issues


def validate_meta_file(meta_path: Path, expected_code: str) -> List[str]:
    issues: List[str] = []
    if not meta_path.is_file():
        return [f"_meta.json: missing file ({meta_path})"]
    try:
        data = json.loads(meta_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return [f"_meta.json: invalid JSON ({exc})"]
    if not isinstance(data, dict):
        return ["_meta.json: root must be a JSON object"]
    for req in ("code", "name", "nativeName"):
        if not isinstance(data.get(req), str) or not data[req].strip():
            issues.append(f"_meta.json: missing or empty string field '{req}'")
    if isinstance(data.get("code"), str) and data["code"].strip().lower() != expected_code.lower():
        issues.append(f"_meta.json: code '{data['code']}' does not match folder '{expected_code}'")
    return issues


def resolve_pack_json_dir(packs_dir: Path, lang: str) -> Path:
    loc_parent = packs_dir / lang / "SKSE" / "Plugins" / "MARAS" / "localization"
    direct = loc_parent / lang
    if direct.is_dir() or not loc_parent.is_dir():
        return direct
    lang_low = lang.lower()
    for cand in sorted(loc_parent.iterdir()):
        if cand.is_dir() and cand.name.lower() == lang_low:
            return cand
    return direct


def resolve_dsd_target_file(packs_dir: Path, lang: str, explicit_dsd_file: Optional[Path]) -> Path:
    if explicit_dsd_file is not None:
        return explicit_dsd_file
    dsd_folder = packs_dir / lang / "SKSE" / "Plugins" / "DynamicStringDistributor" / "TT_MARAS.esp"
    direct = dsd_folder / f"{lang}.json"
    if direct.is_file() or not dsd_folder.is_dir():
        return direct
    # Check case-insensitive or `<prefix>_<lang>.json` match if present
    lang_low = lang.lower()
    for candidate in sorted(dsd_folder.glob("*.json")):
        stem_low = candidate.stem.lower()
        if stem_low == lang_low or stem_low.endswith(f"_{lang_low}") or stem_low.endswith(f"-{lang_low}"):
            return candidate
    return direct


def audit_dsd(
    lang: str,
    dsd_template_path: Path,
    dsd_target_path: Path,
) -> Tuple[int, int, int, int, int, List[str], List[str], Dict[str, Any]]:
    """
    Audit DSD `<lang>.json` against `TT_MARAS_dsd_template.json`.
    Returns `(total_tmpl, translated_count, missing_count, changed_count, obsolete_count, errors, warnings, dsd_todo_dict)`.
    """
    if not dsd_template_path.is_file():
        return (0, 0, 0, 0, 0, [f"DSD template not found: {dsd_template_path}"], [], {})

    try:
        tmpl_list = json.loads(dsd_template_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return (0, 0, 0, 0, 0, [f"DSD template invalid JSON ({exc})"], [], {})

    if not isinstance(tmpl_list, list):
        return (0, 0, 0, 0, 0, ["DSD template root must be a JSON array"], [], {})

    tmpl_by_norm_key: Dict[str, Dict[str, Any]] = {}
    errors: List[str] = []
    warnings: List[str] = []

    for item in tmpl_list:
        if not isinstance(item, dict):
            continue
        schema_err = validate_dsd_entry_schema(item, "DSD template")
        if schema_err:
            errors.append(schema_err)
        nk = make_dsd_key(item, canonical_display=False)
        if nk in tmpl_by_norm_key:
            errors.append(f"DSD template duplicate entry key: {make_dsd_key(item, canonical_display=True)}")
        tmpl_by_norm_key[nk] = item

    total_tmpl = len(tmpl_by_norm_key)

    # For `--lang en`, self-validate the DSD template itself
    if lang.lower() == "en":
        return (total_tmpl, total_tmpl, 0, 0, 0, errors, warnings, {})

    tgt_by_norm_key: Dict[str, Dict[str, Any]] = {}
    if dsd_target_path.is_file():
        try:
            tgt_list = json.loads(dsd_target_path.read_text(encoding="utf-8"))
            if not isinstance(tgt_list, list):
                errors.append(f"{dsd_target_path.name}: DSD root must be a JSON array")
                return (total_tmpl, 0, total_tmpl, 0, 0, errors, warnings, {})
            for item in tgt_list:
                if isinstance(item, dict) and "form_id" in item and "type" in item:
                    schema_err = validate_dsd_entry_schema(item, f"DSD ({dsd_target_path.name})")
                    if schema_err:
                        errors.append(schema_err)
                    tgt_by_norm_key[make_dsd_key(item, canonical_display=False)] = item
        except Exception as exc:
            errors.append(f"{dsd_target_path}: invalid JSON ({exc})")
            return (total_tmpl, 0, total_tmpl, 0, 0, errors, warnings, {})

    translated_count = 0
    missing_count = 0
    changed_count = 0
    obsolete_count = 0
    dsd_todo: Dict[str, Any] = {}

    dragon_font_note = (
        "Uses Skyrim $DragonFont rune glyphs (supports uppercase ASCII A-Z only). "
        "Keep original uppercase ASCII text verbatim so Dragon runes render instead of "
        "missing-glyph boxes; translate 00004D BOOK FULL and 00004C Translated scroll instead."
    )

    for nk, tmpl_entry in tmpl_by_norm_key.items():
        disp_key = make_dsd_key(tmpl_entry, canonical_display=True)
        en_text = str(tmpl_entry.get("original", tmpl_entry.get("string", "")))
        editor_id = str(tmpl_entry.get("editor_id", ""))
        is_dragon_font = bool(DRAGON_FONT_RE.search(en_text))

        if nk not in tgt_by_norm_key:
            missing_count += 1
            entry_todo: Dict[str, Any] = {"status": "missing"}
            if editor_id:
                entry_todo["editor_id"] = editor_id
            entry_todo["en"] = en_text
            entry_todo["translation"] = en_text if is_dragon_font else ""
            if is_dragon_font:
                entry_todo["note"] = dragon_font_note
            dsd_todo[disp_key] = entry_todo
            continue

        tgt_entry = tgt_by_norm_key[nk]
        tr_text = str(tgt_entry.get("string", ""))
        if not tr_text.strip():
            missing_count += 1
            entry_todo = {"status": "missing"}
            if editor_id:
                entry_todo["editor_id"] = editor_id
            entry_todo["en"] = en_text
            entry_todo["translation"] = en_text if is_dragon_font else ""
            if is_dragon_font:
                entry_todo["note"] = dragon_font_note
            dsd_todo[disp_key] = entry_todo
            continue

        translated_count += 1

        # Detect changed English `.esp` text when translator's file preserved `"original"`
        tgt_orig = tgt_entry.get("original")
        if isinstance(tgt_orig, str) and tgt_orig != en_text:
            changed_count += 1
            entry_todo = {"status": "source_changed"}
            if editor_id:
                entry_todo["editor_id"] = editor_id
            entry_todo["previous_en"] = tgt_orig
            entry_todo["en"] = en_text
            entry_todo["current_translation"] = tr_text
            entry_todo["translation"] = en_text if is_dragon_font else ""
            if is_dragon_font:
                entry_todo["note"] = dragon_font_note
            dsd_todo[disp_key] = entry_todo

        # Validate Creation Kit `<Alias=...>` / `<mag>`, Scaleform `<font>` markup, `%.0f`, and `$DragonFont`
        en_ck_list, en_bk, en_pf = extract_dsd_tags(en_text)
        tr_ck_list, tr_bk, tr_pf = extract_dsd_tags(tr_text)
        en_ck = set(en_ck_list)
        tr_ck = set(tr_ck_list)

        unknown_ck = sorted(tr_ck - en_ck)
        if unknown_ck:
            errors.append(f"DSD '{disp_key}': unknown CK tag(s) {unknown_ck} (expected: {sorted(en_ck)})")

        unknown_pf = sorted(set(tr_pf) - set(en_pf))
        if unknown_pf:
            errors.append(f"DSD '{disp_key}': unknown format token(s) {unknown_pf} (expected: {sorted(set(en_pf))})")

        missing_pf = sorted(set(en_pf) - set(tr_pf))
        if missing_pf:
            warnings.append(f"DSD '{disp_key}': missing format token(s) {missing_pf} from English source")

        for mtag in sorted(en_ck - tr_ck):
            warnings.append(f"DSD '{disp_key}': missing CK tag '{mtag}' present in English source")

        if en_ck_list.count("<mag>") != tr_ck_list.count("<mag>"):
            warnings.append(
                f"DSD '{disp_key}': '<mag>' count changed ({en_ck_list.count('<mag>')} in EN vs {tr_ck_list.count('<mag>')})"
            )

        if sorted(t.lower() for t in en_bk) != sorted(t.lower() for t in tr_bk):
            errors.append(f"DSD '{disp_key}': book markup tag mismatch (EN={en_bk}, translation={tr_bk})")

        if is_dragon_font:
            inner = HTML_BOOK_TAG_RE.sub("", tr_text)
            non_ascii = sorted({ch for ch in inner if ord(ch) > 127})
            if non_ascii:
                errors.append(
                    f"DSD '{disp_key}': $DragonFont text contains non-ASCII character(s) {non_ascii[:8]} "
                    "(Skyrim $DragonFont only supports uppercase ASCII A-Z; keep Dragon runes verbatim)"
                )

        unsafe_chars = find_scaleform_unsafe_chars(tr_text)
        if unsafe_chars:
            errors.append(
                f"DSD '{disp_key}': unrenderable Skyrim Scaleform symbol(s) {', '.join(unsafe_chars)} "
                "(replace long dashes with '-', '...' glyph with '...', smart/angle quotes with '\"' / \"'\")"
            )

    for nk, tgt_entry in tgt_by_norm_key.items():
        if nk not in tmpl_by_norm_key:
            obsolete_count += 1

    return (
        total_tmpl,
        translated_count,
        missing_count,
        changed_count,
        obsolete_count,
        errors,
        warnings,
        dsd_todo,
    )


def apply_todo_file(
    todo_path: Path,
    en_dir: Path,
    target_dir: Path,
    dsd_template_path: Path,
    dsd_target_path: Path,
    prune_obsolete: bool,
    include_json: bool,
    include_dsd: bool,
) -> int:
    if not todo_path.is_file():
        print(f"[ERROR] Todo file not found: {todo_path}", file=sys.stderr)
        return 1

    todo_doc = json.loads(todo_path.read_text(encoding="utf-8"))
    if not isinstance(todo_doc, dict):
        print("[ERROR] Invalid todo JSON structure: expected root object", file=sys.stderr)
        return 1

    updated_json_files = 0
    applied_json_keys = 0

    files_section = todo_doc.get("files")
    if files_section is None and "dsd" not in todo_doc:
        files_section = todo_doc

    if include_json and isinstance(files_section, dict):
        target_dir.mkdir(parents=True, exist_ok=True)
        for fname, entries in sorted(files_section.items()):
            if not isinstance(entries, dict):
                continue
            en_file = en_dir / fname
            base_tree = json.loads(en_file.read_text(encoding="utf-8")) if en_file.is_file() else {}
            base_flat = flatten_json(base_tree)
            base_plural_stems = {
                split_plural_stem(k)[0] for k in base_flat if split_plural_stem(k) is not None  # type: ignore[index]
            }

            dst_file = target_dir / fname
            existing_flat = (
                flatten_json(json.loads(dst_file.read_text(encoding="utf-8"))) if dst_file.is_file() else {}
            )

            file_applied = 0
            for key, item in entries.items():
                if isinstance(item, str):
                    if item.strip():
                        val = sanitize_scaleform_text(item) if is_scaleform_json_key(fname, key) else item
                        existing_flat[key] = val
                        file_applied += 1
                elif isinstance(item, dict):
                    tr_val = item.get("translation")
                    if isinstance(tr_val, str) and tr_val.strip():
                        val = sanitize_scaleform_text(tr_val) if is_scaleform_json_key(fname, key) else tr_val
                        existing_flat[key] = val
                        file_applied += 1

            if prune_obsolete and base_flat:
                pruned: Dict[str, str] = {}
                for k, v in existing_flat.items():
                    if k in base_flat:
                        pruned[k] = v
                        continue
                    sp = split_plural_stem(k)
                    if sp and sp[0] in base_plural_stems:
                        pruned[k] = v
                existing_flat = pruned

            if file_applied > 0 or prune_obsolete or not dst_file.is_file():
                if existing_flat:
                    rebuilt = unflatten_aligned_to_base(existing_flat, base_tree)
                    dst_file.write_text(
                        json.dumps(rebuilt, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8",
                        newline="\n",
                    )
                    updated_json_files += 1
                    applied_json_keys += file_applied
                    print(f"[OK] Updated {dst_file} (+{file_applied} applied, {len(existing_flat)} total keys)")

    dsd_section = todo_doc.get("dsd")
    applied_dsd_keys = 0
    if include_dsd and isinstance(dsd_section, dict) and dsd_template_path.is_file():
        tmpl_list = json.loads(dsd_template_path.read_text(encoding="utf-8"))
        existing_by_nk: Dict[str, Dict[str, Any]] = {}
        if dsd_target_path.is_file():
            for item in json.loads(dsd_target_path.read_text(encoding="utf-8")):
                if isinstance(item, dict) and "form_id" in item and "type" in item:
                    existing_by_nk[make_dsd_key(item, canonical_display=False)] = item

        # Index new translations from `dsd_section` by normalized DSD key
        new_dsd_tr: Dict[str, str] = {}
        for raw_key, item in dsd_section.items():
            parts = str(raw_key).split("|")
            if len(parts) < 3:
                continue
            fake_obj: Dict[str, Any] = {"form_id": f"{parts[0]}|{parts[1]}", "type": parts[2]}
            if len(parts) >= 4 and parts[3].strip() != "":
                fake_obj["index"] = int(parts[3])
            nk = make_dsd_key(fake_obj, canonical_display=False)

            tr_str = ""
            if isinstance(item, str):
                tr_str = item
            elif isinstance(item, dict) and isinstance(item.get("translation"), str):
                tr_str = item["translation"]
            if tr_str.strip():
                new_dsd_tr[nk] = sanitize_scaleform_text(tr_str)

        merged_dsd_list: List[Dict[str, Any]] = []
        seen_nks: Set[str] = set()
        for tmpl_entry in tmpl_list:
            if not isinstance(tmpl_entry, dict):
                continue
            nk = make_dsd_key(tmpl_entry, canonical_display=False)
            seen_nks.add(nk)
            if nk in new_dsd_tr:
                out_entry = dict(tmpl_entry)
                out_entry["string"] = new_dsd_tr[nk]
                merged_dsd_list.append(out_entry)
                applied_dsd_keys += 1
            elif nk in existing_by_nk:
                prev_entry = existing_by_nk[nk]
                if str(prev_entry.get("string", "")).strip():
                    out_entry = dict(tmpl_entry)
                    # Preserve previous `original` if translator hasn't re-translated a changed English string yet
                    if isinstance(prev_entry.get("original"), str):
                        out_entry["original"] = prev_entry["original"]
                    out_entry["string"] = prev_entry["string"]
                    merged_dsd_list.append(out_entry)

        if not prune_obsolete:
            for nk, prev_entry in existing_by_nk.items():
                if nk not in seen_nks:
                    merged_dsd_list.append(prev_entry)

        if applied_dsd_keys > 0 or (prune_obsolete and dsd_target_path.is_file()):
            dsd_target_path.parent.mkdir(parents=True, exist_ok=True)
            dsd_target_path.write_text(
                json.dumps(merged_dsd_list, indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
                newline="\n",
            )
            print(
                f"[OK] Updated DSD {dsd_target_path} "
                f"(+{applied_dsd_keys} applied, {len(merged_dsd_list)} total DSD entries)"
            )

    # Remove applied entries from `todo_path` (or delete `todo_path` if 0 pending items remain)
    if applied_json_keys > 0 or applied_dsd_keys > 0:
        remaining_total = 0
        if include_json and isinstance(todo_doc.get("files"), dict):
            cleaned_files: Dict[str, Dict[str, Any]] = {}
            for fname, entries in todo_doc["files"].items():
                if not isinstance(entries, dict):
                    continue
                rem_entries: Dict[str, Any] = {}
                for key, item in entries.items():
                    tr_val = item if isinstance(item, str) else (item.get("translation", "") if isinstance(item, dict) else "")
                    if not (isinstance(tr_val, str) and tr_val.strip()):
                        rem_entries[key] = item
                if rem_entries:
                    cleaned_files[fname] = rem_entries
                    remaining_total += len(rem_entries)
            todo_doc["files"] = cleaned_files
        elif isinstance(todo_doc.get("files"), dict):
            remaining_total += sum(len(v) for v in todo_doc["files"].values() if isinstance(v, dict))

        if include_dsd and isinstance(todo_doc.get("dsd"), dict):
            rem_dsd: Dict[str, Any] = {}
            for dk, item in todo_doc["dsd"].items():
                tr_val = item if isinstance(item, str) else (item.get("translation", "") if isinstance(item, dict) else "")
                if not (isinstance(tr_val, str) and tr_val.strip()):
                    rem_dsd[dk] = item
            todo_doc["dsd"] = rem_dsd
            remaining_total += len(rem_dsd)
        elif isinstance(todo_doc.get("dsd"), dict):
            remaining_total += len(todo_doc["dsd"])

        if remaining_total == 0 and ("files" in todo_doc or "dsd" in todo_doc):
            todo_path.unlink(missing_ok=True)
            print(f"[CLEAN] All pending entries applied; removed {todo_path}")
        elif "files" in todo_doc or "dsd" in todo_doc:
            todo_path.write_text(json.dumps(todo_doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
            print(f"[TODO] {remaining_total} untranslated entry/entries remain in {todo_path}")

    print(
        f"[DONE] Applied {applied_json_keys} JSON translation(s) across {updated_json_files} file(s) "
        f"and {applied_dsd_keys} DSD translation(s)."
    )
    return 0


def sanitize_existing_pack(
    en_dir: Path,
    target_dir: Path,
    dsd_target_path: Path,
    include_json: bool,
    include_dsd: bool,
) -> int:
    changed_files = 0
    changed_entries = 0

    if include_json and target_dir.is_dir():
        for p in sorted(target_dir.glob("*.json")):
            low = p.name.lower()
            if low.startswith("_") or low.endswith("_todo.json") or low.endswith(".todo.json"):
                continue

            if low == "names.json":
                try:
                    data = json.loads(p.read_text(encoding="utf-8"))
                except Exception as exc:
                    print(f"[ERROR] Cannot parse {p}: {exc}", file=sys.stderr)
                    return 1
                file_changes = 0
                if isinstance(data, dict):
                    if isinstance(data.get("childrenNames"), dict):
                        for race, genders in data["childrenNames"].items():
                            if not isinstance(genders, dict):
                                continue
                            for g_key in ("male", "female"):
                                if isinstance(genders.get(g_key), list):
                                    new_list = []
                                    for x in genders[g_key]:
                                        nx = sanitize_scaleform_text(x) if isinstance(x, str) else x
                                        if nx != x:
                                            file_changes += 1
                                        new_list.append(nx)
                                    genders[g_key] = new_list
                    if isinstance(data.get("critterNames"), dict):
                        for sp, arr in data["critterNames"].items():
                            if isinstance(arr, list):
                                new_list = []
                                for x in arr:
                                    nx = sanitize_scaleform_text(x) if isinstance(x, str) else x
                                    if nx != x:
                                        file_changes += 1
                                    new_list.append(nx)
                                data["critterNames"][sp] = new_list
                if file_changes > 0:
                    p.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
                    changed_files += 1
                    changed_entries += file_changes
                    print(f"[SANITIZE] {p.name}: normalized {file_changes} name(s)")
                continue

            try:
                tree = json.loads(p.read_text(encoding="utf-8"))
            except Exception as exc:
                print(f"[ERROR] Cannot parse {p}: {exc}", file=sys.stderr)
                return 1
            if not isinstance(tree, dict):
                continue

            flat = flatten_json(tree)
            file_changes = 0
            sanitized_flat: Dict[str, str] = {}
            for k, v in flat.items():
                nv = sanitize_scaleform_text(v) if is_scaleform_json_key(p.name, k) else sanitize_ui_text(v)
                if nv != v:
                    file_changes += 1
                sanitized_flat[k] = nv

            if file_changes > 0:
                en_file = en_dir / p.name
                base_tree = json.loads(en_file.read_text(encoding="utf-8")) if en_file.is_file() else tree
                rebuilt = unflatten_aligned_to_base(sanitized_flat, base_tree)
                p.write_text(json.dumps(rebuilt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
                changed_files += 1
                changed_entries += file_changes
                print(f"[SANITIZE] {p.name}: normalized {file_changes} string(s)")

    if include_dsd and dsd_target_path.is_file():
        try:
            dsd_list = json.loads(dsd_target_path.read_text(encoding="utf-8"))
        except Exception as exc:
            print(f"[ERROR] Cannot parse {dsd_target_path}: {exc}", file=sys.stderr)
            return 1
        if isinstance(dsd_list, list):
            dsd_changes = 0
            for item in dsd_list:
                if isinstance(item, dict) and isinstance(item.get("string"), str):
                    ns = sanitize_scaleform_text(item["string"])
                    if ns != item["string"]:
                        item["string"] = ns
                        dsd_changes += 1
            if dsd_changes > 0:
                dsd_target_path.write_text(
                    json.dumps(dsd_list, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )
                changed_files += 1
                changed_entries += dsd_changes
                print(f"[SANITIZE] {dsd_target_path.name}: normalized {dsd_changes} DSD entry/entries")

    print(f"[DONE] Sanitized {changed_entries} entry/entries across {changed_files} file(s).")
    return 0


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description="Audit and synchronize M.A.R.A.S localization JSON packs and TT_MARAS.esp DSD files."
    )
    parser.add_argument("--lang", required=True, help="Language folder code to audit/update (e.g. 'de', 'ru', 'en').")
    parser.add_argument(
        "--en-dir",
        type=Path,
        default=repo_root / "SKSE" / "Plugins" / "MARAS" / "localization" / "en",
        help="Base English localization directory ('en/').",
    )
    parser.add_argument(
        "--packs-dir",
        type=Path,
        default=repo_root / "localization" / "packs",
        help="Root directory containing non-English '<lang>/' output packs.",
    )
    parser.add_argument(
        "--dsd-template",
        type=Path,
        default=repo_root / "localization" / "scripts" / "TT_MARAS_dsd_template.json",
        help="Path to English DSD template JSON exported from .spriggit/TT_MARAS.esp/.",
    )
    parser.add_argument(
        "--dsd-file",
        type=Path,
        default=None,
        help="Explicit path to target DSD JSON (default: localization/packs/<lang>/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json).",
    )
    parser.add_argument(
        "--no-dsd",
        action="store_true",
        help="Skip auditing/syncing Dynamic String Distributor (TT_MARAS.esp) entries.",
    )
    parser.add_argument(
        "--dsd-only",
        action="store_true",
        help="Only audit/sync Dynamic String Distributor (TT_MARAS.esp) entries.",
    )
    parser.add_argument(
        "--prev-en",
        type=Path,
        default=None,
        help="Optional path to previous release's 'en/' folder to detect changed English JSON source strings.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Read-only status check: do not write _todo.json automatically.",
    )
    parser.add_argument(
        "--extract-todo",
        nargs="?",
        const="__AUTO__",
        default=None,
        help="Explicitly write pending entries to a delta JSON file (default: <packs-dir>/<lang>/_todo.json).",
    )
    parser.add_argument(
        "--apply-todo",
        nargs="?",
        const="__AUTO__",
        default=None,
        help="Merge translated entries from _todo.json (default: <packs-dir>/<lang>/_todo.json) into JSON & DSD files.",
    )
    parser.add_argument(
        "--prune-obsolete",
        action="store_true",
        help="When used with --apply-todo, drop keys/entries that no longer exist in English templates.",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Read-only strict check: exit with code 1 if any missing, changed, or warning/error entries exist.",
    )
    parser.add_argument(
        "--sanitize-existing",
        action="store_true",
        help="Normalize LLM typography / Scaleform-unsafe symbols across existing <lang> JSON and DSD files in place.",
    )
    args = parser.parse_args()

    include_json = not args.dsd_only
    include_dsd = not args.no_dsd

    en_dir = args.en_dir
    if include_json and not en_dir.is_dir():
        print(f"[ERROR] Base English directory not found: {en_dir}", file=sys.stderr)
        return 1

    is_en = args.lang.lower() == "en"
    pack_root = args.packs_dir / args.lang
    target_dir = en_dir if is_en else resolve_pack_json_dir(args.packs_dir, args.lang)
    default_todo_path = (en_dir / "_todo.json") if is_en else (pack_root / "_todo.json")
    dsd_target_path = resolve_dsd_target_file(args.packs_dir, args.lang, args.dsd_file)

    if args.sanitize_existing:
        if args.lang.lower() == "en":
            print("[ERROR] --sanitize-existing targets non-English translation packs.", file=sys.stderr)
            return 1
        return sanitize_existing_pack(
            en_dir=en_dir,
            target_dir=target_dir,
            dsd_target_path=dsd_target_path,
            include_json=include_json,
            include_dsd=include_dsd,
        )

    if args.apply_todo is not None:
        todo_in = default_todo_path if args.apply_todo == "__AUTO__" else Path(args.apply_todo)
        return apply_todo_file(
            todo_path=todo_in,
            en_dir=en_dir,
            target_dir=target_dir,
            dsd_template_path=args.dsd_template,
            dsd_target_path=dsd_target_path,
            prune_obsolete=args.prune_obsolete,
            include_json=include_json,
            include_dsd=include_dsd,
        )

    # -------------------------
    # 1. Audit MARAS JSON Pack
    # -------------------------
    meta_issues: List[str] = []
    names_issues: List[str] = []
    extra_domain_files: List[str] = []
    total_en_keys = 0
    total_translated = 0
    missing_by_file: Dict[str, List[str]] = {}
    changed_by_file: Dict[str, List[Tuple[str, str, str]]] = {}
    obsolete_by_file: Dict[str, List[str]] = {}
    placeholder_errors: List[str] = []
    placeholder_warnings: List[str] = []
    todo_files_payload: Dict[str, Dict[str, Any]] = {}
    en_files: List[str] = []

    if include_json:
        if not target_dir.is_dir():
            print(f"[WARN] Language directory does not exist yet: {target_dir}")

        meta_issues = validate_meta_file(target_dir / "_meta.json", args.lang)
        names_issues = (
            validate_names_overlay(target_dir / "names.json") if (target_dir / "names.json").is_file() else []
        )

        en_files = list_domain_files(en_dir)
        target_files = set(list_domain_files(target_dir))
        extra_domain_files = sorted(target_files - set(en_files))

        for fname in en_files:
            en_tree = json.loads((en_dir / fname).read_text(encoding="utf-8"))
            en_flat = flatten_json(en_tree)
            total_en_keys += len(en_flat)

            prev_en_flat: Dict[str, str] = {}
            if args.prev_en and (args.prev_en / fname).is_file():
                prev_en_flat = flatten_json(json.loads((args.prev_en / fname).read_text(encoding="utf-8")))

            tgt_path = target_dir / fname
            tgt_flat: Dict[str, str] = {}
            if tgt_path.is_file():
                try:
                    tgt_flat = flatten_json(json.loads(tgt_path.read_text(encoding="utf-8")))
                except Exception as exc:
                    placeholder_errors.append(f"{fname}: invalid JSON ({exc})")
                    continue

            plural_stems: Dict[str, str] = {}
            for ek in en_flat:
                sp = split_plural_stem(ek)
                if sp is not None:
                    stem, _ = sp
                    ref_key = f"{stem}_other" if f"{stem}_other" in en_flat else ek
                    plural_stems[stem] = ref_key

            for ek, en_val in en_flat.items():
                sp = split_plural_stem(ek)
                if ek not in tgt_flat:
                    if sp is not None and sp[1] == "_one" and f"{sp[0]}_other" in tgt_flat:
                        total_translated += 1
                        continue
                    missing_by_file.setdefault(fname, []).append(ek)
                    todo_files_payload.setdefault(fname, {})[ek] = {
                        "status": "missing",
                        "en": en_val,
                        "translation": "",
                    }
                else:
                    total_translated += 1
                    if prev_en_flat and ek in prev_en_flat and prev_en_flat[ek] != en_val:
                        changed_by_file.setdefault(fname, []).append((ek, prev_en_flat[ek], en_val))
                        todo_files_payload.setdefault(fname, {})[ek] = {
                            "status": "source_changed",
                            "previous_en": prev_en_flat[ek],
                            "en": en_val,
                            "current_translation": tgt_flat[ek],
                            "translation": "",
                        }

            for tk, tr_val in tgt_flat.items():
                ref_en_val: Optional[str] = en_flat.get(tk)
                is_plural_form = False
                sp = split_plural_stem(tk)
                if sp is not None:
                    is_plural_form = True
                    if ref_en_val is None and sp[0] in plural_stems:
                        ref_en_val = en_flat[plural_stems[sp[0]]]

                if ref_en_val is None:
                    obsolete_by_file.setdefault(fname, []).append(tk)
                    continue

                en_tokens = extract_placeholders(ref_en_val)
                tr_tokens = extract_placeholders(tr_val)

                unknown_tokens = sorted(tr_tokens - en_tokens)
                if unknown_tokens:
                    filtered_unknown = [tok for tok in unknown_tokens if not (is_plural_form and tok == "count")]
                    if filtered_unknown:
                        placeholder_errors.append(
                            f"{fname} -> '{tk}': unknown placeholder(s) {filtered_unknown} (allowed: {sorted(en_tokens)})"
                        )

                missing_tokens = sorted(en_tokens - tr_tokens)
                for mtok in missing_tokens:
                    if is_plural_form and mtok == "count" and sp is not None and sp[1] in ("_zero", "_one", "_two"):
                        continue
                    placeholder_warnings.append(
                        f"{fname} -> '{tk}': missing placeholder '{{{mtok}}}' present in English source"
                    )

                if fname == "catalogs.json" and tk.startswith("bonuses.") and tk.endswith(".description"):
                    stripped_tr = tr_val.rstrip()
                    if stripped_tr.endswith((".", "。", "!", "！")):
                        placeholder_warnings.append(
                            f"{fname} -> '{tk}': ends with sentence punctuation '{stripped_tr[-1]}', "
                            "but BuffCard appends ' 15%' inline directly after description"
                        )

                if is_scaleform_json_key(fname, tk):
                    unsafe = find_scaleform_unsafe_chars(tr_val)
                    if unsafe:
                        placeholder_errors.append(
                            f"{fname} -> '{tk}': unrenderable Skyrim Scaleform symbol(s) {', '.join(unsafe)} "
                            "(replace long dashes with '-', '...' glyph with '...', smart/angle quotes with '\"' / \"'\")"
                        )
                else:
                    artifacts = find_universal_llm_artifacts(tr_val, ref_en_val)
                    if artifacts:
                        placeholder_errors.append(
                            f"{fname} -> '{tk}': unexpected LLM typography/whitespace artifact(s) {', '.join(artifacts)}"
                        )

    # -------------------------
    # 2. Audit DSD (TT_MARAS.esp)
    # -------------------------
    dsd_total = 0
    dsd_translated = 0
    dsd_missing = 0
    dsd_changed = 0
    dsd_obsolete = 0
    dsd_errors: List[str] = []
    dsd_warnings: List[str] = []
    dsd_todo_payload: Dict[str, Any] = {}

    if include_dsd and args.dsd_template.is_file():
        (
            dsd_total,
            dsd_translated,
            dsd_missing,
            dsd_changed,
            dsd_obsolete,
            dsd_errors,
            dsd_warnings,
            dsd_todo_payload,
        ) = audit_dsd(args.lang, args.dsd_template, dsd_target_path)

    # -------------------------
    # 3. Print Report
    # -------------------------
    missing_json_count = sum(len(v) for v in missing_by_file.values())
    changed_json_count = sum(len(v) for v in changed_by_file.values())
    obsolete_json_count = sum(len(v) for v in obsolete_by_file.values())

    print(f"=== M.A.R.A.S Localization Audit [{args.lang}] ===")
    if include_json:
        pct = (100.0 * total_translated / total_en_keys) if total_en_keys else 100.0
        print(
            f"JSON Pack Coverage: {total_translated}/{total_en_keys} keys ({pct:.1f}%) across {len(en_files)} domain files"
        )
        print(f"  Missing keys    : {missing_json_count}")
        if args.prev_en:
            print(f"  Changed EN      : {changed_json_count}")
        print(f"  Obsolete keys   : {obsolete_json_count}")
        print(f"  Placeholder err : {len(placeholder_errors)} (warnings: {len(placeholder_warnings)})")

    if include_dsd and dsd_total > 0:
        dsd_pct = 100.0 * dsd_translated / dsd_total
        dsd_label = (
            f"template verified ({dsd_total} entries)"
            if args.lang.lower() == "en"
            else (str(dsd_target_path) if dsd_target_path.is_file() else f"{dsd_target_path} (not created yet)")
        )
        print(f"DSD ESP Coverage  : {dsd_translated}/{dsd_total} entries ({dsd_pct:.1f}%) [{dsd_label}]")
        if args.lang.lower() != "en":
            print(f"  Missing DSD     : {dsd_missing}")
            print(f"  Changed ESP EN  : {dsd_changed}")
            print(f"  Obsolete DSD    : {dsd_obsolete}")
            print(f"  CK/Format err   : {len(dsd_errors)} (warnings: {len(dsd_warnings)})")

    for issue in meta_issues + names_issues:
        print(f"  [META/NAMES] {issue}")
    for extra in extra_domain_files:
        print(f"  [EXTRA FILE] {extra} (not present in en/)")
    for fname, keys in missing_by_file.items():
        preview = ", ".join(keys[:5]) + (f" ... (+{len(keys) - 5} more)" if len(keys) > 5 else "")
        print(f"  [MISSING JSON] {fname} ({len(keys)}): {preview}")
    for fname, items in changed_by_file.items():
        print(f"  [CHANGED JSON] {fname} ({len(items)}): {', '.join(k for k, _, _ in items[:5])}")
    for fname, keys in obsolete_by_file.items():
        preview = ", ".join(keys[:5]) + (f" ... (+{len(keys) - 5} more)" if len(keys) > 5 else "")
        print(f"  [OBSOLETE JSON] {fname} ({len(keys)}): {preview}")
    for err in placeholder_errors + dsd_errors:
        print(f"  [ERROR] {err}")
    for warn in (placeholder_warnings + dsd_warnings)[:20]:
        print(f"  [WARN]  {warn}")

    # -------------------------
    # 4. Write `_todo.json` (Automatic by default when pending entries exist, unless `--check` / `--strict`)
    # -------------------------
    pending_json_total = sum(len(v) for v in todo_files_payload.values())
    pending_dsd_total = len(dsd_todo_payload)
    pending_total = pending_json_total + pending_dsd_total

    should_write_todo = False
    if args.extract_todo is not None:
        should_write_todo = True
    elif not args.check and not args.strict and args.lang.lower() != "en" and pending_total > 0:
        should_write_todo = True

    if should_write_todo:
        out_path = (
            default_todo_path
            if (args.extract_todo is None or args.extract_todo == "__AUTO__")
            else Path(args.extract_todo)
        )
        # Preserve any in-progress non-empty `translation` strings already typed in `out_path`
        if out_path.is_file():
            try:
                prev_todo = json.loads(out_path.read_text(encoding="utf-8"))
                if isinstance(prev_todo, dict):
                    prev_files = prev_todo.get("files", {})
                    if isinstance(prev_files, dict):
                        for fname, fdict in todo_files_payload.items():
                            pf = prev_files.get(fname, {})
                            if isinstance(pf, dict):
                                for k, entry in fdict.items():
                                    old_item = pf.get(k)
                                    old_tr = (
                                        old_item
                                        if isinstance(old_item, str)
                                        else (old_item.get("translation", "") if isinstance(old_item, dict) else "")
                                    )
                                    if isinstance(old_tr, str) and old_tr.strip():
                                        entry["translation"] = old_tr
                    prev_dsd = prev_todo.get("dsd", {})
                    if isinstance(prev_dsd, dict):
                        for dk, entry in dsd_todo_payload.items():
                            old_item = prev_dsd.get(dk)
                            old_tr = (
                                old_item
                                if isinstance(old_item, str)
                                else (old_item.get("translation", "") if isinstance(old_item, dict) else "")
                            )
                            if isinstance(old_tr, str) and old_tr.strip():
                                entry["translation"] = old_tr
            except Exception:
                pass

        out_path.parent.mkdir(parents=True, exist_ok=True)
        apply_cmd = f"python localization/scripts/sync_localization.py --lang {args.lang} --apply-todo"
        if out_path != default_todo_path:
            apply_cmd += f" {out_path.as_posix()}"

        doc: Dict[str, Any] = {
            "_instructions": (
                f"Fill in 'translation' for any entries below, then run: `{apply_cmd}` "
                f"(merges JSON packs into localization/packs/{args.lang}/SKSE/Plugins/MARAS/localization/{args.lang}/ "
                f"and DSD entries into localization/packs/{args.lang}/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/{args.lang}.json)."
            ),
            "language": args.lang,
        }
        if include_json:
            doc["files"] = todo_files_payload
        if include_dsd and args.lang.lower() != "en":
            doc["dsd"] = dsd_todo_payload

        out_path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
        print(
            f"[TODO] Wrote {pending_total} pending entry/entries "
            f"({pending_json_total} JSON + {pending_dsd_total} DSD) to {out_path}"
        )

    has_hard_errors = bool(meta_issues or names_issues or placeholder_errors or dsd_errors)
    if has_hard_errors:
        return 1
    if args.strict and (
        missing_json_count > 0
        or changed_json_count > 0
        or dsd_missing > 0
        or dsd_changed > 0
        or placeholder_warnings
        or dsd_warnings
    ):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
