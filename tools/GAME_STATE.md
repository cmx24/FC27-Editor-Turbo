# GAME_STATE protocol (brief Part C.7) and how the tools in this folder enforce it

`C:\FC 27 Live Editor\turbo_dev\tools\GAME_STATE.txt` (outside git; env `TURBO_GAME_STATE` overrides the path for tests) says who may
talk to the game right now. Only the coordinator writes it. First line:

```
state: <TOKEN> (HH:MM) - <note>
```

| Token | Meaning | Tools in `tools/` |
|---|---|---|
| `CLOSED` | no coordinator session; the game may be closed OR Cassio's own | every live tool refuses (exit 3); nothing is sent, nothing is killed |
| `USER` | Cassio is playing his own game | refused like CLOSED: nothing is sent, nothing is killed |
| `LAUNCHING` | the coordinator is launching (C.3) | `launch_fc27.ps1` runs ONLY in this state (else exit 6); reads, writes and kills allowed |
| `MATCH-AB` | coordinator-only session (no agent calls) | reads, writes and kills allowed (coordinator) |
| `HUB` / `MATCH` | read-only agent windows (none granted since 10-13 18:05) | read-only tools only; every write (`--allow-write`, dev2 write / key / dump) and every wrapper (they may kill) refuses |
| missing / unreadable / anything else | unknown | refused, like CLOSED |

Enforcement:
- `harness.require_live()` at the start of every Python tool that talks to the game (x1_write, match_measure, chain_check, x2_readout,
  x2_sentinels, x3_sentinels, x3_readback, battery_write); `--mock` / `--self-test` never touch the game and skip it. `test_tools_guard.py` fails the
  PY step if a tool imports dev2 without calling it.
- `dev2.call()` itself refuses to post a request unless the state is live (defence in depth), refuses Escape (VK 27) always, and
  refuses any write / key op at any nesting without `allow_write=True`.
- `gk.ps1` refuses every command except `shot` unless the state is live; `launch_fc27.ps1` refuses unless it is LAUNCHING.
- `harness.require_live(write=True)` / `harness.py state --write` / dev2 for write ops accept only LAUNCHING and MATCH-AB.
- Wrappers (`x1_run.sh`, `x2_run.sh`, `x3_run.sh`, `battery_run.sh`) refuse BEFORE installing their kill trap: a refusal never kills (while CLOSED or
  USER the game may be Cassio's). Once running, they kill FC27 on every exit path (`taskkill /IM FC27.exe /F`) at once (no dev call
  before the kill: a written value dies with the process), but only after re-reading GAME_STATE: if it no longer names a
  coordinator state (LAUNCHING / MATCH-AB), the game is NOT killed.
- Every tool reading Turbo's log uses only the lines after the latest `Turbo GUI loading in process <pid>` and refuses unless that pid
  is the running FC27.exe (`harness.current_log`): an address of an earlier game process is never used.
- `mm_stats.py` and the `--replay` / `--compare` modes of match_measure are offline (data only) and run in any state.

Coordinator sequence (C.2 / C.3 / C.8):

```
printf 'state: LAUNCHING (%s) - <purpose>, coordinator only.\n' "$(date +%H:%M)" > ".../turbo_dev/tools/GAME_STATE.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "<repo>/tools/launch_fc27.ps1" -WaitCloseMin 1
printf 'state: MATCH-AB (%s) - <gate>, coordinator only, no agent calls.\n' "$(date +%H:%M)" > ".../GAME_STATE.txt"
... scripted checks ...
kill (the wrapper does it) -> rm -f <opt-ins> -> rmdir turbo_output/turbo_dev_client.lock -> md5 of CmMgrC* == backup
printf 'state: CLOSED (%s) - <result>\n' "$(date +%H:%M)" > ".../GAME_STATE.txt"
```
