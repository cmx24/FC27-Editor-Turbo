# Turbo X — RESUME HERE (state at 2026-11-01 15:00 EST, Turbo X 1.0.2 installed)

Read this first if you are an LLM/agent picking up the Turbo project. It is the single entry point; it links everything else.
Owner: Cassio. Project rules on his PC: `C:\FC 27 Live Editor\CLAUDE.md`, `C:\FC 27 Live Editor\turbo_dev\PITWALL\` (CLAUDE.md, PITWALL.md, BOARD.md).

## 1. Where things are

| What | Where |
|---|---|
| Installed build | **Turbo X 1.0.2**, `Turbo.dll` md5 `e7a48c96156bb97aa00046faaf659edf`, installed 2026-11-01 14:55 EST, INSTALL PASS (120 files, 83,284 user files unchanged) |
| Undo to 1.0 | `C:\FC 27 Live Editor\turbo_backups\install_20261101_145355\Undo-Turbo.cmd` |
| Source of 1.0.2 | GitHub `cmx24/FC27-Editor-Turbo`, branch **`main`** (= `tx-102-int`); product code = commit `bec9b74` (later commits on main are docs/tools only) |
| Package | `dist/FC27_LE_Turbo_X_1.0.2.zip` (sha256 `c6abe740f521798a4d6cd820bc2e242ea5430142bbee545dfee775d9a27f1172`), PC copy `C:\FC_Tools\TurboX\x102\` (+ unpacked `pkg\`) |
| Installer used | `C:\FC 27 Live Editor\_INSTALL_TURBOX_X102.cmd` (log: `turbo_dev\evidence\2026-11-01\x102\install_x102.txt`) |
| Today's plans, audits, reports | `C:\FC 27 Live Editor\turbo_dev\turbox_20_assessment_20261101\` (00–08 plans/reviews, 09 picker audit, 10 consult brief) and its `handover\` (11 hand-over prep, 12 render throughput, 13 offline GPU render, 14 render look spec, 15 game-lane report, 16 FCM27 coexistence) |
| Release notes / changelog / day log | `docs/turbo-x/RELEASE_NOTES.md`, `CHANGELOG.md`, `docs/turbo-x/BUILD_LOG.md` |

## 2. What 1.0.2 contains (on top of 1.0 final12)

1.0.1 (e797310): real-face PLAYER has ONE hair/beard editor (faces-gate card; Look card drops only its Hair/Beard rows) — the owner's
bug; club-voice ▶ Play on the Commentary card; DX12 SRV heap 512→2048 with a reserved fallback descriptor (slot 0 never reused).
Round 2: R2-P hair/beard tile order = Turbo in-game capture (`turbo_output\pictures\raw\<field>\<id>.png` or generator manifest
"done") > FC 27 catalog preview > FC Editor FC27 picture > RDBM26 (tile, big preview, tooltip agree); R2-O1 the real-face
"Change his hair and beard" fold opens by default; R2-O2 ▶ plays the clip in effect (donor if set); R2-F picker filters per field
(show_blank, fav_only, group, hair facets) and Players/Clubs list filters persist in gui_settings.json (search text resets);
R2-R split panes commit only real drags, never-dragged pages open at the last dragged width of the same id, 8 dialogs remember
their size (modal_size.h); capture A (no false "did not finish in 75 s" timeouts: start time recorded before `ran`) and B
(stale-capture guard); tattoo tiles = design picture from the game's tattoo texture > FC 27 preview > none.
Test-only commits: Miniface case no longer SIGSEGVs the suite (dad60a3); R2-F list test fix (a305d14); UI test world uses a
missing FC Editor folder (b2094a3). Tools: `tools/attach/` (Attach-Turbo), `turbo/tools/tattoo/` (tattoo design extractor).

Pictures produced today (data, in `turbo_output`, not in the package): 1,309 hair + 133 beard rendered IN GAME by FC 27
(gear generator, 0 failed, 4 min), copied into `pictures\raw\` so R2-P ranks them first (raw: hair 1,447, beard 148);
381 tattoo design tiles `pictures\assets\tattoo_design\<id>.png` (+ DDS cache `turbo_output\cache\tattoo_design_dds\`).
Pre-render backup: `turbo_backups\pictures_prerender_20261101`; raw files replaced: `turbo_backups\raw_before_x102`.

## 3. Proof status (be honest about it)

- Native tests: PC full suite (MSYS2 UCRT64) on bec9b74: 45,010 passed / 1,884 failed, no crash; every failing case also fails on
  1.0.1 (PC base list + cloud base list) except ONE timing-flaky case "Kits page: Club name, colours & crest has its three
  cards" (Fable: frame-diff page_text picks up async kit-worker text; no product code on that page changed). Each lane's new
  tests went red on old code and green after (logs in the cloud scratchpad are gone; numbers are in BUILD_LOG.md).
- Cloud vs PC: the cloud container fails ~155 UI cases for environment reasons; 1.0.1 vs 1.0 = identical failure sets (0 new).
- Fable (reviewer) final verdict: GO WITH NOTE.
- **NOT SEEN IN GAME**: every 1.0.2 change except the 1.0.1 one-picker fix's code path. First owner session checklist:
  one hair/beard editor on a real face (open); hair/beard tiles match the game after Apply; tattoo tiles show designs; filters and
  dragged sizes survive a restart; club ▶ plays the name in effect.

## 4. Open work (priority order)

1. **In-game proof of 1.0.2** (checklist above) — owner session or a LEICESTER driver run (see §6 for the FCM boot problem).
2. **R2-A deferred**: wire `draw_player_contract_actions(App&)` (ui_team_page40.h:16, ui_team_players.cpp:~299, zero callers)
   into the player Profile. Branch `tx-102-r2a` (aa1e360 + c88c7ba) — DO NOT MERGE: the Profile's controls are not even found by
   the test (R2-A case 818/13) and "team-pages 4.0" goes 870/6 ("My team preset" asks nothing). Pending.host idea was wrong.
   Unchecked leads: controls clipped below the Profile's `##editor` pane; Profile move controls write shared state every frame
   (`app.sel_player` via squad_combo, `TeamMoveInputs.to_team` via club_search_box).
3. **Driver vs FCM 27**: with FCM installed the game boots into an Italian "Carica / File impostazioni / Personal Settings 1"
   screen; `gui40_drive.ps1` boot keys don't handle it and its main-menu gate accepts log lines older than the current launch
   (stale-gate bug). The game now runs elevated: agents cannot kill it or send it keys. Fix the gate, teach the boot sequence.
4. **FCM27 coexistence**: Turbo already loads inside the same Live Editor FCM uses (`C:\FC 27 Live Editor`, le_config now has
   `-dataPath FIFAModData` and `CryptBase.dll` = FrostyModSupport proxy). `tools/attach/Attach-Turbo.cmd` (rerun after each FCM
   update) is statically validated and Linux-simulated, **never run on Windows**. Design + G3 list: handover\16.
5. Tattoo leftovers: some designs are split across texture islands (shown side by side); `tattoo_designs.py` needs Pillow for
   Windows `py -3` (not installed; tiles were drawn in the PC's Linux VM).
6. Flaky test hardening (Kits page case: snapshot two base frames). R2-O2 donor-case test does not go red on old code (behaviour
   traced, not proven). Disabled ▶ stand-in is a blank grey square.
7. Offline GPU renderer (`render_tiles.py`, Blender bpy, CUDA/OptiX auto) built and proven on synthetic assets only; FC 27 hair is
   strand-based, FET export of strands unverified (handover\13, \14). Parked: in-game rendering is ~1 s per 6 heads.
8. Old picker code deletion (`docs/turbo-x/PICKER_DELETION_LIST.md`), tag/release (proxy refuses tags from the cloud).

## 5. How to build and test (lessons from today — read before running anything)

- Cloud container: 2 CPUs, **6.27 GB memory cgroup shared by all agents**. `test_main.cpp` at -O1 -g takes ~1.6 GB: run every
  native test/compile through one lock: `source ccache_env.sh && flock <lock> env TURBO_JOBS=1 ...`. Use ccache
  (`PATH=/usr/lib/ccache:$PATH CCACHE_BASEDIR=<root>`). Unlocked parallel compiles were OOM-killed ~20 times today.
- Cross-build: `bash turbogui/scripts/build_win.sh` (MinGW posix; ~12 min cold, 1–6 min incremental). **MinGW is stricter than
  Linux g++** (R2-A compiled on Linux, not on MinGW): always cross-build before calling a lane green. Package: `bash scripts/package.sh`
  (reads version from `kGuiVersion` + Lua `version.lua`; they must match).
- Native suite: `SANITIZE="" TURBO_JOBS=n bash turbogui/tests/native/run_native.sh`; filter with `TURBO_TEST_ONLY="<substring>"`.
  Needs LE Lua libs (gitignored) in `turbo/le27/libs` — source: the live `C:\FC 27 Live Editor\lua\libs` (not le27_libs_1024.tgz).
- **PC test runner (fastest)**: `C:\FC 27 Live Editor\turbo_dev\pcbuild\RUN_PC_TESTS.cmd` (MSYS2 UCRT64, belownormal, refuses
  under 260 GB free; branches in `set "BRANCHES=..."`, `JOBS_EACH` in pc_tests.sh). Results: `pcbuild\results\summary.txt`,
  `<run>.fails`. PC base runs abort at the Miniface case (they lack dad60a3), so compare post-crash cases with a full cloud base.
- Driving the PC: `device_bash` = Linux VM (no Windows processes). Windows steps = a CRLF .cmd double-clicked in File Explorer via
  computer-use, then release the lock. Blockers seen today: a maximised EA app or the Claude window in front (clicks land nowhere,
  keys refused) — ask the owner to minimise it; the elevated game cannot be closed by agents.
- Always check result folders before asking the owner to do something (today an already-finished PC run was missed for 25 min).

## 6. Game lane rules

LEICESTER test career only, never saved; never TORINO/EVERTON/LB/Southampton/Newcastle. Lease file, evidence folders and tags
must be new per run (a reused tag folder refuses with exit 28). The gear generator (request file
`turbo_output\pictures\gear_thumbs\request.json`, 6 reference players 254243, 203980, 213516, 230375, 245158, 253109) works with
the owner-loaded career and no keys: it waits for in-career + capture service ready. `manifest.json` holds only the LAST run —
keep copies (today's: `turbo_dev\evidence\2026-11-01\x101\render\`).

## 7. Working agreement (owner)

Opus runs point and assigns work; Fable is the final reviewer/approver of "ready for Cassio's review". "CELERA" = use all
resources (CPU, RAM, GPU/CUDA, disk keeping >= 250 GB free, all models/agents). Report ETAs proactively; warn at once when a step
slips > 5 min. Never claim tested unless executed; static checks are not tests. Settings/sliders and tactics pages are frozen.
