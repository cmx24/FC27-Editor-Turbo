"""attrib_dump.py - READ-ONLY dump of the live attribulator databases (gameplayattribdb + prematchattribdb) through the dev service,
without the Live Editor natives ("dumper B" of PLAN_tactics_settings.md section 1). Nothing is written to the game.

  python attrib_dump.py [--G ADDR | --layout FILE] [--out FILE.json] [--budget S]      live (coordinator only; GAME_STATE must be live)
  python attrib_dump.py --mock [--mock-limit N] [--out FILE.json]                       offline over a synthetic memory built from the inventory
  python attrib_dump.py --self-test                                                     offline proof on a synthetic inventory

Chain (the one chain_check.py proved): [G] = manager ; every pointer slot of the manager (0x240 bytes) is a handle whose [+8] (bit 2 masked) is
a database object ; a database is IDENTIFIED BY ITS NAME STRING at [DB+0x18] (...gameplayattribdb / ...prematchattribdb) ; DB + family_slot
-> group object ; group + group_slot -> instance (+0 FC27.exe vtable, +8 TypeInfo, +0x18 Name == the asset path, fields from +0x28).
family_slot / group_slot / field offsets come from docs/re/attrib_inventory_6AC07E31.json (decimal ints). G comes from --G or the layout
file (default TO\\assessment_20261013\\T5\\layout_T5.json, outside git: its "G" key).

What is dumped per group (base copy only; the simulation / prematch variants are separate instances):
  scalars (f32 i32 u32 bool i8): the VALUE read at instance+offset
  enum: the raw u32 (the width of an enum field is not proven: raw only)
  arrays / curves / other pointers (af32 ai32 abool au32 curve other): the raw 8-byte pointer slot ONLY - no list layout is assumed.
Output JSON: {tool, mock, started, G, mgr, dbs{kind: {addr, name}}, groups{name: {db, asset, instance, name_ok, name, fields{f: {off, kind, value|raw|ptr}}}},
summary}. Exit 0 all groups' identity OK, 1 an identity FAIL, 3 aborted (refused, no answer, budget).
"""
import argparse, json, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402
import chain_check as cc  # noqa: E402  (Dev: bounded multi reads; masks and pointer tests)

TOOL = "attrib_dump"
INVENTORY = os.environ.get("ATTRIB_INVENTORY", os.path.join(HERE, "..", "docs", "re", "attrib_inventory_6AC07E31.json"))
LAYOUT = os.environ.get("CHAIN_LAYOUT", cc.LAYOUT)
MASK = cc.MASK
SCALAR = {"f32": ("<f", 4), "i32": ("<i", 4), "u32": ("<I", 4), "bool": ("<B", 1), "i8": ("<b", 1)}
MAX_OPS = 200
DB_NAME = {"gameplay": "gameplayattribdb", "prematch": "prematchattribdb"}


class Abort(Exception):
    pass


def u64(b, o=0):
    return struct.unpack_from("<Q", b, o)[0]


def chunks(seq, n):
    for i in range(0, len(seq), n):
        yield seq[i:i + n]


def read_many(dev, reads):
    """reads: [(addr, len)] -> [bytes|None]; split into dev calls of at most MAX_OPS ops"""
    out = []
    for part in chunks(reads, MAX_OPS):
        out.extend(dev.multi(part))
    return out


def decode_field(kind, off, block):
    """-> dict for one inventory field: value for scalars, raw for enum, ptr for pointer kinds; None when the block is too short"""
    if kind in SCALAR:
        fmt, n = SCALAR[kind]
        if off + n > len(block):
            return None
        v = struct.unpack_from(fmt, block, off)[0]
        return {"off": off, "kind": kind, "value": bool(v) if kind == "bool" else v}
    if kind == "enum":
        if off + 4 > len(block):
            return None
        return {"off": off, "kind": kind, "raw": struct.unpack_from("<I", block, off)[0]}
    if off + 8 > len(block):
        return None
    return {"off": off, "kind": kind, "ptr": "0x%X" % u64(block, off)}


def dump(dev, G, inv, say=print):
    """-> result dict. inv = inventory dict (groups). Raises Abort on a structural failure."""
    res = {"G": "0x%X" % G, "mgr": {}, "dbs": {}, "groups": {}}
    (gb,) = dev.multi([(G, 8)])
    mgr = u64(gb) if gb else 0
    if not cc.is_heap_ptr(mgr):
        raise Abort("[G] = 0x%X is not a heap pointer (attribulator not loaded?)" % mgr)
    res["mgr"]["addr"] = "0x%X" % mgr
    (mb,) = dev.multi([(mgr, cc.MGR_SIZE)])
    if mb is None:
        raise Abort("manager at 0x%X unreadable" % mgr)
    slots = [(o, u64(mb, o) & MASK) for o in range(0, cc.MGR_SIZE, 8) if cc.is_heap_ptr(u64(mb, o) & MASK)]
    hb = read_many(dev, [(p, 0x10) for _, p in slots])
    cands = []
    for (o, p), b in zip(slots, hb):
        if b is not None:
            q = u64(b, 8) & MASK
            if cc.is_heap_ptr(q) and q not in [c[2] for c in cands]:
                cands.append((o, p, q))
    cands = cands[:40]
    maxfam = {k: max([g["family_slot"] for g in inv["groups"].values() if g["db"] == k] or [0]) + 8 for k in DB_NAME}
    size = max(maxfam.values())
    dbb = read_many(dev, [(q, size) for _, _, q in cands])
    nm = read_many(dev, [(u64(b, 0x18) & MASK, 0x80) for b in [x or bytes(0x20) for x in dbb]])
    dbs = {}
    for (o, p, q), b, n in zip(cands, dbb, nm):
        name = cc.cstr(n) or ""
        for kind, tail in DB_NAME.items():
            if b is not None and name.lower().endswith(tail) and kind not in dbs:
                dbs[kind] = {"mgr_slot": "0x%X" % o, "addr": q, "name": name, "block": b}
    for kind in DB_NAME:
        if kind not in dbs:
            raise Abort("database %s not found from the manager (name check) - %d candidates" % (DB_NAME[kind], len(cands)))
        res["dbs"][kind] = {k: ("0x%X" % v if k == "addr" else v) for k, v in dbs[kind].items() if k != "block"}
    # group objects: one per (db, family_slot), read up to the highest group_slot used + 8
    fam = {}
    for g in inv["groups"].values():
        key = (g["db"], g["family_slot"])
        fam[key] = max(fam.get(key, 0), g["group_slot"] + 8)
    fkeys = sorted(fam)
    fb = read_many(dev, [(u64(dbs[db]["block"], fs) & MASK if fs + 8 <= len(dbs[db]["block"]) else 0, fam[(db, fs)]) for db, fs in fkeys])
    fblock = dict(zip(fkeys, fb))
    # instances
    names = sorted(inv["groups"])
    iaddr = {}
    for n in names:
        g = inv["groups"][n]
        b = fblock.get((g["db"], g["family_slot"]))
        iaddr[n] = (u64(b, g["group_slot"]) & MASK) if b is not None and g["group_slot"] + 8 <= len(b) else 0
    ib = read_many(dev, [(iaddr[n], inv["groups"][n]["size"]) for n in names])
    nb = read_many(dev, [(u64(b, 0x18) & MASK if b else 0, 0x100) for b in ib])
    n_ok = n_bad = 0
    for n, b, nmb in zip(names, ib, nb):
        g = inv["groups"][n]
        e = {"db": g["db"], "asset": g["asset"], "instance": "0x%X" % iaddr[n], "name": None, "name_ok": False, "fields": {}}
        why = []
        if b is None:
            why.append("instance unreadable")
        else:
            e["name"] = cc.cstr(nmb)
            e["name_ok"] = (e["name"] or "").lower() == g["asset"].lower()
            if not e["name_ok"]:
                why.append("Name %r != %r" % (e["name"], g["asset"]))
            if not cc.is_img_ptr(u64(b, 0)):
                why.append("+0 is not an FC27.exe pointer")
                e["name_ok"] = False
            for fname, off, kind, _van in g["fields"]:
                d = decode_field(kind, off, b)
                if d is not None:
                    e["fields"][fname] = d
        e["why"] = why
        res["groups"][n] = e
        if e["name_ok"]:
            n_ok += 1
        else:
            n_bad += 1
    res["summary"] = "%d groups: %d identity OK / %d FAIL; mgr %s; %d dev calls in %.1f s" % (
        len(names), n_ok, n_bad, res["mgr"]["addr"], dev.calls, time.time() - dev.t0)
    res["ok"], res["bad"] = n_ok, n_bad
    say(res["summary"])
    return res


# ------------------------------------------------------------------------------------------------------------------- mock memory
class Mem:
    """sparse synthetic memory with the dev2 call() shape (ops bytes / multi)"""

    def __init__(self):
        self.pages = {}
        self.top = harness.MOCK_HEAP + 0x10000

    def write(self, addr, data):
        for i, c in enumerate(data):
            a = addr + i
            self.pages.setdefault(a >> 12, bytearray(4096))[a & 4095] = c

    def read(self, addr, n):
        out = bytearray()
        for i in range(n):
            a = addr + i
            p = self.pages.get(a >> 12)
            if p is None:
                return None
            out.append(p[a & 4095])
        return bytes(out)

    def alloc(self, n):
        a = self.top
        self.top += (n + 0xF) & ~0xF
        self.top += 0x10
        return a

    def put(self, data):
        a = self.alloc(len(data))
        self.write(a, data)
        return a

    def call(self, req, timeout=None, allow_write=False):
        if req.get("op") == "multi":
            return {"ok": True, "results": [self.call(o) for o in req["ops"]]}
        if req.get("op") == "bytes":
            b = self.read(int(req["addr"], 16), int(req["len"]))
            return {"ok": True, "hex": b.hex()} if b is not None else {"ok": False, "error": "unreadable"}
        return {"error": "mock: op %r" % req.get("op")}


def build_world(inv, overrides=None, tag_first=True):
    """a synthetic game memory for inv. overrides: {(group, field): value} replaces a scalar. -> (Mem, G)"""
    mem = Mem()
    ov = overrides or {}
    img = harness.IMAGE_BASE
    G = img + 0x2000000
    dbname = {"gameplay": "Fifa/Attribulator/Gameplay/gameplayattribdb", "prematch": "Fifa/Attribulator/Prematch/prematchattribdb"}
    dbobj, famobj = {}, {}
    maxfam = {k: max([g["family_slot"] for g in inv["groups"].values() if g["db"] == k] or [0]) + 8 for k in DB_NAME}
    for kind in DB_NAME:
        blk = bytearray(maxfam[kind] + 0x20)
        struct.pack_into("<Q", blk, 0, img + 0x1000)
        struct.pack_into("<Q", blk, 8, img + 0x2000)
        struct.pack_into("<Q", blk, 0x18, mem.put(dbname[kind].encode() + b"\0" * 8))
        dbobj[kind] = (mem.alloc(len(blk)), blk)
    fsize = {}
    for g in inv["groups"].values():
        key = (g["db"], g["family_slot"])
        fsize[key] = max(fsize.get(key, 0), g["group_slot"] + 0x40)
    for key, sz in fsize.items():
        famobj[key] = (mem.alloc(sz), bytearray(sz))
    for n, g in inv["groups"].items():
        fo, fblk = famobj[(g["db"], g["family_slot"])]
        blk = bytearray(g["size"])
        struct.pack_into("<Q", blk, 0, img + 0x3000)
        struct.pack_into("<Q", blk, 8, img + 0x4000)
        struct.pack_into("<Q", blk, 0x18, mem.put(g["asset"].encode() + b"\0" * 8))
        for fname, off, kind, van in g["fields"]:
            if kind in SCALAR:
                v = ov.get((n, fname), van)
                struct.pack_into(SCALAR[kind][0], blk, off, bool(v) if kind == "bool" else v)
            elif kind == "enum":
                struct.pack_into("<I", blk, off, van if isinstance(van, int) else 0)
            else:
                struct.pack_into("<Q", blk, off, mem.alloc(16))
        ia = mem.put(bytes(blk))
        struct.pack_into("<Q", fblk, g["group_slot"], ia | (4 if tag_first and n == sorted(inv["groups"])[0] else 0))
    for (db, fs), (fo, fblk) in famobj.items():
        mem.write(fo, bytes(fblk))
        struct.pack_into("<Q", dbobj[db][1], fs, fo)
    for kind, (a, blk) in dbobj.items():
        mem.write(a, bytes(blk))
    hA = mem.put(struct.pack("<QQ", 0, dbobj["gameplay"][0] | 4) + bytes(16))   # handle object: [+8] = DB (tag bit 2 set: masked)
    hP = mem.put(struct.pack("<QQ", 0, dbobj["prematch"][0]) + bytes(16))
    mblk = bytearray(cc.MGR_SIZE)
    struct.pack_into("<Q", mblk, 0, hA)
    struct.pack_into("<Q", mblk, 0x20, hP)
    mgr = mem.put(bytes(mblk))
    mem.write(G, struct.pack("<Q", mgr))
    return mem, G


def synthetic_inventory():
    return {"build": "6AC07E31-2145C000", "groups": {
        "gp_a_one": {"db": "gameplay", "asset": "fifa/attribulator/gameplay/groups/gp_a/gp_a_one_runtime", "family_slot": 72, "group_slot": 32,
                     "size": 88, "fields": [["scaleX", 40, "f32", 1.0], ["count", 44, "i32", 3], ["flag", 48, "bool", True],
                                            ["curveA", 56, "curve", [1, 2]], ["listB", 64, "af32", [1.0, 2.0]], ["kind", 72, "enum", 2]]},
        "gp_a_two": {"db": "gameplay", "asset": "fifa/attribulator/gameplay/groups/gp_a/gp_a_two_runtime", "family_slot": 72, "group_slot": 40,
                     "size": 56, "fields": [["scaleX", 40, "f32", 0.5], ["small", 44, "i8", -2]]},
        "pm_b_one": {"db": "prematch", "asset": "fifa/attribulator/prematch/groups/pm_b/pm_b_one_runtime", "family_slot": 64, "group_slot": 24,
                     "size": 64, "fields": [["w", 40, "u32", 7], ["x", 44, "f32", 0.25]]}}}


# ------------------------------------------------------------------------------------------------------------------- CLI
def load_inventory(path, limit=None):
    inv = json.load(open(path, encoding="utf-8"))
    if limit:
        keep = {}
        for db in DB_NAME:
            keep.update(dict([(n, g) for n, g in inv["groups"].items() if g["db"] == db][:limit]))
        inv = dict(inv, groups=keep)
    return inv


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--G", default=None, help="static manager slot address (hex); else the layout file's G")
    ap.add_argument("--layout", default=None)
    ap.add_argument("--inventory", default=INVENTORY)
    ap.add_argument("--out", default=None)
    ap.add_argument("--budget", type=float, default=120.0)
    ap.add_argument("--mock", action="store_true")
    ap.add_argument("--mock-limit", type=int, default=25, help="--mock: groups per database taken from the inventory")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    harness.require_live(a.mock, tool=TOOL)
    box = harness.timebox(a.budget + 30, TOOL)
    try:
        inv = load_inventory(a.inventory, a.mock_limit if a.mock else None)
        if a.mock:
            mem, G = build_world(inv)
            call = mem.call
        else:
            if a.G:
                G = int(a.G, 16)
            else:
                lp = a.layout or LAYOUT
                if not os.path.exists(lp):
                    print("layout %s not found (kept outside git): pass --G or --layout" % lp)
                    return harness.finish("INCONCLUSIVE", tool=TOOL)
                G = int(json.load(open(lp))["G"], 16)
            import dev2
            call = dev2.call
        out = a.out or (None if a.mock else os.path.join(harness.TO, "attrib_dump", "attrib_dump_%s.json" % time.strftime("%Y%m%d-%H%M%S")))
        dev = cc.Dev(call, a.budget)
        try:
            res = dump(dev, G, inv)
        except (Abort, RuntimeError) as e:
            print("ABORT: %s" % e)
            return harness.finish("INCONCLUSIVE", tool=TOOL)
        res.update({"tool": "tools/attrib_dump.py", "mock": a.mock, "started": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "build": inv.get("build")})
        if out:
            os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
            json.dump(res, open(out, "w"), indent=1)
            print("written", out)
        return harness.finish("PASS" if res["bad"] == 0 else "FAIL", tool=TOOL)
    finally:
        box.cancel()


def self_test():
    import contextlib, io
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    quiet = lambda *a: None
    inv = synthetic_inventory()
    mem, G = build_world(inv, {("gp_a_one", "scaleX"): 1.25, ("pm_b_one", "w"): 99})
    r = dump(cc.Dev(mem.call, 30), G, inv, say=quiet)
    g1 = r["groups"]["gp_a_one"]["fields"]
    check(r["bad"] == 0 and r["ok"] == 3, "3 groups found through the mask-tagged handles, names OK")
    check(set(r["dbs"]) == {"gameplay", "prematch"} and r["dbs"]["gameplay"]["name"].lower().endswith("gameplayattribdb"), "both DBs identified by name")
    check(g1["scaleX"]["value"] == 1.25 and g1["count"]["value"] == 3 and g1["flag"]["value"] is True, "scalars read (f32 changed to 1.25, i32, bool)")
    check(r["groups"]["gp_a_two"]["fields"]["small"]["value"] == -2 and r["groups"]["pm_b_one"]["fields"]["w"]["value"] == 99, "i8 and u32, other DB")
    check("ptr" in g1["curveA"] and "ptr" in g1["listB"] and "value" not in g1["curveA"], "curve / array: raw pointer only, no value invented")
    check(g1["kind"] == {"off": 72, "kind": "enum", "raw": 2}, "enum: raw u32 only")
    check(r["groups"]["gp_a_one"]["instance"] != r["groups"]["gp_a_two"]["instance"], "two groups of one family resolve to two instances")
    # a wrong Name FAILs that group only
    mem2, G2 = build_world(inv)
    a1 = int(r["groups"]["gp_a_two"]["instance"], 16)
    ia = int(dump(cc.Dev(mem2.call, 30), G2, inv, say=quiet)["groups"]["gp_a_two"]["instance"], 16)
    np_ = struct.unpack("<Q", mem2.read(ia + 0x18, 8))[0]
    mem2.write(np_, b"WRONG/NAME\0")
    r2 = dump(cc.Dev(mem2.call, 30), G2, inv, say=quiet)
    check(r2["bad"] == 1 and not r2["groups"]["gp_a_two"]["name_ok"] and r2["groups"]["gp_a_one"]["name_ok"], "a wrong instance Name: that group FAILs, the rest stay OK")
    # [G] not a heap pointer -> Abort
    mem3, G3 = build_world(inv)
    mem3.write(G3, struct.pack("<Q", 0))
    try:
        dump(cc.Dev(mem3.call, 30), G3, inv, say=quiet)
        check(False, "[G] = 0: Abort")
    except Abort:
        check(True, "[G] = 0: Abort (no walk)")
    # only reads are ever issued
    ops = []
    orig = mem.call

    def spy(req, timeout=None, allow_write=False):
        for o in (req.get("ops") or [req]):
            ops.append(o.get("op"))
        return orig(req)
    dump(cc.Dev(spy, 30), G, inv, say=quiet)
    check(set(ops) <= {"multi", "bytes"} and ops, "only bytes/multi ops were sent (%d)" % len(ops))
    # the 200-op cap: 450 groups of one family do not exceed it
    big = {"build": "x", "groups": {}}
    for i in range(450):
        big["groups"]["gp_z_%03d" % i] = {"db": "gameplay", "asset": "fifa/attribulator/gameplay/groups/gp_z/gp_z_%03d_runtime" % i,
                                          "family_slot": 72, "group_slot": 8 * (i + 4), "size": 48, "fields": [["v", 40, "f32", float(i)]]}
    big["groups"]["pm_p"] = synthetic_inventory()["groups"]["pm_b_one"]
    memb, Gb = build_world(big)
    calls = []

    def spy2(req, timeout=None, allow_write=False):
        calls.append(len(req.get("ops", [])))
        return memb.call(req)
    rb = dump(cc.Dev(spy2, 30), Gb, big, say=quiet)
    check(max(calls) <= MAX_OPS and rb["bad"] == 0 and rb["groups"]["gp_z_449"]["fields"]["v"]["value"] == 449.0,
          "451 groups: every dev call <= %d ops (max %d), all read" % (MAX_OPS, max(calls)))
    # CLI --mock end to end over the real inventory, if present
    if os.path.exists(INVENTORY):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--mock", "--mock-limit", "15"])
        check(rc == 0 and "identity OK" in buf.getvalue(), "CLI --mock over the real inventory (15 groups per DB): PASS (exit %d)" % rc)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
