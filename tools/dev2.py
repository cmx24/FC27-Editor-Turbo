"""dev2.py - Turbo dev-service client for scripts (coordinator sessions only; read-only unless allow_write=True).

call(req, timeout) -> result dict. Several processes may use it at once: a lock directory serialises the single request/result file pair
(TO\\turbo_dev_request.json / turbo_dev_result.json); a lock older than 180 s is stale. The service polls every 250 ms and runs each new
id once (brief C.7).

Safety (WP-C):
  - Write gate: any write / key op at ANY nesting (a dict "op", a dict sub-op of multi / seq, a string step "write ..." of ops / steps,
    nested multi in multi) needs allow_write=True. The 2026-10-13 client only looked at the top-level op and the STRING entries of
    ops / steps (brief B.9; sweep T6 3 of 10 cases bypassed it); has_write_op() is the sweep's recursive fix (T6\\dev2_gate_fixed.py).
  - GAME_STATE gate: call() refuses unless TOOLS\\GAME_STATE.txt names a live state (LAUNCHING / HUB / MATCH / MATCH-AB): agents never
    post requests while the game is CLOSED, and a missing state file fails closed.
  - Escape: a key op with vk 27 is refused even with allow_write (it tears the Claude session down, brief C.1).
Paths: TURBO_OUTPUT_DIR overrides TO\\ (default C:\\FC 27 Live Editor\\turbo_output); TURBO_GAME_STATE the state file.

CLI:  python dev2.py '{"op":"ping"}'      (prints the result JSON; exit 0 = ok, 1 = error)
      python dev2.py --self-test          offline: gate cases, GAME_STATE refusal (nothing is posted)
"""
import json, os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness  # noqa: E402

OUT = harness.TO
REQ = os.path.join(OUT, "turbo_dev_request.json")
RES = os.path.join(OUT, "turbo_dev_result.json")
LOCK = os.path.join(OUT, "turbo_dev_client.lock")
WRITE_OPS = {"write", "key"}
ESCAPE_VK = 27


def has_write_op(node, in_steps=False):
    """True when the request carries a write op at any nesting: a dict's "op" value, or a string entry of an "ops" / "steps" list
    (the dev service's string form "write <addr> <bytes>"). Other string values (a find pattern, a note) are never ops."""
    if isinstance(node, dict):
        op = node.get("op")
        if isinstance(op, str) and op.split()[:1] and op.split()[0] in WRITE_OPS:
            return True
        return any(has_write_op(v, k in ("ops", "steps")) for k, v in node.items())
    if isinstance(node, (list, tuple)):
        return any(has_write_op(v, in_steps) for v in node)
    if isinstance(node, str) and in_steps:
        w = node.split()
        return bool(w) and w[0] in WRITE_OPS
    return False


def has_escape(node, in_steps=False):
    """True when any key op (dict or string step) names VK 27 / escape"""
    if isinstance(node, dict):
        if node.get("op") == "key":
            vk = node.get("vk")
            try:
                if int(str(vk), 0) == ESCAPE_VK:
                    return True
            except ValueError:
                if str(vk).lower() in ("esc", "escape"):
                    return True
        return any(has_escape(v, k in ("ops", "steps")) for k, v in node.items())
    if isinstance(node, (list, tuple)):
        return any(has_escape(v, in_steps) for v in node)
    if isinstance(node, str) and in_steps:
        w = node.lower().split()
        if len(w) >= 2 and w[0] == "key":
            try:
                return int(w[1], 0) == ESCAPE_VK
            except ValueError:
                return w[1] in ("esc", "escape")
    return False


def _acquire(deadline):
    while time.time() < deadline:
        try:
            os.mkdir(LOCK)
            return True
        except FileExistsError:
            try:
                if time.time() - os.path.getmtime(LOCK) > 180:
                    os.rmdir(LOCK)
                    continue
            except OSError:
                pass
            time.sleep(0.1)
    return False


def call(req, timeout=60, repost=0.7, allow_write=False):
    if has_escape(req):
        return {"error": "dev2: Escape (VK 27) is never sent"}
    if not allow_write and has_write_op(req):
        return {"error": "dev2: write/key ops need allow_write=True (coordinator only)"}
    tok, line = harness.read_game_state()
    if tok not in harness.LIVE_TOKENS:
        return {"error": "dev2: GAME_STATE is %s (%r): no request posted" % (tok or "missing", line)}
    deadline = time.time() + timeout
    if not _acquire(deadline):
        return {"error": "dev2: lock busy (another client)"}
    try:
        rid = int(time.time() * 1000) % 2000000000
        req = dict(req, id=rid)
        next_post = 0
        while time.time() < deadline:
            if time.time() >= next_post:
                try:
                    tmp = REQ + ".tmp%d" % os.getpid()
                    with open(tmp, "w") as f:
                        json.dump(req, f)
                    os.replace(tmp, REQ)
                    next_post = time.time() + repost
                except PermissionError:
                    next_post = time.time() + 0.3
            try:
                with open(RES) as f:
                    r = json.load(f)
                if r.get("id") == rid:
                    return r
            except Exception:
                pass
            time.sleep(0.15)
        return {"error": "timeout"}
    finally:
        try:
            os.rmdir(LOCK)
        except OSError:
            pass


# ------------------------------------------------------------------------------------------------------------------- self-test
GATE_CASES = [  # (request, must be blocked without allow_write?) - the sweep's T6 cases + Escape
    ({"op": "ping"}, False),
    ({"op": "bytes", "addr": "0x1000", "len": 4}, False),
    ({"op": "write", "addr": "0x1000", "hex": "00 00 80 3F"}, True),
    ({"op": "key", "vk": 107}, True),
    ({"op": "multi", "ops": ["bytes 0x10 4", "write 0x10 00"]}, True),
    ({"op": "multi", "ops": [{"op": "write", "addr": "0x10", "hex": "00"}]}, True),
    ({"op": "seq", "steps": [{"op": "bytes", "addr": "0x10", "len": 4}, {"op": "key", "vk": 107}]}, True),
    ({"op": "multi", "ops": [{"op": "multi", "ops": [{"op": "write", "addr": "0x10", "hex": "00"}]}]}, True),
    ({"op": "seq", "steps": [{"op": "bytes", "addr": "0x10", "len": 4}]}, False),
    ({"op": "find", "pattern": "write 48 8B", "note": "the word write inside a value is not an op"}, False),
    ({"op": "seq", "steps": [["write 0x10 00"]]}, True),
]
ESC_CASES = [  # built from ESCAPE_VK so that no literal Escape send exists anywhere in tools/ (test_tools_guard.py greps for it)
    ({"op": "key", "vk": ESCAPE_VK}, True), ({"op": "key", "vk": hex(ESCAPE_VK)}, True), ({"op": "key", "vk": "ESC".lower() + "ape"}, True),
    ({"op": "seq", "steps": [{"op": "key", "vk": 13}, {"op": "key", "vk": ESCAPE_VK}]}, True),
    ({"op": "multi", "ops": ["key %d" % ESCAPE_VK]}, True),
    ({"op": "key", "vk": 107}, False), ({"op": "bytes", "addr": hex(ESCAPE_VK), "len": ESCAPE_VK}, False),
]


def self_test():
    import tempfile
    global OUT, REQ, RES, LOCK
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    for req, block in GATE_CASES:
        check(has_write_op(req) == block, "write gate %s: %s" % ("blocks" if block else "passes", json.dumps(req)))
    for req, esc in ESC_CASES:
        check(has_escape(req) == esc, "escape gate %s: %s" % ("refuses" if esc else "passes", json.dumps(req)))
    saved = (OUT, REQ, RES, LOCK, harness.GAME_STATE)
    with tempfile.TemporaryDirectory() as d:
        OUT, REQ, RES, LOCK = d, os.path.join(d, "req.json"), os.path.join(d, "res.json"), os.path.join(d, "lock")
        st = os.path.join(d, "GAME_STATE.txt")
        harness.GAME_STATE = st
        try:
            r = call({"op": "ping"}, timeout=1)
            check("GAME_STATE is missing" in r.get("error", ""), "missing GAME_STATE: refused (%s)" % r.get("error"))
            for text, token in (("state: CLOSED (19:41) - done.\n", "CLOSED"), ("garbage\n", "missing")):
                with open(st, "w") as f:
                    f.write(text)
                r = call({"op": "ping"}, timeout=1)
                check(("GAME_STATE is %s" % token) in r.get("error", ""), "GAME_STATE %r: refused" % text.strip())
            check(not os.path.exists(REQ), "no request file was posted while refused")
            r = call({"op": "multi", "ops": [{"op": "write", "addr": "0x10", "hex": "00"}]}, timeout=1)
            check("allow_write" in r.get("error", ""), "nested write refused before any state check")
            r = call({"op": "key", "vk": ESCAPE_VK}, timeout=1, allow_write=True)
            check("Escape" in r.get("error", ""), "Escape refused even with allow_write")
            with open(st, "w") as f:
                f.write("state: MATCH-AB (21:00) - coordinator\n")
            tok, _ = harness.read_game_state()
            check(tok == "MATCH-AB", "GAME_STATE token parsed: %s" % tok)
            r = call({"op": "ping"}, timeout=0.6, repost=0.2)  # live state, nobody answers: posts, then times out
            check(r.get("error") == "timeout" and os.path.exists(REQ) and not os.path.exists(LOCK),
                  "live state: request posted, timeout reported, lock released")
        finally:
            OUT, REQ, RES, LOCK, harness.GAME_STATE = saved
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:2] == ["--self-test"]:
        sys.exit(self_test())
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    res = call(json.loads(sys.argv[1]))
    print(json.dumps(res)[:4000])
    sys.exit(0 if not res.get("error") else 1)
