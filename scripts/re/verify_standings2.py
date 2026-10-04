"""Second pass: the counter writes of the incremental standings update, the settings lookup, the service-registry
Get path and the FCEInterfaceImpl layout."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from rx import *  # noqa

print("--- standings update, second half (0x148a5be10..)")
print(dis(0x148a5be10, 110))

print("\n--- settings lookup 0x148a4a208 (DC, compObjId, settingId, out)")
print(dis(0x148a4a208, 90))

print("\n--- FCEInterfaceImpl ctor 0x148a16b20")
print(dis(0x148a16b20, 40))

print("\n--- Get call site 0x147c15b9c and registry global users")
print(dis(0x148a0412d - 0x20, 20))
print(dis(0x148935308 - 0x10, 12))

print("\n--- registry global 0x14c2a8590 rip refs")
r = rip_refs([0x14c2a8590])
print(["%#x" % x for x in r[0x14c2a8590]])

print("\n--- ManagerHub::Init 0x148a2ee24")
print(dis(0x148a2ee24, 70))

# the plugin ids Live Editor uses: ENUM_djb2IFCEInterface_CLSS
for s in ("IFCEInterface", "FCEInterfaceImpl", "FCE::FCEInterfaceImpl", "FCEDataManager", "FCE::DataManager"):
    print("str", s, ["%#x" % h for h in find_str(s)])
