#!/usr/bin/env python3
"""callname_livecheck.py - the 2.20 Callname page live check (brief 2.20-S1-callname, field 5): after ONE short LEICESTER match
start, did the swap the page wrote for one player reach the match? Prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line.
Read-only: it reads the observe log and voice_swaps.json it is given and never touches the game, the install or turbo_output.

  py -3 -I tools/callname_livecheck.py --pid 12345 --callname 900017 [--own-off] [--log LOG] [--store STORE] [--since-t MS]
  py -3 -I tools/callname_livecheck.py --pid 12345 --voice-of 67890 [--log LOG] [--store STORE] [--since-t MS]
  py -3 -I tools/callname_livecheck.py --self-test            (offline: synthetic passing, failing and inconclusive logs)

The Tester's steps (LEICESTER, never saved, about 2 minutes; the runner launches and kills the game):
  0. BEFORE the launch: delete turbo_output\\callname_voice_log.txt (it is appended across sessions; a stale log gives a false PASS) and
     create turbo_output\\callname_voice_log_on.txt (the observe switch). Pick ONE Leicester STARTER without his own recording (the page's
     state line says "Not changed: the game says 'Name'", not "his own recording"); write down his player id.
  1. Career hub, Turbo window: Players -> that player -> Callname. Screenshot S1 (the page: ONE list, search box, Preview, Apply; no tabs,
     no ids). Open Edit Players for him and screenshot his written names (S2). Back on Callname: type in the search box, click ONE
     row (it highlights), press Preview (S3: it plays, or is greyed with a reason), press Apply: "Applied: <name>" and Undo show (S4).
  2. Edit Players again: his written names are the same as in S2 (S5). (This is a screenshot comparison; no log line can show it.)
  3. Start the match (Kick Off / Play Match) and let it run past the kick-off; then kill the game. Never save.
  4. Run this script with his id and the same callname: id of a generic callname (--callname) or the player whose voice it is (--voice-of).
     Undo is checked separately: press Undo before the launch -> voice_swaps.json has no row for him -> run with --expect-none.

Points (what it reads):
  store    voice_swaps.json holds exactly one row for --pid with the expected kick-off id / voice_of (ui_callnames.cpp Apply); with
           --expect-none: no row for him (after Undo)
  kickoff  the observe log has "kickoff pid=<pid> game=<g> override=<id>" with override == the callname id (generic) or -1 (voice-of);
           INCONCLUSIVE when the player never appears (he was not on the pitch: pick a starter; CS218AA missed 2 of 10 players)
  voice    voice-of only: a query line whose pid_before holds him and whose pid_after holds the voice-of id (the rewrite itself);
           with --own-off: pid_after holds 0 where pid_before held him
Lines older than --since-t (the log's t= is GetTickCount64 milliseconds) are ignored.
"""
import argparse
import importlib.util
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_LOG = r'C:\FC 27 Live Editor\turbo_output\callname_voice_log.txt'
DEFAULT_STORE = r'C:\FC 27 Live Editor\turbo_output\callnames\voice_swaps.json'


def _load_log_module():
    path = os.path.join(ROOT, 'scripts', 'callname_voice_log.py')
    spec = importlib.util.spec_from_file_location('callname_voice_log', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_store(path):
    """-> (entries list, error text). A missing file is an empty store."""
    if not os.path.isfile(path):
        return [], 'no store file at ' + path
    try:
        with open(path, encoding='utf-8-sig') as f:
            d = json.load(f)
    except (OSError, ValueError) as e:
        return [], 'unreadable: %s' % e
    ents = d.get('entries') if isinstance(d, dict) else None
    return (ents if isinstance(ents, list) else []), ''


def check_store(entries, err, pid, callname, voice_of, own_off, expect_none):
    rows = [e for e in entries if isinstance(e, dict) and e.get('playerid') == pid]
    if err and not entries:
        return ('FAIL', err) if not expect_none else ('INCONCLUSIVE', err)
    if expect_none:
        return ('PASS', 'no row for %d' % pid) if not rows else ('FAIL', 'row still there: %s' % rows[0])
    if len(rows) != 1:
        return 'FAIL', '%d rows for %d (want exactly 1)' % (len(rows), pid)
    r = rows[0]
    if voice_of is not None:
        ok = r.get('voice_of') == voice_of and r.get('kickoff', -1) == -1
        return ('PASS' if ok else 'FAIL'), 'row: voice_of=%s kickoff=%s' % (r.get('voice_of'), r.get('kickoff'))
    ok = r.get('kickoff') == callname and (r.get('voice_of') == 0 if own_off else 'voice_of' not in r)
    return ('PASS' if ok else 'FAIL'), 'row: kickoff=%s voice_of=%s' % (r.get('kickoff'), r.get('voice_of', '-'))


def check_kickoff(parsed, pid, expect_override):
    ks = [e for e in parsed if e['kind'] == 'kickoff' and e['pid'] == pid]
    if not ks:
        return 'INCONCLUSIVE', 'no kick-off line for %d: he was not on the pitch (pick a starter) or the observe switch was off' % pid
    good = [e for e in ks if e['override'] == expect_override]
    if good:
        return 'PASS', '%d kick-off line(s) with override=%d (game=%d)' % (len(good), expect_override, good[0]['game'])
    return 'FAIL', 'kick-off line(s) for %d but override=%s (want %d)' % (pid, sorted({str(e['override']) for e in ks}), expect_override)


def check_voice(parsed, pid, target):
    qs = [e for e in parsed if e['kind'] == 'query' and any(v == pid for _, v in e['before'])]
    if not qs:
        return 'INCONCLUSIVE', 'no query line holds %d' % pid
    for e in qs:
        for (_, b), (_, a) in zip(e['before'], e['after']):
            if b == pid and a == target:
                return 'PASS', 'a line of his was rewritten %d -> %d (t=%d)' % (pid, target, e['t'])
    return 'FAIL', '%d query line(s) hold %d, none rewritten to %d' % (len(qs), pid, target)


def run(args):
    log = _load_log_module()
    results = []
    entries, err = read_store(args.store)
    results.append(('store', *check_store(entries, err, args.pid, args.callname, args.voice_of, args.own_off, args.expect_none)))
    if args.expect_none:
        pass  # after Undo the match is not examined
    else:
        if not os.path.isfile(args.log):
            results.append(('kickoff', 'INCONCLUSIVE', 'no observe log at ' + args.log))
        else:
            parsed = []
            headers = 0
            for line in log.read_log(args.log):
                e = log.parse_line(line)
                if not e:
                    continue
                if e['kind'] == 'comment':
                    headers += e['text'].startswith('# session') or 0
                    continue
                if e['kind'] in ('query', 'kickoff') and e.get('t', 0) >= args.since_t:
                    parsed.append(e)
            if headers > 1:
                print('  note: the log holds %d session headers: delete it before the launch (stale lines can give a false PASS)' % headers)
            want = args.voice_of is not None
            results.append(('kickoff', *check_kickoff(parsed, args.pid, -1 if want else args.callname)))
            if want:
                results.append(('voice', *check_voice(parsed, args.pid, args.voice_of)))
            elif args.own_off:
                results.append(('voice', *check_voice(parsed, args.pid, 0)))
    for name, verdict, text in results:
        print('%-8s %-12s %s' % (name, verdict, text))
    verdicts = {v for _, v, _ in results}
    final = 'FAIL' if 'FAIL' in verdicts else ('INCONCLUSIVE' if 'INCONCLUSIVE' in verdicts else 'PASS')
    print('RESULT %s (player %d)' % (final, args.pid))
    print('MANUAL: compare his written names in Edit Players before and after Apply (screenshots S2 / S5): equal = PASS.')
    return {'PASS': 0, 'FAIL': 1, 'INCONCLUSIVE': 2}[final]


def self_test():
    ok = [0, 0]

    def check(cond, what):
        ok[0 if cond else 1] += 1
        print('  %s %s' % ('ok  ' if cond else 'FAIL', what))

    with tempfile.TemporaryDirectory() as d:
        store = os.path.join(d, 'voice_swaps.json')
        logp = os.path.join(d, 'callname_voice_log.txt')

        def write(entries, lines):
            with open(store, 'w', encoding='utf-8') as f:
                json.dump({'turbo_voice': 1, 'entries': entries}, f)
            with open(logp, 'w', encoding='utf-8') as f:
                f.write('\n'.join(lines) + '\n')

        def go(extra):
            ns = argparse.Namespace(pid=100, callname=None, voice_of=None, own_off=False, expect_none=False, log=logp, store=store, since_t=0)
            for k, v in extra.items():
                setattr(ns, k, v)
            import io
            import contextlib
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = run(ns)
            return rc, buf.getvalue()

        q = 't=%d tid=1 ev=0x2A q=0x1 pid_before=player_db_pID:%d pid_after=player_db_pID:%d surname=- intensity=- flags=0x0 guard=0'
        # generic callname, passing
        write([{'playerid': 100, 'kickoff': 900017, 'lines': 'all'}], ['# session x', 't=10 kickoff pid=100 game=900002 override=900017', 't=11 kickoff pid=200 game=1 override=-'])
        rc, out = go({'callname': 900017})
        check(rc == 0 and 'RESULT PASS' in out, 'generic callname: store row + kick-off override -> PASS')
        # wrong override -> FAIL
        write([{'playerid': 100, 'kickoff': 900017}], ['t=10 kickoff pid=100 game=900002 override=900004'])
        rc, out = go({'callname': 900017})
        check(rc == 1 and 'RESULT FAIL' in out, 'kick-off with another id -> FAIL')
        # player absent -> INCONCLUSIVE
        write([{'playerid': 100, 'kickoff': 900017}], ['t=10 kickoff pid=200 game=1 override=-'])
        rc, out = go({'callname': 900017})
        check(rc == 2 and 'INCONCLUSIVE' in out, 'player never in the log -> INCONCLUSIVE')
        # no store row -> FAIL
        write([], ['t=10 kickoff pid=100 game=1 override=900017'])
        rc, out = go({'callname': 900017})
        check(rc == 1 and 'store' in out, 'no store row -> FAIL')
        # two rows -> FAIL
        write([{'playerid': 100, 'kickoff': 900017}, {'playerid': 100, 'kickoff': 900018}], ['t=10 kickoff pid=100 game=1 override=900017'])
        rc, out = go({'callname': 900017})
        check(rc == 1, 'two rows for the player -> FAIL')
        # voice-of passing
        write([{'playerid': 100, 'voice_of': 300, 'kickoff': -1}], ['t=10 kickoff pid=100 game=5 override=-1', q % (12, 100, 300)])
        rc, out = go({'voice_of': 300})
        check(rc == 0 and 'RESULT PASS' in out, 'voice-of: store row, silent kick-off, rewrite 100 -> 300 -> PASS')
        # voice-of with no rewrite -> FAIL
        write([{'playerid': 100, 'voice_of': 300, 'kickoff': -1}], ['t=10 kickoff pid=100 game=5 override=-1', q % (12, 100, 100)])
        rc, out = go({'voice_of': 300})
        check(rc == 1 and 'voice' in out, 'voice-of: no rewrite in the log -> FAIL')
        # generic for a player with his own recording: voice_of 0 + kickoff
        write([{'playerid': 100, 'voice_of': 0, 'kickoff': 900017}], ['t=10 kickoff pid=100 game=-1 override=900017', q % (12, 100, 0)])
        rc, out = go({'callname': 900017, 'own_off': True})
        check(rc == 0, 'own recording off + callname -> PASS')
        # since-t drops older lines
        write([{'playerid': 100, 'kickoff': 900017}], ['t=10 kickoff pid=100 game=1 override=900017'])
        rc, out = go({'callname': 900017, 'since_t': 50})
        check(rc == 2, '--since-t ignores older lines -> INCONCLUSIVE')
        # after Undo: no row
        write([], [])
        rc, out = go({'expect_none': True})
        check(rc == 0 and 'RESULT PASS' in out, 'expect-none: no row after Undo -> PASS')
        write([{'playerid': 100, 'kickoff': 900017}], [])
        rc, out = go({'expect_none': True})
        check(rc == 1, 'expect-none: the row is still there -> FAIL')
        # missing log -> INCONCLUSIVE
        write([{'playerid': 100, 'kickoff': 900017}], [])
        os.remove(logp)
        rc, out = go({'callname': 900017})
        check(rc == 2, 'no observe log -> INCONCLUSIVE')
    print('SELF-TEST %s (%d ok, %d failed)' % ('PASSED' if ok[1] == 0 else 'FAILED', ok[0], ok[1]))
    return 0 if ok[1] == 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description='Callname page live check (2.20)')
    ap.add_argument('--pid', type=int, help='the player the swap was applied to')
    ap.add_argument('--callname', type=int, help='generic callname (commentary) id that was applied')
    ap.add_argument('--voice-of', type=int, dest='voice_of', help="player id whose own recording was applied")
    ap.add_argument('--own-off', action='store_true', help='the player has his own recording: Apply turned it off (voice_of 0)')
    ap.add_argument('--expect-none', action='store_true', help='after Undo: the store has no row for him')
    ap.add_argument('--log', default=DEFAULT_LOG)
    ap.add_argument('--store', default=DEFAULT_STORE)
    ap.add_argument('--since-t', type=int, default=0, dest='since_t', help='ignore log lines with t below this (GetTickCount64 ms)')
    ap.add_argument('--self-test', action='store_true')
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if a.pid is None or (a.callname is None and a.voice_of is None and not a.expect_none):
        ap.error('--pid and one of --callname / --voice-of (or --expect-none) are required')
    return run(a)


if __name__ == '__main__':
    sys.exit(main())
