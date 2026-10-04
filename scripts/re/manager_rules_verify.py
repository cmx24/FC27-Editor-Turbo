"""Re-check the manager-rules signatures AND the facts behind the offsets (docs/re/manager_rules.md) on the image.

    python scripts/re/manager_rules_verify.py [path/to/fc27_image.bin]
Exit code 0 when every signature is unique, lands on the recorded VA, and every proof below still holds.
"""
import json
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
if len(sys.argv) > 1:
    os.environ["FC27_IMAGE"] = sys.argv[1]
import rx_jobs as rx  # noqa: E402
from sig_jobs import find  # noqa: E402

data = json.load(open(os.path.join(here, "manager_rules_signatures.json")))
bad = []


def check(cond, what):
    print("  %-4s %s" % ("ok" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def dis_text(va, n):
    return [re.sub(r"\s+", " ", l) for l in rx.dis(va, n)]


def lea_target(ins_va):
    # lea r, [rip + disp32]: 48/4C 8D xx disp32
    disp = rx.i32(ins_va + 3)
    return ins_va + 7 + disp


print("signatures")
for name, f in data["functions"].items():
    hits = find(f["sig"])
    want = int(f["va"], 16)
    check(len(hits) == 1 and hits[0] == want, "%-26s unique at %x (hits %s)" % (name, want, ", ".join("%x" % h for h in hits[:3])))
    if "rip_at" in f and hits:
        t = lea_target(hits[0] + int(f["rip_at"], 16))
        check(t == int(f["resolves_to"], 16), "%-26s rip -> %x" % (name, t))

print("constructors proven by the hub builder (name string, then the ctor call)")
for cls, m in data["managers"].items():
    at = int(m["hub_builder_name_lea"], 16)
    check(rx.cstr(lea_target(at)) == cls.encode(), "%s: lea at %x names %s" % (cls, at, rx.cstr(lea_target(at))))
    ctor = int(m["ctor"], 16)
    calls = [l for l in dis_text(at, 16) if "call" in l and ("%x" % ctor) in l]
    check(len(calls) == 1, "%s: the builder calls the ctor %x right after" % (cls, ctor))
    vt = int(m["vtable"], 16)
    check(rx.u64(vt) != 0, "%s vtable %x slot0 %x" % (cls, vt, rx.u64(vt)))
check(rx.u64(0x14b016598 + 8) == 0x147dd2a30, "JobSwitchManager vtable slot 1 = HandleEvent 0x147dd2a30")
size_ok = any("0x1e8" in l for l in dis_text(0x147f18f30, 6))
check(size_ok, "JobSwitchManager allocation size 0x1e8 at the builder")

print("function names")
for va, s in ((0x147e07e2c, "ClubObjectivesManager::UpdateJobSecurityScore"), (0x147ddf900, "JobSwitchManager::SackManager")):
    found = False
    for l in dis_text(va, 40):
        m = re.search(r"lea r8, \[rip \+ 0x([0-9a-f]+)\]", l)
        if m:
            a = int(l.split()[0], 16)
            if rx.cstr(lea_target(a)) == s.encode():
                found = True
    check(found, "%x uses its name string %s" % (va, s))

print("serialised fields")
def names_in(va, n):
    out = []
    for l in dis_text(va, n):
        m = re.search(r"lea rax, \[rip \+ 0x([0-9a-f]+)\]", l)
        if m:
            a = int(l.split()[0], 16)
            b = rx.cstr(lea_target(a), 60)
            if b.startswith(b"m"):
                out.append(b.decode(errors="ignore"))
    return out
com_names = names_in(0x147edb3a8, 40)
check(com_names[:4] == ["mIsManagerMode", "mUserTeamId", "mJobSecurityScoreAddon", "mJobSecurityScore"],
      "ClubObjectivesManager block: %s" % com_names[:4])
jsm_names = names_in(0x147edf91c, 60)
check("mPendingSack" in jsm_names and "mWasSacked" in jsm_names and jsm_names[0] == "mLastJobSwitchDate",
      "JobSwitchManager block: %s" % jsm_names)

print("threshold loader (store after the NEXT name's lea = the previous name's value)")
seq = []
last = None
for l in dis_text(0x147e00e7c, 140):
    m = re.search(r"lea rdx, \[rip \+ 0x([0-9a-f]+)\]", l)
    if m:
        a = int(l.split()[0], 16)
        nm = rx.cstr(lea_target(a), 80).decode(errors="ignore")
        seq.append(("name", nm))
    m2 = re.search(r"mov dword ptr \[rsi(?: \+ 0x([0-9a-f]+))?\], eax", l)
    if m2:
        seq.append(("store", int(m2.group(1) or "0", 16)))
mapping = {}
names = [v for k, v in seq if k == "name"]
pending = None
for k, v in seq:
    if k == "name":
        if pending is None:
            pending = [v]
        else:
            pending.append(v)
    elif k == "store" and pending and len(pending) >= 2:
        mapping[pending[-2]] = v
        pending = pending[-1:]
want = {"OBJECTIVES/JOB_SECURITY_VERY_INSECURE": 0x28, "OBJECTIVES/JOB_SECURITY_INSECURE": 0x2C, "OBJECTIVES/JOB_SECURITY_OKAY": 0x30,
        "OBJECTIVES/JOB_SECURITY_SAFE": 0x34, "OBJECTIVES/JOB_SECURITY_FIRED_POINTS": 0x3C}
for k, v in want.items():
    check(mapping.get(k) == v, "%s -> settings +0x%x (com +0x%x), got %s" % (k, v, v + 0x10, hex(mapping[k]) if k in mapping else None))

print("level copies and level function")
cp = " ".join(dis_text(0x147e06b37, 16))
for src, dst in ((0x38, 0x278), (0x3c, 0x27c), (0x40, 0x280), (0x44, 0x284), (0x4c, 0x288)):
    check(("[rbx + 0x%x]" % src) in cp and ("[rbx + 0x%x]" % dst) in cp, "com+0x%x copied to com+0x%x" % (src, dst))
lv = " ".join(dis_text(0x147df9a8c, 12))
check("[rcx + 0x1c]" in lv and "mov eax, 3" in lv, "level function: >= [this+0x1c] (com+0x284) -> 3 (safe)")
check(rx.u64(0x14b019638 + 0x30) == 0x147df9a8c, "level object vtable 0x14b019638 slot 6 = 0x147df9a8c")

print("+0x8 is the career manager table, not a hub object (the 04-10-2026 refusal; live proof: manager_rules_live.py)")
ctx = " ".join(dis_text(0x147f2b3e8, 2))
check("mov rax, qword ptr [rcx + 0x10]" in ctx and "ret" in ctx, "0x147f2b3e8 (the ctors' 2nd argument) = [builder+0x10]")
for at, ctor, reg, slot in ((0x147f19468, 0x147df42d0, 0x147ec0750, 133), (0x147f18f27, 0x147db6984, 0x147ec07c0, 54)):
    seq_txt = " ".join(dis_text(at, 22))
    check("mov rdi, qword ptr [r14 + 0x10]" in seq_txt and ("call 0x%x" % ctor) in seq_txt and ("call 0x%x" % reg) in seq_txt,
          "builder at %x: rdi = [r14+0x10] (the table), ctor %x, registrar %x(rdi, obj)" % (at, ctor, reg))
    r = " ".join(dis_text(reg, 4))
    check(("[rcx + 0x%x]" % (slot * 0x20 + 0x10)) in r and ("[rcx + 0x%x]" % (slot * 0x20 + 0x18)) in r,
          "registrar %x writes slot %d (count +0x%x, holder +0x%x)" % (reg, slot, slot * 0x20 + 0x10, slot * 0x20 + 0x18))
init = " ".join(dis_text(0x147e06aac, 40))
for src, dst in ((0x318, 0x248), (0x6d8, 0x250), (0x6f8, 0x258)):
    check(("[rdx + 0x%x]" % src) in init and ("[rbx + 0x%x], rcx" % dst) in init,
          "0x147e06aac: com+0x%x = [[table+0x%x]] (slot %d)" % (dst, src, (src - 0x18) // 0x20))
ct = dis_text(0x147df4363, 80)
st = [i for i, l in enumerate(ct) if "mov qword ptr [r12 + 0x270], rcx" in l]
last_rcx = [l for l in ct[:st[0]] if re.search(r"^\S+ (?:mov|lea|call|xor|add|sub|pop) rcx|^\S+ call ", l)] if st else []
check(bool(st) and last_rcx and "lea rcx, [r12 + 0x11c]" in last_rcx[-1],
      "ctor: level object +0x270 = this+0x11C (last rcx write: %s)" % (last_rcx[-1] if last_rcx else None))
up = " ".join(dis_text(0x147e07e2c, 60))
check("mov rax, qword ptr [rsi + 8]" in up and "mov rcx, qword ptr [rax + 0x4f8]" in up and "call 0x14060124c" in up,
      "UpdateJobSecurityScore posts through [[[this+8]+0x4F8]] (EventsMailBox, slot 39)")
pe = " ".join(dis_text(0x14060124c, 26))
check("mov rcx, qword ptr [rbx]" in pe and "call qword ptr [rax + 0x30]" in pe, "PostEvent 0x14060124c calls [[[mailbox]]+0x30]")

print("sack paths")
he = " ".join(dis_text(0x147dd2a30, 60))
check("cmp byte ptr [rcx + 0x1e0], 0" in he and "jmp 0x147ddf900" in he, "DAY_PASSED: [+0x1E0] -> SackManager")
ce = " ".join(dis_text(0x147f57d14, 20))
check("call 0x147ddf900" in ce, "contract-ended handler 0x147f57d14 calls SackManager")

print("RESULT: %s" % ("all checks passed" if not bad else "%d FAILED" % len(bad)))
sys.exit(1 if bad else 0)
