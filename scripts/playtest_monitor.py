#!/usr/bin/env python3
"""Read-only watcher for a long FC 27 + Turbo play session: new problems go to one log the lead can read later.

usage: python scripts/playtest_monitor.py [--until-event] [--until-exit] [--interval 5] [--max-minutes 0]
           [--le-root "C:\\FC 27 Live Editor"] [--out-dir DIR] [--game-dir DIR]
       python scripts/playtest_monitor.py --self-test

It follows (from their current end; a file that appears later is read from its start):
  * <LE>\\turbo_output\\turbo_gui.log and turbo_boot.log (Turbo.dll and the Lua boot)
  * <LE>\\Logs\\live_editor_<dd-mm-yyyy>.log (today's name; else the newest one, so a session past midnight is kept)
and writes to <LE>\\turbo_dev\\playtest\\monitor_<yyyy-mm-dd>.log (appended):
  * ERROR / WARN lines of Live Editor's log, Turbo's own lines ([Turbo]), Lua errors (".lua:<n>:", "attempt to ...")
  * Turbo GUI problems: hook exceptions, frame / overlay errors, hooks that failed to install, missing signatures
  * new crash dumps and Windows error reports for FC 27 / Live Editor (folders below)
  * FC27.exe start and exit times (tasklist, every 10 s; the only process access)
Repeated lines are written 3 times, then counted ("REPEATS" at each heartbeat). Known noise (Live Editor's
"DX12 Error at DETOURS::D3D12::Present", the Patreon login retries) is only counted.

Dump folders on this PC (2026-10-04): %LOCALAPPDATA%\\EA SPORTS FC 27\\CrashDumps (the game's own handler:
CrashDump_<date>.mdmp + minidump-<date>.dmp per crash), %LOCALAPPDATA%\\CrashDumps (Windows LocalDumps,
FC27.exe.<pid>.dmp), C:\\ProgramData\\Microsoft\\Windows\\WER\\ReportArchive and ReportQueue (AppCrash_FC27.exe_*),
the same two under %LOCALAPPDATA%\\Microsoft\\Windows\\WER, and *.dmp / *.mdmp at the top of the game folder,
<LE>, <LE>\\Logs and <LE>\\turbo_output.

Events (printed to stdout too): CRASH_DUMP, WER_REPORT, HOOK_ERROR, TURBO_ERROR, LUA_EXCEPTION.
--until-event: exit 3 after the first event (plus --grace seconds, 10 by default, to log the dump pair and the exit).
--until-exit: exit 4 when FC27.exe exits (after the grace), 3 when an event came first. Else it runs until Ctrl+C or
--max-minutes (exit 0). It lowers its own priority to idle and never touches the game or Live Editor.
"""
import argparse
import contextlib
import csv
import datetime
import fnmatch
import io
import os
import re
import subprocess
import sys
import tempfile
import time

DEFAULT_LE = r'C:\FC 27 Live Editor'
DEFAULT_GAME = r'C:\Program Files\EA Games\EA SPORTS FC 27'
EVENT_KINDS = ('CRASH_DUMP', 'WER_REPORT', 'HOOK_ERROR', 'TURBO_ERROR', 'LUA_EXCEPTION')
REPEAT_LIMIT = 3
EVENT_REPEAT_LIMIT = 20
MAX_CHUNK = 4 << 20
DUMP_GLOBS = ('*.dmp', '*.mdmp')
RELATED = re.compile(r'fc27|fclive|launcher|turbo', re.I)

# ------------------------------------------------------------------------------------------------------ classifiers

LE_LINE = re.compile(r'^(\d\d:\d\d:\d\d(?:\.\d+)?)\t([A-Za-z]+)\t(.*)$')
LUA_ERR = re.compile(r'\.lua:\d+:|stack traceback:|attempt to (?:index|call|perform|compare|concatenate|get length)|'
                     r'bad argument #', re.I)
LE_NOISE = re.compile(r'DX12 Error at DETOURS::D3D12::Present|patreon\.com/api|^Not Authenticated$')
TURBO_TAG = re.compile(r'\[Turbo|imports[/\\]turbo|turbo_boot|Turbo\.dll', re.I)

GUI_LINE = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d(?:\.\d+)?)\s+(.*)$')
GUI_HOOK_ERR = re.compile(r'game hook \S+: exception caught')
GUI_SIG_SUMMARY = re.compile(r'game hooks: (\d+) found, (\d+) missing, (\d+) ambiguous')
GUI_ERR = re.compile(r'exception|frame error|overlay error|disabled for this session|ImGui check failed|'
                     r'MinHook init failed|enabling hooks failed|hooking \S+ failed|queued job failed|bad arguments|'
                     r'did not finish.*stays off|\bERROR\b')
GUI_WARN = re.compile(r'\bfailed\b|\bWARNING\b|not installed|did not finish|kill switch present|stays off|'
                      r'signature \S+: (?:missing|ambiguous)', re.I)
GUI_BENIGN = re.compile(r'TurboProbe\.exe cannot match it, skipped')


def classify_le(line):
    """(kind, text) for a Live Editor log line worth writing, ('NOISE', text), or None."""
    m = LE_LINE.match(line)
    level, msg = (m.group(2).upper(), m.group(3)) if m else ('', line.strip())
    if not msg:
        return None
    if level not in ('INFO', 'DEBUG') and LUA_ERR.search(msg):     # INFO: Turbo's self-test prints its checks
        return ('LUA_EXCEPTION' if TURBO_TAG.search(msg) else 'LUA_ERROR'), msg
    if level in ('ERROR', 'FATAL', 'CRITICAL'):
        if LE_NOISE.search(msg):
            return 'NOISE', msg
        return ('TURBO_ERROR' if TURBO_TAG.search(msg) else 'LE_ERROR'), msg
    if level in ('WARN', 'WARNING'):
        return ('TURBO_WARN' if TURBO_TAG.search(msg) else 'LE_WARN'), msg
    if level == 'INFO' and msg == 'Main Menu reached':
        return 'MILESTONE', msg
    return None


def classify_gui(line):
    """(kind, text) for a turbo_gui.log / turbo_boot.log line worth writing, or None."""
    m = GUI_LINE.match(line)
    msg = (m.group(2) if m else line).strip()
    if not msg or GUI_BENIGN.search(msg):
        return None
    if GUI_HOOK_ERR.search(msg):
        return 'HOOK_ERROR', msg
    s = GUI_SIG_SUMMARY.search(msg)
    if s:
        return ('TURBO_WARN', msg) if int(s.group(2)) or int(s.group(3)) else None
    if LUA_ERR.search(msg):
        return 'LUA_EXCEPTION', msg
    if GUI_ERR.search(msg):
        return 'TURBO_ERROR', msg
    if GUI_WARN.search(msg):
        return 'TURBO_WARN', msg
    return None


def normalize(text):
    return re.sub(r'0x[0-9A-Fa-f]+|[0-9A-Fa-f]{8,}|\d+', '#', text)[:200]


# ------------------------------------------------------------------------------------------------------- following

class Follower:
    """Reads the complete lines appended to a file since the last poll. resolve() names the file to follow."""

    def __init__(self, label, resolve, start_at_end=True):
        self.label, self.resolve, self.start_at_end = label, resolve, start_at_end
        self.path, self.pos, self.buf, self.first = None, 0, b'', True

    def poll(self, note):
        try:
            p = self.resolve()
        except OSError:
            p = None
        if not p or not os.path.isfile(p):
            self.first = False      # a file that appears later is read from its start
            return []
        try:
            size = os.path.getsize(p)
            if p != self.path:
                self.pos = size if (self.first and self.start_at_end) else 0
                note('FOLLOW', self.label, '%s from %s' % (p, 'its end (%d bytes)' % size if self.pos else 'the start'))
                self.path, self.buf, self.first = p, b'', False
            if size < self.pos:
                note('FOLLOW', self.label, '%s shrank to %d bytes: reading it from the start' % (p, size))
                self.pos, self.buf = 0, b''
            if size == self.pos:
                return []
            with open(p, 'rb') as f:
                f.seek(self.pos)
                data = f.read(min(size - self.pos, MAX_CHUNK))
        except OSError:
            return []       # locked or gone for a moment: the next poll tries again
        self.pos += len(data)
        parts = (self.buf + data).split(b'\n')
        self.buf = parts.pop()[-65536:]
        return [x.rstrip(b'\r').decode('utf-8', 'replace') for x in parts]


def le_log_resolver(logs_dir, today):
    def resolve():
        p = os.path.join(logs_dir, 'live_editor_%s.log' % today().strftime('%d-%m-%Y'))
        if os.path.isfile(p):
            return p
        best, best_m = None, -1.0
        with contextlib.suppress(OSError):
            for n in os.listdir(logs_dir):
                if re.fullmatch(r'live_editor_\d\d-\d\d-\d{4}\.log', n, re.I):
                    m = os.path.getmtime(os.path.join(logs_dir, n))
                    if m > best_m:
                        best, best_m = os.path.join(logs_dir, n), m
        return best
    return resolve


def game_dir_from_launcher(logs_dir):
    try:
        logs = sorted((os.path.join(logs_dir, n) for n in os.listdir(logs_dir)
                       if n.lower().startswith('live_editor_launcher_')), key=os.path.getmtime, reverse=True)
        for p in logs[:3]:
            with open(p, 'rb') as f:
                for raw in f:
                    i = raw.find(b'Game Install Dir: ')
                    if i >= 0:
                        return raw[i + 18:].decode('utf-8', 'replace').strip().rstrip('\\/')
    except OSError:
        pass
    return None


def dump_sources(le_root, game_dir, env=os.environ):
    """[(label, folder, kind)]: kind 'files' = dump files (*.dmp, *.mdmp), 'wer' = report folders."""
    la = env.get('LOCALAPPDATA', '')
    pd = env.get('PROGRAMDATA', r'C:\ProgramData')
    out = [('ea', os.path.join(la, 'EA SPORTS FC 27', 'CrashDumps'), 'files'),
           ('localdumps', os.path.join(la, 'CrashDumps'), 'files')]
    for base, tag in ((pd, 'wer'), (la, 'wer-user')):
        for sub in ('ReportArchive', 'ReportQueue'):
            out.append(('%s %s' % (tag, sub), os.path.join(base, 'Microsoft', 'Windows', 'WER', sub), 'wer'))
    for label, d in (('game', game_dir), ('le', le_root), ('le logs', os.path.join(le_root, 'Logs')),
                     ('turbo_output', os.path.join(le_root, 'turbo_output'))):
        if d:
            out.append((label, d, 'files'))
    return out


def list_dumps(folder, kind):
    try:
        names = os.listdir(folder)
    except OSError:
        return set()
    if kind == 'wer':
        return {n for n in names if n.lower().startswith(('appcrash_', 'apphang_', 'bex64_', 'critical_'))}
    return {n for n in names if any(fnmatch.fnmatch(n.lower(), g) for g in DUMP_GLOBS)}


def fc27_pids():
    """PIDs of FC27.exe from tasklist, or None when tasklist could not run."""
    try:
        r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq FC27.exe', '/FO', 'CSV', '/NH'], capture_output=True,
                           text=True, timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except (OSError, subprocess.SubprocessError):
        return None
    pids = set()
    for row in csv.reader(r.stdout.splitlines()):
        if len(row) >= 2 and row[0].lower() == 'fc27.exe' and row[1].isdigit():
            pids.add(int(row[1]))
    return pids


# --------------------------------------------------------------------------------------------------------- monitor

class Monitor:
    def __init__(self, le_root, out_dir, game_dir=None, now=time.time, today=datetime.date.today, pids=fc27_pids,
                 env=os.environ, echo=print, heartbeat=600, proc_every=10):
        self.le_root, self.now, self.pids_fn, self.echo = le_root, now, pids, echo
        self.heartbeat, self.proc_every = heartbeat, proc_every
        out_dir = out_dir or os.path.join(le_root, 'turbo_dev', 'playtest')
        os.makedirs(out_dir, exist_ok=True)
        self.out_path = os.path.join(out_dir, 'monitor_%s.log' % today().strftime('%Y-%m-%d'))
        logs = os.path.join(le_root, 'Logs')
        tout = os.path.join(le_root, 'turbo_output')
        self.game_dir = game_dir or game_dir_from_launcher(logs) or DEFAULT_GAME
        self.followers = [(Follower('turbo_gui', lambda: os.path.join(tout, 'turbo_gui.log')), classify_gui),
                          (Follower('turbo_boot', lambda: os.path.join(tout, 'turbo_boot.log')), classify_gui),
                          (Follower('live_editor', le_log_resolver(logs, today)), classify_le)]
        self.voice_log = os.path.join(tout, 'callname_voice_log.txt')
        self.sources = dump_sources(le_root, self.game_dir, env)
        self.seen = {folder: list_dumps(folder, kind) for _, folder, kind in self.sources}
        self.counts, self.suppressed, self.noise = {}, {}, {}
        self.events, self.first_event, self.game_exit_at = 0, None, None
        self.pids, self.last_proc, self.last_beat = None, -1e18, self.now()
        self.lines_read = 0
        self.write('MONITOR', 'start', 'le %s, game %s, out %s' % (le_root, self.game_dir, self.out_path))
        for label, folder, _ in self.sources:
            if os.path.isdir(folder):
                self.write('MONITOR', 'dumps', '%s: %s (%d now)' % (label, folder, len(self.seen[folder])))

    # one output line; events also go to stdout
    def write(self, kind, source, text):
        stamp = datetime.datetime.fromtimestamp(self.now()).strftime('%Y-%m-%d %H:%M:%S')
        line = '%s  %-13s %s: %s' % (stamp, kind, source, text)
        with open(self.out_path, 'a', encoding='utf-8') as f:
            f.write(line + '\n')
        if kind in EVENT_KINDS or kind in ('GAME_START', 'GAME_EXIT', 'EXIT'):
            self.echo(line)
            sys.stdout.flush()

    def record(self, kind, source, text):
        if kind == 'NOISE':
            key = normalize(text)[:60]
            self.noise[key] = self.noise.get(key, 0) + 1
            return
        is_event = kind in EVENT_KINDS
        if is_event:
            self.events += 1
            if self.first_event is None:
                self.first_event = self.now()
        key = (kind, source, normalize(text))
        n = self.counts[key] = self.counts.get(key, 0) + 1
        if n <= (EVENT_REPEAT_LIMIT if is_event else REPEAT_LIMIT):
            self.write(kind, source, text[:600])
        else:
            self.suppressed[key] = (self.suppressed.get(key, (0, text))[0] + 1, text)

    def flush_repeats(self):
        for (kind, source, _), (n, text) in sorted(self.suppressed.items(), key=lambda kv: -kv[1][0]):
            self.write('REPEATS', source, '%d more %s like: %s' % (n, kind, text[:300]))
        self.suppressed = {}
        if self.noise:
            self.write('NOISE', 'live_editor', '; '.join('%d x %s' % (n, k) for k, n in
                                                         sorted(self.noise.items(), key=lambda kv: -kv[1])))
            self.noise = {}

    def poll_process(self):
        if self.now() - self.last_proc < self.proc_every:
            return
        self.last_proc = self.now()
        pids = self.pids_fn()
        if pids is None:
            return
        if self.pids is None:
            self.write('GAME', 'tasklist', 'FC27.exe running, pid %s' % ', '.join(map(str, sorted(pids)))
                       if pids else 'FC27.exe not running')
        else:
            for p in sorted(pids - self.pids):
                self.write('GAME_START', 'tasklist', 'FC27.exe started, pid %d' % p)
            for p in sorted(self.pids - pids):
                self.write('GAME_EXIT', 'tasklist', 'FC27.exe exited, pid %d' % p)
            if self.pids and not pids:
                self.game_exit_at = self.now()
            if pids:
                self.game_exit_at = None
        self.pids = pids

    def poll_dumps(self):
        for label, folder, kind in self.sources:
            now = list_dumps(folder, kind)
            for n in sorted(now - self.seen[folder]):
                p = os.path.join(folder, n)
                related = bool(RELATED.search(n)) or label == 'ea'
                size = ''
                with contextlib.suppress(OSError):
                    if os.path.isfile(p):
                        size = ', %d bytes' % os.path.getsize(p)
                if kind == 'wer':
                    self.record('WER_REPORT' if related else 'OTHER_REPORT', label, p)
                else:
                    self.record('CRASH_DUMP' if related else 'OTHER_DUMP', label, p + size)
            self.seen[folder] = now

    def poll(self):
        self.poll_process()
        for fol, classify in self.followers:
            lines = fol.poll(self.write)
            self.lines_read += len(lines)
            for line in lines:
                c = classify(line)
                if c:
                    self.record(c[0], fol.label, c[1])
        self.poll_dumps()
        if self.now() - self.last_beat >= self.heartbeat:
            self.beat()

    def beat(self):
        self.last_beat = self.now()
        self.flush_repeats()
        voice = ''
        with contextlib.suppress(OSError):
            voice = ', callname_voice_log.txt %d bytes' % os.path.getsize(self.voice_log)
        game = ('running, pid %s' % ', '.join(map(str, sorted(self.pids)))) if self.pids else 'not running'
        self.write('ALIVE', 'monitor', 'FC27.exe %s, %d lines read, %d events%s' % (game, self.lines_read,
                                                                                   self.events, voice))


def run(m, interval=5.0, grace=10.0, until_event=False, until_exit=False, max_minutes=0.0, sleep=time.sleep,
        max_polls=None):
    """Poll until a stop rule fires; returns the exit code (3 event, 4 game exit, 0 time limit / max_polls)."""
    start, polls = m.now(), 0
    try:
        while True:
            m.poll()
            polls += 1
            t = m.now()
            code = None
            if until_event and m.first_event is not None and t - m.first_event >= grace:
                code = 3
            elif until_exit and m.game_exit_at is not None and t - m.game_exit_at >= grace:
                code = 3 if m.first_event is not None else 4
            elif max_minutes and t - start >= max_minutes * 60:
                code = 0
            elif max_polls is not None and polls >= max_polls:
                code = 0
            if code is not None:
                m.flush_repeats()
                m.write('EXIT', 'monitor', 'exit %d after %d events (%s)' % (
                    code, m.events, {3: 'event', 4: 'FC27.exe exited', 0: 'time limit'}[code]))
                return code
            sleep(interval)
    except KeyboardInterrupt:
        m.flush_repeats()
        m.write('EXIT', 'monitor', 'stopped (Ctrl+C) after %d events' % m.events)
        return 0


def lower_own_priority():
    with contextlib.suppress(Exception):
        import ctypes
        k = ctypes.windll.kernel32
        k.SetPriorityClass(k.GetCurrentProcess(), 0x40)    # IDLE_PRIORITY_CLASS, this process only


# --------------------------------------------------------------------------------------------------------- self-test

def self_test():
    fails = []

    def check(cond, what):
        print(('  ok    ' if cond else '  FAIL  ') + what)
        if not cond:
            fails.append(what)

    # classifiers on real line shapes (Live Editor 27.1.2, Turbo.dll)
    check(classify_le('16:02:52.873991\tERROR\tresponse from: https://www.patreon.com/api/oauth2/v2/identity?x')[0]
          == 'NOISE', 'LE: Patreon retry is noise')
    check(classify_le('18:51:07.1\tERROR\tDX12 Error at DETOURS::D3D12::Present::NewCode 0x80070057')[0] == 'NOISE',
          'LE: DX12 Present error is noise')
    check(classify_le('17:16:28.546223\tWARN\t[LUA API] [Turbo] export_table stopped: table not found: x')[0]
          == 'TURBO_WARN', 'LE: a Turbo WARN')
    check(classify_le('10:00:00.0\tERROR\t[LUA API] [Turbo] callnames: write failed')[0] == 'TURBO_ERROR',
          'LE: a Turbo ERROR')
    check(classify_le('10:00:00.0\tWARN\t[LUA API] [Turbo] bridge state: C:/FC 27 Live Editor/lua/libs/v2/imports/'
                      'turbo/bridge.lua:88: attempt to index a nil value')[0] == 'LUA_EXCEPTION',
          'LE: a Lua error in Turbo code is an exception')
    check(classify_le('10:00:00.0\tERROR\tlua/autorun/other.lua:3: attempt to call a nil value')[0] == 'LUA_ERROR',
          'LE: a Lua error elsewhere is logged, not an event')
    check(classify_le('10:00:00.0\tERROR\tTeam Not Found (ID: 5)')[0] == 'LE_ERROR', 'LE: other ERROR')
    check(classify_le('18:51:07.744678\tINFO\tWaiting for main menu') is None, 'LE: INFO skipped')
    check(classify_le('10:00:00.0\tINFO\t[LUA API] [Turbo self-test] OK   x = C:\\FC 27 Live Editor\\lua\\a.lua:5: y')
          is None, 'LE: an INFO line quoting a Lua error (self-test output) skipped')
    check(classify_le('stack traceback:')[0] == 'LUA_ERROR', 'LE: a continuation line of a Lua error')
    check(classify_le('        add("GetDBMeta error: " .. tostring(m))') is None, 'LE: a logged Lua source line skipped')
    g = '2026-10-04 14:52:04.571  '
    check(classify_gui(g + 'game hook callname_voice: exception caught in the detour: access violation')[0]
          == 'HOOK_ERROR', 'GUI: hook exception')
    check(classify_gui(g + 'frame error: bad alloc')[0] == 'TURBO_ERROR', 'GUI: frame error')
    check(classify_gui(g + 'hooking Present failed: MH_ERROR_NOT_EXECUTABLE')[0] == 'TURBO_ERROR',
          'GUI: a hook that failed to install')
    check(classify_gui(g + 'the last two Turbo GUI starts did not finish (x): the game may have crashed. Turbo GUI '
                           'stays off.')[0] == 'TURBO_ERROR', 'GUI: crash guard keeps Turbo off')
    check(classify_gui(g + 'the previous Turbo GUI start did not finish (x): the game may have crashed or been '
                           'closed. Trying once more')[0] == 'TURBO_WARN', 'GUI: crash guard retry is a warning')
    check(classify_gui(g + 'game hooks: 50 found, 0 missing, 0 ambiguous, 0 skipped in 80 ms') is None,
          'GUI: a clean signature summary skipped')
    check(classify_gui(g + 'game hooks: 48 found, 2 missing, 0 ambiguous, 0 skipped in 80 ms')[0] == 'TURBO_WARN',
          'GUI: missing signatures warned')
    check(classify_gui(g + 'game thread: WARNING the game tick runs on thread 1 but ...')[0] == 'TURBO_WARN',
          'GUI: WARNING line')
    check(classify_gui(g + 'the game uses its own Direct3D 12 runtime (x): TurboProbe.exe cannot match it, skipped')
          is None, 'GUI: the TurboProbe note skipped')
    check(classify_gui(g + 'game hook pc_slot installed at 0x1470F2A80 (kill switch: x)') is None,
          'GUI: install line skipped')

    with tempfile.TemporaryDirectory() as d:
        le = os.path.join(d, 'LE')
        la = os.path.join(d, 'local')
        pdata = os.path.join(d, 'pd')
        game = os.path.join(d, 'game')
        out_dir = os.path.join(d, 'out')
        for x in (os.path.join(le, 'Logs'), os.path.join(le, 'turbo_output'), game,
                  os.path.join(la, 'EA SPORTS FC 27', 'CrashDumps'), os.path.join(la, 'CrashDumps'),
                  os.path.join(pdata, 'Microsoft', 'Windows', 'WER', 'ReportArchive')):
            os.makedirs(x)
        clock = [1_800_000_000.0]
        today = [datetime.date(2026, 10, 4)]
        pids = [set()]
        echoed = []
        gui = os.path.join(le, 'turbo_output', 'turbo_gui.log')
        lelog = os.path.join(le, 'Logs', 'live_editor_04-10-2026.log')

        def put(p, text, mode='a'):
            with open(p, mode, encoding='utf-8', newline='') as f:
                f.write(text)

        put(gui, '2026-10-04 10:00:00.000  frame error: old, before the monitor\n', 'w')
        put(lelog, '09:00:00.000000\tERROR\t[LUA API] [Turbo] old error\n', 'w')
        put(os.path.join(le, 'Logs', 'live_editor_launcher_04-10-2026.log'),
            '04:15:33.812493\tINFO\tGame Install Dir: %s\\\n' % game, 'w')
        put(os.path.join(la, 'EA SPORTS FC 27', 'CrashDumps', 'CrashDump_old.mdmp'), 'x', 'w')
        env = {'LOCALAPPDATA': la, 'PROGRAMDATA': pdata}
        m = Monitor(le, out_dir, now=lambda: clock[0], today=lambda: today[0], pids=lambda: set(pids[0]), env=env,
                    echo=echoed.append, heartbeat=600, proc_every=10)
        check(m.game_dir == game, 'game folder read from the launcher log')
        check(m.out_path == os.path.join(out_dir, 'monitor_2026-10-04.log'), 'output monitor_<date>.log')
        m.poll()

        def out_text():
            with open(m.out_path, encoding='utf-8') as f:
                return f.read()

        t = out_text()
        check('before the monitor' not in t and 'old error' not in t and 'CrashDump_old' not in t,
              'existing lines and dumps are not reported')
        check('FC27.exe not running' in out_text(), 'first process state written')

        put(gui, '2026-10-04 15:00:00.000  game hook callname_voice: exception caught in the detour: AV\n'
                 '2026-10-04 15:00:00.001  game hooks: 50 found, 0 missing, 0 ambiguous, 0 skipped in 9 ms\n'
                 '2026-10-04 15:00:00.002  dev service: request 7 (region) failed: not in a readable region\n'
                 '2026-10-04 15:00:00.003  frame error: half a li')
        put(lelog, ''.join('18:00:%02d.000000\tERROR\tDX12 Error at DETOURS::D3D12::Present::NewCode 0x80070057\n'
                           % i for i in range(50)))
        put(lelog, ''.join('18:01:%02d.000000\tWARN\t[LUA API] [Turbo] player_moves stopped: unlist x%d\n' % (i, i)
                           for i in range(6)))
        put(lelog, '18:02:00.000000\tERROR\tTeam Not Found (ID: 5)\n')
        clock[0] += 5
        m.poll()
        t = out_text()
        check('HOOK_ERROR' in t and 'callname_voice' in t and m.events == 1, 'hook exception is an event')
        check('TURBO_WARN' in t and 'dev service' in t, 'a failed dev request is a warning')
        check('50 found' not in t and 'DX12' not in t and 'half a li' not in t,
              'clean summary, noise and a half-written line are not written')
        check(t.count('player_moves stopped') == REPEAT_LIMIT, 'repeats written %d times, then counted' % REPEAT_LIMIT)
        check('LE_ERROR' in t and 'Team Not Found' in t, 'other LE errors written')
        check(any('HOOK_ERROR' in e for e in echoed), 'events echoed to stdout')

        put(gui, 'ne\n')
        pids[0] = {4242}
        clock[0] += 11
        m.poll()
        t = out_text()
        check('frame error: half a line' in t, 'the half line is read once complete')
        check('GAME_START' in t and 'pid 4242' in t, 'FC27.exe start')

        put(os.path.join(la, 'EA SPORTS FC 27', 'CrashDumps', 'CrashDump_2026.10.04_15.01.00.000.mdmp'), 'MDMP', 'w')
        put(os.path.join(la, 'CrashDumps', 'Other.exe.1.dmp'), 'x', 'w')
        put(os.path.join(la, 'CrashDumps', 'FC27.exe.4242.dmp'), 'x', 'w')
        os.makedirs(os.path.join(pdata, 'Microsoft', 'Windows', 'WER', 'ReportArchive', 'AppCrash_FC27.exe_ab_cd'))
        pids[0] = set()
        clock[0] += 11
        m.poll()
        t = out_text()
        check('CRASH_DUMP' in t and 'CrashDump_2026.10.04_15.01.00.000.mdmp, 4 bytes' in t, 'the game\'s own dump')
        check('FC27.exe.4242.dmp' in t and 'OTHER_DUMP' in t and 'Other.exe.1.dmp' in t,
              'LocalDumps: FC27 is an event, another program is logged only')
        check('WER_REPORT' in t and 'AppCrash_FC27.exe_ab_cd' in t, 'Windows error report folder')
        check('GAME_EXIT' in t and m.game_exit_at == clock[0], 'FC27.exe exit')
        check(m.events == 5, 'events counted: hook, frame error, dump, LocalDumps, WER (%d)' % m.events)

        put(gui, 'x\n', 'w')
        clock[0] += 5
        m.poll()
        check('shrank' in out_text(), 'a truncated log is read again from its start')

        today[0] = datetime.date(2026, 10, 5)
        put(os.path.join(le, 'Logs', 'live_editor_05-10-2026.log'),
            '00:10:00.000000\tERROR\t[LUA API] [Turbo] new day error\n', 'w')
        clock[0] += 5
        m.poll()
        t = out_text()
        check('live_editor_05-10-2026.log from the start' in t and 'new day error' in t,
              'a new day\'s Live Editor log is followed from its start')

        clock[0] += 600
        m.poll()
        t = out_text()
        check('REPEATS' in t and '3 more TURBO_WARN' in t, 'heartbeat writes the repeat counts')
        check('NOISE' in t and '50 x' in t and 'ALIVE' in t, 'heartbeat writes the noise count and an alive line')

        # stop rules
        m2 = Monitor(le, out_dir, now=lambda: clock[0], today=lambda: today[0], pids=lambda: set(pids[0]), env=env,
                     echo=echoed.append)

        def tick(_):
            clock[0] += 5
        m2.poll()
        put(gui, '2026-10-05 00:20:00.000  overlay error: x; Turbo GUI disabled for this session\n')
        rc = run(m2, interval=5, grace=10, until_event=True, sleep=tick, max_polls=50)
        check(rc == 3 and m2.first_event is not None, '--until-event: exit 3 after the grace')
        pids[0] = {77}
        m3 = Monitor(le, out_dir, now=lambda: clock[0], today=lambda: today[0], pids=lambda: set(pids[0]), env=env,
                     echo=echoed.append, proc_every=0)

        def tick_exit(_):
            clock[0] += 5
            pids[0] = set()
        rc = run(m3, interval=5, grace=10, until_exit=True, sleep=tick_exit, max_polls=50)
        check(rc == 4, '--until-exit: exit 4 when FC27.exe exits')
        check(run(m3, max_polls=2, sleep=tick) == 0, 'max polls: exit 0')
        check('EXIT' in out_text(), 'exit line written')
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(['--le-root', le, '--out-dir', out_dir, '--max-minutes', '0.0001', '--interval', '0'])
        check(rc == 0, 'main() runs and stops at --max-minutes')
    print('SELF-TEST ' + ('PASS' if not fails else 'FAIL (%d)' % len(fails)))
    return 0 if not fails else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--le-root', default=DEFAULT_LE)
    ap.add_argument('--out-dir', help='default <LE>\\turbo_dev\\playtest')
    ap.add_argument('--game-dir', help='default: from the Live Editor launcher log')
    ap.add_argument('--interval', type=float, default=5.0, help='seconds between polls')
    ap.add_argument('--heartbeat', type=float, default=600.0, help='seconds between ALIVE / REPEATS lines')
    ap.add_argument('--grace', type=float, default=10.0, help='seconds kept after the stop event')
    ap.add_argument('--until-event', action='store_true', help='exit 3 after the first crash / dump / Turbo error')
    ap.add_argument('--until-exit', action='store_true', help='exit 4 when FC27.exe exits')
    ap.add_argument('--max-minutes', type=float, default=0.0, help='0 = no limit')
    ap.add_argument('--self-test', action='store_true')
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    lower_own_priority()
    m = Monitor(a.le_root, a.out_dir, a.game_dir, heartbeat=a.heartbeat)
    print('playtest monitor: writing %s' % m.out_path)
    sys.stdout.flush()
    return run(m, a.interval, a.grace, a.until_event, a.until_exit, a.max_minutes)


if __name__ == '__main__':
    sys.exit(main())
