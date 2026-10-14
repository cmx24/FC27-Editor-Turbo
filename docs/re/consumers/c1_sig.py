"""c1_sig.py - byte signatures (wildcarded rip/rel32) for the consumer read sites, unique in .text1 of the OLD image. Output c1_sig.json"""
import sys, json, os, re
sys.path.insert(0, r"C:\FC 27 Live Editor\turbo_output\assessment_20261013\T1")
import t1_re as R
from capstone import x86
CS = R.cs()
m = R.img()
LO, HI = R.va2off(0x140001000), R.va2off(0x148B69000)
TEXT = bytes(m[LO:HI])

def insn_list(func, upto):
    out = []
    for ins in CS.disasm(R.rd(func, (upto - func) + 64), func):
        out.append(ins)
        if ins.address >= upto:
            break
    return out

def wild(ins):
    b = bytearray(ins.bytes)
    mask = [False] * len(b)
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
            # rip-relative disp32 sits at the end of the modrm part; wildcard the 4 bytes matching the disp
            d = (op.mem.disp & 0xFFFFFFFF).to_bytes(4, "little")
            i = bytes(b).rfind(d)
            if i >= 0:
                for k in range(4): mask[i + k] = True
    if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
        if len(b) >= 5 and ins.operands and ins.operands[0].type == x86.X86_OP_IMM:
            n = 4 if (b[0] in (0xE8, 0xE9) or b[0] == 0x0F) else 1
            for k in range(len(b) - n, len(b)): mask[k] = True
    return b, mask

def regex_of(parts):
    return re.compile(b"".join(b"." if w else re.escape(bytes([x])) for x, w in parts), re.DOTALL)

def make_sig(func, va, maxback=14):
    lst = insn_list(func, va)
    idx = [i for i, x in enumerate(lst) if x.address == va]
    if not idx:
        return None
    i0 = idx[0]
    for back in range(1, maxback + 1):
        sel = lst[max(0, i0 - back): i0 + 2]
        parts = []
        for ins in sel:
            b, mk = wild(ins)
            parts += list(zip(b, mk))
        rx = regex_of(parts)
        hits = [mo.start() for mo in rx.finditer(TEXT)]
        if len(hits) == 1 or (back == maxback):
            sig = " ".join("??" if w else "%02X" % x for x, w in parts)
            return {"sig": sig, "hits": len(hits), "start": hex(R.off2va(LO + hits[0])) if hits else None, "insns": len(sel)}
    return None

if __name__ == "__main__":
    h = json.load(open("c1_scan.json"))
    todo = {}
    for key, fl in h.items():
        if key == "CACHE": continue
        for off, v in fl.items():
            for (f, va, txt, acc) in v:
                todo[(f, va)] = (key, off, txt)
    out = {}
    for (f, va), (key, off, txt) in todo.items():
        try:
            s = make_sig(int(f, 16), int(va, 16))
        except Exception as e:
            s = {"err": str(e)}
        out["%s@%s" % (f, va)] = dict(s or {}, key=key, off=off, txt=txt)
    json.dump(out, open("c1_sig.json", "w"))
    print(len(out))
