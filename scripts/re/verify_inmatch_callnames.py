"""Re-resolve every in-match callname signature (docs/re/inmatch-callnames.json) against the image and report drift.

    bash scripts/re/py.sh scripts/re/verify_inmatch_callnames.py [path/to/fc27_image.bin]

For every entry: the pattern must match exactly once; with resolve "rip" the rip-relative instruction (or the call) at
match + offset is resolved and compared with "expect"; with "none" the match itself is compared. Entries whose pattern
reads "(same as X)" reuse X's pattern. Also re-checks the djb2-xor hashes listed under "hashes" and "pid_params".
Exit code 0 when everything agrees.
"""
import json
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
if len(sys.argv) > 1:
    os.environ["FC27_IMAGE"] = sys.argv[1]
import rx  # noqa: E402
import capstone  # noqa: E402

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
cs.detail = True


def djb2x(s):
    h = 5381
    for c in s.encode("latin-1"):
        h = ((h * 33) ^ c) & 0xFFFFFFFF
    return h


doc = json.load(open(os.path.join(here, "..", "..", "docs", "re", "inmatch-callnames.json")))
sigs = doc["signatures"]
ok = True
for name, e in sigs.items():
    pat = e["pattern"]
    if pat.startswith("(same as "):
        pat = sigs[pat[len("(same as "):-1]]["pattern"]
    hits = rx.find_sig(pat, limit=5)
    want = int(e["expect"], 16)
    if len(hits) != 1:
        print("%-30s AMBIGUOUS/MISSING hits=%s" % (name, ["%x" % h for h in hits]))
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
    if name.startswith("speech_str_"):
        text = '  "%s"' % rx.cstr(got, 40)
    status = "OK" if got == want else "DRIFT: expected %x" % want
    print("%-30s match %x -> %x %s%s" % (name, match, got, status, text))
    if got != want:
        ok = False

for key, val in list(doc["hashes"].items()) + list(doc["pid_params"].items()):
    if not val.startswith("0x"):
        continue
    want = int(val.split()[0], 16)
    if djb2x(key) != want:
        print("hash %-28s %08x != %08x" % (key, djb2x(key), want))
        ok = False
# strings by address and text, vtable slots, the player-id parameter names (each one unique string in the image)
for name, e in doc.get("strings", {}).items():
    got = rx.cstr(int(e["address"], 16), 80)
    if got != e["text"]:
        print("string %-26s %s: %r != %r" % (name, e["address"], got, e["text"]))
        ok = False
for name, e in doc.get("vtables", {}).items():
    va = int(e["address"], 16)
    for slot, fn in e.get("slots", {}).items():
        got = rx.u64(va + int(slot, 16))
        if got != int(fn, 16):
            print("vtable %-26s %x+%s -> %x, expected %s" % (name, va, slot, got, fn))
            ok = False
for name, e in doc.get("player_param_names", {}).items():
    hits = ["0x%X" % h for h in rx.find_str(name, limit=3)]
    if hits != e["address"] or len(hits) != 1:
        print("param name %-22s found at %s, expected %s" % (name, hits, e["address"]))
        ok = False
print("hashes, strings, vtables and parameter names checked" if ok else "PROBLEMS")
sys.exit(0 if ok else 1)
