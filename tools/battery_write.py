"""battery_write.py - COORDINATOR-ONLY one-launch check battery for the Turbo gameplay-table knobs (tools/), 2.6 revision. It WRITES knobs through the
dev service (extreme values, always restored) and kills nothing itself: battery_run.sh kills the game on every exit path.

Decides per knob whether it earns a slider (AGENT_RULES_25): (1) ENGINE-READ check - a deterministic observable of the engine's own
consumer (battery_engine.py: width bands, paired length response) - and (2) VISIBLE-EFFECT-AT-EXTREME check - route-C positions of the 22
players in A E A E ... A windows (battery_lib.py: replicate windows of one extreme against the neighbouring baselines, pre-registered
criterion, a-priori MDE printed by `plan`, extremes that cannot reach their dmin deferred with the seconds they would need).
2.6 changes:
  - EVERY COPY: a group that has a simulation variant exists twice (base and simulation database); the match resolves the simulation one. Each
    knob is located in every declared copy (databases from the manager's slots, the hub cache, or `hub --find-dbs`), identity-checked per copy, written
    to ALL copies or none (rollback), restored per copy, and the copy the engine resolves is logged and asserted: the engine-resolved instance
    must be one of the written copies (for the width / length groups the engine-read check compares it with the written address).
  - FAST SAMPLER: one dev-service SEQ request carries several position snapshots (sleep steps between) plus an engine read and the identity
    sentinels: about 4 position samples per request instead of one request per sample (the service answers about once per second).
  - Every window starts with a write BETWEEN two cycles of the same process (no second dev2 client); every extreme window is also a mid-match-write test.

  python battery_write.py plan     --knobs FILE [--win auto --budget 420]        offline: the window schedule, a-priori power, the knob table
  python battery_write.py hub      --knobs FILE [--find-dbs] [--prewrite knob=value --allow-write]   read-only preflight in the hub (GAME_STATE HUB / MATCH-AB)
  python battery_write.py run      --knobs FILE --allow-write [--prewritten knob=value]  in a running match (GAME_STATE MATCH-AB): waits for the clock
  python battery_write.py analyse  EVIDENCE_DIR                                  offline: re-judge a saved run
  python battery_write.py audit                                                  offline: knob catalogue against the attribulator inventory (copies, defaults)
  python battery_write.py --self-test                                            offline proof on the mock world (no game)
Common: --catalogue PATH --owned knob=value (a foreign current value that is Turbo's: e.g. the profile's) --state PATH --wlog PATH --off PATH
        --mock [--mock-world SPEC] (offline) --evidence DIR --allow-underpowered
Owner rule (per knob AND per copy, as x1_write.py): a write needs the copy's current value to be FC 27's default for that copy, the value this tool
wrote last (TO\battery_state.json, same game process), a value declared with --owned, or - for shape.width_in only - the value in
TOOLS\ab_width_state.json.
Evidence: turbo_dev\evidence\<date>\battery_<HHMMSS>\ (plan.json, positions.csv, engine.csv, writes.json, report.txt, result.json, log.txt).
Exit: 0 PASS (run completed, every scheduled knob decided or deferred, restore verified), 1 FAIL (a write / read-back / restore failed or the
identity was lost mid-run), 3 INCONCLUSIVE (refused, no match, nothing judged).
"""
import argparse, contextlib, csv, functools, hashlib, io, json, math, os, re, statistics, struct, sys, time

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


def walk_db(dev, db, loc, allowed):
    """walk one database object to the group's instance: [db + family] -> group -> [group + slot] -> instance, every step identity-checked.
    allowed = the instance Names the walk may land on. -> dict(db, grp, grp_raw, inst, inst_raw, name, ti); raises Refused on any mismatch"""
    fam, gs = int(loc["family_slot"], 16), int(loc["group_slot"], 16)
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
    want = max(len(n) for n in allowed) + 1
    nb = dev.multi([(namep, want)])[0] if be.ptr_ok(namep) else None
    got = (nb or b"").split(b"\0")[0].decode("latin-1", "replace")
    if got not in allowed:
        raise Refused("instance Name %r is none of %s" % (got[:80], [n[-48:] for n in allowed]))
    vt, ti = be.q(blk, 0), be.q(blk, 8)
    if not (be.BASE <= (vt or 0) < be.BASE + be.IMAGE_SIZE):
        raise Refused("instance vtable 0x%X is not an FC27.exe pointer" % (vt or 0))
    if loc["type"].endswith("linelengthwidth") and ti != be.C["TI_LLW"]:
        raise Refused("instance TypeInfo 0x%X != 0x%X" % (ti or 0, be.C["TI_LLW"]))
    return {"db": db, "grp": grp, "grp_raw": grp_raw, "inst": inst, "inst_raw": inst_raw, "name": got, "ti": ti, "fam": fam, "gs": gs}


def locate_knob(dev, mgr, cat, loc):
    """walk the engine's own chain (manager -> CURRENT database handle -> database -> group -> instance) for one locator ->
    dict(inst, db, grp, h, name, variant, ti, chain...) ; raises Refused on any mismatch. This is the copy the engine resolves right now."""
    prem = "prematch" in loc.get("db_type", "")
    slot = int(cat["attrib_manager"]["prematch_slot"], 16) if prem else int(cat["attrib_manager"]["current"][0], 16)
    h = be.q(dev.multi([(mgr + slot, 8)])[0])
    if not be.ptr_ok(h):
        raise Refused("manager slot +0x%X = %s" % (slot, h))
    db = be.q(dev.multi([(h + 8, 8)])[0])
    if not be.ptr_ok(db):
        raise Refused("[handle+8] (database) = %s" % db)
    w = walk_db(dev, db, loc, [v["instance"] for v in loc["variants"]])
    w.update(h=h, variant="simulation" if "/simulation/" in w["name"] else "base", slot=slot, chain=[h, db, w["grp_raw"], w["inst_raw"]])
    return w


# ------------------------------------------------------------------------------------------------------------- databases
def check_dbs(run, addrs):
    """-> {addr: Name} of the addresses that are gameplay database objects (TypeInfo gameplayattribdb, vtable in FC27.exe, Name a known database)"""
    out = {}
    addrs = [a for a in addrs if be.ptr_ok(a)]
    if not addrs:
        return out
    hdr = run.dev.multi([(a, 0x20) for a in addrs])
    nps = [(be.q(h, 0x18) if h else None) for h in hdr]
    nms = run.dev.multi([(p, 120) if be.ptr_ok(p) else (be.BASE, 8) for p in nps])
    known = {n.lower() for n in run.cat["attrib_manager"].get("gameplay_dbs", [])}
    for a, h, p, nm in zip(addrs, hdr, nps, nms):
        if not h or be.q(h, 8) != be.C["TI_DB_GP"] or not (be.BASE <= (be.q(h, 0) or 0) < be.BASE + be.IMAGE_SIZE) or not be.ptr_ok(p) or not nm:
            continue
        name = nm.split(b"\0")[0].decode("latin-1", "replace")
        if name.lower() in known:
            out[a] = name
    return out


def scan_manager_dbs(run):
    """every 8-aligned manager slot whose handle holds a gameplay database (the handle's database pointer is at +8): read-only, bounded to mgr_size"""
    size = int(run.cat["attrib_manager"].get("mgr_size", "0x240"), 16)
    hs = run.dev.multi([(run.mgr + o, 8) for o in range(0, size, 8)])
    handles = [be.q(b) for b in hs]
    cand = run.dev.multi([(h + 8, 8) if be.ptr_ok(h) else (be.BASE, 8) for h in handles])
    return check_dbs(run, [be.q(b) for b, h in zip(cand, handles) if be.ptr_ok(h)])


def find_dbs(run, near, log, budget=150.0):
    """HUB ONLY (a heap search): the database objects are the addresses 8 bytes below a writable qword holding the gameplayattribdb TypeInfo; the 10-13
    hub run found all 15 within ~0x1C000 bytes. Spans: near +-4 MB, +-512 MB, then the whole writable address space (budgeted)."""
    pat = " ".join("%02X" % b for b in struct.pack("<Q", be.C["TI_DB_GP"]))
    spans = [(max(0x10000, near - 0x400000), near + 0x400000), (max(0x10000, near - 0x20000000), near + 0x20000000),
             (0x10000, 0x40000000), (0x40000000, 0x100000000), (0x100000000, 0x200000000), (0x200000000, 0x400000000), (0x400000000, 0x800000000000)]
    found, t_end, t0 = {}, run.clock() + budget, run.clock()
    need = {n.lower() for n in run.need_dbs}
    for lo, hi in spans:
        todo = [(lo, hi)]
        while todo and run.clock() < t_end:
            a, b = todo.pop(0)
            r = run.call({"op": "find", "pattern": pat, "max": 64, "from": "0x%X" % a, "to": "0x%X" % b, "writable": True}, timeout=45)
            if not r.get("ok"):
                log("  find 0x%X..0x%X: %s" % (a, b, r.get("error") or r))
                continue
            if r.get("timed_out") and b - a > 0x1000000:
                m = (a + (b - a) // 2) & ~0xFFF
                todo[:0] = [(a, m), (m, b)]
                continue
            found.update(check_dbs(run, [int(h, 16) - 8 for h in r.get("hits", [])]))
        have = {n.lower() for n in found.values()} | {n.lower() for n in run.dbs}
        if need <= have:
            break
    log("  find: %d database object(s) in %.0f s: %s" % (len(found), run.clock() - t0, sorted(set(n.rsplit("/", 1)[-1] for n in found.values()))))
    return found


def load_db_cache(run):
    try:
        with open(run.a.dbs_cache, encoding="utf-8") as f:
            d = json.load(f)
    except (OSError, ValueError):
        return {}
    if d.get("pid") != run.pid or d.get("build") != be.BUILD:
        return {}
    return {a: n for n, a in ((n, int(a, 16)) for n, a in d.get("dbs", {}).items())}


def save_db_cache(run):
    tmp = run.a.dbs_cache + ".tmp%d" % os.getpid()
    os.makedirs(os.path.dirname(os.path.abspath(run.a.dbs_cache)), exist_ok=True)
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"pid": run.pid, "build": be.BUILD, "t": time.strftime("%Y-%m-%d %H:%M:%S"), "dbs": {n: "0x%X" % a for n, a in run.dbs.items()}}, f, indent=1)
    os.replace(tmp, run.a.dbs_cache)


def enumerate_dbs(run, log, find=False):
    """fills run.dbs {Name: address}: the current database (manager chain), every manager slot, the hub cache of this game process (re-verified) and,
    in the hub with --find-dbs, a heap search. Identity-checked per object. Never searches the heap inside a timed match."""
    found = {}
    cur = be.q(run.dev.multi([(be.q(run.dev.multi([(run.mgr, 8)])[0]) + 8, 8)])[0])
    src = {}
    for a, n in check_dbs(run, [cur]).items():
        found[a] = n
        src[n] = "current database"
    for a, n in scan_manager_dbs(run).items():
        if a not in found:
            found[a] = n
            src[n] = "manager slot"
    cache = load_db_cache(run)
    for a, n in check_dbs(run, [a for a in cache if a not in found]).items():
        found[a] = n
        src[n] = "hub cache"
    run.dbs = {n: a for a, n in found.items()}
    run.cur_db = cur
    miss = {n for n in run.need_dbs if n.lower() not in {k.lower() for k in run.dbs}}
    if miss and find:
        for a, n in find_dbs(run, cur, log).items():
            if a not in found:
                found[a] = n
                src[n] = "heap search"
        run.dbs = {n: a for a, n in found.items()}
    log("gameplay databases known: %s" % "; ".join("%s 0x%X (%s%s)" % (n.rsplit("/", 1)[-1], a, src.get(n, "?"), ", CURRENT" if a == cur else "")
                                                  for n, a in sorted(run.dbs.items(), key=lambda x: x[1])))
    return run.dbs


def db_lookup(run, name):
    for n, a in run.dbs.items():
        if n.lower() == name.lower():
            return a
    return None


# ------------------------------------------------------------------------------------------------------------- knob IO
class KnobIO:
    """one knob: located in EVERY declared copy (base / simulation database), identity-checked per copy, guarded all-or-nothing writes with
    read-back and rollback, restore per copy. `where` = the copy the engine resolves right now (the current database's chain)."""

    def __init__(self, run, kid):
        self.run, self.kid = run, kid
        self.k = run.kn[kid]
        self.loc = run.locs[self.k["locator"]]
        self.off = int(self.k["offset"], 16)
        self.fmt = FMT[self.k["type"]]
        self.probe = bl.probe_of(kid)
        self.where = None
        self.copies = []
        if self.off + struct.calcsize(self.fmt) > int(self.loc["type_size"], 16):
            raise Refused("%s: offset 0x%X + %d is outside the %s byte instance" % (kid, self.off, struct.calcsize(self.fmt), self.loc["type_size"]))

    # -- locate
    def locate(self):
        run = self.run
        self.where = locate_knob(run.dev, run.mgr, run.cat, self.loc)
        self.default = default_for(self.k, self.where["variant"])
        prem = "prematch" in self.loc.get("db_type", "")
        copies = []
        for v in self.loc["variants"]:
            if prem or len(self.loc["variants"]) == 1:      # one shared object: reached through the database the engine resolves
                dbo = self.where["db"]
            else:
                dbo = db_lookup(run, v.get("db", ""))
            if dbo is None:
                raise Refused("copy '%s': its database %s is not known (manager slots / hub cache); run `hub --find-dbs` in the hub first, a knob is "
                              "written into every copy or none" % (v["name"], v.get("db")))
            try:
                w = walk_db(run.dev, dbo, self.loc, [v["instance"]])
            except Refused as e:
                raise Refused("copy '%s' (%s): %s" % (v["name"], (v.get("db") or "").rsplit("/", 1)[-1], e))
            copies.append(dict(w, variant=v["name"], default=default_for(self.k, v["name"]), db_name=v.get("db", "")))
        if len({c["inst"] for c in copies}) != len(copies):
            raise Refused("two copies resolve to the same instance: %s" % [(c["variant"], "0x%X" % c["inst"]) for c in copies])
        res = [c for c in copies if c["inst"] == self.where["inst"]]
        if not res:
            raise Refused("the engine resolves %s 0x%X [%s] from the current database, which is none of the located copies %s: a write would not reach "
                          "what the engine reads" % (self.kid, self.where["inst"], self.where["variant"], [(c["variant"], "0x%X" % c["inst"]) for c in copies]))
        self.copies = copies
        self.resolved = res[0]
        self.where["variant"] = res[0]["variant"]
        self.default = res[0]["default"]
        return self.where

    def describe(self):
        return ", ".join("%s 0x%X%s" % (c["variant"], c["inst"], " <= ENGINE" if c is self.resolved else "") for c in self.copies)

    # -- identity: one multi covering the manager chain and every copy
    def verify(self):
        w, dev, run = self.where, self.run.dev, self.run
        pairs = [(run.mgr + w["slot"], 8), (w["h"] + 8, 8), (w["db"] + w["fam"], 8), (w["grp"] + w["gs"], 8), (w["inst"], 0x20)]
        for c in self.copies:
            pairs += [(c["db"] + c["fam"], 8), (c["grp"] + c["gs"], 8), (c["inst"], 0x20)]
        r = dev.multi(pairs)
        now = [be.q(r[0]), be.q(r[1]), be.q(r[2]), be.q(r[3])]
        want = [w["h"], w["db"], w["chain"][2], w["chain"][3]]
        if now != want:
            return False, "the chain moved (the current database changed?): %s != %s" % ([hex(x or 0) for x in now], [hex(x) for x in want])
        if be.q(r[4], 8) != w["ti"]:
            return False, "TypeInfo changed"
        for k, c in enumerate(self.copies):
            a, b, blk = r[5 + 3 * k], r[6 + 3 * k], r[7 + 3 * k]
            if [be.q(a), be.q(b)] != [c["grp_raw"], c["inst_raw"]]:
                return False, "copy '%s': its chain moved" % c["variant"]
            if be.q(blk, 8) != c["ti"]:
                return False, "copy '%s': TypeInfo changed" % c["variant"]
        return True, "ok"

    # -- values
    def read_copy(self, c):
        b = self.run.dev.multi([(c["inst"] + self.off, struct.calcsize(self.fmt))])[0]
        if b is None:
            raise Failed("%s [%s]: value unreadable" % (self.kid, c["variant"]))
        return struct.unpack(self.fmt, b)[0]

    def read(self):
        """the value of the copy the engine resolves"""
        return self.read_copy(self.resolved)

    def read_all(self):
        return {c["variant"]: self.read_copy(c) for c in self.copies}

    def owned(self, variant=None):
        st = self.run.load_state()
        vals = []
        e = (st.get("knobs", {}).get(self.kid) or {}).get("copies", {}).get(variant or self.resolved["variant"])
        c = next((c for c in self.copies if c["variant"] == (variant or self.resolved["variant"])), self.resolved)
        if e and e.get("inst") == "0x%X" % c["inst"] and st.get("pid") == self.run.pid:
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

    def own_ok(self, c, cur):
        return self.same(cur, c["default"]) or any(self.same(cur, v) for v in self.owned(c["variant"]))

    def _put(self, c, value):
        data = struct.pack(self.fmt, value if self.k["type"] == "f32" else int(value))
        self.run.dev_write(c["inst"] + self.off, data)

    def write(self, value, restore=False):
        """write `value` into EVERY copy (restore: each copy's own FC 27 value). All or nothing: a failure rolls the copies already written back.
        Returns the previous value of the copy the engine resolves."""
        run = self.run
        ok, why = self.verify()
        if not ok:
            raise Failed("%s: identity: %s" % (self.kid, why))
        if not restore and not (self.probe["tmin"] <= value <= self.probe["tmax"]):
            raise Refused("%s = %g is outside the engine-safe test range %g .. %g" % (self.kid, value, self.probe["tmin"], self.probe["tmax"]))
        prev = {}
        for c in self.copies:
            cur = self.read_copy(c)
            if not self.own_ok(c, cur):
                raise Refused("%s [%s]: value %g is neither FC 27's %g nor this tool's last write / a declared owned value %s: someone else owns it" % (
                    self.kid, c["variant"], cur, c["default"], self.owned(c["variant"])))
            prev[c["variant"]] = cur
        if os.path.exists(run.off):
            raise Refused("kill switch %s exists" % run.off)
        target = {c["variant"]: (c["default"] if restore else value) for c in self.copies}
        self.save_state(target, prev)
        done, errors, failed = [], [], None
        for c in self.copies:
            if restore and self.same(prev[c["variant"]], target[c["variant"]]):
                continue
            werr = None
            try:
                self._put(c, target[c["variant"]])
            except Failed as e:
                werr = e
            ok2, why2 = self.verify()
            back = self.read_copy(c) if ok2 else None
            if werr or not ok2 or back is None or not self.same(back, target[c["variant"]]):
                errors.append("%s [%s] %g -> %g FAILED (%s)" % (self.kid, c["variant"], prev[c["variant"]], target[c["variant"]], werr or why2 or "read %s" % back))
                failed = c
                break
            done.append(c)
        if errors:
            rb = []
            ok2, _ = self.verify()
            for c in [x for x in self.copies if x in done or x is failed]:
                if not ok2:
                    rb.append("%s: identity changed, no rollback" % c["variant"])
                    continue
                try:
                    self._put(c, prev[c["variant"]])
                    rb.append("%s rolled back to %g (read back %s)" % (c["variant"], prev[c["variant"]], self.read_copy(c)))
                except Failed as e2:
                    rb.append("%s ROLLBACK FAILED: %s" % (c["variant"], e2))
            line = "battery_write: %s 0x%X %s; %s" % (self.kid, self.resolved["inst"], errors[0], "; ".join(rb))
            run.log(line)
            harness.append_log(run.wlog, line)
            raise Failed(line)
        line = "battery_write: %s %s -> %s (read back ok, %d cop%s: %s)" % (
            self.kid, "/".join("%g" % prev[c["variant"]] for c in self.copies), "/".join("%g" % target[c["variant"]] for c in self.copies),
            len(self.copies), "y" if len(self.copies) == 1 else "ies", self.describe())
        run.log(line)
        harness.append_log(run.wlog, line)
        self.save_state(target, prev)
        return prev[self.resolved["variant"]]

    def restore(self):
        """every copy back to its own FC 27 value; -> (ok, message). When the identity check fails because the current database changed (the match
        ended), the copies are located again from the databases we know and the restore is retried once."""
        for attempt in (1, 2):
            try:
                cur = self.read_all()
                if all(self.same(cur[c["variant"]], c["default"]) for c in self.copies):
                    return True, ""
                self.write(self.default, restore=True)
                return True, "restored %s to FC 27's values%s" % (self.kid, "" if attempt == 1 else " (after locating the copies again)")
            except (Failed, Refused, be.DevError) as ex:
                if attempt == 1 and "identity" in str(ex):
                    try:
                        self.locate()
                        continue
                    except (Refused, Failed, be.DevError) as ex2:
                        return False, "%s: %s; locating again: %s" % (self.kid, ex, ex2)
                return False, "%s: %s" % (self.kid, ex)

    def save_state(self, target, prev):
        st = self.run.load_state()
        if st.get("pid") != self.run.pid:
            st = {"pid": self.run.pid, "knobs": {}}
        st["knobs"][self.kid] = {"copies": {c["variant"]: {"inst": "0x%X" % c["inst"], "value": target[c["variant"]], "prev": prev[c["variant"]]}
                                            for c in self.copies}, "t": time.strftime("%H:%M:%S")}
        tmp = self.run.state + ".tmp%d" % os.getpid()
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(st, f)
        os.replace(tmp, self.run.state)


# ------------------------------------------------------------------------------------------------------------- the run object
ENGINE_INST = {"attrib.attackinglinelengthwidth": "inst", "attrib.defensivelinelengthwidth": "instd"}


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
            # a short repost: the service skips a repeated request id, and the default 0.7 s repost adds to the ~1 s round trip [A]
            self.call = functools.partial(dev2.call, repost=a.repost)
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
        self.dbs, self.cur_db, self.need_dbs = {}, None, []

    def set_need(self, entries):
        """the gameplay database names the listed knobs' copies live in"""
        need = []
        for e in entries:
            kn = self.kn.get(e["id"])
            loc = self.locs.get(kn["locator"]) if kn else None
            if loc and "prematch" not in loc.get("db_type", ""):
                need += [v["db"] for v in loc["variants"] if v.get("db")]
        self.need_dbs = sorted(set(need))

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


def engine_copy_lines(run, ctx):
    """the engine-read assertion of the written copy: for the width / length groups the engine's own resolved instance (ctx) must be the very
    object the knob is written to; -> (lines, refused ids). A knob without an engine observable is only checked at pointer level (engine NA)."""
    lines, bad = [], []
    for kid, kio in run.knobio.items():
        key = ENGINE_INST.get(kio.loc["name"])
        if key is None or kio.resolved is None:
            lines.append("  %-36s engine-resolved copy: %s 0x%X (pointer level: the manager's current-database chain; no engine observable)" % (
                kid, kio.resolved["variant"], kio.resolved["inst"]))
            continue
        if ctx[key] != kio.resolved["inst"]:
            lines.append("  %-36s FAIL: the engine's instance %s is NOT the written %s copy %s (a write would not reach what the engine reads)" % (
                kid, be.H(ctx[key]), kio.resolved["variant"], be.H(kio.resolved["inst"])))
            bad.append(kid)
        else:
            lines.append("  %-36s the engine-resolved instance %s == the written %s copy (all copies: %s)" % (
                kid, be.H(ctx[key]), kio.resolved["variant"], kio.describe()))
    return lines, bad


def parse_owned_pairs(items):
    out = []
    for it in items or []:
        kid, _, v = it.partition("=")
        out.append((kid, float(v)))
    return out


# ------------------------------------------------------------------------------------------------------------- plan / hub
def win_arg(a):
    return "auto" if str(a.win).lower() == "auto" else float(a.win)


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
    windows, deferred, notes = bl.plan_schedule(entries, win=win_arg(a), guard=a.guard, budget=a.budget, allow_underpowered=a.allow_underpowered)
    if pre and windows:
        windows.insert(0, {"name": "P1", "kind": "pre", "knob": pre[0], "value": pre[1], "sign": 0, "dur": max(w["dur"] for w in windows), "guard": 0.0})
    return entries, probs, windows, deferred, notes, pre


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


PREREG = ("PRE-REGISTRATION: a knob extreme is judged on >= %d replicate windows E_j of T seconds against the neighbouring baseline windows (A E A E ... A); "
          "effect = mean_j[E_j - interpolated A]; SE = max(a-priori SE from the measured long-run noise sigma [battery_lib.NOISE_PRIOR], replicate SE); "
          "PASS |effect| >= dmin, effect / SE >= %.1f, the expected sign, >= %d%% of the replicates agree | FAIL |effect| < dmin although MDE = %.1f x SE <= dmin "
          "(or a significant wrong sign) | otherwise INCONCLUSIVE. dmin is a practical size (about 15 - 20 %% of the metric), not derived from the noise.") % (
    bl.MIN_REPS, bl.Z_PASS, int(bl.AGREE_SHARE * 100), bl.Z_MDE)


def print_plan(entries, probs, windows, deferred, a, notes=None):
    print("knob list: %d entries" % len(entries))
    for p in probs:
        print("  PROBLEM %s" % p)
    wl = [w for w in windows if w["kind"] != "pre"]
    ew = next((x for x in wl if x["kind"] == "ext"), None)
    print("schedule (%.0f s of windows incl. guards; extreme / baseline windows %s s, guard %.0f s, budget %.0f s, %d windows):" % (
        bl.schedule_seconds(wl), "%.0f" % ew["dur"] if ew else "-", a.guard, a.budget, len(wl)))
    t = 0.0
    for w in windows:
        print("  %5.0f-%5.0f s  %-3s %-4s %s" % (t, t + w["guard"] + w["dur"], w["name"], w["kind"],
                                               "" if w["knob"] is None else "%s = %g (expected sign %+d)" % (w["knob"], w["value"], w["sign"])))
        t += w["guard"] + w["dur"]
    for kid, why in deferred:
        print("  deferred / skipped: %s - %s" % (kid, why))
    if notes:
        print("a-priori power (sigma = measured long-run noise of the metric; MDE = %.1f x a-priori SE):" % bl.Z_MDE)
        for ln in bl.power_lines(windows, notes):
            print(ln)
    print(PREREG)


def cmd_plan(a):
    entries, probs, windows, deferred, notes, pre = make_plan(a)
    print_plan(entries, probs, windows, deferred, a, notes)
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


def hub_preflight(run, entries, log, find=False):
    """-> (ok, lines). Read-only: build / getter / chain, the databases, per knob locate in every copy + identity + current values + owner rule."""
    ctx, bad = run.engine_ctx(need_match=False)
    if ctx is None:
        return False, ["refused: " + "; ".join(bad)]
    run.mgr = ctx["mgr"]
    out = ["build %s; G %s -> mgr %s -> DB %s [%s] -> attacking llw %s (lineWidthModifier %.4f) / defensive %s (%.4f); G1 = %s (0 outside a match)" % (
        ctx["build"], be.H(ctx["G"]), be.H(ctx["mgr"]), be.H(ctx["DB"]), db_name(run, ctx["DB"]), be.H(ctx["inst"]), ctx["L0"],
        be.H(ctx["instd"]), ctx["Ld0"], be.H(ctx["M"] or 0))]
    ok = True
    run.set_need(entries)
    try:
        enumerate_dbs(run, log, find=find)
        if find or not os.path.exists(run.a.dbs_cache):
            save_db_cache(run)
    except (be.DevError, Refused) as ex:
        out.append("  databases: %s" % ex)
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
            vals = kio.read_all()
            cur = vals[kio.resolved["variant"]]
            own = all(kio.own_ok(c, vals[c["variant"]]) for c in kio.copies)
            out.append("  %-36s %s %d cop%s: %s; values %s (FC 27 %s) %s" % (
                e["id"], "LOCATED", len(kio.copies), "y" if len(kio.copies) == 1 else "ies", kio.describe(),
                "/".join("%g" % vals[c["variant"]] for c in kio.copies), "/".join("%g" % c["default"] for c in kio.copies),
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
    entries, probs, windows, deferred, notes, pre = make_plan(a, run)
    print_plan(entries, probs, windows, deferred, a, notes)
    ok, lines = hub_preflight(run, entries, run.log, find=a.find_dbs)
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
            print("prewritten %s = %s (read back ok, every copy)" % (kid, v))
    print("BATTERY HUB PREFLIGHT: %s" % ("PASS" if ok else "FAIL"))
    return harness.finish("PASS" if ok else "FAIL", tool=TOOL)


# ------------------------------------------------------------------------------------------------------------- offline audit
def cmd_audit(a):
    """the knob catalogue against the attribulator inventory: every locator's declared copies, undeclared copies in the inventory, FC 27's per-copy values"""
    inv_path = a.inventory
    cat, kn, locs = load_cat(a.catalogue)
    inv = json.load(open(inv_path, encoding="utf-8"))["groups"]
    by_asset = {v["asset"]: v for v in inv.values()}
    bad, und = 0, []
    for l in cat["locators"]:
        v = by_asset.get(l["variants"][0]["instance"].lower())
        if v is None:
            print("  %-48s not in the inventory" % l["name"])
            continue
        declared = {x["name"] for x in l["variants"]} - {"base"}
        inv_vars = set(v.get("variants", {}))
        flds = {f[0]: f for f in v["fields"]}
        for k in cat["knobs"]:
            if k["locator"] != l["name"]:
                continue
            f = flds.get(k["field"])
            if f is None:
                print("  FIELD MISSING %s %s" % (k["id"], k["field"]))
                bad += 1
                continue
            for cp, vals in [("base", None)] + [(c, v["variants"][c]["differs"]) for c in sorted(declared & inv_vars)]:
                want = f[3] if vals is None else vals.get(k["field"], f[3])
                want = int(want) if isinstance(want, bool) else want
                got = k["default"] if cp == "base" else (k.get("copy_defaults") or {}).get(cp, k["default"])
                if abs(float(want) - float(got)) > 1e-6:
                    print("  DEFAULT MISMATCH %s [%s]: the catalogue says %s, the inventory %s" % (k["id"], cp, got, want))
                    bad += 1
            if f[1] != int(k["offset"], 16):
                print("  OFFSET MISMATCH %s" % k["id"])
                bad += 1
        miss = sorted(inv_vars - declared)
        if miss:
            und.append((l["name"], miss))
        if declared - inv_vars:
            print("  DECLARED BUT NOT IN THE INVENTORY %s: %s" % (l["name"], sorted(declared - inv_vars)))
            bad += 1
    print("copies: %d locators; with a simulation copy declared: %d; knobs checked against FC 27's per-copy values: %d mismatch(es)" % (
        len(cat["locators"]), sum(1 for l in cat["locators"] if len(l["variants"]) > 1), bad))
    for n, m in und:
        print("  UNDECLARED copies of %s in the inventory: %s - Turbo's tuning resolver refuses the whole locator ('an undeclared copy', "
              "turbo_gui.log 2026-10-14 12:11, [V]); the battery locates declared copies only" % (n, m))
    sim = sum(1 for v in inv.values() if "simulation" in v.get("variants", {}))
    print("inventory: %d groups with a simulation copy; %d of them in the knob catalogue" % (sim, sum(
        1 for l in cat["locators"] if len(l["variants"]) > 1)))
    return harness.finish("PASS" if bad == 0 else "FAIL", tool=TOOL)


# ------------------------------------------------------------------------------------------------------------- sampling run
class Recorder:
    """One CYCLE = one dev-service seq request: ns position snapshots (sleep steps between), then - in some cycles - the engine read, then the
    identity sentinels (G1 -> match object, current database pointer). Position times: the response time minus the scheduled sleeps."""

    def __init__(self, run, ctx, m, windows, evdir):
        self.run, self.ctx, self.m, self.windows, self.evdir = run, ctx, m, windows, evdir
        self.pairs = be.sample_pairs(ctx)
        self.erecs, self.writes, self.samples = [], [], 0
        self.pos_f = open(os.path.join(evdir, "positions.csv"), "w", newline="")
        self.pos_w = csv.writer(self.pos_f)
        self.pos_w.writerow(run.mm.POS_HEADER)
        self.t0 = run.clock()
        self.ts0 = self.t0          # positions.csv t, engine.csv t and the windows' t0 / t1 share ONE time base (seconds since the recorder started)
        self.cur = {}
        self.relocated = 0
        self.last, self.prev = {}, {}
        self.cycles, self.seq_fail, self.legacy = 0, 0, False
        self.lat, self.engine_n = [], 0
        self.t_first = None

    def frozen_now(self, style):
        """True when, for a team with the given build-up style, the last two engine reads have equal declared inputs and equal output"""
        for team, x in self.last.items():
            p = self.prev.get(team)
            if x["style"] == style and p is not None and x["inp"] is not None and p["inp"] == x["inp"] and p["length"] == x["length"]:
                return True
        return False

    def reresolve(self, now):
        run = self.run
        run.log("%.1fs attrib chain changed -> re-resolving" % now)
        ctx2, bad = run.engine_ctx(True)
        if ctx2 is None:
            raise Abort("re-resolve failed: " + "; ".join(bad[:3]))
        self.ctx, self.pairs = ctx2, be.sample_pairs(ctx2)
        run.mgr = ctx2["mgr"]

    def ingest_engine(self, r, now, label):
        """-> False when the chain moved (the caller re-resolves); raises Abort when the owner identity is lost"""
        chain_ok, tab, recs = be.parse_sample(self.ctx, r, now)
        if not chain_ok:
            return False
        if any(not x["ident"] for x in recs):
            raise Abort("owner identity lost (match ended?)")
        for x in recs:
            be.derive(x)
            x["win"], x["i"] = label, self.samples
            self.erecs.append(x)
            self.prev[x["team"]], self.last[x["team"]] = self.last.get(x["team"]), x
        self.engine_n += 1
        return True

    def engine_read(self, label):
        run = self.run
        r = run.dev.multi(self.pairs)
        now = run.clock() - self.t0
        if not self.ingest_engine(r, now, label):
            self.reresolve(now)
            return self.engine_read(label)
        return self.erecs[-2:]

    def pos_sample(self, label):
        """the one-request-per-sample path (legacy / fallback when a seq request fails)"""
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
        smp = (t, s[1], s[2], s[3])
        run.mm.write_sample_rows(self.pos_w, self.samples, smp, {"win": label, "vel": None, "clock": None}, self.m.players, self.ts0)
        self.pos_f.flush()
        self.samples += 1
        return smp

    def cycle(self, label_at, ns, gap_ms, with_engine):
        """one seq request -> number of position samples recorded. label_at(t) -> the window label of a snapshot taken at clock time t."""
        run = self.run
        if self.legacy:
            t = run.clock()
            smp = self.pos_sample(label_at(t))
            if smp is not None and with_engine:
                self.engine_read(label_at(t))
            self.cycles += 1
            return 1 if smp is not None else 0
        ops = self.m.sample_ops()
        nops = len(ops)
        sentinel = [(self.ctx["G1"], 8), (self.ctx["A"] + 8, 8)]
        extra = len(self.pairs) if with_engine else 0
        ns = max(1, min(ns, (be.SEQ_MAX - extra - len(sentinel) + 1) // (nops + 1)))
        steps = []
        for k in range(ns):
            steps += [be.bytes_step(a_, n_) for a_, n_ in ops]
            if k < ns - 1:
                steps.append(be.sleep_step(gap_ms))
        if with_engine:
            steps += [be.bytes_step(a_, n_) for a_, n_ in self.pairs]
        steps += [be.bytes_step(a_, n_) for a_, n_ in sentinel]
        t_send = run.clock()
        try:
            res = run.dev.seq(steps, timeout=30)
        except be.DevError as e:
            self.seq_fail += 1
            run.log("seq request failed (%s)%s" % (e, "; falling back to one request per sample" if self.seq_fail >= 2 else ""))
            if self.seq_fail >= 2:
                self.legacy = True
            return 0
        t_recv = run.clock()
        self.lat.append((t_recv - t_send) - (ns - 1) * gap_ms / 1000.0)
        g1, dbp = be.q(res[-2]), be.q(res[-1])
        if g1 != self.ctx["M"]:
            raise Abort("the match object changed ([G1] = %s, was %s): the match ended?" % (be.H(g1 or 0), be.H(self.ctx["M"])))
        n_ok = 0
        for k in range(ns):
            res_k = res[k * nops:(k + 1) * nops]
            t_k = t_recv - 0.10 - (ns - 1 - k) * (gap_ms / 1000.0 + 0.015)
            try:
                s = self.m.parse_sample(res_k, t_k)
            except (struct.error, TypeError, IndexError, ValueError) as e:
                run.log("snapshot unreadable (%s)" % e)
                continue
            smp = (t_k, s[1], s[2], s[3])
            run.mm.write_sample_rows(self.pos_w, self.samples, smp, {"win": label_at(t_k), "vel": None, "clock": None}, self.m.players, self.ts0)
            self.samples += 1
            n_ok += 1
        self.pos_f.flush()
        if self.t_first is None:
            self.t_first = t_recv
        if with_engine:
            re_ = res[ns * nops:ns * nops + len(self.pairs)]
            if not self.ingest_engine(re_, t_recv - self.t0, label_at(t_recv - 0.1)):
                self.reresolve(t_recv - self.t0)
        elif dbp != self.ctx["DB"]:
            self.reresolve(t_recv - self.t0)
        self.cycles += 1
        return n_ok

    def stats_line(self):
        if not self.lat:
            return "sampler: no seq cycles"
        lat = sorted(self.lat)
        return "sampler: %d cycles, %d position samples, %d engine reads, median request latency %.2f s (min %.2f, max %.2f)" % (
            self.cycles, self.samples, self.engine_n, statistics.median(lat), lat[0], lat[-1])

    def close(self):
        self.pos_f.close()


def desired_state(w):
    return {} if w["kind"] == "base" else {w["knob"]: w["value"]}


def do_run(run, entries, windows, rec, pre, log):
    """the schedule loop; returns (ended_by, knobs still written). Writes happen BETWEEN two cycles of this thread. A knob with a paired
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
    has_paired = any(run.knobio[w["knob"]].probe.get("obs") == "paired_len" for w in windows if w["knob"] and w["knob"] in run.knobio)
    st = {"idx": -1, "win_end": 0.0, "guard_until": 0.0, "pending": [], "deadline": 0.0}
    t_clock0 = clock()

    def default_of(kio):
        return kio.resolved["default"]

    def do_write(kid, v, w):
        kio = run.knobio[kid]
        tw = clock() - rec.t0
        is_default = abs(v - default_of(kio)) < 1e-9
        prev = kio.write(v, restore=is_default)
        tw_end = clock() - rec.t0
        if is_default:
            cur_vals.pop(kid, None)
        else:
            cur_vals[kid] = v
        rec.writes.append({"t": tw, "t_end": tw_end, "knob": kid, "value": v, "prev": prev, "window": w["name"]})
        if kio.probe.get("obs") == "paired_len":
            rec.engine_read("x")      # the post-write read at once: the inputs have not had time to move
        log("%s -> %s at %.0f s: %s = %g (%d cop%s)" % (time.strftime("%H:%M:%S"), w["name"], clock() - t_clock0, kid, v, len(kio.copies),
                                                         "y" if len(kio.copies) == 1 else "ies"))

    def open_window(w):
        st["guard_until"] = clock() + w["guard"]
        st["win_end"] = st["guard_until"] + w["dur"]
        w["t_start"] = clock() - rec.t0
        w["t0"] = 0.0 if w["kind"] == "pre" else st["guard_until"] - rec.t0
        w["t1"] = st["win_end"] - rec.t0

    def service_pending():
        """write the deferred paired-observable changes at the first frozen-input moment (checked right after an engine read, so the
        write follows the last pre-write read within milliseconds) or when PAIRED_WAIT s are over"""
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

    def label_at(t):
        """the window label of a snapshot taken at clock time t: x inside the guard, else the current window"""
        return "x" if t < st["guard_until"] else windows[st["idx"]]["name"]

    ended = "schedule complete"
    stop = a.stop_file
    cyc = 0
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
            changes = [(kid, default_of(run.knobio[kid])) for kid in list(cur_vals) if kid not in want]
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
        eng_every = 1 if (st["pending"] or has_paired or windows[st["idx"]]["kind"] == "pre") else max(1, a.engine_every)
        in_guard = clock() < st["guard_until"]
        n = rec.cycle(label_at, a.burst, a.burst_gap_ms, with_engine=(cyc % eng_every == 0) or in_guard)
        cyc += 1
        if st["pending"]:
            service_pending()
        if cyc % 5 == 0:
            log("%.0fs window %s, %d position samples, %d engine reads" % (clock() - t_clock0, label_at(clock()), rec.samples, rec.engine_n))
        if n == 0 and not rec.legacy:
            sleep(0.5)
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
            # the replicate windows of one value are POOLED (a 30 s window holds too few with-ball engine reads for the 6-sample minimum); the baseline
            # windows too (their conformity to the vanilla band is the FAIL branch's reference)
            by, pw = {}, []
            for v in sorted({round(w["value"], 9) for w in mine}):
                grp = [w for w in mine if abs(w["value"] - v) < 1e-9]
                nm = "%s@%g" % (grp[0]["kind"], v)
                by[nm] = [x for w in grp for x in erecs if x["win"] == w["name"]]
                pw.append(dict(grp[0], name=nm, t0=min((w.get("t0") or 0.0) for w in grp), t1=max((w.get("t1") or 0.0) for w in grp)))
            if base_w:
                by["A*"] = [x for w in base_w for x in erecs if x["win"] == w["name"]]
                pw.append({"name": "A*", "kind": "base", "knob": None, "value": None, "t0": base_w[0].get("t0")})
            by["x"] = [x for x in erecs if x["win"] == "x"]
            j = be.judge_band_knob(kind, by, pw, [x for x in writes if x["knob"] == e["id"]])
            j["kind"] = kind
            res[e["id"]] = j
        else:
            j = be.judge_paired(erecs, [x for x in writes if x["knob"] == e["id"]], pr["obs_style"])
            j["kind"], j["live"] = kind, (True if j["verdict"] == "PASS" else None)
            res[e["id"]] = j
    return res


def analyse_data(positions_csv, erecs, windows, writes, entries, deferred, L, W):
    samples = bl.load_positions_csv(positions_csv)
    roles = bl.derive_roles(samples, {w["name"] for w in windows if w["kind"] == "base"})
    recs = bl.sample_records(samples, L, W, roles)
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
        out.append("A/A control (the baseline windows that differ most, metric %s): diff %+.2f, expected sd of such a difference %.2f, z %.1f (not a gate: a "
                   "noisy baseline widens the replicate SE and ends INCONCLUSIVE)" % (aa["metric"], aa["diff"], aa["se"], aa["z"]))
    out.append(PREREG)
    out.append("")
    out.append("per extreme (knob, value): effect = mean over the replicate windows of (window - interpolated neighbouring baseline); se = max(prior, replicate):")
    for r in table["ext"]:
        out.append("  " + bl.fmt_row(r))
    out.append("per replicate window:")
    for r in table["win"]:
        out.append("    %-4s %-30s %6.3g  %-16s %+7.2f  (window mean %.2f, baseline there %.2f, n %d)" % (
            r["name"], r["knob"], r["value"], "%s:%s" % (r["metric"], r["ctx"]), r["effect"], r["mean"], r["base"], r["n"]))
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
    entries, probs, windows, deferred, notes, pre = make_plan(a, run)
    for p in probs:
        log("PROBLEM %s" % p)
    log("battery evidence: %s" % evdir)
    print_plan(entries, probs, windows, deferred, a, notes)
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
    # the databases (manager slots + the hub cache of this game process; never a heap search inside the timed match), then locate every copy of
    # every knob that has a window with identity checks; refused knobs are dropped and the schedule is re-planned
    run.set_need(entries)
    try:
        enumerate_dbs(run, log, find=False)
    except be.DevError as ex:
        log("databases: %s" % ex)
    refused = []
    keep = []
    for e in entries:
        try:
            kio = KnobIO(run, e["id"])
            w = kio.locate()
            vals = kio.read_all()
            for c in kio.copies:
                if not kio.own_ok(c, vals[c["variant"]]):
                    raise Refused("%s copy: current value %g is neither FC 27's %g nor owned (%s): declare --owned or reset" % (
                        c["variant"], vals[c["variant"]], c["default"], kio.owned(c["variant"])))
            log("  %-36s located %d cop%s: %s; values %s (FC 27 %s)" % (
                e["id"], len(kio.copies), "y" if len(kio.copies) == 1 else "ies", kio.describe(), "/".join("%g" % vals[c["variant"]] for c in kio.copies),
                "/".join("%g" % c["default"] for c in kio.copies)))
            run.knobio[e["id"]] = kio
            keep.append(e)
        except (Refused, Failed, be.DevError) as ex:
            log("  %-36s REFUSED: %s" % (e["id"], ex))
            refused.append((e["id"], "refused: %s" % ex))
    lines, bad = engine_copy_lines(run, ctx)
    log("engine-read assertion of the written copy:")
    for ln in lines:
        log(ln)
    for kid in bad:
        refused.append((kid, "refused: the engine's instance is not the written copy"))
        run.knobio.pop(kid, None)
    keep = [e for e in keep if e["id"] not in bad]
    if pre and pre[0] not in run.knobio:
        pre = None
    entries = keep
    pre = check_pre(pre, entries, []) if pre else None
    windows, deferred2, notes = bl.plan_schedule(entries, win=win_arg(a), guard=a.guard, budget=a.budget, allow_underpowered=a.allow_underpowered)
    if pre and windows:
        windows.insert(0, {"name": "P1", "kind": "pre", "knob": pre[0], "value": pre[1], "sign": 0, "dur": max(w["dur"] for w in windows), "guard": 0.0})
    deferred = deferred2 + refused
    log("final schedule: %d windows, %.0f s" % (len(windows), bl.schedule_seconds([w for w in windows if w["kind"] != "pre"])))
    for ln in bl.power_lines(windows, notes):
        log(ln)
    if not windows:
        return harness.finish("INCONCLUSIVE", ["nothing to schedule after the identity checks: %s" % deferred], tool=TOOL)
    rec = Recorder(run, ctx, m, windows, evdir)
    rec.legacy = bool(a.legacy_sampler)
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
        # restore EVERY copy of every written knob to FC 27's value, whatever happened
        restore_failed = []
        for kid, kio in run.knobio.items():
            if kio.where is None:
                continue
            ok_, msg_ = kio.restore()
            if not ok_:
                restore_failed.append(msg_)
                log("RESTORE FAILED %s" % msg_)
            elif msg_:
                log(msg_)
    log("run ended: %s" % ended)
    log(rec.stats_line())
    # analysis
    save_engine(os.path.join(evdir, "engine.csv"), rec.erecs)
    json.dump({"windows": windows, "writes": rec.writes, "deferred": deferred, "entries": entries, "ended": ended,
               "L": getattr(m, "length", 105.0), "W": getattr(m, "width", 68.0), "relocated": rec.relocated,
               "pre": pre, "notes": [dict(n, knob=k, value=v) for (k, v), n in notes.items()], "sampler": rec.stats_line(),
               "databases": {n: "0x%X" % a_ for n, a_ in run.dbs.items()},
               "copies": {kid: [{"variant": c["variant"], "inst": "0x%X" % c["inst"], "engine": c is kio.resolved} for c in kio.copies]
                          for kid, kio in run.knobio.items()}}, open(os.path.join(evdir, "plan.json"), "w"), indent=1, default=str)
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
    ap.add_argument("cmd", nargs="?", choices=["plan", "hub", "run", "analyse", "audit"])
    ap.add_argument("evidence_dir", nargs="?", help="analyse: the evidence folder")
    ap.add_argument("--knobs", default=os.path.join(HERE, "battery_knobs.txt"))
    ap.add_argument("--catalogue", default=CAT_DEFAULT)
    ap.add_argument("--win", default="auto", help="window length in s (capped at 40), or 'auto': the length (40 / 30 / 24 / 20 s) that schedules the most "
                    "decidable extremes")
    ap.add_argument("--guard", type=float, default=bl.GUARD_S, help="seconds after every write that are not judged (the formation settles in 5 - 10 s)")
    ap.add_argument("--allow-underpowered", action="store_true", help="schedule extremes whose a-priori MDE is above their dmin as well (a small effect ends INCONCLUSIVE)")
    ap.add_argument("--burst", type=int, default=4, help="position snapshots per dev-service seq request (the request limit of 200 steps caps it at 4 - 5)")
    ap.add_argument("--burst-gap-ms", type=int, default=450, help="pause between the snapshots of one request")
    ap.add_argument("--engine-every", type=int, default=2, help="the engine read rides in every N-th request (every request inside a guard)")
    ap.add_argument("--repost", type=float, default=0.25, help="the dev client's repost interval (dev2 default 0.7 s adds to the ~1 s round trip)")
    ap.add_argument("--legacy-sampler", action="store_true", help="one request per position sample and one per engine read (the 2.5 behaviour; for comparison)")
    ap.add_argument("--find-dbs", action="store_true", help="hub only: find the other gameplay database objects with a heap search (read-only)")
    ap.add_argument("--dbs-cache", default=os.path.join(harness.TO, "battery_dbs.json"), help="database addresses found in the hub (valid for this game process)")
    ap.add_argument("--inventory", default=os.path.join(HERE, "..", "docs", "re", "attrib_inventory_6AC07E31.json"), help="audit: the attribulator inventory")
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
    box = harness.timebox((a.max_run or KO_END_S) + a.wait_start + 120 if a.cmd == "run" else (600 if a.find_dbs else 300), TOOL)
    try:
        if a.cmd == "plan":
            return cmd_plan(a)
        if a.cmd == "analyse":
            return cmd_analyse(a)
        if a.cmd == "audit":
            return cmd_audit(a)
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
                           "--ab-state", os.path.join(d, "no_ab.json"), "--stop-file", os.path.join(d, "nostop"),
                           "--dbs-cache", os.path.join(d, tag + "_dbs.json")]
            world = bmock.build_world(world_spec) if not isinstance(world_spec, bmock.MockWorld) else world_spec
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = main(argv, world)
            return rc, buf.getvalue(), world, ev

        def result_of(ev):
            res = json.load(open(os.path.join(ev, "result.json")))
            return {r["knob"]: r for r in res["rows"]}, res

        HUB = "scale=0.02;delay=100000"     # the hub: the match has not started, the current database is the base one
        # ---- offline plan: the a-priori power, the deferral with the seconds needed, the engine-read column
        lst("shape.width_in", "shape.width_out", "shape.width_in_cb", "lineband.deep_max")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["plan", "--knobs", knobs])
        out = buf.getvalue()
        check(rc == 0 and "schedule" in out and "deferred / skipped: lineband.deep_max" in out and "engine-read: band_att" in out,
              "plan: offline schedule, engine-read column, set-up-only knob skipped")
        check("a-priori power" in out and "decidable" in out and "under-powered" in out and "replicates =" in out and "PRE-REGISTRATION" in out,
              "plan: the a-priori power per extreme, an under-powered knob deferred with the seconds it needs, the pre-registration text")
        # ---- audit
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["audit"])
        check(rc == 0 and " 0 mismatch" in buf.getvalue() and "UNDECLARED copies of attrib.defense_tackling" in buf.getvalue(),
              "audit: the catalogue's per-copy defaults agree with the attribulator inventory; undeclared copies are reported")
        # ---- hub: every copy located
        lst("shape.width_in", "shape.width_out", "shape.gap_dmf")
        rc, out, w, ev = run_mock(HUB, ["hub", "--allow-underpowered"], "hub")
        check(rc == 0 and "BATTERY HUB PREFLIGHT: PASS" in out and out.count("LOCATED") == 3 and out.count("<= ENGINE") == 3,
              "hub: 3 knobs located and identity-checked (exit %d)" % rc)
        check("LOCATED 2 copies: base" in out and "simulation 0x" in out and out.count("LOCATED 1 copy") == 2,
              "hub: the knob with a simulation variant is located in BOTH copies, the shared groups once")
        w.put(w.knob_addr["shape.width_in"], "<f", 1.25)
        rc, out, w2, ev = run_mock(w, ["hub"], "hub2")
        check(rc == 1 and "OWNER RULE WOULD REFUSE" in out, "hub: a foreign current value is reported before the run (exit %d)" % rc)
        rc, out, w2, ev = run_mock(w, ["hub", "--owned", "shape.width_in=1.25"], "hub3")
        check(rc == 0, "hub: --owned declares the profile's / foreign value (exit %d)" % rc)
        w3 = bmock.build_world(HUB)
        w3.put(w3.knob_addr_sim["shape.gap_dmf"], "<f", 9.0)
        rc, out, _, _ = run_mock(w3, ["hub"], "hub4")
        check(rc == 1 and "OWNER RULE WOULD REFUSE" in out, "hub: a foreign value in the SIMULATION copy alone is reported too (exit %d)" % rc)
        # ---- databases: only the current one listed -> the other copy is unreachable until the hub heap search found it
        lst("shape.width_in", "shape.gap_dmf")
        w4 = bmock.build_world(HUB + ";dbslot=0")
        rc, out, _, _ = run_mock(w4, ["hub"], "hubnoslot")
        check(rc == 1 and "gap_dmf" in out and "REFUSED" in out and "hub --find-dbs" in out and w4.finds == 0,
              "hub: a copy whose database cannot be found is REFUSED (a knob is written into every copy or none), no heap search without --find-dbs")
        rc, out, _, _ = run_mock(w4, ["hub", "--find-dbs"], "hubfind", "dbs")
        check(rc == 0 and w4.finds >= 1 and "heap search" in out and "LOCATED 2 copies" in out and os.path.exists(os.path.join(d, "dbs_dbs.json")),
              "hub --find-dbs: the heap search finds the simulation database object, both copies located, the cache written (finds %d)" % w4.finds)
        n_finds = w4.finds
        w4.match_delay = 2.0
        w4.t0 = time.time()
        rc, out, _, ev = run_mock(w4, ["run", "--allow-write", "--allow-underpowered"], "runcache", "dbs")
        by, res = result_of(ev)
        check(rc == 0 and w4.finds == n_finds and "hub cache" in out and "located 2 copies" in out,
              "run: the other database comes from the hub cache - no heap search inside the timed match (exit %d)" % rc)
        # ---- full run, everything live; the match resolves the SIMULATION copy
        lst("shape.width_in", "shape.gap_dmf")
        t = time.time()
        rc, out, w, ev = run_mock("scale=0.02;delay=2;lat=1.0", ["run", "--allow-write", "--allow-underpowered", "--win", "20"], "live")
        print(out[-3300:])
        by, res = result_of(ev)
        check(rc == 0, "run: completed, exit 0 (%.0f s)" % (time.time() - t))
        check("Fifa/Attribulator/Gameplay/databases/simulation/simulation" in out and "engine-resolved copy: simulation" in out,
              "run: the log names the database the match resolves (simulation) and the copy of the knob the engine reads")
        check("the engine-resolved instance" in out and "== the written base copy" in out,
              "run: the engine-read check asserts that the engine's own instance is the written copy")
        check(by["shape.width_in"]["engine"] == "PASS" and by["shape.width_in"]["effect"] == "PASS" and by["shape.width_in"]["decision"] == "YES",
              "width_in (live engine, live effect): engine PASS, effect PASS, slider YES")
        check(by["shape.gap_dmf"]["engine"] == "NA" and by["shape.gap_dmf"]["effect"] == "PASS" and by["shape.gap_dmf"]["decision"] == "YES (effect only)",
              "gap_dmf (two copies, the match reads the simulation one): effect PASS only because BOTH copies were written; engine stays NA, never upgraded")
        check(abs(w.knob_value("shape.gap_dmf", "base") - 8.0) < 1e-6 and abs(w.knob_value("shape.gap_dmf", "simulation") - 8.0) < 1e-6
              and abs(w.knob_value("shape.width_in") - 1.0) < 1e-6,
              "every copy of every written knob is back at FC 27's value after the run")
        wl = open(os.path.join(d, "live_w.log")).read()
        check(wl.count("2 copies: base") >= 4 and "FAILED" not in wl and wl.count("(read back ok") >= 8,
              "write log: %d guarded writes, the two-copy knob always in both copies, all read back ok" % wl.count("(read back ok"))
        plan = json.load(open(os.path.join(ev, "plan.json")))
        check(os.path.exists(os.path.join(ev, "positions.csv")) and os.path.exists(os.path.join(ev, "engine.csv")) and plan["notes"]
              and "sampler:" in plan["sampler"] and plan["copies"]["shape.gap_dmf"][1]["engine"] is True,
              "evidence files written: power notes, sampler stats, the copy the engine reads")
        n_fast = sum(1 for _ in open(os.path.join(ev, "positions.csv"))) // 22
        # the same schedule with the 2.5 sampler (one request per position sample, one per engine read) for the throughput comparison
        rc, out2, w_, ev2 = run_mock("scale=0.02;delay=2;lat=1.0", ["run", "--allow-write", "--allow-underpowered", "--win", "20", "--legacy-sampler",
                                                                     "--engine-every", "1"], "legacy")
        n_old = sum(1 for _ in open(os.path.join(ev2, "positions.csv"))) // 22
        check(rc == 0 and n_fast >= 2.9 * n_old,
              "sampler: %d position samples in the same schedule against %d with the 2.5 sampler (x%.1f; the request latency is simulated at 1 s)" % (
                  n_fast, n_old, n_fast / max(1, n_old)))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["analyse", ev])
        check(rc == 0 and "BATTERY KNOB shape.width_in" in buf.getvalue(), "analyse: the saved run re-judges offline")
        # ---- the engine ignores the table and nothing moves -> NO
        lst("shape.width_in")
        rc, out, w, ev = run_mock("scale=0.02;delay=1;engine=width_in:ignore;effects=shape.width_in:ignore:0", ["run", "--allow-write"], "ignore")
        by, res = result_of(ev)
        check(by["shape.width_in"]["engine"] == "FAIL" and by["shape.width_in"]["decision"] == "NO",
              "engine ignores the table and nothing moves: engine FAIL, slider NO (%s / %s)" % (by["shape.width_in"]["engine"], by["shape.width_in"]["decision"]))
        # ---- set-up latch with --prewrite in the hub
        world = bmock.build_world("scale=0.02;delay=100000;engine=width_in:latch;effects=shape.width_in:latch:0.8")
        rc, out, _, _ = run_mock(world, ["hub", "--prewrite", "shape.width_in=0.3", "--allow-write"], "latchhub", "latch")
        check(rc == 0 and abs(world.knob_value("shape.width_in") - 0.3) < 1e-6, "hub --prewrite: 0.3 written before kick-off and read back")
        world.match_delay = 1.0
        world.t0 = time.time()
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--prewritten", "shape.width_in=0.3"], "latch", "latch")
        by, res = result_of(ev)
        r0 = res["rows"][0]
        check(r0["engine"] == "PASS" and r0["live"] is False,
              "latch: the pre-kick-off value is used, mid-match writes are not followed -> engine PASS, mid-match = set-up only (engine %s live %s)" % (
                  r0["engine"], r0["live"]))
        # ---- refusals: a foreign value in either copy, a wrong Name
        lst("shape.width_in", "shape.gap_dmf")
        world = bmock.build_world("scale=0.02;delay=1")
        world.put(world.knob_addr_sim["shape.gap_dmf"], "<f", 9.0)
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--allow-underpowered"], "foreign")
        check("shape.gap_dmf" in out and "REFUSED" in out and "simulation copy" in out,
              "run: a foreign value in the simulation copy refuses that knob, the other runs")
        check(abs(world.knob_value("shape.gap_dmf", "simulation") - 9.0) < 1e-6 and abs(world.knob_value("shape.gap_dmf", "base") - 8.0) < 1e-6,
              "run: neither copy of the refused knob was touched")
        lst("shape.width_in", "marking.tightness")
        world = bmock.build_world("scale=0.02;delay=1")
        inst = world.addr["inst:attrib.markingtightness"]
        namep = world.get(inst + 0x18, "<Q")
        world.mem.write(namep + 10, b"X")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--allow-underpowered"], "badname")
        check("marking.tightness" in out and "REFUSED" in out and "Name" in out and "BATTERY KNOB shape.width_in" in out,
              "run: a corrupted instance Name refuses that knob (identity), the other knob runs")
        check(abs(world.knob_value("marking.tightness") - 0.5) < 1e-6, "run: the refused knob was never written")
        # ---- all or nothing: the second copy's write fails -> the first is rolled back, FAIL
        lst("shape.gap_dmf")
        world = bmock.build_world("scale=0.02;delay=1;failwrite=2")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--allow-underpowered"], "wfail")
        check(rc == 1 and "FAILED" in out and "rolled back" in out,
              "a failing write of the SECOND copy: the first is rolled back, exit 1 FAIL (the wrapper kills) (exit %d)" % rc)
        check(abs(world.knob_value("shape.gap_dmf", "base") - 8.0) < 1e-6 and abs(world.knob_value("shape.gap_dmf", "simulation") - 8.0) < 1e-6,
              "after the failed write both copies are back at FC 27's value")
        lst("shape.width_in", "shape.width_out")
        world = bmock.build_world("scale=0.02;delay=1;failwrite=3")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--allow-underpowered"], "wfail2")
        check(rc == 1 and "FAILED" in out and abs(world.knob_value("shape.width_in") - 1.0) < 1e-6 and abs(world.knob_value("shape.width_out") - 1.0) < 1e-6,
              "a failing write mid-run: exit 1 FAIL, every knob back at FC 27's value (exit %d)" % rc)
        # ---- the identity sentinels: the match ends mid-run
        lst("shape.width_in")
        world = bmock.build_world("scale=0.02;delay=1;end=150")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write"], "ended")
        check(rc == 1 and "the match object changed" in out and abs(world.knob_value("shape.width_in") - 1.0) < 1e-6,
              "the match ends mid-run: the [G1] sentinel aborts, exit 1, the knob restored (exit %d)" % rc)
        # ---- a seq request the service cannot answer: fall back to one request per sample, the run still completes
        world = bmock.build_world("scale=0.02;delay=1;noseq=1")
        rc, out, _, ev = run_mock(world, ["run", "--allow-write"], "noseq")
        check(rc == 0 and "falling back to one request per sample" in out and "BATTERY KNOB shape.width_in" in out,
              "seq unavailable: falls back to the one-request-per-sample sampler and completes (exit %d)" % rc)
        # ---- the engine-resolved instance must be the written copy (unit)
        world = bmock.build_world(HUB)
        lst("shape.width_in")
        a_ = build_parser().parse_args(["--mock", "--knobs", knobs, "--state", os.path.join(d, "u_st.json"), "--wlog", os.path.join(d, "u_w.log"),
                                        "--dbs-cache", os.path.join(d, "u_dbs.json")])
        run_ = Run(a_, world)
        ctx_, bad_ = run_.engine_ctx(need_match=False)
        run_.mgr = ctx_["mgr"]
        run_.set_need([{"id": "shape.width_in"}])
        enumerate_dbs(run_, lambda *x: None)
        kio_ = KnobIO(run_, "shape.width_in")
        kio_.locate()
        run_.knobio["shape.width_in"] = kio_
        l_ok, b_ok = engine_copy_lines(run_, ctx_)
        l_bad, b_bad = engine_copy_lines(run_, dict(ctx_, inst=ctx_["inst"] + 0x10))
        check(not b_ok and b_bad == ["shape.width_in"] and "NOT the written" in l_bad[0],
              "engine-read assertion: the engine's instance == the written copy passes; another object is a refusal")
        check(len(check_dbs(run_, [kio_.resolved["inst"], world.addr["db"], world.addr["sdb"], 0x1234])) == 2,
              "database check: only real gameplay database objects pass (instance, garbage and small addresses are rejected)")
        # ---- no match -> INCONCLUSIVE
        lst("shape.width_in", "shape.width_out")
        world = bmock.build_world("scale=0.02;delay=100000")
        n0 = len(world.writes)
        rc, out, _, ev = run_mock(world, ["run", "--allow-write", "--wait-start", "12"], "nomatch")
        check(rc == 3 and "no running match" in out, "no match within the wait: INCONCLUSIVE (exit %d)" % rc)
        check(len(world.writes) == n0, "no knob write happened without a match")
        # ---- refusal without --allow-write
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
