import json,sys
hits=json.load(open('c1_scan.json'))
inv=json.load(open(r'C:\FC 27 Live Editor\turbo_dev\2.0\wt-26-consumers\docs\re\attrib_inventory_6AC07E31.json'))['groups']
k=json.load(open(r'C:\FC 27 Live Editor\turbo_dev\2.0\wt-26-consumers\turbo\package\turbo\tuning\knobs_6AC07E31.json'))['knobs']
out=[]
for x in sorted(k,key=lambda x:x['rank']):
    g=inv[x['collection']]
    key="%s:%s"%(hex(g['family_slot']),hex(g['group_slot']))
    off=int(x['offset'],16) if isinstance(x['offset'],str) else x['offset']
    h=hits.get(key,{})
    fh=h.get(hex(off),[])
    funcs=sorted({a[0] for a in fh})
    allf=sorted({a[0] for v in h.values() for a in v})
    out.append((x['rank'],x['id'],key,hex(off),len(fh),funcs[:6],len(allf)))
    print(x['rank'],x['id'],key,hex(off),'reads',len(fh),'funcs',funcs[:5],'groupfuncs',len(allf))
