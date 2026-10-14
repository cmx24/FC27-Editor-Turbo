"""c1_scan.py - consumers track (2.6), OLD image 6AB9813C (base 0x140000000), read-only.
For every function that calls get_current_gameplay_attribdb (0x141078530) or reads the manager slot G (0x14C269128) a whole-function
linear sweep tracks registers (and stack spill slots) that hold: DB -> family object ([DB+fam]) -> group instance ([fam+slot]) and records every
instruction that reads/writes [inst+field] (field offset, mnemonic, va). Calls that pass a tracked pointer in rcx/rdx/r8/r9 are followed
(depth 3) with the callee started in that register state.
Output: c1_scan.json  {"fam:slot": {"0xFIELD": [[func, va, text], ...]}} plus per-function listing.
Run: python -I c1_scan.py
"""
import json, os, sys, struct
sys.path.insert(0, r"C:\FC 27 Live Editor\turbo_output\assessment_20261013\T1")
import t1_re as R
from capstone import x86
import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
FUNC = 0x141078530
G = 0x14C269128
CS = R.cs()

ARGS = ["rcx", "rdx", "r8", "r9"]
CANON = {}


def canon(r):
    if r in CANON:
        return CANON[r]
    n = CS.reg_name(r)
    m = {"eax": "rax", "ebx": "rbx", "ecx": "rcx", "edx": "rdx", "esi": "rsi", "edi": "rdi", "ebp": "rbp", "esp": "rsp",
         "ax": "rax", "bx": "rbx", "cx": "rcx", "dx": "rdx", "al": "rax", "bl": "rbx", "cl": "rcx", "dl": "rdx"}
    if n in m:
        n = m[n]
    elif n.startswith("r") and n[1:-1].isdigit() and n[-1] in "dwb":
        n = n[:-1]
    CANON[r] = n
    return n


NONVOL = {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"}


def sweep(start, init=None, maxins=7000, depth=0, hits=None, edges=None, seen=None):
    """state: reg -> tuple ('DB',) | ('FAM', fam) | ('INST', fam, slot); stack: disp(rsp/rbp) -> same"""
    st = dict(init or {})
    stack = {}
    n = 0
    pend_ret = False
    for ins in CS.disasm(R.rd(start, maxins * 8), start, maxins):
        n += 1
        mn = ins.mnemonic
        if mn == "int3" and n > 1:
            break
        ops = ins.operands
        try:
            rd_, wr_ = ins.regs_access()
        except Exception:
            rd_, wr_ = [], []
        # ---- field access through a tracked INST register / loads of fam / inst
        newstate = None
        for op in ops:
            if op.type == x86.X86_OP_MEM and op.mem.base != 0:
                b = canon(op.mem.base)
                s = st.get(b)
                if b in ("rsp", "rbp") and op.mem.index == 0:
                    continue
                if s is None:
                    continue
                off = op.mem.disp
                if s[0] == "DB":
                    if mn == "mov" and ops[0].type == x86.X86_OP_REG and ops[1] is op and ops[0].size == 8:
                        newstate = (canon(ops[0].reg), ("FAM", off))
                elif s[0] == "FAM":
                    if mn == "mov" and ops[0].type == x86.X86_OP_REG and ops[1] is op and ops[0].size == 8:
                        newstate = (canon(ops[0].reg), ("INST", s[1], off))
                elif s[0] == "INST":
                    key = "%s:%s" % (hex(s[1]), hex(s[2]))
                    acc = "W" if (op is ops[0] and mn in ("mov", "movss", "movaps", "movups", "movsd", "mov")) else "R"
                    hits.setdefault(key, {}).setdefault(hex(off), []).append([hex(start), hex(ins.address), "%s %s" % (mn, ins.op_str), acc])
                    # pointer loaded from an instance (arrays/curves): not followed
        # ---- stores of a tracked pointer into object memory (a cache of the group/family pointer)
        if mn == "mov" and len(ops) == 2 and ops[0].type == x86.X86_OP_MEM and ops[1].type == x86.X86_OP_REG and ops[1].size == 8:
            sv = st.get(canon(ops[1].reg))
            bb = ops[0].mem.base
            if sv and sv[0] in ("FAM", "INST") and bb != 0 and canon(bb) not in ("rsp", "rbp"):
                key = "%s:%s" % (hex(sv[1]), hex(sv[2])) if sv[0] == "INST" else "%s:*" % hex(sv[1])
                hits.setdefault("CACHE", {}).setdefault(key, []).append([hex(start), hex(ins.address), "%s %s" % (mn, ins.op_str)])
        # ---- stack spills / reloads
        if mn == "mov" and len(ops) == 2:
            d, s_ = ops[0], ops[1]
            if d.type == x86.X86_OP_MEM and d.mem.base != 0 and canon(d.mem.base) in ("rsp", "rbp") and d.mem.index == 0 and s_.type == x86.X86_OP_REG:
                sv = st.get(canon(s_.reg))
                key = (canon(d.mem.base), d.mem.disp)
                if sv:
                    stack[key] = sv
                else:
                    stack.pop(key, None)
            if s_.type == x86.X86_OP_MEM and s_.mem.base != 0 and canon(s_.mem.base) in ("rsp", "rbp") and s_.mem.index == 0 and d.type == x86.X86_OP_REG:
                key = (canon(s_.mem.base), s_.mem.disp)
                sv = stack.get(key)
                if sv and d.size == 8:
                    newstate = (canon(d.reg), sv)
        # ---- G reads (manager) -> DB through [G] [mgr] [A+8]
        # handled via pseudo states: ('MGR',), ('A',)
        if mn == "mov" and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_MEM:
            m = ops[1].mem
            if m.base == x86.X86_REG_RIP and ins.address + ins.size + m.disp == G:
                newstate = (canon(ops[0].reg), ("MGR",))
            elif m.base != 0 and m.index == 0:
                b = canon(m.base)
                s = st.get(b)
                if s and s[0] == "MGR" and m.disp == 0:
                    newstate = (canon(ops[0].reg), ("A",))
                elif s and s[0] == "A" and m.disp == 8:
                    newstate = (canon(ops[0].reg), ("DB",))
        # ---- register copies / and-masks keep state
        keep = None
        if mn == "mov" and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG and ops[0].size == 8:
            sv = st.get(canon(ops[1].reg))
            if sv:
                newstate = (canon(ops[0].reg), sv)
        if mn == "and" and len(ops) == 2 and ops[1].type == x86.X86_OP_IMM and ops[1].imm in (-5, -4, -8, 0xFFFFFFFFFFFFFFFB) and ops[0].type == x86.X86_OP_REG:
            keep = canon(ops[0].reg)
        # ---- calls
        if mn == "call":
            tgt = ops[0].imm if ops and ops[0].type == x86.X86_OP_IMM else None
            if tgt == FUNC:
                for r_ in NONVOL:
                    st.pop(r_, None)
                st["rax"] = ("DB",)
                continue
            argst = {r_: st[r_] for r_ in ARGS if r_ in st and st[r_][0] in ("DB", "FAM", "INST")}
            if tgt and argst and depth < 3 and edges is not None:
                key = (tgt, tuple(sorted(argst.items())))
                if key not in seen:
                    seen.add(key)
                    edges.append((tgt, argst, depth + 1, start))
            for r_ in NONVOL:
                st.pop(r_, None)
            continue
        # ---- apply writes
        for r_ in wr_:
            nm = canon(r_)
            if nm != keep:
                st.pop(nm, None)
        if newstate:
            st[newstate[0]] = newstate[1]
        if mn == "ret":
            # continue the sweep past a ret only if no int3 follows (multi-return functions)
            nb = R.rd(ins.address + ins.size, 1)
            if nb == b"\xCC":
                break
    return n


def main():
    calls = R.call_xrefs(FUNC)
    funcs = set()
    for va, kind in calls:
        f = R.func_start(va)
        if f:
            funcs.add(f)
    for dv in R.rip_xrefs(G):
        c = R.confirm_rip(dv, G)
        if c:
            f = R.func_start(c[0])
            if f:
                funcs.add(f)
    print("root functions:", len(funcs))
    hits = {}
    edges = []
    seen = set()
    for f in sorted(funcs):
        sweep(f, None, hits=hits, edges=edges, seen=seen)
    todo = edges
    done = 0
    while todo:
        nxt = []
        for (tgt, argst, depth, parent) in todo:
            init = {r_: s for r_, s in argst.items()}
            e2 = []
            sweep(tgt, init, depth=depth, hits=hits, edges=e2, seen=seen)
            nxt += e2
            done += 1
        todo = nxt
    print("followed callees:", done)
    json.dump(hits, open(os.path.join(HERE, "c1_scan.json"), "w"))
    print("groups with field accesses:", len(hits))


if __name__ == "__main__":
    main()
