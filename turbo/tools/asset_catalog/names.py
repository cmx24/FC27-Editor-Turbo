"""Readable names for catalog ids (design 3.5). The first source that names an id wins:
players: 1 callname masters (master:<lang>), 2 FC 26 workbook names sheets (workbook) and extra_player_names.json
(extra_names), 3 EBX head slug (slug), 4 managers.csv (managers);
teams: 1 the kind of a non-team kit / crest id (kind: default, generic, goalkeeper, referee, walk-out), 2 the game's
TeamName_<id> language strings from a dump (loc_strings), 3 kit folder slug (kit_slug), 4 Live Editor's
custom_team_names.csv (le_team_names), 5 Turbo's team_names.json (turbo_team_names).
The GUI puts the live DB name in front of all of them and falls back to "Head #id" etc. for ids nobody names."""
import csv
import glob
import json
import os
import re

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


TEAM_KEY = re.compile(r"^TeamName_(\d+)$")


def load_loc_team_names(paths):
    """Dumps of the game's language strings (tab separated: string id, text[, hash], a header line allowed; e.g. an FC
    editor tool's LanguageStrings export): TeamName_<id> -> the club's full name. Later files fill only gaps."""
    out = {}
    for p in paths or ():
        try:
            f = open(p, encoding="utf-8-sig", errors="replace")
        except OSError:
            continue
        with f:
            for line in f:
                parts = line.rstrip("\r\n").split("\t")
                if len(parts) < 2:
                    continue
                m = TEAM_KEY.match(parts[0].strip())
                text = parts[1].strip()
                if m and text and not text.startswith("TeamName_"):
                    out.setdefault(int(m.group(1)), text)
    return out


def load_le_team_names(path):
    """Live Editor's extensions\\global\\custom_team_names.csv ("key;value", TeamName_<id> rows)."""
    out = {}
    if not path or not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            key, sep, value = line.rstrip("\r\n").partition(";")
            m = TEAM_KEY.match(key.strip())
            if sep and m and value.strip():
                out.setdefault(int(m.group(1)), value.strip())
    return out


def load_turbo_team_names(path):
    """Turbo's turbo_output\\team_names.json ({"teams": [{"teamid", "name"}]}): the clubs renamed through Turbo."""
    out = {}
    if not path or not os.path.isfile(path):
        return out
    try:
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
    except (OSError, ValueError):
        return out
    for t in (d.get("teams") if isinstance(d, dict) else None) or ():
        if isinstance(t, dict) and isinstance(t.get("name"), str) and t["name"].strip():
            try:
                out.setdefault(int(t.get("teamid")), t["name"].strip())
            except (TypeError, ValueError):
                pass
    return out


def kind_name(slug):
    """A readable label for a kit / crest id that is not a club (NON_TEAM_SLUG), else None."""
    if slug == "_default":
        return "Default kit and crest (not a club)"
    for prefix, what in (("otp_generic", "Generic team"), ("goalkeeper_", "Goalkeeper kit"), ("referee_", "Referee kit"),
                         ("uefa_walkout", "Walk-out kit")):
        if slug.startswith(prefix):
            return "%s (%s, not a club)" % (what, slug)
    return None


def build_names(player_ids, team_ids, commentary_ids, heads, kits, masters=(), workbook=None, extra=None, managers=None,
                team_sources=()):
    """heads: {id: {"slugs": [...]}}; kits: {id: {"slug": ...}}; team_sources: [(source tag, {teamid: name})] in priority
    order, offered after the game's language strings ("loc_strings" first when present) and before the kit slugs, except
    the user's renames ("le_team_names", "turbo_team_names"), offered after them. Returns a Names object restricted to
    the catalog ids."""
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
    for i, k in kits.items():  # ids that are not clubs: labelled by their kind first
        if i in team_ids and NON_TEAM_SLUG.match(k["slug"]):
            N.offer("team", i, kind_name(k["slug"]), "kind")
    late = ("le_team_names", "turbo_team_names")
    for src, table in team_sources:
        if src not in late:
            for i, n in table.items():
                if int(i) in team_ids:
                    N.offer("team", i, n, src)
    for i, k in kits.items():
        if i in team_ids and not NON_TEAM_SLUG.match(k["slug"]):
            N.offer("team", i, slug_name(k["slug"]), "kit_slug")
    for src, table in team_sources:
        if src in late:
            for i, n in table.items():
                if int(i) in team_ids:
                    N.offer("team", i, n, src)
    for i, n in (managers or {}).items():
        if i in player_ids:
            N.offer("player", i, n, "managers")
    N.restrict("player", player_ids)
    N.restrict("team", team_ids)
    N.restrict("commentary", commentary_ids)
    return N
