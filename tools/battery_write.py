"""battery_write.py - COORDINATOR-ONLY one-launch check battery for the Turbo 2.5 gameplay-table knobs (tools/). It WRITES knobs through the
dev service (extreme values, always restored) and kills nothing itself: battery_run.sh kills the game on every exit path.

Decides per knob whether it earns a slider (AGENT_RULES_25): (1) ENGINE-READ check - a deterministic observable of the engine's own
consumer (battery_engine.py: width bands, paired length response) - and (2) VISIBLE-EFFECT-AT-EXTREME check - route-C positions of the 22
players in short windows (<= 40 s) with the knob at a far value against baselines measured in the same run (battery_lib.py), with a
pre-registered numeric criterion and the MDE printed. Every window starts with a write BETWEEN two samples of the same process (no second dev2
client: no lock starvation, the pattern of x1_write.py schedule); every extreme window is therefore also a mid-match-write test.

  python battery_write.py plan     --knobs FILE [--win 35 --budget 440]          offline: the window schedule and the knob table
  python battery_write.py hub      --knobs FILE [--prewrite knob=value --allow-write]   read-only preflight in the hub (GAME_STATE HUB / MATCH-AB)
  python battery_write.py run      --knobs FILE --allow-write [--prewritten knob=value]  in a running match (GAME_STATE MATCH-AB): waits for the clock
  python battery_write.py analyse  EVIDENCE_DIR                                  offline: re-judge a saved run
  python battery_write.py --self-test                                            offline proof on the mock world (no game)
Common: --catalogue PATH --owned knob=value (a foreign current value that is Turbo's: e.g. the profile's) --state PATH --wlog PATH --off PATH
        --mock [--mock-world SPEC] (offline) --evidence DIR
Owner rule (per knob, as x1_write.py): a write needs the current value to be FC 27's default for that copy, the value this tool wrote last
(TO\\battery_state.json, same game process), a value declared with --owned, or - for shape.width_in only - the value in TOOLS\\ab_width_state.json.
Evidence: turbo_dev\\evidence\\<date>\\battery_<HHMMSS>\\ (plan.json, positions.csv, engine.csv, writes.json, report.txt, result.json, log.txt).
Exit: 0 PASS (run completed, every scheduled knob decided or deferred, restore verified), 1 FAIL (a write / read-back / restore failed or the
identity was lost mid-run), 3 INCONCLUSIVE (refused, no match, nothing judged).
"""
import argparse, contextlib, csv, hashlib, io, json, math, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402
import battery_lib as bl  # noqa: E402
import battery_engine as be  # noqa: E402

TOOL = "battery_write"
CAT_DEFAULT = os.path.normpath(os.path.join(HERE, "..", "turbo", "package", "turbo", "tuning", "knobs_6AC07E31.json"))
FMT = {"f32": "<f", "i32": "<i", "bool": "<B"}
FROZEN_WAIT_S = 8.0
KO_END_S = 440.0     # seconds after the clock starts at which the run must be over (35 s before half-time of 8-minute halves)


class Refused(Exception):
    pass


class Failed(Exception):
    pass


class Abort(Exception):
    pass


# ------------------------------------------------------------------------------------------------------------- catalogue / knobs
def load_cat(path):
    if not os.path.exists(path):
        raise Refused("knob catalogue %s not found (--catalogue)" % path)
    with open(path, encoding="utf-8") as f:
        cat = json.load(f)
    kn = {k["id"]: k for k in cat["knobs"]}
    loc = {l["name"]: l for l in cat["locators"]}
    return cat, kn, loc


def default_for(knob, variant):
    cd = knob.get("copy_defaults") or {}
    return cd.get(variant, knob["default"])


def locate_knob(dev, mgr, cat, loc):
    """walk the engine's own chain for one locator -> dict(inst, db, grp, h, name, variant, ti, chain) ; raises Refused on any mismatch"""
    prem = "prematch" in loc.get("db_type", "")
    slot = int(cat["attrib_manager"]["prematch_slot"], 16) if prem else int(cat["attrib_manager"]["current"][0], 16)
    fam, gs = int(loc["family_slot"], 16), int(loc["group_slot"], 16)
    h = be.q(dev.multi([(mgr + slot, 8)])[0])
    if not be.ptr_ok(h):
        raise Refused("manager slot +0x%X = %s" % (slot, h))
    db = be.q(dev.multi([(h + 8, 8)])[0])
    if not be.ptr_ok(db):
        raise Refused("[handle+8] (database) = %s" % db)
    grp_raw = be.q(dev.multi([(db + fam, 8)])[0])
    grp = be.untag(grp_raw)
    if not be.ptr_ok(grp):
        raise Refused("[db+0x%X] (group) = %s" % (fam, grp_raw))
    inst_raw = be.q(dev.multi([(grp + gs, 8)])[0])
    inst = be.untag(inst_raw)
    if not be.ptr_ok(inst):
        raise Refused("[group+0x%X] (instance) = %s" % (gs, inst_raw))
    blk = dev.multi([(inst, 0x20)])[0]
    namep = be.q(blk, 0x18)
    names = [v["instance"] for v in loc["variants"]]
    want = max(len(n) for n in names) + 1
    nb = dev.multi([(namep, want)])[0] if be.ptr_ok(namep) else None
    got = (nb or b"").split(b"\0")[0].decode("latin-1", "replace")
    if got not in names:
        raise Refused("instance Name %r is none of %s" % (got[:80], [n[-48:] for n in names]))
    vt, ti = be.q(blk, 0), be.q(blk, 8)
    if not (be.BASE <= (vt or 0) < be.BASE + be.IMAGE_SIZE):
        raise Refused("instance vtable 0x%X is not an FC27.exe pointer" % (vt or 0))
    if loc["type"].endswith("linelengthwidth") and ti != be.C["TI_LLW"]:
        raise Refused("instance TypeInfo 0x%X != 0x%X" % (ti or 0, be.C["TI_LLW"]))
    return {"inst": inst, "db": db, "grp": grp, "h": h, "name": got, "variant": "simulation" if "/simulation/" in got else "base",
            "ti": ti, "slot": slot, "fam": fam, "gs": gs, "chain": [h, db, grp_raw, inst_raw]}


class KnobIO:
    """one knob: located, identity-checked, guarded writes with read-back and rollback"""

    def __init__(self, run, kid):
        self.run, self.kid = run, kid
        self.k = run.kn[kid]
        self.loc = run.locs[self.k["locator"]]
        self.off = int(self.k["offset"], 16)
        self.fmt = FMT[self.k["type"]]
        self.probe = bl.probe_of(kid)
        self.where = None
        if self.off + struct.calcsize(self.fmt) > int(self.loc["type_size"], 16):
            raise Refused("%s: offset 0x%X + %d is outside the %s byte instance" % (kid, self.off, struct.calcsize(self.fmt), self.loc["type_size"]))

    def locate(self):
        self.where = locate_knob(self.run.dev, self.run.mgr, self.run.cat, self.loc)
        self.default = default_for(self.k, self.where["variant"])
        return self.where

    def verify(self):
        """one multi: every hop of the stored chain still points where it did and the Name is intact -> same instance"""
        w, dev = self.where, self.run.dev
        r = dev.multi([(self.run.mgr + w["slot"], 8), (w["h"] + 8, 8), (w["db"] + w["fam"], 8), (w["grp"] + w["gs"], 8), (w["inst"], 0x20)])
        now = [be.q(r[0]), be.q(r[1]), be.q(r[2]), be.q(r[3])]
        want = [w["h"], w["db"], w["chain"][2], w["chain"][3]]
        if now != want:
            return False, "the chain moved (the current database changed?): %s != %s" % ([hex(x or 0) for x in now], [hex(x) for x in want])
        if be.q(r[4], 8) != w["ti"]:
            return False, "TypeInfo changed"
        return True, "ok"

    def read(self):
        b = self.run.dev.multi([(self.where["inst"] + self.off, struct.calcsize(self.fmt))])[0]
        if b is None:
            raise Failed("%s: value unreadable" % self.kid)
        return struct.unpack(self.fmt, b)[0]

    def owned(self):
        st = self.run.load_state()
        vals = []
        e = st.get("knobs", {}).get(self.kid)
        if e and e.get("inst") == "0x%X" % self.where["inst"] and st.get("pid") == self.run.pid:
            vals += [v for v in (e.get("value"), e.get("prev")) if v is not None]
        vals += self.run.owned.get(self.kid, [])
        if self.kid == "shape.width_in":
            try:
                with open(self.run.ab_state, encoding="utf-8") as f:
                    vals.append(float(json.load(f)["value"]))
            except (OSError, ValueError, KeyError, TypeError):
                pass
        return vals

    def same(self, a, b):
        return abs(a - b) <= (1e-6 if self.k["type"] == "f32" else 0.5)

    def write(self, value, restore=False):
        run = self.run
        ok, why = self.verify()
        if not ok:
            raise Failed("%s: identity: %s" % (self.kid, why))
        if not restore and not (self.probe["tmin"] <= value <= self.probe["tmax"]):
            raise Refused("%s = %g is outside the engine-safe test range %g .. %g" % (self.kid, value, self.probe["tmin"], self.probe["tmax"]))
        cur = self.read()
        if not self.same(cur, self.default) and not any(self.same(cur, v) for v in self.owned()):
            raise Refused("%s: value %g is neither FC 27's %g nor this tool's last write / a declared owned value %s: someone else owns it" % (
                self.kid, cur, self.default, self.owned()))
        if os.path.exists(run.off):
            raise Refused("kill switch %s exists" % run.off)
        self.save_state(value, cur)
        data = struct.pack(self.fmt, value if self.k["type"] == "f32" else int(value))
        werr = None
        try:
            run.dev_write(self.where["inst"] + self.off, data)
        except Failed as e:
            werr = e
        ok2, why2 = self.verify()
        back = self.read() if ok2 else None
        if werr or not ok2 or back is None or not self.same(back, value):
            rb = "identity changed: no rollback"
            if ok2:
                try:
                    run.dev_write(self.where["inst"] + self.off, struct.pack(self.fmt, cur if self.k["type"] == "f32" else int(cur)))
                    rb = "rolled back to %g (read back %s)" % (cur, self.read())
                except Failed as e2:
                    rb = "ROLLBACK FAILED: %s" % e2
            line = "battery_write: %s 0x%X %g -> %g FAILED (%s); %s" % (self.kid, self.where["inst"], cur, value, werr or why2 or "read %s" % back, rb)
            run.log(line)
            harness.append_log(run.wlog, line)
            raise Failed(line)
        line = "battery_write: %s 0x%X %g -> %g (read back ok)" % (self.kid, self.where["inst"], cur, value)
        run.log(line)
        harness.append_log(run.wlog, line)
        self.save_state(value, cur)
        return cur

    def save_state(self, value, prev):
        st = self.run.load_state()
        if st.get("pid") != self.run.pid:
            st = {"pid": self.run.pid, "knobs": {}}
        st["knobs"][self.kid] = {"inst": "0x%X" % self.where["inst"], "value": value, "prev": prev, "t": time.strftime("%H:%M:%S")}
        tmp = self.run.state + ".tmp%d" % os.getpid()
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(st, f)
        os.replace(tmp, self.run.state)


# ------------------------------------------------------------------------------------------------------------- the run object
class Run:
    def __init__(self, a, world=None):
        self.a, self.world = a, world
        self.cat, self.kn, self.locs = load_cat(a.catalogue)
        self.mock = bool(a.mock)
        self.state, self.wlog, self.off, self.ab_state = a.state, a.wlog, a.off, a.ab_state
        for pth in (self.state, self.wlog):
            os.makedirs(os.path.dirname(os.path.abspath(pth)), exist_ok=True)
        self.owned = {}
        for it in a.owned or []:
            kid, _, v = it.partition("=")
            self.owned.setdefault(kid, []).append(float(v))
        self.lines = []
        if self.mock:
            self.call = world.mem.call
            self.pid = harness.MOCK_PID
        else:
            import dev2
            self.call = dev2.call
            pids = harness.running_fc27_pids()
            if len(pids) != 1:
                raise Refused("exactly one running FC27.exe is required (found %s)" % (sorted(pids) or "none"))
            self.pid = next(iter(pids))
        self.dev = be.DevMulti(self.call)
        import match_measure as mm
        self.mm = mm
        if self.mock:
            import battery_mock as bmock
            bmock.install_into_match_measure(world, mm)
        self.clock, self.sleep = mm._now, mm._sleep
        self.mgr = None
        self.knobio = {}

    def log(self, s):
        print(s)
        sys.stdout.flush()

    def load_state(self):
        try:
            with open(self.state, encoding="utf-8") as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}

    def dev_write(self, addr, data):
        r = self.call({"op": "write", "addr": "0x%X" % addr, "hex": data.hex(" ")}, timeout=20, allow_write=True)
        if r.get("error") or not r.get("ok"):
            raise Failed("write at 0x%X failed: %s" % (addr, r.get("error") or r))
        return r

    def engine_ctx(self, need_match):
        g1 = None
        try:
            g1 = int(json.load(open(self.mm.CACHE)).get(be.BUILD, {}).get("c_G1_rva", 0)) or None
        except Exception:
            pass
        return be.resolve(self.dev, g1_rva=None if self.mock else g1, need_match=need_match)


def parse_owned_pairs(items):
    out = []
    for it in items or []:
        kid, _, v = it.partition("=")
        out.append((kid, float(v)))
    return out


# ------------------------------------------------------------------------------------------------------------- plan / hub
def make_plan(a, run=None):
    text = open(a.knobs, encoding="utf-8").read()
    cat = None
    if run is not None:
        cat = run.kn
    else:
        _, cat, _ = load_cat(a.catalogue)
    entries, probs = bl.parse_knob_list(text, cat)
    pre = None
    if getattr(a, "prewritten", None):
        kid, _, v = a.prewritten.partition("=")
        pre = (kid, float(v))
    pre = check_pre(pre, entries, probs)
    windows, deferred = bl.plan_schedule(entries, win=a.win, guard=a.guard, base_every=a.base_every, budget=a.budget)
    if pre and windows:
        windows.insert(0, {"name": "P1", "kind": "pre", "knob": pre[0], "value": pre[1], "sign": 0, "dur": a.win, "guard": 0.0})
    return entries, probs, windows, deferred, pre


def check_pre(pre, entries, probs):
    """--prewritten knob=value: only a band-observable knob can separate a set-up latch from a live read, and its extreme must differ from the
    pre-written value (else a latch could not be told from the mid-match write). The entry's extreme is moved to the other band when equal."""
    if not pre:
        return None
    pr = bl.probe_of(pre[0])
    ent = next((e for e in entries if e["id"] == pre[0]), None)
    if pr is None or pr.get("obs") not in ("band_att", "band_def") or ent is None:
        probs.append("--prewritten %s: only a listed knob with a band observable can separate a set-up latch from a live read" % pre[0])
        return None
    if any(abs(v - pre[1]) < 1e-6 for v, _ in ent["ext"]):
        alt = pre[1] * 2.0 if pre[1] < 0.5 else pre[1] / 2.0
        ent["ext"] = [(alt, -1 if alt < 1.0 else 1)]
        probs_note = "NOTE --prewritten %s = %g: the extreme window uses %g instead (it must differ from the pre-written value)" % (pre[0], pre[1], alt)
        print(probs_note)
    return pre


def print_plan(entries, probs, windows, deferred, a):
    print("knob list: %d entries" % len(entries))
    for p in probs:
        print("  PROBLEM %s" % p)
    print("schedule (%.0f s of windows incl. guards; win %.0f s, guard %.0f s, baseline every %d extremes, budget %.0f s):" % (
        bl.schedule_seconds(windows), a.win, a.guard, a.base_every, a.budget))
    t = 0.0
    for w in windows:
        print("  %5.0f-%5.0f s  %-3s %-4s %s" % (t, t + w["guard"] + w["dur"], w["name"], w["kind"],
                                               "" if w["knob"] is None else "%s = %g (expected sign %+d)" % (w["knob"], w["value"], w["sign"])))
        t += w["guard"] + w["dur"]
    for kid, why in deferred:
        print("  deferred / skipped: %s - %s" % (kid, why))
    print("criterion (pre-registered): see battery_lib.py docstring - PASS |shift| >= dmin and z >= %.1f with the expected sign; FAIL no effect "
          "although MDE <= dmin (or a significant wrong sign); else INCONCLUSIVE. MDE = %.1f x SE is printed per extreme." % (bl.Z_PASS, bl.Z_MDE))


def cmd_plan(a):
    entries, probs, windows, deferred, pre = make_plan(a)
    print_plan(entries, probs, windows, deferred, a)
    for e in entries:
        pr = bl.probe_of(e["id"])
        print("  %-36s engine-read: %s" % (e["id"], pr["obs"] or "none - " + pr.get("why_no_obs", "")))
        print("  %-36s consumer: %s" % ("", pr["consumer"]))
    return harness.finish("PASS" if windows and not probs else "INCONCLUSIVE", tool=TOOL)


def db_name(run, db):
    """the Name of the current gameplay database object (Q6: which copy the engine reads in this match), or None"""
    try:
        p = be.q(run.dev.multi([(db + 0x18, 8)])[0])
        if not be.ptr_ok(p):
            return None
        b = run.dev.multi([(p, 96)])[0]
        return (b or b"").split(b"\0")[0].decode("latin-1", "replace") or None
    except (be.DevError, struct.error):
        return None


def tab_instance(run):
    """the instance Turbo's tuning tab logged for the attacking width group in THIS game process (None when there is no usable log line;
    always None in the mock world)"""
    if run.mock:
        return None
    import re
    try:
        pid, start, lines = harness.current_log(os.path.join(harness.TO, "turbo_gui.log"))
    except harness.StaleLog:
        return None
    hit = None
    for line in lines:
        m = re.search(r"locator attrib\.attackinglinelengthwidth found: .* base (0x[0-9A-Fa-f]+)", line)
        if m:
            hit = int(m.group(1), 16)
    return hit


def hub_preflight(run, entries, log):
    """-> (ok, lines). Read-only: build / getter / chain, per knob locate + identity + current value + owner rule."""
    ctx, bad = run.engine_ctx(need_match=False)
    if ctx is None:
        return False, ["refused: " + "; ".join(bad)]
    run.mgr = ctx["mgr"]
    out = ["build %s; G %s -> mgr %s -> DB %s [%s] -> attacking llw %s (lineWidthModifier %.4f) / defensive %s (%.4f); G1 = %s (0 outside a match)" % (
        ctx["build"], be.H(ctx["G"]), be.H(ctx["mgr"]), be.H(ctx["DB"]), db_name(run, ctx["DB"]), be.H(ctx["inst"]), ctx["L0"],
        be.H(ctx["instd"]), ctx["Ld0"], be.H(ctx["M"] or 0))]
    ok = True
    tab = tab_instance(run)
    if tab is not None and tab != ctx["inst"]:
        out.append("  FAIL: the tuning tab's instance %s is NOT the instance the engine resolves %s (a Turbo write would go to a copy the engine "
                   "does not read)" % (be.H(tab), be.H(ctx["inst"])))
        ok = False
    elif tab is not None:
        out.append("  the tuning tab's logged instance %s == the engine-resolved instance" % be.H(tab))
    for e in entries:
        try:
            kio = KnobIO(run, e["id"])
            w = kio.locate()
            cur = kio.read()
            own = kio.same(cur, kio.default) or any(kio.same(cur, v) for v in kio.owned())
            out.append("  %-36s %s 0x%X [%s] value %g (FC 27 %g) %s" % (e["id"], "LOCATED", w["inst"], w["variant"], cur, kio.default,
                                                                      "owner rule ok" if own else "OWNER RULE WOULD REFUSE (declare --owned %s=%g or reset it)" % (e["id"], cur)))
            ok = ok and own
            run.knobio[e["id"]] = kio
        except (Refused, Failed, be.DevError) as ex:
            out.append("  %-36s REFUSED: %s" % (e["id"], ex))
            ok = False
    return ok, out


def cmd_hub(a, world=None):
    harness.require_live(a.mock, tool=TOOL, write=bool(a.prewrite))
    run = Run(a, world)
    entries, probs, windows, deferred, pre = make_plan(a, run)
    print_plan(entries, probs, windows, deferred, a)
    ok, lines = hub_preflight(run, entries, run.log)
    for ln in lines:
        print(ln)
    if probs:
        ok = False
    if ok and a.prewrite:
        if not a.allow_write:
            raise Refused("--prewrite needs --allow-write (coordinator only)")
        for it in a.prewrite:
            kid, _, v = it.partition("=")
            run.knobio.get(kid) or run.knobio.update({kid: KnobIO(run, kid)})
            kio = run.knobio[kid]
            if kio.where is None:
                kio.locate()
            kio.write(float(v))
            print("prewritten %s = %s (read back ok)" % (kid, v))
    print("BATTERY HUB PREFLIGHT: %s" % ("PASS" if ok else "FAIL"))
    return harness.finish("PASS" if ok else "FAIL", tool=TOOL)


# ------------------------------------------------------------------------------------------------------------- sampling run
class Recorder:
    def __init__(self, run, ctx, m, windows, evdir):
        self.run, self.ctx, self.m, self.windows, self.evdir = run, ctx, m, windows, evdir
        self.pairs = be.sample_pairs(ctx)
        self.erecs, self.writes, self.samples = [], [], 0
        self.pos_f = open(os.path.join(evdir, "positions.csv"), "w", newline="")
        self.pos_w = csv.writer(self.pos_f)
        self.pos_w.writerow(run.mm.POS_HEADER)
        self.t0 = run.clock()
        self.ts0 = None
        self.cur = {}
        self.relocated = 0
        self.last, self.prev = {}, {}

    def frozen_now(self, style):
        """True when, for a team with the given build-up style, the last two engine reads have equal declared inputs and equal output"""
        for team, x in self.last.items():
            p = self.prev.get(team)
            if x["style"] == style and p is not None and x["inp"] is not None and p["inp"] == x["inp"] and p["length"] == x["length"]:
                return True
        return False

    def engine_read(self, label):
        run = self.run
        r = run.dev.multi(self.pairs)
        now = run.clock() - self.t0
        chain_ok, tab, recs = be.parse_sample(self.ctx, r, now)
        if not chain_ok:
            run.log("%.1fs attrib chain changed -> re-resolving" % now)
            ctx2, bad = run.engine_ctx(True)
            if ctx2 is None:
                raise Abort("re-resolve failed: " + "; ".join(bad[:3]))
            self.ctx, self.pairs = ctx2, be.sample_pairs(ctx2)
            run.mgr = ctx2["mgr"]
            return self.engine_read(label)
        if any(not x["ident"] for x in recs):
            raise Abort("owner identity lost (match ended?)")
        for x in recs:
            be.derive(x)
            x["win"], x["i"] = label, self.samples
            self.erecs.append(x)
            self.prev[x["team"]], self.last[x["team"]] = self.last.get(x["team"]), x
        return recs

    def pos_sample(self, label):
        run = self.run
        try:
            s = self.m.read_sample()
        except be.DevError:
            raise
        except run.mm.DevError as e:
            run.log("sample failed (%s): locating again" % e)
            key, base, lay = run.mm.load_layout(False)
            self.m = run.mm.locate_any(key, lay, argparse.Namespace(team_pids="", relocate=False), run.log)
            self.relocated += 1
            return None
        t = run.clock()
        if self.ts0 is None:
            self.ts0 = t
        smp = (t, s[1], s[2], s[3])
        run.mm.write_sample_rows(self.pos_w, self.samples, smp, {"win": label, "vel": None, "clock": None}, self.m.players, self.ts0)
        self.pos_f.flush()
        self.samples += 1
        return smp

    def close(self):
        self.pos_f.close()


def desired_state(w):
    return {} if w["kind"] == "base" else {w["knob"]: w["value"]}


def do_run(run, entries, windows, rec, pre, log):
    """the schedule loop; returns (ended_by, knobs still written). Writes happen BETWEEN two samples of this thread. A knob with a paired
    observable is written at the first frozen-input moment (engine reads and positions continue, labelled x) or after PAIRED_WAIT s."""
    a = run.a
    clock, sleep = run.clock, run.sleep
    cur_vals = {}                       # knob id -> value currently written (absent = FC 27's)
    if pre:
        cur_vals[pre[0]] = pre[1]
        kio = run.knobio[pre[0]]
        v = kio.read()
        if not kio.same(v, pre[1]):
            raise Abort("--prewritten %s = %g, but the knob reads %g now (the hub write did not reach the match)" % (pre[0], pre[1], v))
    st = {"idx": -1, "win_end": 0.0, "guard_until": 0.0, "pending": [], "deadline": 0.0}
    t_clock0 = clock()

    def do_write(kid, v, w):
        kio = run.knobio[kid]
        tw = clock() - rec.t0
        prev = kio.write(v, restore=abs(v - default_for(kio.k, kio.where["variant"])) < 1e-9)
        tw_end = clock() - rec.t0
        if abs(v - default_for(kio.k, kio.where["variant"])) < 1e-9:
            cur_vals.pop(kid, None)
        else:
            cur_vals[kid] = v
        rec.writes.append({"t": tw, "t_end": tw_end, "knob": kid, "value": v, "prev": prev, "window": w["name"]})
        if kio.probe.get("obs") == "paired_len":
            rec.engine_read("x")      # the post-write read at once: the inputs have not had time to move
        log("%s -> %s at %.0f s: %s = %g" % (time.strftime("%H:%M:%S"), w["name"], clock() - t_clock0, kid, v))

    def open_window(w):
        st["guard_until"] = clock() + w["guard"]
        st["win_end"] = st["guard_until"] + w["dur"]
        w["t_start"] = clock() - rec.t0
        w["t0"] = 0.0 if w["kind"] == "pre" else st["guard_until"] - rec.t0
        w["t1"] = st["win_end"] - rec.t0

    def service_pending():
        """write the deferred paired-observable changes at the first frozen-input moment (checked right after an engine read, so the
        write follows the last pre-write read within milliseconds) or when PAIRED_WAIT is over"""
        w = windows[st["idx"]]
        todo = []
        for kid, v in st["pending"]:
            style = run.knobio[kid].probe["obs_style"]
            frozen = rec.frozen_now(style)
            if frozen or clock() > st["deadline"]:
                if not frozen:
                    log("  %s: no frozen-input moment within %.0f s, writing anyway (the paired event is marked not frozen)" % (
                        kid, bl.PAIRED_WAIT))
                do_write(kid, v, w)
            else:
                todo.append((kid, v))
        st["pending"] = todo
        if not todo:
            open_window(w)

    ended = "schedule complete"
    stop = a.stop_file
    next_t = clock()
    hz = 1.0
    while True:
        now = clock()
        if stop and os.path.exists(stop):
            ended = "stopped by %s" % stop
            break
        if now - t_clock0 > a.max_run:
            ended = "deadline: %.0f s since the clock started" % a.max_run
            break
        if st["pending"]:
            service_pending()
        elif st["idx"] < 0 or now >= st["win_end"]:
            st["idx"] += 1
            if st["idx"] >= len(windows):
                break
            w = windows[st["idx"]]
            want = desired_state(w)
            changes = [(kid, default_for(run.kn[kid], run.knobio[kid].where["variant"])) for kid in list(cur_vals) if kid not in want]
            changes += [(kid, v) for kid, v in want.items() if cur_vals.get(kid) is None or abs(cur_vals[kid] - v) > 1e-9]
            if w["kind"] == "pre":
                changes = []
            later = []
            for kid, v in changes:
                if run.knobio[kid].probe.get("obs") == "paired_len":
                    later.append((kid, v))
                else:
                    do_write(kid, v, w)
            st["pending"], st["deadline"] = later, clock() + bl.PAIRED_WAIT
            if later:
                st["guard_until"], st["win_end"] = 1e18, 1e18
            else:
                open_window(w)
            next_t = clock()
        label = windows[st["idx"]]["name"] if clock() >= st["guard_until"] else "x"
        smp = rec.pos_sample(label)
        if smp is not None:
            rec.engine_read(label)
            if st["pending"]:
                service_pending()
            if rec.samples % 10 == 0:
                log("%.0fs window %s, %d samples" % (clock() - t_clock0, label, rec.samples))
        next_t += 1.0 / hz
        sleep(next_t - clock())
    return ended, cur_vals


def wait_for_match(run, timeout, log):
    """-> (ctx, m) once the engine chain resolves with a match object AND the 22 players move (the clock runs); None after timeout"""
    t_end = run.clock() + timeout
    last = None
    mm = run.mm
    while run.clock() < t_end:
        try:
            ctx, bad = run.engine_ctx(True)
        except be.DevError as e:
            ctx, bad = None, [str(e)]
        if ctx is not None:
            try:
                key, base, lay = mm.load_layout(False)
                m = mm.locate_any(key, lay, argparse.Namespace(team_pids="", relocate=False), log)
                s1 = m.read_sample()
                run.sleep(1.2)
                s2 = m.read_sample()
                moved = sum(1 for p, q in zip(s1[1], s2[1]) if p and q and math.dist(p, q) > 0.05)
                if moved >= 10:
                    log("the clock runs: %d of %d players moved between two reads; pitch %.1f x %.1f m" % (moved, len(m.players), m.length, m.width))
                    return ctx, m
                bad = ["only %d players moved (clock not running yet)" % moved]
            except (mm.DevError, be.DevError, KeyError, struct.error) as e:
                bad = ["positions: %s" % e]
        if bad and bad != last:
            log("waiting for the match: %s" % "; ".join(bad[:2]))
            last = bad
        run.sleep(2.0)
    return None


# ------------------------------------------------------------------------------------------------------------- analysis
ENG_FIELDS = ["i", "t", "team", "win", "ident", "chain", "flags", "mode", "disp", "poss", "tval", "cap", "width", "length", "back", "kflag", "K",
              "style", "L", "Ld", "W", "len_bal", "len_pos", "inp"]


def save_engine(path, erecs):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=ENG_FIELDS, extrasaction="ignore")
        w.writeheader()
        for x in erecs:
            row = dict(x)
            inp = x.get("inp")
            row["inp"] = hashlib.sha1(b"|".join(inp)).hexdigest()[:16] if inp else ""
            w.writerow(row)


def load_engine(path):
    out = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            x = {}
            for k, v in r.items():
                if k in ("win", "inp"):
                    x[k] = v
                elif v == "" or v == "None":
                    x[k] = None
                else:
                    x[k] = float(v) if k in ("t", "tval", "cap", "width", "length", "K", "L", "Ld", "W", "len_bal", "len_pos") else int(float(v))
            x["inp"] = x["inp"] or None
            out.append(be.derive(x))
    return out


def engine_verdicts(erecs, windows, writes, entries):
    """-> {knob: dict(kind, verdict, why, live, ...)}"""
    res = {}
    base_w = [w for w in windows if w["kind"] == "base"]
    for e in entries:
        pr = bl.probe_of(e["id"])
        kind = pr["obs"]
        if kind is None:
            res[e["id"]] = {"kind": None, "verdict": "NA", "why": pr.get("why_no_obs", ""), "live": None}
            continue
        mine = [w for w in windows if w["knob"] == e["id"] and w["kind"] in ("ext", "pre")]
        if not mine:
            res[e["id"]] = {"kind": kind, "verdict": "NA", "why": "not scheduled", "live": None}
            continue
        if kind in ("band_att", "band_def"):
            by = {}
            for w in mine + base_w:
                by[w["name"]] = [x for x in erecs if x["win"] == w["name"]]
            by["x"] = [x for x in erecs if x["win"] == "x"]
            j = be.judge_band_knob(kind, by, [dict(w, t0=w.get("t0")) for w in base_w + mine],
                                   [x for x in writes if x["knob"] == e["id"]])
            j["kind"] = kind
            res[e["id"]] = j
        else:
            j = be.judge_paired(erecs, [x for x in writes if x["knob"] == e["id"]], pr["obs_style"])
            j["kind"], j["live"] = kind, (True if j["verdict"] == "PASS" else None)
            res[e["id"]] = j
    return res


def analyse_data(positions_csv, erecs, windows, writes, entries, deferred, L, W):
    samples = bl.load_positions_csv(positions_csv)
    recs = bl.sample_records(samples, L, W)
    table = bl.effect_table(recs, [w for w in windows if w["kind"] in ("base", "ext")])
    eff = bl.knob_verdicts(table, entries, deferred)
    eng = engine_verdicts(erecs, windows, writes, entries)
    rows = []
    for e in entries:
        kid = e["id"]
        ev, ef = eng.get(kid, {"verdict": "NA"}), eff.get(kid, {"effect": "SKIP", "why": "not scheduled", "rows": []})
        dec, why = bl.slider_decision(ev["verdict"], ef["effect"])
        live = ev.get("live")
        if live is None:
            live = True if ef["effect"] == "PASS" else None
        rows.append({"knob": kid, "engine": ev["verdict"], "engine_why": ev.get("why", ""), "effect": ef["effect"], "effect_why": ef["why"],
                     "live": live, "decision": dec, "decision_why": why, "latency": ev.get("latency"), "extremes": ef["rows"]})
    return table, rows, eng


def render(table, rows, eng, deferred, windows, meta):
    out = ["", "==== BATTERY RESULT ===="]
    aa = table.get("aa")
    if aa:
        out.append("A/A control (first vs last baseline, worst metric %s): diff %+.2f, se %.2f, z %.1f -> %s" % (
            aa["metric"], aa["diff"], aa["se"], aa["z"], "stable" if bl.aa_ok(aa) else "DRIFTS (all verdicts INCONCLUSIVE)"))
    out.append("pre-registered: PASS |shift| >= dmin and z >= %.1f, expected sign | FAIL no effect with MDE <= dmin, or a significant wrong sign | "
               "MDE = %.1f x SE" % (bl.Z_PASS, bl.Z_MDE))
    out.append("")
    out.append("per extreme window (shift of the metric against the trend-interpolated baseline):")
    for r in table["ext"]:
        out.append("  " + bl.fmt_row(r))
    out.append("")
    out.append("engine-read observables:")
    for kid, e in eng.items():
        out.append("  %-36s %-6s %s%s" % (kid, e["verdict"], e.get("why", ""), "  [latency <= %.2f s]" % e["latency"] if e.get("latency") is not None else ""))
    out.append("")
    out.append("%-36s %-14s %-13s %-11s %s" % ("KNOB", "ENGINE-READ", "EFFECT", "MID-MATCH", "SLIDER"))
    for r in rows:
        mid = {True: "live", False: "set-up only", None: "undecided"}[r["live"]]
        out.append("%-36s %-14s %-13s %-11s %s" % (r["knob"], r["engine"], r["effect"], mid, r["decision"]))
    for r in rows:
        out.append("BATTERY KNOB %s: engine=%s effect=%s mid_match=%s slider=%s (%s)" % (
            r["knob"], r["engine"], r["effect"], {True: "live", False: "setup-only", None: "undecided"}[r["live"]], r["decision"], r["decision_why"]))
    for kid, why in deferred:
        out.append("BATTERY SKIPPED %s: %s" % (kid, why))
    return out


# ------------------------------------------------------------------------------------------------------------- run command
def evidence_dir(a):
    if a.evidence:
        d = a.evidence
    else:
        d = os.path.join(harness.EV, time.strftime("%Y-%m-%d"), "battery_" + time.strftime("%H%M%S"))
    os.makedirs(d, exist_ok=True)
    return d


class Tee:
    def __init__(self, path, orig):
        self.f, self.orig = open(path, "a", encoding="utf-8"), orig

    def write(self, s):
        self.orig.write(s)
        self.f.write(s)

    def flush(self):
        self.orig.flush()
        self.f.flush()


def cmd_run(a, world=None):
    harness.require_live(a.mock, tool=TOOL, write=True)
    if not a.allow_write:
        raise Refused("a write run needs --allow-write (coordinator only)")
    evdir = evidence_dir(a)
    tee = Tee(os.path.join(evdir, "log.txt"), sys.stdout)
    old = sys.stdout
    sys.stdout = tee
    try:
        return _run(a, world, evdir)
    finally:
        sys.stdout = old
        tee.f.close()


def _run(a, world, evdir):
    run = Run(a, world)
    log = run.log
    entries, probs, windows, deferred, pre = make_plan(a, run)
    for p in probs:
        log("PROBLEM %s" % p)
    log("battery evidence: %s" % evdir)
    print_plan(entries, probs, windows, deferred, a)
    if probs and not entries:
        return harness.finish("INCONCLUSIVE", ["no usable knob"], tool=TOOL)
    log("waiting for the match (clock) for up to %.0f s" % a.wait_start)
    t_wait0 = run.clock()
    got = wait_for_match(run, a.wait_start, log)
    if got is None:
        return harness.finish("INCONCLUSIVE", ["no running match with a moving clock within %.0f s" % a.wait_start], tool=TOOL)
    ctx, m = got
    waited = run.clock() - t_wait0
    left = KO_END_S - a.since_ko - waited
    a.budget = min(a.budget, left - 20.0)          # the schedule must end 20 s before KO_END
    if a.max_run is None:
        a.max_run = left
    log("time: %.0f s waited, %.0f s left before the half-time margin; schedule budget %.0f s" % (waited, left, a.budget))
    run.mgr = ctx["mgr"]
    log("engine chain resolved: M %s T %s F %s owners %s; L %.4f Ldef %.4f; current gameplay DB: %s" % (
        be.H(ctx["M"]), be.H(ctx["T"]), be.H(ctx["F"]), [be.H(o) for o in ctx["owner"]], ctx["L0"], ctx["Ld0"], db_name(run, ctx["DB"])))
    log("build-up styles read from the tactic blocks (team index: style): %s" % {r["team"]: r["style"] for r in
        be.parse_sample(ctx, run.dev.multi(be.sample_pairs(ctx)), 0.0)[2]})
    # locate + identity of every knob that has a window; refused knobs are dropped and the schedule is re-planned
    refused = []
    keep = []
    for e in entries:
        try:
            kio = KnobIO(run, e["id"])
            w = kio.locate()
            cur = kio.read()
            if not (kio.same(cur, kio.default) or any(kio.same(cur, v) for v in kio.owned())):
                raise Refused("current value %g is neither FC 27's %g nor owned (%s): declare --owned or reset" % (cur, kio.default, kio.owned()))
            log("  %-36s located 0x%X [%s] value %g (FC 27 %g)" % (e["id"], w["inst"], w["variant"], cur, kio.default))
            run.knobio[e["id"]] = kio
            keep.append(e)
        except (Refused, Failed, be.DevError) as ex:
            log("  %-36s REFUSED: %s" % (e["id"], ex))
            refused.append((e["id"], "refused: %s" % ex))
    if pre and pre[0] not in run.knobio:
        pre = None
    entries = keep
    pre = check_pre(pre, entries, []) if pre else None
    windows, deferred2 = bl.plan_schedule(entries, win=a.win, guard=a.guard, base_every=a.base_every, budget=a.budget)
    if pre and windows:
        windows.insert(0, {"name": "P1", "kind": "pre", "knob": pre[0], "value": pre[1], "sign": 0, "dur": a.win, "guard": 0.0})
    deferred = deferred2 + refused
    if not windows:
        return harness.finish("INCONCLUSIVE", ["nothing to schedule after the identity checks: %s" % deferred], tool=TOOL)
    rec = Recorder(run, ctx, m, windows, evdir)
    ended, rc_fail = "", None
    cur_vals = {}
    try:
        try:
            ended, cur_vals = do_run(run, entries, windows, rec, pre, log)
        except (Failed, Refused) as ex:
            rc_fail = "write failed: %s" % ex
            ended = rc_fail
        except Abort as ex:
            rc_fail = "aborted: %s" % ex
            ended = rc_fail
        except be.DevError as ex:
            rc_fail = "dev service error: %s" % ex
            ended = rc_fail
        except Exception as ex:      # any other bug: still restore, still analyse what was recorded, and fail
            import traceback
            traceback.print_exc()
            rc_fail = "unexpected error: %s: %s" % (type(ex).__name__, ex)
            ended = rc_fail
    finally:
        rec.close()
        # restore EVERY written knob to FC 27's value, whatever happened
        restore_failed = []
        for kid, kio in run.knobio.items():
            try:
                if kio.where is None:
                    continue
                cur = kio.read()
                if not kio.same(cur, kio.default):
                    kio.write(kio.default, restore=True)
                    log("restored %s to %g" % (kid, kio.default))
            except (Failed, Refused, be.DevError) as ex:
                restore_failed.append("%s: %s" % (kid, ex))
                log("RESTORE FAILED %s: %s" % (kid, ex))
    log("run ended: %s" % ended)
    ok_restore = not restore_failed
    # analysis
    save_engine(os.path.join(evdir, "engine.csv"), rec.erecs)
    json.dump({"windows": windows, "writes": rec.writes, "deferred": deferred, "entries": entries, "ended": ended,
               "L": getattr(m, "length", 105.0), "W": getattr(m, "width", 68.0), "relocated": rec.relocated,
               "pre": pre}, open(os.path.join(evdir, "plan.json"), "w"), indent=1, default=str)
    json.dump(rec.writes, open(os.path.join(evdir, "writes.json"), "w"), indent=1)
    if rec.samples < 20:
        return harness.finish("FAIL" if rc_fail else "INCONCLUSIVE", ["only %d samples (%s)" % (rec.samples, ended)], tool=TOOL)
    done_wins = [w for w in windows if "t1" in w]
    table, rows, eng = analyse_data(os.path.join(evdir, "positions.csv"), rec.erecs, done_wins, rec.writes,
                                    [e for e in entries], deferred, getattr(m, "length", 105.0), getattr(m, "width", 68.0))
    lines = render(table, rows, eng, deferred, done_wins, {})
    for ln in lines:
        print(ln)
    with open(os.path.join(evdir, "report.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    json.dump({"rows": rows, "aa": table.get("aa"), "ext": table["ext"], "ended": ended, "restore_failed": restore_failed},
              open(os.path.join(evdir, "result.json"), "w"), indent=1, default=str)
    print("evidence: %s" % evdir)
    if rc_fail or restore_failed:
        return harness.finish("FAIL", [x for x in (rc_fail,) if x] + restore_failed, tool=TOOL)
    decided = [r for r in rows if r["decision"] != "DEFER" or r["effect"] != "INCONCLUSIVE"]
    return harness.finish("PASS" if rows else "INCONCLUSIVE", [], tool=TOOL)


def cmd_analyse(a):
    d = a.evidence_dir
    plan = json.load(open(os.path.join(d, "plan.json")))
    erecs = load_engine(os.path.join(d, "engine.csv"))
    windows = [w for w in plan["windows"] if "t1" in w]
    table, rows, eng = analyse_data(os.path.join(d, "positions.csv"), erecs, windows, plan["writes"], plan["entries"],
                                    [tuple(x) for x in plan["deferred"]], plan["L"], plan["W"])
    for ln in render(table, rows, eng, [tuple(x) for x in plan["deferred"]], windows, {}):
        print(ln)
    return harness.finish("PASS", tool=TOOL)


# ------------------------------------------------------------------------------------------------------------- CLI
def build_parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", choices=["plan", "hub", "run", "analyse"])
    ap.add_argument("evidence_dir", nargs="?", help="analyse: the evidence folder")
    ap.add_argument("--knobs", default=os.path.join(HERE, "battery_knobs.txt"))
    ap.add_argument("--catalogue", default=CAT_DEFAULT)
    ap.add_argument("--win", type=float, default=35.0, help="window length in s (capped at 40)")
    ap.add_argument("--guard", type=float, default=5.0)
    ap.add_argument("--base-every", type=int, default=3)
    ap.add_argument("--budget", type=float, default=420.0, help="seconds the schedule may take (windows + guards)")
    ap.add_argument("--since-ko", type=float, default=15.0, help="seconds since the kick-off when this phase starts (the run ends %.0f s after "
                    "the kick-off: 35 s before half-time of 8-minute halves)" % KO_END_S)
    ap.add_argument("--max-run", type=float, default=None, help="hard end, seconds after the clock started (default: until KO_END)")
    ap.add_argument("--wait-start", type=float, default=150.0, help="how long to wait for a running match")
    ap.add_argument("--owned", action="append", help="knob=value: a current value that is Turbo's (e.g. the tuning profile's)")
    ap.add_argument("--prewrite", action="append", help="hub: knob=value written before kick-off (latch test)")
    ap.add_argument("--prewritten", default=None, help="run: knob=value that the hub wrote (adds the P1 window)")
    ap.add_argument("--state", default=os.path.join(harness.TO, "battery_state.json"))
    ap.add_argument("--wlog", default=os.path.join(harness.TO, "battery_write.log"))
    ap.add_argument("--off", default=os.path.join(harness.TO, "battery_write_off.txt"), help="kill switch file")
    ap.add_argument("--ab-state", default=os.path.join(harness.TOOLS_DIR, "ab_width_state.json"))
    ap.add_argument("--stop-file", default=os.path.join(harness.TO, "battery_stop"))
    ap.add_argument("--evidence", default=None)
    ap.add_argument("--allow-write", action="store_true")
    ap.add_argument("--mock", action="store_true")
    ap.add_argument("--mock-world", default="")
    ap.add_argument("--self-test", action="store_true")
    return ap


def main(argv=None, world=None):
    a = build_parser().parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.cmd:
        build_parser().print_help()
        return 2
    box = harness.timebox((a.max_run or KO_END_S) + a.wait_start + 120 if a.cmd == "run" else 300, TOOL)
    try:
        if a.cmd == "plan":
            return cmd_plan(a)
        if a.cmd == "analyse":
            return cmd_analyse(a)
        if a.mock and world is None:
            import battery_mock as bmock
            world = bmock.build_world(a.mock_world)
        if a.cmd == "hub":
            return cmd_hub(a, world)
        return cmd_run(a, world)
    except Refused as e:
        return harness.finish("INCONCLUSIVE", ["refused: %s" % e], tool=TOOL)
    except Failed as e:
        return harness.finish("FAIL", [str(e)], tool=TOOL)
    except be.DevError as e:
        return harness.finish("INCONCLUSIVE", ["dev service: %s" % e], tool=TOOL)
    finally:
        box.cancel()


# ------------------------------------------------------------------------------------------------------------- self-test
def self_test():
    import shutil, tempfile
    import battery_mock as bmock
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        sys.stdout.flush()
        ok = ok and cond

    with tempfile.TemporaryDirectory() as d:
        knobs = os.path.join(d, "knobs.txt")

        def lst(*lines):
            with open(knobs, "w") as f:
                f.write("\n".join(lines) + "\n")

        def run_mock(world_spec, args, name, tag=None):
            ev = os.path.join(d, name)
            tag = tag or name        # the owner state is per tag: a hub pre-write and the following run share it, as the real default does
            argv = args + ["--mock", "--knobs", knobs, "--evidence", ev, "--state", os.path.join(d, tag + "_st.json"),
                           "--wlog", os.path.join(d, tag + "_w.log"), "--off", os.path.join(d, tag + "_off"),
                           "--ab-state", os.path.join(d, "no_ab.json"), "--stop-file", os.path.join(d, "nostop")]
            world = bmock.build_world(world_spec) if not isinstance(world_spec, bmock.MockWorld) else world_spec
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = main(argv, world)
            return rc, buf.getvalue(), world, ev

        # offline plan
        lst("shape.width_in", "shape.width_out", "shape.width_in_cb", "lineband.deep_max")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["plan", "--knobs", knobs])
        check(rc == 0 and "schedule" in buf.getvalue() and "deferred / skipped: lineband.deep_max" in buf.getvalue()
              and "engine-read: band_att" in buf.getvalue(), "plan: offline schedule, engine-read column, set-up-only knob skipped")
        # hub preflight
        rc, out, w, ev = run_mock("scale=0.02;delay=0", ["hub"], "hub")
        check(rc == 0 and "BATTERY HUB PREFLIGHT: PASS" in out and out.count("LOCATED") == 4, "hub: build / chain / 4 knobs located and identity-checked (exit %d)" % rc)
        w.knob_addr and w.put(w.knob_addr["shape.width_in"], "<f", 1.25)
        rc, out, w2, ev = run_mock(w, ["hub"], "hub2")
        check(rc == 1 and "OWNER RULE WOULD REFUSE" in out, "hub: a foreign current value is reported before the run (exit %d)" % rc)
        rc, out, w2, ev = run_mock(w, ["hub", "--owned", "shape.width_in=1.25"], "hub3")
        check(rc == 0, "hub: --owned declares the profile's / foreign value (exit %d)" % rc)
        # full run, everything live
        lst("shape.width_in", "shape.width_out", "shape.width_in_cb", "shape.length_in", "shape.length_out", "marking.tightness",
            "lineband.deep_max")
        t = time.time()
        rc, out, w, ev = run_mock("scale=0.02;delay=2", ["run", "--allow-write", "--budget", "420"], "live")
        print(out[-2500:])
        res = json.load(open(os.path.join(ev, "result.json")))
        by = {r["knob"]: r for r in res["rows"]}
        check(rc == 0, "run: completed, exit 0 (%.0f s)" % (time.time() - t))
        check(by["shape.width_in"]["engine"] == "PASS" and by["shape.width_in"]["effect"] == "PASS" and by["shape.width_in"]["decision"] == "YES",
              "width_in (live engine, live effect): engine PASS, effect PASS, slider YES")
        check(by["shape.width_out"]["engine"] == "PASS" and by["shape.width_out"]["decision"] in ("YES", "DEFER"),
              "width_out: the without-ball band reads the defensive value (engine %s, effect %s)" % (by["shape.width_out"]["engine"], by["shape.width_out"]["effect"]))
        check(by["shape.width_in_cb"]["engine"] == "NA" and by["shape.width_in_cb"]["effect"] in ("PASS", "INCONCLUSIVE"),
              "width_in_cb: no observable (NA), effect check alone (%s)" % by["shape.width_in_cb"]["effect"])
        check(by["shape.length_out"]["effect"] in ("FAIL", "INCONCLUSIVE") and by["shape.length_out"]["decision"] in ("NO", "DEFER"),
              "length_out (ignored by the mock engine): never a slider (%s / %s)" % (by["shape.length_out"]["effect"], by["shape.length_out"]["decision"]))
        check("lineband.deep_max" in out and "BATTERY SKIPPED lineband.deep_max" in out, "the set-up-only knob is reported as skipped")
        gots = {kid: w.knob_value(kid) for kid in ("shape.width_in", "shape.width_out", "shape.length_in", "marking.tightness")}
        check(all(abs(gots[k] - float(w.cat["knobs"][[x["id"] for x in w.cat["knobs"]].index(k)]["default"])) < 1e-6 for k in gots),
              "every written knob is back at FC 27's value after the run %s" % gots)
        wl = open(os.path.join(d, "live_w.log")).read()
        check(wl.count("(read back ok)") >= 6 and "FAILED" not in wl, "write log: %d guarded writes, all read back ok" % wl.count("(read back ok)"))
        check(os.path.exists(os.path.join(ev, "positions.csv")) and os.path.exists(os.path.join(ev, "engine.csv")), "evidence files written")
        # offline re-analysis gives the same table
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["analyse", ev])
        check(rc == 0 and "shape.width_in" in buf.getvalue() and "BATTERY KNOB shape.width_in" in buf.getvalue(), "analyse: the saved run re-judges offline")
        # engine ignores + positions ignore -> NO
        lst("shape.width_in", "shape.width_out")
        rc, out, w, ev = run_mock("scale=0.02;delay=1;engine=width_in:ignore,width_out:ignore;effects=shape.width_in:ignore:0,shape.width_out:ignore:0",
                                  ["run", "--allow-write"], "ignore")
        res = json.load(open(os.path.join(ev, "result.json")))
        by = {r["knob"]: r for r in res["rows"]}
        check(by["shape.width_in"]["engine"] == "FAIL" and by["shape.width_in"]["decision"] == "NO" and by["shape.width_out"]["decision"] == "NO",
              "engine ignores the table and nothing moves: engine FAIL, slider NO for both (%s / %s)" % (by["shape.width_in"]["engine"], by["shape.width_in"]["decision"]))
        # set-up latch with --prewrite in the hub
        lst("shape.width_in")
        world = bmock.build_world("scale=0.02;delay=1;engine=width_in:latch;effects=shape.width_in:latch:0.8")
        rc, out, _, _ = run_mock(world, ["hub", "--prewrite", "shape.width_in=0.3", "--allow-write"], "latchhub", "latch")
        check(rc == 0 and abs(world.knob_value("shape.width_in") - 0.3) < 1e-6, "hub --prewrite: 0.3 written before kick-off and read back")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--prewritten", "shape.width_in=0.3"], "latch", "latch")
        res = json.load(open(os.path.join(ev, "result.json")))
        r0 = res["rows"][0]
        check(r0["engine"] == "PASS" and r0["live"] is False,
              "latch: the pre-kick-off value is used, mid-match writes are not followed -> engine PASS, mid-match = set-up only (engine %s live %s)" % (r0["engine"], r0["live"]))
        # refusals: foreign value, wrong name
        lst("shape.width_in", "shape.width_out")
        world = bmock.build_world("scale=0.02;delay=1")
        world.put(world.knob_addr["shape.width_out"], "<f", 1.3)
        rc, out, _, ev = run_mock(world, ["run", "--allow-write"], "foreign")
        check("shape.width_out" in out and "REFUSED" in out, "run: a foreign current value refuses that knob, the other runs")
        check(abs(world.knob_value("shape.width_out") - 1.3) < 1e-6, "run: the foreign value was never touched")
        lst("shape.width_in", "marking.tightness")
        world = bmock.build_world("scale=0.02;delay=1")
        inst = world.addr["inst:attrib.markingtightness"]
        namep = world.get(inst + 0x18, "<Q")
        world.mem.write(namep + 10, b"X")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write"], "badname")
        check("marking.tightness" in out and "REFUSED" in out and "Name" in out and "BATTERY KNOB shape.width_in" in out,
              "run: a corrupted instance Name refuses that knob (identity), the other knob runs")
        check(abs(world.knob_value("marking.tightness") - 0.5) < 1e-6, "run: the refused knob was never written")
        # write failure mid-run -> FAIL, restore attempted
        world = bmock.build_world("scale=0.02;delay=1")
        orig_one = world.mem._one
        cnt = {"n": 0}

        def flaky(op, d_=None):
            if op.get("op") == "write":
                cnt["n"] += 1
                if cnt["n"] == 3:
                    return {"ok": False, "error": "mock: dev timeout"}
            return orig_one(op, d_)
        world.mem._one = flaky
        lst("shape.width_in", "shape.width_out")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write"], "wfail")
        check(rc == 1 and "FAILED" in out, "a failing write: exit 1 FAIL (the wrapper kills), rollback / restore attempted (exit %d)" % rc)
        check(abs(world.knob_value("shape.width_in") - 1.0) < 1e-6 and abs(world.knob_value("shape.width_out") - 1.0) < 1e-6,
              "after the failed write both knobs are back at FC 27's value")
        # no match -> INCONCLUSIVE
        world = bmock.build_world("scale=0.02;delay=100000")
        n0 = len(world.writes)
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--wait-start", "12"], "nomatch")
        check(rc == 3 and "no running match" in out, "no match within the wait: INCONCLUSIVE (exit %d)" % rc)
        check(len(world.writes) == n0, "no knob write happened without a match")
        # refusal without --allow-write / closed GAME_STATE (non mock)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            try:
                rc = main(["run", "--mock", "--knobs", knobs], bmock.build_world("scale=0.02"))
            except Refused as e:
                rc = 3
        check(rc in (3,), "run without --allow-write is refused (exit %d)" % rc)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
