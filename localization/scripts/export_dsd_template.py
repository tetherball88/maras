#!/usr/bin/env python3
"""
Export a Dynamic String Distributor (DSD) JSON template from `.spriggit/TT_MARAS.esp/`.

Why this exists:
  `TT_MARAS.esp` intentionally keeps inline English strings (`Small` ESL flag only;
  no `Localized` header flag and no `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS` binary tables).
  Translators ship `.esp` record translations as zero-ESP-override DSD JSON files:
    - Repository output pack: `localization/packs/<lang>/dsd/TT_MARAS.esp/<lang>.json`
    - Installed game path:    `Data/SKSE/Plugins/DynamicStringDistributor/TT_MARAS.esp/<lang>.json`

Usage:
  python localization/scripts/export_dsd_template.py
  python localization/scripts/export_dsd_template.py --check

Zero third-party dependencies (Python 3.10+ standard library only).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


ESCAPE_MAP = {
    "0": "\0",
    "a": "\a",
    "b": "\b",
    "t": "\t",
    "n": "\n",
    "v": "\v",
    "f": "\f",
    "r": "\r",
    "e": "\x1b",
    " ": " ",
    '"': '"',
    "/": "/",
    "\\": "\\",
    "N": "\x85",
    "_": "\xa0",
}


def unescape_double_quoted(raw: str) -> str:
    """Decode YAML 1.2 double-quoted scalar escapes."""
    out: List[str] = []
    i = 0
    n = len(raw)
    while i < n:
        ch = raw[i]
        if ch == "\\" and i + 1 < n:
            nxt = raw[i + 1]
            if nxt in ESCAPE_MAP:
                out.append(ESCAPE_MAP[nxt])
                i += 2
                continue
            if nxt == "x" and i + 3 < n:
                out.append(chr(int(raw[i + 2 : i + 4], 16)))
                i += 4
                continue
            if nxt == "u" and i + 5 < n:
                out.append(chr(int(raw[i + 2 : i + 6], 16)))
                i += 6
                continue
            if nxt == "U" and i + 9 < n:
                out.append(chr(int(raw[i + 2 : i + 10], 16)))
                i += 10
                continue
        out.append(ch)
        i += 1
    return "".join(out)


def fold_yaml_block(block_lines: List[str], chomp: str, is_folded: bool) -> str:
    """Decode YAML literal (`|`, `|-`, `|+`) or folded (`>`, `>-`, `>+`) block scalar."""
    if not block_lines:
        return ""

    content_indents = [len(line) - len(line.lstrip(" ")) for line in block_lines if line.strip()]
    if not content_indents:
        return "\n" * len(block_lines) if chomp == "+" else ""

    base_indent = min(content_indents)
    stripped = [line[base_indent:] if len(line) >= base_indent else "" for line in block_lines]

    # Separate trailing empty lines for chomping
    trailing_empty = 0
    while stripped and stripped[-1] == "":
        stripped.pop()
        trailing_empty += 1

    if not stripped:
        return "\n" * trailing_empty if chomp == "+" else ""

    if not is_folded:
        body = "\n".join(stripped)
    else:
        parts: List[str] = []
        idx = 0
        total = len(stripped)
        while idx < total:
            line = stripped[idx]
            is_more_indented = line.startswith(" ")
            parts.append(line)
            if idx == total - 1:
                break

            # Count consecutive empty lines following `idx`
            empty_run = 0
            peek = idx + 1
            while peek < total and stripped[peek] == "":
                empty_run += 1
                peek += 1

            if peek >= total:
                break

            next_more_indented = stripped[peek].startswith(" ")
            if empty_run == 0:
                if is_more_indented or next_more_indented:
                    parts.append("\n")
                else:
                    parts.append(" ")
            else:
                if is_more_indented or next_more_indented:
                    parts.append("\n" * (empty_run + 1))
                else:
                    parts.append("\n" * empty_run)
            idx = peek
        body = "".join(parts)

    if chomp == "-":
        return body
    if chomp == "+":
        return body + "\n" * (trailing_empty + 1)
    return body + "\n"


def parse_yaml_scalar_at(lines: List[str], line_idx: int, key_prefix: str) -> Tuple[str, int]:
    """
    Parse `Value: ...` starting at `lines[line_idx]` and return `(decoded_value, next_line_idx)`.
    Supports Spriggit's plain, single-quoted, double-quoted, folded (`>`, `>-`), and literal (`|`, `|-`) scalars.
    """
    line = lines[line_idx]
    val_indent = len(line) - len(line.lstrip(" "))
    raw = line.strip()[len(key_prefix) :].strip()

    # Block scalar (`>-`, `>`, `|-`, `|`, `>+`, `|+`)
    if raw in (">-", ">", ">+", "|-", "|", "|+"):
        is_folded = raw.startswith(">")
        chomp = raw[1:] if len(raw) > 1 else ""
        block: List[str] = []
        nxt = line_idx + 1
        while nxt < len(lines):
            candidate = lines[nxt]
            if candidate.strip() == "":
                block.append("")
                nxt += 1
                continue
            cand_indent = len(candidate) - len(candidate.lstrip(" "))
            if cand_indent <= val_indent:
                break
            block.append(candidate)
            nxt += 1
        return fold_yaml_block(block, chomp, is_folded), nxt

    # Single-quoted scalar (including multi-line single-quoted)
    if raw.startswith("'"):
        acc = raw[1:]
        nxt = line_idx + 1
        while True:
            # Check if `acc` ends with an unescaped single quote (odd count of trailing `'`)
            trailing_quotes = len(acc) - len(acc.rstrip("'"))
            if trailing_quotes % 2 == 1:
                inner = acc[:-1]
                return inner.replace("''", "'"), nxt
            if nxt >= len(lines):
                return acc.replace("''", "'"), nxt
            cont = lines[nxt].strip()
            acc = (acc + " " + cont) if cont else (acc + "\n")
            nxt += 1

    # Double-quoted scalar (including multi-line double-quoted)
    if raw.startswith('"'):
        acc = raw[1:]
        nxt = line_idx + 1
        while True:
            # Find unescaped closing quote
            escaped = False
            closed_pos = -1
            for pos, ch in enumerate(acc):
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == '"':
                    closed_pos = pos
                    break
            if closed_pos != -1:
                return unescape_double_quoted(acc[:closed_pos]), nxt
            if nxt >= len(lines):
                return unescape_double_quoted(acc), nxt
            cont = lines[nxt].strip()
            acc = (acc + " " + cont) if cont else (acc + "\n")
            nxt += 1

    # Empty scalar (possibly followed by indented continuation)
    if raw == "":
        nxt = line_idx + 1
        parts: List[str] = []
        while nxt < len(lines):
            cand = lines[nxt]
            if not cand.strip():
                nxt += 1
                continue
            cand_indent = len(cand) - len(cand.lstrip(" "))
            if cand_indent <= val_indent:
                break
            parts.append(cand.strip())
            nxt += 1
        return " ".join(parts), nxt

    # Plain scalar (with optional multi-line continuation)
    parts = [raw]
    nxt = line_idx + 1
    while nxt < len(lines):
        cand = lines[nxt]
        if not cand.strip():
            break
        cand_indent = len(cand) - len(cand.lstrip(" "))
        if cand_indent <= val_indent:
            break
        parts.append(cand.strip())
        nxt += 1
    return " ".join(parts), nxt


def format_dsd_form_id(spriggit_form_key: str) -> str:
    """Convert Spriggit `000162:TT_MARAS.esp` into DSD `000162|TT_MARAS.esp`."""
    parts = spriggit_form_key.strip().split(":", 1)
    if len(parts) != 2:
        return spriggit_form_key.strip()
    local_hex, plugin = parts[0].strip().upper(), parts[1].strip()
    return f"{local_hex}|{plugin}"


def parse_header_metadata(lines: List[str]) -> Tuple[str, str]:
    """Extract top-level `FormKey` and `EditorID` from a Spriggit YAML file."""
    form_key = ""
    editor_id = ""
    for line in lines[:20]:
        if line.startswith("FormKey:"):
            form_key = line.split(":", 1)[1].strip()
        elif line.startswith("EditorID:"):
            editor_id = line.split(":", 1)[1].strip()
    return format_dsd_form_id(form_key), editor_id


def extract_translated_blocks(lines: List[str]) -> List[Tuple[int, str, str]]:
    """
    Find every `TargetLanguage:` + `Value:` pair in `lines`.
    Returns list of `(header_indent, header_key_line_stripped, decoded_value)`.
    """
    results: List[Tuple[int, str, str]] = []
    i = 0
    n = len(lines)
    while i < n:
        stripped = lines[i].strip()
        if stripped.startswith("TargetLanguage:") and i >= 1 and i + 1 < n:
            header_line = lines[i - 1]
            header_indent = len(header_line) - len(header_line.lstrip(" "))
            header_stripped = header_line.strip()
            val_line_stripped = lines[i + 1].strip()
            if val_line_stripped.startswith("Value:"):
                val, nxt = parse_yaml_scalar_at(lines, i + 1, "Value:")
                results.append((header_indent, header_stripped, val))
                i = nxt
                continue
        i += 1
    return results


def should_include_text(text: str, editor_id: str) -> bool:
    """Skip empty strings and internal EditorID-named technical records."""
    if not text or not text.strip():
        return False
    if editor_id and text.strip() == editor_id:
        return False
    if text.strip().startswith("TTM_"):
        return False
    return True


def make_entry(
    form_id: str,
    dsd_type: str,
    text: str,
    editor_id: str = "",
    index: Optional[int] = None,
) -> Dict[str, Any]:
    entry: Dict[str, Any] = {"form_id": form_id}
    if editor_id:
        entry["editor_id"] = editor_id
    entry["type"] = dsd_type
    if index is not None:
        entry["index"] = index
    entry["original"] = text
    entry["string"] = text
    return entry


def parse_simple_record_file(
    yaml_path: Path,
    field_map: Dict[str, str],
) -> List[Dict[str, Any]]:
    lines = yaml_path.read_text(encoding="utf-8").splitlines()
    form_id, editor_id = parse_header_metadata(lines)
    if not form_id:
        return []
    entries: List[Dict[str, Any]] = []
    for indent, header, val in extract_translated_blocks(lines):
        if indent == 0:
            key = header.rstrip(":")
            dsd_type = field_map.get(key)
            if dsd_type and should_include_text(val, editor_id):
                entries.append(make_entry(form_id, dsd_type, val, editor_id=editor_id))
    return entries


def parse_message_file(yaml_path: Path) -> List[Dict[str, Any]]:
    lines = yaml_path.read_text(encoding="utf-8").splitlines()
    form_id, editor_id = parse_header_metadata(lines)
    if not form_id:
        return []
    entries: List[Dict[str, Any]] = []
    button_idx = 0
    for indent, header, val in extract_translated_blocks(lines):
        if indent == 0 and header == "Name:":
            if should_include_text(val, editor_id):
                entries.append(make_entry(form_id, "MESG FULL", val, editor_id=editor_id))
        elif indent == 0 and header == "Description:":
            if should_include_text(val, editor_id):
                entries.append(make_entry(form_id, "MESG DESC", val, editor_id=editor_id))
        elif header in ("- Text:", "Text:"):
            if should_include_text(val, editor_id):
                entries.append(make_entry(form_id, "MESG ITXT", val, editor_id=editor_id, index=button_idx))
            button_idx += 1
    return entries


def parse_quest_file(yaml_path: Path) -> List[Dict[str, Any]]:
    lines = yaml_path.read_text(encoding="utf-8").splitlines()
    form_id, editor_id = parse_header_metadata(lines)
    if not form_id:
        return []
    entries: List[Dict[str, Any]] = []
    section: Optional[str] = None
    stage_index = 0
    log_entry_idx = -1
    in_log_entries = False
    obj_index = 0
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        stripped = line.strip()
        indent = len(line) - len(line.lstrip(" "))

        if indent == 0 and not line.startswith("-"):
            if stripped == "Stages:":
                section = "stages"
                i += 1
                continue
            if stripped == "Objectives:":
                section = "objectives"
                i += 1
                continue
            section = None
            in_log_entries = False

        if section == "stages":
            if indent == 0 and line.startswith("- "):
                stage_index = 0
                log_entry_idx = -1
                in_log_entries = False
                if stripped.startswith("- Index:"):
                    stage_index = int(stripped.split(":", 1)[1].strip())
            elif indent == 2 and stripped.startswith("Index:"):
                stage_index = int(stripped.split(":", 1)[1].strip())
            elif indent == 2 and stripped == "LogEntries:":
                in_log_entries = True
                log_entry_idx = -1
            elif in_log_entries and indent == 2 and line.startswith("  - "):
                log_entry_idx += 1
        elif section == "objectives":
            if indent == 0 and line.startswith("- "):
                obj_index = 0
                if stripped.startswith("- Index:"):
                    obj_index = int(stripped.split(":", 1)[1].strip())
            elif indent == 2 and stripped.startswith("Index:"):
                obj_index = int(stripped.split(":", 1)[1].strip())

        if stripped.startswith("TargetLanguage:") and i >= 1 and i + 1 < n:
            header_line = lines[i - 1]
            header_indent = len(header_line) - len(header_line.lstrip(" "))
            header_stripped = header_line.strip()
            val_line_stripped = lines[i + 1].strip()
            if val_line_stripped.startswith("Value:"):
                val, nxt = parse_yaml_scalar_at(lines, i + 1, "Value:")
                if header_indent == 0 and header_stripped == "Name:":
                    if should_include_text(val, editor_id):
                        entries.append(make_entry(form_id, "QUST FULL", val, editor_id=editor_id))
                elif section == "stages" and header_stripped == "Entry:":
                    if should_include_text(val, editor_id) and val.strip() != "....":
                        idx_val = stage_index + max(0, log_entry_idx)
                        entries.append(make_entry(form_id, "QUST CNAM", val, editor_id=editor_id, index=idx_val))
                elif section == "objectives" and header_stripped == "DisplayText:":
                    if should_include_text(val, editor_id):
                        entries.append(make_entry(form_id, "QUST NNAM", val, editor_id=editor_id, index=obj_index))
                i = nxt
                continue
        i += 1
    return entries


def parse_dialog_topic_dir(topic_dir: Path) -> List[Dict[str, Any]]:
    entries: List[Dict[str, Any]] = []
    record_yaml = topic_dir / "RecordData.yaml"
    if record_yaml.is_file():
        lines = record_yaml.read_text(encoding="utf-8").splitlines()
        form_id, editor_id = parse_header_metadata(lines)
        for indent, header, val in extract_translated_blocks(lines):
            if indent == 0 and header == "Name:" and should_include_text(val, editor_id):
                entries.append(make_entry(form_id, "DIAL FULL", val, editor_id=editor_id))

    responses_dir = topic_dir / "Responses"
    if responses_dir.is_dir():
        for resp_file in sorted(responses_dir.glob("*.yaml")):
            lines = resp_file.read_text(encoding="utf-8").splitlines()
            info_form_id, info_editor_id = parse_header_metadata(lines)
            if not info_form_id:
                continue
            in_responses = False
            resp_num: Optional[int] = None
            fallback_num = 0
            i = 0
            n = len(lines)
            while i < n:
                line = lines[i]
                stripped = line.strip()
                indent = len(line) - len(line.lstrip(" "))

                if indent == 0 and not line.startswith("-"):
                    in_responses = stripped == "Responses:"
                elif in_responses and indent == 0 and line.startswith("- "):
                    resp_num = None
                    fallback_num += 1
                    if stripped.startswith("- ResponseNumber:"):
                        resp_num = int(stripped.split(":", 1)[1].strip())
                elif in_responses and indent == 2 and stripped.startswith("ResponseNumber:"):
                    resp_num = int(stripped.split(":", 1)[1].strip())

                if stripped.startswith("TargetLanguage:") and i >= 1 and i + 1 < n:
                    header_line = lines[i - 1]
                    header_indent = len(header_line) - len(header_line.lstrip(" "))
                    header_stripped = header_line.strip()
                    val_line_stripped = lines[i + 1].strip()
                    if val_line_stripped.startswith("Value:"):
                        val, nxt = parse_yaml_scalar_at(lines, i + 1, "Value:")
                        if header_indent == 0 and header_stripped == "Prompt:":
                            if should_include_text(val, info_editor_id):
                                entries.append(make_entry(info_form_id, "INFO RNAM", val, editor_id=info_editor_id))
                        elif in_responses and header_stripped == "Text:":
                            if should_include_text(val, info_editor_id):
                                idx_val = resp_num if resp_num is not None else max(1, fallback_num)
                                entries.append(
                                    make_entry(
                                        info_form_id, "INFO NAM1", val, editor_id=info_editor_id, index=idx_val
                                    )
                                )
                        i = nxt
                        continue
                i += 1
    return entries


def form_id_sort_key(entry: Dict[str, Any]) -> Tuple[str, int, str, int]:
    form_part = entry["form_id"].split("|", 1)[0]
    try:
        num = int(form_part, 16)
    except ValueError:
        num = 0
    rec_sig = entry["type"].split(" ", 1)[0]
    order_map = {
        "QUST": "01_QUST",
        "BOOK": "02_BOOK",
        "MESG": "03_MESG",
        "SPEL": "04_SPEL",
        "MGEF": "05_MGEF",
        "PERK": "06_PERK",
        "DIAL": "07_DIAL",
        "INFO": "08_INFO",
    }
    return (
        order_map.get(rec_sig, f"99_{rec_sig}"),
        num,
        entry["type"],
        int(entry.get("index", -1)),
    )


def build_dsd_entries(spriggit_dir: Path) -> List[Dict[str, Any]]:
    all_entries: List[Dict[str, Any]] = []

    simple_folders: List[Tuple[str, Dict[str, str]]] = [
        ("Books", {"Name": "BOOK FULL", "BookText": "BOOK DESC", "Description": "BOOK CNAM"}),
        ("Spells", {"Name": "SPEL FULL", "Description": "SPEL DESC"}),
        ("MagicEffects", {"Name": "MGEF FULL", "Description": "MGEF DNAM"}),
        ("Perks", {"Name": "PERK FULL", "Description": "PERK DESC"}),
    ]

    for folder_name, fmap in simple_folders:
        folder = spriggit_dir / folder_name
        if folder.is_dir():
            for ypath in sorted(folder.glob("*.yaml")):
                all_entries.extend(parse_simple_record_file(ypath, fmap))

    messages_dir = spriggit_dir / "Messages"
    if messages_dir.is_dir():
        for ypath in sorted(messages_dir.glob("*.yaml")):
            all_entries.extend(parse_message_file(ypath))

    quests_dir = spriggit_dir / "Quests"
    if quests_dir.is_dir():
        for ypath in sorted(quests_dir.glob("*.yaml")):
            all_entries.extend(parse_quest_file(ypath))

    topics_dir = spriggit_dir / "DialogTopics"
    if topics_dir.is_dir():
        for tdir in sorted(p for p in topics_dir.iterdir() if p.is_dir()):
            all_entries.extend(parse_dialog_topic_dir(tdir))

    all_entries.sort(key=form_id_sort_key)
    return all_entries


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description="Export a Dynamic String Distributor (DSD) JSON template from .spriggit/TT_MARAS.esp/."
    )
    parser.add_argument(
        "--spriggit-dir",
        type=Path,
        default=repo_root / ".spriggit" / "TT_MARAS.esp",
        help="Path to Spriggit serialized TT_MARAS.esp directory.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "localization" / "scripts" / "TT_MARAS_dsd_template.json",
        help="Output DSD JSON template file path.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify that the existing output file is up-to-date without writing.",
    )
    args = parser.parse_args()

    if not args.spriggit_dir.is_dir():
        print(f"[ERROR] Spriggit directory not found: {args.spriggit_dir}", file=sys.stderr)
        return 1

    entries = build_dsd_entries(args.spriggit_dir)
    rendered = json.dumps(entries, indent=2, ensure_ascii=False) + "\n"

    # Summarize counts by DSD record/subrecord type
    counts: Dict[str, int] = {}
    for item in entries:
        counts[item["type"]] = counts.get(item["type"], 0) + 1

    if args.check:
        if not args.output.is_file():
            print(f"[FAIL] DSD template missing: {args.output}", file=sys.stderr)
            return 1
        existing = args.output.read_text(encoding="utf-8")
        if existing != rendered:
            print(
                f"[FAIL] {args.output} is out of sync with {args.spriggit_dir}. "
                f"Run `python localization/scripts/export_dsd_template.py` to regenerate.",
                file=sys.stderr,
            )
            return 1
        print(f"[OK] {args.output} is up to date ({len(entries)} DSD entries).")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8", newline="\n")
    summary = ", ".join(f"{k}: {v}" for k, v in sorted(counts.items()))
    print(f"[OK] Wrote {len(entries)} DSD entries to {args.output}")
    print(f"     Breakdown: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
