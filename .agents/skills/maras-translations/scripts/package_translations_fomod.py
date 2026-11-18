#!/usr/bin/env python3
"""
M.A.R.A.S Multi-Language FOMOD Translation Packager.

Discovers completed non-English translation packs in:
  - `SKSE/Plugins/MARAS/localization/<lang>/`
  - `SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json`

Validates JSON syntax, `{named}` placeholders, CLDR plural stems, Scaleform-safe
punctuation, Creation Kit DSD tags, and `$DragonFont` ASCII preservation, then builds
a single MO2/Vortex-compatible `SelectExactlyOne` FOMOD archive
(`MARAS_Translations_<version>.zip`).

100% standalone (Python 3.10+ standard library only; zero dependency on `tools/`).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple
from xml.sax.saxutils import escape as xml_escape


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_REPO_ROOT = SCRIPT_DIR.parents[3]

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

KNOWN_LANGUAGES: Dict[str, Tuple[str, str, str, List[str]]] = {
    "de": ("de", "German", "Deutsch", ["GERMAN"]),
    "fr": ("fr", "French", "Français", ["FRENCH"]),
    "es": ("es", "Spanish", "Español", ["SPANISH"]),
    "it": ("it", "Italian", "Italiano", ["ITALIAN"]),
    "ru": ("ru", "Russian", "Русский", ["RUSSIAN"]),
    "pl": ("pl", "Polish", "Polski", ["POLISH"]),
    "uk": ("uk", "Ukrainian", "Українська", ["UKRAINIAN"]),
    "cs": ("cs", "Czech", "Čeština", ["CZECH"]),
    "tr": ("tr", "Turkish", "Türkçe", ["TURKISH"]),
    "pt-br": ("pt-BR", "Portuguese (Brazil)", "Português (Brasil)", ["PORTUGUESE", "BRAZILIAN"]),
    "ja": ("ja", "Japanese", "日本語", ["JAPANESE"]),
    "ko": ("ko", "Korean", "한국어", ["KOREAN"]),
    "zh-cn": ("zh-CN", "Simplified Chinese", "简体中文", ["CHINESE"]),
    "zh-tw": ("zh-TW", "Traditional Chinese", "繁體中文", ["TRADITIONAL CHINESE"]),
}

SCALEFORM_UNSAFE_CHARS: Set[str] = {
    "\u2010", "\u2011", "\u2012", "\u2013", "\u2014", "\u2015", "\u2212", "\uFE58", "\uFE63", "\uFF0D",
    "\u2026", "\u22EF", "\uFE19",
    "\u00AB", "\u00BB", "\u201C", "\u201D", "\u201E", "\u201F", "\u2033", "\u2036", "\u301D", "\u301E", "\uFF02",
    "\u2018", "\u2019", "\u201A", "\u201B", "\u02BC", "\u02B9", "\u2032", "\u2035", "\uFF07",
    "\u00A0", "\u202F", "\u2000", "\u2001", "\u2002", "\u2003", "\u2004", "\u2005", "\u2006", "\u2007", "\u2008",
    "\u2009", "\u200A", "\u205F",
    "\u200B", "\u200C", "\u200D", "\u2060", "\uFEFF",
    "\u2022", "\u2023", "\u25E6", "\u2043", "\u00B7",
    "\u2192", "\u2794", "\u279C", "\u2190", "\u2194",
    "\u00D7", "\u2248", "\u2264", "\u2265", "\u2260", "\u2116",
}


@dataclass
class LanguagePack:
    code: str
    name: str
    native_name: str
    author: str
    lang_dir: Path
    json_files: List[Path]
    has_names: bool
    dsd_file: Optional[Path]
    json_done: int
    json_total: int
    dsd_done: int
    dsd_total: Optional[int]


def xml_attr(value: str) -> str:
    return xml_escape(value, {'"': "&quot;"})


def is_scaleform_json_key(fname: str, key: str) -> bool:
    return fname == "hud_and_modals.json" and key.startswith(("hud.", "pet.", "common.", "mcm."))


def find_scaleform_unsafe_chars(text: str) -> List[str]:
    seen: Set[str] = set()
    found: List[str] = []
    for ch in text:
        if ch in SCALEFORM_UNSAFE_CHARS or ord(ch) > 0xFFFF:
            if ch not in seen:
                seen.add(ch)
                if ch.isprintable() and not ch.isspace():
                    found.append(f"U+{ord(ch):04X} ('{ch}')")
                else:
                    found.append(f"U+{ord(ch):04X}")
    return found


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


def split_plural_stem(key: str) -> Optional[Tuple[str, str]]:
    for suf in PLURAL_SUFFIXES:
        if key.endswith(suf) and len(key) > len(suf):
            return key[: -len(suf)], suf
    return None


def extract_placeholders(template: str) -> Set[str]:
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


def extract_dsd_tags(text: str) -> Tuple[List[str], List[str], List[str]]:
    return CK_TAG_RE.findall(text), HTML_BOOK_TAG_RE.findall(text), PRINTF_TAG_RE.findall(text)


def normalize_dsd_form_id(raw_form_id: str) -> str:
    parts = raw_form_id.strip().split("|", 1)
    if len(parts) != 2:
        return raw_form_id.strip().lower()
    hex_part = parts[0].strip()
    if hex_part.lower().startswith("0x"):
        hex_part = hex_part[2:]
    try:
        norm_hex = f"{int(hex_part, 16):06X}"
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


def validate_dsd_entry_schema(entry: Dict[str, Any], source_label: str) -> Optional[str]:
    disp_key = make_dsd_key(entry, canonical_display=True)
    raw_fid = entry.get("form_id")
    if not isinstance(raw_fid, str) or "|tt_maras.esp" not in raw_fid.strip().lower():
        return f"{source_label} '{disp_key}': 'form_id' must reference '|TT_MARAS.esp'"

    raw_type = entry.get("type")
    if not isinstance(raw_type, str) or not raw_type.strip():
        return f"{source_label} '{disp_key}': missing or empty 'type'"
    norm_type = raw_type.strip().upper()
    if norm_type not in VALID_DSD_TYPES:
        hint = " (use 'MGEF DNAM' for Magic Effect descriptions)" if norm_type == "MGEF DESC" else ""
        return f"{source_label} '{disp_key}': unsupported DSD type '{norm_type}'{hint}"

    index_val = entry.get("index")
    parsed_idx: Optional[int] = None
    if index_val is not None:
        if isinstance(index_val, bool) or not isinstance(index_val, int):
            return f"{source_label} '{disp_key}': 'index' must be an integer (got {index_val!r})"
        parsed_idx = index_val

    if norm_type == "INFO NAM1":
        if parsed_idx is None or parsed_idx < 1:
            return (
                f"{source_label} '{disp_key}': 'INFO NAM1' requires 1-based ResponseNumber 'index' >= 1 "
                f"(got {index_val!r})"
            )
    elif norm_type in ("INFO RNAM", "BOOK CNAM"):
        if parsed_idx is not None and parsed_idx != 0:
            return f"{source_label} '{disp_key}': '{norm_type}' must not set non-zero index (got {parsed_idx})"
    elif norm_type in DSD_ZERO_BASED_INDEX_TYPES:
        if parsed_idx is None or parsed_idx < 0:
            return f"{source_label} '{disp_key}': '{norm_type}' requires non-negative integer 'index' (got {index_val!r})"
    elif norm_type == "QUST CNAM" and parsed_idx is not None and parsed_idx < 0:
        return f"{source_label} '{disp_key}': 'QUST CNAM' index must be >= 0 (got {parsed_idx})"

    if not isinstance(entry.get("string"), str):
        return f"{source_label} '{disp_key}': 'string' must be a JSON string"
    return None


def list_domain_json_paths(folder: Path) -> List[Path]:
    if not folder.is_dir():
        return []
    out: List[Path] = []
    for p in sorted(folder.glob("*.json")):
        low = p.name.lower()
        if p.name in SPECIAL_FILES or low.startswith("_") or low.endswith("_todo.json") or low.endswith(".todo.json"):
            continue
        out.append(p)
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
        issues.append("names.json: use key 'childrenNames' (not 'childNames')")
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
                                        f"contains Scaleform-unsafe symbol(s) {', '.join(bad)}"
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
                                f"contains Scaleform-unsafe symbol(s) {', '.join(bad)}"
                            )
    return issues


def resolve_dsd_file(repo_root: Path, code: str) -> Optional[Path]:
    dsd_dir = repo_root / "SKSE" / "Plugins" / "DynamicStringDistributor" / "TT_MARAS.esp"
    if not dsd_dir.is_dir():
        return None
    direct = dsd_dir / f"{code}.json"
    if direct.is_file():
        return direct
    code_low = code.lower()
    for cand in sorted(dsd_dir.glob("*.json")):
        stem_low = cand.stem.lower()
        if stem_low == code_low or stem_low.endswith(f"_{code_low}") or stem_low.endswith(f"-{code_low}"):
            return cand
    return None


def validate_and_load_pack(
    repo_root: Path,
    lang_dir: Path,
    en_flats: Dict[str, Dict[str, str]],
    dsd_template_by_nk: Optional[Dict[str, Dict[str, Any]]],
    allow_no_dsd: bool,
    strict: bool,
) -> Tuple[Optional[LanguagePack], List[str], List[str]]:
    errors: List[str] = []
    warnings: List[str] = []

    meta_path = lang_dir / "_meta.json"
    try:
        meta_raw = json.loads(meta_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return None, [f"{lang_dir.name}/_meta.json: invalid JSON ({exc})"], warnings
    if not isinstance(meta_raw, dict):
        return None, [f"{lang_dir.name}/_meta.json: root must be a JSON object"], warnings

    known = KNOWN_LANGUAGES.get(lang_dir.name.lower())
    code = str(meta_raw.get("code") or (known[0] if known else lang_dir.name)).strip()
    if code.lower() != lang_dir.name.lower():
        errors.append(f"{lang_dir.name}/_meta.json: code '{code}' does not match folder '{lang_dir.name}'")
    name = str(meta_raw.get("name") or (known[1] if known else code)).strip()
    native_name = str(meta_raw.get("nativeName") or (known[2] if known else name)).strip()
    author = str(meta_raw.get("author") or "").strip()

    domain_paths = list_domain_json_paths(lang_dir)
    names_path = lang_dir / "names.json"
    has_names = names_path.is_file()
    if has_names:
        for issue in validate_names_overlay(names_path):
            errors.append(f"{code}/{issue}")

    total_en_keys = sum(len(flat) for flat in en_flats.values())
    translated_en_keys = 0

    tgt_flats: Dict[str, Dict[str, str]] = {}
    for p in domain_paths:
        try:
            tree = json.loads(p.read_text(encoding="utf-8"))
        except Exception as exc:
            errors.append(f"{code}/{p.name}: invalid JSON ({exc})")
            continue
        if not isinstance(tree, dict):
            errors.append(f"{code}/{p.name}: root must be a JSON object")
            continue
        tgt_flats[p.name] = flatten_json(tree)

    for fname, en_flat in en_flats.items():
        tgt_flat = tgt_flats.get(fname, {})

        plural_stems: Dict[str, str] = {}
        for ek in en_flat:
            sp = split_plural_stem(ek)
            if sp is not None:
                stem, _ = sp
                plural_stems[stem] = f"{stem}_other" if f"{stem}_other" in en_flat else ek

        for ek in en_flat:
            sp = split_plural_stem(ek)
            if ek in tgt_flat:
                translated_en_keys += 1
            elif sp is not None and sp[1] == "_one" and f"{sp[0]}_other" in tgt_flat:
                translated_en_keys += 1
            elif strict:
                warnings.append(f"{code}/{fname}: missing translation key '{ek}'")

        for tk, tr_val in tgt_flat.items():
            ref_en_val: Optional[str] = en_flat.get(tk)
            is_plural_form = False
            sp = split_plural_stem(tk)
            if sp is not None:
                is_plural_form = True
                if ref_en_val is None and sp[0] in plural_stems:
                    ref_en_val = en_flat[plural_stems[sp[0]]]

            if ref_en_val is not None:
                en_tokens = extract_placeholders(ref_en_val)
                tr_tokens = extract_placeholders(tr_val)
                unknown_tokens = sorted(tr_tokens - en_tokens)
                if unknown_tokens:
                    filtered = [tok for tok in unknown_tokens if not (is_plural_form and tok == "count")]
                    if filtered:
                        errors.append(
                            f"{code}/{fname} -> '{tk}': unknown placeholder(s) {filtered} (expected: {sorted(en_tokens)})"
                        )
                missing_tokens = sorted(en_tokens - tr_tokens)
                for mtok in missing_tokens:
                    if is_plural_form and mtok == "count" and sp is not None and sp[1] in ("_zero", "_one", "_two"):
                        continue
                    warnings.append(f"{code}/{fname} -> '{tk}': missing placeholder '{{{mtok}}}'")

            if is_scaleform_json_key(fname, tk):
                unsafe = find_scaleform_unsafe_chars(tr_val)
                if unsafe:
                    errors.append(
                        f"{code}/{fname} -> '{tk}': Scaleform-unsafe symbol(s) {', '.join(unsafe)}"
                    )

    if strict and translated_en_keys < total_en_keys:
        errors.append(
            f"{code}: incomplete JSON coverage ({translated_en_keys}/{total_en_keys} keys) with --strict"
        )

    dsd_file = resolve_dsd_file(repo_root, code)
    dsd_done = 0
    dsd_total: Optional[int] = len(dsd_template_by_nk) if dsd_template_by_nk is not None else None

    if dsd_file is None:
        if not allow_no_dsd:
            errors.append(
                f"{code}: missing DSD file SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/{code}.json "
                "(pass --allow-no-dsd to skip)"
            )
    else:
        try:
            dsd_list = json.loads(dsd_file.read_text(encoding="utf-8"))
        except Exception as exc:
            errors.append(f"{code}/{dsd_file.name}: invalid DSD JSON ({exc})")
            dsd_list = None

        if dsd_list is not None and not isinstance(dsd_list, list):
            errors.append(f"{code}/{dsd_file.name}: DSD root must be a JSON array")
            dsd_list = None

        if isinstance(dsd_list, list):
            seen_nks: Set[str] = set()
            matched_tmpl_nks: Set[str] = set()
            for item in dsd_list:
                if not isinstance(item, dict):
                    errors.append(f"{code}/{dsd_file.name}: DSD entries must be JSON objects")
                    continue
                schema_err = validate_dsd_entry_schema(item, f"{code}/{dsd_file.name}")
                if schema_err:
                    errors.append(schema_err)
                    continue

                nk = make_dsd_key(item, canonical_display=False)
                disp_key = make_dsd_key(item, canonical_display=True)
                if nk in seen_nks:
                    errors.append(f"{code}/{dsd_file.name}: duplicate DSD entry '{disp_key}'")
                    continue
                seen_nks.add(nk)

                tr_text = str(item.get("string", ""))
                if not tr_text.strip():
                    continue

                if dsd_template_by_nk is not None:
                    if nk in dsd_template_by_nk:
                        matched_tmpl_nks.add(nk)
                else:
                    dsd_done += 1

                orig_text: Optional[str] = None
                if dsd_template_by_nk is not None and nk in dsd_template_by_nk:
                    tmpl_entry = dsd_template_by_nk[nk]
                    orig_text = str(tmpl_entry.get("original", tmpl_entry.get("string", "")))
                elif isinstance(item.get("original"), str):
                    orig_text = item["original"]

                is_dragon_font = (
                    nk == "00004d|tt_maras.esp|book desc"
                    or bool(orig_text and DRAGON_FONT_RE.search(orig_text))
                    or bool(DRAGON_FONT_RE.search(tr_text))
                )

                if orig_text is not None:
                    en_ck_list, en_bk, en_pf = extract_dsd_tags(orig_text)
                    tr_ck_list, tr_bk, tr_pf = extract_dsd_tags(tr_text)
                    en_ck = set(en_ck_list)
                    tr_ck = set(tr_ck_list)

                    unknown_ck = sorted(tr_ck - en_ck)
                    if unknown_ck:
                        errors.append(
                            f"{code} DSD '{disp_key}': unknown CK tag(s) {unknown_ck} (expected: {sorted(en_ck)})"
                        )
                    unknown_pf = sorted(set(tr_pf) - set(en_pf))
                    if unknown_pf:
                        errors.append(
                            f"{code} DSD '{disp_key}': unknown printf token(s) {unknown_pf} (expected: {sorted(set(en_pf))})"
                        )
                    for mtag in sorted(en_ck - tr_ck):
                        warnings.append(f"{code} DSD '{disp_key}': missing CK tag '{mtag}'")
                    for mpf in sorted(set(en_pf) - set(tr_pf)):
                        warnings.append(f"{code} DSD '{disp_key}': missing printf token '{mpf}'")
                    if en_ck_list.count("<mag>") != tr_ck_list.count("<mag>"):
                        warnings.append(
                            f"{code} DSD '{disp_key}': '<mag>' count changed ({en_ck_list.count('<mag>')} vs {tr_ck_list.count('<mag>')})"
                        )
                    if sorted(t.lower() for t in en_bk) != sorted(t.lower() for t in tr_bk):
                        errors.append(f"{code} DSD '{disp_key}': book markup tag mismatch (EN={en_bk}, TR={tr_bk})")

                if is_dragon_font:
                    inner = HTML_BOOK_TAG_RE.sub("", tr_text)
                    non_ascii = sorted({ch for ch in inner if ord(ch) > 127})
                    if non_ascii:
                        errors.append(
                            f"{code} DSD '{disp_key}': $DragonFont text contains non-ASCII character(s) {non_ascii[:8]}"
                        )

                unsafe = find_scaleform_unsafe_chars(tr_text)
                if unsafe:
                    errors.append(f"{code} DSD '{disp_key}': Scaleform-unsafe symbol(s) {', '.join(unsafe)}")

            if dsd_template_by_nk is not None:
                dsd_done = len(matched_tmpl_nks)
                if strict and dsd_done < len(dsd_template_by_nk):
                    errors.append(
                        f"{code}: incomplete DSD coverage ({dsd_done}/{len(dsd_template_by_nk)} entries) with --strict"
                    )

    if strict and warnings:
        errors.extend([f"[strict] {w}" for w in warnings])

    pack = LanguagePack(
        code=code,
        name=name,
        native_name=native_name,
        author=author,
        lang_dir=lang_dir,
        json_files=domain_paths,
        has_names=has_names,
        dsd_file=dsd_file,
        json_done=translated_en_keys,
        json_total=total_en_keys,
        dsd_done=dsd_done,
        dsd_total=dsd_total,
    )
    return pack, errors, warnings


def build_info_xml(version: str) -> str:
    return "\n".join(
        [
            "<fomod>",
            "\t<Name>M.A.R.A.S Translations</Name>",
            "\t<Author>TangyTweaks</Author>",
            f"\t<Version>{xml_escape(version)}</Version>",
            "\t<Website>https://www.nexusmods.com/skyrimspecialedition/mods/159033</Website>",
            "\t<Description>Official translation packs for M.A.R.A.S (UI Dashboard, HUD, notifications, catalogs, and Dynamic String Distributor TT_MARAS.esp records).</Description>",
            "</fomod>",
            "",
        ]
    )


def build_module_config_xml(packs: List[LanguagePack]) -> str:
    lines: List[str] = [
        '<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">',
        "\t<moduleName>M.A.R.A.S Translations</moduleName>",
        '\t<installSteps order="Explicit">',
        '\t\t<installStep name="Select Language">',
        '\t\t\t<optionalFileGroups order="Explicit">',
        '\t\t\t\t<group name="Language" type="SelectExactlyOne">',
        '\t\t\t\t\t<plugins order="Explicit">',
    ]

    for pack in packs:
        title = (
            f"{pack.name} ({pack.native_name}) [{pack.code}]"
            if pack.native_name != pack.name
            else f"{pack.name} [{pack.code}]"
        )
        json_pct = (100.0 * pack.json_done / pack.json_total) if pack.json_total else 100.0
        if pack.dsd_file is None:
            dsd_summary = "None"
        elif pack.dsd_total:
            dsd_pct = 100.0 * pack.dsd_done / pack.dsd_total
            dsd_summary = f"{pack.dsd_done}/{pack.dsd_total} ({dsd_pct:.1f}%)"
        else:
            dsd_summary = f"{pack.dsd_done} entries"

        coverage_line = f"UI / HUD / Catalogs JSON: {pack.json_done}/{pack.json_total} ({json_pct:.1f}%) | TT_MARAS.esp DSD: {dsd_summary}"
        if pack.has_names:
            coverage_line += ", includes localized child & critter names"

        desc_lines = [
            f"Installs the {pack.name} ({pack.native_name}) localization pack for M.A.R.A.S.",
            "Requires Dynamic String Distributor (DSD) for TT_MARAS.esp dialogue, quests, spells, perks, and letters: https://www.nexusmods.com/skyrimspecialedition/mods/107676",
            coverage_line,
        ]
        if pack.author:
            desc_lines.append(f"Translator: {pack.author}")

        escaped_desc = "&#13;&#10;\n".join(xml_escape(line) for line in desc_lines)

        lines.extend(
            [
                f'\t\t\t\t\t\t<plugin name="{xml_attr(title)}">',
                f"\t\t\t\t\t\t\t<description>{escaped_desc}</description>",
                "\t\t\t\t\t\t\t<files>",
                f'\t\t\t\t\t\t\t\t<folder source="languages\\{xml_attr(pack.code)}\\SKSE" destination="SKSE" priority="0" />',
                "\t\t\t\t\t\t\t</files>",
                "\t\t\t\t\t\t\t<typeDescriptor>",
                '\t\t\t\t\t\t\t\t<type name="Optional"/>',
                "\t\t\t\t\t\t\t</typeDescriptor>",
                "\t\t\t\t\t\t</plugin>",
            ]
        )

    lines.extend(
        [
            "\t\t\t\t\t</plugins>",
            "\t\t\t\t</group>",
            "\t\t\t</optionalFileGroups>",
            "\t\t</installStep>",
            "\t</installSteps>",
            "</config>",
            "",
        ]
    )
    return "\n".join(lines)


def write_fomod_archive(out_zip: Path, version: str, packs: List[LanguagePack]) -> List[str]:
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    archived: List[str] = []
    info_xml = build_info_xml(version).encode("utf-8")
    module_xml = build_module_config_xml(packs).encode("utf-8")

    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("fomod/info.xml", info_xml)
        archived.append("fomod/info.xml")
        zf.writestr("fomod/ModuleConfig.xml", module_xml)
        archived.append("fomod/ModuleConfig.xml")

        for pack in packs:
            loc_prefix = f"languages/{pack.code}/SKSE/Plugins/MARAS/localization/{pack.code}"
            to_pack: List[Path] = [pack.lang_dir / "_meta.json", *pack.json_files]
            if pack.has_names:
                to_pack.append(pack.lang_dir / "names.json")
            to_pack.sort(key=lambda p: p.name.lower())

            for src_path in to_pack:
                arcname = f"{loc_prefix}/{src_path.name}"
                zf.write(src_path, arcname)
                archived.append(arcname)

            if pack.dsd_file is not None:
                dsd_arc = (
                    f"languages/{pack.code}/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/{pack.code}.json"
                )
                zf.write(pack.dsd_file, dsd_arc)
                archived.append(dsd_arc)

    return archived


def read_default_version(repo_root: Path) -> str:
    ver_file = repo_root / "SKSE" / "Plugins" / "MARAS" / "version.txt"
    if ver_file.is_file():
        val = ver_file.read_text(encoding="utf-8").strip()
        if val:
            return val
    return "dev"


def load_optional_dsd_template(repo_root: Path, no_dsd_template: bool) -> Optional[Dict[str, Dict[str, Any]]]:
    if no_dsd_template:
        return None
    tmpl_path = repo_root / "tools" / "localization" / "TT_MARAS_dsd_template.json"
    if not tmpl_path.is_file():
        return None
    raw = json.loads(tmpl_path.read_text(encoding="utf-8"))
    if not isinstance(raw, list):
        raise ValueError(f"DSD template root must be a JSON array: {tmpl_path}")
    by_nk: Dict[str, Dict[str, Any]] = {}
    for item in raw:
        if isinstance(item, dict) and "form_id" in item and "type" in item:
            by_nk[make_dsd_key(item, canonical_display=False)] = item
    return by_nk


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate all M.A.R.A.S language packs and build a multi-language FOMOD archive."
    )
    parser.add_argument("--version", default=None, help="FOMOD / archive version string (default: version.txt or 'dev').")
    parser.add_argument(
        "--output",
        default=None,
        help="Output .zip path (default: dist/MARAS_Translations_<version>.zip).",
    )
    parser.add_argument(
        "--langs",
        nargs="+",
        default=None,
        help="Optional subset of language folder codes to package (default: auto-discover all non-en packs).",
    )
    parser.add_argument("--repo-root", default=str(DEFAULT_REPO_ROOT), help="Workspace root directory.")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail if any English JSON keys or DSD entries are missing/warned.",
    )
    parser.add_argument(
        "--allow-no-dsd",
        action="store_true",
        help="Allow packaging a language that does not yet have a DSD TT_MARAS.esp/<lang>.json file.",
    )
    parser.add_argument(
        "--no-dsd-template",
        action="store_true",
        help="Ignore tools/localization/TT_MARAS_dsd_template.json even if present locally (simulates CI).",
    )
    args = parser.parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    loc_root = repo_root / "SKSE" / "Plugins" / "MARAS" / "localization"
    en_dir = loc_root / "en"
    if not en_dir.is_dir():
        print(f"[ERROR] Base English localization directory missing: {en_dir}", file=sys.stderr)
        return 1

    en_flats: Dict[str, Dict[str, str]] = {}
    for p in list_domain_json_paths(en_dir):
        en_flats[p.name] = flatten_json(json.loads(p.read_text(encoding="utf-8")))
    if not en_flats:
        print(f"[ERROR] No base English domain JSON files found in {en_dir}", file=sys.stderr)
        return 1

    try:
        dsd_template_by_nk = load_optional_dsd_template(repo_root, args.no_dsd_template)
    except Exception as exc:
        print(f"[ERROR] Failed loading DSD template: {exc}", file=sys.stderr)
        return 1

    requested_langs = {x.strip().lower() for x in args.langs} if args.langs else None
    candidate_dirs: List[Path] = []
    if requested_langs is not None:
        by_lower = {d.name.lower(): d for d in sorted(loc_root.iterdir()) if d.is_dir()}
        for req in sorted(requested_langs):
            if req == "en":
                print("[ERROR] 'en' is built into the main mod archive and cannot be packaged in FOMOD.", file=sys.stderr)
                return 1
            if req not in by_lower:
                print(f"[ERROR] Requested language folder not found: {loc_root / req}", file=sys.stderr)
                return 1
            candidate_dirs.append(by_lower[req])
    else:
        for d in sorted(loc_root.iterdir()):
            if d.is_dir() and d.name.lower() != "en":
                candidate_dirs.append(d)

    packs: List[LanguagePack] = []
    all_errors: List[str] = []
    all_warnings: List[str] = []

    for lang_dir in candidate_dirs:
        meta_file = lang_dir / "_meta.json"
        domain_files = list_domain_json_paths(lang_dir)
        if not meta_file.is_file() or not domain_files:
            msg = f"[SKIP] {lang_dir}: no completed localization files found"
            if requested_langs is not None:
                all_errors.append(msg)
            else:
                print(msg)
            continue

        pack, errs, warns = validate_and_load_pack(
            repo_root=repo_root,
            lang_dir=lang_dir,
            en_flats=en_flats,
            dsd_template_by_nk=dsd_template_by_nk,
            allow_no_dsd=args.allow_no_dsd,
            strict=args.strict,
        )
        all_errors.extend(errs)
        all_warnings.extend(warns)
        if pack is not None:
            packs.append(pack)

    for warn in all_warnings:
        print(f"[WARN] {warn}")
    if all_errors:
        for err in all_errors:
            print(f"[ERROR] {err}", file=sys.stderr)
        return 1
    if not packs:
        print("[ERROR] 0 valid non-English language packs discovered.", file=sys.stderr)
        return 1

    packs.sort(key=lambda p: (p.name.lower(), p.code.lower()))
    version = args.version.strip() if args.version else read_default_version(repo_root)
    out_zip = (
        Path(args.output).resolve()
        if args.output
        else (repo_root / "dist" / f"MARAS_Translations_{version}.zip")
    )

    archived = write_fomod_archive(out_zip, version, packs)
    print(f"[FOMOD] Built {out_zip} ({len(packs)} language(s), {len(archived)} file(s)):")
    for pack in packs:
        dsd_str = (
            f"{pack.dsd_done}/{pack.dsd_total}"
            if pack.dsd_total is not None
            else (f"{pack.dsd_done} entries" if pack.dsd_file else "none")
        )
        print(
            f"  - {pack.code}: {pack.name} ({pack.native_name}) | "
            f"JSON {pack.json_done}/{pack.json_total} | DSD {dsd_str} | names.json={pack.has_names}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
