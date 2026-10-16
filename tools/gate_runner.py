"""gate_runner.py - one command for a Turbo gameplay-preset gate (attrib EBX presets) on the throwaway career LEICESTER.

  python tools/gate_runner.py --preset <attrib_preset.json> --ev <dated evidence folder> [--tag T] [--vanilla positions.csv] [--seconds 180]
  python tools/gate_runner.py --preset <...> --ev <...> --dry-run        everything except the game (see below)

Evidence folder: <anything>\\YYYY-MM-DD\\<name>, must not exist yet (a backup is never overwritten). Nothing in this tool hard-codes a day: the
old prelaunch / teardown scripts tie their baseline paths to 2026-10-15, so backup, md5 record, restore and teardown are done here.

Steps (the first refusal stops everything, nothing is written or killed before the evidence folder is created):
  0 preflight   refuse: attrib_ebx_off.txt / attrib_preset.json / settings_pending.json / turbo_gui_start.flag in turbo_output,
                game or launcher running, not exactly one Settings* file or its md5 differs from tools\\baseline.json, invalid preset
  1 backup      copy the whole game settings folder (saves included), fcsetup.ini, le_config.json, reapply_edits.json, Turbo.dll, lua assets
                into <ev>\\pre and record every md5 (<ev>\\prelaunch_md5.txt); copies are md5-verified
  2 deploy      preset -> turbo_output\\attrib_preset.json (md5 verified); a stale attrib_ebx_log.txt is archived first
  3 launch      only through launch_fc27.ps1 (the ops tools folder), then the hook-log gate BEFORE any menu input: every patch of the preset
                must be logged as applied (old -> new), nothing decompressed before the table was published
  4 drive       boot -> Manager Career -> offline -> ORIGINAL -> Load Career -> LEICESTER (screenshot-verified before the Enter; TORINO /
                EVERTON are never entered) -> hub read-back of EVERY preset field through the dev service (retried while the service warms
                up, once more at the Saturday hub) -> Advance -> Matchday -> Customise > Game Settings > Game Type Settings > Gameplay Type
                CUSTOM (screenshot; Authenticity Features OFF is NOT machine-read: the screenshot is the proof) -> Back clicks -> Tactical
                View (one Enter) -> positions measured from the intro / scoreboard clock (rg_drive.match_path = the B210 measure code)
  5 teardown    always: kill the game, copy post-run evidence, remove the preset, archive + remove the hook log, delete game-written Settings*
                files (after copying them), restore the Settings / le_config baseline, md5-compare everything against the pre record
  6 RESULT.md   in the evidence folder (+ result.json); one verdict line on stdout
Never sends Escape (rg_drive's key whitelist has none; leaving screens is by clicking the on-screen Back hint), never saves the career.

--dry-run: the same code path with a sandbox (copies of the small real files under <ev>\\dryrun_sandbox) instead of the live install, a
simulated game (test_run_gate.Sim: saved screenshots as screens, virtual time) instead of the launch / keys / screen capture, a synthetic
hook log and a synthetic dev-service dump. Nothing is launched, nothing under the live install is written (the real turbo_output / settings
are only read). It also runs self-checks: each refusal fires, the read-back accepts the L6 evidence dump and rejects wrong values, the md5
compare detects a perturbed file, no key outside the whitelist and every Enter preceded by a screenshot.

Out-of-git dependencies (ops tools folder, default C:\\FC 27 Live Editor\\turbo_dev\\tools, override --tools-dir / GATE_OPS_TOOLS):
rg_drive.py rg_screens.py gk.ps1 launch_fc27.ps1 v_l5.py baseline.json (+ test_run_gate.py for --dry-run); rg_drive points at attrib_dump.py /
match_measure.py / mm_stats.py in turbo_dev\\2.0\\wt-30-release\\tools. Their md5s are written into RESULT.md.
Exit: 0 PASS, 1 FAIL, 2 REFUSED, 4 run PASS but the optional --vanilla effect gate is not PASS.
"""
import argparse, hashlib, importlib.util, json, os, re, shutil, struct, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
LE = os.environ.get("FC27_LE_DIR", r"C:\FC 27 Live Editor")
OPS_TOOLS = os.environ.get("GATE_OPS_TOOLS", os.path.join(LE, "turbo_dev", "tools"))
GAME_PROCS = ("FC27", "LauncherNoAuth")          # image names without .exe
SCHEMA = "turbo-attrib-preset/1"
LIVE = "[live] "
SANDBOX_MAX_BYTES = 2 * 1024 * 1024               # dry run: only small real files are copied into the sandbox
REQUIRED_OPS = ("gk.ps1", "launch_fc27.ps1", "rg_drive.py", "rg_screens.py", "v_l5.py")
DUMP_ATTEMPTS_HUB, DUMP_ATTEMPTS_SAT, DUMP_GAP_S = 6, 4, 25.0


class Refused(Exception):
    pass


# ------------------------------------------------------------------------------------------------------------------ paths / env
class Env:
    """every live location the gate touches; the dry run builds the same object over a sandbox"""

    def __init__(self, le=LE, game_data=None):
        self.le = le
        self.game_data = game_data or os.path.join(os.environ.get("LOCALAPPDATA", ""), "EA SPORTS FC 27")
        self.set_dir = os.path.join(self.game_data, "settings")
        self.fcsetup = os.path.join(self.game_data, "fcsetup.ini")
        self.le_cfg = os.path.join(le, "le_config.json")
        self.to = os.path.join(le, "turbo_output")
        self.reapply = os.path.join(self.to, "reapply_edits.json")
        self.dll = os.path.join(le, "turbo", "Turbo.dll")
        self.lua_dir = os.path.join(le, "mods", "legacy", "data", "fifarna", "lua", "assets")
        self.preset = os.path.join(self.to, "attrib_preset.json")
        self.kits = os.path.join(self.to, "kits3d_on.txt")
        self.ebx_off = os.path.join(self.to, "attrib_ebx_off.txt")
        self.ebx_log = os.path.join(self.to, "attrib_ebx_log.txt")
        self.gui_log = os.path.join(self.to, "turbo_gui.log")
        self.flag = os.path.join(self.to, "turbo_gui_start.flag")
        self.pending = os.path.join(self.to, "settings_pending.json")
        self.lock = os.path.join(self.to, "turbo_dev_client.lock")


def md5(path):
    if not os.path.isfile(path):
        return "ABSENT"
    h = hashlib.md5()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest().upper()


def walk_files(root):
    """sorted relative paths (forward slashes) of every file under root"""
    out = []
    for dp, dn, fn in os.walk(root):
        for f in fn:
            out.append(os.path.relpath(os.path.join(dp, f), root).replace("\\", "/"))
    return sorted(out)


def snapshot(env):
    """{key: md5} of everything the gate must leave as it found it"""
    snap = {}
    if os.path.isdir(env.set_dir):
        for rel in walk_files(env.set_dir):
            snap["settings/" + rel] = md5(os.path.join(env.set_dir, rel))
    snap["fcsetup.ini"] = md5(env.fcsetup)
    snap["le_config.json"] = md5(env.le_cfg)
    snap["reapply_edits.json"] = md5(env.reapply)
    snap["Turbo.dll"] = md5(env.dll)
    if os.path.isdir(env.lua_dir):
        for rel in walk_files(env.lua_dir):
            snap["lua_assets/" + rel] = md5(os.path.join(env.lua_dir, rel))
    else:
        snap["lua_assets_folder"] = "ABSENT"
    return snap


def diff_snapshot(pre, post):
    """-> (changed, missing, new): keys whose md5 differs, keys gone, keys not in the pre record"""
    changed = sorted(k for k in pre if k in post and pre[k] != post[k])
    missing = sorted(k for k in pre if k not in post)
    new = sorted(k for k in post if k not in pre)
    return changed, missing, new


def procs_running():
    out = []
    for n in GAME_PROCS:
        try:
            r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq %s.exe" % n, "/NH"], capture_output=True, text=True, timeout=20)
        except Exception:
            continue
        for ln in (r.stdout or "").splitlines():
            if ln.lower().startswith(n.lower() + ".exe"):
                out.append(ln.split()[0] + " pid " + (ln.split()[1] if len(ln.split()) > 1 else "?"))
    return out


def kill_game():
    for n in GAME_PROCS:
        subprocess.run(["taskkill", "/IM", n + ".exe", "/F"], capture_output=True, text=True, timeout=30)
    t0 = time.time()
    while procs_running() and time.time() - t0 < 25:
        time.sleep(0.5)
    return procs_running()


# ------------------------------------------------------------------------------------------------------------------ preset
def parse_off(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def f32(x):
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def load_preset(path):
    """-> (preset dict or None, [errors])"""
    errs = []
    try:
        p = json.load(open(path, encoding="utf-8"))
    except Exception as e:
        return None, ["preset is not readable JSON: %r" % e]
    if not isinstance(p, dict):
        return None, ["preset JSON is not an object"]
    if p.get("schema") != SCHEMA:
        errs.append("schema is %r, expected %r" % (p.get("schema"), SCHEMA))
    if p.get("mode") != "patch":
        errs.append("mode is %r, expected 'patch'" % p.get("mode"))
    if not p.get("build"):
        errs.append("no build string")
    locs, pats = p.get("locations"), p.get("patches")
    if not isinstance(locs, list) or not locs:
        errs.append("no locations")
        locs = []
    if not isinstance(pats, list) or not pats:
        errs.append("no patches")
        pats = []
    la = set()
    for i, l in enumerate(locs):
        for k in ("asset", "toc", "cas", "off", "size"):
            if k not in l:
                errs.append("locations[%d] lacks %s" % (i, k))
        la.add(str(l.get("asset", "")).lower())
    for i, q in enumerate(pats):
        for k in ("knob", "asset", "path", "offset", "kind", "expect", "value"):
            if k not in q:
                errs.append("patches[%d] lacks %s" % (i, k))
        try:
            parse_off(q.get("offset"))
            float(q.get("value"))
            float(q.get("expect"))
        except Exception:
            errs.append("patches[%d] offset / value / expect is not a number" % i)
        if str(q.get("asset", "")).lower() not in la:
            errs.append("patches[%d] asset has no entry in locations" % i)
    return (p if not errs else None), errs


# ------------------------------------------------------------------------------------------------------------------ read-back
def check_readback(preset, dump):
    """compare EVERY preset patch with the dump (attrib_dump.py JSON). -> (status, rows).
    status OK = every field reads the patched value; NOT_READY = a group / field missing or identity not valid (service / memory not ready:
    retry); MISMATCH = the memory was read and holds another value (definitive)."""
    rows = []
    by_asset = {}
    for name, g in (dump.get("groups") or {}).items():
        by_asset.setdefault((g.get("asset") or "").lower(), (name, g))
    for p in preset["patches"]:
        want, exp = float(p["value"]), float(p["expect"])
        row = {"knob": p["knob"], "asset": p["asset"], "path": p["path"], "kind": p["kind"], "want": want, "expect": exp, "ok": False, "ready": False, "got": None, "note": ""}
        rows.append(row)
        hit = by_asset.get(p["asset"].lower())
        if not hit:
            row["note"] = "group of this asset is not in the dump"
            continue
        name, g = hit
        row["group"], row["instance"] = name, g.get("instance")
        if not g.get("name_ok"):
            row["note"] = "group identity check failed (instance name != asset)"
            continue
        fld = (g.get("fields") or {}).get(p["path"])
        if not fld:
            row["note"] = "field is not in the dump"
            continue
        row["ready"] = True
        row["off"] = fld.get("off")
        got = fld.get("value", fld.get("raw"))
        row["got"] = got
        if fld.get("off") != parse_off(p["offset"]):
            row["note"] = "offset in the dump %r != preset %s" % (fld.get("off"), p["offset"])
            continue
        if fld.get("kind") != p["kind"]:
            row["note"] = "kind in the dump %r != preset %r" % (fld.get("kind"), p["kind"])
            continue
        if got is None:
            row["note"] = "no value in the dump (pointer kind)"
            continue
        if p["kind"] == "f32":
            row["ok"] = abs(float(got) - f32(want)) <= 1e-6 * max(1.0, abs(want))
        else:
            row["ok"] = float(got) == want
        if not row["ok"]:
            row["note"] = "reads the vanilla value (patch NOT live)" if abs(float(got) - exp) <= 1e-6 * max(1.0, abs(exp)) else "reads another value"
        elif abs(f32(want) - f32(exp)) <= 1e-6 * max(1.0, abs(exp)):
            row["note"] = "preset value equals expect: this read-back cannot tell patched from vanilla"
    if rows and all(r["ok"] for r in rows):
        return "OK", rows
    if any(not r["ready"] for r in rows):
        return "NOT_READY", rows
    return "MISMATCH", rows


# ------------------------------------------------------------------------------------------------------------------ hook log gate
NUM = r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?"


def parse_ebx_log(text, preset):
    """attrib_ebx_log.txt (Turbo 2.10 hook log): every preset patch must be logged as applied 'path old -> new' on its asset's patch line, and
    nothing may have been decompressed before the table was published. -> (ok, rows, notes)"""
    notes = []
    lines = text.splitlines()
    pat_lines = {}
    for ln in lines:
        m = re.match(r"\s*patch\s+(\S+)\s+\[.*\(patched:\s*(.*)\)\s*$", ln)
        if m:
            pat_lines[m.group(1).lower()] = m.group(2)
    rows = []
    for p in preset["patches"]:
        row = {"knob": p["knob"], "path": p["path"], "want": float(p["value"]), "expect": float(p["expect"]), "ok": False, "note": ""}
        rows.append(row)
        body = pat_lines.get(p["asset"].lower())
        if body is None:
            row["note"] = "no applied-patch line for the asset yet"
            continue
        m = re.search(r"\b%s\s+(%s)\s*->\s*(%s)" % (re.escape(p["path"]), NUM, NUM), body)
        if not m:
            row["note"] = "path not in the patched list"
            continue
        old, new = float(m.group(1)), float(m.group(2))
        row["old"], row["new"] = old, new
        row["ok"] = abs(new - row["want"]) <= 1e-4 * max(1.0, abs(row["want"])) and abs(old - row["expect"]) <= 1e-4 * max(1.0, abs(row["expect"]))
        if not row["ok"]:
            row["note"] = "logged %s -> %s, preset expects %s -> %s" % (m.group(1), m.group(2), p["expect"], p["value"])
    zero_ok = True
    m1 = re.search(r"\(before the table:\s*(\d+)\)", text)
    m2 = re.search(r"BEFORE the table was published:\s*(\d+)", text)
    if not m1 or not m2:
        zero_ok = False
        notes.append("the 'before the table' counters are not in the log (format changed?)")
    elif int(m1.group(1)) != 0 or int(m2.group(1)) != 0:
        zero_ok = False
        notes.append("decompress calls before the table: %s / expected-asset calls before the table: %s" % (m1.group(1), m2.group(1)))
    return bool(rows) and all(r["ok"] for r in rows) and zero_ok, rows, notes


# ------------------------------------------------------------------------------------------------------------------ preflight
def preflight(env, preset_path, ev, tools_dir, baseline=None, procs=procs_running, need_pil=True):
    """-> list of refusal strings (empty = go). Reads only. Messages about the LIVE install start with LIVE (the dry run, which never touches it, only warns)."""
    r = []
    parent = os.path.basename(os.path.dirname(os.path.abspath(ev)))
    if not re.match(r"^\d{4}-\d{2}-\d{2}$", parent):
        r.append("evidence folder must sit in a dated folder YYYY-MM-DD (parent is %r): %s" % (parent, ev))
    if os.path.exists(ev) and (not os.path.isdir(ev) or os.listdir(ev)):
        r.append("evidence folder exists already and is not empty (a backup is never overwritten): %s" % ev)
    if not os.path.isfile(preset_path):
        r.append("preset missing: %s" % preset_path)
    else:
        _, errs = load_preset(preset_path)
        r.extend("preset invalid: " + e for e in errs)
    running = procs()
    if running:
        r.append(LIVE + "game / launcher is running: " + ", ".join(running))
    # kits3d_on.txt is no refusal since 2.11: the shared Oodle hook runs the 3D kit swap and the attrib patch together (combined test)
    for label, p in (("attrib_ebx_off.txt", env.ebx_off), ("attrib_preset.json", env.preset),
                     ("settings_pending.json", env.pending), ("turbo_gui_start.flag", env.flag)):
        if os.path.exists(p):
            why = {"attrib_ebx_off.txt": "the patch would be off", "attrib_preset.json": "a preset is installed already (another test?)",
                   "settings_pending.json": "a staged settings preset would be applied too", "turbo_gui_start.flag": "a crashed start is recorded"}[label]
            r.append(LIVE + "%s exists in turbo_output (%s); remove it yourself: %s" % (label, why, p))
    if not os.path.isfile(env.dll):
        r.append(LIVE + "installed Turbo.dll not found: " + env.dll)
    if not os.path.isfile(env.le_cfg):
        r.append(LIVE + "le_config.json not found: " + env.le_cfg)
    sets = [f for f in (os.listdir(env.set_dir) if os.path.isdir(env.set_dir) else []) if f.startswith("Settings") and os.path.isfile(os.path.join(env.set_dir, f))]
    if len(sets) != 1:
        r.append(LIVE + "expected exactly one Settings* file in %s, found %d: %s" % (env.set_dir, len(sets), ", ".join(sets)))
    elif baseline and md5(os.path.join(env.set_dir, sets[0])).upper() != str(baseline.get("settings_md5", "")).upper():
        r.append(LIVE + "Settings file %s md5 %s differs from tools\\baseline.json (%s %s): a settings write is installed or the baseline is stale" % (
            sets[0], md5(os.path.join(env.set_dir, sets[0]))[:8], baseline.get("settings_name"), str(baseline.get("settings_md5"))[:8]))
    for f in REQUIRED_OPS:
        if not os.path.isfile(os.path.join(tools_dir, f)):
            r.append("ops tool missing: " + os.path.join(tools_dir, f))
    if need_pil:
        try:
            import PIL  # noqa: F401
        except Exception:
            r.append("python module PIL not importable (screen capture)")
    return r


# ------------------------------------------------------------------------------------------------------------------ backup
def backup(env, ev):
    """copy + md5-verify + record. -> pre record {key: md5}. Raises on any mismatch."""
    pre = os.path.join(ev, "pre")
    os.makedirs(pre, exist_ok=True)
    rec = snapshot(env)
    if os.path.isdir(env.set_dir):
        shutil.copytree(env.set_dir, os.path.join(pre, "settings"))
    for src in (env.fcsetup, env.le_cfg, env.reapply, env.dll):
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(pre, os.path.basename(src)))
    if os.path.isdir(env.lua_dir):
        shutil.copytree(env.lua_dir, os.path.join(pre, "lua_assets"))
    bad = []
    for key, m in rec.items():
        if m == "ABSENT":
            continue
        if key.startswith("settings/"):
            cp = os.path.join(pre, "settings", key[len("settings/"):])
        elif key.startswith("lua_assets/"):
            cp = os.path.join(pre, "lua_assets", key[len("lua_assets/"):])
        else:
            cp = os.path.join(pre, key)
        if md5(cp) != m:
            bad.append(key)
    if bad:
        raise RuntimeError("backup copy md5 differs from the source for: %s" % ", ".join(bad))
    with open(os.path.join(ev, "prelaunch_md5.txt"), "w", encoding="utf-8", newline="\n") as f:
        for k, m in rec.items():
            f.write("%s|%s\n" % (k, m))
    sets = sorted(k[len("settings/"):] for k in rec if k.startswith("settings/") and "/" not in k[len("settings/"):] and k.startswith("settings/Settings"))
    state = {"time": time.strftime("%Y-%m-%dT%H:%M:%S"), "entries": len(rec), "settings_files": sets, "turbo_dll_md5": rec.get("Turbo.dll")}
    json.dump(state, open(os.path.join(ev, "prelaunch_state.json"), "w"), indent=1)
    return rec


# ------------------------------------------------------------------------------------------------------------------ teardown
def teardown(env, ev, pre, kill, log):
    """kill -> copy post evidence -> remove preset / hook log / flag -> Settings baseline restore -> md5 compare. -> dict"""
    res = {"problems": [], "removed": [], "restored": [], "kill": None}
    post = os.path.join(ev, "post")
    os.makedirs(os.path.join(post, "settings"), exist_ok=True)
    try:
        res["kill"] = kill()
    except Exception as e:
        res["problems"].append("kill failed: %r" % e)
    # evidence first (a failed copy stops BEFORE anything is removed or restored)
    try:
        for f in sorted(os.listdir(env.set_dir)) if os.path.isdir(env.set_dir) else []:
            src = os.path.join(env.set_dir, f)
            if f.startswith("Settings") and os.path.isfile(src):
                dst = os.path.join(post, "settings", f)
                shutil.copyfile(src, dst)
                if md5(dst) != md5(src):
                    raise RuntimeError("copy of %s does not match the source" % f)
        for src, name in ((env.fcsetup, "fcsetup.ini"), (env.gui_log, "turbo_gui.log"), (env.ebx_log, "attrib_ebx_log.txt")):
            if os.path.isfile(src):
                shutil.copyfile(src, os.path.join(post, name))
        ld = os.path.join(env.le, "Logs")
        if os.path.isdir(ld):
            os.makedirs(os.path.join(post, "logs"), exist_ok=True)
            today = time.strftime("%Y-%m-%d")
            for f in os.listdir(ld):
                p = os.path.join(ld, f)
                if os.path.isfile(p) and time.strftime("%Y-%m-%d", time.localtime(os.path.getmtime(p))) == today:
                    shutil.copyfile(p, os.path.join(post, "logs", f))
        cd = os.path.join(env.game_data, "CrashDumps")
        with open(os.path.join(post, "crashdumps_listing.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join("%s %12d %s" % (time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(os.path.getmtime(os.path.join(cd, n)))), os.path.getsize(os.path.join(cd, n)), n)
                              for n in sorted(os.listdir(cd))) if os.path.isdir(cd) else "(folder absent) " + cd)
    except Exception as e:
        res["problems"].append("EVIDENCE COPY FAILED, nothing removed or restored: %r" % e)
        res["pass"] = False
        return res
    # remove what the run installed
    for p, label in ((env.preset, "attrib_preset.json"), (env.ebx_log, "attrib_ebx_log.txt"), (env.flag, "turbo_gui_start.flag")):
        if os.path.isfile(p):
            os.remove(p)
            res["removed"].append(label)
    if os.path.isdir(env.lock):
        shutil.rmtree(env.lock, ignore_errors=True)
        res["removed"].append("turbo_dev_client.lock")
    # Settings baseline: game-written Settings* not in the record go (after the copy above); recorded files that changed come back from pre
    pre_settings = {k[len("settings/"):] for k in pre if k.startswith("settings/")}
    if os.path.isdir(env.set_dir):
        for f in sorted(os.listdir(env.set_dir)):
            p = os.path.join(env.set_dir, f)
            if f.startswith("Settings") and os.path.isfile(p) and f not in pre_settings:
                os.remove(p)
                res["removed"].append("settings/" + f)
    changed, missing, new = diff_snapshot(pre, snapshot(env))
    for k in changed + missing:
        if k == "le_config.json" or (k.startswith("settings/") and os.path.basename(k).startswith("Settings") and k.count("/") == 1):
            src = os.path.join(ev, "pre", "settings", k[len("settings/"):]) if k.startswith("settings/") else os.path.join(ev, "pre", k)
            dst = os.path.join(env.set_dir, k[len("settings/"):]) if k.startswith("settings/") else env.le_cfg
            shutil.copyfile(src, dst)
            res["restored"].append(k)
    changed, missing, new = diff_snapshot(pre, snapshot(env))
    res["changed"], res["missing"], res["new"] = changed, missing, new
    for label, lst in (("changed", changed), ("missing", missing), ("new (not deleted: never delete anything but game-written Settings*)", new)):
        if lst:
            res["problems"].append("%s after teardown: %s" % (label, ", ".join(lst)))
    for p, label in ((env.preset, "attrib_preset.json"), (env.ebx_log, "attrib_ebx_log.txt")):
        if os.path.exists(p):
            res["problems"].append("%s still present after teardown" % label)
    res["compared"] = len(pre)
    res["pass"] = not res["problems"]
    log("teardown: %s (%d md5 entries compared, removed %s, restored %s, problems %s)" % (
        "PASS" if res["pass"] else "FAIL", len(pre), res["removed"], res["restored"], res["problems"]))
    return res


# ------------------------------------------------------------------------------------------------------------------ ops tool loading
def import_ops(tools_dir):
    """rg_drive (and through it rg_screens) from the ops tools folder"""
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)
    import rg_drive  # noqa: E402
    return rg_drive


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def tool_md5s(tools_dir, G):
    out = {}
    for label, p in (("rg_drive.py", os.path.join(tools_dir, "rg_drive.py")), ("rg_screens.py", os.path.join(tools_dir, "rg_screens.py")),
                     ("gk.ps1", os.path.join(tools_dir, "gk.ps1")), ("launch_fc27.ps1", os.path.join(tools_dir, "launch_fc27.ps1")),
                     ("v_l5.py", os.path.join(tools_dir, "v_l5.py")), ("baseline.json", os.path.join(tools_dir, "baseline.json")),
                     ("attrib_dump.py", os.path.join(G.WT, "attrib_dump.py")), ("match_measure.py", os.path.join(G.WT, "match_measure.py")),
                     ("mm_stats.py", os.path.join(G.WT, "mm_stats.py")), ("gate_runner.py", os.path.abspath(__file__))):
        out[label] = md5(p)
    return out


def git_head():
    try:
        r = subprocess.run(["git", "-C", HERE, "log", "-1", "--format=%h %s"], capture_output=True, text=True, timeout=20)
        b = subprocess.run(["git", "-C", HERE, "branch", "--show-current"], capture_output=True, text=True, timeout=20)
        return "%s on %s" % ((r.stdout or "").strip(), (b.stdout or "").strip())
    except Exception:
        return "unknown"


# ------------------------------------------------------------------------------------------------------------------ the run
class Ctx:
    pass


def make_driver_class(G):
    class GateDriver(G.Driver):
        """Driver that also keeps a few full-resolution frames once the scoreboard clock is up (a small frame misled the L6 first read)"""
        fullres_left = 6

        def save(self, label, img=None):
            p = super().save(label, img)
            try:
                on_clock = label == "match_clock" or (self.last_state == "match_clock" and (label.startswith("match_") or label == "MATCH_final"))
                if on_clock and self.fullres_left > 0 and self.last_img is not None:
                    self.fullres_left -= 1
                    d = os.path.join(self.ev, "fullres")
                    os.makedirs(d, exist_ok=True)
                    self.last_img.convert("RGB").save(os.path.join(d, "%s.png" % label))
            except Exception as e:
                self.log("fullres save failed: %r" % e)
            return p
    return GateDriver


def stage_readback(d, ctx, stage, attempts):
    """dev-service read-back of every preset field; retried while the service / memory is not ready. -> status"""
    status = "NOT_READY"
    for i in range(1, attempts + 1):
        if not d.io.alive():
            raise ctx.G.Abort("the game exited during the read-back")
        out = os.path.join(ctx.ev, "attrib", "dump_%s_%d.json" % (stage, i))
        info = ctx.dump_fn(out, d)
        dump = None
        if os.path.isfile(out):
            try:
                dump = json.load(open(out, encoding="utf-8"))
            except Exception as e:
                info["note"] = "dump unreadable: %r" % e
        if dump is None:
            status, rows = "NOT_READY", []
        else:
            status, rows = check_readback(ctx.preset, dump)
        att = {"stage": stage, "attempt": i, "status": status, "dump": out if dump else None, "info": info, "rows": rows}
        ctx.readbacks.append(att)
        d.log("READ-BACK %s #%d: %s %s" % (stage, i, status, json.dumps([(r["path"], r.get("got"), r["want"], r["ok"]) for r in rows])))
        if status in ("OK", "MISMATCH"):
            return status
        if i < attempts:
            d.io.sleep(DUMP_GAP_S)
    return status


def drive(d, ctx):
    G, a, res = ctx.G, ctx.args, d.result
    okp = lambda x: x.startswith("preview_") and not x.endswith("none")  # noqa: E731
    d.boot()
    d.to_manager_career()
    d.offline()
    d.to_original()
    d.load_career_card()
    d.load_leicester()
    d.io.sleep(4.0)
    st = stage_readback(d, ctx, "hub", DUMP_ATTEMPTS_HUB)
    if st == "MISMATCH":
        raise G.Abort("hub read-back: the memory holds another value than the preset (patch not live): not starting a match")
    hub_first = d.state()
    if hub_first.startswith("hub_sun"):
        d.hub_to("Sunday", "hub_sun_btn", "hub_sun_nofocus", lambda x: x.startswith("hub_sat") or x.startswith("preview_"), "Advance")
    d.wait_for(lambda x: x.startswith("hub_sat"), 120, tolerate={"hub_sun_btn", "hub_sun_nofocus"})
    d.io.sleep(1.0)
    if st != "OK":
        st = stage_readback(d, ctx, "sat", DUMP_ATTEMPTS_SAT)
        if st != "OK":
            raise G.Abort("read-back %s at the Saturday hub as well: not starting a match" % st)
    d.hub_to("Saturday", "hub_sat_btn", "hub_sat_nofocus", lambda x: x.startswith("preview_"), "Matchday")
    # ---- Gameplay Type CUSTOM (Authenticity Features OFF is read by eye from the screenshot)
    cs = G.open_game_type_custom(d)
    shot = d.save("SETTINGS_CUSTOM_AUTHENTICITY")
    try:
        os.makedirs(os.path.join(ctx.ev, "fullres"), exist_ok=True)
        d.last_img.convert("RGB").save(os.path.join(ctx.ev, "fullres", "settings_custom.png"))
    except Exception as e:
        d.log("settings fullres save failed: %r" % e)
    res["settings"] = {"state": cs, "custom": cs == "gs_type_custom", "shot": shot,
                       "authenticity": "NOT machine-read: look at the screenshot (Authenticity Features row must be OFF)"}
    d.log("SETTINGS: %s" % json.dumps(res["settings"]))
    if cs != "gs_type_custom":
        raise G.Abort("Gameplay Type is not CUSTOM (screen %r): not starting a match" % cs)
    d.back_click(88, 801, lambda x: x.startswith("ha_"), {"gs_type_custom", "gs_type_other", "gs_type_auth", "gs_match"}, "Game Settings -> Customise")
    d.back_click(240, 802, okp, {"ha_kit", "ha_sides", "ha_gs", "ha_ctrl", "ha_lock", "ha_none"}, "Customise -> match preview")
    # ---- Tactical View + positions from the intro / scoreboard clock (the B210 measure code); vanilla verdict is run by this tool, after the kill
    ns = argparse.Namespace(mode="match", tag=ctx.tag, ev=ctx.ev, preset="", settings_write=False, vanilla="", match_button="tactical", no_dump=True,
                            seconds=a.seconds, target_n=20, min_samples=76)
    G.match_path(d, ns, res, ctx.state_file)


def effect_gate(ctx, tools_dir, G, csv_path):
    """optional v_l5 verdict (team length, meaningful for shape presets only), run after the game is dead"""
    if not ctx.args.vanilla:
        return {"gate": "NOT_RUN", "line": "no --vanilla given: the effect is judged from the screenshots / position tables"}
    v = subprocess.run([sys.executable, "-I", os.path.join(tools_dir, "v_l5.py"), "--vanilla", ctx.args.vanilla, "--test", csv_path, "--test-win", ctx.tag],
                       capture_output=True, text=True, timeout=180)
    out = (v.stdout or "") + (v.stderr or "")
    open(os.path.join(ctx.ev, "measure", "v_l5_out.txt"), "w", encoding="utf-8").write(out)
    g = re.search(r"L5 gate: (\w+)", out)
    return {"gate": g.group(1) if g else "ERROR", "line": ((out.strip().splitlines() or [""])[-1])[:300]}


def position_summary(G, tools_dir, csv_path, win):
    """mean team length / width per phase from the kick-off window (v_l5.cells with its default filters); {} when unavailable"""
    try:
        v5 = load_module(os.path.join(tools_dir, "v_l5.py"), "v_l5_gate")
        mm = G.load_mm_stats()
        cells, labels = v5.cells(mm, csv_path, win, argparse.Namespace(stoppage=0.5, max_dt=1.5))
        return {"cells": {"%d|%s|%s" % k: v for k, v in cells.items()}, "labels": dict(labels)}
    except Exception as e:
        return {"error": repr(e)}


def run(args):
    t_start = time.time()
    tools_dir = os.path.abspath(args.tools_dir)
    ev = os.path.abspath(args.ev)
    tag = args.tag or os.path.basename(ev)
    dry = args.dry_run
    ctx = Ctx()
    ctx.args, ctx.ev, ctx.tag, ctx.tools = args, ev, tag, tools_dir
    ctx.readbacks, ctx.steps, ctx.hook, ctx.errors = [], [], None, []
    real = Env()
    baseline = None
    bp = os.path.join(tools_dir, "baseline.json")
    if os.path.isfile(bp):
        try:
            baseline = json.load(open(bp, encoding="utf-8"))
        except Exception:
            baseline = None
    refusals = preflight(real, args.preset, ev, tools_dir, baseline, need_pil=True)
    warnings = []
    if dry:   # the dry run only reads the live install: its state is a warning, not a refusal (the refusals themselves are self-checked on a sandbox)
        warnings = [x for x in refusals if x.startswith(LIVE)]
        refusals = [x for x in refusals if not x.startswith(LIVE)]
    if dry:
        for f in ("test_run_gate.py",):
            if not os.path.isfile(os.path.join(tools_dir, f)):
                refusals.append("--dry-run needs the ops simulation: " + os.path.join(tools_dir, f))
    if refusals:
        print("GATE_RUNNER %s VERDICT=REFUSED | %s | nothing was written, started or killed" % (tag, " | ".join(refusals)))
        return 2
    preset, _ = load_preset(args.preset)
    ctx.preset = preset
    os.makedirs(ev)
    logf = open(os.path.join(ev, "gate_runner.log"), "a", encoding="utf-8")

    def log(msg):
        line = "%s  %s" % (time.strftime("%H:%M:%S"), msg)
        logf.write(line + "\n")
        logf.flush()
        print(line, flush=True)

    ctx.log = log
    G = import_ops(tools_dir)
    ctx.G = G
    ctx.state_file = os.path.join(ev, "GAME_STATE.txt")
    for x in warnings:
        log("DRY RUN WARNING (live install, would REFUSE a real run): " + x[len(LIVE):])
    ctx.warnings = warnings
    log("gate_runner start: tag %s%s preset %s md5 %s ev %s" % (tag, " (DRY RUN)" if dry else "", args.preset, md5(args.preset)[:8], ev))
    shutil.copyfile(args.preset, os.path.join(ev, "attrib_preset.deployed.json"))
    env = real
    sim = None
    if dry:
        env = make_sandbox(real, os.path.join(ev, "dryrun_sandbox"))
        log("DRY RUN: sandbox %s (real files are only read)" % os.path.join(ev, "dryrun_sandbox"))
    verdict = {"run": False, "teardown": False}
    pre = {}
    launched = False
    td = None
    d = None
    res = {}
    try:
        # ---- 1 backup
        pre = backup(env, ev)
        ctx.steps.append(("backup", "OK", "%d md5 entries recorded (prelaunch_md5.txt), copies verified" % len(pre)))
        log("backup OK: %d md5 entries" % len(pre))
        # ---- 2 deploy
        if os.path.isfile(env.ebx_log):
            shutil.move(env.ebx_log, os.path.join(ev, "pre", "stale_attrib_ebx_log.txt"))
            log("archived a stale attrib_ebx_log.txt")
        os.makedirs(env.to, exist_ok=True)
        shutil.copyfile(args.preset, env.preset)
        if md5(env.preset) != md5(args.preset):
            raise RuntimeError("deployed preset md5 differs from the source")
        ctx.steps.append(("deploy", "OK", "preset -> %s (md5 %s)" % (env.preset, md5(env.preset)[:8])))
        log("deployed preset md5 %s" % md5(env.preset)[:8])
        # ---- 3 launch + hook-log gate
        launched = True
        t_launch = time.time()
        if dry:
            launch_sim(env, preset, log)
        else:
            lp = os.path.join(tools_dir, "launch_fc27.ps1")
            r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", lp, "-WaitCloseMin", "1"], capture_output=True, text=True, timeout=900)
            for ln in (r.stdout or "").splitlines():
                log("  launch: " + ln)
            if r.returncode != 0:
                raise RuntimeError("launch_fc27.ps1 exit %d" % r.returncode)
        ctx.steps.append(("launch", "OK", "launch_fc27.ps1" if not dry else "SIMULATED (nothing launched)"))
        ctx.hook = wait_hook_log(env, preset, t_launch, timeout=240, dry=dry, log=log)
        ctx.steps.append(("hook-log gate", "OK" if ctx.hook["ok"] else "FAIL", ctx.hook["summary"]))
        if not ctx.hook["ok"]:
            raise RuntimeError("hook-log gate failed before any menu input: " + ctx.hook["summary"])
        # ---- 4 drive
        if dry:
            T = load_module(os.path.join(tools_dir, "test_run_gate.py"), "test_run_gate_ops")
            sim = T.Sim(tempfile.mkdtemp(prefix="gate_sim_"), "settings")
            io_ = T.FakeIO(sim)
            G.Measure = T.FakeMeasure
            ctx.dump_fn = make_mock_dump_fn(preset, sim_fail_first=1)
            ctx.sim, ctx.T = sim, T
            if not args.vanilla:
                args.vanilla = T.VAN
        else:
            io_ = G.RealIO()
            ctx.dump_fn = make_real_dump_fn(G, ctx)
        Drv = make_driver_class(G)
        d = Drv(io_, ev, tag, log_fn=lambda l: (logf.write(l + "\n"), logf.flush()))
        res = d.result
        res["drive"] = "RUNNING"
        try:
            drive(d, ctx)
            res["drive"] = "OK"
        except G.Abort as e:
            res["drive"] = "ABORT: %s" % e
            log("ABORT: %s" % e)
            try:
                d.save("ABORT_last_screen")
            except Exception:
                pass
        except BaseException as e:  # incl. KeyboardInterrupt: teardown still runs
            res["drive"] = "ERROR: %r" % (e,)
            log("ERROR: %r" % (e,))
        ctx.steps.append(("drive", "OK" if res["drive"] == "OK" else "FAIL", res["drive"]))
        if dry:
            simulate_side_effects(env, preset)
    except BaseException as e:
        ctx.errors.append("%r" % (e,))
        log("ABORT before / outside the drive: %r" % (e,))
    finally:
        if d is not None and getattr(d, "measure", None):
            try:
                d.measure.kill()
            except Exception:
                pass
        if launched or pre:
            if dry:
                kill = lambda: "DRY RUN: would kill %s (running now: %s)" % ("/".join(GAME_PROCS), procs_running() or "none")  # noqa: E731
            else:
                kill = kill_game
            td = teardown(env, ev, pre, kill, log)
            if not dry and td.get("kill"):
                td["problems"].append("still running after the kill: %s" % td["kill"])
                td["pass"] = False
        ctx.steps.append(("kill + teardown", "PASS" if td and td["pass"] else "FAIL", "md5 compare %s" % ("PASS" if td and td["pass"] else "FAIL")))
    # ---- 5 verdicts
    csv_path = (res.get("measure") or {}).get("csv")
    eff = {"gate": "NOT_RUN", "line": "no measurement"}
    ctx.positions = {}
    if csv_path and os.path.isfile(csv_path):
        ctx.positions = position_summary(G, tools_dir, csv_path, tag)
        try:
            eff = effect_gate(ctx, tools_dir, G, csv_path)
        except Exception as e:
            eff = {"gate": "ERROR", "line": repr(e)}
    ctx.effect = eff
    rb_ok = any(a_["status"] == "OK" for a_ in ctx.readbacks)
    mech = (res.get("drive") == "OK" and bool(res.get("mode_ok")) and rb_ok and bool(ctx.hook and ctx.hook["ok"]) and not ctx.errors)
    ctx.selfchecks = dry_self_checks(ctx, env, ev, pre, real, tools_dir) if dry else []
    sc_ok = all(c[0] for c in ctx.selfchecks)
    td_ok = bool(td and td["pass"])
    ctx.res, ctx.td, ctx.mech, ctx.td_ok = res, td, mech, td_ok
    ok = mech and td_ok and (sc_ok if dry else True)
    if ok and args.vanilla and not dry and eff["gate"] != "PASS":
        code = 4
    else:
        code = 0 if ok else 1
    ctx.code = code
    ctx.minutes = (time.time() - t_start) / 60.0
    write_result(ctx, env, G, tools_dir, dry)
    line = "GATE_RUNNER %s%s VERDICT=%s | drive=%s | hook-log=%s | read-back=%s | measure=%s | effect(v_l5)=%s | teardown=%s | %.1f min | %s" % (
        tag, " (DRY RUN)" if dry else "", {0: "PASS", 1: "FAIL", 4: "RUN PASS, EFFECT NOT PASS"}[code], res.get("drive", "n/a"),
        "OK" if ctx.hook and ctx.hook["ok"] else "FAIL/none", "OK" if rb_ok else "FAIL/none", res.get("mode_result", "n/a"), eff["gate"],
        "PASS" if td_ok else "FAIL", ctx.minutes, os.path.join(ev, "RESULT.md"))
    log(line)
    return code


# ------------------------------------------------------------------------------------------------------------------ real dev-service dump
def make_real_dump_fn(G, ctx):
    ad = os.path.join(ctx.ev, "attrib")
    os.makedirs(ad, exist_ok=True)

    def dump_fn(out, d):
        G.write_state(ctx.state_file, "HUB", ctx.tag, "read-only attrib dump (gate_runner)")
        env = dict(os.environ, TURBO_GAME_STATE=ctx.state_file, PYTHONUNBUFFERED="1")
        info = {"rc": None, "last": ""}
        try:
            r = subprocess.run([sys.executable, os.path.join(G.WT, "attrib_dump.py"), "--out", out], env=env, capture_output=True, text=True, timeout=240, cwd=G.WT)
            txt = (r.stdout or "") + (r.stderr or "")
            info["rc"], info["last"] = r.returncode, ((txt.strip().splitlines() or [""])[-1])[:200]
        except subprocess.TimeoutExpired:
            txt = "attrib_dump timed out after 240 s"
            info["last"] = txt
        with open(os.path.join(ad, "attrib_dump.out.txt"), "a", encoding="utf-8") as f:
            f.write("== %s\n%s\n" % (os.path.basename(out), txt))
        return info
    return dump_fn


# ------------------------------------------------------------------------------------------------------------------ hook log gate (poll)
def wait_hook_log(env, preset, t_launch, timeout, dry, log):
    t0 = time.time()
    last = "no attrib_ebx_log.txt yet"
    while True:
        p = env.ebx_log
        if os.path.isfile(p) and os.path.getmtime(p) >= t_launch - 2:
            try:
                txt = open(p, encoding="utf-8", errors="replace").read()
            except OSError:
                txt = ""
            ok, rows, notes = parse_ebx_log(txt, preset)
            last = "; ".join(["%s %s" % (r["path"], "applied %s -> %s" % (r.get("old"), r.get("new")) if r["ok"] else "NOT applied (%s)" % r["note"]) for r in rows] + notes)
            if ok:
                log("hook-log gate OK: " + last)
                return {"ok": True, "summary": last, "rows": rows, "notes": notes}
        if time.time() - t0 > timeout or dry:
            log("hook-log gate FAILED: " + last)
            return {"ok": False, "summary": last, "rows": [], "notes": []}
        time.sleep(3)


# ------------------------------------------------------------------------------------------------------------------ dry-run helpers
def make_sandbox(real, root):
    """an Env over copies of the SMALL real files (never the saves): same layout, so backup / teardown run unchanged"""
    sb = Env(le=os.path.join(root, "le"), game_data=os.path.join(root, "gamedata"))

    def cp(src, dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    if os.path.isdir(real.set_dir):
        for rel in walk_files(real.set_dir):
            s = os.path.join(real.set_dir, rel)
            if os.path.getsize(s) <= SANDBOX_MAX_BYTES:
                cp(s, os.path.join(sb.set_dir, rel))
    os.makedirs(sb.set_dir, exist_ok=True)
    for s, d_ in ((real.fcsetup, sb.fcsetup), (real.le_cfg, sb.le_cfg), (real.reapply, sb.reapply), (real.dll, sb.dll)):
        if os.path.isfile(s):
            cp(s, d_)
    if os.path.isdir(real.lua_dir):
        for rel in walk_files(real.lua_dir):
            cp(os.path.join(real.lua_dir, rel), os.path.join(sb.lua_dir, rel))
    os.makedirs(sb.to, exist_ok=True)
    return sb


def synthetic_ebx_log(preset):
    """the Turbo 2.10 hook log format, built from the preset (dry run only; the file says so)"""
    by = {}
    for p in preset["patches"]:
        by.setdefault(p["asset"], []).append(p)
    out = ["Turbo attrib EBX patch log (SYNTHETIC - gate_runner dry run, no game was started)",
           "hook installed %s, mode patch, assets %d" % (time.strftime("%Y-%m-%d %H:%M:%S.000"), len(by)),
           "table published %s | decompress calls 1 (before the table: 0) | attrib blocks recognised %d | overwritten %d" % (time.strftime("%Y-%m-%d %H:%M:%S.001"), len(by), len(by)),
           "calls with an expected asset's sizes BEFORE the table was published: 0"]
    for asset, ps in by.items():
        out.append("  patch  %s [Patch/Win32/fc/fcgame/fcgame.toc, 1/1 B]  seen 1  first x  last x  (patched: %s)" % (
            asset, ", ".join("%s %g -> %g" % (p["path"], float(p["expect"]), float(p["value"])) for p in ps)))
    return "\n".join(out) + "\n"


def launch_sim(env, preset, log):
    os.makedirs(env.to, exist_ok=True)
    open(env.ebx_log, "w", encoding="utf-8").write(synthetic_ebx_log(preset))
    log("DRY RUN: launch skipped; wrote a synthetic hook log into the sandbox")


def mock_dump(preset, patched=True):
    """a minimal attrib_dump.py-shaped dict holding the preset's fields (patched values, or the vanilla 'expect' values)"""
    groups = {}
    for i, p in enumerate(preset["patches"]):
        name = p["asset"].rsplit("/", 1)[-1]
        g = groups.setdefault(name, {"db": "gameplay", "asset": p["asset"], "instance": "0x%X" % (0x10000 + i), "name": p["asset"], "name_ok": True, "fields": {}})
        g["fields"][p["path"]] = {"off": parse_off(p["offset"]), "kind": p["kind"], "value": f32(p["value"] if patched else p["expect"])}
    return {"tool": "attrib_dump", "mock": True, "groups": groups}


def make_mock_dump_fn(preset, sim_fail_first=1):
    n = {"calls": 0}

    def dump_fn(out, d):
        n["calls"] += 1
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if n["calls"] <= sim_fail_first:
            return {"rc": 3, "last": "ABORT: dev call 1: timeout (SIMULATED service warm-up)"}
        json.dump(mock_dump(preset), open(out, "w"))
        return {"rc": 0, "last": "mock dump (SIMULATED)"}
    return dump_fn


def simulate_side_effects(env, preset):
    """what a game run leaves behind, on the sandbox: a game-written Settings file, a modified original, the gui log"""
    sets = sorted(f for f in os.listdir(env.set_dir) if f.startswith("Settings") and os.path.isfile(os.path.join(env.set_dir, f)))
    if sets:
        orig = os.path.join(env.set_dir, sets[0])
        shutil.copyfile(orig, os.path.join(env.set_dir, "Settings29991231235959999"))
        with open(orig, "ab") as f:
            f.write(b"\x00")
    open(env.gui_log, "w").write("synthetic turbo_gui.log (dry run)\n")
    os.makedirs(env.lock, exist_ok=True)


def dry_self_checks(ctx, env, ev, pre, real, tools_dir):
    """-> [(ok, text)]"""
    out = []

    def chk(c, t):
        out.append((bool(c), t))
    # refusals (planted files in throwaway envs)
    tmp = tempfile.mkdtemp(prefix="gate_pf_")
    try:
        te = make_sandbox(real, os.path.join(tmp, "sb"))
        for extra in sorted(f for f in os.listdir(te.set_dir) if f.startswith("Settings"))[1:]:
            os.remove(os.path.join(te.set_dir, extra))      # a clean live install has exactly one
        dated = os.path.join(tmp, "2026-10-16", "x")
        none = lambda: []  # noqa: E731
        base = preflight(te, ctx.args.preset, dated, tools_dir, None, procs=none, need_pil=False)
        chk(base == [], "preflight on a clean sandbox: no refusal %s" % base)
        for label, path, key in (("attrib_preset.json", te.preset, "attrib_preset.json"),
                                 ("attrib_ebx_off.txt", te.ebx_off, "attrib_ebx_off.txt"), ("settings_pending.json", te.pending, "settings_pending.json")):
            open(path, "w").write("x")
            rr = preflight(te, ctx.args.preset, dated, tools_dir, None, procs=none, need_pil=False)
            chk(any(key in x for x in rr), "refuses when %s exists" % label)
            os.remove(path)
        open(te.kits, "w").write("x")       # 2.11: the 3D kit opt-in file is allowed next to an attrib preset (shared Oodle hook)
        rr = preflight(te, ctx.args.preset, dated, tools_dir, None, procs=none, need_pil=False)
        chk(rr == [], "does NOT refuse when kits3d_on.txt exists (combined kits3d + attrib test) %s" % rr)
        os.remove(te.kits)
        rr = preflight(te, ctx.args.preset, dated, tools_dir, None, procs=lambda: ["game pid 1"], need_pil=False)
        chk(any("running" in x for x in rr), "refuses while the game / launcher runs")
        os.makedirs(dated)
        open(os.path.join(dated, "f"), "w").write("x")
        rr = preflight(te, ctx.args.preset, dated, tools_dir, None, procs=none, need_pil=False)
        chk(any("exists already" in x for x in rr), "refuses an evidence folder that exists and is not empty")
        rr = preflight(te, ctx.args.preset, os.path.join(tmp, "undated", "x"), tools_dir, None, procs=none, need_pil=False)
        chk(any("dated folder" in x for x in rr), "refuses an evidence folder outside a YYYY-MM-DD folder")
        rr = preflight(te, ctx.args.preset, dated + "2", tools_dir, {"settings_md5": "0" * 32, "settings_name": "n"}, procs=none, need_pil=False)
        chk(any("baseline.json" in x for x in rr), "refuses a Settings file that differs from baseline.json")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    # read-back logic
    p = ctx.preset
    chk(check_readback(p, mock_dump(p, True))[0] == "OK", "read-back: patched values -> OK")
    chk(check_readback(p, mock_dump(p, False))[0] == "MISMATCH", "read-back: vanilla values -> MISMATCH")
    chk(check_readback(p, {"groups": {}})[0] == "NOT_READY", "read-back: empty dump -> NOT_READY (retry)")
    l6d = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(ev))), "2026-10-16", "L6", "attrib", "dump_retry.json")
    l6p = os.path.join(tools_dir, "attrib_preset_l6.json")
    l5p = os.path.join(tools_dir, "attrib_preset_l5.json")
    a6 = load_preset(l6p)[0] if os.path.isfile(l6p) else None
    a5 = load_preset(l5p)[0] if os.path.isfile(l5p) else None
    if os.path.isfile(l6d) and a6 and a5:
        dj = json.load(open(l6d, encoding="utf-8"))
        chk(check_readback(a6, dj)[0] == "OK", "read-back: the L6 evidence dump matches attrib_preset_l6.json -> OK")
        chk(check_readback(a5, dj)[0] == "MISMATCH", "read-back: the same dump against attrib_preset_l5.json -> MISMATCH")
    else:
        chk(False, "L6 evidence dump / presets not found (%s, %s): read-back known-answer check not run" % (l6d, l6p))
    l6log = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(ev))), "2026-10-16", "L6", "attrib_ebx_log.txt")
    if os.path.isfile(l6log) and a6 and a5:
        chk(parse_ebx_log(open(l6log, encoding="utf-8").read(), a6)[0], "hook-log parser: the real L6 hook log matches attrib_preset_l6.json")
        chk(not parse_ebx_log(open(l6log, encoding="utf-8").read(), a5)[0], "hook-log parser: the same log does not match attrib_preset_l5.json")
    # md5 compare detects a perturbation
    base = snapshot(env)
    some = [k for k in base if k.startswith("settings/") and base[k] != "ABSENT"]
    chk(diff_snapshot(pre, base) == ([], [], []), "md5 compare: sandbox after the dry-run teardown equals the pre record (%d entries)" % len(pre))
    if some:
        victim = os.path.join(env.set_dir, some[0][len("settings/"):])
        data = open(victim, "rb").read()
        with open(victim, "ab") as f:
            f.write(b"\x01")
        ch, _, _ = diff_snapshot(pre, snapshot(env))
        open(victim, "wb").write(data)
        chk(ch == [some[0]], "md5 compare detects a perturbed file (%s)" % some[0])
        chk(diff_snapshot(pre, snapshot(env)) == ([], [], []), "md5 compare: equal again after reverting the perturbation")
    # teardown really restored the simulated side effects
    chk(not os.path.exists(os.path.join(env.set_dir, "Settings29991231235959999")), "teardown deleted the game-written Settings file (copied to post\\settings first)")
    chk(os.path.isfile(os.path.join(ev, "post", "settings", "Settings29991231235959999")), "the game-written Settings file is in the post evidence")
    # the simulated drive: keys + Enter discipline
    sim, T = getattr(ctx, "sim", None), getattr(ctx, "T", None)
    if sim is not None:
        keys = [k[1] for k in sim.keys]
        chk(all(k in ctx.G.KEYS_OK or str(k).startswith("click") for k in keys), "only whitelisted keys sent in the simulated drive %s" % sorted(set(map(str, keys))))
        n_enter = sum(1 for k in keys if k == "enter")
        shots = [f for f in os.listdir(os.path.join(ev, "drive")) if "ENTER_on_" in f or "before_Enter" in f]
        chk(n_enter >= 1 and len(shots) >= n_enter, "every Enter had a saved screenshot before it (%d Enter, %d ENTER_on_/before_Enter shots)" % (n_enter, len(shots)))
        chk(not sim.bad_enter, "no Enter on a forbidden screen / slot %s" % sim.bad_enter)
        chk(sim.slot_names[sim.slot_i] == "LEICESTER", "the focused career card at the load Enter was LEICESTER")
        chk(sum(1 for k in sim.keys if k[1] == "enter" and k[4] == "prev_tac") == 1, "exactly one Enter on Tactical View")
        chk(sum(1 for k in sim.keys if str(k[1]).startswith("click")) == 2, "left Game Settings with 2 Back clicks")
        chk(sum(1 for a_ in ctx.readbacks if a_["status"] == "NOT_READY") >= 1 and any(a_["status"] == "OK" for a_ in ctx.readbacks), "read-back retried after the simulated service warm-up failure and then passed")
    else:
        chk(False, "the simulation did not run")
    return out


# ------------------------------------------------------------------------------------------------------------------ RESULT.md
def f6(v):
    return "n/a" if v is None else ("%.4f" % v if isinstance(v, float) else str(v))


def write_result(ctx, env, G, tools_dir, dry):
    res, td = ctx.res, ctx.td or {}
    L = []
    w = L.append
    verdict = {0: "PASS", 1: "FAIL", 4: "RUN PASS, EFFECT GATE NOT PASS"}[ctx.code]
    w("# Gate run %s - RESULT (%s, %s)" % (ctx.tag, time.strftime("%Y-%m-%d %H:%M"), "DRY RUN: nothing was launched, the drive ran on a simulation" if dry else "LEICESTER, never saved"))
    w("")
    w("## VERDICT: %s" % verdict)
    w("- Mechanics (hook log + drive + read-back + measurement): %s" % ("PASS" if ctx.mech else "FAIL"))
    w("- Teardown (md5 compare against the pre record): %s" % ("PASS" if ctx.td_ok else "FAIL"))
    if dry:
        w("- Effect: SIMULATED (%s). The simulated 'test' positions are the vanilla file itself, so a v_l5 FAIL is the expected known answer here." % ctx.effect["gate"])
    else:
        w("- Effect: %s. %s" % (ctx.effect["gate"], ctx.effect["line"]))
    if dry:
        w("- Self-checks: %d/%d PASS" % (sum(1 for c in ctx.selfchecks if c[0]), len(ctx.selfchecks)))
        w("- DRY RUN ONLY: the launch, the key presses, the screen capture, the dev service and the position measurement were SIMULATED. The live path is NOT proven by this run.")
    w("- The pass/fail of the preset's EFFECT on the pitch is not decided by this tool unless a --vanilla v_l5 gate is shown above (that gate only measures team length / width); judge the screenshots in drive\\ and fullres\\.")
    w("")
    w("## Run")
    w("- tag %s, evidence %s, %.1f min, git %s" % (ctx.tag, ctx.ev, ctx.minutes, git_head()))
    w("- preset %s (md5 %s), deployed copy attrib_preset.deployed.json" % (ctx.args.preset, md5(ctx.args.preset)[:8]))
    w("")
    w("| step | status | detail |")
    w("|---|---|---|")
    for s, st, dt in ctx.steps:
        w("| %s | %s | %s |" % (s, st, str(dt).replace("|", "/")[:300]))
    w("")
    w("## Preset fields (read-back through the dev service, retried while it warms up)")
    w("| knob | path | vanilla (expect) | want | read | ok | note |")
    w("|---|---|---|---|---|---|---|")
    final = [a_ for a_ in ctx.readbacks if a_["rows"]]
    for r in (final[-1]["rows"] if final else []):
        w("| %s | %s | %s | %s | %s | %s | %s |" % (r["knob"], r["path"], f6(r["expect"]), f6(r["want"]), f6(r["got"]), "YES" if r["ok"] else "NO", r["note"]))
    w("")
    w("Attempts: " + "; ".join("%s #%d %s (%s)" % (a_["stage"], a_["attempt"], a_["status"], (a_["info"] or {}).get("last", "")[:80]) for a_ in ctx.readbacks))
    w("")
    w("## Hook log gate (before any menu input)")
    w("- " + (ctx.hook["summary"] if ctx.hook else "not reached"))
    w("")
    w("## Settings screen")
    st = res.get("settings") or {}
    w("- Gameplay Type CUSTOM: %s (machine-classified screen %r)" % ("YES" if st.get("custom") else "NO / not reached", st.get("state")))
    w("- Authenticity Features OFF: NOT machine-verified. Look at %s and fullres\\settings_custom.png." % (st.get("shot") or "the SETTINGS_CUSTOM_AUTHENTICITY screenshot"))
    w("")
    w("## Positions from kick-off")
    ms = res.get("measure") or {}
    w("- csv %s, route %s, without-ball n %s, filtered samples %s, window rule met: %s, stopped: %s" % (ms.get("csv"), ms.get("route"), ms.get("without_ball_n"), ms.get("filtered_samples"), ms.get("window_rule_met"), ms.get("reason")))
    pc = (ctx.positions or {}).get("cells")
    if pc:
        w("")
        w("| team | phase | metric | mean | n |")
        w("|---|---|---|---|---|")
        for k in sorted(pc):
            t, ph, mt = k.split("|")
            w("| %s | %s | %s | %s | %s |" % (t, ph, mt, f6(pc[k][0]), pc[k][1]))
    elif ctx.positions:
        w("- position summary unavailable: %s" % ctx.positions.get("error"))
    w("")
    w("## Teardown")
    w("- kill: %s" % td.get("kill"))
    w("- removed: %s; restored: %s" % (td.get("removed"), td.get("restored")))
    w("- md5 entries compared: %s; changed %s, missing %s, new %s" % (td.get("compared"), td.get("changed"), td.get("missing"), td.get("new")))
    w("- problems: %s" % (td.get("problems") or "none"))
    w("- post-run evidence: post\\ (Settings files, fcsetup.ini, turbo_gui.log, attrib_ebx_log.txt, LE logs of today, crash dump listing)")
    if dry:
        w("")
        w("## Dry-run self-checks")
        for ok, t in ctx.selfchecks:
            w("- %s %s" % ("PASS" if ok else "FAIL", t))
    w("")
    w("## Out-of-git tool versions (md5)")
    for k, v in tool_md5s(tools_dir, G).items():
        w("- %s %s" % (k, v[:8]))
    w("")
    w("## Not proven")
    w("- Authenticity OFF is read by eye only. The in-match settings are not re-read (never Escape).")
    w("- One preset, one match, CUSTOM + Authenticity OFF only: another mode may read another attrib copy.")
    if dry:
        w("- This was a dry run: the live launch / drive / dev-service read-back / measurement code path has not been exercised by it.")
    open(os.path.join(ctx.ev, "RESULT.md"), "w", encoding="utf-8", newline="\n").write("\n".join(L) + "\n")
    json.dump({"tag": ctx.tag, "dry_run": dry, "code": ctx.code, "mechanics": ctx.mech, "teardown": ctx.td_ok, "effect": ctx.effect, "hook": ctx.hook,
               "readbacks": ctx.readbacks, "drive": res, "teardown_detail": td, "steps": ctx.steps, "errors": ctx.errors,
               "selfchecks": ctx.selfchecks}, open(os.path.join(ctx.ev, "result.json"), "w"), indent=1, default=str)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", required=True, help="attrib_preset.json to test (schema turbo-attrib-preset/1)")
    ap.add_argument("--ev", required=True, help="evidence folder, must be <...>\\YYYY-MM-DD\\<name> and not exist yet")
    ap.add_argument("--tag", default="", help="window label of the measurement (default: the evidence folder name)")
    ap.add_argument("--vanilla", default="", help="optional vanilla positions.csv for the v_l5 team-length gate")
    ap.add_argument("--seconds", type=int, default=180, help="measurement time box")
    ap.add_argument("--tools-dir", default=OPS_TOOLS, help="ops tools folder (rg_drive.py, gk.ps1, launch_fc27.ps1, ...)")
    ap.add_argument("--dry-run", action="store_true", help="sandbox + simulated game; nothing launched")
    return run(ap.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
