"""Disassemble: disas.py <va_hex> [count] [--strings]   (annotates rip-relative targets that are C strings / pointers)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_capture as rx

va = int(sys.argv[1], 16)
n = int(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("-") else 60
for ins in rx.md.disasm(rx.rd(va, n * 15), va, n):
    note = ""
    if "rip" in ins.op_str or ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
        # resolve rip-relative / rel32 target
        try:
            if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
                if ins.op_str.startswith("0x"):
                    t = int(ins.op_str, 16)
                    note = "-> %x" % t
                    if ins.mnemonic == "call":
                        pass
            if "rip" in ins.op_str:
                import re
                m = re.search(r"rip ([+-]) 0x([0-9a-f]+)", ins.op_str)
                if m:
                    d = int(m.group(2), 16) * (1 if m.group(1) == "+" else -1)
                    t = ins.address + ins.size + d
                    s = rx.cstr(t, 60)
                    printable = all(32 <= ord(c) < 127 for c in s) and len(s) >= 3
                    note = "-> %x" % t + ("  \"%s\"" % s if printable else "  q=%x" % rx.u64(t))
        except Exception as e:
            note = "?"
    print("%x  %-8s %-44s %s" % (ins.address, ins.mnemonic, ins.op_str, note))
