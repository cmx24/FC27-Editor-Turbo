#!/usr/bin/env python
"""x2_readout.py - READ-ONLY readout of the match-setup team tactic at kick-off (gate X2, decision 4.2). From the sweep's
T2/kickoff_readout.py (2026-10-13): decode, signatures and checks unchanged; WP-C adds the GAME_STATE gate, the time box, paths
through harness.py, the X2 branch of table 4.2, and a synthetic --mock / --self-test.

  python x2_readout.py [-v] [--team N] [--map-lh 95 --map-bu 1 --map-da 0] [--teams-lh 77 --teams-bu 0] [--budget 90]
Reads, through the dev service (multi/bytes, modules, find; never write/key):
  G (per-side setup global)  -> P = [G] -> S_side = [P + 8 + side*8]  (side 0, 1)
  S_side + 0x10C             team id of the side
  S_side + 0x2F0A8 + i*0x194 tactic slot D_i (5 slots): +0x1C ffn, +0x20/+0x24 sf, +0x28 team, +0x2C audio,
                             +0x30 11 positions, +0x118 11 role words, +0x168 11 pids,
                             +0x150 style struct: +0x158 Line Height (u8), +0x15B Build-Up style (u8, engine enum),
                             +0x15C Defensive Approach (u8: 0 Deep, 1 Balanced, 2 High, 3 Aggressive)
OLD-BUILD (6AB9813C) layout decoded statically (sweep T2); on the live build it is confirmed by code signatures that EMBED the offsets,
two independent rip-resolved votes for G, and identity checks on the data. Anything not confirmed is printed with [A].
X2 branch (the user's club = --team, default Turbo's log line "channel E: team N record"):
  LH == the map-record sentinel (95)  -> map record is the channel (design default; DA 0 = STORED, DA band(LH) = DERIVED)
  LH == the teams.* sentinel (77)     -> teams.* is the channel
  else                                -> neither / undecodable: WP-B3 contingency
Exit 0 PASS (both sides decode and pass the checks; the branch is printed), 1 FAIL (decode / identity), 3 INCONCLUSIVE (refused,
dev service, budget). Signature cache: TO\\x2_kickoff_layout.json.
"""
import argparse, json, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402
import sheet_copies as sc  # noqa: E402

TOOL = "x2_readout"
CACHE = os.path.join(harness.TO, "x2_kickoff_layout.json")
MM_CACHE = os.path.join(harness.TO, "match_measure_layout.json")
DEV = None  # the dev call (dev2.call or a mock)

# ---- OLD-BUILD constants (6AB9813C), each one confirmed live by a signature or a data check, else printed [A]
S_TEAMID = 0x10C          # S + 0x10C team id            (embedded in SIG_BUILDER)
S_DBLOCK = 0x2F0A8        # S + 0x2F0A8 tactic slots      (embedded in SIG_BUILDER)
D_STRIDE = 0x194          # 5 slots (embedded in SIG_UPDATER as 0x2F890 = end of the block)
D_SLOTS = 5
D_STYLE = 0x150           # (embedded in SIG_STYLE as 0x2F1F8)
D_LH, D_BU, D_DA = 0x158, 0x15B, 0x15C   # (0x2F203 / 0x2F204 embedded in SIG_STYLE for BU / DA)
D_FFN, D_SF1, D_SF2, D_TEAM, D_AUDIO, D_POS, D_ROLE, D_PID = 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x118, 0x168
DA_LABEL = {0: "Deep", 1: "Balanced", 2: "High", 3: "Aggressive"}   # bands of the depth converter (<=30, <=60, <=90, >90)

# dev "find" syntax. Each pattern is the OLD build's bytes with rel32 displacements wildcarded.
SIGS = {
    # builder: mov rax,[rip+G]; mov rdx,[rax+rbx*8+8]; ...; mov r12d,[rdx+0x10C]; lea rsi,[rdx+0x2F0A8]
    "SIG_BUILDER": ("48 8B 05 ?? ?? ?? ?? 48 8B 54 D8 08 EB 03 48 8B D7 44 8B A2 0C 01 00 00 48 8D B2 A8 F0 02 00", 3),
    # style consumer: movsxd rax,[rcx+0x18F98]; ...; mov rax,[rip+G]; mov rbx,[rax+rcx*8+8]; ...; lea rax,[rbx+0x2F1F8]
    "SIG_STYLE": ("48 63 81 98 8F 01 00 48 8B FA 83 F8 01 77 ?? 48 8B C8 48 8B 05 ?? ?? ?? ?? 48 8B 5C C8 08 48 85 DB 74 ?? "
                  "48 8D 83 F8 F1 02 00", 0x15),
    # post-build step: cmp cl,2; jae; mov rax,[rip+G]; movzx ecx,cl; mov rsi,[rax+rcx*8+8]; ...; add rsi,0x2F890
    "SIG_UPDATER": ("80 F9 02 73 11 48 8B 05 ?? ?? ?? ?? 0F B6 C9 48 8B 74 C8 08 EB 03 48 8B F3", 8),
}
STYLE_TAIL = ("0F B6 93 03 F2 02 00", "0F B6 93 04 F2 02 00")   # movzx edx, byte [rbx+0x2F203] / [rbx+0x2F204]
UPDATER_END = b"\x48\x81\xc6\x90\xf8\x02\x00"                   # add rsi, 0x2F890


class Budget:
    def __init__(self, s):
        self.t0, self.s = time.time(), s

    def left(self):
        return self.s - (time.time() - self.t0)

    def need(self, n, what):
        if self.left() < n:
            raise RuntimeError("time budget: %.0f s left, %s needs %d s" % (self.left(), what, n))


def H(x):
    return "0x%X" % x


def call(req, timeout):
    r = DEV(req, timeout=timeout)
    if not isinstance(r, dict) or r.get("error"):
        raise RuntimeError("dev2 %s: %s" % (req.get("op"), (r or {}).get("error", r)))
    return r


def rd_many(pairs, timeout=20):
    out = []
    for i in range(0, len(pairs), 100):
        r = call({"op": "multi", "ops": [{"op": "bytes", "addr": H(a), "len": n} for a, n in pairs[i:i + 100]]}, timeout)
        for x in r.get("results", []):
            out.append(bytes.fromhex(x["hex"]) if x.get("ok") else None)
    return out


def rd(a, n, timeout=20):
    return rd_many([(a, n)], timeout)[0]


def u64(b, o=0):
    return struct.unpack_from("<Q", b, o)[0]


def i32(b, o=0):
    return struct.unpack_from("<i", b, o)[0]


def ptr_ok(v):
    return 0x10000 <= v < 0x7FFFFFFFFFFF


def pat_bytes(p):
    return [None if t == "??" else int(t, 16) for t in p.split()]


def pat_match(p, data):
    toks = pat_bytes(p)
    return len(data) >= len(toks) and all(t is None or t == data[i] for i, t in enumerate(toks))


band = sc.band


def decode(args, B):
    """the sweep's readout: -> (results {side: {...}}, verdict reasons). Prints as it goes."""
    notes, verdict = [], []
    B.need(5, "modules")
    r = call({"op": "modules"}, 15)
    base = size = None
    for m in r.get("modules", []):
        if m["name"].lower() == "fc27.exe":
            base, size = int(str(m["base"]), 16), int(m["size"])
    if base is None:
        return None, ["FC27.exe not in the module list"]
    hdr = rd(base, 0x400)
    e = struct.unpack_from("<I", hdr, 0x3C)[0]
    key = "%08X-%08X" % (struct.unpack_from("<I", hdr, e + 8)[0], size)
    print("build %s base %s size %#x" % (key, H(base), size))
    cache = {}
    if not args.no_cache:
        try:
            cache = json.load(open(args.cache)).get(key, {})
        except Exception:
            cache = {}
    sig_hits = {}
    for name, (pat, rip_off) in SIGS.items():
        if cache.get(name) and cache[name].get("at"):
            b = rd(cache[name]["at"], 0x70)
            if b and pat_match(pat, b):
                sig_hits[name] = cache[name]
                continue
            print("  sig %-12s cached hit %s no longer matches: rescanning" % (name, H(cache[name]["at"])))
        B.need(22, "find " + name)
        t0 = time.time()
        r = call({"op": "find", "pattern": pat, "max": 4, "from": H(base), "to": H(base + size)}, 20)
        hits = [int(h, 16) for h in r.get("hits", [])]
        print("  sig %-12s %d hit(s) %.1fs %s" % (name, len(hits), time.time() - t0, " ".join(H(h) for h in hits[:4])))
        if len(hits) == 1:
            h = hits[0]
            b = rd(h, 0x70)
            g = h + rip_off + 4 + struct.unpack_from("<i", b, rip_off)[0]
            sig_hits[name] = {"at": h, "G": g, "bytes": b.hex()}
        else:
            sig_hits[name] = {"at": None, "G": None, "hits": len(hits)}
    try:
        allc = json.load(open(args.cache)) if os.path.exists(args.cache) else {}
        allc[key] = sig_hits
        json.dump(allc, open(args.cache, "w"), indent=1)
    except Exception as ex:
        notes.append("cache not written: %s" % ex)
    votes = sorted(set(v["G"] for v in sig_hits.values() if v.get("G")))
    if not votes:
        return None, ["no signature found G (layout changed on this build); hits: %s" % {k: v.get("hits") for k, v in sig_hits.items()}]
    if len(votes) > 1:
        return None, ["signatures disagree on G: %s" % [H(v) for v in votes]]
    G = votes[0]
    found = [k for k, v in sig_hits.items() if v.get("G")]
    print("G = %s (rva %#x) votes: %s" % (H(G), G - base, ", ".join(found)))
    conf = {"S+0x10C/S+0x2F0A8": "SIG_BUILDER" in found, "D+0x150": "SIG_STYLE" in found, "D stride (0x2F890 end)": False,
            "D+0x15B/+0x15C": False}
    if "SIG_STYLE" in found:
        tail = bytes.fromhex(sig_hits["SIG_STYLE"]["bytes"])[len(pat_bytes(SIGS["SIG_STYLE"][0])):]
        conf["D+0x15B/+0x15C"] = all(any(pat_match(t, tail[i:]) for i in range(len(tail) - 7)) for t in STYLE_TAIL)
    if "SIG_UPDATER" in found:
        conf["D stride (0x2F890 end)"] = UPDATER_END in bytes.fromhex(sig_hits["SIG_UPDATER"]["bytes"])
    for k, v in conf.items():
        print("  offset check %-26s %s" % (k, "[V live]" if v else "[A] (not confirmed by code on this build)"))
    try:
        mm = json.load(open(args.mm_cache)).get(key)
    except Exception:
        mm = None
    if mm and mm.get("c_G1_rva"):
        Mv = u64(rd(base + mm["c_G1_rva"], 8))
        print("route C: G1 value %s -> %s" % (H(Mv), "a gameplay match object exists" if ptr_ok(Mv) else "no match object yet"))
    else:
        print("route C: no match_measure layout cache for this build; match presence [U]")
    print("TeamState and MatchState: [U] not chained (sweep T2 s.4)")
    B.need(10, "data reads")
    P = u64(rd(G, 8))
    if not ptr_ok(P):
        return None, ["[G] = %s is not a pointer (no match set up yet?)" % H(P)]
    pb = rd(P, 0x18)
    S = [u64(pb, 8), u64(pb, 0x10)]
    print("P = %s  S0 = %s  S1 = %s" % (H(P), H(S[0]), H(S[1])))
    results = {}
    for side in (0, 1):
        s = S[side]
        if not ptr_ok(s):
            verdict.append("side %d: S pointer %s invalid" % (side, H(s)))
            continue
        head, blk = rd_many([(s + S_TEAMID, 4), (s + S_DBLOCK, D_STRIDE * D_SLOTS)])
        if head is None or blk is None:
            verdict.append("side %d: S unreadable" % side)
            continue
        team = i32(head)
        print("side %d  S %s  team id (S+0x10C) = %d" % (side, H(s), team))
        side_ok = True
        for i in range(D_SLOTS):
            d = blk[i * D_STRIDE:(i + 1) * D_STRIDE]
            dteam, ffn, sf1, sf2, audio = i32(d, D_TEAM), i32(d, D_FFN), i32(d, D_SF1), i32(d, D_SF2), i32(d, D_AUDIO)
            pos = struct.unpack_from("<11i", d, D_POS)
            roles = struct.unpack_from("<11i", d, D_ROLE)
            pids = struct.unpack_from("<11i", d, D_PID)
            st = d[D_STYLE:D_STYLE + 0x16]
            lh, bu, da = d[D_LH], d[D_BU], d[D_DA]
            chk = {
                "team==S+0x10C": dteam == team and 0 < team < 1000000,
                "positions": pos[0] == 0 and all(0 <= p <= 27 for p in pos),
                "pids": all(p > 0 for p in pids) and len(set(pids)) == 11,
                "roles": all(0 < r_ < 65536 and ((r_ >> 6) & 0x3F) < 64 for r_ in roles),
                "style range": 1 <= lh <= 100 and bu <= 3 and da <= 3,
            }
            soft = {"+0x159/+0x15A == 2/2": st[9] == 2 and st[10] == 2, "DA == band(LH)": da == band(lh)}
            ok = all(chk.values())
            if i == 0:
                side_ok = ok
                results[side] = {"team": team, "LH": lh, "BU": bu, "DA": da, "ffn": ffn, "ok": ok, "D": s + S_DBLOCK}
            tag = "[V identity]" if ok else "[A]"
            line = ("  D_%d @%s team %d ffn %d sf %d/%d audio %d | LH %d  BU %d (engine enum)  DA %d %s | %s" % (
                i, H(s + S_DBLOCK + i * D_STRIDE), dteam, ffn, sf1, sf2, audio, lh, bu, da, DA_LABEL.get(da, "?"), tag))
            if i == 0 or args.v or ok:
                print(line)
            if i == 0 or args.v:
                print("     style bytes +0x150: %s" % st.hex(" "))
                print("     checks %s  soft %s" % ({k: int(v) for k, v in chk.items()}, {k: int(v) for k, v in soft.items()}))
        if not side_ok:
            verdict.append("side %d: D_0 failed an identity check" % side)
    for n in notes:
        print("note:", n)
    return results, verdict


def x2_branch(results, a):
    """table 4.2 for the user's club -> (branch line, notes) or (None, reason)"""
    user = [sd for sd, r in results.items() if r["team"] == a.team]
    if not user:
        return None, "the club %s is on neither side (sides: %s)" % (a.team, [r["team"] for r in results.values()])
    r = results[user[0]]
    lh, bu, da = r["LH"], r["BU"], r["DA"]
    if lh == a.map_lh:
        da_txt = ("DA STORED (the record's +0xB4 value %d reaches the match)" % da if da == a.map_da else
                  "DA DERIVED (band of LH = %s)" % DA_LABEL[band(lh)] if da == band(lh) else "DA %d: neither stored nor derived" % da)
        return ("map record is the channel (design default): WP-B1 as written, teams.* writes removed; %s; BU %d (%s)" % (
            da_txt, bu, "the record's" if bu == a.map_bu else "not the record's %d" % a.map_bu)), None
    if lh == a.teams_lh:
        return ("teams.* is the channel: the Tactics tab's teams.* write joins WP-B1's job (same gates); BU %d (%s)" % (
            bu, "teams.buildupplay" if bu == a.teams_bu else "not teams.buildupplay %d" % a.teams_bu)), None
    return ("neither / undecodable (LH %d is neither %d nor %d): WP-B3 contingency (MCOPY post-hook)" % (lh, a.map_lh, a.teams_lh)), None


def main(argv=None):
    global DEV
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--budget", type=float, default=90.0)
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--cache", default=CACHE)
    ap.add_argument("--mm-cache", default=MM_CACHE)
    ap.add_argument("--team", type=int, default=None, help="the user's club (default: Turbo's log line 'channel E: team N record')")
    ap.add_argument("--log", default=sc.LOG)
    ap.add_argument("--map-lh", type=int, default=95)
    ap.add_argument("--map-bu", type=int, default=1)
    ap.add_argument("--map-da", type=int, default=0)
    ap.add_argument("--teams-lh", type=int, default=77)
    ap.add_argument("--teams-bu", type=int, default=0)
    ap.add_argument("--mock", nargs="?", const=os.environ.get("TURBO_MOCK_MEM") or "", default=None, metavar="MEMFILE")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    harness.require_mock_file(a.mock, tool=TOOL)
    harness.require_live(a.mock is not None, tool=TOOL)
    box = harness.timebox(a.budget + 20, TOOL)
    try:
        if a.mock:
            DEV = harness.mock_call(a.mock)
        else:
            import dev2
            DEV = dev2.call
        if a.team is None:
            try:
                a.team = sc.from_log(a.log, a.mock or None)[1]
            except harness.StaleLog as e:
                print("club from the log refused: %s" % e)
        B = Budget(a.budget)
        results, verdict = decode(a, B)
        if results is None or len(results) != 2 or verdict:
            return harness.finish("FAIL", verdict or ["incomplete"], tool=TOOL)
        print("READOUT side0 team %d: LH %d BU %d DA %d (%s) | side1 team %d: LH %d BU %d DA %d (%s)   (%.1f s)" % (
            results[0]["team"], results[0]["LH"], results[0]["BU"], results[0]["DA"], DA_LABEL.get(results[0]["DA"], "?"),
            results[1]["team"], results[1]["LH"], results[1]["BU"], results[1]["DA"], DA_LABEL.get(results[1]["DA"], "?"),
            time.time() - B.t0))
        if a.team is None:
            print("X2 BRANCH: [U] the club is unknown (no 'channel E: team N record' log line; pass --team)")
        else:
            br, why = x2_branch(results, a)
            print("X2 BRANCH: %s" % (br or why))
        return harness.finish("PASS", tool=TOOL)
    except RuntimeError as ex:
        return harness.finish("INCONCLUSIVE", [str(ex)], tool=TOOL)
    finally:
        box.cancel()


# ------------------------------------------------------------------------------------------------------------------ mock
def mock_world(path, user=(95, 1, 0), other=(50, 0, 1), user_team=sc.MOCK_TEAM, other_team=8, break_sig=None, bad_pid=False):
    """synthetic image (the three signatures resolving to a fake global G, inside FC27.exe's range) + P + two sides with their D
    blocks: side 0 = the user's club with (LH, BU, DA) = user. No game data: every address, team id and pid is made up."""
    base, size = harness.IMAGE_BASE, harness.BUILDS["6AC07E31"]["size"]
    code = base + 0x01000000
    G = base + 0x0C100000
    blob = bytearray(b"\xCC" * 0x400)
    for k, (name, (pat, rip_off)) in enumerate(SIGS.items()):
        at = code + 0x100 * k
        bs = bytes(0x90 if t is None else t for t in pat_bytes(pat))
        if name == break_sig:
            bs = b"\x90" + bs[1:]
        blob[0x100 * k:0x100 * k + len(bs)] = bs
        struct.pack_into("<i", blob, 0x100 * k + rip_off, G - (at + rip_off + 4))
        tail = 0x100 * k + len(bs)
        if name == "SIG_STYLE":
            blob[tail:tail + 7] = bytes.fromhex(STYLE_TAIL[0].replace(" ", ""))
            blob[tail + 7:tail + 14] = bytes.fromhex(STYLE_TAIL[1].replace(" ", ""))
        if name == "SIG_UPDATER":
            blob[tail:tail + 7] = UPDATER_END
    P = harness.MOCK_HEAP + 0x300000
    S = [harness.MOCK_HEAP + 0x400000, harness.MOCK_HEAP + 0x500000]
    regions = [(base, harness.pe_header(0x6AC07E31, size)), (code, bytes(blob)), (G, struct.pack("<Q", P)),
               (P, struct.pack("<QQQ", 0, S[0], S[1]))]
    for side, (team, sty) in enumerate(((user_team, user), (other_team, other))):
        regions.append((S[side] + S_TEAMID, struct.pack("<i", team)))
        d = bytearray(D_STRIDE * D_SLOTS)
        struct.pack_into("<i", d, D_TEAM, team)
        struct.pack_into("<11i", d, D_POS, 0, *range(1, 11))
        struct.pack_into("<11i", d, D_ROLE, *[(k + 1) | (3 << 6) for k in range(11)])
        pids = [100 * (side + 1) + k for k in range(11)]
        if bad_pid and side == 1:
            pids[3] = pids[4]
        struct.pack_into("<11i", d, D_PID, *pids)
        d[D_LH], d[D_STYLE + 9], d[D_STYLE + 10], d[D_BU], d[D_DA] = sty[0], 2, 2, sty[1], sty[2]
        regions.append((S[side] + S_DBLOCK, bytes(d)))
    mods = [{"name": "FC27.exe", "base": "0x%X" % base, "size": size}]
    return harness.MockMem.create(path, regions, mods, kv={"fc27_pid": harness.MOCK_PID})


def self_test():
    import contextlib, io, subprocess, tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    with tempfile.TemporaryDirectory() as d:
        def go(world, *args):
            p = os.path.join(d, "m%d.json" % len(os.listdir(d)))
            mock_world(p, **world)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = main(["--mock", p, "--cache", os.path.join(d, "cache.json"), "--mm-cache", os.path.join(d, "none.json"),
                           "--team", str(sc.MOCK_TEAM), "--no-cache"] + list(args))
            return rc, buf.getvalue()

        rc, out = go({})
        check(rc == 0 and "map record is the channel" in out and "DA STORED" in out and "[V live]" in out,
              "match copy LH 95 / DA 0: X2 BRANCH map record, DA stored; offsets confirmed by the signatures (exit %d)" % rc)
        rc, out = go({"user": (95, 1, 3)})
        check(rc == 0 and "DA DERIVED" in out, "LH 95 with DA 3 (Aggressive): DA derived from LH")
        rc, out = go({"user": (77, 0, 2)})
        check(rc == 0 and "teams.* is the channel" in out, "LH 77: X2 BRANCH teams.*")
        rc, out = go({"user": (25, 0, 0)})
        check(rc == 0 and "neither / undecodable" in out and "WP-B3" in out, "LH 25 (neither sentinel): WP-B3 contingency")
        rc, out = go({"user_team": 9})
        check(rc == 0 and "on neither side" in out, "the club on neither side: said so")
        rc, out = go({"break_sig": "SIG_STYLE"})
        check(rc == 0 and "D+0x150" in out and "[A] (not confirmed" in out, "one signature missing: the readout still decodes, offsets [A]")
        rc, out = go({"bad_pid": True})
        check(rc == 1 and "side 1: D_0 failed an identity check" in out, "a side failing an identity check (duplicate pid): FAIL")
        st = os.path.join(d, "GAME_STATE.txt")
        open(st, "w").write("state: CLOSED (21:00) - offline\n")
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, TURBO_GAME_STATE=st))
        check(r.returncode == 3 and "refused" in r.stdout, "live while GAME_STATE is CLOSED: refused, exit 3")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
