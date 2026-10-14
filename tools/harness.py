"""harness.py - shared plumbing of the Turbo proof harness (tools/, WP-C of docs/TACTICS_OVERHAUL_DECISION.md 3.3).

Every check script in tools/:
  - is time-boxed (timebox(): a watchdog prints INCONCLUSIVE and exits 3 when the box is exceeded),
  - ends with one line "VERDICT PASS|FAIL|INCONCLUSIVE" and exits 0 / 1 / 3 (finish()),
  - refuses to talk to the game while TOOLS\\GAME_STATE.txt says CLOSED (or is missing) unless --mock (require_live()),
  - is read-only unless its name says write; write tools refuse without --allow-write.
Paths default to the documented locations (LE\\ = C:\\FC 27 Live Editor) and are overridable through the environment:
  FC27_LE_DIR, TURBO_OUTPUT_DIR (TO\\), TURBO_TOOLS_DIR (TOOLS\\, outside git: GAME_STATE.txt, session plans), TURBO_GAME_STATE.

--mock: MockMem is a file-backed fake of the dev service (ping / modules / bytes / multi / write) so several processes of one offline
run (x1_write.py, match_measure.py) share one synthetic "game memory". Nothing in it is game data: the instance, its name and the type
pointer are the documented build constants of the X1 knob plus synthetic heap addresses (MOCK_HEAP).
"""
import json, os, struct, sys, threading, time

LE = os.environ.get("FC27_LE_DIR", r"C:\FC 27 Live Editor")
TO = os.environ.get("TURBO_OUTPUT_DIR", os.path.join(LE, "turbo_output"))
TOOLS_DIR = os.environ.get("TURBO_TOOLS_DIR", os.path.join(LE, "turbo_dev", "tools"))
EV = os.environ.get("TURBO_EVIDENCE_DIR", os.path.join(LE, "turbo_dev", "evidence"))
GAME_STATE = os.environ.get("TURBO_GAME_STATE", os.path.join(TOOLS_DIR, "GAME_STATE.txt"))

PASS, FAIL, INCONCLUSIVE = 0, 1, 3
EXIT = {"PASS": PASS, "FAIL": FAIL, "INCONCLUSIVE": INCONCLUSIVE}

# GAME_STATE tokens (brief C.7). Live tools run only when the first line names one of these.
LIVE_TOKENS = ("LAUNCHING", "HUB", "MATCH", "MATCH-AB")


def read_game_state(path=None):
    """-> (token or None, first line). Token = the word after 'state:' (CLOSED / LAUNCHING / HUB / MATCH / MATCH-AB)."""
    path = path or GAME_STATE
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            line = f.readline().strip()
    except OSError:
        return None, ""
    low = line.lower()
    if not low.startswith("state:"):
        return None, line
    words = line[len("state:"):].split()
    return (words[0].upper() if words else None), line


def require_live(mock, path=None, tool=""):
    """Refuse (exit 3, INCONCLUSIVE) unless --mock or GAME_STATE names a live state. Returns the token."""
    if mock:
        return "MOCK"
    tok, line = read_game_state(path)
    if tok not in LIVE_TOKENS:
        why = "missing or unreadable" if tok is None else "says %s" % tok
        print("%s: refused - %s %s (line: %r). Live tools run only in a coordinator session (brief C.7); "
              "use --mock for the offline proof." % (tool or os.path.basename(sys.argv[0]), path or GAME_STATE, why, line))
        print("VERDICT INCONCLUSIVE")
        sys.stdout.flush()
        sys.exit(INCONCLUSIVE)
    return tok


def finish(verdict, reasons=(), tool=""):
    """print the reasons and the one VERDICT line; return the exit code (0 / 1 / 3)"""
    for r in reasons:
        print("  - " + r)
    print("VERDICT %s%s" % (verdict, (" (" + tool + ")") if tool else ""))
    sys.stdout.flush()
    return EXIT[verdict]


def timebox(seconds, tool=""):
    """watchdog: after `seconds` print INCONCLUSIVE and exit 3 (os._exit: no cleanup can hang). Returns the timer (cancel() it)."""
    def fire():
        print("%s: time box of %.0f s exceeded" % (tool or os.path.basename(sys.argv[0]), seconds))
        print("VERDICT INCONCLUSIVE")
        sys.stdout.flush()
        os._exit(INCONCLUSIVE)
    t = threading.Timer(seconds, fire)
    t.daemon = True
    t.start()
    return t


# ----------------------------------------------------------------------------------------------------------- Turbo's log, per process
# turbo_gui.log is appended across game launches. Addresses (knob instances, the team-sheet service) are valid only in the process
# that logged them, so every tool reads ONLY the lines after the latest "Turbo GUI loading in process <pid>" line and refuses unless
# that pid is the running FC27.exe (review B1, 2026-10-13: the latest locator line belonged to the previous process).
MOCK_PID = 4242  # the synthetic FC27.exe pid of the mock worlds


class StaleLog(Exception):
    pass


def log_since_start(path):
    """-> (pid, start line, [lines after it]) of the LATEST Turbo start in the log; (None, None, []) when there is none"""
    import re
    rx = re.compile(r"Turbo GUI loading in process (\d+)")
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError as e:
        raise StaleLog("log %s unreadable: %s" % (path, e))
    for i in range(len(lines) - 1, -1, -1):
        m = rx.search(lines[i])
        if m:
            return int(m.group(1)), lines[i], lines[i + 1:]
    return None, None, []


def running_fc27_pids(mock_mem=None):
    """pids of the running FC27.exe (tasklist, read-only); with a mock world: its synthetic pid (kv fc27_pid)"""
    if mock_mem:
        p = MockMem(mock_mem).kv("fc27_pid")
        return {int(p)} if p else set()
    import csv as _csv, io as _io, subprocess
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq FC27.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True,
                             timeout=20).stdout
    except (OSError, subprocess.SubprocessError):
        return set()
    pids = set()
    for row in _csv.reader(_io.StringIO(out)):
        if len(row) >= 2 and row[0].lower() == "fc27.exe" and row[1].isdigit():
            pids.add(int(row[1]))
    return pids


def current_log(path, mock_mem=None):
    """-> (pid, start line, lines) of the RUNNING game's Turbo only; raises StaleLog when the log has no start line or its latest start
    is not a running FC27.exe (a stale address from an earlier process is never used)"""
    pid, start, lines = log_since_start(path)
    if pid is None:
        raise StaleLog("no 'Turbo GUI loading in process' line in %s" % path)
    pids = running_fc27_pids(mock_mem)
    if pid not in pids:
        raise StaleLog("the log's latest Turbo start is process %d but the running FC27.exe is %s: its addresses are stale" % (
            pid, sorted(pids) or "none"))
    return pid, start, lines


def write_marker(path, label):
    """the window marker file read by match_measure.py --marker (first token = the label of the current samples)"""
    if not path:
        return
    tmp = path + ".tmp%d" % os.getpid()
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(label + "\n")
    os.replace(tmp, path)


def append_log(path, line):
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + line + "\n")
    except OSError as e:
        print("log %s not written: %s" % (path, e))


# ----------------------------------------------------------------------------------------------------------- the X1 knob (data)
# Build 6AC07E31-2145C000 constants, documented in docs/TACTICS_OVERHAUL_DECISION.md / TO\ATTRIBULATOR_LIVE.txt. FC 27's own vanilla
# values only; ranges are the generic engine ranges (decision 3.1). No mod values.
IMAGE_BASE = 0x140000000
BUILDS = {"6AC07E31": {"size": 0x2145C000}}
KNOBS = {
    "width_in": {
        "label": "gp_positioningformation_attackinglinelengthwidth.lineWidthModifier",
        "name": "Fifa/Attribulator/Gameplay/groups/gp_positioningformation/"
                "gp_positioningformation_attackinglinelengthwidth_runtime",
        "typeinfo": {"6AC07E31": 0x14BE84240},
        "off": 0x28, "fmt": "<f", "vanilla": 1.0, "range": (0.5, 1.7),
        "log_re": r"locator attrib\.attackinglinelengthwidth found: .* base (0x[0-9A-Fa-f]+)",
    },
}


# ----------------------------------------------------------------------------------------------------------- mock dev service
MOCK_HEAP = 0x00005EED00000000  # synthetic heap base of the mock world (not a game address)


class MockMem:
    """file-backed fake game memory. JSON: {"regions": [[base, hex], ...], "modules": [...], "kv": {...}, "writes": n}.
    call() mirrors dev2.call(): the same write gate (allow_write), result shapes {"ok", "hex"} / {"results": [...]}."""

    def __init__(self, path):
        self.path = path

    def _load(self):
        with open(self.path, encoding="utf-8") as f:
            return json.load(f)

    def _save(self, d):
        tmp = self.path + ".tmp%d" % os.getpid()
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(d, f)
        for _ in range(50):  # another mock process may hold the file for a moment (Windows)
            try:
                os.replace(tmp, self.path)
                return
            except PermissionError:
                time.sleep(0.02)
        os.replace(tmp, self.path)

    def _read_json(self):
        for _ in range(50):
            try:
                return self._load()
            except (PermissionError, json.JSONDecodeError):
                time.sleep(0.02)
        return self._load()

    @staticmethod
    def create(path, regions, modules=(), kv=None):
        d = {"regions": [[b, data.hex()] for b, data in regions], "modules": list(modules), "kv": kv or {}, "writes": 0}
        with open(path, "w", encoding="utf-8") as f:
            json.dump(d, f)
        return MockMem(path)

    def read(self, addr, n, d=None):
        d = d or self._read_json()
        for base, hx in d["regions"]:
            data = bytes.fromhex(hx)
            if base <= addr and addr + n <= base + len(data):
                return data[addr - base:addr - base + n]
        return None

    def write(self, addr, data):
        d = self._read_json()
        for reg in d["regions"]:
            base, hx = reg
            buf = bytearray.fromhex(hx)
            if base <= addr and addr + len(data) <= base + len(buf):
                buf[addr - base:addr - base + len(data)] = data
                reg[1] = buf.hex()
                d["writes"] = d.get("writes", 0) + 1
                self._save(d)
                return True
        return False

    def kv(self, key, default=None):
        return self._read_json().get("kv", {}).get(key, default)

    def set_kv(self, key, value):
        d = self._read_json()
        d.setdefault("kv", {})[key] = value
        self._save(d)

    def _one(self, op, d):
        name = op.get("op")
        if name == "ping":
            return {"ok": True, "pong": True}
        if name == "modules":
            return {"ok": True, "modules": d.get("modules", [])}
        if name == "bytes":
            a, n = int(str(op["addr"]), 16), int(op["len"])
            b = self.read(a, n, d)
            return {"ok": True, "hex": b.hex()} if b is not None else {"ok": False, "error": "unreadable"}
        if name == "find":  # pattern with ?? wildcards over the mock regions inside [from, to)
            toks = [None if t == "??" else int(t, 16) for t in op["pattern"].split()]
            lo, hi = int(str(op.get("from", "0x0")), 16), int(str(op.get("to", "0x7FFFFFFFFFFF")), 16)
            hits = []
            for base, hx in d["regions"]:
                if base + len(hx) // 2 <= lo or base >= hi:
                    continue
                data = bytes.fromhex(hx)
                for i in range(len(data) - len(toks) + 1):
                    if lo <= base + i < hi and all(t is None or data[i + j] == t for j, t in enumerate(toks)):
                        hits.append("0x%X" % (base + i))
            return {"ok": True, "hits": hits[:int(op.get("max", 16))]}
        if name == "write":
            a = int(str(op["addr"]), 16)
            nth = os.environ.get("MOCK_WRITE_FAIL_NTH")  # tests: the n-th write attempts fail (a dev timeout / error)
            if nth:
                n = int(self.kv("write_attempts", 0)) + 1
                self.set_kv("write_attempts", n)
                if str(n) in nth.split(","):
                    return {"ok": False, "error": "mock: write attempt %d refused" % n}
            if ("0x%x" % a) in os.environ.get("MOCK_WRITE_FAIL", "").lower().split(","):  # tests: these addresses refuse the write
                return {"ok": False, "error": "mock: write refused at 0x%X" % a}
            data = bytes.fromhex(str(op.get("hex") or op.get("bytes")).replace(" ", ""))
            if not self.write(a, data):
                return {"ok": False, "error": "unwritable"}
            back = self.read(a, len(data))
            return {"ok": True, "hex": back.hex()}
        return {"ok": False, "error": "mock: op %s not supported" % name}

    def call(self, req, timeout=60, repost=0.7, allow_write=False):
        """X1_MOCK_LATENCY (simulated s per call, scaled by X1_TIME_SCALE) emulates the real client's single lock directory: one
        caller at a time, polled every 0.1 s, held for the call. A 1 Hz sampler whose calls take about a second then starves every
        other client - the 10-13 X1 v1 failure - unless it pauses (match_measure --pause)."""
        import dev2  # the same gates as the real client
        if dev2.has_escape(req):
            return {"error": "dev2: Escape (VK 27) is never sent"}
        if not allow_write and dev2.has_write_op(req):
            return {"error": "dev2: write/key ops need allow_write=True (coordinator only)"}
        lat = float(os.environ.get("X1_MOCK_LATENCY", "0") or 0)
        if lat <= 0:
            return self._dispatch(req)
        lock, deadline = self.path + ".lock", time.time() + timeout
        while True:
            try:
                os.mkdir(lock)
                break
            except FileExistsError:
                if time.time() > deadline:
                    return {"error": "dev2: lock busy (another client)"}
                time.sleep(0.1)
        try:
            time.sleep(lat * float(os.environ.get("X1_TIME_SCALE", "1") or 1))
            return self._dispatch(req)
        finally:
            os.rmdir(lock)

    def _dispatch(self, req):
        d = self._read_json()
        if req.get("op") == "multi":
            res = [self._one(o, d) for o in req.get("ops", [])]
            return {"ok": True, "results": res}
        return self._one(req, d)


def pe_header(timestamp, size):
    """a minimal PE header (0x400 bytes): e_lfanew 0x80, TimeDateStamp at e_lfanew + 8 (what build_key reads)"""
    h = bytearray(0x400)
    h[0:2] = b"MZ"
    struct.pack_into("<I", h, 0x3C, 0x80)
    h[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<I", h, 0x88, timestamp)
    struct.pack_into("<I", h, 0x80 + 0x50, size)
    return bytes(h)


def build_x1_world(path, log_path=None, knob="width_in", build="6AC07E31", value=None, wrong_typeinfo=False):
    """the mock world of X1: FC27.exe module + PE header, one knob instance with vtable / TypeInfo / Name / value, and (optionally) a
    turbo_gui.log line naming the instance the way Turbo's tuning tab logs it. Returns (MockMem, instance address)."""
    k = KNOBS[knob]
    size = BUILDS[build]["size"]
    inst = MOCK_HEAP + 0x1000
    namep = MOCK_HEAP + 0x2000
    blob = bytearray(0x100)
    struct.pack_into("<Q", blob, 0, IMAGE_BASE + 0x100000)  # vtable: an image pointer
    struct.pack_into("<Q", blob, 8, k["typeinfo"][build] + (0x10 if wrong_typeinfo else 0))
    struct.pack_into("<Q", blob, 0x18, namep)
    struct.pack_into(k["fmt"], blob, k["off"], k["vanilla"] if value is None else value)
    name = k["name"].encode() + b"\0"
    mods = [{"name": "FC27.exe", "base": "0x%X" % IMAGE_BASE, "size": size}]
    mem = MockMem.create(path, [(IMAGE_BASE, pe_header(int(build, 16), size)), (inst, bytes(blob)),
                                (namep, name + bytes(64 - len(name) % 64))], mods, kv={"fc27_pid": MOCK_PID})
    if log_path:
        with open(log_path, "a", encoding="utf-8") as f:
            f.write("2026-10-13 21:00:00.000  Turbo GUI loading in process %d\n" % MOCK_PID)
            f.write("2026-10-13 21:00:30.000  gameplay tuning: locator attrib.attackinglinelengthwidth found: type "
                    "AttribSchema_gp_positioningformation_linelengthwidth, base 0x%X\n" % inst)
    return mem, inst


def mock_call(path):
    """dev2-compatible call() over a MockMem file"""
    return MockMem(path).call


# ----------------------------------------------------------------------------------------------------------- CLI (for the wrappers)
def _cli(argv):
    """python harness.py state            -> prints the GAME_STATE line; exit 0 when live, 3 otherwise
       python harness.py mock-x1 DIR      -> builds the X1 mock world in DIR (mem.json, turbo_gui.log); prints the instance
       python harness.py marker FILE LBL  -> atomically writes the window marker"""
    if argv[:1] == ["state"]:
        tok, line = read_game_state()
        print("GAME_STATE %s: %s" % (tok or "missing", line or GAME_STATE))
        return 0 if tok in LIVE_TOKENS else INCONCLUSIVE
    if argv[:1] == ["mock-x1"] and len(argv) == 2:
        os.makedirs(argv[1], exist_ok=True)
        mem, inst = build_x1_world(os.path.join(argv[1], "mem.json"), os.path.join(argv[1], "turbo_gui.log"))
        mem.set_kv("x1_inst", "0x%X" % inst)
        print("mock world: %s instance 0x%X" % (os.path.join(argv[1], "mem.json"), inst))
        return 0
    if argv[:1] == ["marker"] and len(argv) == 3:
        write_marker(argv[1], argv[2])
        return 0
    print(_cli.__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(_cli(sys.argv[1:]))
