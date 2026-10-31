#!/usr/bin/env python3
"""Callname coverage self-check: does Turbo know every name the commentary of one language can speak?

Ground truth G (what the game has audio for) = the extracted segment wavs of the four player-name banks, joined with
the banks' selection tables (FIFA Editor Tool: Data Explorer > Sound > Speech > LocCommentary > <lang> > SoundWaves >
<lang>_FULL > <bank> > "Export Data Set", saved as "p<BANK> Selection.xlsx"):

  bank                  wav name                             selection row: VariationIndex -> id
  pSIMPLE_SURNAME       pSIMPLE_SURNAME_<seg>_<seg>.wav      surname_ID      = commentary id (900001..965000, 98xxxx)
  pPLAYER_NAMES_SIMPLE  pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav player_db_pID   = player id (his own recording)
  pPLAYER_NAMES_LINK    pPLAYER_NAMES_LINK_<seg>_<seg>.wav   player_db_pID   = player id (his own recording)
  pPLAYER_NAMES_HIGH    pPLAYER_NAMES_HIGH_<seg>_<seg>.wav   player_db_pID   = player id (high-intensity recording)

<seg> is the selection table's VariationIndex (both numbers are always equal). Every VariationIndex has two rows
(cm_sim 0 and 1) with the same id; one VariationIndex can name several player ids (one person's base / icon / hero
items, e.g. 250, 258778, 258779).

Turbo's set T is read the way the GUI's loader reads it (turbogui/src/core/callnames.cpp, callname_audio.cpp):
  generic ids : spoken_<lang>.txt (900000..965000; the whole list is refused when its header count is wrong)
                | master "generic_ids"                                   (Callnames::spoken_answer / has_recording)
  own players : master "real_players" (else real_simple_players | real_link_players)
  per bank    : SIMPLE = segments.real, LINK = segments.real_link, SURNAME = segments.generic, HIGH = segments.real_high
                (HIGH is listed but never played or counted as an own recording: the kick-off check asks SIMPLE / LINK)

usage:
  python turbo/tools/callname_coverage.py <wav_dir | wav list .txt> <selection xlsx dir> <master json>
         [--spoken spoken_<lang>.txt] [--db DIR] [--examples N]
         [--write-spoken OUT] [--write-master OUT [--wav-dir-json DIR] [--wavset-only] [--game-build B]]
  <wav_dir>   the folder of extracted wavs (searched recursively: flat, or generic\\ real\\ real_link\\ real_high\\);
              or a text file with one wav name per line
  --db DIR    folder with the game DB exports commentarynames.txt, playernames.txt, playernamemap.txt (tab separated,
              header row): name checks and the name-row coverage
  --write-spoken OUT   a spoken_<lang>.txt holding exactly G's surname ids in 900001..965000 (loader format)
  --write-master OUT   a masters\\<lang>.json ("game": "fc27") whose ids and segments are exactly G; labels: commentary
              names from --db commentarynames (trimmed), else the input master; player names from the input master.
              Players the input master lists only from other commentary sets (all their segments >= 100000, not in
              this wav folder) are carried over unless --wavset-only. --wav-dir-json sets "wav_dir" (default: the input
              master's). The master holds real names: it stays on the user's PC (scripts/package.sh name guard).
Exit code 0 when G \\ T is empty for every bank, 1 otherwise, 2 on bad input. Python 3 + openpyxl only.
"""
import argparse
import collections
import csv
import datetime
import json
import os
import re
import sys
import unicodedata

BANKS = ("SIMPLE_SURNAME", "PLAYER_NAMES_SIMPLE", "PLAYER_NAMES_LINK", "PLAYER_NAMES_HIGH")
SEG_KEY = {"SIMPLE_SURNAME": "generic", "PLAYER_NAMES_SIMPLE": "real", "PLAYER_NAMES_LINK": "real_link",
           "PLAYER_NAMES_HIGH": "real_high"}
ID_KEY = {"SIMPLE_SURNAME": "generic_ids", "PLAYER_NAMES_SIMPLE": "real_simple_players",
          "PLAYER_NAMES_LINK": "real_link_players", "PLAYER_NAMES_HIGH": "real_high_players"}
WAV_RX = re.compile(r"^p(" + "|".join(BANKS) + r")_(\d+)_(\d+)\.wav$", re.I)
SPOKEN_LO, SPOKEN_HI = 900000, 965000   # parse_spoken_list's range
NO_CALLNAME = 900000
OTHER_SET = 100000                      # master segments >= this: index * 100000 + seg of a later commentary set


def die(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(2)


# ---------------------------------------------------------------- inputs
def read_wavs(src):
    """bank -> {segment: relative folder}; plus names that look like wavs of these banks but do not parse"""
    out = {b: {} for b in BANKS}
    odd = []
    names = []
    if os.path.isdir(src):
        for root, _dirs, files in os.walk(src):
            rel = os.path.relpath(root, src)
            for f in files:
                if f.lower().endswith(".wav"):
                    names.append(("" if rel == "." else rel, f))
    elif os.path.isfile(src):
        with open(src, encoding="utf-8-sig") as fh:
            for line in fh:
                line = line.strip().replace("\\", "/")
                if line:
                    names.append((os.path.dirname(line), os.path.basename(line)))
    else:
        die(f"no wav folder or list at {src}")
    for rel, f in names:
        m = WAV_RX.match(f)
        if not m:
            if f.lower().startswith(("pplayer_names", "psimple_surname")):
                odd.append(os.path.join(rel, f))
            continue
        bank = next(b for b in BANKS if b.lower() == m.group(1).lower())
        a, b = int(m.group(2)), int(m.group(3))
        if a != b:
            odd.append(os.path.join(rel, f))
            continue
        out[bank][a] = rel
    return out, odd


def read_selection(folder, bank):
    """VariationIndex -> set of ids (player ids, or commentary ids for SIMPLE_SURNAME)"""
    try:
        import openpyxl
    except ImportError:
        die("openpyxl is missing: pip install openpyxl")
    want = f"p{bank} selection.xlsx".lower()
    path = next((os.path.join(folder, f) for f in os.listdir(folder) if f.lower() == want), None)
    if not path:
        return None, f"no '{want}' in {folder}"
    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    ws = wb.worksheets[0]
    rows = ws.iter_rows(values_only=True)
    hdr = [str(h or "").strip().lower() for h in next(rows)]
    try:
        ci = hdr.index("variationindex")
    except ValueError:
        return None, f"{path}: no VariationIndex column ({hdr})"
    idcol = next((hdr.index(c) for c in ("surname_id", "player_db_pid", "commentaryid", "playerid") if c in hdr), None)
    if idcol is None:
        return None, f"{path}: no surname_ID / player_db_pID column ({hdr})"
    m = collections.defaultdict(set)
    for r in rows:
        if r is None or len(r) <= max(ci, idcol) or r[ci] is None or r[idcol] is None:
            continue
        try:
            m[int(r[ci])].add(int(r[idcol]))
        except (TypeError, ValueError):
            continue
    wb.close()
    return m, path


def read_spoken_list(path):
    """parse_spoken_list (callnames.cpp): ids in 900000..965000; refused (empty) when the header count is wrong"""
    ids, expected, first = set(), -1, True
    with open(path, encoding="utf-8-sig") as fh:
        for line in fh:
            line = line.rstrip("\r\n")
            if first and line.startswith("#turbo-spoken "):
                parts = line[14:].split()
                if len(parts) > 1 and re.fullmatch(r"-?\d+", parts[1]):
                    expected = int(parts[1])
                first = False
                continue
            first = False
            s = line.lstrip(" \t")
            if not s or s[0] == "#":
                continue
            m = re.match(r"-?\d+", s)
            if not m:
                continue
            rest = s[m.end():]
            if rest and rest[0] not in "\t #,;":
                continue
            v = int(m.group(0))
            if SPOKEN_LO <= v <= SPOKEN_HI:
                ids.add(v)
    if not ids:
        return set(), "no commentary ids (900000..965000) in the list"
    if expected >= 0 and expected != len(ids):
        return set(), f"the header announces {expected} ids but the file holds {len(ids)} (the GUI refuses the list)"
    return ids, ""


def read_tsv(path):
    with open(path, encoding="utf-8-sig", newline="") as fh:
        rows = list(csv.reader(fh, delimiter="\t", quoting=csv.QUOTE_NONE))
    if not rows:
        return []
    hdr = [h.strip().lower() for h in rows[0]]
    return [dict(zip(hdr, r)) for r in rows[1:] if r]


def ids_of(v):
    out = set()
    for x in v if isinstance(v, list) else []:
        if isinstance(x, bool):
            continue
        if isinstance(x, int) and x > 0:
            out.add(x)
        elif isinstance(x, float) and x > 0 and x == int(x):
            out.add(int(x))
    return out


def seg_map(master, key):
    out = {}
    for k, v in ((master.get("segments") or {}).get(key) or {}).items():
        if re.fullmatch(r"\d+", str(k)) and isinstance(v, list):
            out[int(k)] = sorted({int(s) for s in v if isinstance(s, (int, float)) and not isinstance(s, bool)})
    return out


def fold(s):
    return "".join(c for c in unicodedata.normalize("NFKD", s) if not unicodedata.combining(c)).casefold()


def text_cause(a, b):
    """why two labels of one id differ"""
    if a.strip() == b.strip():
        return "whitespace"
    a, b = a.strip(), b.strip()
    if unicodedata.normalize("NFC", a) == unicodedata.normalize("NFC", b):
        return "unicode form (NFC/NFD)"
    if a.casefold() == b.casefold():
        return "case"
    ap = lambda s: s.replace("’", "'").replace("`", "'").replace("´", "'")
    if ap(a) == ap(b):
        return "apostrophe"
    if fold(a) == fold(b) or fold(a).replace("ss", "ß") == fold(b).replace("ss", "ß"):
        return "accents"
    return "different text"


# ---------------------------------------------------------------- core
def build_g(wavs, sel):
    """bank -> {id: [segments with a wav]}; plus selection rows without a wav and wavs without a selection row"""
    g, sel_no_wav, wav_no_sel = {}, {}, {}
    for b in BANKS:
        d = collections.defaultdict(set)
        for seg, ids in sel[b].items():
            if seg in wavs[b]:
                for i in ids:
                    d[i].add(seg)
        g[b] = {i: sorted(s) for i, s in d.items()}
        sel_no_wav[b] = sorted(set(sel[b]) - set(wavs[b]))
        wav_no_sel[b] = sorted(set(wavs[b]) - set(sel[b]))
    return g, sel_no_wav, wav_no_sel


def turbo_sets(master, spoken):
    t = {}
    t["spoken_list"] = spoken
    t["generic_ids"] = ids_of(master.get("generic_ids"))
    if "real_players" in master:
        t["real_players"] = ids_of(master.get("real_players"))
    else:
        t["real_players"] = ids_of(master.get("real_simple_players")) | ids_of(master.get("real_link_players"))
    t["seg"] = {b: seg_map(master, SEG_KEY[b]) for b in BANKS}
    t["bank_ids"] = {b: set(t["seg"][b]) | ids_of(master.get(ID_KEY[b])) for b in BANKS}
    return t


def report(g, t, master, wavs, sel_no_wav, wav_no_sel, odd, db, examples, lang):
    ok = True
    ex = lambda xs: xs[:examples]
    names = {int(k): v for k, v in (master.get("names") or {}).items() if str(k).isdigit()}
    gnames = {int(k): v for k, v in (master.get("generic_names") or {}).items() if str(k).isdigit()}
    print(f"== callname coverage {lang}: master {master.get('language')} game={master.get('game')} built={master.get('built')}")
    print("-- G: the game's audio (wav present AND selection row)")
    for b in BANKS:
        nseg = sum(len(v) for v in g[b].values())
        print(f"  {b:20s} wavs {len(wavs[b]):6d}  segments in G {len(set(s for v in g[b].values() for s in v)):6d}"
              f"  distinct ids {len(g[b]):5d}  (id,segment) pairs {nseg}")
        if sel_no_wav[b]:
            print(f"    selection rows without a wav: {len(sel_no_wav[b])} e.g. {ex(sel_no_wav[b])}")
        if wav_no_sel[b]:
            print(f"    wavs without a selection row (no id: not in G): {len(wav_no_sel[b])} e.g. {ex(wav_no_sel[b])}")
    if odd:
        print(f"  unparsed bank-like wav names: {len(odd)} e.g. {ex(odd)}")
    own_g = set(g["PLAYER_NAMES_SIMPLE"]) | set(g["PLAYER_NAMES_LINK"])
    high_only = set(g["PLAYER_NAMES_HIGH"]) - own_g
    print(f"  players with an own recording (SIMPLE | LINK): {len(own_g)}; LINK only {len(set(g['PLAYER_NAMES_LINK']) - set(g['PLAYER_NAMES_SIMPLE']))};"
          f" HIGH {len(g['PLAYER_NAMES_HIGH'])} of which HIGH only {len(high_only)}")
    gen_g = set(g["SIMPLE_SURNAME"])
    print(f"  generic surname ids: {len(gen_g)} ({sum(1 for c in gen_g if SPOKEN_LO < c <= SPOKEN_HI)} in 900001..965000,"
          f" {sum(1 for c in gen_g if c > SPOKEN_HI)} above 965000)")
    print(f"  distinct spoken names (generic ids + own-recording players): {len(gen_g) + len(own_g)}")

    print("-- T: Turbo, as the GUI loader reads it")
    gen_t = t["spoken_list"] | t["generic_ids"]
    print(f"  spoken list ids {len(t['spoken_list'])} | master generic_ids {len(t['generic_ids'])} = {len(gen_t)} generic ids spoken")
    print(f"  master real_players (own recording) {len(t['real_players'])}")
    for b in BANKS:
        print(f"  {b:20s} ids with segments {len(t['seg'][b]):5d}  ids listed (segments | {ID_KEY[b]}) {len(t['bank_ids'][b]):5d}")

    print("-- diff per bank (G \\ T = spoken by the game, unknown to Turbo; T \\ G = Turbo claims it, no wav here)")
    for b in BANKS:
        gb, tb, ts = g[b], t["bank_ids"][b], t["seg"][b]
        if b == "SIMPLE_SURNAME":
            known = gen_t
        elif b == "PLAYER_NAMES_HIGH":
            known = tb
        else:
            known = t["real_players"]
        miss = sorted(set(gb) - known)
        noseg = sorted(i for i in gb if i in known and i not in ts)
        segdiff = sorted(i for i in gb if i in ts and set(ts[i]) != set(gb[i]))
        segmiss = sorted(i for i in gb if i in ts and set(gb[i]) - set(ts[i]))
        extra = sorted(set(tb) - set(gb))
        if b == "SIMPLE_SURNAME":
            extra = sorted((gen_t | set(ts)) - set(gb))
        elif b != "PLAYER_NAMES_HIGH":
            extra = sorted(set(ts) - set(gb))
        other = [i for i in extra if ts.get(i) and all(s >= OTHER_SET for s in ts[i])]
        noaud = [i for i in extra if i not in other]
        print(f"  {b}: G {len(gb)}  G\\T {len(miss)}  in T without segments {len(noseg)}  segments missing {len(segmiss)}"
              f"  segment lists differ {len(segdiff)}  T\\G {len(extra)} (other commentary sets {len(other)}, no recording {len(noaud)})")
        if b == "PLAYER_NAMES_HIGH" and gb and not ts:
            print("    (the loader never reads segments.real_high: HIGH recordings are not played; no player is HIGH only)"
                  if not high_only else f"    HIGH-only players (not own recordings for the kick-off check): {sorted(high_only)}")
        lab = (lambda i: gnames.get(i, "")) if b == "SIMPLE_SURNAME" else (lambda i: names.get(i, ""))
        for title, xs in (("G\\T", miss), ("without segments", noseg), ("segments missing", segmiss),
                          ("T\\G other sets", other), ("T\\G no recording", noaud)):
            if xs and b != "PLAYER_NAMES_HIGH" or (xs and title.startswith("G")):
                print(f"    {title}: " + ", ".join(f"{i} {lab(i)!r}" for i in ex(xs)))
        if miss and b != "PLAYER_NAMES_HIGH":
            ok = False
    # the pickers (CallnameIndex) before the has_recording fix used the spoken list alone
    pick_miss = sorted(gen_g - t["spoken_list"])
    print(f"  generic ids in G but not in the spoken list (pickers of a GUI before has_recording): {len(pick_miss)} e.g. "
          + ", ".join(f"{i} {gnames.get(i, '')!r}" for i in ex(pick_miss)))
    sp_extra = sorted(t["spoken_list"] - gen_g)
    print(f"  spoken-list ids without a recording: {len(sp_extra)} e.g. {ex(sp_extra)}")
    unnamed = sorted(i for i in own_g | set(g["PLAYER_NAMES_HIGH"]) if i not in names)
    print(f"  G players without a name in the master: {len(unnamed)} e.g. {ex(unnamed)}")
    print(f"  G generic ids without a name in the master: {len(gen_g - set(gnames))} e.g. {ex(sorted(gen_g - set(gnames)))}")

    if db:
        cn = {int(r["commentaryid"]): r["commentarystring"] for r in db["commentarynames"]}
        print("-- game DB")
        print(f"  commentarynames {len(cn)}; with a recording {len(set(cn) & gen_g)}; without (the game shows them, never says them)"
              f" {len(set(cn) - gen_g)} e.g. " + ", ".join(f"{c} {cn[c]!r}" for c in ex(sorted(set(cn) - gen_g))))
        print(f"  recorded ids not in commentarynames: {len(gen_g - set(cn))} e.g. "
              + ", ".join(f"{c} {gnames.get(c, '')!r}" for c in ex(sorted(gen_g - set(cn)))))
        causes = collections.defaultdict(list)
        for c in sorted(gen_g & set(cn)):
            if c in gnames and gnames[c] != cn[c]:
                causes[text_cause(gnames[c], cn[c])].append(c)
        for k, v in sorted(causes.items()):
            print(f"  master generic_names vs commentarystring, {k}: {len(v)} e.g. "
                  + ", ".join(f"{c} {gnames[c]!r}/{cn[c]!r} seg {g['SIMPLE_SURNAME'][c]}" for c in ex(v)))
        pn = [(int(r["nameid"]), int(r["commentaryid"]), r.get("name", "")) for r in db["playernames"]
              if r.get("nameid", "").strip().lstrip("-").isdigit() and r.get("commentaryid", "").strip().isdigit()]
        with_c = [x for x in pn if x[1] != NO_CALLNAME]
        in_g = [x for x in with_c if x[1] in gen_g]
        pick_old = [x for x in with_c if x[1] in t["spoken_list"]]
        pick_new = [x for x in with_c if x[1] in gen_t]
        print(f"  playernames rows {len(pn)}; with a callname {len(with_c)}; callname recorded (G) {len(in_g)};"
              f" Turbo spoken: spoken list only {len(pick_old)}, list | master {len(pick_new)};"
              f" Turbo spoken without a recording {len([x for x in pick_new if x[1] not in gen_g])}")
        fg = collections.defaultdict(set)
        for c in gen_g & set(cn):
            fg[fold(cn[c].strip())].add(c)
        linkable = [x for x in pn if x[1] == NO_CALLNAME and x[2].strip() and fold(x[2].strip()) in fg]
        print(f"  playernames rows with no callname (900000) whose text matches a recorded commentary name (accent/case-"
              f"insensitive; the game does NOT say them until the commentary id is set): {len(linkable)} e.g. "
              + ", ".join(f"{n} {s!r}->{sorted(fg[fold(s.strip())])}" for n, _c, s in ex(linkable)))
        pm = [(int(r["playerid"]), int(r["commentaryid"])) for r in db["playernamemap"]
              if r.get("playerid", "").isdigit() and r.get("commentaryid", "").isdigit()]
        pm_g = [x for x in pm if x[1] in gen_g or x[0] in own_g]
        print(f"  playernamemap rows {len(pm)}; callname recorded {len([x for x in pm if x[1] in gen_g])};"
              f" player has an own recording {len([x for x in pm if x[0] in own_g])}; either {len(pm_g)};"
              f" Turbo spoken callname: list only {len([x for x in pm if x[1] in t['spoken_list']])},"
              f" list | master {len([x for x in pm if x[1] in gen_t])}")
        silent = sorted({c for _p, c in pm if c not in gen_g})
        print(f"  playernamemap callnames without a recording: {len(silent)} ids {ex(silent)}")
    print("-- result: " + ("T covers G in every bank" if ok else "Turbo misses names the game speaks (G \\ T above)"))
    return ok


# ---------------------------------------------------------------- writers
def write_spoken(path, lang, g):
    ids = sorted(c for c in g["SIMPLE_SURNAME"] if SPOKEN_LO < c <= SPOKEN_HI)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\r\n") as o:   # CRLF, as the shipped lists
        o.write(f"#turbo-spoken {lang} {len(ids)}\n")
        for c in ids:
            o.write(f"{c}\n")
    print(f"wrote {path}: {len(ids)} ids (G's surname ids in 900001..965000; ids above 965000 come from the master)")


def write_master(path, lang, g, master, db, wav_dir, wavset_only, game_build, sources):
    names = {int(k): v for k, v in (master.get("names") or {}).items() if str(k).isdigit()}
    gnames_old = {int(k): v for k, v in (master.get("generic_names") or {}).items() if str(k).isdigit()}
    cn = {int(r["commentaryid"]): r["commentarystring"].strip() for r in db["commentarynames"]} if db else {}
    seg = {b: {i: list(v) for i, v in g[b].items()} for b in BANKS}
    simple, link, high = set(seg["PLAYER_NAMES_SIMPLE"]), set(seg["PLAYER_NAMES_LINK"]), set(seg["PLAYER_NAMES_HIGH"])
    carried = set()
    if not wavset_only:   # players known only from other commentary sets (segments >= 100000): keep them as they were
        old_real = ids_of(master.get("real_players"))
        for key, bank in (("real", "PLAYER_NAMES_SIMPLE"), ("real_link", "PLAYER_NAMES_LINK")):
            for i, s in seg_map(master, key).items():
                if i in simple | link or not s or not all(x >= OTHER_SET for x in s):
                    continue
                seg[bank][i] = s
                carried.add(i)
        simple |= set(i for i in seg["PLAYER_NAMES_SIMPLE"]) - simple
        link |= set(i for i in seg["PLAYER_NAMES_LINK"]) - link
        extra_noseg = sorted(old_real - simple - link)
    else:
        extra_noseg = []
    own = simple | link
    gen = sorted(seg["SIMPLE_SURNAME"])
    gnames = {}
    for c in gen:
        # the game's own label (commentarynames, trimmed); the previous label kept beside it when the text differs (not
        # only case / spaces), so a search finds both: "Matsui | Matheuzinho" (ear-check those segments)
        db_t, old_t = cn.get(c, ""), gnames_old.get(c, "").strip()
        if db_t and old_t and db_t.casefold() == old_t.casefold():
            t = old_t
        elif db_t and old_t:
            t = f"{db_t} | {old_t}"
        else:
            t = db_t or old_t
        if t:
            gnames[str(c)] = t
    out = {
        "turbo_masters": 1, "language": lang, "game": "fc27",
        "source": "; ".join(sources),
        "built": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "note": ("Built by turbo/tools/callname_coverage.py from the extracted wavs + the banks' selection tables (ids = "
                 "selection rows whose wav exists). 'real' = PLAYER_NAMES_SIMPLE, 'real_link' = PLAYER_NAMES_LINK (own "
                 "recordings, by player id), 'real_high' = PLAYER_NAMES_HIGH (not an own recording for the kick-off check), "
                 "'generic' = pSIMPLE_SURNAME (commentary id). Labels: commentarynames (trimmed; 'DB | previous' "
                 "when the previous master's label is a different text), else the previous master."),
        "counts": {"generic_ids": len(gen), "real_players": len(own), "real_simple_players": len(simple),
                   "real_link_players": len(link), "real_high_players": len(high),
                   "carried_other_set_players": len(carried),
                   "segments": {SEG_KEY[b]: sum(len(v) for v in seg[b].values()) for b in BANKS}},
        "real_players": sorted(own), "real_simple_players": sorted(simple), "real_link_players": sorted(link),
        "real_high_players": sorted(high), "generic_ids": gen, "generic_high_ids": [],
        "names": {str(i): names[i] for i in sorted(own | high) if i in names},
        "unnamed_players": sorted(i for i in own | high if i not in names),
        "generic_names": gnames,
        "wav_dir": wav_dir,
        "segments": {SEG_KEY[b]: {str(i): seg[b][i] for i in sorted(seg[b])} for b in BANKS},
    }
    if game_build:
        out["game_build"] = game_build
    if carried:
        out["carried_note"] = (f"{len(carried)} players come only from other commentary sets of the previous master "
                               "(segments >= 100000 = set index * 100000 + segment; not in this wav folder); "
                               "--wavset-only drops them")
    if extra_noseg:
        out["dropped_players"] = extra_noseg
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as o:
        json.dump(out, o, ensure_ascii=False, separators=(",", ":"))
    os.replace(tmp, path)
    print(f"wrote {path}: {len(gen)} generic ids, {len(own)} own-recording players ({len(simple)} SIMPLE, {len(link)} LINK,"
          f" {len(carried)} carried from other sets), {len(high)} HIGH; wav_dir {wav_dir!r}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wavs")
    ap.add_argument("selection_dir")
    ap.add_argument("master")
    ap.add_argument("--spoken", help="spoken_<lang>.txt (default: none = only the master's generic ids)")
    ap.add_argument("--db", help="folder with commentarynames.txt, playernames.txt, playernamemap.txt")
    ap.add_argument("--examples", type=int, default=20)
    ap.add_argument("--lang", help="language code (default: the master's)")
    ap.add_argument("--write-spoken")
    ap.add_argument("--write-master")
    ap.add_argument("--wav-dir-json", help="wav_dir written to the master (default: the input master's)")
    ap.add_argument("--wavset-only", action="store_true")
    ap.add_argument("--game-build")
    a = ap.parse_args()

    with open(a.master, encoding="utf-8-sig") as fh:
        master = json.load(fh)
    if not isinstance(master, dict):
        die(f"{a.master}: not a master object")
    lang = (a.lang or master.get("language") or "").lower()
    wavs, odd = read_wavs(a.wavs)
    sel = {}
    for b in BANKS:
        m, why = read_selection(a.selection_dir, b)
        if m is None:
            die(why)
        sel[b] = m
    spoken = set()
    if a.spoken:
        spoken, err = read_spoken_list(a.spoken)
        if err:
            print(f"warning: {a.spoken}: {err}")
    db = None
    if a.db:
        db = {}
        for t in ("commentarynames", "playernames", "playernamemap"):
            p = os.path.join(a.db, t + ".txt")
            if not os.path.isfile(p):
                die(f"no {p}")
            db[t] = read_tsv(p)
    g, sel_no_wav, wav_no_sel = build_g(wavs, sel)
    t = turbo_sets(master, spoken)
    ok = report(g, t, master, wavs, sel_no_wav, wav_no_sel, odd, db, a.examples, lang)
    if a.write_spoken:
        write_spoken(a.write_spoken, lang, g)
    if a.write_master:
        src = [os.path.abspath(a.wavs), os.path.abspath(a.selection_dir), "labels: " + os.path.abspath(a.master)]
        write_master(a.write_master, lang, g, master, db, a.wav_dir_json if a.wav_dir_json is not None else master.get("wav_dir", ""),
                     a.wavset_only, a.game_build, src)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
