#!/usr/bin/env python3
"""Import the user's FC 26 callname master workbooks into Turbo's per-language lists (turbo\\callnames\\masters).

The user mapped, for FC 26, every recording of each commentary language in one Excel workbook per language
(<root>\\<folder>\\<name>_master.xlsm, sheet "callnames", header row SegmentID, VariationId, playerid, name, type, ...):

  type 'real'     the recording belongs to ONE player: the playerid column is that player's id. The game says these
                  players' names from their own recordings (PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK with player_db_pID),
                  whatever playernamemap or the name ids say (docs/callnames.md sections 1 and 5.3).
  type 'generic'  a generic surname recording: the playerid column holds its COMMENTARY id (9xxxxx).

FC 27 mostly reuses those recordings, so the lists are the best available answer to "has this player his own
recording?" - the game's audio service only answers that during a match, and its set is incomplete. The Turbo GUI
(Players > Callname) reads <Live Editor>\\turbo\\callnames\\masters\\<lang>.json and shows the source as
"your FC 26 list".

usage: python turbo/tools/import_callname_masters.py [--root "C:\\FC_Tools\\My Mods"] [--le "C:\\FC 27 Live Editor"]
                                                       [--out DIR] [--lang ita_it ...] [--dry-run] [--self-test]
  --out defaults to <le>\\turbo\\callnames\\masters. The workbooks are only read (openpyxl, read-only mode).

Output, one file per language (<out>\\<lang>.json, UTF-8):
  {"turbo_masters": 1, "language": "ita_it", "source": "<workbook path>", "built": "<ISO time>",
   "counts": {...}, "real_players": [sorted unique player ids of 'real' rows],
   "generic_ids": [sorted unique commentary ids of 'generic' rows], "names": {"<playerid>": "<name>", ...}}
"""
import argparse
import collections
import datetime
import json
import os
import sys
import tempfile

# Folder under the root -> FC 27 commentary language code (the pack names under <game>\commentary and Data\Win32:
# commentaryfull_<code>). The workbook's own name is the fallback for a file found elsewhere.
FOLDER_LANG = {"br": "por_br", "eng": "eng_us", "fra": "fre_fr", "ger": "ger_de", "ita": "ita_it", "ned": "dut_nl",
               "spa": "spa_es"}
STEM_LANG = {"br": "por_br", "uk": "eng_us", "eng": "eng_us", "france": "fre_fr", "fra": "fre_fr", "ger": "ger_de",
             "italy": "ita_it", "ita": "ita_it", "dutch": "dut_nl", "ned": "dut_nl", "spain": "spa_es", "spa": "spa_es"}
EXT_RANK = {".xlsm": 0, ".xlsx": 1}  # several workbooks for one language: .xlsm first (ned has an older .xlsx copy)
SHEET = "callnames"
COMMENTARY_MIN, COMMENTARY_MAX = 900001, 965000  # the player-name range every commentary bank uses
PLAYER_ID_MAX = 999999                            # FC player ids are below a million; bigger ones are bank oddities


def default_le_root():
    return os.environ.get("TURBO_LE_ROOT", r"C:\FC 27 Live Editor")


def find_workbooks(root):
    """{lang: [paths, best first]} for every *_master.xls[mx] under root (one folder level deep, plus root itself)."""
    found = collections.defaultdict(list)
    dirs = [root] + [os.path.join(root, d) for d in sorted(os.listdir(root)) if os.path.isdir(os.path.join(root, d))]
    for d in dirs:
        folder = os.path.basename(d).lower() if d != root else ""
        for f in sorted(os.listdir(d)):
            stem, ext = os.path.splitext(f)
            if f.startswith("~$") or ext.lower() not in EXT_RANK or not stem.lower().endswith("_master"):
                continue
            lang = FOLDER_LANG.get(folder) or STEM_LANG.get(stem.lower()[: -len("_master")])
            if lang:
                found[lang].append(os.path.join(d, f))
    for lang, paths in found.items():
        paths.sort(key=lambda p: (EXT_RANK[os.path.splitext(p)[1].lower()], -os.path.getmtime(p)))
    return dict(found)


def as_id(v):
    """An id cell as a positive int, else None (text, blanks, fractions, zero or negative)."""
    if isinstance(v, bool) or v is None:
        return None
    if isinstance(v, int):
        return v if v > 0 else None
    if isinstance(v, float):
        return int(v) if v.is_integer() and v > 0 else None
    s = str(v).strip()
    if s.isdigit():
        n = int(s)
        return n if n > 0 else None
    return None


def read_workbook(path):
    """Rows of the "callnames" sheet -> (real ids -> name counter, generic ids, counts). Raises ValueError when the
    workbook has no such sheet or header."""
    import openpyxl  # imported here so --help works without it

    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    try:
        sheet = next((n for n in wb.sheetnames if n.strip().lower() == SHEET), None)
        if sheet is None:
            raise ValueError(f"no sheet '{SHEET}' (sheets: {', '.join(wb.sheetnames)})")
        rows = wb[sheet].iter_rows(values_only=True)
        header = next(rows, None)
        cols = {str(h).strip().lower(): i for i, h in enumerate(header or ()) if h is not None}
        missing = [c for c in ("playerid", "name", "type") if c not in cols]
        if missing:
            raise ValueError(f"sheet '{sheet}' has no column {', '.join(missing)} in its header row")
        ci, cn, ct = cols["playerid"], cols["name"], cols["type"]
        real = collections.defaultdict(collections.Counter)
        first_seen = {}
        generic = set()
        c = collections.Counter()
        for row in rows:
            if row is None or all(v is None for v in row):
                c["blank_rows"] += 1
                continue
            c["rows"] += 1
            get = lambda i: row[i] if i < len(row) else None  # noqa: E731 - short rows end early in openpyxl
            kind = str(get(ct) or "").strip().lower()
            if kind not in ("real", "generic"):
                c["other_type_rows"] += 1
                continue
            pid = as_id(get(ci))
            if pid is None:
                c["non_numeric_id_rows"] += 1
                continue
            if kind == "real":
                c["real_rows"] += 1
                name = str(get(cn) or "").strip()
                if name:
                    real[pid][name] += 1
                    first_seen.setdefault((pid, name), c["rows"])
                else:
                    real[pid]  # noqa: B018 - the id counts even without a name
            else:
                c["generic_rows"] += 1
                generic.add(pid)
        names = {}
        for pid, counter in real.items():
            if counter:
                # the most frequent spelling; ties go to the first one in the sheet
                names[pid] = min(counter.items(), key=lambda kv: (-kv[1], first_seen[(pid, kv[0])]))[0]
        return real, names, generic, c
    finally:
        wb.close()


def build_list(lang, path, built=None):
    real, names, generic, c = read_workbook(path)
    real_ids = sorted(real)
    generic_ids = sorted(generic)
    counts = {
        "rows": c["rows"], "blank_rows": c["blank_rows"], "real_rows": c["real_rows"], "generic_rows": c["generic_rows"],
        "other_type_rows": c["other_type_rows"], "non_numeric_id_rows": c["non_numeric_id_rows"],
        "real_players": len(real_ids), "generic_ids": len(generic_ids),
        "real_ids_over_999999": sum(1 for p in real_ids if p > PLAYER_ID_MAX),
        "generic_ids_outside_900001_965000": sum(1 for g in generic_ids if not COMMENTARY_MIN <= g <= COMMENTARY_MAX),
    }
    return {
        "turbo_masters": 1,
        "language": lang,
        "source": os.path.abspath(path),
        "built": built or datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "note": "FC 26 data: FC 27 mostly reuses these recordings. 'real' = the player's own recording (bound to his "
                "player id); 'generic' = a generic surname recording (commentary id).",
        "counts": counts,
        "real_players": real_ids,
        "generic_ids": generic_ids,
        "names": {str(p): names[p] for p in real_ids if p in names},
    }


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".tmp_", suffix=".json", dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
            f.write("\n")
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def run(root, out, langs=None, dry_run=False, log=print):
    if not os.path.isdir(root):
        log(f"error: no folder {root}")
        return 2, {}
    books = find_workbooks(root)
    if langs:
        books = {k: v for k, v in books.items() if k in langs}
    if not books:
        log(f"error: no *_master.xlsm / .xlsx workbook under {root}")
        return 1, {}
    results, failed = {}, 0
    for lang in sorted(books):
        paths = books[lang]
        path = paths[0]
        for other in paths[1:]:
            log(f"{lang}: also found {other}: skipped ({os.path.basename(path)} preferred: .xlsm first, then the newest)")
        try:
            data = build_list(lang, path)
        except Exception as e:  # one unreadable workbook must not stop the others
            failed += 1
            log(f"{lang}: {path}: NOT imported: {e}")
            continue
        results[lang] = data
        k = data["counts"]
        log(f"{lang}: {path}: {k['real_players']} players with their own recording ('real', {k['real_rows']} rows), "
            f"{k['generic_ids']} generic commentary ids ({k['generic_rows']} rows); skipped {k['non_numeric_id_rows']} rows "
            f"with a non-numeric id, {k['other_type_rows']} rows of another type, {k['blank_rows']} blank rows; notes: "
            f"{k['real_ids_over_999999']} real ids over 999999, {k['generic_ids_outside_900001_965000']} generic ids "
            f"outside 900001..965000 (kept)")
        if not dry_run:
            dest = os.path.join(out, f"{lang}.json")
            write_json(dest, data)
            log(f"{lang}: wrote {dest}")
    return (1 if failed and not results else 0), results


def self_test():
    """Synthetic workbooks: language mapping, the .xlsm/.xlsx duplicate, non-numeric ids, names, the JSON written."""
    import openpyxl

    with tempfile.TemporaryDirectory() as tmp:
        root, out = os.path.join(tmp, "My Mods"), os.path.join(tmp, "out")

        def book(rel, rows, sheet=SHEET, header=("SegmentID", "VariationId", "playerid", "name", "type", "Play Audio")):
            p = os.path.join(root, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            wb = openpyxl.Workbook()
            ws = wb.active
            ws.title = sheet
            ws.append(header)
            for r in rows:
                ws.append(r)
            wb.save(p)  # an .xlsm saved by openpyxl without VBA still loads read-only
            return p

        book("ita/italy_master.xlsm", [(1, 1, 216435, "Stanislav Lobotka", "real"), (2, 2, 216435, "Lobotka", "real"),
                                       (3, 3, 216435, "Lobotka", "real"), (4, 4, 920014, "Abbiati", "generic"),
                                       (5, 5, "n/a", "Broken", "real"), (6, 6, 244263.0, "Amir Rrahmani", "Real "),
                                       (None, None, None, None, None), (7, 7, 920014, "Abbiati", "generic"),
                                       (8, 8, 12, "Other", "unknown")])
        old = book("ned/dutch_master.xlsx", [(1, 1, 1, "Old", "real")])
        new = book("ned/dutch_master.xlsm", [(1, 1, 2, "New", "real")])
        os.utime(old, (2_000_000_000, 2_000_000_000))  # the .xlsx is newer, the .xlsm still wins
        book("misc/spain_master.xlsm", [(1, 1, 3, "Tres", "real")])
        book("eng/uk_master.xlsm", [(1, 1, 4, "x", "real")], sheet="other")
        book("fra/france_master.xlsm", [(1, 1, 5, "x", "real")], header=("a", "b", "c"))
        logs = []
        rc, res = run(root, out, log=logs.append)
        ok = True

        def check(cond, what):
            nonlocal ok
            print(("  ok    " if cond else "  FAIL  ") + what)
            ok = ok and cond

        check(rc == 0, "run succeeded")
        check(sorted(res) == ["dut_nl", "ita_it", "spa_es"], f"languages: {sorted(res)}")
        it = json.load(open(os.path.join(out, "ita_it.json"), encoding="utf-8"))
        check(it["language"] == "ita_it" and it["real_players"] == [216435, 244263], f"real players: {it['real_players']}")
        check(it["generic_ids"] == [920014], f"generic ids: {it['generic_ids']}")
        check(it["names"] == {"216435": "Lobotka", "244263": "Amir Rrahmani"}, f"names (most frequent): {it['names']}")
        check(it["counts"]["non_numeric_id_rows"] == 1 and it["counts"]["other_type_rows"] == 1 and it["counts"]["blank_rows"] == 1,
              f"counts: {it['counts']}")
        nl = json.load(open(os.path.join(out, "dut_nl.json"), encoding="utf-8"))
        check(nl["source"] == os.path.abspath(new) and nl["real_players"] == [2], "dutch: the .xlsm wins over the newer .xlsx")
        check(any("dutch_master.xlsx: skipped" in s for s in logs), "the skipped duplicate is reported")
        check(any(s.startswith("eng_us:") and "NOT imported" in s and "no sheet" in s for s in logs), "missing sheet reported")
        check(any(s.startswith("fre_fr:") and "NOT imported" in s and "no column" in s for s in logs), "missing columns reported")
        check(not os.path.exists(os.path.join(out, "eng_us.json")), "nothing written for a failed workbook")
        rc2, _ = run(root, os.path.join(tmp, "dry"), dry_run=True, log=lambda s: None)
        check(rc2 == 0 and not os.path.exists(os.path.join(tmp, "dry")), "dry run writes nothing")
        print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
        return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=r"C:\FC_Tools\My Mods", help="folder with the language folders (default: %(default)s)")
    ap.add_argument("--le", default=default_le_root(), help="Live Editor folder (default: %(default)s, or TURBO_LE_ROOT)")
    ap.add_argument("--out", help="output folder (default: <le>\\turbo\\callnames\\masters)")
    ap.add_argument("--lang", nargs="*", help="only these language codes (e.g. ita_it)")
    ap.add_argument("--dry-run", action="store_true", help="read and report, write nothing")
    ap.add_argument("--self-test", action="store_true", help="run the built-in test on synthetic workbooks and exit")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    try:
        import openpyxl  # noqa: F401
    except ImportError:
        print("error: openpyxl is not installed (python -m pip install openpyxl)", file=sys.stderr)
        return 2
    out = a.out or os.path.join(a.le, "turbo", "callnames", "masters")
    rc, res = run(a.root, out, set(x.lower() for x in a.lang) if a.lang else None, a.dry_run)
    if res:
        print(f"{len(res)} language list(s) {'read' if a.dry_run else 'written to ' + out}; restart the Turbo window's "
              f"Callname tab (Refresh) to load them")
    return rc


if __name__ == "__main__":
    sys.exit(main())
