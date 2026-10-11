#!/usr/bin/env python3
"""Turbo's callname files for every commentary language the game has, in one run: masters\\<lang>.json (players with
their own recording, generic callnames, audio links) and spoken_<lang>.txt (the spoken generic callnames).
DRY RUN BY DEFAULT: it reads and reports; nothing is written without --write.

Languages: the packs installed in the game folder, found as Turbo's GUI finds them (callnames.cpp
installed_commentary_packs): <game>\\commentary\\commentaryfull_<lang>[.toc] = downloaded (the language picked in the
game's audio settings), <game>\\Data\\Win32\\commentaryfull_<lang>.toc = the base game's; plus every --lang.
--no-detect: only the --lang ones. --all-workbooks: also every language that has a master workbook.

Per language L:
  a. FC 27 bank and master (only with --extract, and only for a DOWNLOADED pack: fc27_commentary.py cannot read the
     base game's pack, whose cas data is in the install chunks):
       fc27_commentary.py --lang L --game <game> --out <raw>\\L_bank.json --wav <audio>
       build_callname_master.py --bank <raw>\\L_bank.json --players <raw>\\turbo_table_players.csv
         --edited <raw>\\turbo_table_editedplayernames.csv --names <raw>\\bridge_names.txt
         --commentary <raw>\\bridge_commentary.txt --template <the FC 26 workbook of L>
         --out-xlsm <masters>\\<its folder>\\<its name>_master_fc27.xlsm --out-json <masters>\\L.json --wav-dir <audio>
     <masters> = <le>\\turbo_dev\\masters, <raw> = <masters>\\raw (the career database exports: Turbo Tools > Exports >
     Export tables), <audio> = --wav-dir L=DIR, else <mods>\\i27 for ita_it (the 2026-10-04 extraction), else
     <mods>\\<L>_fc27. Without --extract the FC 27 master already on disk (if any) is used as it is.
  b. masters JSON: import_callname_masters.py (run in this process) over --mods and <masters>: the FC 27 master
     (<name>_master_fc27.xls[mx]) when there is one, else the FC 26 workbook ("game": "fc26"); --wav-dir passes on.
  c. spoken_L.txt: from the FC 27 bank <raw>\\L_bank.json (its generic rows; --spoken-intensity any | 1 | 2, default
     any) when the master is an FC 27 one of the same game build; else the master's generic ids (for an FC 26 list:
     the FC 26 generic ids, not FC 27's bank). Ids 900000..965000 only (Turbo ignores the others). The line under the
     header says which source was used.
  d. audio check: per family (generic, real, real_link, real_high, generic_high) the ids with at least one segment
     wav on disk (<wav_dir>\\<family>\\<prefix>_<seg>_<seg>.wav, the names Turbo plays), the ids without a wav and
     without a segment; written into the JSON as "audio_check" and printed.
  e. --write: <out>\\masters\\L.json and <out>\\spoken_L.txt, <out> = --out or <le>\\turbo\\callnames. An --out inside a
     git worktree (a .git above it) is refused: the JSONs hold real player names and stay on this PC; putting anything
     into the repository is a separate, human decision.
At the end: one table row per language.

usage: python turbo/tools/replicate_language_structure.py [--le DIR] [--mods DIR] [--game DIR] [--lang L ...]
          [--no-detect] [--all-workbooks] [--out DIR] [--write | --dry-run] [--extract] [--wav-dir L=DIR ...]
          [--spoken-intensity any|1|2]
       python turbo/tools/replicate_language_structure.py --self-test
"""

import argparse
import datetime
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import import_callname_masters as icm  # noqa: E402 - the importer next to this file

DEFAULT_LE = r"C:\FC 27 Live Editor"
DEFAULT_GAME = r"C:\Program Files\EA Games\EA SPORTS FC 27"
DEFAULT_MODS = r"C:\FC_Tools\My Mods"
PACK_PREFIX = "commentaryfull_"
SPOKEN_MIN, SPOKEN_MAX = 900000, 965000   # Turbo's kCallnameMin / kCallnameMax (callnames.h)
# JSON family -> (id list in the JSON, wav prefix). The wav folder is <wav_dir>\<family> (any case).
AUDIO_FAMILIES = (("generic", "generic_ids", "pSIMPLE_SURNAME"), ("real", "real_simple_players", "pPLAYER_NAMES_SIMPLE"),
                  ("real_link", "real_link_players", "pPLAYER_NAMES_LINK"),
                  ("real_high", "real_high_players", "pPLAYER_NAMES_HIGH"),
                  ("generic_high", "generic_high_ids", "pPLAYER_NAMES_HIGH"))
RAW_INPUTS = (("--players", "turbo_table_players.csv"), ("--edited", "turbo_table_editedplayernames.csv"),
              ("--names", "bridge_names.txt"), ("--commentary", "bridge_commentary.txt"))


# ------------------------------------------------------------------------------------------------ languages
def pack_code(file_name, toc):
    """'ita_it' for commentaryfull_ita_it (folder) / commentaryfull_ita_it.toc (file), else '' (callnames.cpp)."""
    n = file_name.lower()
    if not n.startswith(PACK_PREFIX):
        return ""
    code = n[len(PACK_PREFIX):]
    if toc:
        if not code.endswith(".toc"):
            return ""
        code = code[:-4]
    if len(code) != 6 or code[3] != "_" or not (code[:3] + code[4:]).isalpha():
        return ""
    return code


def installed_packs(game):
    """{code: downloaded?} of the commentary packs in the game folder (folder and toc NAMES only)."""
    out = {}
    for sub, downloaded in ((os.path.join(game, "commentary"), True), (os.path.join(game, "Data", "Win32"), False)):
        try:
            with os.scandir(sub) as it:
                entries = list(it)
        except OSError:
            continue
        for e in entries:
            code = pack_code(e.name, not e.is_dir())
            if code:
                out[code] = out.get(code, False) or downloaded
    return out


# ------------------------------------------------------------------------------------------------ helpers
def git_worktree_above(path):
    """The folder holding a .git (file or folder) at or above path, else None."""
    p = os.path.abspath(path)
    while True:
        if os.path.exists(os.path.join(p, ".git")):
            return p
        parent = os.path.dirname(p)
        if parent == p:
            return None
        p = parent


def read_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def subfolder(folder, name):
    """<folder>\\<name> matched without case (br has Generic\\ and Real\\), else None."""
    try:
        with os.scandir(folder) as it:
            return next((e.path for e in it if e.is_dir() and e.name.lower() == name), None)
    except OSError:
        return None


def audio_check(data):
    """Per family: ids, with_wav (at least one listed segment's wav on disk), without_wav, without_segment."""
    wav_dir = data.get("wav_dir")
    out = {"wav_dir": wav_dir, "checked": datetime.datetime.now().astimezone().isoformat(timespec="seconds")}
    if not wav_dir:
        out["note"] = "no wav_dir: nothing checked (the play buttons stay off; --wav-dir L=DIR sets one)"
        return out
    segs = data.get("segments") or {}
    for fam, ids_key, prefix in AUDIO_FAMILIES:
        ids = data.get(ids_key)
        if ids is None and fam == "real":
            ids = data.get("real_players", [])   # an FC 26 list: real_players are the 'real' rows
        if not ids:
            continue
        folder = subfolder(wav_dir, fam)
        files = set()
        if folder:
            files = {f.lower() for f in os.listdir(folder)}
        r = {"folder": folder, "ids": len(ids), "with_wav": 0, "without_wav": 0, "without_segment": 0}
        fam_segs = segs.get(fam) or {}
        for i in ids:
            listed = fam_segs.get(str(i))
            if not listed:
                r["without_segment"] += 1
            elif any(f"{prefix}_{s}_{s}.wav".lower() in files for s in listed):
                r["with_wav"] += 1
            else:
                r["without_wav"] += 1
        out[fam] = r
    return out


def spoken_ids(lang, data, raw, intensity, log):
    """(sorted ids, source text) for spoken_<lang>.txt."""
    bank_path = os.path.join(raw, f"{lang}_bank.json")
    if data.get("game") == "fc27":
        bank = read_json(bank_path)
        if isinstance(bank, dict) and str(bank.get("lang", "")).lower() == lang and bank.get("generic"):
            mb, bb = data.get("game_build"), bank.get("game_build")
            if mb and bb and mb != bb:
                log(f"{lang}: WARNING: the bank {bank_path} is of game build {bb}, the master of {mb}: spoken list from the "
                    f"master (run with --extract --write to rebuild both)")
            else:
                ids = sorted({int(r["commentaryid"]) for r in bank["generic"]
                              if (intensity == "any" or int(intensity) in (r.get("intensity") or []))
                              and SPOKEN_MIN <= int(r["commentaryid"]) <= SPOKEN_MAX})
                return ids, (f"FC 27 bank {bank_path} (game build {bb}), generic rows at player intensity {intensity}, "
                             f"ids {SPOKEN_MIN}..{SPOKEN_MAX}")
    label = "FC 27 master" if data.get("game") == "fc27" else "FC 26 list (not FC 27's bank)"
    ids = sorted(g for g in data.get("generic_ids", []) if SPOKEN_MIN <= g <= SPOKEN_MAX)
    return ids, f"generic ids of the {label} {data.get('source')}, ids {SPOKEN_MIN}..{SPOKEN_MAX}"


def write_spoken(path, lang, ids, source):
    """#turbo-spoken header (Turbo checks the count), one comment line, one id per line; CRLF as the files before."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".tmp_", suffix=".txt", dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\r\n") as f:
            f.write(f"#turbo-spoken {lang} {len(ids)}\n# source: {source}\n")
            f.writelines(f"{i}\n" for i in ids)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def default_audio(lang, mods, wav_dirs):
    if wav_dirs.get(lang):
        return wav_dirs[lang]
    if lang in icm.FC27_AUDIO_FOLDER:
        return os.path.join(mods, icm.FC27_AUDIO_FOLDER[lang])
    return os.path.join(mods, f"{lang}_fc27")


def extraction_plan(lang, le, mods, game, packs, books, wav_dirs):
    """([commands], note): step a for one language, or no commands and the reason."""
    if not packs.get(lang):
        why = "base game pack" if lang in packs else "no pack installed"
        return [], f"FC 27 bank not readable ({why}: fc27_commentary.py reads downloaded packs only)"
    template = next((p for p, g in books.get(lang, []) if g == "fc26"), None)
    if not template:
        return [], "no FC 26 workbook of this language to use as the template"
    masters = os.path.join(le, "turbo_dev", "masters")
    raw = os.path.join(masters, "raw")
    missing = [f for _o, f in RAW_INPUTS if not os.path.isfile(os.path.join(raw, f))]
    if missing:
        return [], f"missing in {raw}: {', '.join(missing)} (Turbo Tools > Exports > Export tables)"
    tools = HERE
    bank = os.path.join(raw, f"{lang}_bank.json")
    audio = default_audio(lang, mods, wav_dirs)
    stem = icm.workbook_kind(os.path.basename(template))[0]
    out_xlsm = os.path.join(masters, os.path.basename(os.path.dirname(template)), f"{stem}_master_fc27.xlsm")
    cmds = [[sys.executable, os.path.join(tools, "fc27_commentary", "fc27_commentary.py"), "--lang", lang, "--game", game,
             "--out", bank, "--wav", audio],
            [sys.executable, os.path.join(tools, "build_callname_master.py"), "--bank", bank]
            + [x for o, f in RAW_INPUTS for x in (o, os.path.join(raw, f))]
            + ["--template", template, "--out-xlsm", out_xlsm, "--out-json", os.path.join(masters, f"{lang}.json"),
               "--lang", lang, "--wav-dir", audio]]
    return cmds, ""


def run_command(cmd):
    return subprocess.run(cmd).returncode


# ------------------------------------------------------------------------------------------------ the pipeline
def replicate(le=DEFAULT_LE, mods=DEFAULT_MODS, game=DEFAULT_GAME, langs=(), detect=True, all_workbooks=False,
              out=None, write=False, extract=False, wav_dirs=None, spoken_intensity="any", runner=run_command, log=print):
    """Returns (rc, {lang: summary}). rc 0 = every language done, 1 = some failed, 2 = refused."""
    wav_dirs = wav_dirs or {}
    out = out or os.path.join(le, "turbo", "callnames")
    if write:
        repo = git_worktree_above(out)
        if repo:
            log(f"error: --out {out} is inside the git worktree {repo}: refused (the master JSONs hold real player names "
                f"and stay local; copying into the repository is a separate decision)")
            return 2, {}
    masters_root = os.path.join(le, "turbo_dev", "masters")
    raw = os.path.join(masters_root, "raw")
    roots = [r for r in (mods, masters_root) if os.path.isdir(r)]
    packs = installed_packs(game) if detect else {}
    books = icm.find_workbooks(roots)
    wanted = set(packs) | {x.lower() for x in langs or ()}
    if all_workbooks:
        wanted |= set(books)
    log(f"mode: {'WRITE to ' + out if write else 'dry run (nothing written; --write writes)'}")
    log("packs in the game folder: " + (", ".join(f"{c} ({'downloaded' if d else 'base game'})" for c, d in sorted(packs.items()))
                                        or ("none found under " + game if detect else "not looked for (--no-detect)")))
    if not wanted:
        log("error: no language: no pack found and no --lang given")
        return 1, {}
    summary, failed = {}, 0
    for lang in sorted(wanted):
        s = {"pack": ("downloaded" if packs[lang] else "base game") if lang in packs else "-", "written": False}
        summary[lang] = s
        # a. FC 27 bank + master
        if extract:
            cmds, note = extraction_plan(lang, le, mods, game, packs, books, wav_dirs)
            if note:
                log(f"{lang}: extract: skipped: {note}")
                s["extract"] = "skipped: " + note
            for cmd in cmds:
                log(f"{lang}: extract: {'would run' if not write else 'running'}: " + subprocess.list2cmdline(cmd))
                if write:
                    rc = runner(cmd)
                    if rc != 0:
                        log(f"{lang}: extract: FAILED (exit {rc}); the FC 27 master already on disk (if any) is used")
                        s["extract"] = f"failed (exit {rc})"
                        break
            else:
                if cmds:
                    s["extract"] = "done" if write else "planned"
            if write and cmds and s.get("extract") == "done":
                books = icm.find_workbooks(roots)   # the new FC 27 master
        # b. the master JSON
        logs = []
        rc, res = icm.run(roots, None, langs={lang}, dry_run=True, log=logs.append, wav_dirs=wav_dirs)
        for line in logs:
            log("  " + line)
        data = res.get(lang)
        if not data:
            log(f"{lang}: FAILED: no master workbook could be imported (looked in {', '.join(roots) or 'nothing'})")
            s["error"] = "no master workbook"
            failed += 1
            continue
        # c. spoken list
        ids, source = spoken_ids(lang, data, raw, spoken_intensity, log)
        # d. audio check
        data["audio_check"] = audio_check(data)
        s.update({"game": data["game"], "source": data["source"], "real_players": len(data["real_players"]),
                  "generic_ids": len(data["generic_ids"]), "spoken": len(ids), "spoken_source": source,
                  "wav_dir": data.get("wav_dir"), "audio_check": data["audio_check"]})
        for fam, _k, _p in AUDIO_FAMILIES:
            a = data["audio_check"].get(fam)
            if a:
                log(f"{lang}: audio {fam}: {a['with_wav']} of {a['ids']} ids with a wav, {a['without_wav']} without, "
                    f"{a['without_segment']} without a segment ({a['folder'] or 'no ' + fam + ' folder'})")
        if "note" in data["audio_check"]:
            log(f"{lang}: audio: {data['audio_check']['note']}")
        log(f"{lang}: spoken_{lang}.txt: {len(ids)} ids from the {source}")
        # e. write
        if write:
            jp = os.path.join(out, "masters", f"{lang}.json")
            sp = os.path.join(out, f"spoken_{lang}.txt")
            icm.write_json(jp, data)
            write_spoken(sp, lang, ids, source)
            s["written"] = True
            log(f"{lang}: wrote {jp} and {sp}")
    print_table(summary, log)
    return (1 if failed else 0), summary


def print_table(summary, log):
    log("")
    log(f"{'lang':7} {'pack':10} {'game':5} {'real':>6} {'generic':>7} {'spoken':>6}  {'wavs real':>11} {'generic':>11} "
        f"{'link':>9} {'high':>9}  {'written':7} wav_dir / source")

    def w(s, fam):
        a = (s.get("audio_check") or {}).get(fam)
        return f"{a['with_wav']}/{a['ids']}" if a else "-"

    for lang, s in sorted(summary.items()):
        if "error" in s:
            log(f"{lang:7} {s['pack']:10} FAILED: {s['error']}")
            continue
        log(f"{lang:7} {s['pack']:10} {s['game']:5} {s['real_players']:>6} {s['generic_ids']:>7} {s['spoken']:>6}  "
            f"{w(s, 'real'):>11} {w(s, 'generic'):>11} {w(s, 'real_link'):>9} {w(s, 'real_high'):>9}  "
            f"{'yes' if s['written'] else 'no':7} {s['wav_dir'] or 'NO wav_dir'} / {s['source']}")


# ------------------------------------------------------------------------------------------------ self-test
def self_test():
    """Synthetic game folder, workbooks, bank and wavs in a temporary folder: detection, dry run, write, spoken lists
    from the bank / the master, audio check, the ned\\audio folder, the git-worktree refusal, the extraction plan."""
    import openpyxl

    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    with tempfile.TemporaryDirectory() as tmp:
        le, mods, game = os.path.join(tmp, "LE"), os.path.join(tmp, "My Mods"), os.path.join(tmp, "game")

        def touch(*parts):
            p = os.path.join(tmp, *parts)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w").close()
            return p

        def book(rel, rows):
            p = os.path.join(mods, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            wb = openpyxl.Workbook()
            ws = wb.active
            ws.title = "callnames"
            ws.append(("SegmentID", "VariationId", "playerid", "name", "type", "Play Audio"))
            for r in rows:
                ws.append(r)
            wb.save(p)
            return p

        def files():
            return sorted(os.path.relpath(os.path.join(d, f), tmp) for d, _s, fs in os.walk(tmp) for f in fs)

        # the game: ita_it downloaded (folder + toc), eng_us base, a World Cup bank that is no pack
        os.makedirs(os.path.join(game, "commentary", "commentaryfull_ita_it"))
        touch("game", "commentary", "commentaryfull_ita_it.toc")
        touch("game", "Data", "Win32", "commentaryfull_eng_us.toc")
        touch("game", "Data", "Win32", "commentarywc_por_br.toc")
        # workbooks: FC 26 Italian (the template), the FC 27 Italian master with the bank families, English, Dutch
        it26 = book("ita/italy_master.xlsm", [(1, 1, 10, "Uno", "real"), (2, 2, 900001, "Rossi", "generic")])
        book("i27/italy_master_fc27.xlsx", [(3, 1, 10, "Uno", "real"), (4, 2, 20, "Due", "real link"),
                                            (5, 3, 30, "Tre", "real high"), (6, 4, 900001, "Rossi", "generic"),
                                            (7, 5, 900002, "Bianchi", "generic"), (8, 6, 970001, "Big", "generic")])
        book("eng/uk_master.xlsm", [(1, 1, 40, "Forty", "real"), (2, 2, 41, "FortyOne", "real"),
                                    (3, 3, 900010, "Smith", "generic"), (0, 4, 900011, "Jones", "generic")])
        book("ned/dutch_master.xlsm", [(1, 1, 50, "Vijftig", "real"), (2, 2, 900020, "Jansen", "generic")])
        for rel in (("i27", "generic", "pSIMPLE_SURNAME_6_6.wav"), ("i27", "generic", "pSIMPLE_SURNAME_7_7.wav"),
                    ("i27", "real", "pPLAYER_NAMES_SIMPLE_3_3.wav"), ("i27", "real_link", "pPLAYER_NAMES_LINK_4_4.wav"),
                    ("eng", "Generic", "pSIMPLE_SURNAME_0_0.wav"), ("eng", "Real", "pPLAYER_NAMES_SIMPLE_1_1.wav"),
                    ("ned", "audio", "generic", "pSIMPLE_SURNAME_2_2.wav"), ("ned", "audio", "real", "pPLAYER_NAMES_SIMPLE_1_1.wav")):
            touch("My Mods", *rel)
        os.makedirs(os.path.join(mods, "i27", "real_high"))   # 30's HIGH wav is missing
        # the FC 27 bank and build_callname_master's JSON of the Italian master (same build)
        raw = os.path.join(le, "turbo_dev", "masters", "raw")
        os.makedirs(raw)
        bank = {"turbo_bank": 1, "lang": "ita_it", "game_build": "1.0.test",
                "generic": [{"segment": 6, "commentaryid": 900001, "intensity": [2]},
                            {"segment": 7, "commentaryid": 900002, "intensity": [1]},
                            {"segment": 8, "commentaryid": 970001, "intensity": [2]}]}
        with open(os.path.join(raw, "ita_it_bank.json"), "w", encoding="utf-8") as f:
            json.dump(bank, f)
        with open(os.path.join(le, "turbo_dev", "masters", "ita_it.json"), "w", encoding="utf-8") as f:
            json.dump({"language": "ita_it", "game": "fc27", "game_build": "1.0.test", "source": "x\\i27\\italy_master_fc27.xlsx"}, f)
        out = os.path.join(le, "turbo", "callnames")
        kw = dict(le=le, mods=mods, game=game)

        check(installed_packs(game) == {"eng_us": False, "ita_it": True} and pack_code("commentaryfull_ita_it.toc", True) == "ita_it"
              and pack_code("commentarywc_por_br.toc", True) == "" and pack_code("commentaryfull_x.toc", True) == "",
              f"packs as the GUI finds them: {installed_packs(game)}")
        # 1. the default is a dry run: nothing written anywhere
        before = files()
        logs = []
        rc, s = replicate(log=logs.append, **kw)
        check(rc == 0 and sorted(s) == ["eng_us", "ita_it"], f"dry run over the installed packs: rc {rc}, {sorted(s)}")
        check(files() == before and not os.path.exists(out), "the dry run wrote nothing")
        check(any(l.startswith("mode: dry run") for l in logs), "the mode is said")
        check(s["ita_it"]["pack"] == "downloaded" and s["eng_us"]["pack"] == "base game", "pack kinds")
        # 2. --write: exactly the masters JSON + spoken list per language, under --out only
        logs = []
        rc, s = replicate(write=True, log=logs.append, **kw)
        written = sorted(set(files()) - set(before))
        rel = lambda *p: os.path.relpath(os.path.join(out, *p), tmp)  # noqa: E731
        check(rc == 0 and written == sorted([rel("masters", "eng_us.json"), rel("masters", "ita_it.json"),
                                             rel("spoken_eng_us.txt"), rel("spoken_ita_it.txt")]),
              f"written: exactly the 4 files under --out: {written}")
        it = read_json(os.path.join(out, "masters", "ita_it.json"))
        check(it["game"] == "fc27" and it["real_players"] == [10, 20] and it["real_high_players"] == [30]
              and it.get("game_build") == "1.0.test" and it["wav_dir"] == os.path.join(mods, "i27"),
              f"ita_it from the FC 27 master: {it['real_players']}, build {it.get('game_build')}, {it.get('wav_dir')}")
        ac = it["audio_check"]
        check(ac["generic"] == {"folder": os.path.join(mods, "i27", "generic"), "ids": 3, "with_wav": 2, "without_wav": 1,
                                "without_segment": 0}
              and (ac["real"]["with_wav"], ac["real"]["ids"]) == (1, 1) and (ac["real_link"]["with_wav"], ac["real_link"]["ids"]) == (1, 1)
              and (ac["real_high"]["with_wav"], ac["real_high"]["without_wav"]) == (0, 1), f"audio check per family: {ac}")
        with open(os.path.join(out, "spoken_ita_it.txt"), "rb") as f:
            sp = f.read().decode("utf-8")
        check(sp.startswith("#turbo-spoken ita_it 2\r\n# source: FC 27 bank ") and sp.endswith("900001\r\n900002\r\n")
              and "intensity any" in sp, "spoken_ita_it.txt from the bank (any intensity, 970001 out of range), CRLF")
        with open(os.path.join(out, "spoken_eng_us.txt"), "rb") as f:
            sp = f.read().decode("utf-8")
        check(sp.startswith("#turbo-spoken eng_us 2\r\n# source: generic ids of the FC 26 list") and sp.endswith("900010\r\n900011\r\n"),
              "spoken_eng_us.txt from the FC 26 list's generic ids, said so")
        en = read_json(os.path.join(out, "masters", "eng_us.json"))
        check(en["game"] == "fc26" and en["audio_check"]["generic"]["with_wav"] == 1 and en["audio_check"]["real"]["with_wav"] == 1
              and en["audio_check"]["real"]["without_wav"] == 1, f"eng_us: Generic\\ / Real\\ in any case, segment 0: {en['audio_check']}")
        check(any(l.startswith("eng_us ") and "base game" in l and " yes " in l for l in logs)
              and any(l.startswith("lang ") for l in logs), "the table")
        # 3. --spoken-intensity 2 (Q4), a bank of another build, --lang / --no-detect, ned\audio, --wav-dir
        _, s2 = replicate(spoken_intensity="2", log=lambda x: None, **kw)
        check(s2["ita_it"]["spoken"] == 1 and "intensity 2" in s2["ita_it"]["spoken_source"], "--spoken-intensity 2: only 900001")
        bank["game_build"] = "1.0.other"
        with open(os.path.join(raw, "ita_it_bank.json"), "w", encoding="utf-8") as f:
            json.dump(bank, f)
        logs = []
        _, s3 = replicate(log=logs.append, **kw)
        check(s3["ita_it"]["spoken"] == 2 and "generic ids of the FC 27 master" in s3["ita_it"]["spoken_source"]
              and any("WARNING" in l and "1.0.other" in l for l in logs), "a bank of another build: master's ids + warning")
        _, s4 = replicate(langs=["dut_nl"], detect=False, log=lambda x: None, **kw)
        check(sorted(s4) == ["dut_nl"] and s4["dut_nl"]["wav_dir"] == os.path.join(mods, "ned", "audio")
              and s4["dut_nl"]["audio_check"]["generic"]["with_wav"] == 1 and s4["dut_nl"]["audio_check"]["real"]["with_wav"] == 1,
              f"--no-detect --lang dut_nl: ned\\audio: {s4['dut_nl'].get('wav_dir')}")
        _, s5 = replicate(langs=["dut_nl"], log=lambda x: None, wav_dirs={"dut_nl": ""}, **kw)
        check(sorted(s5) == ["dut_nl", "eng_us", "ita_it"] and s5["dut_nl"]["wav_dir"] is None
              and "note" in s5["dut_nl"]["audio_check"], "--lang adds to the packs; --wav-dir dut_nl= links no audio")
        _, s6 = replicate(detect=False, all_workbooks=True, log=lambda x: None, **kw)
        check(sorted(s6) == ["dut_nl", "eng_us", "ita_it"], f"--all-workbooks: {sorted(s6)}")
        # 4. never into a git worktree
        repo = os.path.join(tmp, "repo")
        os.makedirs(os.path.join(repo, "turbo", "package"))
        touch("repo", ".git")
        logs = []
        before = files()
        rc, _ = replicate(write=True, out=os.path.join(repo, "turbo", "package", "turbo", "callnames"), log=logs.append, **kw)
        check(rc == 2 and files() == before and any("git worktree" in l for l in logs), "an --out inside a git worktree is refused")
        # 5. --extract: the plan for the downloaded pack, a note for the base pack; with --write the commands run in order
        logs = []
        replicate(extract=True, log=logs.append, **kw)
        check(any(l.startswith("ita_it: extract: skipped") and "turbo_table_players.csv" in l for l in logs),
              "--extract without the career exports: skipped, the missing files named")
        for f in ("turbo_table_players.csv", "turbo_table_editedplayernames.csv", "bridge_names.txt", "bridge_commentary.txt"):
            touch("LE", "turbo_dev", "masters", "raw", f)
        logs = []
        before = files()
        replicate(extract=True, log=logs.append, **kw)
        plan = [l for l in logs if "extract:" in l]
        check(any(l.startswith("ita_it: extract: would run:") and "fc27_commentary.py" in l and "--lang ita_it" in l for l in plan)
              and any(l.startswith("ita_it: extract: would run:") and "build_callname_master.py" in l
                      and os.path.basename(it26) in l and "italy_master_fc27.xlsm" in l for l in plan)
              and any(l.startswith("eng_us: extract: skipped") and "base game pack" in l for l in plan)
              and files() == before,
              "--extract dry run: ita_it planned (fc27_commentary, then build_callname_master with the FC 26 template)")
        calls = []
        _, s7 = replicate(extract=True, write=True, langs=["ita_it"], detect=True, log=lambda x: None,
                          runner=lambda cmd: calls.append(cmd) or 0, **kw)
        check(len(calls) == 2 and calls[0][1].endswith("fc27_commentary.py") and calls[1][1].endswith("build_callname_master.py")
              and calls[0][calls[0].index("--wav") + 1] == os.path.join(mods, "i27")
              and calls[1][calls[1].index("--out-xlsm") + 1] == os.path.join(le, "turbo_dev", "masters", "ita", "italy_master_fc27.xlsm")
              and s7["ita_it"].get("extract") == "done", "--extract --write: fc27_commentary then build_callname_master")
        calls.clear()
        _, s8 = replicate(extract=True, write=True, log=lambda x: None, runner=lambda cmd: calls.append(cmd) or 3, **kw)
        check(len(calls) == 1 and s8["ita_it"].get("extract") == "failed (exit 3)" and s8["ita_it"]["written"],
              "a failed extraction stops there; the master on disk is still imported")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--le", default=DEFAULT_LE, help="Live Editor folder (default: %(default)s)")
    ap.add_argument("--mods", default=DEFAULT_MODS, help="folder of the master workbooks and audio (default: %(default)s)")
    ap.add_argument("--game", default=DEFAULT_GAME, help="FC 27 folder, for the installed packs (default: %(default)s)")
    ap.add_argument("--lang", nargs="*", default=[], help="also these languages (e.g. dut_nl)")
    ap.add_argument("--no-detect", action="store_true", help="do not look at the game folder: only --lang")
    ap.add_argument("--all-workbooks", action="store_true", help="also every language that has a master workbook")
    ap.add_argument("--out", help="output folder (default: <le>\\turbo\\callnames); never inside a git worktree")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="write the files (default: dry run)")
    mode.add_argument("--dry-run", action="store_true", help="read and report only (the default)")
    ap.add_argument("--extract", action="store_true", help="step a: extract the FC 27 bank and build the FC 27 master "
                    "(downloaded packs; with --write it runs fc27_commentary.py and build_callname_master.py)")
    ap.add_argument("--wav-dir", action="append", metavar="LANG=DIR", help="audio folder of one language; LANG= none")
    ap.add_argument("--spoken-intensity", choices=("any", "1", "2"), default="any",
                    help="spoken list from an FC 27 bank: generic rows at this player intensity (default: any)")
    ap.add_argument("--self-test", action="store_true", help="run the built-in test on synthetic data and exit")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    try:
        wav_dirs = icm.parse_lang_map(a.wav_dir, "--wav-dir")
    except ValueError as e:
        ap.error(str(e))
    try:
        import openpyxl  # noqa: F401
    except ImportError:
        print("error: openpyxl is not installed (python -m pip install openpyxl)", file=sys.stderr)
        return 2
    rc, _ = replicate(a.le, a.mods, a.game, a.lang, not a.no_detect, a.all_workbooks, a.out, a.write, a.extract, wav_dirs,
                      a.spoken_intensity)
    return rc


if __name__ == "__main__":
    sys.exit(main())
