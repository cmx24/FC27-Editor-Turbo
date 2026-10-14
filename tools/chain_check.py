"""chain_check.py - READ-ONLY walk of the attribulator chain from the static manager slot G in the RUNNING game for every chain
locator of a layout file, with an identity check per object and a FC 27 / changed verdict per scalar field (sweep T5, 2026-10-13;
WP-C copy in tools/). Dev-service ops: bytes and multi ONLY (dev2 refuses write/key without allow_write). <= 8 dev calls, time-boxed
(--budget, default 60 s): the coordinator runs it in X1's hub phase (x1_run.sh hub runs it when present).

  python chain_check.py [--layout FILE]          live (coordinator only; GAME_STATE must be live, else refused)
  python chain_check.py --mock [--layout FILE]   offline over a synthetic memory built from the layout (default: a synthetic layout)
  python chain_check.py --mock --bad             the same with one wrong TypeInfo (that group alone FAILs)
  python chain_check.py --self-test              offline proof on a synthetic layout (no game data in git)

The layout (type data: TypeInfo / TypeInfoData addresses of build 6AC07E31, sizes, signatures, GUIDs, field offsets, slots, asset
names, FC 27 vanilla values) is NOT in git: default TO\\assessment_20261013\\T5\\layout_T5.json (env CHAIN_LAYOUT, --layout).
Output: TO\\chain_check\\chain_check_<stamp>.json (--out) and PASS / FAIL per group; last line VERDICT; exit 0 all groups PASS,
1 a group FAILs, 3 aborted (no answer, budget, refused).

Chain (from the sweep's T5.md): G -> mgr ; [mgr+0] = A, [A+8] = current gameplay DB ; [mgr+0x10] -> +8 = perks DB [A] ; prematch
handle [U] (scanned: every pointer slot of mgr whose +8 target carries the prematch TypeInfo) ; DB +family -> group ; group +slot ->
instance (+0 vtable, +8 TypeInfo, +0x18 Name, scalars from +0x28). Pointer slots may carry tag bit 2: masked (& ~4) at every step.
Identity per object: TypeInfo at +8 == the expected live TI AND the TypeInfoData behind it has the expected size / field count /
signature / GUID AND the Name at +0x18 equals the expected asset path (case-insensitive) AND +0 is an FC27.exe image pointer.
"""
import argparse, json, os, struct, sys, time, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # dev2 (gate-fixed) and harness live next to this file
import harness  # noqa: E402
LAYOUT = os.environ.get("CHAIN_LAYOUT", os.path.join(harness.TO, "assessment_20261013", "T5", "layout_T5.json"))
IMG_LO, IMG_HI = harness.IMAGE_BASE, harness.IMAGE_BASE + harness.BUILDS["6AC07E31"]["size"]  # FC27.exe 6AC07E31-2145C000
MASK = ~4 & 0xFFFFFFFFFFFFFFFF
PRIM = {"Float32": ("<f", 4), "Int32": ("<i", 4), "Uint32": ("<I", 4), "UInt32": ("<I", 4), "Boolean": ("<B", 1)}
MGR_SIZE = 0x240


def hx(v):
    return "0x%X" % v


def u64(b, o=0):
    return struct.unpack_from("<Q", b, o)[0]


def is_heap_ptr(v):
    return 0x10000 <= v < 0x7FFFFFFFFFFF and v % 8 == 0


def is_img_ptr(v):
    return IMG_LO <= v < IMG_HI


class Dev:
    def __init__(self, call, budget):
        self.call, self.budget, self.t0, self.calls, self.log = call, budget, time.time(), 0, []

    def multi(self, reads):
        """reads: list of (addr, len) -> list of bytes|None. ONE dev call (multi of bytes ops); addr 0 / bad -> None without a read."""
        left = self.budget - (time.time() - self.t0)
        if left < 3:
            raise RuntimeError("time budget exhausted before call %d" % (self.calls + 1))
        ops, idx = [], []
        for i, (a, n) in enumerate(reads):
            if a and is_heap_ptr(a) or (a and is_img_ptr(a)):
                ops.append({"op": "bytes", "addr": hx(a), "len": int(min(max(n, 1), 65536))})
                idx.append(i)
        out = [None] * len(reads)
        if not ops:
            return out
        if len(ops) > 200:
            raise RuntimeError("multi of %d ops exceeds the dev service limit of 200" % len(ops))
        t = time.time()
        r = self.call({"op": "multi", "ops": ops}, timeout=max(5, int(left)))
        self.calls += 1
        self.log.append({"call": self.calls, "ops": len(ops), "seconds": round(time.time() - t, 2), "ok": r.get("ok"), "error": r.get("error")})
        if r.get("error"):
            raise RuntimeError("dev call %d: %s" % (self.calls, r["error"]))
        for k, i in enumerate(idx):
            x = (r.get("results") or [{}] * len(ops))[k]
            out[i] = bytes.fromhex(x["hex"]) if x.get("ok") and x.get("hex") else None
        return out


def cstr(b):
    if b is None:
        return None
    e = b.find(b"\0")
    return b[:e if e >= 0 else len(b)].decode("latin1", errors="replace")


def td_check(tdb, want):
    """TypeInfoData header bytes (0x48) vs the type data: size, field count, signature, GUID"""
    if tdb is None or len(tdb) < 0x48:
        return False, "TypeInfoData unreadable"
    sz = struct.unpack_from("<H", tdb, 0xE)[0]
    nf, sig = struct.unpack_from("<HI", tdb, 0x32)
    g = str(uuid.UUID(bytes_le=bytes(tdb[0x10:0x20])))
    ok = sz == int(want["size"], 16) and nf == want["nf"] and sig == int(want["sig"], 16) and g == want["guid"]
    return ok, "size %s nf %d sig %s guid %s" % (hex(sz), nf, hex(sig), g)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mock", action="store_true")
    ap.add_argument("--bad", action="store_true", help="mock with one wrong TypeInfo")
    ap.add_argument("--budget", type=float, default=60.0)
    ap.add_argument("--layout", default=None, help="layout JSON (outside git); default %s (--mock: a synthetic layout)" % LAYOUT)
    ap.add_argument("--out", default=None)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    harness.require_live(a.mock, tool="chain_check")
    box = harness.timebox(a.budget + 30, "chain_check")
    try:
        return run(a)
    finally:
        box.cancel()


def run(a):
    if a.layout or not a.mock:
        lp = a.layout or LAYOUT
        if not os.path.exists(lp):
            print("layout %s not found (it is kept outside git: --layout / CHAIN_LAYOUT)" % lp)
            return harness.finish("INCONCLUSIVE", tool="chain_check")
        L = json.load(open(lp))
    else:
        L = synthetic_layout()
    types = L["types"]
    if a.mock:
        call = mock_call(L, bad=a.bad)
        out_path = a.out
    else:
        import dev2
        call = dev2.call
        os.makedirs(os.path.join(harness.TO, "chain_check"), exist_ok=True)
        out_path = a.out or os.path.join(harness.TO, "chain_check", "chain_check_%s.json" % time.strftime("%Y%m%d-%H%M%S"))
    dev = Dev(call, a.budget)
    res = {"tool": "tools/chain_check.py", "mock": a.mock, "bad": a.bad, "started": time.strftime("%Y-%m-%d %H:%M:%S"), "G": L["G"], "mgr": {},
           "dbs": {}, "groups": {}, "summary": None, "calls": dev.log}
    G = int(L["G"], 16)
    ti_of = {n: int(t["ti_live"], 16) for n, t in types.items()}
    db_ti = {"gameplay": ti_of["AttribDBRuntime_gameplayattribdb"], "prematch": ti_of["AttribDBRuntime_prematchattribdb"]}
    try:
        # R1 G -> mgr
        (gb,) = dev.multi([(G, 8)])
        mgr = u64(gb) if gb else 0
        res["mgr"]["addr"] = hx(mgr)
        if not is_heap_ptr(mgr):
            raise RuntimeError("[G] = %s is not a heap pointer (attributulator not loaded?)" % hx(mgr))
        # R2 the manager object
        (mb,) = dev.multi([(mgr, MGR_SIZE)])
        if mb is None:
            raise RuntimeError("manager at %s unreadable" % hx(mgr))
        A, h8, h10 = u64(mb, 0), u64(mb, 8), u64(mb, 0x10)
        res["mgr"].update({"A": hx(A), "slot8": hx(h8), "slot10": hx(h10), "db_index_i32": struct.unpack_from("<i", mb, 0x18)[0],
                           "cnt_0x100": struct.unpack_from("<I", mb, 0x100)[0], "list_0x110": hx(u64(mb, 0x110))})
        # every pointer-looking slot of the manager (for the prematch handle scan)
        slots = [(o, u64(mb, o)) for o in range(0, MGR_SIZE, 8) if is_heap_ptr(u64(mb, o))]
        # R3 handles: +8 of every pointer slot target (A at +0 proven: [A+8] = current DB)
        hb = dev.multi([(p, 0x10) for _, p in slots])
        cands = []  # (mgr slot, handle, db candidate)
        for (o, p), b in zip(slots, hb):
            if b is not None:
                q = u64(b, 8) & MASK
                if is_heap_ptr(q):
                    cands.append((o, p, q))
        cands = cands[:40]
        # R4 DB candidates: TI at +8, Name at +0x18, family slots (0x170 covers the gameplay DB)
        dbb = dev.multi([(q, 0x170) for _, _, q in cands])
        dbs = {}  # kind -> dict
        for (o, p, q), b in zip(cands, dbb):
            if b is None:
                continue
            ti = u64(b, 8)
            kind = next((k for k, v in db_ti.items() if v == ti), None)
            if kind and kind not in dbs:
                dbs[kind] = {"mgr_slot": hx(o), "handle": hx(p), "addr": hx(q), "TI": hx(ti), "block": b}
            elif kind is None and o in (0x10,):
                res["mgr"]["slot10_db_TI"] = hx(ti)
        res["mgr"]["pointer_slots"] = [hx(o) for o, _ in slots]
        res["mgr"]["prematch_handle_slot"] = dbs.get("prematch", {}).get("mgr_slot")
        res["mgr"]["gameplay_handle_slot"] = dbs.get("gameplay", {}).get("mgr_slot")
        # R5 group blocks of every group, DB Name strings, DB TI -> TD
        groups = L["groups"]
        gsize = {"AttribGroupRuntime_gp_positioning": 0x1C0, "AttribGroupRuntime_gp_positioningformation": 0x40, "AttribGroupRuntime_gp_customization": 0x20,
                 "AttribGroupRuntime_pm_teamtacticssettings": 0x20, "AttribGroupRuntime_pm_tacticalrole": 0x2E8}
        greads, gkeys = [], []
        for key, g in groups.items():
            d = dbs.get(g["db"])
            gaddr = (u64(d["block"], int(g["family_slot"], 16)) & MASK) if d else 0
            greads.append((gaddr, gsize[g["group_type"]]))
            gkeys.append((key, gaddr))
        dbkeys = list(dbs.keys())
        r5 = dev.multi(greads + [(u64(dbs[k]["block"], 0x18), 0x80) for k in dbkeys] + [(int(dbs[k]["TI"], 16), 8) for k in dbkeys])
        gblocks = dict(zip([k for k, _ in gkeys], r5[:len(greads)]))
        gaddrs = dict(gkeys)
        for i, k in enumerate(dbkeys):
            dbs[k]["name"] = cstr(r5[len(greads) + i])
            dbs[k]["TD"] = hx(u64(r5[len(greads) + len(dbkeys) + i])) if r5[len(greads) + len(dbkeys) + i] else None
        # R6 instance blocks, group TI -> TD, DB TD headers
        ireads, ikeys = [], []
        for key, g in groups.items():
            gb = gblocks.get(key)
            if gb is None:
                continue
            size = int(types[g["schema"]]["size"], 16)
            if g["group_slot"] is None:  # pm_tacticalrole: sample the first three role slots
                for s in (0x18, 0x20, 0x28):
                    ireads.append((u64(gb, s) & MASK, size))
                    ikeys.append((key, hx(s)))
            else:
                ireads.append((u64(gb, int(g["group_slot"], 16)) & MASK, size))
                ikeys.append((key, g["group_slot"]))
        gti = {key: u64(gblocks[key], 8) for key in gblocks if gblocks[key] is not None}
        r6 = dev.multi(ireads + [(gti[k], 8) for k in gti] + [(int(dbs[k]["TD"], 16), 0x48) for k in dbkeys if dbs[k]["TD"]])
        iblocks = r6[:len(ireads)]
        gtd = {k: (u64(x) if x else 0) for k, x in zip(gti, r6[len(ireads):len(ireads) + len(gti)])}
        dbtd = dict(zip([k for k in dbkeys if dbs[k]["TD"]], r6[len(ireads) + len(gti):]))
        # R7 instance Names, instance TI -> TD (one per TI), group TD headers
        inst = []  # per sampled instance
        for (key, slot), (addr, size), b in zip(ikeys, ireads, iblocks):
            inst.append({"group": key, "slot": slot, "addr": hx(addr), "block": b, "vtable": hx(u64(b, 0)) if b else None,
                         "TI": u64(b, 8) if b else 0, "name_ptr": u64(b, 0x18) if b else 0})
        tis = sorted({x["TI"] for x in inst if x["TI"]})
        r7 = dev.multi([(x["name_ptr"], 0x100) for x in inst] + [(t, 8) for t in tis] + [(gtd[k], 0x48) for k in gtd])
        for x, nb in zip(inst, r7[:len(inst)]):
            x["name"] = cstr(nb)
        titd = {t: (u64(b) if b else 0) for t, b in zip(tis, r7[len(inst):len(inst) + len(tis)])}
        gtdh = dict(zip(list(gtd.keys()), r7[len(inst) + len(tis):]))
        # R8 instance TD headers
        tdl = sorted({v for v in titd.values() if v})
        r8 = dev.multi([(t, 0x48) for t in tdl])
        tdh = dict(zip(tdl, r8))
        # ---- verdicts
        for k in dbkeys:
            ok, txt = td_check(dbtd.get(k), types["AttribDBRuntime_%sattribdb" % k])
            dbs[k]["td_check"] = txt
            dbs[k]["pass"] = ok and (dbs[k]["name"] or "").lower() == ("fifa/attribulator/gameplay/gameplayattribdb" if k == "gameplay" else "fifa/attribulator/prematch/prematchattribdb")
            res["dbs"][k] = {kk: vv for kk, vv in dbs[k].items() if kk != "block"}
        n_pass = n_fail = 0
        for key, g in groups.items():
            out = {"family_slot": g["family_slot"], "group_slot": g["group_slot"], "group_addr": hx(gaddrs.get(key, 0)), "instances": [], "pass": False, "why": []}
            if g["db"] not in dbs:
                out["why"].append("its database (%s) was not found from the manager" % g["db"])
            gb = gblocks.get(key)
            if gb is None:
                out["why"].append("group object unreadable")
            else:
                want_ti = ti_of[g["group_type"]]
                ok_ti = gti.get(key) == want_ti
                ok_td, td_txt = td_check(gtdh.get(key), types[g["group_type"]])
                out["group_TI"] = hx(gti.get(key, 0)); out["group_TI_expected"] = hx(want_ti); out["group_TI_tag"] = types[g["group_type"]]["ti_live_tag"]
                out["group_td_check"] = td_txt
                if not ok_td:
                    out["why"].append("group TypeInfoData mismatch (%s)" % td_txt)
                if not ok_ti:
                    out["why"].append("group TypeInfo %s != expected %s [%s]" % (out["group_TI"], out["group_TI_expected"], out["group_TI_tag"]))
            mine = [x for x in inst if x["group"] == key]
            for x in mine:
                st = types[g["schema"]]
                want_ti = ti_of[g["schema"]]
                e = {"slot": x["slot"], "addr": x["addr"], "vtable": x["vtable"], "TI": hx(x["TI"]), "TI_expected": hx(want_ti), "TI_tag": st["ti_live_tag"],
                     "name": x["name"], "knobs": {}}
                ok_td, td_txt = td_check(tdh.get(titd.get(x["TI"], 0)), st)
                e["td_check"] = td_txt
                whys = []
                if x["block"] is None:
                    whys.append("instance unreadable")
                else:
                    if not is_img_ptr(u64(x["block"], 0)):
                        whys.append("+0 is not an FC27.exe pointer")
                    if x["TI"] != want_ti:
                        whys.append("TypeInfo %s != expected %s [%s]" % (hx(x["TI"]), hx(want_ti), st["ti_live_tag"]))
                    if not ok_td:
                        whys.append("TypeInfoData mismatch (%s)" % td_txt)
                    want_name = g["instance_name"]
                    if want_name and (x["name"] or "").lower() != want_name.lower():
                        whys.append("Name %r != expected %r" % (x["name"], want_name))
                    van = g["vanilla"] or {}
                    changed = 0
                    for fname, foff, fty in st["fields"]:
                        if fty in PRIM and foff + PRIM[fty][1] <= len(x["block"]):
                            v = struct.unpack_from(PRIM[fty][0], x["block"], foff)[0]
                            if fty == "Boolean":
                                v = bool(v)
                            vv = van.get(fname)
                            same = None if vv is None else (abs(float(v) - float(vv)) < 1e-6 if fty != "Boolean" else bool(vv) == v)
                            e["knobs"][fname] = {"offset": hex(foff), "type": fty, "value": v, "fc27": vv, "verdict": "no vanilla" if same is None else ("FC27" if same else "changed")}
                            if same is False:
                                changed += 1
                    e["changed_count"] = changed
                e["pass"] = not whys
                e["why"] = whys
                out["instances"].append(e)
            out["pass"] = not out["why"] and bool(out["instances"]) and all(e["pass"] for e in out["instances"])
            if not out["instances"] and not out["why"]:
                out["why"].append("no instance read")
            res["groups"][key] = out
            n_pass += out["pass"]
            n_fail += not out["pass"]
            chg = sum(e.get("changed_count", 0) for e in out["instances"])
            print("%-4s %-52s inst %s  TI %s  changed %d  %s" % ("PASS" if out["pass"] else "FAIL", key,
                                                               ",".join(e["addr"] for e in out["instances"]) or "-",
                                                               ",".join(e["TI"] for e in out["instances"]) or "-", chg,
                                                               "; ".join(out["why"] + [w for e in out["instances"] for w in e["why"]])))
        secs = time.time() - dev.t0
        res["summary"] = "%d PASS / %d FAIL of %d groups; mgr %s gameplay handle %s prematch handle %s (db index %s, current DB %s); %d dev calls in %.1f s" % (
            n_pass, n_fail, len(groups), hx(mgr), res["mgr"].get("gameplay_handle_slot"), res["mgr"].get("prematch_handle_slot") or "NOT FOUND",
            res["mgr"]["db_index_i32"], (dbs.get("gameplay") or {}).get("name"), dev.calls, secs)
    except Exception as ex:
        res["error"] = str(ex)
        res["summary"] = "ABORT: %s after %d dev calls in %.1f s" % (ex, dev.calls, time.time() - dev.t0)
    print(res["summary"])
    for g in res["groups"].values():
        for e in g["instances"]:
            e.pop("block", None)
    if out_path:
        json.dump(res, open(out_path, "w"), indent=1, default=str)
        print("written", out_path)
    if res.get("error") is not None:
        return harness.finish("INCONCLUSIVE", tool="chain_check")
    return harness.finish("PASS" if res["groups"] and all(g["pass"] for g in res["groups"].values()) else "FAIL", tool="chain_check")


# ------------------------------------------------------------------------------------------------- mock (sweep T5 mock_dev2.py)
class Mem:
    """sparse synthetic memory (4 KiB pages) with a bump allocator on harness.MOCK_HEAP"""

    def __init__(self):
        self.pages = {}
        self.heap = harness.MOCK_HEAP

    def write(self, addr, data):
        for i, b in enumerate(data):
            a = addr + i
            self.pages.setdefault(a >> 12, bytearray(0x1000))[a & 0xFFF] = b

    def read(self, addr, n):
        out = bytearray()
        for i in range(n):
            a = addr + i
            pg = self.pages.get(a >> 12)
            if pg is None:
                return None
            out.append(pg[a & 0xFFF])
        return bytes(out)

    def alloc(self, n, align=16):
        self.heap = (self.heap + align - 1) // align * align
        a = self.heap
        self.heap += n + 0x40
        return a

    def q(self, addr, v):
        self.write(addr, struct.pack("<Q", v))

    def cstring(self, s):
        a = self.alloc(len(s) + 1, 1)
        self.write(a, s.encode("latin1") + b"\0")
        return a


GROUP_SIZE = {"AttribGroupRuntime_gp_positioning": 0x1C0, "AttribGroupRuntime_gp_positioningformation": 0x40,
              "AttribGroupRuntime_gp_customization": 0x20, "AttribGroupRuntime_pm_teamtacticssettings": 0x20,
              "AttribGroupRuntime_pm_tacticalrole": 0x2E8}
DB_NAMES = {"gameplay": "Fifa/Attribulator/Gameplay/gameplayattribdb", "prematch": "Fifa/Attribulator/PreMatch/prematchattribdb"}


def mock_call(L, bad=False):
    """a dev2-compatible read-only call() over a memory built from the layout: TypeInfo / TypeInfoData at the layout's addresses,
    DBs, groups (one tagged slot), instances with the layout's vanilla values, the manager and G. bad = the first group's instance
    gets a wrong TypeInfo (that group alone must FAIL)."""
    mem = Mem()
    types = L["types"]
    vt = {}
    for i, (n, t) in enumerate(sorted(types.items())):
        td, ti = int(t["td_live"], 16), int(t["ti_live"], 16)
        name = mem.cstring(n)
        hdr = bytearray(0x48)
        struct.pack_into("<Q", hdr, 0, name)
        struct.pack_into("<H", hdr, 0xE, int(t["size"], 16))
        hdr[0x10:0x20] = uuid.UUID(t["guid"]).bytes_le
        struct.pack_into("<HHI", hdr, 0x30, 8, t["nf"], int(t["sig"], 16))
        mem.write(td, bytes(hdr))
        mem.q(ti, td)
        vt[n] = int(t["vtable_live"], 16) if t.get("vtable_live") else IMG_LO + 0x100000 + 0x100 * i  # an image pointer

    def instance(schema, name, van):
        t = types[schema]
        a = mem.alloc(int(t["size"], 16))
        mem.q(a, vt[schema])
        mem.q(a + 8, int(t["ti_live"], 16))
        mem.q(a + 0x18, mem.cstring(name))
        for fname, foff, fty in t["fields"]:
            if fty in PRIM and van and fname in van:
                v = van[fname]
                mem.write(a + foff, struct.pack(PRIM[fty][0], int(bool(v)) if fty == "Boolean" else v))
        return a

    def obj(tname, size):
        a = mem.alloc(size)
        mem.q(a, vt.get(tname, IMG_LO + 0x100000))
        mem.q(a + 8, int(types[tname]["ti_live"], 16))
        return a

    dbs = {}
    for kind in ("gameplay", "prematch"):
        tname = "AttribDBRuntime_%sattribdb" % kind
        d = obj(tname, int(types[tname]["size"], 16))
        mem.q(d + 0x18, mem.cstring(DB_NAMES[kind]))
        dbs[kind] = d
    groups, first = {}, True
    for key, g in L["groups"].items():
        gt = g["group_type"]
        if gt not in groups:
            groups[gt] = obj(gt, GROUP_SIZE[gt])
            mem.q(dbs[g["db"]] + int(g["family_slot"], 16), groups[gt] | (4 if first else 0))  # one tagged slot
        if g["group_slot"] is None:  # a role table: three synthetic roles
            for i in range(3):
                nm = "Fifa/Attribulator/PreMatch/groups/pm_tacticalrole/role_%d" % i
                mem.q(groups[gt] + 0x18 + 8 * i, instance(g["schema"], nm, g.get("vanilla") or {}))
        else:
            a = instance(g["schema"], g["instance_name"], g["vanilla"])
            if bad and first:
                mem.q(a + 8, int(types[g["schema"]]["ti_live"], 16) + 0x10)  # a wrong TypeInfo
            mem.q(groups[gt] + int(g["group_slot"], 16), a)
        first = False
    mgr = mem.alloc(0x240)
    A = mem.alloc(0x20); mem.q(A + 8, dbs["gameplay"]); mem.q(mgr, A)  # noqa: E702
    H = mem.alloc(0x20); mem.q(H + 8, dbs["prematch"]); mem.q(mgr + 8, H)  # noqa: E702
    mem.write(mgr + 0x18, struct.pack("<i", 0))
    mem.q(int(L["G"], 16), mgr)

    def one(req):
        op = req.get("op")
        if op == "ping":
            return {"ok": True}
        if op in ("bytes", "read"):
            a = int(req["addr"], 16)
            n = max(1, min(int(req.get("len", 16)), 65536))
            b = mem.read(a, n)
            return {"ok": True, "addr": "0x%X" % a, "hex": b.hex()} if b is not None else {"ok": False, "error": "unreadable at 0x%X" % a}
        return {"ok": False, "error": "mock: op %s refused (read-only)" % op}

    def call(req, timeout=60, repost=0.7, allow_write=False):
        if req.get("op") == "multi":
            ops = req.get("ops") or []
            if len(ops) > 200:
                return {"ok": False, "error": "ops: a list of at most 200"}
            res = [one(o) for o in ops]
            return {"ok": all(r.get("ok") for r in res), "results": res}
        return one(req)

    call.mem = mem
    return call


def synthetic_layout():
    """a made-up layout with the real SHAPE (two DB kinds, two group types, two schemas) and synthetic TypeInfo / TypeInfoData
    addresses, sizes, signatures and GUIDs: no game data. Slot offsets and names are the documented ones of the knobs file."""
    def ty(i, size, nf, fields):
        return {"ti_live": hex(IMG_LO + 0x0A000000 + 0x100 * i), "td_live": hex(IMG_LO + 0x0B000000 + 0x100 * i), "size": hex(size),
                "nf": nf, "sig": hex(0x1000 + i), "guid": "00000000-0000-4000-8000-%012x" % i, "fields": fields, "ti_live_tag": "synthetic"}
    llw = [["lineWidthModifier", 0x28, "Float32"], ["lineLengthModifier", 0x2C, "Float32"], ["enabled", 0x30, "Boolean"]]
    tts = [["mentality", 0x28, "Int32"]]
    types = {"AttribDBRuntime_gameplayattribdb": ty(1, 0x170, 40, []), "AttribDBRuntime_prematchattribdb": ty(2, 0x70, 8, []),
             "AttribGroupRuntime_gp_positioningformation": ty(3, 0x40, 5, []),
             "AttribGroupRuntime_pm_teamtacticssettings": ty(4, 0x20, 1, []),
             "AttribSchema_gp_positioningformation_linelengthwidth": ty(5, 0x40, 3, llw),
             "AttribSchema_pm_teamtacticssettings": ty(6, 0x30, 1, tts)}
    groups = {
        "gp_positioningformation_attackinglinelengthwidth": {
            "db": "gameplay", "family_slot": "0x128", "group_type": "AttribGroupRuntime_gp_positioningformation", "group_slot": "0x28",
            "schema": "AttribSchema_gp_positioningformation_linelengthwidth",
            "instance_name": harness.KNOBS["width_in"]["name"],
            "vanilla": {"lineWidthModifier": 1.0, "lineLengthModifier": 1.0, "enabled": True}},
        "pm_teamtacticssettings": {
            "db": "prematch", "family_slot": "0x40", "group_type": "AttribGroupRuntime_pm_teamtacticssettings", "group_slot": "0x18",
            "schema": "AttribSchema_pm_teamtacticssettings",
            "instance_name": "Fifa/Attribulator/PreMatch/groups/pm_teamtacticssettings/teamtacticssettings", "vanilla": {"mentality": 2}},
    }
    return {"G": hex(IMG_LO + 0x0C000000), "types": types, "groups": groups}


def self_test():
    import contextlib, io, subprocess, tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    def go(*args):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(list(args))
        return rc, buf.getvalue()

    with tempfile.TemporaryDirectory() as d:
        rc, out = go("--mock", "--out", os.path.join(d, "good.json"))
        res = json.load(open(os.path.join(d, "good.json")))
        check(rc == 0 and "2 PASS / 0 FAIL" in out and "VERDICT PASS" in out, "synthetic layout, good memory: 2/2 groups PASS (exit %d)" % rc)
        check(len(res["calls"]) <= 8, "at most 8 dev calls (%d)" % len(res["calls"]))
        k = res["groups"]["gp_positioningformation_attackinglinelengthwidth"]["instances"][0]["knobs"]["lineWidthModifier"]
        check(k["verdict"] == "FC27" and abs(k["value"] - 1.0) < 1e-6, "width_in reads FC 27's 1.0, verdict FC27")
        check(res["mgr"]["prematch_handle_slot"] == "0x8", "prematch handle found at mgr+0x8")
        rc, out = go("--mock", "--bad")
        check(rc == 1 and "1 PASS / 1 FAIL" in out and "TypeInfo" in out, "one wrong TypeInfo: that group alone FAILs (exit %d)" % rc)
        L2 = synthetic_layout()
        L2["groups"]["gp_positioningformation_attackinglinelengthwidth"]["vanilla"]["lineWidthModifier"] = 0.6
        p = os.path.join(d, "l2.json")
        json.dump(L2, open(p, "w"))
        rc, out = go("--mock", "--layout", p, "--out", os.path.join(d, "l2out.json"))
        check(rc == 0 and "changed 0" in out, "the layout's own vanilla is what the mock holds: no change reported")
        st = os.path.join(d, "GAME_STATE.txt")
        open(st, "w").write("state: CLOSED (21:00) - offline\n")
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, TURBO_GAME_STATE=st))
        check(r.returncode == 3 and "refused" in r.stdout, "live while GAME_STATE is CLOSED: refused, exit 3")
        rc, out = go("--mock", "--budget", "1")
        check(rc == 3 and "budget" in out, "budget too small: aborted, INCONCLUSIVE (exit %d)" % rc)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
