"""Make + check the transfer-list signatures (docs/re/transfer_lists.md) and dump the bytes the native test embeds.

    python scripts/re/sig_transfer_lists.py            # print signatures, uniqueness, resolved targets, test bytes
    python scripts/re/sig_transfer_lists.py --json     # also rewrite scripts/re/transfer_list_signatures.json

Every anchor is a function start (or an instruction inside a constructor) of FC27.exe 1.0.140.64835; "rip" entries resolve
the rip-relative operand at anchor + offset (the vtable the constructor writes).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402
from sig_jobs import make_sig  # noqa: E402

BUILD = "6AB9813C-211EF000"
# name, anchor VA, resolve, rip offset, expected target (0 = the anchor itself), note
ANCHORS = [
    ("uah_add_transfer_list", 0x147F68368, "none", 0, 0,
     "void UserActionsHandlingHelperImpl::AddToTransferList(this, playerId) 0x147F68368: TransferManager::AddToTransferList + UserTransferlisted (0x79) event"),
    ("uah_add_loan_list", 0x147F68214, "none", 0, 0,
     "void UserActionsHandlingHelperImpl::AddToLoanList(this, playerId) 0x147F68214: TransferManager::AddToLoanList + UserLoanlisted (0x7A) event"),
    ("uah_try_remove_from_list", 0x147F8E300, "none", 0, 0,
     "bool UserActionsHandlingHelperImpl::TryToRemoveFromList(this, playerId, bool loanList) 0x147F8E300: TransferManager::RemoveFromLists + the events"),
    ("uah_vtable", 0x147F0F267, "rip", 0, 0x14B029440,
     "UserActionsHandlingHelperImpl vtable (0x14B029440): the lea rax,[rip+..] at 0x147F0F267 of the CareerDaoFactoryImpl ctor (sub-object at +0x478, managers at +0x480)"),
    ("dao_vtable", 0x147F0EB10, "rip", 0x22, 0x14B025C48,
     "CareerDaoFactoryImpl vtable (0x14B025C48): the lea rax,[rip+..] at +0x22 of its ctor 0x147F0EB10 (the object [[comm+0x20]+0x30])"),
    ("tm_vtable", 0x147C26450, "rip", 0x31, 0x14B0055A8,
     "TransferManager vtable (0x14B0055A8): the lea rax,[rip+..] at +0x31 of its ctor 0x147C26450 (manager type 127, 0x2FA0 bytes)"),
    ("pcm_vtable", 0x147E54FB0, "rip", 0x6, 0x14B01E240,
     "PlayerContractManager vtable (0x14B01E240): the lea rax,[rip+..] at +0x6 of its ctor 0x147E54FB0 (manager type 77, 0x458 bytes)"),
    ("um_vtable", 0x147AB2EB8, "rip", 0x25, 0x14AFDF150,
     "UserManager vtable (0x14AFDF150): the lea rax,[rip+..] at +0x25 of its ctor 0x147AB2EB8 (manager type 129, 0xB20 bytes; "
     "+0x10 user count, +0x14 active user index, +0x18 users: new[] array of 0x348-byte users, header -0x10 = count; "
     "user +0x1F4 = the user's team id; proven live 2026-10-04)"),
]


def main():
    out = {"build": BUILD, "game": "FC27.exe", "signatures": {}}
    ok = True
    for name, va, resolve, off, target, note in ANCHORS:
        sig, n = make_sig(va, 24, 120)
        if n != 1:
            print("!! %s: %d matches" % (name, n))
            ok = False
        if resolve == "rip":
            t = rx.rip_target(va + off)
            if t != target:
                print("!! %s: rip at %x resolves to %s, expected %x" % (name, va + off, ("%x" % t) if t else "none", target))
                ok = False
        print("%-26s %x  matches=%d  %s" % (name, va, n, sig))
        out["signatures"][name] = {"pattern": sig, "resolve": resolve, "offset": off, "note": note, "va": "0x%X" % va,
                                   "target": "0x%X" % (target or va)}
    if "--json" in sys.argv:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "transfer_list_signatures.json")
        with open(path, "w") as f:
            json.dump(out, f, indent=1)
        print("written", path)
    if "--bytes" in sys.argv:
        for name, va, resolve, off, target, note in ANCHORS:
            n = max(off + 8, 64)
            b = rx.read(va, n)
            print("// %s: FC27.exe bytes at 0x%X (%d bytes)" % (name, va, n))
            for i in range(0, n, 16):
                print("    " + ", ".join("0x%02X" % x for x in b[i:i + 16]) + ",")
    print("ALL UNIQUE" if ok else "PROBLEMS")


if __name__ == "__main__":
    main()
