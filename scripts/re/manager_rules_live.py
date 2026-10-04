"""Live proof of the manager-rules objects (docs/re/manager_rules.md section 1.1), read-only through Turbo's dev service.

    python scripts/re/manager_rules_live.py [team]

Needs the game running with a Manager Career loaded and Turbo.dll's dev service (turbo_output\\turbo_dev_request.json).
Only the `read` op is used (never write / key); one request at a time, >= 1.3 s apart (the service polls the file and
may be shared: an unanswered request is written again after 60 s). Steps, the same checks Turbo.dll makes
(turbogui/src/core/manager_rules.cpp: validate_com, validate_jsm, check_update_path):
  1. the career manager table from bridge_state.json "managers" ([[comm+0x20]+0x10]); slot n = table + n*0x20:
     +0 i32 type id, +8 type descriptor (+0x10 == 1), +0x10 i32 instance count (1), +0x18 holder -> object;
  2. slot 133 / 54 objects: vtable 0x14B019370 / 0x14B016598 (the constructors' lea), +0x8 == the table (the table's
     own first qword is slot 0's type id 0: no object, no vtable), the slot holds them;
  3. ClubObjectivesManager fields (block owner, manager mode, user team, scores, bands) and everything
     UpdateJobSecurityScore 0x147E07E2C dereferences: +0x248/+0x250/+0x258 == slots 24/54/55, the inline level object
     (vtable slot 6, +0x270 == this+0x11C), the objectives' vtable slots 0..4, [[[[table+0x4F8]]]]+0x30.
Exit code 0 when every check holds.
"""
import json
import os
import struct
import sys
import time
import uuid

OUT = r"C:\FC 27 Live Editor\turbo_output"
REQ = os.path.join(OUT, "turbo_dev_request.json")
RES = os.path.join(OUT, "turbo_dev_result.json")
COM_VT, JSM_VT, LEVEL_VT = 0x14B019370, 0x14B016598, 0x14B019638
T_CAL, T_MBOX, T_JSM, T_LIVE, T_COM = 24, 39, 54, 55, 133
_last = [0.0]
_cache = {}
bad = []


def _request(req, timeout=60.0):
    assert req.get("op") == "read"
    rid = "mrlive-" + uuid.uuid4().hex[:10]
    req = dict(req, id=rid)
    for _ in range(4):
        wait = 1.3 - (time.time() - _last[0])
        if wait > 0:
            time.sleep(wait)
        tmp = REQ + ".mrlive.tmp"
        with open(tmp, "w") as f:
            json.dump(req, f)
        os.replace(tmp, REQ)
        _last[0] = time.time()
        t0 = time.time()
        while time.time() - t0 < timeout:
            time.sleep(0.25)
            try:
                with open(RES) as f:
                    d = json.load(f)
            except (OSError, ValueError):
                continue
            if d.get("id") == rid:
                return d
    raise SystemExit("the dev service did not answer " + rid)


def read(addr, n):
    """n bytes at addr (None when unreadable); cached per request"""
    key = (addr, n)
    if key not in _cache:
        d = _request({"op": "read", "addr": hex(addr), "len": n})
        _cache[key] = bytes.fromhex(d["hex"]) if d.get("ok") else None
    return _cache[key]


def q(addr):
    b = read(addr, 8)
    return struct.unpack("<Q", b)[0] if b else None


def i32(addr):
    b = read(addr, 4)
    return struct.unpack("<i", b)[0] if b else None


def is_ptr(v, align=1):
    return v is not None and 0x10000 <= v <= 0x7FFFFFFEFFFF and v % align == 0


def check(cond, what):
    print("  %-4s %s" % ("ok" if cond else "FAIL", what))
    if not cond:
        bad.append(what)
    return cond


def slot_object(table, t):
    s = read(table + 0x20 * t, 0x20)
    if not s:
        return None, "slot %d unreadable" % t
    tid = struct.unpack_from("<i", s, 0)[0]
    typ = struct.unpack_from("<Q", s, 0x08)[0]
    count = struct.unpack_from("<i", s, 0x10)[0]
    holder = struct.unpack_from("<Q", s, 0x18)[0]
    if count != 1:
        return None, "slot %d holds %d instances" % (t, count)
    if not is_ptr(typ) or i32(typ + 0x10) != 1:
        return None, "slot %d has no type descriptor" % t
    obj = q(holder) if is_ptr(holder, 8) else None
    if not is_ptr(obj, 8):
        return None, "slot %d holder does not point at an object" % t
    return obj, "slot %d: type id %d, holder 0x%X -> 0x%X" % (t, tid, holder, obj)


def vcall(obj, off):
    vt = q(obj)
    if not is_ptr(vt, 8):
        return None
    fn = q(vt + off)
    return fn if is_ptr(fn) and read(fn, 1) is not None else None


def main():
    team = int(sys.argv[1]) if len(sys.argv) > 1 else None
    st = json.load(open(os.path.join(OUT, "bridge_state.json")))
    table = int(st["managers"], 16)
    team = team or st.get("user_team")
    print("career manager table 0x%X (bridge_state.json), user team %s" % (table, team))
    first = q(table)
    check(first == 0, "the table's first qword is %r (slot 0's type id): it is no object with a vtable" % first)
    objs = {}
    for t in (T_CAL, T_MBOX, T_JSM, T_LIVE, T_COM):
        o, why = slot_object(table, t)
        check(o is not None, why)
        objs[t] = o
    com, jsm = objs[T_COM], objs[T_JSM]
    if com is None or jsm is None:
        return 1
    print("ClubObjectivesManager 0x%X" % com)
    c = read(com, 0x2A0)
    check(c is not None, "0x2A0 bytes readable")
    u = lambda o: struct.unpack_from("<Q", c, o)[0]  # noqa: E731
    s32 = lambda o: struct.unpack_from("<i", c, o)[0]  # noqa: E731
    check(u(0) == COM_VT, "vtable 0x%X (expected 0x%X, ctor 0x147DF42D0)" % (u(0), COM_VT))
    check(u(8) == table, "+0x8 = 0x%X == the table (ctor rdx = [builder+0x10], registrar 0x147EC0750 -> slot 133)" % u(8))
    check(u(0x108) == com, "+0x108 serialised block owner = this")
    check(c[0x110] == 1, "+0x110 mIsManagerMode = %d" % c[0x110])
    check(team is None or s32(0x114) == team, "+0x114 mUserTeamId = %d" % s32(0x114))
    print("       addon %d, scores (season final / previous / current) %d / %d / %d" % (s32(0x118), s32(0x11C), s32(0x120), s32(0x124)))
    check(0 <= s32(0x124) <= 100, "score in 0..100")
    bands = [s32(0x278 + 4 * i) for i in range(5)]
    sets = [s32(0x38 + 4 * i) for i in (0, 1, 2, 3, 5)]
    check(bands == sets, "level bands +0x278.. %s == OBJECTIVES settings +0x38/+0x3C/+0x40/+0x44/+0x4C %s" % (bands, sets))
    check(u(0x248) == objs[T_CAL], "+0x248 = 0x%X == CalendarManager (slot 24)" % u(0x248))
    check(u(0x250) == jsm, "+0x250 = 0x%X == JobSwitchManager (slot 54)" % u(0x250))
    check(u(0x258) == objs[T_LIVE], "+0x258 = 0x%X == LiveServicesManager (slot 55)" % u(0x258))
    check(read(objs[T_CAL], 0x40) is not None and read(objs[T_LIVE], 0x38) is not None, "calendar +0..0x40 / live services +0..0x38 readable")
    check(u(0x268) == LEVEL_VT, "+0x268 level object vtable 0x%X (ctor lea 0x14B019638)" % u(0x268))
    fn = vcall(com + 0x268, 0x30)
    check(fn is not None, "level function (vtable slot 6) = %s" % (hex(fn) if fn else None))
    check(u(0x270) == com + 0x11C, "+0x270 = this+0x11C (the level object's score pointer)")
    b, e = u(0x218), u(0x220)
    n = (e - b) // 8 if is_ptr(b, 8) and e >= b else -1
    check(b == e == 0 or (is_ptr(b, 8) and is_ptr(e, 8) and e >= b and (e - b) % 8 == 0 and n <= 64),
          "objectives vector 0x%X..0x%X (%d)" % (b, e, n))
    for k in range(max(n, 0)):
        o = q(b + 8 * k)
        if not o:
            continue
        slots = [vcall(o, 8 * i) for i in range(5)]
        check(all(slots), "objective %d at 0x%X, vtable 0x%X, slots 0..4 %s" % (k, o, q(o), [hex(x) if x else None for x in slots]))
    holder = q(table + 0x4F8)
    mbox = q(holder) if is_ptr(holder, 8) else None
    check(mbox == objs[T_MBOX], "[[table+0x4F8]] = %s == EventsMailBox (slot 39)" % (hex(mbox) if mbox else None))
    disp = q(mbox) if mbox else None
    pf = vcall(disp, 0x30) if is_ptr(disp, 8) else None
    check(pf is not None, "EventsMailBox dispatcher %s, vtable +0x30 = %s (PostEvent 0x14060124C calls it)" %
          (hex(disp) if disp else None, hex(pf) if pf else None))
    print("JobSwitchManager 0x%X" % jsm)
    j = read(jsm, 0x1E8)
    check(j is not None, "0x1E8 bytes readable (allocation size 0x147F18F41)")
    ju = lambda o: struct.unpack_from("<Q", j, o)[0]  # noqa: E731
    ji = lambda o: struct.unpack_from("<i", j, o)[0]  # noqa: E731
    check(ju(0) == JSM_VT, "vtable 0x%X (expected 0x%X, ctor 0x147DB6984)" % (ju(0), JSM_VT))
    check(ju(8) == table, "+0x8 == the table (registrar 0x147EC07C0 -> slot 54)")
    check(j[0x1E0] <= 1 and j[0x1E1] <= 1, "sack flags pending %d / sacked %d" % (j[0x1E0], j[0x1E1]))
    print("       mLastJobSwitchDate %d, mPreviousTeamId %d" % (ji(0x1B8), ji(0x1BC)))
    print("result: %s" % ("all checks hold" if not bad else "%d FAILED" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
