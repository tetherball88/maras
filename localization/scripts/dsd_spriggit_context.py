#!/usr/bin/env python3
"""
Spriggit `.spriggit/TT_MARAS.esp/` Context & Chronological Grouping Helper for M.A.R.A.S DSD Batches.

Parses Quests, DialogBranches, Scenes, DialogTopics, and DialogResponses YAML files
(using Python 3.10+ stdlib regex/BFS only) so `translation_batches.py split` emits DSD
entries grouped by Quest -> Branch -> Topic LinkTo chain and by Scene -> Variant -> Phase,
with speaker/prompt context hints and generic cross-file LQA notes.
"""

from __future__ import annotations

import re
from collections import defaultdict, deque
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


FORMKEY_PREFIX_RE = r"([0-9A-F]{6}):TT_MARAS\.esp"
RE_FORMKEY = re.compile(rf"^FormKey:\s*{FORMKEY_PREFIX_RE}", re.M | re.I)
RE_EDITOR_ID = re.compile(r"^EditorID:\s*(\S+)", re.M)
RE_QUEST_REF = re.compile(rf"^Quest:\s*{FORMKEY_PREFIX_RE}", re.M | re.I)
RE_BRANCH_REF = re.compile(rf"^Branch:\s*{FORMKEY_PREFIX_RE}", re.M | re.I)
RE_START_TOPIC = re.compile(rf"^StartingTopic:\s*{FORMKEY_PREFIX_RE}", re.M | re.I)
RE_SUBTYPE = re.compile(r"^SubtypeName:\s*(\S+)", re.M)
RE_TOPIC_REF = re.compile(rf"Topic:\s*{FORMKEY_PREFIX_RE}", re.I)
RE_ACTION_IDX = re.compile(r"Index:\s*(\d+)")
RE_ACTOR_ID = re.compile(r"ActorID:\s*(\d+)")
RE_START_PHASE = re.compile(r"StartPhase:\s*(\d+)")
RE_LINK_TARGET = re.compile(FORMKEY_PREFIX_RE, re.I)
RE_COMP_VALUE = re.compile(r"ComparisonValue:\s*(\S+)")
RE_ALIAS_IDX = re.compile(r"ReferenceAliasIndex:\s*(\d+)")
RE_VOICE_TYPE = re.compile(r"VoiceTypeOrList:\s*(\S+)")
RE_NPC_OBJ = re.compile(r"Object:\s*(\S+)")

DIAL_TYPES = {"DIAL FULL", "INFO RNAM", "INFO NAM1"}

TARGETED_DSD_NOTES: Dict[str, str] = {
    "00005c|tt_maras.esp|qust full": (
        "Referenced in ui_settings.json 'settings.general.disablePolygamyQuestDesc'. "
        "Keep the localized quest title identical across both files."
    ),
    "00005c|tt_maras.esp|qust cnam|0": (
        "English source uses 2nd-person 'you', whereas sibling stages in this quest use 1st-person 'I'. "
        "Keep quest journal POV consistent across all QUST CNAM stages."
    ),
    "00011a|tt_maras.esp|qust cnam|10": (
        "English source uses 2nd-person 'you', whereas sibling stages in this quest use 1st-person 'I'. "
        "Keep quest journal POV consistent across all QUST CNAM stages."
    ),
    "0000e1|tt_maras.esp|book desc": (
        "Mourning letter: <Alias=Victim> is already deceased (use past tense). "
        "Distinguish singular addressee (<Alias=Player>) from plural couple pronouns."
    ),
    "0000e2|tt_maras.esp|book desc": (
        "Mourning letter: <Alias=Victim> is already deceased (use past tense). "
        "Distinguish singular addressee (<Alias=Player>) from plural couple pronouns."
    ),
    "0000e3|tt_maras.esp|book desc": (
        "Mourning letter: <Alias=Victim> is already deceased (use past tense). "
        "Distinguish singular addressee (<Alias=Player>) from plural couple pronouns."
    ),
    "0000cd|tt_maras.esp|info nam1|1": (
        "'mead' refers to fermented Nordic alcoholic mead (Met / hydromel), not raw bee honey."
    ),
    "000569|tt_maras.esp|info nam1|1": (
        "Spoken when a child walks in on multiple siblings (>= 2) talking together; "
        "use plural 2nd-person address where applicable."
    ),
    "00056a|tt_maras.esp|info nam1|1": (
        "Spoken when a child walks in on multiple siblings (>= 2) talking together; "
        "use plural 2nd-person address where applicable."
    ),
}


def _parse_quests(spriggit_dir: Path) -> Dict[str, str]:
    quests: Dict[str, str] = {}
    quests_dir = spriggit_dir / "Quests"
    if not quests_dir.is_dir():
        return quests
    for p in sorted(quests_dir.glob("*.yaml")):
        txt = p.read_text(encoding="utf-8", errors="replace")
        m_fk = RE_FORMKEY.search(txt)
        if not m_fk:
            continue
        m_ed = RE_EDITOR_ID.search(txt)
        quests[m_fk.group(1).upper()] = m_ed.group(1) if m_ed else p.stem
    return quests


def _parse_branches(spriggit_dir: Path) -> Dict[str, Dict[str, str]]:
    branches: Dict[str, Dict[str, str]] = {}
    br_dir = spriggit_dir / "DialogBranches"
    if not br_dir.is_dir():
        return branches
    for p in sorted(br_dir.glob("*.yaml")):
        txt = p.read_text(encoding="utf-8", errors="replace")
        m_fk = RE_FORMKEY.search(txt)
        if not m_fk:
            continue
        m_ed = RE_EDITOR_ID.search(txt)
        m_qu = RE_QUEST_REF.search(txt)
        m_st = RE_START_TOPIC.search(txt)
        branches[m_fk.group(1).upper()] = {
            "editor_id": m_ed.group(1) if m_ed else p.stem,
            "quest": m_qu.group(1).upper() if m_qu else "",
            "starting_topic": m_st.group(1).upper() if m_st else "",
        }
    return branches


def _parse_scenes(spriggit_dir: Path) -> Dict[str, Dict[str, Any]]:
    topic_scene: Dict[str, Dict[str, Any]] = {}
    scenes_dir = spriggit_dir / "Scenes"
    if not scenes_dir.is_dir():
        return topic_scene
    for p in sorted(scenes_dir.glob("*.yaml")):
        txt = p.read_text(encoding="utf-8", errors="replace")
        m_ed = RE_EDITOR_ID.search(txt)
        scene_ed = m_ed.group(1) if m_ed else p.stem
        if "Actions:" not in txt:
            continue
        actions_block = txt.split("Actions:", 1)[1]
        for chunk in re.split(r"\n-\s+", "\n" + actions_block):
            m_top = RE_TOPIC_REF.search(chunk)
            if not m_top:
                continue
            m_idx = RE_ACTION_IDX.search(chunk)
            m_act = RE_ACTOR_ID.search(chunk)
            m_ph = RE_START_PHASE.search(chunk)
            topic_scene[m_top.group(1).upper()] = {
                "scene": scene_ed,
                "action_idx": int(m_idx.group(1)) if m_idx else 0,
                "actor_id": int(m_act.group(1)) if m_act else 0,
                "phase": int(m_ph.group(1)) if m_ph else 0,
            }
    return topic_scene


def _parse_topics_and_infos(
    spriggit_dir: Path,
) -> Tuple[Dict[str, Dict[str, Any]], Dict[str, Dict[str, Any]]]:
    topics: Dict[str, Dict[str, Any]] = {}
    infos: Dict[str, Dict[str, Any]] = {}
    topics_dir = spriggit_dir / "DialogTopics"
    if not topics_dir.is_dir():
        return topics, infos

    for p in sorted(topics_dir.glob("**/RecordData.yaml")):
        txt = p.read_text(encoding="utf-8", errors="replace")
        m_fk = RE_FORMKEY.search(txt)
        if not m_fk:
            continue
        m_ed = RE_EDITOR_ID.search(txt)
        m_qu = RE_QUEST_REF.search(txt)
        m_br = RE_BRANCH_REF.search(txt)
        m_sub = RE_SUBTYPE.search(txt)
        topics[m_fk.group(1).upper()] = {
            "editor_id": m_ed.group(1) if m_ed else "",
            "quest": m_qu.group(1).upper() if m_qu else "",
            "branch": m_br.group(1).upper() if m_br else "",
            "subtype": m_sub.group(1) if m_sub else "",
            "dir": p.parent,
        }

    for tfid, tinfo in topics.items():
        resp_dir = tinfo["dir"] / "Responses"
        if not resp_dir.is_dir():
            continue
        topic_links: List[str] = []
        seen_topic_links: set[str] = set()

        for p in sorted(resp_dir.glob("*.yaml")):
            txt = p.read_text(encoding="utf-8", errors="replace")
            m_fk = RE_FORMKEY.search(txt)
            if not m_fk:
                continue
            fid = m_fk.group(1).upper()

            links: List[str] = []
            if "LinkTo:" in txt:
                after = txt.split("LinkTo:", 1)[1]
                link_sec = re.split(r"\n[A-Za-z]+:", after)[0]
                links = [x.upper() for x in RE_LINK_TARGET.findall(link_sec)]
                for dst in links:
                    if dst not in seen_topic_links:
                        seen_topic_links.add(dst)
                        topic_links.append(dst)

            scene_idx: Optional[int] = None
            alias_idx: Optional[int] = None
            speaker_hints: List[str] = []
            blocks = re.split(r"\n-\s+MutagenObjectType:", txt)
            for b in blocks:
                if "::sceneIdx_var" in b and scene_idx is None:
                    m_cv = RE_COMP_VALUE.search(b)
                    try:
                        scene_idx = int(float(m_cv.group(1))) if m_cv else 0
                    except ValueError:
                        scene_idx = 0
                if "GetIsAliasRefConditionData" in b and alias_idx is None:
                    m_ai = RE_ALIAS_IDX.search(b)
                    alias_idx = int(m_ai.group(1)) if m_ai else 0
                if "GetIsSexConditionData" in b:
                    sex = "Female" if "MaleFemaleGender: Female" in b else "Male"
                    hint = f"ActorSex={sex}"
                    if hint not in speaker_hints:
                        speaker_hints.append(hint)
                elif "GetPCIsSexConditionData" in b:
                    sex = "Female" if "MaleFemaleGender: Female" in b else "Male"
                    hint = f"PlayerSex={sex}"
                    if hint not in speaker_hints:
                        speaker_hints.append(hint)
                elif "GetIsVoiceTypeConditionData" in b:
                    if "013AE8" in b:
                        hint = "Voice=MaleChild"
                    elif "013AE9" in b:
                        hint = "Voice=FemaleChild"
                    else:
                        m_vt = RE_VOICE_TYPE.search(b)
                        hint = f"Voice={m_vt.group(1)}" if m_vt else ""
                    if hint and hint not in speaker_hints:
                        speaker_hints.append(hint)
                elif "GetIsIDConditionData" in b:
                    m_id = RE_NPC_OBJ.search(b)
                    if m_id:
                        hint = f"NPC={m_id.group(1)}"
                        if hint not in speaker_hints:
                            speaker_hints.append(hint)

            infos[fid] = {
                "topic": tfid,
                "links": links,
                "scene_idx": scene_idx,
                "alias_idx": alias_idx,
                "speaker_hints": speaker_hints,
            }

        tinfo["links"] = topic_links

    return topics, infos


def _compute_branch_bfs(
    quests: Dict[str, str],
    branches: Dict[str, Dict[str, str]],
    topics: Dict[str, Dict[str, Any]],
) -> Tuple[Dict[str, str], Dict[str, Tuple[int, int]]]:
    """
    Propagate branch EditorID and `(bfs_depth, bfs_order)` from each branch's
    `StartingTopic` through `INFO.links` (`LinkTo`) so conversation sub-topics stay
    grouped with their parent dialogue branch in natural turn order.
    """
    topic_branch_name: Dict[str, str] = {}
    for tfid, tinfo in topics.items():
        br = branches.get(tinfo.get("branch", ""), {})
        if br.get("editor_id"):
            topic_branch_name[tfid] = br["editor_id"]

    topic_bfs: Dict[str, Tuple[int, int]] = {}
    queue: deque[Tuple[str, str, int]] = deque()
    bfs_seq = 0

    def branch_sort_key(item: Tuple[str, Dict[str, str]]) -> Tuple[str, str, str]:
        bfid, bdata = item
        q_ed = quests.get(bdata.get("quest", ""), bdata.get("quest", ""))
        return (q_ed, bdata.get("editor_id", ""), bfid)

    # Seed from explicit branch StartingTopics first
    for _, bdata in sorted(branches.items(), key=branch_sort_key):
        st = bdata.get("starting_topic", "")
        bname = bdata.get("editor_id", "")
        if st and st in topics and st not in topic_bfs:
            bfs_seq += 1
            topic_bfs[st] = (0, bfs_seq)
            if bname and st not in topic_branch_name:
                topic_branch_name[st] = bname
            queue.append((st, topic_branch_name.get(st, bname), 0))

    # Seed any branch-owned root topic that wasn't already marked as StartingTopic
    for tfid in sorted(topics.keys()):
        if tfid in topic_bfs:
            continue
        bname = topic_branch_name.get(tfid, "")
        if bname:
            bfs_seq += 1
            topic_bfs[tfid] = (0, bfs_seq)
            queue.append((tfid, bname, 0))

    while queue:
        cur_topic, bname, depth = queue.popleft()
        tinfo = topics.get(cur_topic, {})
        for dst in tinfo.get("links", []):
            if dst not in topic_branch_name and bname:
                topic_branch_name[dst] = bname
            if dst not in topic_bfs:
                bfs_seq += 1
                next_depth = depth + 1
                topic_bfs[dst] = (next_depth, bfs_seq)
                queue.append((dst, topic_branch_name.get(dst, bname), next_depth))

    return topic_branch_name, topic_bfs


def load_spriggit_metadata(spriggit_dir: Path) -> Dict[str, Any]:
    """Parse `.spriggit/TT_MARAS.esp/` records; returns empty metadata cleanly if absent."""
    if not spriggit_dir.is_dir():
        return {
            "quests": {},
            "branches": {},
            "topic_scene": {},
            "topics": {},
            "infos": {},
            "topic_branch_name": {},
            "topic_bfs": {},
        }

    quests = _parse_quests(spriggit_dir)
    branches = _parse_branches(spriggit_dir)
    topic_scene = _parse_scenes(spriggit_dir)
    topics, infos = _parse_topics_and_infos(spriggit_dir)
    topic_branch_name, topic_bfs = _compute_branch_bfs(quests, branches, topics)

    return {
        "quests": quests,
        "branches": branches,
        "topic_scene": topic_scene,
        "topics": topics,
        "infos": infos,
        "topic_branch_name": topic_branch_name,
        "topic_bfs": topic_bfs,
    }


def _non_dialogue_sort_key(dkey: str, orig_idx: int) -> Tuple[int, str, int, int, int]:
    parts = dkey.split("|")
    fid = parts[0].strip().upper()
    dtype = parts[2].strip().upper() if len(parts) >= 3 else ""
    rec_type = dtype.split(" ", 1)[0] if dtype else ""
    sub_type = dtype.split(" ", 1)[1] if " " in dtype else ""

    cat_map = {"QUST": 0, "BOOK": 1, "MESG": 2, "SPEL": 3, "MGEF": 4, "PERK": 5}
    sub_map = {"FULL": 0, "NNAM": 1, "CNAM": 2, "DNAM": 2, "DESC": 2, "ITXT": 3}

    cat_rank = cat_map.get(rec_type, 6)
    sub_rank = sub_map.get(sub_type, 4)
    idx_val = 0
    if len(parts) >= 4 and parts[3].strip():
        try:
            idx_val = int(parts[3].strip())
        except ValueError:
            idx_val = 0
    return (cat_rank, fid, sub_rank, idx_val, orig_idx)


def _dial_subrecord_rank(dtype: str) -> int:
    if dtype == "DIAL FULL":
        return 0
    if dtype == "INFO RNAM":
        return 1
    return 2


def enrich_and_group_dsd_units(
    repo_root: Path,
    dsd_items: List[Tuple[str, Dict[str, Any]]],
    dsd_en_by_key: Dict[str, str],
) -> List[List[Tuple[str, Dict[str, Any]]]]:
    """
    Enrich DSD batch entries with Spriggit dialogue/scene context + targeted LQA notes,
    and group them into chronological atomic units:
      1. Non-dialogue records grouped by FormID (`QUST`, `BOOK`, `MESG`, `SPEL`, `MGEF`, `PERK`)
      2. Non-scene dialogue grouped by Topic (`DIAL FULL` -> `INFO RNAM` -> `INFO NAM1`),
         ordered by `(quest, branch, bfs_depth, bfs_order, topic_fid)`
      3. Scene dialogue grouped by `(scene_editor_id, variant_idx)`, ordered internally by
         `(phase, action_idx, subrecord_rank, index, orig_idx)`
    Preserves exact 1-to-1 item coverage.
    """
    if not dsd_items:
        return []

    meta = load_spriggit_metadata(repo_root / ".spriggit" / "TT_MARAS.esp")
    quests: Dict[str, str] = meta["quests"]
    topic_scene: Dict[str, Dict[str, Any]] = meta["topic_scene"]
    topics: Dict[str, Dict[str, Any]] = meta["topics"]
    infos: Dict[str, Dict[str, Any]] = meta["infos"]
    topic_branch_name: Dict[str, str] = meta["topic_branch_name"]
    topic_bfs: Dict[str, Tuple[int, int]] = meta["topic_bfs"]

    non_dial_groups: Dict[Tuple[int, str], List[Tuple[Tuple[int, str, int, int, int], str, Dict[str, Any]]]] = defaultdict(list)
    non_scene_groups: Dict[str, List[Tuple[Tuple[int, int, int], str, Dict[str, Any]]]] = defaultdict(list)
    non_scene_group_order: Dict[str, Tuple[str, str, int, int, str]] = {}
    scene_groups: Dict[Tuple[str, int], List[Tuple[Tuple[int, int, int, int, int], str, Dict[str, Any]]]] = defaultdict(list)

    for orig_idx, (dkey, raw_entry) in enumerate(dsd_items):
        entry_obj = dict(raw_entry)
        parts = dkey.split("|")
        fid = parts[0].strip().upper()
        dtype = parts[2].strip().upper() if len(parts) >= 3 else ""
        idx_num = 0
        if len(parts) >= 4 and parts[3].strip():
            try:
                idx_num = int(parts[3].strip())
            except ValueError:
                idx_num = 0

        note_hint = TARGETED_DSD_NOTES.get(dkey.lower())
        if note_hint and not str(entry_obj.get("note", "")).strip():
            entry_obj["note"] = note_hint

        if dtype not in DIAL_TYPES:
            sk = _non_dialogue_sort_key(dkey, orig_idx)
            non_dial_groups[(sk[0], sk[1])].append((sk, dkey, entry_obj))
            continue

        # Dialogue item (`DIAL FULL`, `INFO RNAM`, `INFO NAM1`)
        ctx: Dict[str, Any] = {}
        tfid = ""
        iinfo: Dict[str, Any] = {}

        if dtype == "DIAL FULL":
            tfid = fid
            tinfo = topics.get(tfid, {})
            qfid = tinfo.get("quest", "")
            quest_name = quests.get(qfid, qfid)
            branch_name = topic_branch_name.get(tfid, "")
            topic_name = tinfo.get("editor_id") or str(entry_obj.get("editor_id", ""))
            subtype = tinfo.get("subtype", "")
            if quest_name:
                ctx["quest"] = quest_name
            if branch_name:
                ctx["branch"] = branch_name
            if topic_name:
                ctx["topic"] = topic_name
            if subtype:
                ctx["subtype"] = subtype
            if tfid in topic_scene:
                sc_raw = topic_scene[tfid]
                ctx["scene"] = {
                    "name": sc_raw["scene"],
                    "phase": sc_raw["phase"],
                    "action_idx": sc_raw["action_idx"],
                    "actor_id": sc_raw["actor_id"],
                }
        else:
            iinfo = infos.get(fid, {})
            tfid = iinfo.get("topic", "")
            tinfo = topics.get(tfid, {}) if tfid else {}
            qfid = tinfo.get("quest", "")
            quest_name = quests.get(qfid, qfid)
            branch_name = topic_branch_name.get(tfid, "") if tfid else ""
            topic_name = (tinfo.get("editor_id") or tfid) if tfid else ""
            if quest_name:
                ctx["quest"] = quest_name
            if branch_name:
                ctx["branch"] = branch_name
            if topic_name:
                ctx["topic"] = topic_name
            if tfid:
                prompt_en = dsd_en_by_key.get(f"{tfid.lower()}|tt_maras.esp|dial full", "").strip()
                if prompt_en:
                    ctx["prompt_en"] = prompt_en
            if iinfo.get("speaker_hints"):
                ctx["speaker_hints"] = list(iinfo["speaker_hints"])
            if iinfo.get("links"):
                ctx["links_to"] = [
                    (topics.get(dst, {}).get("editor_id") or dst) for dst in iinfo["links"]
                ]
            if tfid and tfid in topic_scene:
                sc_raw = topic_scene[tfid]
                sc_ctx: Dict[str, Any] = {"name": sc_raw["scene"]}
                if iinfo.get("scene_idx") is not None:
                    sc_ctx["variant"] = iinfo["scene_idx"]
                sc_ctx["phase"] = sc_raw["phase"]
                sc_ctx["action_idx"] = sc_raw["action_idx"]
                if iinfo.get("alias_idx") is not None:
                    sc_ctx["speaker_alias"] = iinfo["alias_idx"]
                ctx["scene"] = sc_ctx

        if ctx:
            entry_obj["context"] = ctx

        sub_rank = _dial_subrecord_rank(dtype)

        if tfid and tfid in topic_scene:
            sc_raw = topic_scene[tfid]
            scene_name = sc_raw["scene"]
            variant = iinfo.get("scene_idx") if dtype != "DIAL FULL" else None
            variant_key = variant if variant is not None else -1
            item_sk = (sc_raw["phase"], sc_raw["action_idx"], sub_rank, idx_num, orig_idx)
            scene_groups[(scene_name, variant_key)].append((item_sk, dkey, entry_obj))
        else:
            group_id = tfid if tfid else f"UNRESOLVED_{fid}_{orig_idx}"
            if group_id not in non_scene_group_order:
                tinfo = topics.get(tfid, {}) if tfid else {}
                q_name = quests.get(tinfo.get("quest", ""), tinfo.get("quest", "")) or "~"
                b_name = (topic_branch_name.get(tfid, "") if tfid else "") or "~"
                bfs_depth, bfs_order = topic_bfs.get(tfid, (999999, 999999))
                non_scene_group_order[group_id] = (q_name, b_name, bfs_depth, bfs_order, group_id)
            non_scene_groups[group_id].append(((sub_rank, idx_num, orig_idx), dkey, entry_obj))

    units: List[List[Tuple[str, Dict[str, Any]]]] = []

    for gkey in sorted(non_dial_groups.keys()):
        rows = sorted(non_dial_groups[gkey], key=lambda r: r[0])
        units.append([(dk, obj) for _, dk, obj in rows])

    for gid in sorted(non_scene_groups.keys(), key=lambda k: non_scene_group_order[k]):
        rows = sorted(non_scene_groups[gid], key=lambda r: r[0])
        units.append([(dk, obj) for _, dk, obj in rows])

    for skey in sorted(scene_groups.keys()):
        rows = sorted(scene_groups[skey], key=lambda r: r[0])
        units.append([(dk, obj) for _, dk, obj in rows])

    return units
