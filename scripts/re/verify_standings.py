"""Re-verifies the FCE standings/fixtures notes (docs/re/standings-fixtures-notes.md) against the current image.

Run:  python scripts/re/verify_standings.py            (prints each check; nothing is written)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from rx import *  # noqa

print("image", IMG, "size %#x" % SIZE)
for s in SECTIONS:
    print("  %-10s va=%#x size=%#x ch=%#x" % s)

# 1. reflection names of StandingData / FixtureData
for name in ["mHomeWins", "mHomeGoalsAgainst", "mAwayGoalsAgainst", "mPoints", "mHomeStandingId", "mGameCompletion",
             "StandingsDataList", "FixtureDataList", "SettingsDataList", "fce_standings", "fce_fixtures",
             "FCEI::RequestGetStandings", "FCEI::RequestUpdateMatchResult", "FCE::DataSorter"]:
    hits = find_str(name)
    print("str %-32s %s" % (name, ["%#x" % h for h in hits[:6]]))

# 2. the reflection visitor of StandingData (0x148a0d230 in the notes): disassemble a bit
for va in (0x148a0d230, 0x148a0cb20):
    print("\n--- reflection %#x" % va)
    print(dis(va, 24))

# 3. DataManager ctor (0x148a1661c): list offsets
print("\n--- DataManager ctor 0x148a1661c")
print(dis(0x148a1661c, 60))

# 4. GetItem signature
sig_getitem = "45 33 C0 44 8B CA 85 D2 78 32 4C 8B 11 48 B8 AB AA AA AA AA AA AA 2A 48 8B 49 08"
print("\nGetItem sig hits", ["%#x" % h for h in find_sig(sig_getitem, limit=5)])
print(dis(0x148a2d1d4, 20))

# 5. RequestGetStandings handler signature
sig_rgs = "48 8B 41 18 4C 8B EA 8B 5A 20 4C 8B F9 8B D3 89 5C 24 30 4C 8B 70 18 49 8B 8E 88 00 00 00 E8"
print("\nRequestGetStandings sig hits", ["%#x" % h for h in find_sig(sig_rgs, limit=5)])
print(dis(0x148a54b00, 24))

# 6. service registry global signature
sig_reg = "48 8B 0D ?? ?? ?? ?? BA 9A 3B 61 0A 48 8B 01 FF 50 (30|38)"
hits = find_sig(sig_reg, limit=10)
print("\nservice registry sig hits", ["%#x" % h for h in hits])
for h in hits:
    d = i32(h + 3)
    print("  ref at %#x -> global %#x" % (h, h + 7 + d))
sig_get = "BA 9A 3B 61 0A 48 8B 01 FF 50 40"
print("Get(0xA613B9A) sig hits", ["%#x" % h for h in find_sig(sig_get, limit=10)])
print(dis(0x147c15b90, 16))

# 7. incremental standings update on match result (0x148a5bc04)
print("\n--- standings update 0x148a5bc04")
print(dis(0x148a5bc04, 120))

# 8. fixture score write (0x148a5bb40)
print("\n--- fixture score write 0x148a5bb40")
print(dis(0x148a5bb40, 50))

# 9. vtables
for name, va in (("DataManager vtable", 0x14b180ca8), ("FCEInterfaceImpl vtable", 0x14b180df0), ("ManagerHub vtable", 0x14b180c28),
                 ("StandingsManager vtable", 0x14b1852e8)):
    print("%s %#x: %s" % (name, va, ["%#x" % u64(va + 8 * i) for i in range(8)]))

# 10. settings lookup 0x148a4e5bc
print("\n--- comp settings lookup 0x148a4e5bc")
print(dis(0x148a4e5bc, 60))
