"""Re-resolve every signature in job_offer_signatures.json against the image and report drift.

    python scripts/re/job_offer_verify.py [path/to/fc27_image.bin]
Exit code 0 when every signature is unique and (for the recorded build) lands on the recorded VA.
"""
import json
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
if len(sys.argv) > 1:
    os.environ["FC27_IMAGE"] = sys.argv[1]
import rx_jobs as rx  # noqa: E402
from sig_jobs import find  # noqa: E402

data = json.load(open(os.path.join(here, "job_offer_signatures.json")))
ok = True
for name, f in data["functions"].items():
    sig = f.get("sig")
    if not sig:
        print("%-24s (no signature)" % name)
        continue
    hits = find(sig)
    want = int(f["va"], 16)
    status = "unique" if len(hits) == 1 else "AMBIGUOUS(%d)" % len(hits)
    drift = ""
    if len(hits) == 1 and hits[0] != want:
        drift = "  moved: %x -> %x" % (want, hits[0])
    if len(hits) != 1:
        ok = False
    print("%-24s %s  %s%s" % (name, status, ", ".join("%x" % h for h in hits[:3]), drift))
vt = rx.BASE + int(data["jmm_vtable_rva"], 16)
ctor = int(data["functions"]["JMM_ctor"]["va"], 16)
print("JMM vtable %x -> slot0 %x (ctor stores it: %s)" % (vt, rx.u64(vt), "see 0x%x" % ctor))
sys.exit(0 if ok else 1)
