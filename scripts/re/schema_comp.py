"""Lists the competition-related tables of the user's in-game schema dump (turbo/le27/fc27_db_schema.json)."""
import json
import os
import sys

p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "turbo", "le27", "fc27_db_schema.json")
if not os.path.exists(p):
    p = r"C:\FC 27 Live Editor\turbo_dev\FC27-Editor-Turbo\turbo\le27\fc27_db_schema.json"
s = json.load(open(p))
t = s["tables"]
for n in ("compobj", "leagues", "competition", "leagueteamlinks", "settings", "compids"):
    if n in t:
        print(n, [f["name"] for f in t[n]["fields"]][:40])
    else:
        print(n, "MISSING")
print([k for k in t if "comp" in k or "leag" in k])
