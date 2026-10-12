"""EBX name census: classify bundle-manifest EBX names into catalog families (regexes from the asset research
ebx_census.py SPECS, the head regex and the critic's corrections: boots content vs content_ts, descriptive accessory
folders). A toc's result is a small JSON-able dict, so it can be cached per toc (size + mtime)."""
import re

# family -> regex on the EBX name; group 1 is the id unless noted
SPECS = [
    ("head3d", r"^content/character/player/player_\d+/([a-z0-9_]+?)_(\d+)/var_(\d+)(/.*|_.*)$"),
    ("hair", r"^content/character/generichair/hairgen_(\d+)[/_]"),
    ("facialhair", r"^content/character/facialhair/facialhair_(\d+)[/_]"),
    ("boots", r"^content/character/shoe/shoe_(\d+)[/_]"),
    ("boots_ts", r"^content_ts/character/shoe/shoe_(\d+)[/_]"),
    ("gkglove", r"^content/character/gkglove/gkglove_(\d+)[/_]"),
    ("glove", r"^content/character/glove/glove_(\d+)/"),
    ("accessory", r"^content(?:_ts)?/character/(?:base_assets/)?accessory/([a-z0-9_\-]+?)_(\d+)/"),
    ("tattoo", r"^content/character/tattoo/tattoo_(\d+)_(?:color|hotspot)"),
    ("eyebrow", r"^content/character/cranium/presets/decals/eyebrows/eyebrows_(\d+)_preset$"),
    ("skinsurface", r"^content/character/cranium/presets/decals/regional_surface/regional_surface_region\d+_surface_(\d+)_preset$"),
    ("ball", r"^content/character/ball/ball_(\d+)[/_]"),
    ("kit", r"^content/character/kit/kit_\d+/([a-z0-9_]+?)_(\d+)/(?:loc\d+_)?([a-z_]+?)_(\d+)_(\d+)/"),
]
RX = [(f, re.compile(r)) for f, r in SPECS]
_DIGITS = re.compile(r"\d+")

# Toc letters for head3d.toc: L = launch bundle, S = contentsb
HEAD_TOC_LETTER = {"contentlaunchsb": "L", "contentsb": "S"}


def toc_letter(toc_rel):
    stem = toc_rel.replace("\\", "/").rsplit("/", 1)[-1]
    stem = stem[:-4] if stem.endswith(".toc") else stem
    return HEAD_TOC_LETTER.get(stem, "")


def _kind(rest):
    tail = rest.split("/")[-1] if "/" in rest else rest
    return _DIGITS.sub("N", tail)


def classify(names, acc=None):
    """Fold EBX names into a per-toc result: {family: {id: ...}}. Heads keep slugs, vars and the proof kinds."""
    if acc is None:
        acc = {}
    for e in names:
        if "/character/" not in e:
            continue
        for fam, rx in RX:
            m = rx.match(e)
            if not m:
                continue
            if fam == "head3d":
                slug, hid, var, rest = m.groups()
                h = acc.setdefault("head3d", {}).setdefault(hid, {"slugs": [], "vars": [], "face": 0, "morph": 0, "star": 0, "longhair": 0})
                if slug not in h["slugs"]:
                    h["slugs"].append(slug)
                v = int(var)
                if v not in h["vars"]:
                    h["vars"].append(v)
                k = _kind(rest)
                if k == "face_N_N_N_color":
                    h["face"] = 1
                elif k == "headmorphpreset_N_N_N":
                    h["morph"] = 1
                elif k.startswith(("_starhead_brt", "_launch_starhead_brt", "_strand")):
                    h["star"] = 1
                if "longhair" in rest or "long_hair" in rest:
                    h["longhair"] = 1
            elif fam == "kit":
                slug, tid, ktype, kt, yr = m.groups()
                k = acc.setdefault("kit", {}).setdefault(tid, {"slug": slug, "kits": []})
                name = "%s_%s_%s" % (ktype, kt, yr)
                if name not in k["kits"]:
                    k["kits"].append(name)
            elif fam == "accessory":
                desc, aid = m.groups()
                a = acc.setdefault("accessory", {})
                if aid not in a:
                    a[aid] = desc
            else:
                acc.setdefault(fam, {})[m.group(1)] = 1
            break
    return acc


def merge(results):
    """Merge per-toc results [(toc_rel, result)] into one census: {family: {int id: info}}."""
    out = {}
    for toc_rel, r in results:
        letter = toc_letter(toc_rel)
        for fam, ids in r.items():
            F = out.setdefault(fam, {})
            for sid, info in ids.items():
                i = int(sid)
                if fam == "head3d":
                    h = F.setdefault(i, {"slugs": [], "vars": set(), "face": 0, "morph": 0, "star": 0, "longhair": 0,
                                         "toc": set(), "tocs": set()})
                    for s in info["slugs"]:
                        if s not in h["slugs"]:
                            h["slugs"].append(s)
                    h["vars"].update(info["vars"])
                    for k in ("face", "morph", "star", "longhair"):
                        h[k] = h[k] or info.get(k, 0)
                    if letter:
                        h["toc"].add(letter)
                    h["tocs"].add(toc_rel)
                elif fam == "kit":
                    k = F.setdefault(i, {"slug": info["slug"], "kits": set()})
                    k["kits"].update(info["kits"])
                elif fam == "accessory":
                    F.setdefault(i, info)
                else:
                    F[i] = 1
    return out


def head_is_generic(h):
    return all(s == "generic" or s.startswith("generic_") for s in h["slugs"])


def head_proven(h):
    """Write proof for headclasscode 0: face texture + morph preset + starhead bundle."""
    return bool(h["face"] and h["morph"] and h["star"])


# Kit folder slugs that are not clubs (critic C3): never used as team names
NON_TEAM_SLUG = re.compile(r"^(_default|otp_generic.*|goalkeeper_.*|referee_.*|uefa_walkout.*)$")


def slug_name(slug):
    """EBX folder slug -> readable name in EA's word order: 'doe_john' -> 'Doe John'."""
    parts = [p for p in slug.split("_") if p]
    return " ".join(p[:1].upper() + p[1:] for p in parts)
