# Building and testing Turbo on Windows

How Turbo is built, tested, packaged and installed on the development PC (Windows 11, MSYS2). The technical contract of the code is
`docs/turbo-reference.md`; the release steps are in `docs/TACTICS_OVERHAUL_BRIEF.md` Part B.7 and the per-release checklist in
`turbo_output\RELEASE_<nn>_CHECKLIST.txt`.

## What must be installed
- **MSYS2** in `C:\msys64` with the UCRT64 toolchain: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-binutils zip`.
  `scripts/msys.sh` runs any command inside UCRT64 (`/ucrt64/bin` and `/usr/local/bin` first on the PATH) from Git Bash or PowerShell.
- **Lua 5.4** as `lua5.4` in MSYS2's `/usr/local/bin` (the Lua suite and the native suite's simulated career world use it).
- **Live Editor's own Lua libs** (not in git, never redistributed): copy `<Live Editor>\lua\libs` to `turbo/le27/libs` and the schema dump
  to `turbo/le27/fc27_db_schema.json` in every worktree (`turbo/le27/README.txt`). Without them the suites stop at once with a message.
- **Python 3 with openpyxl** for the PY step of check_all (a Windows Python; `PYTHON=...` picks one). Without it the step is skipped
  and says so.
- Git Bash is fine for git and for `scripts/package.sh --guard`; it has no `zip`, so packaging runs through `scripts/msys.sh`.

## Commands (from the worktree root)
| Command | What it does | Time |
| --- | --- | --- |
| `bash scripts/msys.sh "bash scripts/check_all.sh"` | BUILD (Turbo.dll, TurboProbe.exe, TurboInjector.exe), LUA suite, NATIVE suite (ASan + UBSan on Linux, plain on Windows), PY tool self-tests, CATALOG runners; one summary line per step, logs in `turbogui/build/check_logs` (`CHECK_LOG_DIR` to move them) | ~3-4 min |
| `bash scripts/msys.sh "bash turbogui/scripts/build_win.sh"` | the build alone: parallel and incremental (`turbogui/scripts/parallel_compile.sh` is sourced by it and by `run_native.sh`; only changed sources and objects whose headers changed are compiled) | ~30 s full, seconds incremental |
| `bash scripts/msys.sh "bash turbogui/tests/native/run_native.sh [OUT_DIR]"` | the native suite alone (`TURBO_TEST_ONLY=<text>` runs only the cases whose name holds the text) | ~2-3 min |
| `bash scripts/msys.sh "bash turbo/tests/run_tests.sh"` | the Lua suite alone | ~1 min |
| `bash scripts/package.sh --guard` | the name guard only (no masters, catalog, banks, gate files or mod values in the zip) | ~1 s |
| `bash scripts/msys.sh "bash scripts/package.sh"` | `dist/FC27_LE_Turbo_<version>.zip` + `.sha256`; refuses unless `version.lua` equals `kGuiVersion` | ~10 s |
| `bash scripts/deploy.sh` | installs the build of THIS worktree into `C:\FC 27 Live Editor` (refuses while FC27.exe runs; backs up every replaced file to `turbo_dev\backups\installs\`; prints the sha256 of the binaries) | seconds |

Expected result: `BUILD exit=0 warnings=0`, `LUA ... 0 failed`, `NATIVE ... 0 failed`, PY `PASSED`, CATALOG `exit=0`. The counts of the
current release are in `CHANGELOG.md` / `docs/HANDOVER.md`; a merge of branches must give the sum of each branch's added or removed
native checks, otherwise a registration was lost.

## Rules that save time
- **A new .cpp needs three registrations:** `turbogui/scripts/build_win.sh` (source list), `turbogui/tests/native/run_native.sh` (no
  `src/win/*` there) and `turbogui/tests/native/test_main.cpp` (`#include`, `xxx_test::ui_cases(...)`, `xxx_test::run()`).
- **Line endings:** there is no `.gitattributes` and `core.autocrlf=false`. About 44 files are CRLF (among them
  `turbogui/src/ui/app.h`, `ui_tactics.cpp`, `ui_tactics.h`, `tests/native/test_main.cpp`, `test_tactics_tab.h`; `git ls-files --eol |
  grep i/crlf` lists them all); the rest is LF. Edit a file in its own line endings (Git Bash's `sed -i` turns CRLF into LF), and check that
  `git ls-files --eol <changed files> | grep mixed` prints nothing before a commit.
- **Several worktrees build at once:** every worktree has its own `turbogui/build`; run_native.sh locks its output folder and its
  object folder, so two runs never share files.
- **Never build while the game launches:** while `turbo_dev\tools\GAME_LAUNCH_HOLD.txt` exists, start no build or check_all (Live
  Editor's launcher needs the CPU). Deploy only with the game closed.
- The integration worktree's `turbogui/build/win` can be stale: deploy from the worktree you just built.
