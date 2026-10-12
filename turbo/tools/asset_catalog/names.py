"""Readable names for catalog ids (design 3.5). The first source that names an id wins:
1 callname masters (master:<lang>), 2 FC 26 workbook names sheets (workbook) and extra_player_names.json (extra_names),
3 EBX head slug (slug), 4 kit folder slug (kit_slug, teams), 5 managers.csv (managers). The GUI puts the live DB name
in front of all of them and falls back to "Head #id" etc. for ids nobody names."""
import csv
import glob
import json
import os

from families import NON_TEAM_SLUG, slug_name


class Names:
    def __init__(self):
        self.tables = {"player": {}, "team": {}, "commentary": {}}
        self.counts = {}

    def offer(self, table, i, name, source):
        """Keep the first non-empty name offered for (table, id). Returns True when it was taken."""
        if name is None:
            return False
        name = " ".join(str(name).split())
        if not name:
            return False
        t = self.tables[table]
        key = str(int(i))
        if key in t:
            return False
        t[key] = [name, source]
        self.counts[source] = self.counts.get(source, 0) + 1
        return True

    def restrict(self, table, wanted):
        """Drop names of ids the catalog does not list (keeps the file small)."""
        t = self.tables[table]
        for k in [k for k in t if int(k) not in wanted]:
            del t[k]

    def as_json(self):
        return {t: {k: v for k, v in sorted(m.items(), key=lambda kv: int(kv[0]))} for t, m in self.tables.items()}


def load_masters(folder):
    """Callname masters, FC 27 ones first: [(lang, {id: name}, {generic id: name})]."""
    out = []
    for p in sorted(glob.glob(os.path.join(folder, "*.json"))):
        try:
            with open(p, encoding="utf-8") as f:
                m = json.load(f)
        except (OSError, ValueError):
            continue
        if not isinstance(m, dict) or "names" not in m:
            continue
        lang = m.get("language") or os.path.splitext(os.path.basename(p))[0]
        out.append((m.get("game") != "fc27", lang, m.get("names") or {}, m.get("generic_names") or {}))
    out.sort(key=lambda x: (x[0], x[1]))
    return [(lang, n, g) for _, lang, n, g in out]


def load_workbook_names(path):
    """A JSON dump of the FC 26 workbooks' names sheets: {workbook: {"names": {id: name}}} or {id: name}."""
    if not path or not os.path.isfile(path):
        return {}
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    merged = {}
    if isinstance(d, dict):
        for v in d.values():
            if isinstance(v, dict) and isinstance(v.get("names"), dict):
                for k, n in v["names"].items():
                    merged.setdefault(k, n)
            elif isinstance(v, str):
                merged = {k: n for k, n in d.items() if isinstance(n, str)}
                break
    return merged


def load_extra_names(path):
    if not path or not os.path.isfile(path):
        return {}
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    if isinstance(d, dict) and isinstance(d.get("names"), dict):
        d = d["names"]
    return {k: v for k, v in d.items() if isinstance(v, str)} if isinstance(d, dict) else {}


def load_managers(path):
    """turbo_dev managers.csv: info.managerid -> "First Last"."""
    out = {}
    if not path or not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        for r in csv.DictReader(f):
            try:
                i = int(r.get("info.managerid") or "")
            except ValueError:
                continue
            n = " ".join(x for x in (r.get("info.firstname", ""), r.get("info.surname", "")) if x)
            if n:
                out[i] = n
    return out


def build_names(player_ids, team_ids, commentary_ids, heads, kits, masters=(), workbook=None, extra=None, managers=None):
    """heads: {id: {"slugs": [...]}}; kits: {id: {"slug": ...}}. Returns a Names object restricted to the catalog ids."""
    N = Names()
    for lang, names, generic in masters:
        src = "master:%s" % lang
        for k, n in names.items():
            if k.isdigit() and int(k) in player_ids:
                N.offer("player", k, n, src)
        for k, n in generic.items():
            if k.isdigit() and int(k) in commentary_ids:
                N.offer("commentary", k, n, src)
    for src, table in (("workbook", workbook or {}), ("extra_names", extra or {})):
        for k, n in table.items():
            if str(k).isdigit() and int(k) in player_ids:
                N.offer("player", k, n, src)
    for i, h in heads.items():
        named = [s for s in h["slugs"] if s != "generic" and not s.startswith("generic_")]
        if named:
            N.offer("player", i, slug_name(sorted(named)[0]), "slug")
    for i, k in kits.items():
        if i in team_ids and not NON_TEAM_SLUG.match(k["slug"]):
            N.offer("team", i, slug_name(k["slug"]), "kit_slug")
    for i, n in (managers or {}).items():
        if i in player_ids:
            N.offer("player", i, n, "managers")
    N.restrict("player", player_ids)
    N.restrict("team", team_ids)
    N.restrict("commentary", commentary_ids)
    return N
