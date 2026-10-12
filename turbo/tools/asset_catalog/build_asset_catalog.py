"""Build Turbo's game asset catalog (docs/asset-catalog.md, schema v1) for the installed FC 27 build.

Read-only on the game, Live Editor and saves. Writes only under <out> (default <LE>\\turbo_output\\asset_catalog):
catalog_<buildkey>.json, catalog_<buildkey>.summary.txt, cache\\*.json (per toc, keyed by size + mtime, so an
unchanged rerun takes seconds) and banks\\<lang>_bank.json: the commentary bank of every installed language is built
(turbo\\tools\\fc27_commentary) when it is missing or its game files changed, so one command does everything.

usage: python -I build_asset_catalog.py [--game DIR] [--le DIR] [--out DIR] [--bank FILE ...] [--no-bank-build]
                                        [--workbook-names FILE] [--team-names-loc FILE ...]
Run it in a Turbo source checkout with Python 3 (numpy makes the first run fast): py -3 -I turbo\\tools\\asset_catalog\\build_asset_catalog.py
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import families  # noqa: E402
import frostbite as fb  # noqa: E402
import legacy  # noqa: E402
import names as namesmod  # noqa: E402

BUILDER = "build_asset_catalog.py 1"
SCHEMA = "turbo-asset-catalog/1"
CACHE_VERSION = 1
DEFAULT_GAME = r"C:\Program Files\EA Games\EA SPORTS FC 27"
DEFAULT_LE = r"C:\FC 27 Live Editor"
FAMILY_KEYS = ("head3d", "miniface", "youthface", "staffface", "legendface", "hair", "facialhair", "boots", "gkglove",
               "tattoo", "accessory", "eyebrow", "skinsurface", "crest", "kit", "ball", "callnames")
MODEL_FAMILIES = ("hair", "facialhair", "boots", "gkglove", "tattoo", "accessory", "ball")
CALL_KINDS = {"generic": "generic", "generic_high": "generic", "real": "simple", "real_link": "link", "real_high": "high",
              "simple": "simple", "link": "link", "high": "high"}


def stat_key(path):
    st = os.stat(path)
    return {"size": int(st.st_size), "mtime": int(st.st_mtime)}


def rel(game, path):
    return os.path.relpath(path, game).replace("\\", "/")


def list_tocs(game):
    """Every toc under Data\\Win32 and Patch\\Win32, except the superbundle layouts and the commentary packs."""
    out = []
    for layer in ("Data", "Patch"):
        for p in sorted(glob.glob(os.path.join(game, layer, "Win32", "**", "*.toc"), recursive=True)):
            r = rel(game, p)
            if "superbundlelayout" in r or os.path.basename(p).startswith("commentary"):
                continue
            out.append(p)
    return out


def cache_name(r):
    return r.replace("/", "_").replace("\\", "_") + ".json"


def read_cache(path, key):
    try:
        with open(path, encoding="utf-8") as f:
            c = json.load(f)
        if c.get("v") == CACHE_VERSION and c.get("key") == key:
            return c
    except (OSError, ValueError):
        pass
    return None


def write_json(path, obj, indent=None):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, ensure_ascii=False, indent=indent, separators=(",", ":") if indent is None else None)
    os.replace(tmp, path)


# ---------------------------------------------------------------- EBX census
def scan_toc(game_io, toc_path):
    with open(toc_path, "rb") as f:
        d = f.read()
    acc, errors, nb = {}, 0, 0
    for locs in fb.parse_toc_bundles(d):
        if not locs:
            continue
        nb += 1
        try:
            families.classify(game_io.manifest(locs), acc)
        except Exception:  # missing cas (partial install) or an unreadable manifest: counted, not fatal
            errors += 1
    return {"result": acc, "bundles": nb, "errors": errors}


def ebx_census(game, game_io, cache_dir, log):
    results, stats, sources = [], {"scanned": 0, "cached": 0, "errors": 0}, {}
    for p in list_tocs(game):
        r = rel(game, p)
        key = stat_key(p)
        sources[r] = key
        cp = os.path.join(cache_dir, cache_name(r))
        c = read_cache(cp, key)
        if c is None:
            t = time.time()
            c = scan_toc(game_io, p)
            c.update({"v": CACHE_VERSION, "key": key, "toc": r})
            write_json(cp, c)
            stats["scanned"] += 1
            log("  scanned %-48s %6d bundles %4d errors %5.1fs" % (r, c["bundles"], c["errors"], time.time() - t))
        else:
            stats["cached"] += 1
        stats["errors"] += c["errors"]
        results.append((r, c["result"]))
    return families.merge(results), stats, sources


# ---------------------------------------------------------------- legacy census
def manifest_hashes(game, game_io):
    """Entry hashes of the ChunkFileCollector manifests (collector EBX in globals.toc, Patch over Data)."""
    chunks = {}
    for layer in ("Patch", "Data"):
        for stem in ("globals", "globalsfull"):
            p = os.path.join(game, layer, "Win32", stem + ".toc")
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    for g, v in fb.parse_toc_chunks(f.read()).items():
                        chunks.setdefault(g, v)
    found, per = {}, {}
    for layer in ("Patch", "Data"):
        p = os.path.join(game, layer, "Win32", "globals.toc")
        if not os.path.isfile(p):
            continue
        with open(p, "rb") as f:
            bundles = fb.parse_toc_bundles(f.read())
        for locs in bundles:
            if not locs or len(found) == len(legacy.COLLECTORS):
                continue
            try:
                ebx = game_io.manifest(locs)
            except Exception:
                continue
            for name in legacy.COLLECTORS:
                if name in found or name not in ebx:
                    continue
                k = ebx.index(name)
                found[name] = fb.read_blocks(game_io.raw(locs[1 + k]), game_io.oodle)
    hashes = set()
    for name, e in found.items():
        n = 0
        for g in legacy.guids_in(e, chunks):
            hs = legacy.parse_manifest_chunk(fb.read_blocks(game_io.raw(chunks[g]), game_io.oodle))
            if hs:
                hashes.update(hs)
                n += len(hs)
        per[name] = n
    return hashes, per


def legacy_census(game, le, game_io, cache_dir, log):
    csvs = [p for p in (os.path.join(le, "legacy_filename_hash_list.csv"),
                        os.path.join(le, "extensions", "legacy_filename_hash_list.csv")) if os.path.isfile(p)]
    inputs = [os.path.join(game, l, "Win32", s + ".toc") for l in ("Data", "Patch") for s in ("globals", "globalsfull")]
    key = {"inputs": {rel(game, p): stat_key(p) for p in inputs if os.path.isfile(p)},
           "csv": {os.path.basename(os.path.dirname(p)) + "/" + os.path.basename(p): stat_key(p) for p in csvs},
           "brute": hashlib.sha1(json.dumps(legacy.BRUTE).encode()).hexdigest()}
    cp = os.path.join(cache_dir, "legacy.json")
    c = read_cache(cp, key)
    if c is not None:
        return c, True
    t = time.time()
    hashes, per = manifest_hashes(game, game_io)
    log("  legacy manifests: %d hashes %s %.1fs" % (len(hashes), per, time.time() - t))
    files = {}  # lower path -> path as listed
    for p in csvs:
        for path in legacy.read_le_csv(p):
            files.setdefault(path.lower(), path)
    paths = list(files.values())
    hs = legacy.hash_many(paths)
    in_game = {p.lower(): p for p, h in zip(paths, hs) if h in hashes}
    named = set(h for h in hs if h in hashes)
    log("  csv: %d paths, %d in the game %.1fs" % (len(paths), len(in_game), time.time() - t))
    gsorted = None
    if legacy.np is not None:
        gsorted = legacy.np.array(sorted(hashes), dtype=legacy.np.uint64)
    brute_new = 0
    for fam, prefix, (lo, hi), sufs in legacy.BRUTE:
        for _i, path in legacy.brute_hits(prefix, lo, hi, sufs, hashes, gsorted):
            if path.lower() not in in_game:
                in_game[path.lower()] = path
                brute_new += 1
            named.add(legacy.path_hash(path))
    log("  brute force: %d files the csv lists do not name %.1fs" % (brute_new, time.time() - t))
    fam = {}
    for lp, path in in_game.items():
        r = legacy.classify_path(path)
        if r is None:
            continue
        f, i, rank = r
        F = fam.setdefault(f, {})
        cur = F.get(str(i))
        if cur is None or list(rank) < cur[1]:
            F[str(i)] = [path, list(rank)]
    c = {"v": CACHE_VERSION, "key": key, "manifest_hashes": len(hashes), "per_collector": per,
         "csv_paths": len(paths), "csv_in_game": len(paths) - sum(1 for h in hs if h not in hashes) if paths else 0,
         "brute_new": brute_new, "unnamed": len(hashes - named),
         "families": {f: {i: v[0] for i, v in F.items()} for f, F in fam.items()},
         "hashes": sorted("%016x" % h for h in hashes)}
    write_json(cp, c)
    return c, False


# ---------------------------------------------------------------- banks
PACK_RX = re.compile(r"^commentary(?:full|launch)_([a-z]{3}_[a-z]{2})\.toc$", re.I)


def installed_languages(game):
    """Commentary languages with a full or launch pack: Data / Patch Win32 and the downloaded <game>\\commentary copy."""
    langs = set()
    for d in (os.path.join(game, "Data", "Win32"), os.path.join(game, "Patch", "Win32"), os.path.join(game, "commentary")):
        try:
            names = os.listdir(d)
        except OSError:
            continue
        for n in names:
            m = PACK_RX.match(n)
            if m:
                langs.add(m.group(1).lower())
    return sorted(langs)


def bank_outdated(path, game_version):
    """Why the bank file must be built again ("" = up to date): missing, another format, another game build, or one of
    the game files it was read from changed (size or time) or is gone."""
    try:
        with open(path, encoding="utf-8") as f:
            b = json.load(f)
    except OSError:
        return "missing"
    except ValueError:
        return "not readable"
    if b.get("turbo_bank") != 1 or not isinstance(b.get("sets"), dict):
        return "old format"
    if game_version != "unknown" and b.get("game_build") not in (None, game_version):
        return "built for game %s" % b.get("game_build")
    for s in b.get("source_files") or ():
        p = s.get("path") if isinstance(s, dict) else None
        if not p or s.get("size") is None:
            continue
        try:
            st = os.stat(p)
        except OSError:
            return "a source file is gone: %s" % os.path.basename(p)
        when = datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds")
        if st.st_size != s.get("size") or when != s.get("modified"):
            return "a source file changed: %s" % os.path.basename(p)
    return ""


def ensure_banks(game, out, game_version, log):
    """Builds banks\\<lang>_bank.json with fc27_commentary for every installed language whose bank is missing or
    outdated. Returns {lang: note} for the summary. A language that fails keeps its old bank (the note says so)."""
    notes = {}
    tool = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fc27_commentary")
    bank_dir = os.path.join(out, "banks")
    os.makedirs(bank_dir, exist_ok=True)
    for lang in installed_languages(game):
        path = os.path.join(bank_dir, "%s_bank.json" % lang)
        why = bank_outdated(path, game_version)
        if not why:
            notes[lang] = "up to date"
            continue
        t = time.time()
        log("  bank %s: %s, building it with fc27_commentary" % (lang, why))
        try:
            if tool not in sys.path:
                sys.path.insert(0, tool)
            import fc27_commentary  # noqa: E402  (turbo\\tools\\fc27_commentary, a sibling folder of this tool)
            tmp = path + ".building"
            rc = fc27_commentary.main(["--lang", lang, "--game", game, "--out", tmp])
            if rc not in (0, None) or not os.path.isfile(tmp):
                raise RuntimeError("fc27_commentary exit %s" % rc)
            os.replace(tmp, path)
            notes[lang] = "built (%s) in %.1fs" % (why, time.time() - t)
        except Exception as ex:  # a failed language keeps its old bank; the run goes on
            notes[lang] = "NOT built (%s): %s%s" % (why, ex, "; the old bank is used" if os.path.isfile(path) else "")
        log("  bank %s: %s" % (lang, notes[lang]))
    return notes


def default_banks(le, out=None):
    """The built banks (<out>\\banks), then, for languages they lack, the older locations."""
    out = out or os.path.join(le, "turbo_output", "asset_catalog")
    built = sorted(glob.glob(os.path.join(out, "banks", "*.json")))
    have = set()
    for p in built:
        m = re.match(r"^([a-z]{3}_[a-z]{2})_bank\.json$", os.path.basename(p), re.I)
        if m:
            have.add(m.group(1).lower())
    extra = []
    for p in [os.path.join(le, "turbo_output", "commentary_banks", "*.json"),
              os.path.join(le, "turbo_output", "asset_research", "callnames", "ita_it_bank_now.json"),
              os.path.join(le, "turbo_output", "asset_research", "callnames", "eng_us_namebank.json")]:
        for q in sorted(glob.glob(p)):
            if os.path.basename(q)[:6].lower() not in have:
                extra.append(q)
    return built + extra


def _row_ids(rows, key):
    out = set()
    for r in rows or ():
        if isinstance(r, dict):
            v = r.get(key)
            if v is None:
                v = r.get("playerid", r.get("commentaryid"))
        else:
            v = r
        try:
            out.add(int(v))
        except (TypeError, ValueError):
            pass
    return out


def set_team(lang, set_name):
    """'eng_us_dr_full' -> 'dr'; 'ita_it_full' -> ''."""
    rest = set_name[len(lang):].strip("_") if set_name.startswith(lang) else set_name
    parts = [p for p in rest.split("_") if p]
    return parts[0] if len(parts) >= 2 else ""


def read_bank(path, game):
    """A fc27_commentary bank (turbo_bank 1, flat lists) or a per-set name bank ({sets: {<lang>_<team>_<kind>: ...}}).
    Returns (lang, {team: {kind: set(ids)}}, sources)."""
    with open(path, encoding="utf-8") as f:
        b = json.load(f)
    lang = b.get("lang") or b.get("language")
    if not lang:
        raise ValueError("no lang")
    teams = {}

    def add(team, kind_key, rows):
        kind = CALL_KINDS.get(kind_key)
        if kind is None:
            return
        ids = _row_ids(rows, "commentaryid" if kind == "generic" else "playerid")
        teams.setdefault(team, {}).setdefault(kind, set()).update(ids)

    if isinstance(b.get("sets"), dict):
        for sname, s in b["sets"].items():
            if not isinstance(s, dict):
                continue
            # fc27_commentary (WP-03): {team, kind, ids: {generic, simple, link, high: [ids]}}; research banks: kind lists
            # straight in the set
            rows_of = s["ids"] if isinstance(s.get("ids"), dict) else s
            team = s["team"] if isinstance(s.get("team"), str) else set_team(lang, sname)
            for k, rows in rows_of.items():
                if isinstance(rows, list):
                    add(team, k, rows)
    else:
        for k in CALL_KINDS:
            if isinstance(b.get(k), list):
                add("", k, b[k])
    sources = {}
    tocs = list(b.get("tocs") or []) + [s.get("path") for s in b.get("source_files") or [] if isinstance(s, dict)]
    for t in tocs:
        if t and t.lower().endswith(".toc") and os.path.isfile(t) and os.path.abspath(t).lower().startswith(os.path.abspath(game).lower()):
            sources[rel(game, t)] = stat_key(t)
    return lang, teams, sources


# ---------------------------------------------------------------- assemble
def ids_of(d):
    return sorted(int(k) for k in d)


def assemble(game_key, game_version, built_at, sources, census, leg, banks):
    fams = {k: {} for k in FAMILY_KEYS}
    heads = census.get("head3d", {})
    proven = {i: h for i, h in heads.items() if families.head_proven(h)}
    fams["head3d"] = {"ids": sorted(proven),
                      "vars": {str(i): sorted(h["vars"]) for i, h in sorted(proven.items())},
                      "toc": {str(i): "".join(sorted(h["toc"])) for i, h in sorted(proven.items()) if h["toc"]},
                      "generic": sorted(i for i, h in proven.items() if families.head_is_generic(h)),
                      "unproven": sorted(i for i in heads if i not in proven)}
    lf = leg["families"]
    for key, src in (("miniface", "miniface"), ("youthface", "youthface"), ("staffface", "staffface"), ("legendface", "legendface")):
        fams[key] = {"ids": ids_of(lf.get(src, {}))}

    def previews(*srcs):
        out = {}
        for s in srcs:
            for i, p in lf.get(s, {}).items():
                out.setdefault(i, p)
        return {k: out[k] for k in sorted(out, key=int)}

    def model(f):
        return ids_of(census.get(f, {}))

    fams["hair"] = {"model": model("hair"), "preview": previews("hair", "hair_old")}
    fams["facialhair"] = {"model": model("facialhair"), "preview": previews("facialhair", "facialhair_old")}
    boots_c = set(census.get("boots", {}))
    boots_ts = set(census.get("boots_ts", {}))
    # Gate = the content/ shoe ids; content_ts-only ids are listed but not writable until checked in game (critic C3)
    fams["boots"] = {"model": sorted(boots_c), "preview": previews("boots", "boots_old"), "model_ts_only": sorted(boots_ts - boots_c)}
    fams["gkglove"] = {"model": model("gkglove"), "preview": previews("gkglove")}
    fams["tattoo"] = {"model": model("tattoo"), "preview": previews("tattoo")}
    acc = census.get("accessory", {})
    fams["accessory"] = {"model": sorted(acc), "preview": previews("accessory"),
                         "desc": {str(i): acc[i] for i in sorted(acc)}}
    fams["eyebrow"] = {"ids": model("eyebrow")}
    fams["skinsurface"] = {"ids": model("skinsurface")}
    # create-a-club crest templates (crest/custom/l<id>.dds only: 99010101..99171212, no kit, no name) are not club crests
    crest = {k: v for k, v in lf.get("crest", {}).items() if "/custom/" not in v.lower()}
    fams["crest"] = {"ids": ids_of(crest), "paths": {k: crest[k] for k in sorted(crest, key=int)}}
    kits = census.get("kit", {})
    fams["kit"] = {"teams": {str(i): {"slug": k["slug"], "kits": sorted(k["kits"])} for i, k in sorted(kits.items())}}
    fams["ball"] = {"model": model("ball"), "preview": previews("ball")}
    cn = {}
    for lang, teams in sorted(banks.items()):
        cn[lang] = {"teams": {t: {k: sorted(v) for k, v in sorted(kinds.items())} for t, kinds in sorted(teams.items())}}
    fams["callnames"] = cn
    return {"schema": SCHEMA, "build_key": game_key, "game_version": game_version, "built_at": built_at,
            "builder": BUILDER, "sources": {k: sources[k] for k in sorted(sources)}, "families": fams}


def catalog_ids(cat):
    F = cat["families"]
    player = set(F["head3d"]["ids"]) | set(F["miniface"]["ids"]) | set(F["staffface"]["ids"]) | set(F["legendface"]["ids"])
    commentary = set()
    for l in F["callnames"].values():
        for kinds in l["teams"].values():
            for k, ids in kinds.items():
                (commentary if k == "generic" else player).update(ids)
    team = set(F["crest"]["ids"]) | {int(k) for k in F["kit"]["teams"]}
    return player, team, commentary


def summary_text(cat, timings, stats, leg, extra):
    F = cat["families"]
    L = ["Turbo game asset catalog %s (game %s), built %s by %s" % (cat["build_key"], cat["game_version"], cat["built_at"], cat["builder"]), ""]
    L.append("Family        ids / models   previews   notes")
    for k in FAMILY_KEYS:
        v = F[k]
        if k == "head3d":
            L.append("%-13s %8d   %8s   generic %d, unproven (not writable) %d, launch-only %d, contentsb-only %d, both %d" % (
                k, len(v["ids"]), "", len(v["generic"]), len(v["unproven"]),
                sum(1 for t in v["toc"].values() if t == "L"), sum(1 for t in v["toc"].values() if t == "S"),
                sum(1 for t in v["toc"].values() if t == "LS")))
        elif "model" in v:
            note = ""
            if k == "boots":
                note = "content/ models %d (gate); content_ts-only %d listed, not writable; all shoe EBX ids %d" % (
                    len(v["model"]), len(v["model_ts_only"]), len(v["model"]) + len(v["model_ts_only"]))
            pv = set(int(i) for i in v["preview"])
            md = set(v["model"])
            note += "%s model-only %d, preview-only %d" % ("," if note else "", len(md - pv), len(pv - md))
            L.append("%-13s %8d   %8d   %s" % (k, len(v["model"]), len(v["preview"]), note))
        elif k == "crest":
            templates = sum(1 for v in leg["families"].get("crest", {}).values() if "/custom/" in v.lower())
            L.append("%-13s %8d   %8s   club crests; create-a-club templates (crest/custom only) left out: %d" % (
                k, len(v["ids"]), "", templates))
        elif k == "kit":
            teams = [t for t in v["teams"].values() if not families.NON_TEAM_SLUG.match(t["slug"])]
            L.append("%-13s %8d   %8s   club kits %d, other kit ids (referee, goalkeeper, generic) %d" % (
                k, len(v["teams"]), "", len(teams), len(v["teams"]) - len(teams)))
        elif k == "callnames":
            for lang, l in v.items():
                for t, kinds in l["teams"].items():
                    L.append("%-13s %s team %-3s %s" % (k, lang, t or "-", ", ".join("%s %d" % (kk, len(ids)) for kk, ids in kinds.items())))
            if not v:
                L.append("%-13s none (no bank file found)" % k)
        else:
            L.append("%-13s %8d" % (k, len(v["ids"])))
    L.append("")
    N = cat["names"]
    player, team, commentary = catalog_ids(cat)
    L.append("Names: player %d of %d ids, team %d of %d, commentary %d of %d; by source %s" % (
        len(N["player"]), len(player), len(N["team"]), len(team), len(N["commentary"]), len(commentary), extra.get("name_sources")))
    L.append("Legacy manifests: %d hashes %s; csv paths %d (in the game %d); brute-force-only files %d; game legacy files still unnamed %d" % (
        leg["manifest_hashes"], leg["per_collector"], leg["csv_paths"], leg["csv_in_game"], leg["brute_new"], leg["unnamed"]))
    L.append("EBX census: %d tocs scanned, %d from cache, %d unreadable bundles" % (stats["scanned"], stats["cached"], stats["errors"]))
    if extra.get("glove"):
        L.append("Not in schema v1 (counted only): outfield glove models %d" % extra["glove"])
    for k, v in extra.get("checks", {}).items():
        L.append("Check %s: %s" % (k, v))
    for lang, note in sorted((extra.get("banks") or {}).items()):
        L.append("Bank %s: %s" % (lang, note))
    L.append("Timings: " + ", ".join("%s %.1fs" % kv for kv in timings.items()))
    return "\n".join(L) + "\n"


def export_checks(le, hashes):
    """Turbo's exported legacy files (turbo_output\\cache\\legacy\\data) must all be in the manifests; missing.txt none."""
    out = {}
    root = os.path.join(le, "turbo_output", "cache", "legacy")
    paths = []
    for dp, _dn, fns in os.walk(os.path.join(root, "data")):
        for fn in fns:
            paths.append(os.path.relpath(os.path.join(dp, fn), root).replace("\\", "/"))
    if paths:
        hs = legacy.hash_many(paths)
        miss = [p for p, h in zip(paths, hs) if h not in hashes]
        out["exports_in_manifests"] = "%d of %d (not: %s)" % (len(paths) - len(miss), len(paths), ", ".join(miss[:6]))
    mp = os.path.join(root, "missing.txt")
    if os.path.isfile(mp):
        with open(mp, encoding="utf-8", errors="replace") as f:
            m = sorted({l.strip() for l in f if l.strip().lower().startswith("data/")})
        hs = legacy.hash_many(m)
        out["missing_txt_in_manifests"] = "%d of %d unique lines" % (sum(1 for h in hs if h in hashes), len(m))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--le", default=DEFAULT_LE)
    ap.add_argument("--out", default=None, help="output folder (default <LE>\\turbo_output\\asset_catalog)")
    ap.add_argument("--bank", action="append", default=None, help="commentary bank json (repeatable; default: discovered)")
    ap.add_argument("--no-bank-build", action="store_true",
                    help="do not build missing or outdated banks (default: built with fc27_commentary)")
    ap.add_argument("--team-names-loc", action="append", default=None,
                    help="dump of the game's language strings with TeamName_<id> rows (repeatable; default: "
                         "<out>\\names\\LanguageStrings*.txt and C:\\FC_Tools\\My Mods\\loc\\LanguageStrings*.txt)")
    ap.add_argument("--workbook-names", default=None, help="JSON dump of the FC 26 workbooks' names sheets")
    ap.add_argument("--extra-names", default=None)
    ap.add_argument("--managers", default=None)
    ap.add_argument("--masters", default=None, help="callname masters folder (default <LE>\\turbo\\callnames\\masters)")
    a = ap.parse_args(argv)
    t0 = time.time()
    timings = {}
    out = a.out or os.path.join(a.le, "turbo_output", "asset_catalog")
    cache_dir = os.path.join(out, "cache")
    os.makedirs(cache_dir, exist_ok=True)
    log = lambda s: print(s, flush=True)  # noqa: E731
    key = fb.exe_build_key(os.path.join(a.game, "FC27.exe"))
    game_version = "unknown"
    try:
        import re
        with open(os.path.join(a.game, "__Installer", "installerdata.xml"), encoding="utf-8", errors="replace") as f:
            m = re.search(r'<gameVersion\s+version="([0-9.]+)"', f.read())
        if m:
            game_version = m.group(1)
    except OSError:
        pass
    log("build key %s, game %s" % (key, game_version))
    gio = fb.Game(a.game)
    census, stats, sources = ebx_census(a.game, gio, cache_dir, log)
    timings["ebx"] = time.time() - t0
    t = time.time()
    leg, cached = legacy_census(a.game, a.le, gio, cache_dir, log)
    timings["legacy" + (" (cache)" if cached else "")] = time.time() - t
    for k, v in leg["key"]["inputs"].items():
        sources.setdefault(k, v)
    for k, v in leg["key"]["csv"].items():
        sources["LE/" + k] = v
    t = time.time()
    bank_notes = {}
    if a.bank is None and not a.no_bank_build:
        bank_notes = ensure_banks(a.game, out, game_version, log)
    banks, bank_files = {}, a.bank if a.bank is not None else default_banks(a.le, out)
    for p in bank_files:
        try:
            lang, teams, src = read_bank(p, a.game)
        except (OSError, ValueError) as ex:
            log("  bank %s skipped: %s" % (p, ex))
            continue
        L = banks.setdefault(lang, {})
        for team, kinds in teams.items():
            for k, ids in kinds.items():
                L.setdefault(team, {}).setdefault(k, set()).update(ids)
        sources.update(src)
        log("  bank %s: %s" % (os.path.basename(p), lang))
    timings["banks"] = time.time() - t
    built_at = datetime.datetime.now().astimezone().replace(microsecond=0).isoformat()
    cat = assemble(key, game_version, built_at, sources, census, leg, banks)
    t = time.time()
    player, team, commentary = catalog_ids(cat)
    masters = namesmod.load_masters(a.masters or os.path.join(a.le, "turbo", "callnames", "masters"))
    wb = a.workbook_names or os.path.join(a.le, "turbo_output", "asset_research", "callnames", "names_sheets.json")
    dev = os.path.join(a.le, "turbo_dev")
    loc_files = a.team_names_loc if a.team_names_loc is not None else (
        sorted(glob.glob(os.path.join(out, "names", "LanguageStrings*.txt"))) +
        sorted(glob.glob(os.path.join(r"C:\FC_Tools\My Mods\loc", "LanguageStrings*.txt"))))
    team_sources = [("loc_strings", namesmod.load_loc_team_names(loc_files)),
                    ("le_team_names", namesmod.load_le_team_names(os.path.join(a.le, "extensions", "global", "custom_team_names.csv"))),
                    ("turbo_team_names", namesmod.load_turbo_team_names(os.path.join(a.le, "turbo_output", "team_names.json")))]
    N = namesmod.build_names(player, team, commentary, census.get("head3d", {}), census.get("kit", {}), masters,
                             namesmod.load_workbook_names(wb),
                             namesmod.load_extra_names(a.extra_names or os.path.join(dev, "masters", "raw", "extra_player_names.json")),
                             namesmod.load_managers(a.managers or os.path.join(dev, "managers.csv")),
                             team_sources=team_sources)
    cat["names"] = N.as_json()
    timings["names"] = time.time() - t
    hashes = {int(h, 16) for h in leg["hashes"]}
    extra = {"name_sources": N.counts, "glove": len(census.get("glove", {})), "checks": export_checks(a.le, hashes),
             "banks": bank_notes}
    path = os.path.join(out, "catalog_%s.json" % key)
    write_json(path, cat)
    timings["total"] = time.time() - t0
    txt = summary_text(cat, timings, stats, leg, extra)
    with open(os.path.join(out, "catalog_%s.summary.txt" % key), "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)
    gio.close()
    print(txt)
    print("wrote", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
