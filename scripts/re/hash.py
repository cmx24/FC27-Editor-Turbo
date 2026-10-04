"""hash.py <name>... : djb2 variants (seed 0x1505, the game's FE message id hash) of each name, with a lookup of the
ids 0xb1fee06d 0x694ce8b7 0x73804979 seen in the PlayerCaptureController listener GetTypeId slots."""
import sys


def djb2_add(s, seed=0x1505):
    h = seed
    for c in s.encode("latin-1"):
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def djb2_xor(s, seed=0x1505):
    h = seed
    for c in s.encode("latin-1"):
        h = ((h * 33) ^ c) & 0xFFFFFFFF
    return h


def fnv1(s):
    h = 0x811C9DC5
    for c in s.encode("latin-1"):
        h = ((h * 0x01000193) ^ c) & 0xFFFFFFFF
    return h


def fnv1a(s):
    h = 0x811C9DC5
    for c in s.encode("latin-1"):
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


wanted = {0xb1fee06d, 0x694ce8b7, 0x73804979}
for n in sys.argv[1:]:
    vals = {"djb2+": djb2_add(n), "djb2^": djb2_xor(n), "fnv1": fnv1(n), "fnv1a": fnv1a(n)}
    hit = [k for k, v in vals.items() if v in wanted]
    print("%-48s %s %s" % (n, " ".join("%s=%08x" % kv for kv in vals.items()), ("<== MATCH " + ",".join(hit)) if hit else ""))
