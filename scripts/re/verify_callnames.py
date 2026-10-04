"""Re-resolve every commentary-audio signature (docs/re/E4-callnames-signatures.json) against the image and report drift.

    bash scripts/re/py.sh scripts/re/verify_callnames.py [path/to/fc27_image.bin]

For every entry: the pattern must match exactly once; with resolve "rip" the rip-relative instruction at match + offset
is resolved and compared with "expect"; with "none" the match itself is compared. Entries whose pattern reads
"(same as X)" reuse X's pattern. Exit code 0 when everything agrees.
"""
import json
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
if len(sys.argv) > 1:
    os.environ["FC27_IMAGE"] = sys.argv[1]
import rx  # noqa: E402
import capstone  # noqa: E402

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
cs.detail = True

doc = json.load(open(os.path.join(here, "..", "..", "docs", "re", "E4-callnames-signatures.json")))
sigs = doc["signatures"]
ok = True
for name, e in sigs.items():
    pat = e["pattern"]
    if pat.startswith("(same as "):
        pat = sigs[pat[len("(same as "):-1]]["pattern"]
    hits = rx.find_sig(pat, limit=5)
    want = int(e["expect"], 16)
    if len(hits) != 1:
        print("%-34s AMBIGUOUS/MISSING hits=%s" % (name, ["%x" % h for h in hits]))
        ok = False
        continue
    match = hits[0]
    got = match
    if e.get("resolve") == "rip":
        at = match + int(e.get("offset", 0))
        ins = next(cs.disasm(rx.rb(at, 16), at))
        if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            got = int(ins.op_str, 16)
        else:
            got = ins.address + ins.size + ins.disp
    text = ""
    if name.startswith("commentary_str_"):
        text = '  "%s"' % rx.cstr(got, 40)
    status = "OK" if got == want else "DRIFT: expected %x" % want
    print("%-34s match %x -> %x %s%s" % (name, match, got, status, text))
    if got != want:
        ok = False
vt = int(sigs["commentary_service_vtable"]["expect"], 16)
print("service vtable %x: slot1 %x slot3 %x slot12 %x" % (vt, rx.u64(vt + 8), rx.u64(vt + 0x18), rx.u64(vt + 0x60)))
nvt = int(sigs["commentary_names_vtable"]["expect"], 16)
print("names vtable   %x: slot24 %x" % (nvt, rx.u64(nvt + 0xC0)))
sys.exit(0 if ok else 1)
