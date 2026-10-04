#!/usr/bin/env python3
"""Summarize Turbo's voice-swap observe log (turbo_output\\callname_voice_log.txt, docs/callnames.md section 12).

usage: python scripts/callname_voice_log.py ["<Live Editor>/turbo_output/callname_voice_log.txt"]
           [--player 278319 --player 216435,261865] [--event PLAYER_LOW_SIMPLE] [--raw] [--top 40]
           [--events extra.json ...]
       python scripts/callname_voice_log.py --self-test

The log (written by Turbo.dll, win/callname_voice_win.cpp, every 5 s while turbo_output\\callname_voice_log_on.txt
exists; appended; oldest entry first). One line per ring entry, tokens separated by one space, values never contain
spaces. Lines starting with '#' are comments (the writer's header, "# dropped <n>" when the ring overflowed).

  query line (the Preprocess hook, one per CommentaryDb query it looked at):
    t=<ms> tid=<n> ev=<event id> q=<hex> pid_before=<name>:<v>,... pid_after=<name>:<v>,... surname=<id>
    intensity=<n> flags=<hex> guard=<0|1>
      t           milliseconds, monotonic (GetTickCount64)
      tid         GetCurrentThreadId()
      ev          the event id [ctx+0x44], 0x%08X (decimal accepted); djb2-xor of the event name
      q           the SpeechQuery address, 0x%llX
      pid_before  every single-value int parameter whose name contains "_pID", in parameter order, as the game's
                  Preprocess left it: name:value pairs joined by ','; '-' when the query has none
      pid_after   the same parameters, same order, after Turbo's rewrite (equal to pid_before when nothing changed)
      surname     the surname_ID value, '-' when the event does not declare it
      intensity   the player_intensity value, '-' when not declared
      flags       OR over the line's "_pID" descriptors: 0x1 an allowed-values list (the u32 count at [desc+0x18]-4 is not 0),
                  0x2 [desc+0x45] != 0, 0x4 a "_pID" parameter left out because it is not a single-value int,
                  0x8 the query was bounded (count above kMaxParams); other bits reserved
      guard       1 = the double-pass guard skipped a rewrite on this query
  kick-off line (the GetCallname hook, one per call):
    t=<ms> kickoff pid=<n> game=<id> override=<id|->
      game        the game's own result (-1 = surname lines silent, else a commentary id)
      override    the table's kick-off id that replaced it, '-' when the table has none (the game's result stands)

Extra key=value tokens are allowed on both kinds of line (for example tid= on a kick-off line) and are ignored.
Event names come from docs/re/inmatch-callnames.json (name_events, events_with_surname_or_player_db_pid, the FC 26
families), then docs/re/E4-callnames-live/registry_events_*.json when present, then any --events file (a JSON with
{"events": {name: id}}, a list of {"event", "id"}, or the inmatch-callnames.json layout). Unknown ids print as hex.
"""
import argparse
import contextlib
import glob
import io
import json
import os
import sys
import tempfile
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVENTS_JSON = os.path.join(ROOT, 'docs', 're', 'inmatch-callnames.json')
REGISTRY_GLOB = os.path.join(ROOT, 'docs', 're', 'E4-callnames-live', 'registry_events_*.json')
DEFAULT_LOG = r'C:\FC 27 Live Editor\turbo_output\callname_voice_log.txt'
FLAG_BITS = {0x1: 'value list', 0x2: 'desc+0x45', 0x4: 'non-int or multi _pID skipped', 0x8: 'bounded'}
QUERY_KEYS = ('t', 'tid', 'ev', 'q', 'pid_before', 'pid_after', 'surname', 'intensity', 'flags', 'guard')


def djb2x(s):
    h = 5381
    for c in s.encode('latin-1'):
        h = ((h * 33) ^ c) & 0xFFFFFFFF
    return h


# ------------------------------------------------------------------------------------------------------- event names

def _id(v):
    if isinstance(v, int):
        return v & 0xFFFFFFFF
    s = str(v).strip().split()[0]
    return int(s, 16) if s.lower().startswith('0x') else int(s)


def load_event_names(paths):
    """{event id: name}; earlier files win. Missing files are skipped."""
    names = {}

    def add(name, eid):
        try:
            names.setdefault(_id(eid), name)
        except (ValueError, IndexError, TypeError):
            pass

    for p in paths:
        if not p or not os.path.isfile(p):
            continue
        with open(p, encoding='utf-8') as f:
            d = json.load(f)
        if isinstance(d, list):
            for e in d:
                if isinstance(e, dict) and 'event' in e and 'id' in e:
                    add(e['event'], e['id'])
            continue
        for name, v in (d.get('name_events') or {}).items():
            if isinstance(v, dict) and 'id' in v:
                add(name, v['id'])
        for name, v in (d.get('events_with_surname_or_player_db_pid') or {}).items():
            add(name, v)
        for e in d.get('name_events_fc26_families') or []:
            if isinstance(e, dict) and 'event' in e and 'id' in e:
                add(e['event'], e['id'])
        ev = d.get('events')
        if isinstance(ev, dict):
            for name, v in ev.items():
                add(name, v)
        for name in (d.get('triggers_with_player_params') or {}):
            add(name, djb2x(name))
    return names


def default_event_files():
    return [EVENTS_JSON] + sorted(glob.glob(REGISTRY_GLOB), reverse=True)


# ------------------------------------------------------------------------------------------------------------ parsing

def _int(s, default=None):
    if s is None or s == '-' or s == '':
        return default
    try:
        return int(s, 16) if s.lower().startswith(('0x', '-0x')) else int(s)
    except ValueError:
        return default


def _pids(s):
    """'player_db_pID:278319,keeper_pID:0' -> [('player_db_pID', 278319), ('keeper_pID', 0)]; '-' -> []."""
    out = []
    if not s or s == '-':
        return out
    for part in s.split(','):
        name, sep, v = part.rpartition(':')
        if not sep:
            raise ValueError('bad pid pair ' + part)
        out.append((name, int(v)))
    return out


def parse_line(line):
    """A dict with kind 'query' / 'kickoff' / 'comment' / 'blank', or None for a line that does not parse."""
    s = line.strip()
    if not s:
        return {'kind': 'blank'}
    if s.startswith('#'):
        return {'kind': 'comment', 'text': s}
    toks = s.split()
    kv = {}
    bare = []
    for tok in toks:
        k, sep, v = tok.partition('=')
        if sep:
            kv[k] = v
        else:
            bare.append(tok)
    try:
        if 'kickoff' in bare:
            ov = kv.get('override', '-')
            return {'kind': 'kickoff', 't': _int(kv.get('t'), 0), 'pid': int(kv['pid']), 'game': int(kv['game']),
                    'override': None if ov == '-' else int(ov), 'tid': _int(kv.get('tid'))}
        if bare or any(k not in kv for k in ('t', 'tid', 'ev', 'pid_before')):
            return None
        before = _pids(kv['pid_before'])
        after = _pids(kv.get('pid_after', kv['pid_before']))
        if len(after) != len(before):
            return None
        return {'kind': 'query', 't': int(kv['t']), 'tid': int(kv['tid']), 'ev': _int(kv['ev']) & 0xFFFFFFFF,
                'q': kv.get('q', '-'), 'before': before, 'after': after, 'surname': _int(kv.get('surname')),
                'intensity': _int(kv.get('intensity')), 'flags': _int(kv.get('flags'), 0) or 0,
                'guard': kv.get('guard', '0') == '1'}
    except (KeyError, ValueError, TypeError, AttributeError):
        return None


def read_log(path):
    with open(path, 'rb') as f:
        raw = f.read()
    text = raw.decode('utf-8-sig', errors='replace')
    return text.splitlines()


def entry_players(e):
    if e['kind'] == 'kickoff':
        return {e['pid']}
    return {v for _, v in e['before']} | {v for _, v in e['after']}


def parse_filters(players, events, names):
    want_p = set()
    for p in players or []:
        for x in str(p).split(','):
            if x.strip():
                want_p.add(int(x))
    want_e = set()
    by_name = {n.lower(): i for i, n in names.items()}
    for e in events or []:
        for x in str(e).split(','):
            x = x.strip()
            if not x:
                continue
            if x.lower() in by_name:
                want_e.add(by_name[x.lower()])
            else:
                try:
                    want_e.add(_id(x))
                except ValueError:
                    want_e.add(djb2x(x))
    return want_p, want_e


# ---------------------------------------------------------------------------------------------------------- summary

def summarize(lines, names, players=None, events=None):
    """Everything the report prints, as plain data (the self-test reads it)."""
    want_p, want_e = set(players or ()), set(events or ())
    s = {'lines': 0, 'comments': [], 'unparsed': [], 'queries': 0, 'kickoffs': 0, 't_first': None, 't_last': None,
         'events': Counter(), 'event_players': defaultdict(set), 'threads': defaultdict(Counter),
         'players': {}, 'rewrites': Counter(), 'rewrite_events': Counter(), 'rewrite_lines': 0,
         'guard': 0, 'guard_events': Counter(), 'repeat_passes': 0, 'flags': Counter(), 'flag_events': Counter(),
         'surnames': Counter(), 'intensities': Counter(), 'kick': {}, 'selected': []}
    last_by_tid = {}

    def player(pid):
        return s['players'].setdefault(pid, {'lines': 0, 'events': Counter(), 'params': Counter(), 'to': Counter(),
                                             'surnames': Counter(), 'intensities': Counter(), 'kick': []})

    for n, line in enumerate(lines, 1):
        e = parse_line(line)
        if e is None:
            s['unparsed'].append((n, line.rstrip()))
            continue
        if e['kind'] == 'blank':
            continue
        if e['kind'] == 'comment':
            s['comments'].append(e['text'])
            continue
        s['lines'] += 1
        if want_p and not (entry_players(e) & want_p):
            continue
        if want_e and (e['kind'] != 'query' or e['ev'] not in want_e):
            continue
        s['selected'].append((n, e, line.rstrip()))
        t = e['t']
        s['t_first'] = t if s['t_first'] is None else min(s['t_first'], t)
        s['t_last'] = t if s['t_last'] is None else max(s['t_last'], t)
        if e['kind'] == 'kickoff':
            s['kickoffs'] += 1
            if e['tid'] is not None:
                s['threads'][e['tid']]['kickoff'] += 1
            k = s['kick'].setdefault(e['pid'], [])
            k.append((e['game'], e['override']))
            player(e['pid'])['kick'].append((e['game'], e['override']))
            continue
        s['queries'] += 1
        ev = e['ev']
        s['events'][ev] += 1
        s['threads'][e['tid']]['query'] += 1
        key = (e['q'], ev)
        if last_by_tid.get(e['tid']) == key:
            s['repeat_passes'] += 1
        last_by_tid[e['tid']] = key
        if e['guard']:
            s['guard'] += 1
            s['guard_events'][ev] += 1
        if e['flags']:
            for bit in range(32):
                if e['flags'] & (1 << bit):
                    s['flags'][1 << bit] += 1
            s['flag_events'][ev] += 1
        if e['surname'] is not None:
            s['surnames'][e['surname']] += 1
        if e['intensity'] is not None:
            s['intensities'][e['intensity']] += 1
        changed = False
        for (name, v), (_, w) in zip(e['before'], e['after']):
            if v > 0:
                p = player(v)
                p['lines'] += 1
                p['events'][ev] += 1
                p['params'][name] += 1
                if e['surname'] is not None:
                    p['surnames'][e['surname']] += 1
                if e['intensity'] is not None:
                    p['intensities'][e['intensity']] += 1
                s['event_players'][ev].add(v)
            if w != v:
                changed = True
                s['rewrites'][(name, v, w)] += 1
                s['rewrite_events'][ev] += 1
                if v > 0:
                    s['players'][v]['to'][w] += 1
        s['rewrite_lines'] += changed
    return s


def ev_name(names, eid):
    return names.get(eid, '0x%08X' % eid)


def report(s, names, top=40, raw=False, out=print):
    def head(t):
        out('')
        out(t)

    def cap(items):
        items = list(items)
        return items if not top else items[:top]

    span = '' if s['t_first'] is None else ', t %d..%d (%.1f s)' % (s['t_first'], s['t_last'],
                                                                    (s['t_last'] - s['t_first']) / 1000.0)
    out('%d entries: %d queries, %d kick-offs%s' % (s['lines'], s['queries'], s['kickoffs'], span))
    if len(s['selected']) != s['lines']:
        out('selected by the filters: %d' % len(s['selected']))
    for c in s['comments'][:5]:
        out('  ' + c)
    if s['unparsed']:
        out('unparsed lines: %d (first: line %d: %s)' % (len(s['unparsed']), s['unparsed'][0][0],
                                                          s['unparsed'][0][1][:120]))
    head('Threads')
    for tid, c in sorted(s['threads'].items(), key=lambda kv: -sum(kv[1].values())):
        out('  tid %-8d queries %-7d kick-offs %d' % (tid, c['query'], c['kickoff']))
    head('Events (%d distinct)' % len(s['events']))
    for eid, c in cap(s['events'].most_common()):
        out('  %7d  %-46s 0x%08X  players %d' % (c, ev_name(names, eid), eid, len(s['event_players'][eid])))
    head('Players seen (%d)' % len(s['players']))
    for pid, p in cap(sorted(s['players'].items(), key=lambda kv: -kv[1]['lines'])):
        to = ', '.join('%d x%d' % (w, c) for w, c in p['to'].most_common())
        params = ', '.join('%s %d' % kv for kv in p['params'].most_common())
        out('  %-8d lines %-6d events %-4d %s%s' % (pid, p['lines'], len(p['events']), params,
                                                    ('; rewritten to ' + to) if to else ''))
        if p['events']:
            out('           top: ' + ', '.join('%s %d' % (ev_name(names, e), c) for e, c in p['events'].most_common(4)))
        if p['surnames']:
            out('           surname_ID: ' + ', '.join('%d x%d' % kv for kv in p['surnames'].most_common(4)))
    head('Rewrites: %d values on %d lines' % (sum(s['rewrites'].values()), s['rewrite_lines']))
    for (name, v, w), c in cap(s['rewrites'].most_common()):
        out('  %7d  %-24s %d -> %d' % (c, name, v, w))
    for eid, c in cap(s['rewrite_events'].most_common(10)):
        out('           in %s %d' % (ev_name(names, eid), c))
    head('Guard hits: %d; same query twice in a row on a thread: %d' % (s['guard'], s['repeat_passes']))
    for eid, c in cap(s['guard_events'].most_common(10)):
        out('  %7d  %s' % (c, ev_name(names, eid)))
    head('Descriptor flags: %d lines' % sum(s['flag_events'].values()))
    for bit, c in sorted(s['flags'].items()):
        out('  0x%X %-32s %d lines' % (bit, FLAG_BITS.get(bit, 'reserved'), c))
    for eid, c in cap(s['flag_events'].most_common(10)):
        out('           in %s %d' % (ev_name(names, eid), c))
    if s['intensities']:
        head('player_intensity: ' + ', '.join('%d x%d' % kv for kv in sorted(s['intensities'].items())))
    head('Kick-off results (%d players)' % len(s['kick']))
    for pid, rs in sorted(s['kick'].items()):
        res = Counter('%d -> %d' % (g, o) if o is not None else '%d' % g for g, o in rs)
        out('  %-8d %s' % (pid, ', '.join('%s x%d' % kv if kv[1] > 1 else kv[0] for kv in res.most_common())))
    if raw:
        head('Lines')
        for n, e, line in s['selected']:
            if e['kind'] == 'query':
                out('  %6d  %-34s %s' % (n, ev_name(names, e['ev']), line))
            else:
                out('  %6d  %-34s %s' % (n, 'kickoff', line))


# --------------------------------------------------------------------------------------------------------- self-test

def self_test():
    fails = []

    def check(cond, what):
        print(('  ok    ' if cond else '  FAIL  ') + what)
        if not cond:
            fails.append(what)

    check(djb2x('CommentaryDbEvents') == 0x515CA0C5 and djb2x('player_db_pID') == 0x73D7AD2D, 'djb2-xor hashes')
    check(djb2x('PLAYER_LOW_SIMPLE') == 0x9D0D5E6C, 'event id = djb2-xor of the name')
    real = load_event_names(default_event_files())
    if os.path.isfile(EVENTS_JSON):
        check(real.get(0x9D0D5E6C) == 'PLAYER_LOW_SIMPLE' and real.get(0x0A3EC8A2) == 'PLAYER_LOW_LINK'
              and real.get(0x232D915F) == 'PLAYER_NAME_HIGH', 'repo events JSON: name events mapped')
        bad = [n for i, n in real.items() if djb2x(n) != i]
        check(len(real) > 1600 and not bad, 'repo events JSON: %d names, ids match djb2-xor (%d bad)'
              % (len(real), len(bad)))
    with tempfile.TemporaryDirectory() as d:
        ev_file = os.path.join(d, 'events.json')
        with open(ev_file, 'w', encoding='utf-8') as f:
            json.dump({'events': {'PLAYER_LOW_SIMPLE': '0x9d0d5e6c', 'GOAL_X': '0x00000010'}}, f)
        lst_file = os.path.join(d, 'list.json')
        with open(lst_file, 'w', encoding='utf-8') as f:
            json.dump([{'event': 'SAVE_Y', 'id': '0x00000020'}, {'event': 'GOAL_DUP', 'id': '0x10'}], f)
        names = load_event_names([ev_file, lst_file, os.path.join(d, 'missing.json')])
        check(names == {0x9D0D5E6C: 'PLAYER_LOW_SIMPLE', 0x10: 'GOAL_X', 0x20: 'SAVE_Y'},
              'event files: {"events"} and list layouts, first file wins, missing file skipped')
        log = os.path.join(d, 'callname_voice_log.txt')
        text = '\n'.join([
            '# Turbo voice-swap observe log',
            't=1000 tid=7 ev=0x9D0D5E6C q=0x1A0 pid_before=player_db_pID:278319 pid_after=player_db_pID:216435 '
            'surname=- intensity=2 flags=0x0 guard=0',
            't=1001 tid=7 ev=0x9D0D5E6C q=0x1A0 pid_before=player_db_pID:216435 pid_after=player_db_pID:216435 '
            'surname=- intensity=2 flags=0x0 guard=1',
            't=1500 tid=9 ev=16 q=0x2B0 pid_before=player_db_pID:261865,keeper_pID:0 '
            'pid_after=player_db_pID:0,keeper_pID:0 surname=900000 intensity=- flags=0x3 guard=0',
            't=1600 tid=9 ev=0x00000099 q=0x2C0 pid_before=- pid_after=- surname=922149 intensity=2 flags=0x0 guard=0',
            't=2000 kickoff pid=278319 game=900762 override=-1',
            't=2001 kickoff pid=261865 game=900000 override=-',
            't=2002 kickoff pid=261865 game=900000 override=922149 tid=4',
            'this line is not a log line',
            't=3000 tid=7 ev=0x10 q=0x1A0 pid_before=player_db_pID:1 pid_after=player_db_pID:1,keeper_pID:2 '
            'surname=- intensity=- flags=0x0 guard=0',
            '',
        ])
        with open(log, 'w', encoding='utf-8') as f:
            f.write(text)
        lines = read_log(log)
        s = summarize(lines, names)
        check(s['lines'] == 7 and s['queries'] == 4 and s['kickoffs'] == 3, 'counts: 4 queries, 3 kick-offs')
        check([n for n, _ in s['unparsed']] == [9, 10], 'a stray line and a pid_after of another length: unparsed')
        check(s['comments'] == ['# Turbo voice-swap observe log'], 'comment kept')
        check(s['events'] == Counter({0x9D0D5E6C: 2, 0x10: 1, 0x99: 1}), 'events per id (hex and decimal ev)')
        check(s['rewrites'] == Counter({('player_db_pID', 278319, 216435): 1, ('player_db_pID', 261865, 0): 1})
              and s['rewrite_lines'] == 2, 'rewrites: B -> A and B -> 0 (own recording off)')
        check(s['guard'] == 1 and s['repeat_passes'] == 1, 'guard hit and the double pass on one query')
        check(s['flags'] == Counter({1: 1, 2: 1}) and s['flag_events'] == Counter({0x10: 1}), 'flag bits')
        check(sorted(s['players']) == [216435, 261865, 278319], 'players seen (0 never counted)')
        check(s['players'][278319]['to'] == Counter({216435: 1}) and s['players'][261865]['to'] == Counter({0: 1}),
              'per-player rewrite targets')
        check(s['players'][261865]['surnames'] == Counter({900000: 1}), 'per-player surname_ID')
        check(s['kick'] == {278319: [(900762, -1)], 261865: [(900000, None), (900000, 922149)]},
              'kick-off results per player, override "-" = none')
        check(dict(s['threads']) == {7: Counter({'query': 2}), 9: Counter({'query': 2}), 4: Counter({'kickoff': 1})},
              'threads (a kick-off line with tid=)')
        check(s['intensities'] == Counter({2: 3}) and s['surnames'] == Counter({900000: 1, 922149: 1}),
              'intensity and surname_ID counts, "-" skipped')
        want_p, want_e = parse_filters(['261865'], None, names)
        f1 = summarize(lines, names, want_p, want_e)
        check(f1['queries'] == 1 and f1['kickoffs'] == 2 and sorted(f1['players']) == [261865],
              '--player 261865: his query and his two kick-offs')
        want_p, want_e = parse_filters(['278319,216435'], None, names)
        f2 = summarize(lines, names, want_p, want_e)
        check(f2['queries'] == 2 and f2['kickoffs'] == 1, '--player a,b: either id, before or after the rewrite')
        want_p, want_e = parse_filters(None, ['player_low_simple', 'GOAL_X'], names)
        check(want_e == {0x9D0D5E6C, 0x10} and summarize(lines, names, want_p, want_e)['queries'] == 3,
              '--event by name (any case); kick-off lines left out')
        printed = []
        report(s, names, raw=True, out=printed.append)
        body = '\n'.join(printed)
        check('PLAYER_LOW_SIMPLE' in body and '0x00000099' in body and '278319 -> 216435' in body
              and '900000 -> 922149' in body and 'Guard hits: 1' in body,
              'report: names, unknown id as hex, rewrites, kick-off override')
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            rc = main([log, '--events', ev_file, '--player', '278319', '--raw'])
            rc2 = main([os.path.join(d, 'none.txt')])
        check(rc == 0 and 'Kick-off results (1 players)' in buf.getvalue(), 'main() on the file with --player')
        check(rc2 == 2, 'main(): a missing log is exit 2')
    print('SELF-TEST ' + ('PASS' if not fails else 'FAIL (%d)' % len(fails)))
    return 0 if not fails else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('log', nargs='?', default=DEFAULT_LOG)
    ap.add_argument('--player', action='append', help='player id(s), repeat or comma-separate')
    ap.add_argument('--event', action='append', help='event name or id, repeat or comma-separate')
    ap.add_argument('--events', action='append', default=[], help='extra event-name JSON (after the repo files)')
    ap.add_argument('--top', type=int, default=40, help='rows per section (0 = all)')
    ap.add_argument('--raw', action='store_true', help='also print every selected line')
    ap.add_argument('--self-test', action='store_true')
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if not os.path.isfile(a.log):
        print('no log at %s (turbo_output\\callname_voice_log_on.txt turns it on)' % a.log, file=sys.stderr)
        return 2
    names = load_event_names(default_event_files() + a.events)
    want_p, want_e = parse_filters(a.player, a.event, names)
    s = summarize(read_log(a.log), names, want_p, want_e)
    print('%s (%d event names)' % (a.log, len(names)))
    report(s, names, top=a.top, raw=a.raw)
    return 0


if __name__ == '__main__':
    sys.exit(main())
