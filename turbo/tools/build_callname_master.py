#!/usr/bin/env python3
"""Build an FC 27 callname master (workbook + Turbo masters JSON) for one commentary language.

Port of turbo_dev\\masters\\build_master_v0.py (2026-10-04, ita_it), including its later column H "nameid" and the
Play macro patch; the JSON also carries the audio folder and the segments per id (Turbo's play buttons).

Inputs:
  --bank        <lang>_bank.json, extracted from the FC 27 commentary files on disk by turbo/tools/fc27_commentary
                (keys: lang, game_build, real [{segment, variation, playerid}], real_link [...], generic
                [{segment, variation, commentaryid}])
  --players     turbo_table_players.csv (FC 27 career database export: Turbo Tools > Exports > Export tables)
  --edited      turbo_table_editedplayernames.csv (same export)
  --names       bridge_names.txt (name id<TAB>text; FC 27's playernames table is empty in the career database)
  --commentary  bridge_commentary.txt (commentary id<TAB>text)
  --template    the user's FC 26 workbook of that language (e.g. C:\\FC_Tools\\My Mods\\ita\\italy_master.xlsm). It is
                only read and copied, never written: its styles, column widths, other sheets and VBA project are kept.
                Its 'callnames' rows also give names and categories (column G) for ids FC 27 has no name for, and its
                'real' rows of players in the FC 27 database teach the nationality -> category map.
  --lang        language code for the JSON (default: the bank's "lang")
  --wav-dir     folder holding real\\ and generic\\ (the segment wavs: real\\pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav,
                generic\\pSIMPLE_SURNAME_<seg>_<seg>.wav), written to the JSON for Turbo's play buttons; every row's wav
                is checked there (default: the bank's "wav_dir", where fc27_commentary wrote them). Italian:
                C:\\FC_Tools\\My Mods\\i27
  --vba-from / --vba-to
                same-length text patch of the copied workbook's VBA project (vba_tool.patch_text: module source,
                p-code and __SRP caches), e.g. the Play macro's audio base C:\\FC_Tools\\My Mods\\ita\\ ->
                C:\\FC_Tools\\My Mods\\i27\\ (the FC 27 audio). Without them the VBA project stays byte-identical.
                Both must have the same cp1252 byte length. The build fails (nothing written) when the text is not
                found, or when afterwards a module's source still holds --vba-from or none holds --vba-to.
  --copy-to     also copy the finished workbook into this folder (build_master_v0 delivers it to the FC 27 audio folder),
                after the JSON is written, as <that folder>\\<basename of --out-xlsm> (open in Excel: <name>_new.xlsm);
                skipped with a note when it is not a folder
  --extra-names JSON of names for 'real' rows whose player is neither in the FC 27 database nor in the template
                ({"names": {"<playerid>": {"name": ..., "source": ...}}} as turbo_dev\\masters\\raw\\extra_player_names.json,
                or {"<playerid>": [name, source]}); default: C:\\FC 27 Live Editor\\turbo_dev\\masters\\raw\\
                extra_player_names.json when it exists ('' = none)
  --names-from  another FC 26 master workbook (repeatable; the first one naming an id wins): its 'callnames' rows
                (column D of 'real' rows, by column C) and its 'names' / 'fc26 heads' sheets (id, name), the last
                fallback for 'real' rows: FC 27 database name, else template, else --extra-names, else these

Outputs:
  --out-xlsm    <name>_master_fc27.xlsm: sheet 'callnames' (SegmentID, VariationId, playerid, name, type, Play, category,
                nameid) one row per recording, sorted accent-insensitively by name (then real before generic, id,
                segment, variation; rows without a name last); H 'nameid' (generic rows) = the playernames id(s) whose
                text equals the commentary name (exact text, else case-insensitive; several joined with ','): the id a
                player's last / common name must be set to for that recording. Sheet 'names' (id, name). The VBA
                project byte-identical unless --vba-from/--vba-to. When the file is open in Excel, <name>_new.xlsm is
                written instead. No destination (--out-xlsm, its _new name, the --copy-to copies, --out-json) may be the
                template, compared as the system does (Windows: case-insensitive, links resolved); --copy-to may not be
                the template's folder. Checked before anything is written.
  --out-json    <lang>.json for Turbo (turbo\\callnames\\masters):
                {"turbo_masters": 1, "language", "game": "fc27", "game_build", "source", "built", "counts": {...},
                 "real_players" (PLAYER_NAMES_SIMPLE | PLAYER_NAMES_LINK ids = players with their own recording),
                 "real_simple_players", "real_link_players", "generic_ids", "real_high_players" (PLAYER_NAMES_HIGH by
                 player id; not in real_players: the game's kick-off check asks SIMPLE / LINK only), "generic_high_ids"
                 (PLAYER_NAMES_HIGH by surname_ID), "names" {"<playerid>": name, SIMPLE only},
                 "generic_names" {"<commentaryid>": text}, "wav_dir" (--wav-dir),
                 "segments" {"generic": {"<commentaryid>": [seg, ...]}, "real": {"<playerid>": [seg, ...]} (SIMPLE),
                             "real_link" / "real_high" / "generic_high": {...} only the segments whose wav is in
                             wav_dir\\real_link, real_high, generic_high}}
                The bank's real_link / real_high / generic_high rows are added to 'callnames' as types "real link",
                "real high", "generic high" (column F = HYPERLINK to the wav: the Play macro plays only "real" and
                "generic"); the FC 26 types' rows stay exactly as before.

Audio: the user's Play macro hard-codes C:\\FC_Tools\\My Mods\\<lang folder>\\ as the audio base (the FC 26 folders).
The FC 27 workbook is shipped with its own real\\ and generic\\ folders of FC 27 recordings next to it (--vba-from /
--vba-to point its macro there); this tool does not write audio.

usage: python turbo/tools/build_callname_master.py --bank B --players P --edited E --names N --commentary C
                                                    --template T --out-xlsm X --out-json J [--lang ita_it]
                                                    [--wav-dir W] [--vba-from OLD --vba-to NEW] [--copy-to D]
                                                    [--extra-names X.json] [--names-from OTHER_master.xlsm ...]
       python turbo/tools/build_callname_master.py --self-test
ita_it as build_master_v0: --wav-dir "C:\\FC_Tools\\My Mods\\i27" --vba-from "C:\\FC_Tools\\My Mods\\ita\\"
                           --vba-to "C:\\FC_Tools\\My Mods\\i27\\" --copy-to "C:\\FC_Tools\\My Mods\\i27"
"""
import argparse
import collections
import csv
import datetime
import json
import os
import shutil
import sys
import tempfile
import unicodedata
import zipfile
from copy import copy

PLAY = '\u25b6 Play'
# names of 'real' recordings whose player is in neither the FC 27 database nor the Italian FC 26 master (the lead's
# build_master_v0.py reads the same file)
DEFAULT_EXTRA_NAMES = r'C:\FC 27 Live Editor\turbo_dev\masters\raw\extra_player_names.json'
# every family of recordings: workbook type (column E), bank key, id key, wav folder, wav prefix. The first two are the
# FC 26 workbook's types (the Play macro plays them); the others are added rows whose Play cell is a HYPERLINK to the wav
# (the macro exits for any other type).
KINDS = (('real', 'real', 'playerid', 'real', 'pPLAYER_NAMES_SIMPLE'),
         ('generic', 'generic', 'commentaryid', 'generic', 'pSIMPLE_SURNAME'),
         ('real link', 'real_link', 'playerid', 'real_link', 'pPLAYER_NAMES_LINK'),
         ('real high', 'real_high', 'playerid', 'real_high', 'pPLAYER_NAMES_HIGH'),
         ('generic high', 'generic_high', 'commentaryid', 'generic_high', 'pPLAYER_NAMES_HIGH'))
OLD_TYPES = ('real', 'generic')
TYPE_RANK = {'real': 0, 'real link': 1, 'real high': 2, 'generic': 3, 'generic high': 4}   # real before generic, as before
# by workbook type and by bank key
WAV_PREFIX = {k: prefix for typ, key, _i, _s, prefix in KINDS for k in (typ, key)}
WAV_SUB = {k: sub for typ, key, _i, sub, _p in KINDS for k in (typ, key)}


def wav_path(wav_dir, kind, seg):
    """<wav_dir>\\<family folder>\\<prefix>_<seg>_<seg>.wav (the names fc27_commentary writes; real and generic are the
    ones the Play macro reads); kind = a workbook type or a bank key"""
    return os.path.join(wav_dir, WAV_SUB[kind], f'{WAV_PREFIX[kind]}_{seg}_{seg}.wav')


def play_cell(wav_dir, typ, seg):
    """Column F: the Play macro's text for the FC 26 types; for the added types a HYPERLINK that opens the wav"""
    if typ in OLD_TYPES or not wav_dir:
        return PLAY
    return '=HYPERLINK("%s","%s")' % (wav_path(wav_dir, typ, seg).replace('"', '""'), PLAY)


def read_tsv_map(path):
    m = {}
    with open(path, encoding='utf-8', errors='replace') as f:  # some names are cut inside a UTF-8 sequence
        for line in f:
            if line.startswith('#'):
                continue
            a = line.rstrip('\n').split('\t')
            if len(a) >= 2 and a[0].strip().isdigit():
                m[int(a[0])] = a[1]
    return m


def sort_key(s):
    s = unicodedata.normalize('NFKD', s or '')
    return ''.join(ch for ch in s if not unicodedata.combining(ch)).casefold()


def read_csv_by_playerid(path):
    out = {}
    with open(path, encoding='utf-8', newline='') as f:
        for r in csv.DictReader(f):
            out[int(r['playerid'])] = r
    return out


def vba(path):
    with zipfile.ZipFile(path) as z:
        return z.read('xl/vbaProject.bin')


def _vba_tool():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import vba_tool  # same folder; stdlib only (OLE + MS-OVBA)
    return vba_tool


def vba_module_sources(vba_bin):
    """{module name: source text}: every module of a vbaProject.bin decompressed (vba_tool), in its code page"""
    vt = _vba_tool()
    ole = vt.Ole(vba_bin)
    mods, cp = vt.parse_dir(ole)
    return {m['name']: vt.decompress(ole.read('VBA/' + m['stream'])[m['offset']:]).decode('cp%d' % cp, errors='replace')
            for m in mods}


def new_name(path):
    """<name>_new.xlsm: written instead of <name>.xlsm while that one is open in Excel"""
    root, ext = os.path.splitext(path)
    return root + '_new' + (ext or '.xlsm')


def same_path(a, b):
    """True when a and b name the same file as the system sees it: os.path.samefile when both exist, else the
    normalized real paths (normcase: case-insensitive on Windows)"""
    try:
        if os.path.exists(a) and os.path.exists(b) and os.path.samefile(a, b):
            return True
    except OSError:
        pass
    return os.path.normcase(os.path.realpath(a)) == os.path.normcase(os.path.realpath(b))


def check_destinations(template, out_xlsm, out_json=None, copy_to=None):
    """Raise ValueError when a file the build writes would be the template (which is only ever read), before anything
    is written: --out-xlsm, its .building and _new names, the --copy-to copies, --out-json, or --copy-to = the
    template's folder (its copy could be the template under another name)"""
    dests = [('--out-xlsm', out_xlsm), ('--out-xlsm (_new)', new_name(out_xlsm)),
             ('--out-xlsm (temporary)', out_xlsm + '.building.xlsm')]
    if out_json:
        dests.append(('--out-json', out_json))
    if copy_to:
        if same_path(copy_to, os.path.dirname(os.path.abspath(template))):
            raise ValueError(f'--copy-to {copy_to} is the template\'s folder: the template is never written')
        base = os.path.join(copy_to, os.path.basename(out_xlsm))
        dests += [('--copy-to', base), ('--copy-to (_new)', new_name(base))]
    for what, d in dests:
        if same_path(d, template):
            raise ValueError(f'{what} {d} is the template {template}: the template is never written')


def _id(v):
    """An id cell (int, whole float or digit text) as an int, else None"""
    if isinstance(v, bool):
        return None
    if isinstance(v, int):
        return v
    if isinstance(v, float) and v.is_integer():
        return int(v)
    if isinstance(v, str) and v.strip().isdigit():
        return int(v.strip())
    return None


def read_extra_names(path):
    """{playerid: name} from {"names": {"<id>": {"name": ...}}} or {"<id>": [name, source]} (a plain name string is
    accepted too); keys that are not ids ("note") and empty names are skipped"""
    with open(path, encoding='utf-8') as f:
        j = json.load(f)
    if not isinstance(j, dict):
        raise ValueError(f'{path}: not a JSON object')
    src = j['names'] if isinstance(j.get('names'), dict) else j
    out = {}
    for k, v in src.items():
        i = _id(k)
        if i is None:
            continue
        n = v.get('name') if isinstance(v, dict) else v[0] if isinstance(v, (list, tuple)) and v else v
        if isinstance(n, str) and n.strip():
            out[i] = n
    return out


def read_workbook_names(path):
    """{playerid: name} from another FC 26 master: 'callnames' column D of 'real' rows (id in C), then the 'names' and
    'fc26 heads' sheets (id, name); formulas and empty names skipped, the first name of an id kept"""
    import openpyxl

    out = {}

    def put(i, n):
        if i is not None and isinstance(n, str) and n.strip() and not n.startswith('='):
            out.setdefault(i, n)

    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    try:
        if 'callnames' in wb.sheetnames:
            for row in wb['callnames'].iter_rows(min_row=2, values_only=True):
                if len(row) >= 5 and row[4] == 'real':
                    put(_id(row[2]), row[3])
        for sheet in ('names', 'fc26 heads'):
            if sheet in wb.sheetnames:
                for row in wb[sheet].iter_rows(min_row=2, values_only=True):
                    if len(row) >= 2:
                        put(_id(row[0]), row[1])
    finally:
        wb.close()
    return out


def nameid_index(names):
    """name text -> name ids, exact and case-insensitive (column H)"""
    exact, folded = collections.defaultdict(list), collections.defaultdict(list)
    for nid, txt in names.items():
        exact[txt.strip()].append(nid)
        folded[txt.strip().casefold()].append(nid)
    return exact, folded


def nameids_for(text, exact, folded, stats):
    """Column H: the name id (an int), several joined with ',' (a str), None when no name has this text"""
    t = (text or '').strip()
    ids = exact.get(t)
    if ids:
        stats['exact'] += 1
    else:
        ids = folded.get(t.casefold())
        stats['casefold' if ids else 'none'] += 1
    if not ids:
        return None
    ids = sorted(ids)
    return ids[0] if len(ids) == 1 else ','.join(map(str, ids))


def segments_by_id(rows, key):
    out = collections.defaultdict(set)
    for b in rows:
        out[int(b[key])].add(int(b['segment']))
    return {str(i): sorted(v) for i, v in sorted(out.items())}


def build(bank_path, players_path, edited_path, names_path, comm_path, template, out_xlsm, out_json, lang=None,
          wav_dir=None, vba_from=None, vba_to=None, copy_to=None, log=print, extra_names=None, names_from=()):
    import openpyxl

    check_destinations(template, out_xlsm, out_json, copy_to)  # before anything is written
    if bool(vba_from) != bool(vba_to):
        raise ValueError('--vba-from and --vba-to go together')
    if vba_from:
        try:
            if len(vba_from.encode('cp1252')) != len(vba_to.encode('cp1252')):
                raise ValueError('--vba-from and --vba-to must have the same cp1252 byte length')
        except UnicodeEncodeError as e:
            raise ValueError(f'--vba-from / --vba-to must be cp1252 text: {e}')
    # the last fallbacks for 'real' rows: --extra-names, then the --names-from workbooks (the first naming an id wins)
    extra = read_extra_names(extra_names) if extra_names else {}
    if extra_names:
        log(f'extra names: {len(extra)} from {extra_names}')
    other = {}
    for wbp in names_from or ():
        got = read_workbook_names(wbp)
        for i, n in got.items():
            other.setdefault(i, n)
        log(f'names from {wbp}: {len(got)}')

    with open(bank_path, encoding='utf-8') as f:
        bank = json.load(f)
    lang = lang or bank.get('lang')
    names = read_tsv_map(names_path)
    comm = read_tsv_map(comm_path)
    players = read_csv_by_playerid(players_path)
    edited = read_csv_by_playerid(edited_path)

    def player_name(pid):
        e = edited.get(pid)
        if e:
            common = (e.get('commonname') or '').strip()
            if common:
                return common
            return ' '.join(x for x in ((e.get('firstname') or '').strip(), (e.get('surname') or '').strip()) if x)
        p = players.get(pid)
        if not p:
            return None

        def nm(field):
            v = int(p.get(field) or 0)
            return names.get(v, '').strip() if v > 0 else ''

        common = nm('commonnameid')
        if common:
            return common
        return ' '.join(x for x in (nm('firstnameid'), nm('lastnameid')) if x)

    # FC 26 template rows: names and categories by (type, id)
    wb26 = openpyxl.load_workbook(template, read_only=True, data_only=True)
    cat26, name26 = {}, {}
    for row in wb26['callnames'].iter_rows(min_row=2, values_only=True):
        if len(row) < 7 or row[4] not in ('real', 'generic') or row[2] is None:
            continue
        key = (row[4], int(row[2]))
        if row[6]:
            cat26.setdefault(key, row[6])
        if row[3] and not str(row[3]).startswith('='):
            name26.setdefault(key, str(row[3]))
    wb26.close()

    # nationality -> category, learned from the FC 26 'real' rows of players that are in the FC 27 database
    votes = collections.defaultdict(collections.Counter)
    for (typ, pid), cat in cat26.items():
        if typ == 'real' and pid in players:
            votes[players[pid].get('nationality')][cat] += 1
    nat_cat = {n: c.most_common(1)[0][0] for n, c in votes.items()}

    rows = []
    missing_names = collections.Counter()
    name_from = collections.Counter()   # where the 'real' rows' names came from (logged)
    for typ, bkey, key, _sub, _prefix in KINDS:
        for b in bank.get(bkey, []) if typ not in OLD_TYPES else bank[bkey]:
            i = int(b[key])
            if key == 'playerid':
                n = None
                for where, get in (('fc27 db', player_name), ('template', lambda x: name26.get(('real', x))),
                                   ('extra names', extra.get), ('other masters', other.get)):
                    n = get(i)
                    if n:
                        name_from[where] += 1
                        break
                cat = cat26.get(('real', i)) or (nat_cat.get(players[i].get('nationality')) if i in players else None)
            else:
                n = comm.get(i) or name26.get(('generic', i))
                cat = cat26.get(('generic', i))
            if not n:
                missing_names[typ] += 1
                n = ''
            rows.append((int(b['segment']), int(b['variation']), i, n, typ, cat))
    # real before generic as in v0 (the added types between / after them: the old rows keep their order)
    rows.sort(key=lambda r: (sort_key(r[3]) or '~', TYPE_RANK[r[4]], r[2], r[0], r[1]))
    log(f'player row names: {dict(name_from)}; without a name: {dict(missing_names)}')
    wav_dir = wav_dir if wav_dir is not None else (bank.get('wav_dir') or '')

    # column H 'nameid' (generic rows): the playernames id(s) whose text equals the commentary name
    exact, folded = nameid_index(names)
    nameid_stats = collections.Counter()

    # workbook: copy the template (to a temporary name), replace the data rows, keep styles, widths and the VBA project
    out_dir = os.path.dirname(os.path.abspath(out_xlsm))
    os.makedirs(out_dir, exist_ok=True)
    building = out_xlsm + '.building.xlsm'
    shutil.copyfile(template, building)
    try:
        _fill_workbook(building, rows, exact, folded, nameid_stats, template, vba_from, vba_to, log, wav_dir)
    except BaseException:
        if os.path.exists(building):   # never leave a half-built workbook behind
            os.remove(building)
        raise
    requested = out_xlsm
    try:
        os.replace(building, out_xlsm)
    except PermissionError:   # the previous build is open in Excel
        out_xlsm = new_name(out_xlsm)
        os.replace(building, out_xlsm)
        log('NOTE: the previous workbook is open in Excel; wrote ' + out_xlsm)
    log('nameid (generic rows): ' + str(dict(nameid_stats)))
    idnames = {}
    for seg, var, i, n, typ, cat in rows:
        if n:
            idnames.setdefault(i, n)

    # audio: the folder Turbo's play buttons read (every row's wav is checked there) and the segments of every id
    if wav_dir and os.path.isdir(wav_dir):
        miss = [(typ, seg, i) for seg, var, i, n, typ, cat in rows if not os.path.isfile(wav_path(wav_dir, typ, seg))]
        log(f'wav_dir {wav_dir}: rows without a wav: {len(miss)} {miss[:10]}')
    elif wav_dir:
        log(f'wav_dir {wav_dir} is not a folder here: the wavs were not checked')
    segments = {'generic': segments_by_id(bank['generic'], 'commentaryid'), 'real': segments_by_id(bank['real'], 'playerid')}
    for _typ, bkey, key, _sub, _prefix in KINDS[2:]:   # the added families: only the segments whose wav is there
        have = [b for b in bank.get(bkey, [])
                if wav_dir and os.path.isfile(wav_path(wav_dir, bkey, int(b['segment'])))]
        if have:
            segments[bkey] = segments_by_id(have, key)
    real_ids = sorted({int(b['playerid']) for b in bank['real']})
    link_ids = sorted({int(b['playerid']) for b in bank.get('real_link', [])})
    high_ids = sorted({int(b['playerid']) for b in bank.get('real_high', [])})
    gen_ids = sorted({int(b['commentaryid']) for b in bank['generic']})
    gen_high_ids = sorted({int(b['commentaryid']) for b in bank.get('generic_high', [])})
    real_set = set(real_ids)
    counts = {'rows': len(rows), 'real_rows': sum(r[4] == 'real' for r in rows),
              'generic_rows': sum(r[4] == 'generic' for r in rows), 'real_players': len(real_ids),
              'real_link_players': len(link_ids), 'own_recording_players': len(real_set | set(link_ids)),
              'generic_ids': len(gen_ids), 'real_in_fc27_db': sum(1 for i in real_ids if i in players),
              'missing_names': dict(missing_names), 'category_empty': sum(1 for r in rows if not r[5]),
              'real_link_rows': sum(r[4] == 'real link' for r in rows),
              'real_high_rows': sum(r[4] == 'real high' for r in rows),
              'generic_high_rows': sum(r[4] == 'generic high' for r in rows),
              'real_high_players': len(high_ids), 'generic_high_ids': len(gen_high_ids),
              'any_recording_players': len(real_set | set(link_ids) | set(high_ids))}
    # real_players stays SIMPLE | LINK: the game's kick-off check (GetCallname) asks PLAYER_LOW_SIMPLE / _LINK only
    # (docs/re/inmatch-callnames.md section 0.2), so a HIGH-only player still gets his database callname
    out = {'turbo_masters': 1, 'language': lang, 'game': 'fc27', 'game_build': bank.get('game_build'),
           'source': out_xlsm, 'built': datetime.datetime.now().isoformat(timespec='seconds'), 'counts': counts,
           'real_players': sorted(real_set | set(link_ids)), 'real_simple_players': real_ids,
           'real_link_players': link_ids, 'generic_ids': gen_ids,
           'real_high_players': high_ids, 'generic_high_ids': gen_high_ids,
           'names': {str(i): n for i, n in sorted(idnames.items()) if i in real_set},
           'generic_names': {str(i): comm.get(i, '') for i in gen_ids},
           'wav_dir': wav_dir, 'segments': segments}
    out_json_dir = os.path.dirname(os.path.abspath(out_json))
    os.makedirs(out_json_dir, exist_ok=True)
    with open(out_json, 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    # the copy last (the JSON is written whatever happens to it), under the requested name
    if copy_to:
        if not os.path.isdir(copy_to):
            log(f'copy skipped: --copy-to {copy_to} is not a folder')
        else:
            dest = os.path.join(copy_to, os.path.basename(requested))
            if same_path(dest, out_xlsm):
                log(f'copy skipped: {dest} is the workbook just written')
            else:
                try:
                    shutil.copyfile(out_xlsm, dest)
                except PermissionError:   # that copy is open in Excel
                    dest = new_name(dest)
                    shutil.copyfile(out_xlsm, dest)
                    log('NOTE: the copy in --copy-to is open in Excel; copied to ' + dest)
                else:
                    log('copied to ' + dest)
    return out


def _fill_workbook(building, rows, exact, folded, nameid_stats, template, vba_from, vba_to, log, wav_dir=''):
    """The copied template's sheets 'callnames' and 'names' replaced by the rows; its VBA project kept byte for byte,
    or patched (--vba-from / --vba-to) and checked. Raises on any problem (build removes the file)."""
    import openpyxl

    wb = openpyxl.load_workbook(building, keep_vba=True)
    ws = wb['callnames']
    style_row = [copy(c._style) for c in ws[3]]   # a plain 'real' row of the template
    head_style = copy(ws.cell(row=1, column=7)._style)
    ws.delete_rows(2, ws.max_row)
    for r_i, (seg, var, i, n, typ, cat) in enumerate(rows, start=2):
        nid = nameids_for(n, exact, folded, nameid_stats) if typ in ('generic', 'generic high') else None
        for c_i, v in enumerate((seg, var, i, n, typ, play_cell(wav_dir, typ, seg), cat, nid), start=1):
            c = ws.cell(row=r_i, column=c_i, value=v)
            c._style = copy(style_row[min(c_i, len(style_row)) - 1] if c_i != 8 else style_row[2])  # H styled like C (an id)
    h = ws.cell(row=1, column=8, value='nameid')
    h._style = head_style
    ws.column_dimensions['H'].width = max(ws.column_dimensions['C'].width or 12, 14)
    ws.auto_filter.ref = f'A1:H{len(rows) + 1}'
    ws.freeze_panes = 'A2'

    wn = wb['names']
    wn.delete_rows(2, wn.max_row)
    idnames = {}
    for seg, var, i, n, typ, cat in rows:
        if n:
            idnames.setdefault(i, n)
    for r_i, (i, n) in enumerate(sorted(idnames.items()), start=2):
        wn.cell(row=r_i, column=1, value=i)
        wn.cell(row=r_i, column=2, value=n)
    wb.save(building)

    if vba(building) != vba(template):
        raise RuntimeError('VBA project changed by openpyxl')
    if not (vba_from or vba_to):
        return
    vt = _vba_tool()
    vba_new, report = vt.patch_text(vba(template), vba_from, vba_to)
    if not report:
        raise RuntimeError(f'--vba-from text not found in the VBA project: {vba_from!r} (nothing patched)')
    vt.replace_zip_member(building, 'xl/vbaProject.bin', vba_new)
    if vba(building) != vba_new:
        raise RuntimeError('VBA project not replaced')
    # as build_master_v0: every module decompressed again; the old text gone, the new one there
    src = ''.join(vba_module_sources(vba_new).values())
    if vba_from in src:
        raise RuntimeError(f'--vba-from text still in a VBA module after the patch: {vba_from!r}')
    if vba_to not in src:
        raise RuntimeError(f'--vba-to text in no VBA module after the patch: {vba_to!r}')
    log('VBA patched: ' + '; '.join(report))


# ---------------------------------------------------------------------------------------------------------- self-test

HEADER = ('SegmentID', 'VariationId', 'playerid', 'name', 'type', 'Play Audio', 'category')
DUMMY_VBA = b'\xd0\xcf\x11\xe0dummy vba project \x00\x01\x02' * 7


def _make_template(path):
    import openpyxl
    from openpyxl.styles import Font, PatternFill

    wb = openpyxl.Workbook()
    ws = wb.active
    ws.title = 'callnames'
    ws.append(HEADER)
    ws.append((1, 10, 900001, 'Rossi', 'generic', PLAY, 'Generic'))
    ws.append((2, 20, 100, 'Old Name', 'real', PLAY, 'Serie A'))   # row 3: the style row
    ws.append((3, 30, 200, 'Zeta', 'real', PLAY, 'Liga'))
    for c in ws[3]:
        c.font = Font(bold=True, color='FF123456')
        c.fill = PatternFill('solid', fgColor='FFFFEE00')
    ws.column_dimensions['D'].width = 33
    wn = wb.create_sheet('names')
    wn.append(('id', 'name'))
    wn.append((1, 'stale'))
    wb.create_sheet('fc26 heads').append(('kept',))
    plain = path + '.tmp.xlsx'
    wb.save(plain)
    with zipfile.ZipFile(plain) as zin, zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == '[Content_Types].xml':
                data = data.replace(b'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml',
                                    b'application/vnd.ms-excel.sheet.macroEnabled.main+xml')
                data = data.replace(b'</Types>', b'<Default Extension="bin" '
                                    b'ContentType="application/vnd.ms-office.vbaProject"/></Types>')
            if item.filename == 'xl/_rels/workbook.xml.rels':
                data = data.replace(b'</Relationships>', b'<Relationship Id="rIdVBA" Type="http://schemas.microsoft.'
                                    b'com/office/2006/relationships/vbaProject" Target="vbaProject.bin"/>'
                                    b'</Relationships>')
            zout.writestr(item, data)
        zout.writestr('xl/vbaProject.bin', DUMMY_VBA)
    os.remove(plain)


def self_test():
    import openpyxl

    fails = []

    def check(cond, what):
        print(('  ok    ' if cond else '  FAIL  ') + what)
        if not cond:
            fails.append(what)

    with tempfile.TemporaryDirectory() as d:
        p = lambda n: os.path.join(d, n)  # noqa: E731
        template = p('test_master.xlsm')
        _make_template(template)
        bank = {'turbo_bank': 1, 'lang': 'ita_it', 'game_build': '1.0.test',
                'real': [{'segment': 5, 'variation': 2, 'playerid': 100}, {'segment': 5, 'variation': 1, 'playerid': 100},
                         {'segment': 6, 'variation': 1, 'playerid': 200}, {'segment': 7, 'variation': 1, 'playerid': 300},
                         {'segment': 8, 'variation': 1, 'playerid': 400}],
                'real_link': [{'segment': 9, 'variation': 1, 'playerid': 100},
                              {'segment': 10, 'variation': 2, 'playerid': 500}],
                'real_high': [{'segment': 11, 'variation': 3, 'playerid': 100}],
                'generic_high': [{'segment': 12, 'variation': 4, 'commentaryid': 900001}],
                'generic': [{'segment': 1, 'variation': 10, 'commentaryid': 900001},
                            {'segment': 2, 'variation': 11, 'commentaryid': 900002}]}
        with open(p('ita_it_bank.json'), 'w', encoding='utf-8') as f:
            json.dump(bank, f)
        with open(p('players.csv'), 'w', encoding='utf-8', newline='') as f:
            f.write('playerid,firstnameid,lastnameid,commonnameid,nationality\n'
                    '100,1,2,0,27\n200,3,4,0,45\n300,0,0,5,27\n')
        with open(p('edited.csv'), 'w', encoding='utf-8', newline='') as f:
            f.write('playerid,firstname,surname,commonname\n200,Ángel,Álvarez,\n')
        with open(p('names.txt'), 'w', encoding='utf-8') as f:
            # 6 / 9 'Rossi': two exact matches for column H; 7 'rossi' loses to them; 8 'ÄBEL' matches 'Äbel' by case
            f.write('# nameid\tname\n1\tÉmile\n2\tZola\n3\tX\n4\tY\n5\tBeta\n6\tRossi\n7\trossi\n8\tÄBEL\n9\tRossi\n')
        with open(p('comm.txt'), 'w', encoding='utf-8') as f:
            f.write('900001\tRossi\n900002\tÄbel\n')
        wav_dir = p('wavs')
        for sub, name in (('generic', 'pSIMPLE_SURNAME_1_1.wav'), ('real', 'pPLAYER_NAMES_SIMPLE_5_5.wav'),
                          ('real_link', 'pPLAYER_NAMES_LINK_9_9.wav'),   # the LINK wav of segment 10 is missing
                          ('real_high', 'pPLAYER_NAMES_HIGH_11_11.wav')):   # the generic HIGH wav of 12 is missing
            os.makedirs(os.path.join(wav_dir, sub), exist_ok=True)
            open(os.path.join(wav_dir, sub, name), 'wb').close()
        out_xlsm, out_json = p(os.path.join('out', 'test_master_fc27.xlsm')), p(os.path.join('out', 'ita_it.json'))
        logged = []
        out = build(p('ita_it_bank.json'), p('players.csv'), p('edited.csv'), p('names.txt'), p('comm.txt'),
                    template, out_xlsm, out_json, wav_dir=wav_dir, log=logged.append)

        wb = openpyxl.load_workbook(out_xlsm, keep_vba=True)
        ws = wb['callnames']
        got = [tuple(c.value for c in r) for r in ws.iter_rows(min_row=1, max_row=ws.max_row)]
        link = lambda sub, pre, s: '=HYPERLINK("%s","%s")' % (os.path.join(wav_dir, sub, f'{pre}_{s}_{s}.wav'), PLAY)  # noqa: E731
        want = [HEADER + ('nameid',),
                (2, 11, 900002, 'Äbel', 'generic', PLAY, None, 8),
                (6, 1, 200, 'Ángel Álvarez', 'real', PLAY, 'Liga', None),
                (7, 1, 300, 'Beta', 'real', PLAY, 'Serie A', None),
                (5, 1, 100, 'Émile Zola', 'real', PLAY, 'Serie A', None),
                (5, 2, 100, 'Émile Zola', 'real', PLAY, 'Serie A', None),
                (9, 1, 100, 'Émile Zola', 'real link', link('real_link', 'pPLAYER_NAMES_LINK', 9), 'Serie A', None),
                (11, 3, 100, 'Émile Zola', 'real high', link('real_high', 'pPLAYER_NAMES_HIGH', 11), 'Serie A', None),
                (1, 10, 900001, 'Rossi', 'generic', PLAY, 'Generic', '6,9'),
                (12, 4, 900001, 'Rossi', 'generic high', link('generic_high', 'pPLAYER_NAMES_HIGH', 12), 'Generic', '6,9'),
                (8, 1, 400, None, 'real', PLAY, None, None),   # '' is written as an empty cell
                (10, 2, 500, None, 'real link', link('real_link', 'pPLAYER_NAMES_LINK', 10), None, None)]
        check(got[0] == want[0], 'header row kept, H = nameid')
        check(len(got) == len(want), f'rows: {len(got) - 1} data rows (want {len(want) - 1})')
        check(got == want, 'sort order (accent-insensitive by name, nameless last), names, categories, nameid (generic)')
        if got != want:
            for g in got:
                print('        ', g)
        check(all(c.font.bold and c.font.color.rgb == 'FF123456' and c.fill.fgColor.rgb == 'FFFFEE00'
                  for r in ws.iter_rows(min_row=2) for c in r), 'styles copied from the template style row')
        check(ws.column_dimensions['D'].width == 33, 'column width kept')
        check(ws.auto_filter.ref == f'A1:H{len(want)}' and ws.freeze_panes == 'A2', 'filter and frozen header')
        check(ws.cell(row=2, column=8).font.bold and ws.column_dimensions['H'].width >= 14, 'column H styled and wide')
        check(not os.path.exists(out_xlsm + '.building.xlsm'), 'no temporary workbook left')
        wn = wb['names']
        check([tuple(c.value for c in r) for r in wn.iter_rows(min_row=2)] ==
              [(100, 'Émile Zola'), (200, 'Ángel Álvarez'), (300, 'Beta'), (900001, 'Rossi'), (900002, 'Äbel')],
              'names sheet')
        check('fc26 heads' in wb.sheetnames, 'other sheets kept')
        check(vba(out_xlsm) == DUMMY_VBA == vba(template), 'VBA bytes identical')

        with open(out_json, encoding='utf-8') as f:
            j = json.load(f)
        check(list(j) == ['turbo_masters', 'language', 'game', 'game_build', 'source', 'built', 'counts',
                          'real_players', 'real_simple_players', 'real_link_players', 'generic_ids',
                          'real_high_players', 'generic_high_ids', 'names', 'generic_names', 'wav_dir', 'segments'],
              'json keys')
        check(j['wav_dir'] == wav_dir, 'json wav_dir')
        check(j['segments'] == {'generic': {'900001': [1], '900002': [2]},
                                'real': {'100': [5], '200': [6], '300': [7], '400': [8]},
                                'real_link': {'100': [9]}, 'real_high': {'100': [11]}},
              'json segments (LINK / HIGH only where their wav exists)')
        check(any('rows without a wav: 6' in s for s in logged), 'missing wavs counted: ' + '; '.join(logged))
        check(j['real_high_players'] == [100] and j['generic_high_ids'] == [900001], 'json HIGH id lists')
        check(j['turbo_masters'] == 1 and j['language'] == 'ita_it' and j['game'] == 'fc27'
              and j['game_build'] == '1.0.test' and j['source'] == out_xlsm, 'json header fields')
        check(j['real_players'] == [100, 200, 300, 400, 500] and j['real_simple_players'] == [100, 200, 300, 400]
              and j['real_link_players'] == [100, 500] and j['generic_ids'] == [900001, 900002], 'json id lists')
        check(j['names'] == {'100': 'Émile Zola', '200': 'Ángel Álvarez', '300': 'Beta'}, 'json names (SIMPLE only)')
        check(j['generic_names'] == {'900001': 'Rossi', '900002': 'Äbel'}, 'json generic names')
        check(j['counts'] == {'rows': 11, 'real_rows': 5, 'generic_rows': 2, 'real_players': 4, 'real_link_players': 2,
                              'own_recording_players': 5, 'generic_ids': 2, 'real_in_fc27_db': 3,
                              'missing_names': {'real': 1, 'real link': 1}, 'category_empty': 3, 'real_link_rows': 2,
                              'real_high_rows': 1, 'generic_high_rows': 1, 'real_high_players': 1, 'generic_high_ids': 1,
                              'any_recording_players': 5}, 'json counts')
        check(out == j, 'returned dict equals the written JSON')
        # no --wav-dir and none in the bank: an empty wav_dir, the SIMPLE / generic segments, no real_link
        out2 = build(p('ita_it_bank.json'), p('players.csv'), p('edited.csv'), p('names.txt'), p('comm.txt'),
                     template, p(os.path.join('out2', 'm.xlsm')), p(os.path.join('out2', 'ita_it.json')), log=logged.append)
        check(out2['wav_dir'] == '' and sorted(out2['segments']) == ['generic', 'real'], 'no wav folder: segments only')

        args = (p('ita_it_bank.json'), p('players.csv'), p('edited.csv'), p('names.txt'), p('comm.txt'), template)

        # --extra-names (both shapes) and --names-from: the last fallbacks for 'real' rows (player 400 has no name)
        with open(p('extra_a.json'), 'w', encoding='utf-8') as f:
            json.dump({'note': 'x', 'names': {'400': {'name': 'Extra Four', 'source': 'br_master.xlsm'},
                                              '100': {'name': 'Never Used', 'source': 'x'}}}, f)
        with open(p('extra_b.json'), 'w', encoding='utf-8') as f:
            json.dump({'400': ['Extra Four B', 'FC Editor'], 'note': ['not an id', 'x']}, f)
        other = p('other_master.xlsx')
        wbo = openpyxl.Workbook()
        wbo.active.title = 'callnames'
        wbo['callnames'].append(HEADER)
        wbo['callnames'].append((1, 1, 999, 'Callnames Nine', 'real', PLAY, None))
        wbo['callnames'].append((1, 1, 400, '=formula', 'real', PLAY, None))
        wbo.create_sheet('names').append(('ID', 'origname'))
        wbo['names'].append((400, ''))
        wbo.create_sheet('fc26 heads').append(('ID', 'Name'))
        wbo['fc26 heads'].append((400, 'Heads Four'))
        wbo.save(other)
        check(read_workbook_names(other) == {999: 'Callnames Nine', 400: 'Heads Four'},
              "--names-from: 'callnames' D of real rows, then 'names' / 'fc26 heads' (formulas, empty names skipped)")
        for tag, kw, want400 in (('a', {'extra_names': p('extra_a.json')}, 'Extra Four'),
                                 ('b', {'extra_names': p('extra_b.json')}, 'Extra Four B'),
                                 ('c', {'names_from': [other]}, 'Heads Four'),
                                 ('d', {'extra_names': p('extra_a.json'), 'names_from': [other]}, 'Extra Four')):
            o = build(*args, p(os.path.join('o' + tag, 'm.xlsm')), p(os.path.join('o' + tag, 'j.json')), log=logged.append, **kw)
            check(o['names'].get('400') == want400 and o['names']['100'] == 'Émile Zola'
                  and o['counts']['missing_names'] == {'real link': 1},
                  f'fallback names ({tag}): 400 = {o["names"].get("400")!r}, the FC 27 database first')

        # the template is never a destination, compared as the system does (case variant, '.', its folder for --copy-to)
        def refused(what, **kw):
            o_x = kw.pop('out_xlsm', p(os.path.join('og', 'm.xlsm')))
            o_j = kw.pop('out_json', p(os.path.join('og', 'j.json')))
            try:
                build(*args, o_x, o_j, log=logged.append, **kw)
            except ValueError as e:
                check('template' in str(e) and not os.path.exists(p('og')), f'refused before writing: {what}')
                return
            check(False, f'refused: {what} (it was written)')

        stamp = os.path.getmtime(template), os.path.getsize(template)
        variant = os.path.join(os.path.dirname(template), os.path.basename(template).upper())
        if os.path.exists(variant):   # a case-insensitive file system (Windows): the same file
            refused('--out-xlsm, a case variant of the template', out_xlsm=variant)
        refused("--out-xlsm, the template through '.'", out_xlsm=os.path.join(d, '.', 'test_master.xlsm'))
        refused("--copy-to, the template's folder", copy_to=d.upper() if os.path.exists(d.upper()) else d)
        refused('--out-json, the template', out_json=template)
        t_new = p('t2_new.xlsm')   # a template named like another workbook's _new name
        shutil.copyfile(template, t_new)
        try:
            build(*args[:5], t_new, p('t2.xlsm'), p(os.path.join('og', 'j.json')), log=logged.append)
            check(False, '--out-xlsm whose _new name is the template: refused')
        except ValueError as e:
            check('_new' in str(e) and not os.path.exists(p('og')) and not os.path.exists(p('t2.xlsm')),
                  '--out-xlsm whose _new name is the template: refused')
        check((os.path.getmtime(template), os.path.getsize(template)) == stamp, 'the template untouched')
        try:
            main(['--bank', args[0], '--players', args[1], '--edited', args[2], '--names', args[3], '--commentary', args[4],
                  '--template', template, '--out-xlsm', os.path.join(d, '.', 'test_master.xlsm'), '--out-json', p('j.json')])
            check(False, 'main refuses the template as --out-xlsm')
        except SystemExit as e:
            check(e.code == 2 and not os.path.exists(p('j.json')), 'main refuses the template as --out-xlsm (usage error)')

        # --vba-from / --vba-to: any failure leaves nothing (no .building file, no workbook, no JSON)
        def vba_fails(what, fake_patch=None, fake_sources=None, vba_from='ita\\', vba_to='i27\\'):
            o_x, o_j = p(os.path.join('ov', 'm.xlsm')), p(os.path.join('ov', 'j.json'))
            g = globals()
            saved = g['_vba_tool'], g['vba_module_sources']
            try:
                if fake_patch:
                    real = saved[0]()

                    class Fake:
                        patch_text = staticmethod(fake_patch)
                        replace_zip_member = staticmethod(real.replace_zip_member)
                    g['_vba_tool'] = lambda: Fake
                if fake_sources:
                    g['vba_module_sources'] = fake_sources
                try:
                    build(*args, o_x, o_j, log=logged.append, vba_from=vba_from, vba_to=vba_to)
                except (RuntimeError, ValueError, AssertionError) as e:
                    left = sorted(os.listdir(p('ov'))) if os.path.isdir(p('ov')) else []
                    check(left == [], f'{what}: failed, nothing left ({e}; {left})')
                    return
                check(False, f'{what}: should fail')
            finally:
                g['_vba_tool'], g['vba_module_sources'] = saved

        vba_fails('not a VBA project (the real vba_tool)')
        vba_fails('--vba-from not found (patch_text reports nothing)', fake_patch=lambda b, o, n: (b, []))
        patched = DUMMY_VBA.replace(b'dummy', b'dumbo')
        vba_fails('--vba-from still in a module afterwards', fake_patch=lambda b, o, n: (patched, ['VBA/Module1: source 1']),
                  fake_sources=lambda b: {'Module1': 'x = "ita\\" & y'})
        vba_fails('--vba-to in no module afterwards', fake_patch=lambda b, o, n: (patched, ['VBA/Module1: source 1']),
                  fake_sources=lambda b: {'Module1': 'x = "zzz\\" & y'})
        vba_fails('a different cp1252 byte length', vba_from='ita\\', vba_to='i2\\')
        g = globals()
        saved = g['_vba_tool'], g['vba_module_sources']
        try:
            real = saved[0]()

            class Fake:
                patch_text = staticmethod(lambda b, o, n: (patched, ['VBA/Module1: source 1']))
                replace_zip_member = staticmethod(real.replace_zip_member)
            g['_vba_tool'] = lambda: Fake
            g['vba_module_sources'] = lambda b: {'Module1': 'x = "i27\\" & y'}
            o = build(*args, p(os.path.join('ov2', 'm.xlsm')), p(os.path.join('ov2', 'j.json')), log=logged.append,
                      vba_from='ita\\', vba_to='i27\\')
            check(vba(o['source']) == patched and any(s.startswith('VBA patched') for s in logged), 'a checked patch is written')
        finally:
            g['_vba_tool'], g['vba_module_sources'] = saved

        # --copy-to: after the JSON, as <copy_to>\<requested basename>; open in Excel -> <name>_new.xlsm; not a folder -> skipped
        dest_dir = p('deliver')
        os.makedirs(dest_dir)
        real_copy = shutil.copyfile
        seen = []

        def copy_locked(src, dst):
            seen.append((os.path.basename(dst), os.path.exists(p(os.path.join('oc', 'j.json')))))
            if os.path.basename(dst) == 'italy_master_fc27.xlsm':
                raise PermissionError('open in Excel')
            return real_copy(src, dst)

        shutil.copyfile = copy_locked
        try:
            build(*args, p(os.path.join('oc', 'italy_master_fc27.xlsm')), p(os.path.join('oc', 'j.json')), log=logged.append,
                  copy_to=dest_dir)
        finally:
            shutil.copyfile = real_copy
        check([s for s in seen if s[0] != 'test_master.xlsm' and not s[0].endswith('.building.xlsm')] ==
              [('italy_master_fc27.xlsm', True), ('italy_master_fc27_new.xlsm', True)],
              f'copy after the JSON; locked copy -> _new: {seen}')
        check(sorted(os.listdir(dest_dir)) == ['italy_master_fc27_new.xlsm'] and any('copied to' in s and '_new' in s for s in logged),
              'the copy written as <name>_new.xlsm and logged')
        logged.clear()
        o = build(*args, p(os.path.join('od', 'm.xlsm')), p(os.path.join('od', 'j.json')), log=logged.append,
                  copy_to=p('no_such_folder'))
        check(os.path.isfile(o['source']) and not os.path.exists(p('no_such_folder')) and any('not a folder' in s for s in logged),
              '--copy-to not a folder: skipped with a note')
    print('SELF-TEST ' + ('PASS' if not fails else f'FAIL ({len(fails)})'))
    return 0 if not fails else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--bank')
    ap.add_argument('--players')
    ap.add_argument('--edited')
    ap.add_argument('--names')
    ap.add_argument('--commentary')
    ap.add_argument('--template')
    ap.add_argument('--out-xlsm')
    ap.add_argument('--out-json')
    ap.add_argument('--lang')
    ap.add_argument('--wav-dir')
    ap.add_argument('--vba-from')
    ap.add_argument('--vba-to')
    ap.add_argument('--copy-to')
    ap.add_argument('--extra-names', help=f'default: {DEFAULT_EXTRA_NAMES} when it exists; \'\' = none')
    ap.add_argument('--names-from', action='append', default=[], metavar='WORKBOOK')
    ap.add_argument('--self-test', action='store_true')
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    need = ('bank', 'players', 'edited', 'names', 'commentary', 'template', 'out_xlsm', 'out_json')
    missing = ['--' + k.replace('_', '-') for k in need if not getattr(a, k)]
    if missing:
        ap.error('missing ' + ', '.join(missing))
    try:
        check_destinations(a.template, a.out_xlsm, a.out_json, a.copy_to)
    except ValueError as e:
        ap.error(str(e))
    if bool(a.vba_from) != bool(a.vba_to):
        ap.error('--vba-from and --vba-to go together')
    if a.vba_from:
        try:
            same = len(a.vba_from.encode('cp1252')) == len(a.vba_to.encode('cp1252'))
        except UnicodeEncodeError as e:
            ap.error(f'--vba-from / --vba-to must be cp1252 text: {e}')
        if not same:
            ap.error('--vba-from and --vba-to must have the same cp1252 byte length')
        if a.vba_from == a.vba_to:
            ap.error('--vba-from and --vba-to are the same text')
    extra = a.extra_names
    if extra is None:
        extra = DEFAULT_EXTRA_NAMES if os.path.isfile(DEFAULT_EXTRA_NAMES) else ''
    if extra and not os.path.isfile(extra):
        ap.error(f'--extra-names {extra} not found')
    for wbp in a.names_from:
        if not os.path.isfile(wbp):
            ap.error(f'--names-from {wbp} not found')
    out = build(a.bank, a.players, a.edited, a.names, a.commentary, a.template, a.out_xlsm, a.out_json, a.lang,
                wav_dir=a.wav_dir, vba_from=a.vba_from, vba_to=a.vba_to, copy_to=a.copy_to, extra_names=extra or None,
                names_from=a.names_from)
    print(json.dumps(out['counts'], indent=1))
    print(f"wrote {out['source']}\nwrote {a.out_json}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
