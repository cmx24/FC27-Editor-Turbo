"""c1_index.py - linear-sweep index of .text1 (OLD image): for every memory operand [base+disp] (base not rip/rsp/rbp) with disp in WANT, record
(func_start_guess, va, mnemonic text, base reg). Chunks start after int3 padding so instruction alignment is real. Read-only."""
import sys, json, os, time, re
sys.path.insert(0, r"C:\FC 27 Live Editor\turbo_output\assessment_20261013\T1")
import t1_re as R
from capstone import x86
HERE = os.path.dirname(os.path.abspath(__file__))
inv = json.load(open(r'C:\FC 27 Live Editor\turbo_dev\2.0\wt-26-consumers\docs\re\attrib_inventory_6AC07E31.json'))['groups']
knobs = json.load(open(r'C:\FC 27 Live Editor\turbo_dev\2.0\wt-26-consumers\turbo\package\turbo\tuning\knobs_6AC07E31.json'))['knobs']
WANT = {}
for k in knobs:
    off = int(k['offset'], 16)
    if off >= 0x100:
        WANT.setdefault(off, []).append(k['id'])
zd = inv['gp_positioning_zonal_defense']['fields']
for f in zd:
    if f[1] >= 0x600:
        WANT.setdefault(f[1], []).append('zonal.' + f[0])
print("wanted disps:", len(WANT))
CS = R.cs()
CS.detail = True
m = R.img()
LO, HI = R.va2off(0x140001000), R.va2off(0x148B69000)
buf = bytes(m[LO:HI])
print("loaded", len(buf))
# chunk boundaries: runs of >= 3 CC
chunks = []
for mo in re.finditer(rb"\xCC{3,}", buf):
    chunks.append(mo.end())
chunks = [0] + chunks
print("chunks", len(chunks))
sites = {}
t0 = time.time()
n = 0
for ci, s in enumerate(chunks):
    e = chunks[ci + 1] if ci + 1 < len(chunks) else len(buf)
    # find the end of this chunk's code: up to the next padding start
    va = 0x140001000 + s
    code = buf[s:e]
    for ins in CS.disasm(code, va):
        n += 1
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM and op.mem.disp in WANT and op.mem.base not in (0, x86.X86_REG_RIP, x86.X86_REG_RSP, x86.X86_REG_RBP):
                sites.setdefault(op.mem.disp, []).append((ins.address, "%s %s" % (ins.mnemonic, ins.op_str), va))
    if ci % 20000 == 0:
        print(ci, n, time.time() - t0, flush=True)
json.dump({hex(k): v for k, v in sites.items()}, open(os.path.join(HERE, "c1_index.json"), "w"))
print("done", n, time.time() - t0)
