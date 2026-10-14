import json, sys
sys.path.insert(0, '.')
import c1_sig as S
h = json.load(open('c1_scan.json'))
h2 = json.load(open('c1_scan2.json'))
inv = json.load(open(r'C:\FC 27 Live Editor\turbo_dev\2.0\wt-26-consumers\docs\re\attrib_inventory_6AC07E31.json'))['groups']
k = json.load(open(r'C:\FC 27 Live Editor\turbo_dev\2.0\wt-26-consumers\turbo\package\turbo\tuning\knobs_6AC07E31.json'))['knobs']
out = {}
for x in k:
    g = inv[x['collection']]
    key = "%s:%s" % (hex(g['family_slot']), hex(g['group_slot']))
    for src in (h,):
        for (f, va, txt, acc) in src.get(key, {}).get(x['offset'], [])[:2]:
            s = S.make_sig(int(f, 16), int(va, 16))
            out.setdefault(x['id'], []).append(dict(s or {}, func=f, read=va, txt=txt))
# manual sites: (id, func start, read va)
manual = [("pressing.partial_cooldownmultiplier", 0x14164F338, 0x14164F444), ("pressing.partial_highworkratetime", 0x14164F5A8, 0x14164F62F), ("pressing.partial_lowworkratetime", 0x14164F5A8, 0x14164F638), ("pressing.partial_min_penalty", 0x14164F338, 0x14164F41C), ("pressing.partial_max_penalty", 0x14164F338, 0x14164F425), ("lineband.deep_max", 0x141DAAD24, 0x141DAAD6A),
          ("lineband.balanced_min", 0x141DAAD24, 0x141DAAD7F), ("lineband.balanced_max", 0x141DAAD24, 0x141DAAD71),
          ("lineband.high_min", 0x141DAAD24, 0x141DAAD86), ("lineband.high_max", 0x141DAAD24, 0x141DAAD76),
          ("lineband.aggressive_min", 0x141DAAD24, 0x141DAAD8C)]
for kid, f, va in manual:
    s = S.make_sig(f, va)
    out.setdefault(kid, []).append(dict(s or {}, func=hex(f), read=hex(va), txt="manual"))
json.dump(out, open('c1_sig.json', 'w'), indent=1)
for kid, v in out.items():
    for s in v:
        print(kid, s.get('func'), s.get('read'), 'hits', s.get('hits'), 'insns', s.get('insns'), (s.get('sig') or '')[:70])
