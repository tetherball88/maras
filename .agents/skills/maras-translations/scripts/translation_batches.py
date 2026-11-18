#!/usr/bin/env python3
"""
M.A.R.A.S Translation Batch, Validation & Packaging Helper CLI.

Works alongside `tools/localization/sync_localization.py` so AI agents and human translators
never have to read or edit a monolithic ~2,870-entry `_todo.json` file in a single pass.

Subcommands:
  init-meta  Create `SKSE/Plugins/MARAS/localization/<lang>/_meta.json` with canonical Skyrim metadata.
  split      Run `sync_localization.py` and split pending JSON + DSD entries into right-sized
             `batch_*.json` files with language-aware CLDR plural scaffolding and inline context notes.
  validate   Check `batch_*.json` files for UTF-8 JSON validity, `{named}` placeholders, DSD Creation Kit
             `<Alias=...>` / `<mag>` / `%.0f` / `<font>` tags, `$DragonFont` ASCII safety, and concat rules.
  apply      Validate all completed entries across `--batch-dir` and merge them atomically into
             `localization/<lang>/*.json` and `DynamicStringDistributor/TT_MARAS.esp/<lang>.json`.
  package    Verify `--strict` (unless `--allow-partial`) and build a zero-ESP MO2/Vortex `.zip` archive.

Zero third-party dependencies (Python 3.10+ standard library only).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_REPO_ROOT = SCRIPT_DIR.parents[3]
sys.path.insert(0, str(DEFAULT_REPO_ROOT / "tools" / "localization"))
try:
    from sync_localization import (
        find_scaleform_unsafe_chars as _sync_find_scaleform_unsafe_chars,
        find_universal_llm_artifacts as _sync_find_universal_llm_artifacts,
        is_scaleform_json_key as _sync_is_scaleform_json_key,
        validate_dsd_schema_fields as _sync_validate_dsd_schema_fields,
    )
except Exception:
    _sync_find_scaleform_unsafe_chars = None
    _sync_find_universal_llm_artifacts = None
    _sync_is_scaleform_json_key = None
    _sync_validate_dsd_schema_fields = None

PLURAL_SUFFIXES = ("_zero", "_one", "_two", "_few", "_many", "_other")
TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
CK_TAG_RE = re.compile(r"<[A-Za-z0-9_=.]+>")
HTML_BOOK_TAG_RE = re.compile(r"</?(?:font|p|br|img|div)\b[^>]*>|\[pagebreak\]", re.IGNORECASE)
PRINTF_TAG_RE = re.compile(r"%(?:\.\d+)?[fdgs]")
DRAGON_FONT_RE = re.compile(r'face\s*=\s*["\']?\$DragonFont["\']?', re.IGNORECASE)

# Canonical Skyrim SE language metadata aligned with C++ `MapSkyrimIniLanguageToCode`
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

SLAVIC_PRIMARY_SUBTAGS = {"ru", "uk", "be", "sr", "hr", "bs", "pl"}
CZECH_SLOVAK_PRIMARY_SUBTAGS = {"cs", "sk"}
INVARIANT_PLURAL_PRIMARY_SUBTAGS = {"ja", "zh", "ko", "tr", "vi", "th", "id"}
ZERO_IS_ONE_PRIMARY_SUBTAGS = {"fr", "pt"}

CONFIRM_ENTER_LABELS = {"label_dating", "label_engaged", "label_lover", "label_married"}
CONFIRM_BREAKUP_LABELS = {"label_exdating", "label_jilted", "label_exlover", "label_divorced"}


def primary_subtag(lang_code: str) -> str:
    cleaned = lang_code.strip().replace("_", "-")
    if not cleaned:
        return "en"
    return cleaned.split("-", 1)[0].lower()


def canonical_lang_code(lang_code: str) -> str:
    low = lang_code.strip().replace("_", "-").lower()
    if low in KNOWN_LANGUAGES:
        return KNOWN_LANGUAGES[low][0]
    return lang_code.strip()


def extract_placeholders(template: str) -> List[str]:
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
                cand = template[i + 1 : close]
                if TOKEN_RE.match(cand):
                    tokens.add(cand)
                    i = close + 1
                    continue
            i += 1
        elif ch == "}" and i + 1 < n and template[i + 1] == "}":
            i += 2
        else:
            i += 1
    return sorted(tokens)


def extract_dsd_tokens(text: str) -> Tuple[List[str], List[str], List[str]]:
    ck_tags = CK_TAG_RE.findall(text)
    book_tags = HTML_BOOK_TAG_RE.findall(text)
    printf_tags = PRINTF_TAG_RE.findall(text)
    return ck_tags, book_tags, printf_tags


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


def find_true_cldr_plural_stems(en_flat: Dict[str, str]) -> Set[str]:
    """
    A true CLDR plural stem in `en/` always defines BOTH `<stem>_one` and `<stem>_other`.
    This avoids false positives like `hud.spouse_shared_home_stopped_other` or `sub_guardian_one`.
    """
    stems: Set[str] = set()
    for key in en_flat:
        if key.endswith("_other"):
            stem = key[: -len("_other")]
            if stem and f"{stem}_one" in en_flat:
                stems.add(stem)
    return stems


def required_plural_suffixes_for_lang(lang_code: str) -> Tuple[str, ...]:
    sub = primary_subtag(lang_code)
    if sub in INVARIANT_PLURAL_PRIMARY_SUBTAGS:
        return ("_other",)
    if sub in SLAVIC_PRIMARY_SUBTAGS:
        return ("_one", "_few", "_many", "_other")
    if sub in CZECH_SLOVAK_PRIMARY_SUBTAGS:
        return ("_one", "_few", "_other")
    return ("_one", "_other")


def plural_hint_for_lang(lang_code: str, suffix: str) -> str:
    sub = primary_subtag(lang_code)
    if sub == "pl":
        rules = {
            "_one": "n == 1",
            "_few": "ends in 2-4 (except 12-14), e.g. 2, 3, 4, 22",
            "_many": "0, 5-21, or ends in 0/1/5-9/12-14, e.g. 0, 5, 12, 21",
            "_other": "fractions / general fallback",
        }
        return f"Polish CLDR '{suffix}' ({rules.get(suffix, 'plural form')})."
    if sub in SLAVIC_PRIMARY_SUBTAGS:
        rules = {
            "_one": "ends in 1 (except 11), e.g. 1, 21, 31",
            "_few": "ends in 2-4 (except 12-14), e.g. 2, 3, 4, 22",
            "_many": "0, 5-20, or ends in 0/5-9/11-14, e.g. 0, 5, 11, 14, 25",
            "_other": "fractions / general fallback",
        }
        return f"Slavic CLDR '{suffix}' ({rules.get(suffix, 'plural form')})."
    if sub in CZECH_SLOVAK_PRIMARY_SUBTAGS:
        rules = {
            "_one": "n == 1",
            "_few": "n in 2..4",
            "_other": "0, 5+, or fractions",
        }
        return f"Czech/Slovak CLDR '{suffix}' ({rules.get(suffix, 'plural form')})."
    if sub in ZERO_IS_ONE_PRIMARY_SUBTAGS:
        rules = {
            "_one": "n == 0 or n == 1 (keep '{count}' if 0 is possible)",
            "_other": "n >= 2",
        }
        return f"French/Portuguese CLDR '{suffix}' ({rules.get(suffix, 'plural form')})."
    if sub in INVARIANT_PLURAL_PRIMARY_SUBTAGS:
        return f"Invariant cardinal plural '{suffix}' (used for all counts in '{lang_code}')."
    rules = {"_one": "n == 1", "_other": "n == 0 or n >= 2"}
    return f"CLDR '{suffix}' ({rules.get(suffix, 'plural form')})."


def json_context_note(fname: str, key: str) -> Optional[str]:
    if fname == "catalogs.json" and key.startswith("bonuses.") and key.endswith(".description"):
        return (
            "BuffCard UI renders '{description} <strong>15%</strong>' inline. "
            "Phrase so the trailing value follows naturally and DO NOT end with a period."
        )
    if fname == "hud_and_modals.json" and key.startswith("modal.confirm.label_"):
        leaf = key.rsplit(".", 1)[-1]
        if leaf in CONFIRM_ENTER_LABELS:
            return (
                "Substituted as {action} into 'modal.confirm.msg_enter'. "
                "Keep grammatical case/framing compatible with msg_enter."
            )
        if leaf in CONFIRM_BREAKUP_LABELS:
            return (
                "Substituted as {action} into 'modal.confirm.msg_breakup'. "
                "Keep grammatical case/framing compatible with msg_breakup."
            )
    return None


def resolve_paths(args: argparse.Namespace) -> Tuple[Path, Path, Path, Path]:
    repo_root = Path(args.repo_root).resolve()
    loc_dir = Path(args.loc_dir).resolve() if args.loc_dir else (repo_root / "SKSE" / "Plugins" / "MARAS" / "localization")
    dsd_template = (
        Path(args.dsd_template).resolve()
        if args.dsd_template
        else (repo_root / "tools" / "localization" / "TT_MARAS_dsd_template.json")
    )
    lang = canonical_lang_code(args.lang)
    dsd_file = (
        Path(args.dsd_file).resolve()
        if getattr(args, "dsd_file", None)
        else (repo_root / "SKSE" / "Plugins" / "DynamicStringDistributor" / "TT_MARAS.esp" / f"{lang}.json")
    )
    return repo_root, loc_dir, dsd_template, dsd_file


def read_maras_version(repo_root: Path) -> str:
    ver_file = repo_root / "SKSE" / "Plugins" / "MARAS" / "version.txt"
    if ver_file.is_file():
        return ver_file.read_text(encoding="utf-8").strip()
    return ""


def ensure_meta_file(
    repo_root: Path,
    loc_dir: Path,
    lang: str,
    name: Optional[str] = None,
    native_name: Optional[str] = None,
    author: Optional[str] = None,
    font_family: Optional[str] = None,
    force: bool = False,
) -> Path:
    canon = canonical_lang_code(lang)
    target_dir = loc_dir / canon
    meta_path = target_dir / "_meta.json"
    if meta_path.is_file() and not force:
        return meta_path

    known = KNOWN_LANGUAGES.get(canon.lower())
    resolved_name = name or (known[1] if known else canon)
    resolved_native = native_name or (known[2] if known else resolved_name)
    skyrim_langs = known[3] if known else []

    meta_obj: Dict[str, Any] = {
        "code": canon,
        "name": resolved_name,
        "nativeName": resolved_native,
    }
    if skyrim_langs:
        meta_obj["skyrimLanguages"] = skyrim_langs
    if author:
        meta_obj["author"] = author
    ver = read_maras_version(repo_root)
    if ver:
        meta_obj["marasVersion"] = ver
    meta_obj["fontFamily"] = font_family or ""

    target_dir.mkdir(parents=True, exist_ok=True)
    meta_path.write_text(json.dumps(meta_obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(f"[OK] Wrote {meta_path} ({resolved_native} / {resolved_name})")
    return meta_path


def run_sync_script(repo_root: Path, extra_args: List[str]) -> subprocess.CompletedProcess[str]:
    sync_script = repo_root / "tools" / "localization" / "sync_localization.py"
    cmd = [sys.executable, str(sync_script), *extra_args]
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")


def cmd_init_meta(args: argparse.Namespace) -> int:
    repo_root, loc_dir, _, _ = resolve_paths(args)
    ensure_meta_file(
        repo_root=repo_root,
        loc_dir=loc_dir,
        lang=args.lang,
        name=args.name,
        native_name=args.native_name,
        author=args.author,
        font_family=args.font_family,
        force=args.force,
    )
    return 0


def load_existing_batch_translations(out_dir: Path) -> Tuple[Dict[str, Dict[str, str]], Dict[str, str]]:
    saved_files: Dict[str, Dict[str, str]] = {}
    saved_dsd: Dict[str, str] = {}
    if not out_dir.is_dir():
        return saved_files, saved_dsd
    for bp in sorted(out_dir.glob("batch_*.json")):
        try:
            doc = json.loads(bp.read_text(encoding="utf-8"))
        except Exception:
            continue
        if not isinstance(doc, dict):
            continue
        for fname, entries in (doc.get("files") or {}).items():
            if not isinstance(entries, dict):
                continue
            for k, item in entries.items():
                tr = item if isinstance(item, str) else (item.get("translation", "") if isinstance(item, dict) else "")
                if isinstance(tr, str) and tr.strip():
                    saved_files.setdefault(fname, {})[k] = tr
        for dk, item in (doc.get("dsd") or {}).items():
            tr = item if isinstance(item, str) else (item.get("translation", "") if isinstance(item, dict) else "")
            if isinstance(tr, str) and tr.strip():
                saved_dsd[dk] = tr
    return saved_files, saved_dsd


def cmd_split(args: argparse.Namespace) -> int:
    repo_root, loc_dir, dsd_template, dsd_file = resolve_paths(args)
    lang = canonical_lang_code(args.lang)
    if lang.lower() == "en":
        print("[ERROR] 'split' targets non-English translation packs (--lang de, ru, zh-CN, etc.).", file=sys.stderr)
        return 1

    if args.init_meta:
        ensure_meta_file(repo_root=repo_root, loc_dir=loc_dir, lang=lang)

    sync_args = [
        "--lang",
        lang,
        "--loc-dir",
        str(loc_dir),
        "--dsd-template",
        str(dsd_template),
        "--dsd-file",
        str(dsd_file),
    ]
    if args.prev_en:
        sync_args.extend(["--prev-en", str(Path(args.prev_en).resolve())])
    if args.no_dsd:
        sync_args.append("--no-dsd")
    if args.dsd_only:
        sync_args.append("--dsd-only")

    res = run_sync_script(repo_root, sync_args)
    if res.stdout:
        print(res.stdout.rstrip())
    if res.stderr:
        print(res.stderr.rstrip(), file=sys.stderr)

    todo_path = loc_dir / lang / "_todo.json"
    if not todo_path.is_file():
        if res.returncode != 0:
            return res.returncode
        print(f"[OK] Zero pending translations for '{lang}' — nothing to split.")
        return 0

    todo_doc = json.loads(todo_path.read_text(encoding="utf-8"))
    out_dir = Path(args.out_dir).resolve()
    saved_files_tr, saved_dsd_tr = load_existing_batch_translations(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # Remove stale batch files before writing fresh deterministic slice set
    for old_bp in out_dir.glob("batch_*.json"):
        old_bp.unlink(missing_ok=True)

    batch_size = max(10, int(args.batch_size))
    req_plurals = required_plural_suffixes_for_lang(lang)
    en_dir = loc_dir / "en"
    lang_dir = loc_dir / lang

    manifest_batches: List[Dict[str, Any]] = []
    batch_counter = 0

    # 1. Build JSON domain batches (preserving plural sibling groups together)
    files_todo: Dict[str, Dict[str, Any]] = todo_doc.get("files") or {}
    for fname, raw_entries in sorted(files_todo.items()):
        if not isinstance(raw_entries, dict) or not raw_entries:
            continue

        en_file = en_dir / fname
        en_flat = flatten_json(json.loads(en_file.read_text(encoding="utf-8"))) if en_file.is_file() else {}
        tgt_file = lang_dir / fname
        tgt_flat = flatten_json(json.loads(tgt_file.read_text(encoding="utf-8"))) if tgt_file.is_file() else {}
        true_plural_stems = find_true_cldr_plural_stems(en_flat)

        # Group entries into atomic units (so plural siblings never split across batch boundaries)
        units: List[List[Tuple[str, Dict[str, Any]]]] = []
        handled_stems: Set[str] = set()

        for key, item in raw_entries.items():
            sp = split_plural_stem(key)
            if sp is not None and sp[0] in true_plural_stems:
                stem = sp[0]
                if stem in handled_stems:
                    continue
                handled_stems.add(stem)

                ref_other_en = en_flat.get(f"{stem}_other", en_flat.get(key, ""))
                ref_one_en = en_flat.get(f"{stem}_one", ref_other_en)
                group: List[Tuple[str, Dict[str, Any]]] = []

                for suf in req_plurals:
                    sib_key = f"{stem}{suf}"
                    if sib_key in tgt_flat and sib_key not in raw_entries:
                        continue
                    raw_sib = raw_entries.get(sib_key)
                    en_src = ref_one_en if suf == "_one" else ref_other_en
                    entry_obj: Dict[str, Any] = {}
                    if isinstance(raw_sib, dict):
                        entry_obj.update(raw_sib)
                    else:
                        entry_obj["status"] = "missing_plural_form" if sib_key not in en_flat else "missing"
                        entry_obj["en"] = en_src
                        entry_obj["translation"] = ""

                    saved = saved_files_tr.get(fname, {}).get(sib_key, "")
                    if saved and not str(entry_obj.get("translation", "")).strip():
                        entry_obj["translation"] = saved

                    phs = extract_placeholders(en_src)
                    if phs:
                        entry_obj["placeholders"] = phs
                    entry_obj["plural_rule"] = plural_hint_for_lang(lang, suf)
                    note = json_context_note(fname, sib_key)
                    if note:
                        entry_obj["note"] = note
                    group.append((sib_key, entry_obj))

                if group:
                    units.append(group)
                continue

            entry_obj = dict(item) if isinstance(item, dict) else {"status": "missing", "en": "", "translation": str(item)}
            saved = saved_files_tr.get(fname, {}).get(key, "")
            if saved and not str(entry_obj.get("translation", "")).strip():
                entry_obj["translation"] = saved
            phs = extract_placeholders(str(entry_obj.get("en", en_flat.get(key, ""))))
            if phs:
                entry_obj["placeholders"] = phs
            note = json_context_note(fname, key)
            if note:
                entry_obj["note"] = note
            units.append([(key, entry_obj)])

        # Slice units into batches of ~batch_size keys
        chunks: List[List[Tuple[str, Dict[str, Any]]]] = []
        cur_chunk: List[Tuple[str, Dict[str, Any]]] = []
        for unit in units:
            if cur_chunk and len(cur_chunk) + len(unit) > batch_size:
                chunks.append(cur_chunk)
                cur_chunk = []
            cur_chunk.extend(unit)
        if cur_chunk:
            chunks.append(cur_chunk)

        stem_slug = Path(fname).stem
        for part_idx, chunk in enumerate(chunks, start=1):
            batch_counter += 1
            part_suffix = f"_p{part_idx}" if len(chunks) > 1 else ""
            bname = f"batch_{batch_counter:02d}_json_{stem_slug}{part_suffix}.json"
            bpath = out_dir / bname
            bdoc = {
                "_instructions": (
                    f"Translate 'en' into '{lang}' by filling 'translation' for every entry. "
                    "Preserve {placeholders} verbatim (never rename tokens inside braces)."
                ),
                "language": lang,
                "layer": "json",
                "domain_file": fname,
                "files": {fname: {k: v for k, v in chunk}},
            }
            bpath.write_text(json.dumps(bdoc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
            done_cnt = sum(1 for _, v in chunk if str(v.get("translation", "")).strip())
            manifest_batches.append(
                {
                    "file": bname,
                    "layer": "json",
                    "domain": fname,
                    "entries": len(chunk),
                    "translated_in_batch": done_cnt,
                }
            )

    # 2. Build DSD batches (TT_MARAS.esp)
    dsd_todo: Dict[str, Any] = todo_doc.get("dsd") or {}
    if isinstance(dsd_todo, dict) and dsd_todo:
        dsd_items: List[Tuple[str, Dict[str, Any]]] = []
        for dkey, raw_item in dsd_todo.items():
            entry_obj = (
                dict(raw_item)
                if isinstance(raw_item, dict)
                else {"status": "missing", "en": "", "translation": str(raw_item)}
            )
            en_text = str(entry_obj.get("en", ""))
            saved = saved_dsd_tr.get(dkey, "")
            if saved and not str(entry_obj.get("translation", "")).strip():
                entry_obj["translation"] = saved

            ck_tags, book_tags, pf_tags = extract_dsd_tokens(en_text)
            req_tags = ck_tags + book_tags + pf_tags
            if req_tags:
                entry_obj["required_tags"] = req_tags

            if DRAGON_FONT_RE.search(en_text):
                if not str(entry_obj.get("translation", "")).strip():
                    entry_obj["translation"] = en_text
                entry_obj["note"] = (
                    "Uses Skyrim $DragonFont rune glyphs (supports uppercase ASCII A-Z only). "
                    "Keep original uppercase ASCII text verbatim so Dragon runes render instead of "
                    "missing-glyph boxes; translate 00004D BOOK FULL and 00004C Translated scroll instead."
                )

            dsd_items.append((dkey, entry_obj))

        total_dsd_chunks = (len(dsd_items) + batch_size - 1) // batch_size
        for part_idx in range(total_dsd_chunks):
            chunk = dsd_items[part_idx * batch_size : (part_idx + 1) * batch_size]
            batch_counter += 1
            bname = f"batch_{batch_counter:02d}_dsd_p{part_idx + 1:02d}.json"
            bpath = out_dir / bname
            bdoc = {
                "_instructions": (
                    f"Translate TT_MARAS.esp DSD records from 'en' into '{lang}' by filling 'translation'. "
                    "Preserve Creation Kit <Alias=...>, <mag>, <dur>, %.0f, and <font> tags verbatim."
                ),
                "language": lang,
                "layer": "dsd",
                "dsd": {k: v for k, v in chunk},
            }
            bpath.write_text(json.dumps(bdoc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
            done_cnt = sum(1 for _, v in chunk if str(v.get("translation", "")).strip())
            manifest_batches.append(
                {
                    "file": bname,
                    "layer": "dsd",
                    "domain": "TT_MARAS.esp",
                    "entries": len(chunk),
                    "translated_in_batch": done_cnt,
                }
            )

    total_entries = sum(b["entries"] for b in manifest_batches)
    manifest = {
        "language": lang,
        "batch_size": batch_size,
        "required_plural_suffixes": list(req_plurals),
        "total_batches": len(manifest_batches),
        "total_entries": total_entries,
        "batches": manifest_batches,
    }
    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")

    print(
        f"[SPLIT] Wrote {len(manifest_batches)} batch file(s) ({total_entries} total entries, "
        f"plural forms={list(req_plurals)}) to {out_dir}"
    )
    return 0


def validate_batches_internal(
    loc_dir: Path,
    dsd_template_path: Path,
    batch_files: List[Path],
    require_complete: bool = False,
) -> Tuple[int, int, List[str], List[str], Dict[str, Dict[str, str]], Dict[str, str]]:
    en_dir = loc_dir / "en"
    en_by_file: Dict[str, Dict[str, str]] = {}
    plural_stems_by_file: Dict[str, Set[str]] = {}
    if en_dir.is_dir():
        for p in sorted(en_dir.glob("*.json")):
            if p.name in ("_meta.json", "names.json") or p.name.startswith("_"):
                continue
            flat = flatten_json(json.loads(p.read_text(encoding="utf-8")))
            en_by_file[p.name] = flat
            plural_stems_by_file[p.name] = find_true_cldr_plural_stems(flat)

    dsd_en_by_key: Dict[str, str] = {}
    if dsd_template_path.is_file():
        for item in json.loads(dsd_template_path.read_text(encoding="utf-8")):
            if not isinstance(item, dict):
                continue
            fid = str(item.get("form_id", "")).strip()
            dtype = str(item.get("type", "")).strip().upper()
            dkey = f"{fid}|{dtype}|{int(item['index'])}" if item.get("index") is not None else f"{fid}|{dtype}"
            dsd_en_by_key[dkey.lower()] = str(item.get("original", item.get("string", "")))

    errors: List[str] = []
    warnings: List[str] = []
    merged_files: Dict[str, Dict[str, str]] = {}
    merged_dsd: Dict[str, str] = {}
    total_seen = 0
    total_filled = 0

    for bp in batch_files:
        try:
            doc = json.loads(bp.read_text(encoding="utf-8"))
        except Exception as exc:
            errors.append(f"{bp.name}: invalid JSON ({exc})")
            continue
        if not isinstance(doc, dict):
            errors.append(f"{bp.name}: root must be a JSON object")
            continue

        # Validate JSON entries
        files_sec = doc.get("files") or {}
        if isinstance(files_sec, dict):
            for fname, entries in files_sec.items():
                if not isinstance(entries, dict):
                    errors.append(f"{bp.name}: files['{fname}'] must be an object")
                    continue
                en_flat = en_by_file.get(fname, {})
                true_stems = plural_stems_by_file.get(fname, set())

                for key, item in entries.items():
                    total_seen += 1
                    tr_val = (
                        item
                        if isinstance(item, str)
                        else (item.get("translation", "") if isinstance(item, dict) else None)
                    )
                    if not isinstance(tr_val, str):
                        errors.append(f"{bp.name} -> {fname}:{key}: 'translation' must be a string")
                        continue
                    if not tr_val.strip():
                        if require_complete:
                            errors.append(f"{bp.name} -> {fname}:{key}: empty translation")
                        continue

                    total_filled += 1
                    merged_files.setdefault(fname, {})[key] = tr_val

                    ref_en = en_flat.get(key)
                    sp = split_plural_stem(key)
                    is_plural = False
                    if sp is not None and sp[0] in true_stems:
                        is_plural = True
                        if ref_en is None:
                            ref_en = en_flat.get(f"{sp[0]}_other")
                    if ref_en is None and isinstance(item, dict) and isinstance(item.get("en"), str):
                        ref_en = item["en"]
                    if ref_en is None:
                        warnings.append(f"{bp.name} -> {fname}:{key}: key not found in base en/{fname}")
                        continue

                    en_phs = set(extract_placeholders(ref_en))
                    tr_phs = set(extract_placeholders(tr_val))

                    extra_phs = sorted(tr_phs - en_phs)
                    if is_plural:
                        extra_phs = [tok for tok in extra_phs if tok != "count"]
                    if extra_phs:
                        errors.append(
                            f"{bp.name} -> {fname}:{key}: unexpected placeholder(s) {extra_phs} (allowed: {sorted(en_phs)})"
                        )

                    missing_phs = sorted(en_phs - tr_phs)
                    for mtok in missing_phs:
                        if is_plural and mtok == "count" and sp is not None and sp[1] in ("_zero", "_one", "_two"):
                            continue
                        errors.append(f"{bp.name} -> {fname}:{key}: missing placeholder '{{{mtok}}}'")

                    if fname == "catalogs.json" and key.startswith("bonuses.") and key.endswith(".description"):
                        stripped = tr_val.rstrip()
                        if stripped.endswith((".", "。", "!", "！")):
                            warnings.append(
                                f"{bp.name} -> {fname}:{key}: ends with sentence punctuation '{stripped[-1]}', "
                                "but BuffCard appends ' 15%' directly after description"
                            )

                    if _sync_is_scaleform_json_key is not None and _sync_is_scaleform_json_key(fname, key):
                        unsafe = _sync_find_scaleform_unsafe_chars(tr_val) if _sync_find_scaleform_unsafe_chars else []
                        if unsafe:
                            errors.append(
                                f"{bp.name} -> {fname}:{key}: unrenderable Skyrim Scaleform symbol(s) {', '.join(unsafe)} "
                                "(replace long dashes with '-', '...' glyph with '...', smart/angle quotes with '\"' / \"'\")"
                            )
                    elif _sync_find_universal_llm_artifacts is not None:
                        artifacts = _sync_find_universal_llm_artifacts(tr_val, ref_en)
                        if artifacts:
                            errors.append(
                                f"{bp.name} -> {fname}:{key}: unexpected LLM typography/whitespace artifact(s) {', '.join(artifacts)}"
                            )

        # Validate DSD entries
        dsd_sec = doc.get("dsd") or {}
        if isinstance(dsd_sec, dict):
            for dkey, item in dsd_sec.items():
                total_seen += 1
                parts = str(dkey).split("|")
                if len(parts) < 3:
                    errors.append(f"{bp.name} -> DSD '{dkey}': malformed DSD key (expected form_id|plugin|type[|index])")
                    continue
                if _sync_validate_dsd_schema_fields is not None:
                    raw_idx: Any = None
                    if len(parts) >= 4 and parts[3].strip() != "":
                        try:
                            raw_idx = int(parts[3])
                        except ValueError:
                            raw_idx = parts[3]
                    schema_err = _sync_validate_dsd_schema_fields(dkey, parts[2], raw_idx, f"{bp.name} -> DSD")
                    if schema_err:
                        errors.append(schema_err)
                tr_val = (
                    item
                    if isinstance(item, str)
                    else (item.get("translation", "") if isinstance(item, dict) else None)
                )
                if not isinstance(tr_val, str):
                    errors.append(f"{bp.name} -> DSD '{dkey}': 'translation' must be a string")
                    continue
                if not tr_val.strip():
                    if require_complete:
                        errors.append(f"{bp.name} -> DSD '{dkey}': empty translation")
                    continue

                total_filled += 1
                merged_dsd[dkey] = tr_val

                en_text = dsd_en_by_key.get(dkey.lower())
                if en_text is None and isinstance(item, dict) and isinstance(item.get("en"), str):
                    en_text = item["en"]
                if en_text is None:
                    warnings.append(f"{bp.name} -> DSD '{dkey}': key not found in DSD template")
                    continue

                en_ck, en_bk, en_pf = extract_dsd_tokens(en_text)
                tr_ck, tr_bk, tr_pf = extract_dsd_tokens(tr_val)

                unknown_ck = sorted(set(tr_ck) - set(en_ck))
                if unknown_ck:
                    errors.append(
                        f"{bp.name} -> DSD '{dkey}': unknown CK tag(s) {unknown_ck} (expected: {sorted(set(en_ck))})"
                    )
                missing_ck = sorted(set(en_ck) - set(tr_ck))
                if missing_ck:
                    errors.append(f"{bp.name} -> DSD '{dkey}': missing CK tag(s) {missing_ck}")

                if en_ck.count("<mag>") != tr_ck.count("<mag>"):
                    warnings.append(
                        f"{bp.name} -> DSD '{dkey}': '<mag>' count changed ({en_ck.count('<mag>')} in EN vs {tr_ck.count('<mag>')})"
                    )

                if sorted(en_pf) != sorted(tr_pf):
                    errors.append(
                        f"{bp.name} -> DSD '{dkey}': printf format mismatch (EN={en_pf}, translation={tr_pf})"
                    )

                if sorted(t.lower() for t in en_bk) != sorted(t.lower() for t in tr_bk):
                    errors.append(
                        f"{bp.name} -> DSD '{dkey}': book markup tag mismatch (EN={en_bk}, translation={tr_bk})"
                    )

                if DRAGON_FONT_RE.search(en_text):
                    inner = HTML_BOOK_TAG_RE.sub("", tr_val)
                    non_ascii = sorted({ch for ch in inner if ord(ch) > 127})
                    if non_ascii:
                        errors.append(
                            f"{bp.name} -> DSD '{dkey}': $DragonFont text contains non-ASCII characters "
                            f"{non_ascii[:8]} which render as missing-glyph boxes in Skyrim"
                        )

                if _sync_find_scaleform_unsafe_chars is not None:
                    unsafe = _sync_find_scaleform_unsafe_chars(tr_val)
                    if unsafe:
                        errors.append(
                            f"{bp.name} -> DSD '{dkey}': unrenderable Skyrim Scaleform symbol(s) {', '.join(unsafe)} "
                            "(replace long dashes with '-', '...' glyph with '...', smart/angle quotes with '\"' / \"'\")"
                        )

    return total_seen, total_filled, errors, warnings, merged_files, merged_dsd


def collect_batch_files(batch_dir: Optional[Path], batch_file: Optional[Path]) -> List[Path]:
    if batch_file is not None:
        return [batch_file.resolve()] if batch_file.is_file() else []
    if batch_dir is not None and batch_dir.is_dir():
        return sorted(batch_dir.resolve().glob("batch_*.json"))
    return []


def cmd_validate(args: argparse.Namespace) -> int:
    _, loc_dir, dsd_template, _ = resolve_paths(args)
    bfiles = collect_batch_files(
        Path(args.batch_dir) if args.batch_dir else None,
        Path(args.batch_file) if args.batch_file else None,
    )
    if not bfiles:
        print("[ERROR] No batch files found to validate.", file=sys.stderr)
        return 1

    seen, filled, errors, warnings, _, _ = validate_batches_internal(
        loc_dir=loc_dir,
        dsd_template_path=dsd_template,
        batch_files=bfiles,
        require_complete=args.require_complete,
    )
    pct = (100.0 * filled / seen) if seen else 100.0
    print(f"=== Batch Validation [{canonical_lang_code(args.lang)}] ===")
    print(f"  Batch files : {len(bfiles)}")
    print(f"  Translated  : {filled}/{seen} ({pct:.1f}%)")
    print(f"  Errors      : {len(errors)} (warnings: {len(warnings)})")
    for err in errors[:30]:
        print(f"  [ERROR] {err}")
    if len(errors) > 30:
        print(f"  ... (+{len(errors) - 30} more error(s))")
    for warn in warnings[:20]:
        print(f"  [WARN]  {warn}")
    return 1 if errors else 0


def cmd_apply(args: argparse.Namespace) -> int:
    repo_root, loc_dir, dsd_template, dsd_file = resolve_paths(args)
    lang = canonical_lang_code(args.lang)
    bfiles = collect_batch_files(
        Path(args.batch_dir) if args.batch_dir else None,
        Path(args.batch_file) if args.batch_file else None,
    )
    if not bfiles:
        print("[ERROR] No batch files found to apply.", file=sys.stderr)
        return 1

    seen, filled, errors, warnings, merged_files, merged_dsd = validate_batches_internal(
        loc_dir=loc_dir,
        dsd_template_path=dsd_template,
        batch_files=bfiles,
        require_complete=False,
    )
    for warn in warnings[:20]:
        print(f"[WARN] {warn}")
    if errors:
        print(f"[ERROR] Aborting apply: {len(errors)} validation error(s) found:", file=sys.stderr)
        for err in errors[:30]:
            print(f"  [ERROR] {err}", file=sys.stderr)
        return 1
    if filled == 0:
        print("[WARN] 0 non-empty translations found across batch files; nothing to apply.")
        return 0

    merged_payload: Dict[str, Any] = {"language": lang, "files": merged_files, "dsd": merged_dsd}
    with tempfile.NamedTemporaryFile(
        mode="w", suffix="_maras_todo.json", encoding="utf-8", newline="\n", delete=False
    ) as tmp:
        tmp_path = Path(tmp.name)
        tmp.write(json.dumps(merged_payload, indent=2, ensure_ascii=False) + "\n")

    try:
        apply_args = [
            "--lang",
            lang,
            "--loc-dir",
            str(loc_dir),
            "--dsd-template",
            str(dsd_template),
            "--dsd-file",
            str(dsd_file),
            "--apply-todo",
            str(tmp_path),
        ]
        if args.prune_obsolete:
            apply_args.append("--prune-obsolete")
        res = run_sync_script(repo_root, apply_args)
        if res.stdout:
            print(res.stdout.rstrip())
        if res.stderr:
            print(res.stderr.rstrip(), file=sys.stderr)
        if res.returncode != 0:
            return res.returncode
    finally:
        tmp_path.unlink(missing_ok=True)

    # Refresh or clean `<lang>/_todo.json` and print updated coverage
    refresh_args = [
        "--lang",
        lang,
        "--loc-dir",
        str(loc_dir),
        "--dsd-template",
        str(dsd_template),
        "--dsd-file",
        str(dsd_file),
    ]
    refresh_res = run_sync_script(repo_root, refresh_args)
    if refresh_res.stdout:
        print(refresh_res.stdout.rstrip())
    if refresh_res.stderr:
        print(refresh_res.stderr.rstrip(), file=sys.stderr)

    # Remove stale `<lang>/_todo.json` if 0 missing/changed items remain
    todo_path = loc_dir / lang / "_todo.json"
    if todo_path.is_file() and "[TODO] Wrote " not in (refresh_res.stdout or ""):
        todo_path.unlink(missing_ok=True)
        print(f"[CLEAN] Removed completed {todo_path}")

    return refresh_res.returncode


def cmd_package(args: argparse.Namespace) -> int:
    repo_root, loc_dir, dsd_template, dsd_file = resolve_paths(args)
    lang = canonical_lang_code(args.lang)
    if lang.lower() == "en":
        print("[ERROR] 'package' is for non-English translation packs.", file=sys.stderr)
        return 1

    audit_flag = "--check" if args.allow_partial else "--strict"
    audit_res = run_sync_script(
        repo_root,
        [
            "--lang",
            lang,
            "--loc-dir",
            str(loc_dir),
            "--dsd-template",
            str(dsd_template),
            "--dsd-file",
            str(dsd_file),
            audit_flag,
        ],
    )
    if audit_res.stdout:
        print(audit_res.stdout.rstrip())
    if audit_res.stderr:
        print(audit_res.stderr.rstrip(), file=sys.stderr)
    if audit_res.returncode != 0:
        print(
            f"[ERROR] Audit failed ({audit_flag}). Fix issues or pass --allow-partial for WIP archives.",
            file=sys.stderr,
        )
        return audit_res.returncode

    lang_dir = loc_dir / lang
    if not lang_dir.is_dir():
        print(f"[ERROR] Language directory missing: {lang_dir}", file=sys.stderr)
        return 1

    ver = read_maras_version(repo_root)
    ver_tag = f"_v{ver}" if ver else ""
    out_zip = (
        Path(args.output).resolve()
        if args.output
        else (repo_root / "dist" / f"MARAS_Translation_{lang}{ver_tag}.zip")
    )
    out_zip.parent.mkdir(parents=True, exist_ok=True)

    archived_files: List[str] = []
    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(lang_dir.glob("*.json")):
            low = p.name.lower()
            if low != "_meta.json" and (low.startswith("_") or low.endswith("_todo.json") or low.endswith(".todo.json")):
                continue
            arcname = f"SKSE/Plugins/MARAS/localization/{lang}/{p.name}"
            zf.write(p, arcname)
            archived_files.append(arcname)

        if dsd_file.is_file():
            dsd_arc = f"SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/{dsd_file.name}"
            zf.write(dsd_file, dsd_arc)
            archived_files.append(dsd_arc)

    print(f"[PACKAGE] Created {out_zip} ({len(archived_files)} file(s), zero .esp plugins):")
    for arc in archived_files:
        print(f"  + {arc}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Batch-split, validate, apply, and package M.A.R.A.S translations."
    )
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--lang", required=True, help="Target language code (e.g. de, ru, fr, es, ja, zh-CN).")
    common.add_argument("--repo-root", default=str(DEFAULT_REPO_ROOT), help="Workspace root directory.")
    common.add_argument("--loc-dir", default=None, help="Override localization root directory.")
    common.add_argument("--dsd-template", default=None, help="Override TT_MARAS_dsd_template.json path.")
    common.add_argument("--dsd-file", default=None, help="Override target DSD <lang>.json path.")

    sub = parser.add_subparsers(dest="command", required=True)

    p_meta = sub.add_parser("init-meta", parents=[common], help="Create localization/<lang>/_meta.json.")
    p_meta.add_argument("--name", default=None, help="English display name (e.g. 'German').")
    p_meta.add_argument("--native-name", default=None, help="Native display name (e.g. 'Deutsch').")
    p_meta.add_argument("--author", default=None, help="Optional translator/author credit.")
    p_meta.add_argument("--font-family", default="", help="Optional CSS font-family override.")
    p_meta.add_argument("--force", action="store_true", help="Overwrite existing _meta.json.")
    p_meta.set_defaults(func=cmd_init_meta)

    p_split = sub.add_parser("split", parents=[common], help="Split pending strings into batch_*.json files.")
    p_split.add_argument("--out-dir", required=True, help="Output folder for batch_*.json files and manifest.json.")
    p_split.add_argument("--batch-size", type=int, default=100, help="Approximate entries per batch file (default: 100).")
    p_split.add_argument("--init-meta", action="store_true", help="Create _meta.json automatically if missing.")
    p_split.add_argument("--prev-en", default=None, help="Previous release en/ folder to detect changed JSON strings.")
    p_split.add_argument("--no-dsd", action="store_true", help="Skip TT_MARAS.esp DSD entries.")
    p_split.add_argument("--dsd-only", action="store_true", help="Split TT_MARAS.esp DSD entries only.")
    p_split.set_defaults(func=cmd_split)

    p_val = sub.add_parser("validate", parents=[common], help="Validate translated batch_*.json files.")
    p_val.add_argument("--batch-dir", default=None, help="Directory containing batch_*.json files.")
    p_val.add_argument("--batch-file", default=None, help="Validate a single batch JSON file.")
    p_val.add_argument("--require-complete", action="store_true", help="Fail if any entry has an empty translation.")
    p_val.set_defaults(func=cmd_validate)

    p_apply = sub.add_parser("apply", parents=[common], help="Validate and merge completed batch_*.json files.")
    p_apply.add_argument("--batch-dir", default=None, help="Directory containing batch_*.json files.")
    p_apply.add_argument("--batch-file", default=None, help="Apply a single batch JSON file.")
    p_apply.add_argument("--prune-obsolete", action="store_true", help="Prune obsolete keys removed from English.")
    p_apply.set_defaults(func=cmd_apply)

    p_pkg = sub.add_parser("package", parents=[common], help="Build MO2/Vortex translation .zip archive.")
    p_pkg.add_argument("--output", default=None, help="Output .zip path (default: dist/MARAS_Translation_<lang>.zip).")
    p_pkg.add_argument("--allow-partial", action="store_true", help="Allow packaging incomplete WIP translations.")
    p_pkg.set_defaults(func=cmd_package)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
