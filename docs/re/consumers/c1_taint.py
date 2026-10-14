"""c1_taint.py <read_va_hex> [max_ins] - forward taint of the value read by the instruction at read_va (OLD image). Prints the instructions that
consume it (arith), the stores of tainted data (sinks: [base+disp]) and compares. Linear sweep, no CFG: [A]. Read-only."""
import sys, os
sys.path.insert(0, r"C:\FC 27 Live Editor\turbo_output\assessment_20261013\T1")
import t1_re as R
from capstone import x86
CS = R.cs()

def canon(r):
    n = CS.reg_name(r)
    if n.startswith(("xmm", "ymm")):
        return "xmm" + n[3:]
    m = {"eax": "rax", "ebx": "rbx", "ecx": "rcx", "edx": "rdx", "esi": "rsi", "edi": "rdi", "ebp": "rbp", "esp": "rsp", "ax": "rax", "bx": "rbx", "cx": "rcx", "dx": "rdx", "al": "rax", "bl": "rbx", "cl": "rcx", "dl": "rdx"}
    if n in m:
        return m[n]
    if n.startswith("r") and n[1:-1].isdigit() and n[-1] in "dwb":
        return n[:-1]
    return n

def run(va, maxins=250, out=print):
    taint = set()
    stack = set()
    first = True
    n = 0
    for ins in CS.disasm(R.rd(va, maxins * 12), va, maxins):
        n += 1
        ops = ins.operands
        rd_, wr_ = ins.regs_access()
        rds = {canon(r) for r in rd_}
        wrs = {canon(r) for r in wr_}
        txt = "%s %s" % (ins.mnemonic, ins.op_str)
        if first:
            first = False
            taint |= wrs - {"rsp", "rflags", "eflags"}
            out("%s  SOURCE  %s -> taint %s" % (R.hx(ins.address), txt, sorted(taint)))
            continue
        # memory operands
        memrd = [op for op in ops if op.type == x86.X86_OP_MEM]
        stack_rd = any((canon(op.mem.base) in ("rsp", "rbp") and (canon(op.mem.base), op.mem.disp) in stack) for op in memrd if op.mem.base)
        src_t = bool(rds & taint) or stack_rd
        # store of tainted reg into memory
        if ins.mnemonic.startswith(("mov", "vmov")) and ops and ops[0].type == x86.X86_OP_MEM:
            srcreg = ops[1].type == x86.X86_OP_REG and canon(ops[1].reg) in taint
            if srcreg:
                b = canon(ops[0].mem.base) if ops[0].mem.base else ""
                if b in ("rsp", "rbp"):
                    stack.add((b, ops[0].mem.disp))
                    out("%s  spill   %s" % (R.hx(ins.address), txt))
                else:
                    out("%s  SINK    %s" % (R.hx(ins.address), txt))
                continue
        if ins.mnemonic.startswith(("call",)):
            tg = ops[0].imm if ops and ops[0].type == x86.X86_OP_IMM else None
            out("%s  call    %s   args tainted: %s" % (R.hx(ins.address), ins.op_str, sorted(taint & {"rcx", "rdx", "r8", "r9", "xmm0", "xmm1", "xmm2", "xmm3"})))
            taint -= {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"} | {"xmm%d" % i for i in range(6)}
            if not taint and not stack:
                break
            continue
        if src_t:
            newt = wrs - {"rsp", "rflags", "eflags", "rip"}
            if ins.mnemonic.startswith(("comis", "ucomis", "vcomis", "vucomis", "cmp", "test")):
                out("%s  CMP     %s" % (R.hx(ins.address), txt))
            else:
                out("%s  use     %s" % (R.hx(ins.address), txt))
            taint |= newt
        else:
            taint -= (wrs & taint)
        if ins.mnemonic in ("ret",) :
            break
        if ins.mnemonic == "int3":
            break
        if not taint and not stack:
            break
    return

if __name__ == "__main__":
    run(int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 250)
