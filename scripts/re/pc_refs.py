"""Step 2: who references the player-capture strings (code rip-refs + data pointer tables)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_capture as rx
targets = [0x1496e5728, 0x1496e53e0, 0x14aee00c8, 0x14aedfe60, 0x14aedfe50, 0x14b0178e8, 0x149799650, 0x14b0afb70, 0x14ac3c218,
           0x14b08d0d0, 0x14affa060, 0x14aff9c08, 0x14aff9a50, 0x14aff9ea8, 0x14a95a388, 0x14a95a1a0, 0x14a9496e8, 0x14ada8148,
           0x14ada8180, 0x1496eef58, 0x149799638, 0x14961c3a8, 0x1496e4d20, 0x1496e5ab8, 0x14a95a348, 0x14a95a160]
if len(sys.argv) > 1:
    targets = [int(a, 16) for a in sys.argv[1:]]
rx.show_refs(targets, label=lambda t: rx.cstr(t, 80))
