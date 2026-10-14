"""battery_mock.py - synthetic FC 27 for the offline proof of the knob battery (tools/; never touches the game, no dev service).

MockWorld is an in-RAM fake of the dev service's memory (harness.MockMem's call() gates and result shapes) holding the SAME object chain
the battery reads on build 6AC07E31: PE header, the get_current_gameplay_attribdb stub, G -> manager -> gameplay / prematch handles ->
databases -> family groups -> knob instances (built from the real knob catalogue: locators, slots, offsets, FC 27 defaults), the match
object G1 -> M -> T -> teams -> AiFormationPositioning owners with their formation state, tactic block and K object. Nothing in it is game
data: heap addresses are harness.MOCK_HEAP-based, values are the catalogue's FC 27 defaults and synthetic dynamics.

The engine model (re-computed from the table values at EVERY dev call, like the real update): possession alternates every 12 s with a
loose gap, g in [0.85, 1.20] and h in [0.50, 0.80] vary with time, cap = clamp(lerp(h * Ldef, g * L, t) * K * W, 0, 1.2 W) (X1P.md 2.4),
the formation length follows the table value through a blend weight, the ball vector is frozen during a dead-ball stretch of each 25 s
cycle. Per-knob engine modes: live (reads the table each call), ignore (uses FC 27's value), latch (uses the value present at match start).
MockMatch2 is the position source (route C stand-in): 22 players whose spread follows the table values per knob mode and an effect
strength, plus AR(1) noise and a SLOW game-state component, so the statistics see game-state noise.
2.6: two gameplay databases like the game's - the base one (current in the hub) and the simulation one (current in the match); a locator with a
simulation variant has a SEPARATE instance in the simulation database (the position model reads the copy the CURRENT database resolves, so a
write to the base copy alone has no effect in the match: the trap of TEST_27), every other group is one shared object. The dev service ops
seq (bytes + sleep steps, <= 200) and find (fixed bytes) exist; spec options lat=<sim s per call>, dbslot=0|1 (manager lists the other databases),
trap is the default.
"""
import json, math, os, random, statistics, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402
import battery_engine as be  # noqa: E402

CAT_PATH = os.path.normpath(os.path.join(HERE, "..", "turbo", "package", "turbo", "tuning", "knobs_6AC07E31.json"))
FEET = 1.0 / 0.3048
CYCLE = 20.0           # s: dead-ball stretch [0, DEAD) then play
DEAD = 8.0
POSS_PERIOD = 12.0

DEFAULT_ENGINE = {"width_in": "live", "width_out": "live", "length": "live"}
MOCKPOS = harness.MOCK_HEAP + 0x7F000     # pseudo address: a bytes read here returns the position snapshot (JSON) of the mock match
MOCKPOS_LEN = 8192
DEFAULT_EFFECTS = {   # knob id -> (mode, strength 0..1: how much of the table change reaches the positions)
    "shape.width_in": ("live", 0.8), "shape.width_out": ("live", 0.8), "shape.width_in_cb": ("live", 0.9),
    "shape.length_in": ("live", 0.7), "shape.length_in_possession": ("live", 0.7), "shape.length_out": ("ignore", 0.0),
    "runs.frequency": ("live", 0.6), "marking.tightness": ("live", 0.6), "shape.gap_dmf": ("live", 1.0),
}


def load_catalogue(path=CAT_PATH):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


class WorldMem(harness.MockMem):
    """in-RAM MockMem: regions are bytearrays, every dispatch first lets the world recompute its dynamic objects"""

    def __init__(self, world):
        self.world, self.path, self.regions = world, "<ram>", []

    def add(self, base, size):
        buf = bytearray(size)
        self.regions.append((base, buf))
        return buf

    def read(self, addr, n, d=None):
        for base, buf in self.regions:
            if base <= addr and addr + n <= base + len(buf):
                return bytes(buf[addr - base:addr - base + n])
        return None

    def write(self, addr, data):
        for base, buf in self.regions:
            if base <= addr and addr + len(data) <= base + len(buf):
                buf[addr - base:addr - base + len(data)] = data
                self.world.writes.append((addr, bytes(data)))
                return True
        return False

    def kv(self, key, default=None):
        return default

    def _one(self, op, d=None):
        name = op.get("op")
        w = self.world
        if name == "ping":
            return {"ok": True, "pong": True}
        if name == "modules":
            return {"ok": True, "modules": [{"name": "FC27.exe", "base": "0x%X" % be.BASE, "size": be.IMAGE_SIZE}]}
        if name == "sleep":
            time.sleep(min(10000, max(0, int(op.get("ms", 0)))) / 1000.0 * w.scale)
            return {"ok": True}
        if name == "bytes":
            a = int(str(op["addr"]), 16)
            if a == MOCKPOS:
                snap = w.position_snapshot()
                return {"ok": True, "hex": (snap + bytes(max(0, int(op["len"]) - len(snap))))[:int(op["len"])].hex()}
            b = self.read(a, int(op["len"]))
            return {"ok": True, "hex": b.hex()} if b is not None else {"ok": False, "error": "unreadable"}
        if name == "find":    # fixed bytes only (the battery searches a TypeInfo qword), over the mock regions
            pat = bytes(int(t, 16) for t in op["pattern"].split())
            lo, hi = int(str(op.get("from", "0x0")), 16), int(str(op.get("to", "0x7FFFFFFFFFFF")), 16)
            hits = []
            for base, buf in self.regions:
                i = 0
                while True:
                    i = bytes(buf).find(pat, i)
                    if i < 0:
                        break
                    if lo <= base + i < hi:
                        hits.append("0x%X" % (base + i))
                    i += 1
            w.finds += 1
            return {"ok": True, "hits": hits[:int(op.get("max", 16))]}
        if name == "write":
            a = int(str(op["addr"]), 16)
            w.write_attempts += 1
            if w.fail_writes or w.write_attempts in w.fail_nth:
                return {"ok": False, "error": "mock: write refused (dev timeout)"}
            data = bytes.fromhex(str(op.get("hex") or op.get("bytes")).replace(" ", ""))
            if not self.write(a, data):
                return {"ok": False, "error": "unwritable"}
            return {"ok": True, "hex": self.read(a, len(data)).hex()}
        return {"ok": False, "error": "mock: op %s not supported" % name}

    def call(self, req, timeout=60, repost=0.7, allow_write=False):
        """the dev2 gates (write / key / Escape) then the dispatch; a simulated per-call latency (spec lat=, sim seconds) makes the
        throughput of the sampler measurable offline"""
        import dev2
        if dev2.has_escape(req):
            return {"error": "dev2: Escape (VK 27) is never sent"}
        if not allow_write and dev2.has_write_op(req):
            return {"error": "dev2: write/key ops need allow_write=True (coordinator only)"}
        self.world.calls += 1
        if self.world.latency > 0:
            time.sleep(self.world.latency * self.world.scale)
        return self._dispatch(req)

    def _dispatch(self, req):
        self.world.tick()
        if req.get("op") == "multi":
            return {"ok": True, "results": [self._one(o) for o in req.get("ops", [])]}
        if req.get("op") == "seq":
            if self.world.noseq:
                return {"ok": False, "error": "mock: seq is not supported"}
            steps = req.get("steps", [])
            if len(steps) > 200:
                return {"ok": False, "error": "steps: a list of at most 200"}
            out = []
            for st in steps:
                if st.get("op") != "sleep":
                    self.world.tick()
                out.append(self._one(st))
            return {"ok": all(x.get("ok") for x in out), "results": out}
        return self._one(req)


class MockWorld:
    def __init__(self, catalogue=None, engine=None, effects=None, scale=0.02, match_delay=3.0, seed=7, styles=(1, 0), latency=0.0, dbslot=1,
                 curdb="sim", drift=0.0, slow=0.12, noseq=0, end=None):
        self.latency, self.dbslot, self.curdb = latency, dbslot, curdb
        self.noseq, self.end = noseq, end
        self.calls, self.finds = 0, 0
        self.drift, self.slow = drift, slow
        self.pos_model = None
        self.cat = catalogue or load_catalogue()
        self.engine = dict(DEFAULT_ENGINE, **(engine or {}))
        self.effects = dict(DEFAULT_EFFECTS, **(effects or {}))
        self.scale, self.match_delay, self.styles = scale, match_delay, styles
        self.t0 = time.time()
        self.writes, self.fail_writes, self.killed = [], False, False
        self.fail_nth, self.write_attempts = set(), 0
        self.latched = {}
        self.rng = random.Random(seed)
        self.mem = WorldMem(self)
        self.addr = {}
        self.knob_addr = {}      # knob id -> address of the field in the BASE copy
        self.knob_addr_sim = {}  # knob id -> address of the field in the SIMULATION copy (only knobs whose locator has a simulation variant)
        self.loc_where = {}      # locator name -> (family slot, group slot)
        self.name_ptr = {}
        self._build()

    # ---- clock
    def now(self):
        return (time.time() - self.t0) / self.scale

    # ---- memory helpers
    def put(self, addr, fmt, *vals):
        assert self.mem.write(addr, struct.pack(fmt, *vals))

    def get(self, addr, fmt):
        return struct.unpack(fmt, self.mem.read(addr, struct.calcsize(fmt)))[0]

    def alloc(self, size):
        a = (self.heap_next + 0xF) & ~0xF
        self.heap_next = a + size
        assert self.heap_next < self.heap_end
        return a

    def _build(self):
        m = self.mem
        base = harness.MOCK_HEAP
        self.heap_next, self.heap_end = base, base + 0x80000
        m.add(self.heap_next, 0x80000)
        hdr = bytearray(harness.pe_header(0x6AC07E31, be.IMAGE_SIZE))
        m.regions.append((be.BASE, hdr))
        for a, n in ((be.C["GETTER"], 32), (be.C["G_ATTRIB"], 8), (be.BASE + be.C["G1_RVA"], 8)):
            m.add(a, n)
        stub = bytes.fromhex("488B0D") + struct.pack("<i", be.C["G_ATTRIB"] - (be.C["GETTER"] + 7)) + bytes.fromhex(
            "33C04885C9740C488B114885D27404488B4208C3")
        assert m.write(be.C["GETTER"], stub)
        vt = be.BASE + 0x100000
        mgr, h0, h1, h2, h3 = self.alloc(0x240), self.alloc(0x40), self.alloc(0x40), self.alloc(0x40), self.alloc(0x40)
        db, sdb, pdb = self.alloc(0x170), self.alloc(0x170), self.alloc(0x70)
        self.put(be.C["G_ATTRIB"], "<Q", mgr)
        self.put(mgr, "<Q", h0)
        self.put(mgr + 8, "<Q", h1)
        self.put(h0 + 8, "<Q", db)           # the current database: the base one in the hub, the simulation one in a match (tick)
        self.put(h1 + 8, "<Q", pdb)
        if self.dbslot:                      # the manager also lists the two gameplay databases (slots +0x20 / +0x28)
            self.put(mgr + 0x20, "<Q", h2)
            self.put(mgr + 0x28, "<Q", h3)
            self.put(h2 + 8, "<Q", db)
            self.put(h3 + 8, "<Q", sdb)
        self.h0 = h0
        for d_, ti in ((db, be.C["TI_DB_GP"]), (sdb, be.C["TI_DB_GP"]), (pdb, be.C["TI_DB_GP"] + 0x10)):
            self.put(d_, "<Q", vt)
            self.put(d_ + 8, "<Q", ti)
        for d_, nm_ in ((db, b"Fifa/Attribulator/Gameplay/gameplayattribdb\0"),
                        (sdb, b"Fifa/Attribulator/Gameplay/databases/simulation/simulation\0"),
                        (pdb, b"Fifa/Attribulator/PreMatch/prematchattribdb\0")):
            nmp_ = self.alloc(len(nm_) + 8)
            assert m.write(nmp_, nm_)
            self.put(d_ + 0x18, "<Q", nmp_)
        self.addr.update(mgr=mgr, db=db, sdb=sdb, pdb=pdb)
        groups = {}
        loc_group = {}
        n_tag = 0
        for loc in self.cat["locators"]:
            dbo = pdb if "prematch" in loc.get("db_type", "") else db
            fam = int(loc["family_slot"], 16)
            if (dbo, fam) not in groups:
                g = self.alloc(0x200)
                self.put(g, "<Q", vt)
                self.put(g + 8, "<Q", be.C["TI_GRP_PF"] if loc["group_type"].endswith("positioningformation") else be.C["TI_GRP_PF"] + 0x20 * (len(groups) + 1))
                tag = 4 if (len(groups) == 1) else 0   # one family pointer carries the tag bit the game masks with -5
                self.put(dbo + fam, "<Q", g | tag)
                groups[(dbo, fam)] = g
            g = groups[(dbo, fam)]
            inst = self.alloc(int(loc["type_size"], 16))
            nm = loc["variants"][0]["instance"].encode() + b"\0"
            nmp = self.alloc(len(nm) + 8)
            assert m.write(nmp, nm)
            self.put(inst, "<Q", vt)
            ti = be.C["TI_LLW"] if loc["type"].endswith("linelengthwidth") else be.C["TI_LLW"] + 0x40 * (1 + len(self.knob_addr))
            self.put(inst + 8, "<Q", ti)
            self.put(inst + 0x18, "<Q", nmp)
            n_tag += 1
            self.put(g + int(loc["group_slot"], 16), "<Q", inst | (4 if n_tag == 3 else 0))
            self.addr["inst:" + loc["name"]] = inst
            self.loc_where[loc["name"]] = (fam, int(loc["group_slot"], 16))
            loc_group[loc["name"]] = (dbo, fam, g)
            for k in self.cat["knobs"]:
                if k["locator"] == loc["name"]:
                    self.knob_addr[k["id"]] = inst + int(k["offset"], 16)
                    self.put_knob(k, k["default"])
        # the simulation database: a group object of its own for every family; a locator with a simulation variant has a separate instance
        # (its own Name, FC 27's value of that copy), every other group slot points at the SAME instance as the base database
        sgroups = {}
        for (dbo, fam), g in groups.items():
            if dbo != db:
                continue
            gs = self.alloc(0x200)
            self.put(gs, "<Q", vt)
            self.put(gs + 8, "<Q", self.get(g + 8, "<Q"))
            self.put(sdb + fam, "<Q", gs | (self.get(db + fam, "<Q") & 4))
            sgroups[fam] = gs
            for off in range(0x10, 0x200, 8):
                self.put(gs + off, "<Q", self.get(g + off, "<Q"))
        for loc in self.cat["locators"]:
            if "prematch" in loc.get("db_type", ""):
                continue
            fam, slot = self.loc_where[loc["name"]]
            sim = next((v for v in loc["variants"] if v["name"] == "simulation"), None)
            if not sim:
                continue
            base_inst = self.addr["inst:" + loc["name"]]
            size = int(loc["type_size"], 16)
            inst = self.alloc(size)
            assert m.write(inst, self.mem.read(base_inst, size))
            nm = sim["instance"].encode() + b"\0"
            nmp = self.alloc(len(nm) + 8)
            assert m.write(nmp, nm)
            self.put(inst + 0x18, "<Q", nmp)
            self.put(sgroups[fam] + slot, "<Q", inst)
            self.addr["sim_inst:" + loc["name"]] = inst
            for k in self.cat["knobs"]:
                if k["locator"] == loc["name"]:
                    self.knob_addr_sim[k["id"]] = inst + int(k["offset"], 16)
                    self.put_knob_at(k, self.knob_addr_sim[k["id"]], (k.get("copy_defaults") or {}).get("simulation", k["default"]))
        att = self.addr["inst:attrib.attackinglinelengthwidth"]
        for off, v in ((0x2C, 0.9), (0x38, 0.8), (0x3C, 1.0), (0x4C, 0.7), (0x54, 0.9)):
            self.put(att + off, "<f", v)
        self.att, self.dfn = att, self.addr["inst:attrib.defensivelinelengthwidth"]
        # match object (G1 stays 0 until match_delay)
        M, T, F = self.alloc(0x1000), self.alloc(0x4000), self.alloc(0x100)
        arr = self.alloc(16)
        self.put(M + be.C["M_T"], "<Q", T)
        self.put(M + be.C["M_F"], "<Q", F)
        self.put(T + be.C["T_TEAMS"], "<Q", arr)
        self.put(F + be.C["F_LEN"], "<2f", 105.0 * FEET, 68.0 * FEET)
        self.tms, self.owners, self.tbs, self.kobjs = [], [], [], []
        for k in range(2):
            tm, ow, kobj, cell, tb = self.alloc(0x2300), self.alloc(0x2000), self.alloc(0x5C00), self.alloc(16), self.alloc(0x900)
            self.put(arr + 8 * k, "<Q", tm)
            self.put(tm + be.C["TM_M"], "<Q", M)
            self.put(tm + be.C["TM_IDX"], "<i", k)
            self.put(tm + be.C["TM_TAC"], "<Q", cell)
            self.put(tm + be.C["TM_OWNER"], "<Q", ow)
            self.put(cell, "<Q", tb)
            self.put(tb + be.C["TB_STYLE"], "<i", self.styles[k])
            self.put(tb + 0x4D4, "<i", 50 + k)
            self.put(ow, "<Q", vt)
            self.put(ow + be.C["OW_T"], "<Q", T)
            self.put(ow + be.C["OW_TM"], "<Q", tm)
            self.put(ow + be.C["OW_F"], "<Q", F)
            self.put(ow + be.C["OW_M"], "<Q", M)
            self.put(ow + be.C["OW_KOBJ"], "<Q", kobj)
            self.put(ow + be.C["OW_TEAM"], "<i", k)
            self.put(kobj + be.C["K_BASE"] + k * be.C["K_STRIDE"], "<f", 1.0)
            self.put(ow + be.C["OW_STATE"] + be.C["ST_BACK"], "<i", 4)
            for nm_, lst in (("tm", self.tms), ("ow", self.owners), ("tb", self.tbs), ("kobj", self.kobjs)):
                lst.append({"tm": tm, "ow": ow, "tb": tb, "kobj": kobj}[nm_])
        self.M, self.T, self.F = M, T, F

    def put_knob(self, k, v):
        a = self.knob_addr[k["id"]]
        fmt = {"f32": "<f", "i32": "<i", "bool": "<B"}[k["type"]]
        self.put(a, fmt, float(v) if k["type"] == "f32" else int(v))

    def put_knob_at(self, k, addr, v):
        fmt = {"f32": "<f", "i32": "<i", "bool": "<B"}[k["type"]]
        self.put(addr, fmt, float(v) if k["type"] == "f32" else int(v))

    def knob_value(self, kid, copy="base"):
        """the value in the base copy (default) or the simulation copy of a knob"""
        k = next(x for x in self.cat["knobs"] if x["id"] == kid)
        fmt = {"f32": "<f", "i32": "<i", "bool": "<B"}[k["type"]]
        addr = self.knob_addr_sim.get(kid) if copy == "simulation" else self.knob_addr[kid]
        if addr is None:
            addr = self.knob_addr[kid]       # one shared object: both copies are the same bytes
        return self.get(addr, fmt)

    def resolved_knob_value(self, kid):
        """the value the engine reads: the instance the CURRENT database resolves for the knob's group (what the position model follows)"""
        k = next(x for x in self.cat["knobs"] if x["id"] == kid)
        fam, slot = self.loc_where[k["locator"]]
        cur = self.get(self.get(self.get(be.C["G_ATTRIB"], "<Q"), "<Q") + 8, "<Q")
        grp = self.get(cur + fam, "<Q") & ~4
        inst = self.get(grp + slot, "<Q") & ~4
        fmt = {"f32": "<f", "i32": "<i", "bool": "<B"}[k["type"]]
        return self.get(inst + int(k["offset"], 16), fmt)

    def position_snapshot(self):
        t, out, ball, carrier = self.positions_model().read_sample(paid=True)
        return json.dumps({"t": t, "pos": out, "ball": ball, "carrier": carrier}).encode()

    # ---- dynamics
    def in_match(self):
        return (not self.killed) and self.now() >= self.match_delay and (self.end is None or self.now() < self.end)

    def poss(self, t):
        ph = (t % (2 * POSS_PERIOD))
        if ph % POSS_PERIOD > POSS_PERIOD - 1.5:
            return -1
        return int(ph // POSS_PERIOD)

    def frozen(self, t):
        return (t % CYCLE) < DEAD

    def ball(self, t):
        if self.frozen(t):
            t = (t // CYCLE) * CYCLE
        return (30.0 * math.sin(t / 3.0), 0.0, 20.0 * math.sin(t / 2.0))

    def table_for(self, kind, t):
        """the table value the ENGINE uses for a knob group, honouring the engine mode (live / ignore / latch)"""
        mode = self.engine.get(kind, "live")
        att, dfn = self.att, self.dfn
        cur = {"width_in": lambda: self.get(att + 0x28, "<f"), "width_out": lambda: self.get(dfn + 0x28, "<f"),
               "len_bal": lambda: self.get(att + 0x30, "<f"), "len_pos": lambda: self.get(att + 0x50, "<f")}[kind]()
        if mode == "ignore":
            return {"width_in": 1.0, "width_out": 1.0, "len_bal": 0.84, "len_pos": 0.8}[kind]
        if mode == "latch":
            return self.latched.setdefault(kind, cur)
        return cur

    def tick(self):
        t = self.now()
        g1 = be.BASE + be.C["G1_RVA"]
        want = self.M if self.in_match() else 0
        cur = self.addr["sdb"] if (want and self.curdb == "sim") else self.addr["db"]     # the match loads the simulation database
        if self.get(self.h0 + 8, "<Q") != cur:
            self.put(self.h0 + 8, "<Q", cur)
        if self.get(g1, "<Q") != want:
            self.put(g1, "<Q", want)
        if not want:
            return
        L, Ld = self.table_for("width_in", t), self.table_for("width_out", t)
        len_bal, len_pos = self.table_for("len_bal", t), self.table_for("len_pos", t)
        W = 68.0 * FEET
        poss = self.poss(t)
        ball = self.ball(t)
        for k in range(2):
            ow = self.owners[k]
            s = ow + be.C["OW_STATE"]
            tv = 1.0 if poss == k else (0.0 if poss == 1 - k else 0.5)
            g = 0.85 + 0.35 * (0.5 + 0.5 * math.sin(t / 7.0 + k))
            h = 0.50 + 0.30 * (0.5 + 0.5 * math.sin(t / 5.0 + k))
            K = 1.0
            cap = max(0.0, min(1.2 * W, (h * Ld + (g * L - h * Ld) * tv) * K * W))
            width = (min(cap, W * 0.6 * Ld) * (1 - tv)) + (min(cap, W * L) * tv)
            style = self.get(self.tbs[k] + be.C["TB_STYLE"], "<i")
            lm = len_pos if style == 0 else len_bal
            length = 52.5 * FEET * (0.9 + 0.9 * (1 + 0.8 * (lm - 1.0))) / 1.8 * (1.0 + 0.05 * tv)
            self.put(s + be.C["ST_FLAGS"], "<Q", 0)
            self.put(s + be.C["ST_MODE"], "<i", 0)
            self.put(s + be.C["ST_PHASE"], "<i", 1)
            self.put(s + be.C["ST_POSS"], "<i", poss)
            self.put(s + be.C["ST_LEN"], "<f", length)
            self.put(s + be.C["ST_CAP"], "<f", cap)
            self.put(s + be.C["ST_WIDTH"], "<f", width)
            self.put(s + be.C["ST_T"], "<f", tv)
            self.put(s + be.C["ST_BALL"], "<4f", ball[0] * FEET, ball[1], ball[2] * FEET, 1.0)
            self.put(ow + 0x70, "<f", 0.5)
            self.put(ow + 0x74, "<f", 0.25)

    # ---- positions (route C stand-in)
    def positions_model(self):
        if self.pos_model is None:
            self.pos_model = MockMatch2(self)
        return self.pos_model


class MockMatch2:
    """position source with the same interface as match_measure's Match: players, length, width, route, read_sample() -> (t, pos, ball,
    carrier). The knob values reach the geometry per knob mode and strength (default effects table)."""
    route = "mock (synthetic engine)"
    pitch_read = True

    def __init__(self, world, ar=0.6):
        self.w = world
        self.rng = random.Random(11)
        self.ar = ar
        self.noise = {(tm, k): [0.0, 0.0] for tm in (0, 1) for k in range(11)}
        self.players = [{"handle": tm * 25 + k, "team": tm, "fpos": 0 if k == 0 else k + 3, "pid": None, "obj": 0, "state": 0}
                        for tm in (0, 1) for k in range(11)]
        self.length, self.width, self.ball = 105.0, 68.0, None
        self.last_ball_vel = None
        self.last_clock = None
        self.latch = {}
        self.slow, self.slow_t = 0.0, None

    def locate(self):
        return self

    def sample_ops(self):
        """the stand-in for match_measure.Match.sample_ops: one read of the pseudo address that returns this model's snapshot"""
        return [(MOCKPOS, MOCKPOS_LEN)]

    def parse_sample(self, res, t):
        d = json.loads(res[0].rstrip(bytes([0])).decode())
        pos = [tuple(q) if q else None for q in d["pos"]]
        return t, pos, (tuple(d["ball"]) if d["ball"] else None), d["carrier"]

    def factor(self, kid, t):
        mode, strength = self.w.effects.get(kid, ("ignore", 0.0))
        v = self.w.resolved_knob_value(kid)      # the copy the CURRENT database resolves: a write to the other copy has no effect
        k = next(x for x in self.w.cat["knobs"] if x["id"] == kid)
        dflt = float(k["default"])
        if mode == "ignore":
            return 1.0
        if mode == "latch":
            v = self.latch.setdefault(kid, v)
        ratio = (v / dflt) if dflt else 1.0
        return 1.0 + strength * (ratio - 1.0)

    def read_sample(self, paid=False):
        w, rng = self.w, self.rng
        if not paid and w.latency > 0:      # a direct (one request per sample) read costs one simulated request; a seq snapshot has paid already
            time.sleep(w.latency * w.scale)
        t = w.now()
        poss = w.poss(t)
        ball = w.ball(t)
        if w.slow or w.drift:       # slow game-state variation of the spread (correlation time 10 s) and an optional linear drift
            dt = 0.0 if self.slow_t is None else max(0.0, t - self.slow_t)
            a = math.exp(-dt / 10.0)
            self.slow = a * self.slow + rng.gauss(0, math.sqrt(max(0.0, 1 - a * a)))
            self.slow_t = t
        g_slow = (1.0 + w.slow * self.slow) * (1.0 + w.drift * t)
        lat_base = [0, -28, -10, 10, 28, -22, -6, 6, 22, -12, 12]
        dep_base = [3, 14, 16, 16, 14, 34, 36, 36, 34, 55, 55]
        out = []
        for tm in (0, 1):
            s = 1.0 if tm == 0 else -1.0
            has = poss == tm
            style = w.styles[tm]
            f_w = self.factor("shape.width_in", t) if has else self.factor("shape.width_out", t)
            f_cb = self.factor("shape.width_in_cb", t) if has else 1.0
            if has:
                f_l = self.factor("shape.length_in_possession" if style == 0 else "shape.length_in", t)
            else:
                f_l = self.factor("shape.length_out", t)
            f_run = self.factor("runs.frequency", t) if has else 1.0
            f_mark = self.factor("marking.tightness", t) if (poss in (0, 1) and not has) else 1.0
            f_gap = self.factor("shape.gap_dmf", t) if not has else 1.0
            for k in range(11):
                n = self.noise[(tm, k)]
                n[0] = self.ar * n[0] + rng.gauss(0, 3.0 * math.sqrt(1 - self.ar ** 2))
                n[1] = self.ar * n[1] + rng.gauss(0, 1.5 * math.sqrt(1 - self.ar ** 2))
                spread = (1.1 if has else 0.9) * f_w * (g_slow if has else 1.0)
                lat = lat_base[k] * spread + n[0]
                if k in (2, 3):
                    lat = lat_base[k] * 1.0 * f_cb * (1.1 if has else 0.9) + n[0]
                dep = (dep_base[k] - 10) * f_l + 0.2 * (s * ball[0] + 52.5) + n[1]
                if k >= 9 and has:
                    dep += 6.0 * (f_run - 1.0) * 3.0
                if not has and 5 <= k <= 8:
                    dep += 25.0 * (f_gap - 1.0)
                if not has and k >= 1:
                    lat = lat * (1.0 + 0.15 * (f_mark - 1.0))
                out.append((s * (52.5 - dep), 0.0, lat))
        carrier = None if poss == -1 else poss * 25 + 7
        self.ball = ball
        return t, out, ball, carrier


def build_world(spec=None, **kw):
    fail_nth = None
    """spec: 'engine=width_in:ignore,length:live;effects=shape.width_in:ignore:0.8;delay=3;scale=0.02' (tests and --mock CLI)"""
    engine, effects, extra = {}, {}, dict(kw)
    for part in (spec or "").split(";"):
        part = part.strip()
        if not part:
            continue
        key, _, val = part.partition("=")
        if key == "engine":
            for it in val.split(","):
                a, b = it.split(":")
                engine[a] = b
        elif key == "effects":
            for it in val.split(","):
                a, mode, st = it.split(":")
                effects[a] = (mode, float(st))
        elif key == "delay":
            extra["match_delay"] = float(val)
        elif key == "scale":
            extra["scale"] = float(val)
        elif key == "failwrite":
            extra["fail_nth"] = {int(x) for x in val.split(",")}
        elif key == "styles":
            extra["styles"] = tuple(int(x) for x in val.split(","))
        elif key == "lat":
            extra["latency"] = float(val)
        elif key == "dbslot":
            extra["dbslot"] = int(val)
        elif key == "curdb":
            extra["curdb"] = val
        elif key == "slow":
            extra["slow"] = float(val)
        elif key == "noseq":
            extra["noseq"] = int(val)
        elif key == "end":
            extra["end"] = float(val)
        elif key == "drift":
            extra["drift"] = float(val)
    fail_nth = extra.pop("fail_nth", None)
    w = MockWorld(engine=engine, effects=effects, **extra)
    if fail_nth:
        w.fail_nth = set(fail_nth)
    return w


def install_into_match_measure(world, mm):
    """route match_measure's live entry points to the world's position source (what match_measure.install_mock does for its own mock)"""
    mm.SCALE = world.scale
    mm.load_layout = lambda relocate=False, log=print: ("MOCK", be.BASE, {})
    holder = {}

    def _loc(key, lay, args, log=print):
        if "m" not in holder:
            holder["m"] = world.positions_model()
        return holder["m"]
    mm.locate_any = _loc


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        sys.stdout.flush()
        ok = ok and cond

    w = MockWorld(scale=0.01, match_delay=0.0)
    dev = be.DevMulti(w.mem.call)
    time.sleep(0.01)
    ctx, bad = be.resolve(dev)
    check(ctx is not None, "mock world: the engine chain resolves with every identity check (%s)" % ("; ".join(bad[:2]) if bad else "ok"))
    if ctx:
        r = dev.multi(be.sample_pairs(ctx))
        chain_ok, tab, recs = be.parse_sample(ctx, r, 0.0)
        for x in recs:
            be.derive(x)
        check(chain_ok and all(x["ident"] for x in recs) and recs[0]["style"] == 1 and recs[1]["style"] == 0,
              "mock world: a sample parses, both owners identified, styles 1 / 0 read from the tactic block")
        check(abs(tab["L"] - 1.0) < 1e-6 and abs(tab["len_bal"] - 0.84) < 1e-6 and abs(tab["W"] - 68.0 * FEET) < 0.01,
              "mock world: table values read from the engine-resolved instances (L 1.0, Balanced 0.84, pitch in feet)")
    w2 = MockWorld(scale=0.01, match_delay=1e9)
    c2, bad2 = be.resolve(be.DevMulti(w2.mem.call))
    check(c2 is None and any("not in a running match" in b for b in bad2), "mock world: before the match start the check refuses (G1 = 0)")
    w3 = MockWorld(scale=0.01, match_delay=0.0)
    w3.put(w3.tms[0] + be.C["TM_TAC"], "<Q", 0)
    time.sleep(0.01)
    c3, bad3 = be.resolve(be.DevMulti(w3.mem.call))
    if c3 is not None:
        r3 = be.DevMulti(w3.mem.call).multi(be.sample_pairs(c3))
        recs3 = be.parse_sample(c3, r3, 0.0)[2]
    check(c3 is not None and c3["tb_ok"] == [False, True] and recs3[0]["style"] is None and recs3[0]["inp"] is None and recs3[1]["style"] == 0,
          "mock world: an unreadable tactic block disables only that team's style (width checks still resolve)")
    check(w.mem.call({"op": "write", "addr": "0x%X" % w.knob_addr["shape.width_in"], "hex": "00 00 80 3F"})["error"].startswith("dev2"),
          "mock world: a write without allow_write is refused by the same gate as dev2")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    print(__doc__)
    sys.exit(2)
