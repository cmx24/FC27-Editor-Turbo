#!/usr/bin/env python3
"""FC Editor -> Turbo player preset converter.

Turns players that FC Editor (decoruiz) has prepared - typically fetched from CMTracker and exported as a
`.player` file - into Turbo player presets (turbo_output\\players\\<name>_<id>.json + the miniface .dds), which
Turbo's Players > Import / "Create player..." read as they are.

Offline: no network, no keys, no calls into FC Editor. It only reads files FC Editor already wrote:
  * <name>.player            UTF-16 TSV, header + one row; the four name columns hold TEXT, `number` = shirt number
  * <fce>\\legacy\\imgAssets\\heads\\p<id>.DDS   the miniface (DXT5), copied as is
  * <fce>\\db\\27\\fifa_ng_db-meta.xml           FC 27 field ranges (values are clamped to them)
  * <fce>\\_temp\\players.txt (+ names tables)  with --playerid: a player of the current FC Editor workspace

FC 26 -> FC 27 value changes (FC Editor does the same): skintonecode 1..10 -> 10..100, contractvaliduntil to
2000..2120, other fields clamped to the FC 27 range. The game of a file is detected from its columns
(FC 27 has growthprofile / wage / releaseclause / role6..9) or given with --source-game.

Usage:
  python fce_to_preset.py "C:\\...\\200001 - Some Player.player" [more files or folders]
  python fce_to_preset.py --playerid 192985            # from FC Editor's _temp tables
Options: --fce DIR  --out DIR  --source-game 26|27  --teamid N  --keep-name-ids  --dry
"""
import argparse, csv, datetime, glob, json, os, re, shutil, sys, unicodedata

DEFAULT_FCE = r"C:\FC_Tools\FC Editor"
DEFAULT_OUT = r"C:\FC 27 Live Editor\turbo_output\players"
NAME_COLS = {"firstnameid": "firstname", "lastnameid": "surname", "commonnameid": "commonname",
             "playerjerseynameid": "playerjerseyname"}
FC27_ONLY = {"growthprofile", "wage", "releaseclause", "role6", "role7", "role8", "role9"}
FORMAT = "turbo-player-preset"
CONTRACT_DEFAULT = 2030


def read_text(path):
    raw = open(path, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    return raw.decode("utf-8-sig", errors="replace")


def read_tsv(path):
    rows = list(csv.reader(read_text(path).splitlines(), delimiter="\t", quoting=csv.QUOTE_NONE))
    if not rows:
        return [], []
    return rows[0], rows[1:]


def load_ranges(fce, game="27"):
    """{field: (low, high)} of the players table from FC Editor's meta XML, or {} when unavailable."""
    p = os.path.join(fce, "db", game, "fifa_ng_db-meta.xml")
    if not os.path.isfile(p):
        return {}
    xml = open(p, encoding="utf-8", errors="replace").read()
    m = re.search(r'<table name="players".*?</table>', xml, re.S)
    out = {}
    for fld in re.findall(r"<field [^>]*/>", m.group(0) if m else ""):
        def g(k):
            r = re.search(k + r'="(-?\w+)"', fld)
            return r.group(1) if r else None
        if g("name") and g("rangelow") is not None:
            out[g("name")] = (int(g("rangelow")), int(g("rangehigh")))
    return out


def load_game_ranges():
    """Ranges Live Editor reports for the FC 27 players table (turbo/le27/fc27_db_schema.json) - the ones Turbo checks."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "le27", "fc27_db_schema.json")
    try:
        fields = json.load(open(p, encoding="utf-8"))["tables"]["players"]["fields"]
        return {f["name"]: (f["min"], f["max"]) for f in fields}
    except (OSError, KeyError, ValueError):
        return {}


def detect_game(cols):
    return "27" if FC27_ONLY & set(cols) else "26"


def to_int(v):
    try:
        return int(str(v).strip())
    except ValueError:
        try:
            return int(float(str(v).strip()))
        except ValueError:
            return None


def convert_values(players, game, ranges, notes):
    """FC 26 -> FC 27 value changes, then clamp every value to the FC 27 range."""
    if game == "26":
        t = players.get("skintonecode")
        if t is not None and 1 <= t <= 10:
            players["skintonecode"] = t * 10
            notes.append("skintonecode %d -> %d (FC 26 scale 1..10 -> FC 27 10..100)" % (t, t * 10))
        c = players.get("contractvaliduntil")
        if c is not None and not 2000 <= c <= 2120:
            players["contractvaliduntil"] = CONTRACT_DEFAULT
            notes.append("contractvaliduntil %d -> %d" % (c, CONTRACT_DEFAULT))
    for f in list(players):
        r = ranges.get(f)
        if r is None:
            continue
        v = players[f]
        nv = min(max(v, r[0]), r[1])
        if nv != v:
            players[f] = nv
            notes.append("%s %d clamped to %d (FC 27 range %d..%d)" % (f, v, nv, r[0], r[1]))


def build_preset(row, game, ranges, teamid, keep_name_ids):
    """row: {column: text}. Returns (preset dict, notes)."""
    notes = []
    names = {"firstname": "", "surname": "", "playerjerseyname": "", "commonname": ""}
    players = {}
    for col, text in row.items():
        col = col.strip().lower()
        text = (text or "").strip()
        if col in NAME_COLS:
            # FC Editor's .player files carry the name text; a numeric value is a bare name id (no text to use)
            if text and not re.fullmatch(r"-?\d+", text):
                names[NAME_COLS[col]] = text
            elif text and keep_name_ids:
                players[col] = int(text)
        elif col == "number" or col == "":
            continue
        else:
            v = to_int(text)
            if v is not None:
                players[col] = v
    pid = players.get("playerid")
    if pid is None:
        raise ValueError("row has no playerid")
    convert_values(players, game, ranges, notes)
    links = []
    jersey = to_int(row.get("number", ""))
    if teamid is not None:
        links.append({"jerseynumber": jersey if jersey is not None else 0, "teamid": teamid,
                      "national": False, "position": 29})
    label = names["commonname"] or ("%s %s" % (names["firstname"], names["surname"])).strip() or "player"
    preset = {
        "format": FORMAT, "version": 1, "name": label, "playerid": pid, "names": names, "players": players,
        "links": links, "miniface": None, "exported": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "turbo": "fce_to_preset", "source": "FC Editor (game %s)" % game,
    }
    return preset, notes


def safe_name(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii")
    s = re.sub(r"[^\w\-]+", "_", s, flags=re.A).strip("_")
    return (s or "player")[:60]


def find_miniface(fce, pid):
    heads = os.path.join(fce, "legacy", "imgAssets", "heads")
    for ext in ("DDS", "dds"):
        p = os.path.join(heads, "p%d.%s" % (pid, ext))
        if os.path.isfile(p):
            return p
    png = os.path.join(fce, "minifaces_png", "p%d.png" % pid)
    if os.path.isfile(png):
        return png
    return None


def png_to_dds(png, dest):
    try:
        from PIL import Image
        Image.open(png).convert("RGBA").save(dest, format="DDS", pixel_format="DXT5")
        return True
    except Exception as e:  # Pillow missing / too old to write DXT5
        print("   ! could not convert %s to DDS (%s)" % (png, e))
        return False


def write_outputs(preset, fce, out, dry, no_miniface=False):
    pid = preset["playerid"]
    base = "%s_%d" % (safe_name(preset["name"]), pid)
    msg = []
    mini = None if no_miniface else find_miniface(fce, pid)
    if mini:
        dds_name = base + ".dds"
        if dry:
            msg.append("miniface %s" % mini)
            preset["miniface"] = dds_name
        else:
            os.makedirs(out, exist_ok=True)
            dest = os.path.join(out, dds_name)
            ok = True
            if mini.lower().endswith(".png"):
                ok = png_to_dds(mini, dest)
            else:
                shutil.copyfile(mini, dest)
            if ok:
                preset["miniface"] = dds_name
                msg.append("miniface -> " + dds_name)
    else:
        msg.append("no miniface found in FC Editor (heads\\p%d.DDS / minifaces_png)" % pid)
    path = os.path.join(out, base + ".json")
    if not dry:
        os.makedirs(out, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(preset, f, ensure_ascii=False, indent=1)
    return path, msg


def from_player_files(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += sorted(glob.glob(os.path.join(p, "*.player")))
        elif os.path.isfile(p):
            files.append(p)
        else:
            files += sorted(glob.glob(p))
    for f in files:
        header, rows = read_tsv(f)
        for r in rows:
            if r:
                yield f, header, dict(zip(header, r))


def load_name_tables(fce):
    """playernames / dcplayernames: {nameid: text}; editedplayernames: {playerid: row}. Missing tables -> {}."""
    def names(path):
        out = {}
        if os.path.isfile(path):
            h, rows = read_tsv(path)
            if "nameid" in h and "name" in h:
                ni, na = h.index("nameid"), h.index("name")
                for r in rows:
                    if len(r) > max(ni, na):
                        out[r[ni]] = r[na]
        return out
    pn = names(os.path.join(fce, "default", "27", "playernames.txt"))
    pn.update(names(os.path.join(fce, "_temp", "dcplayernames.txt")))
    edited = {}
    ep = os.path.join(fce, "_temp", "editedplayernames.txt")
    if os.path.isfile(ep):
        h, rows = read_tsv(ep)
        for r in rows:
            d = dict(zip(h, r))
            if d.get("playerid"):
                edited[d["playerid"]] = d
    return pn, edited


def from_workspace(fce, pids):
    """Players of FC Editor's current workspace (_temp\\players.txt) by playerid, with names resolved to text."""
    p = os.path.join(fce, "_temp", "players.txt")
    if not os.path.isfile(p):
        sys.exit("FC Editor workspace table not found: " + p)
    header, rows = read_tsv(p)
    pidx = header.index("playerid")
    want = {str(x) for x in pids}
    pn, edited = load_name_tables(fce)
    for r in rows:
        if len(r) <= pidx or r[pidx] not in want:
            continue
        d = dict(zip(header, r))
        e = edited.get(r[pidx])
        for col, key in NAME_COLS.items():
            txt = (e or {}).get(key) if e else None
            if not txt:
                txt = pn.get(d.get(col, ""), "")
            d[col] = txt
        want.discard(r[pidx])
        yield p, header, d
    for missing in sorted(want):
        print("player %s not found in %s" % (missing, p))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help=".player files or folders")
    ap.add_argument("--playerid", type=int, action="append", default=[], help="read this player from FC Editor's _temp tables")
    ap.add_argument("--fce", default=DEFAULT_FCE, help="FC Editor folder (default %s)" % DEFAULT_FCE)
    ap.add_argument("--out", default=DEFAULT_OUT, help="output folder (default Turbo's turbo_output\\players)")
    ap.add_argument("--source-game", choices=("26", "27"), help="game of the files (default: detected from the columns)")
    ap.add_argument("--teamid", type=int, help="club link to put in the preset (position 29 = reserve)")
    ap.add_argument("--keep-name-ids", action="store_true", help="keep numeric name ids (only valid for the same game's name tables)")
    ap.add_argument("--no-miniface", action="store_true")
    ap.add_argument("--dry", action="store_true", help="show what would be written")
    a = ap.parse_args(argv)
    if not a.paths and not a.playerid:
        ap.error("give .player files / folders or --playerid")
    ranges = load_ranges(a.fce)
    ranges.update(load_game_ranges())  # the game's own limits win over FC Editor's meta XML
    if not ranges:
        print("note: FC 27 meta XML not found under %s - values are not clamped here (Turbo range-checks on import)" % a.fce)
    sources = list(from_player_files(a.paths)) + (list(from_workspace(a.fce, a.playerid)) if a.playerid else [])
    if not sources:
        sys.exit("nothing to convert")
    done = 0
    for src, header, row in sources:
        game = a.source_game or detect_game(header)
        if a.playerid and src.endswith("players.txt"):
            game = a.source_game or "27"
        try:
            preset, notes = build_preset(row, game, ranges, a.teamid, a.keep_name_ids)
        except ValueError as e:
            print("%s: skipped (%s)" % (os.path.basename(src), e))
            continue
        path, msg = write_outputs(preset, a.fce, a.out, a.dry, a.no_miniface)
        print("%s%s  [%s, game %s]" % ("(dry) " if a.dry else "", path, os.path.basename(src), game))
        for n in notes + msg:
            print("   - " + n)
        done += 1
    print("%d player(s) %s" % (done, "planned" if a.dry else "written"))


if __name__ == "__main__":
    main()
