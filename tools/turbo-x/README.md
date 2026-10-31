# tools/turbo-x - protect, restore, build identity

Three Windows PowerShell 5.1 scripts for the owner's PC (`C:\FC 27 Live Editor\`). All of them refuse to run while the game or
the Live Editor runs, take every root as a parameter, pause with `Press Enter to close` before every exit (pass `-NoPause` for
automation), support `-DryRun`, and never call `Invoke-Expression`, `Remove-Item` or `Move-Item`.

Run from anywhere, e.g. a prompt in `C:\Windows\System32`:

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\FC 27 Live Editor\turbo_dev\<worktree>\tools\turbo-x\Protect-TurboFiles.ps1" -DryRun
```

## STATUS: statically validated, NOT executed

These scripts were written on Linux where neither `pwsh` nor `powershell.exe` exists. They were checked with
`python3 -I tools/turbo-x/static_check.py <the three .ps1>` (tokenizes strings, here-strings and comments, then verifies
bracket/quote balance, ASCII only, no tabs, `param(` first, `$ErrorActionPreference = 'Stop'`, the pause line, no
Invoke-Expression, no Remove-Item/Move-Item/Rename-Item/Set-Content/Out-File, `-LiteralPath` on every path cmdlet, no bare
relative path tokens) and simulated line by line against Windows PowerShell 5.1 semantics. They have **not** been run. The first
run on the PC should be `-DryRun`, then a real `Protect-TurboFiles.ps1`, then `Restore-TurboFiles.ps1 -DryRun` against that
backup, before relying on them. The checker is also unproven on anything but these files.

## Protect-TurboFiles.ps1

Creates `C:\FC 27 Live Editor\turbo_backups\protect_<yyyyMMdd_HHmmss>\` and copies, md5-verified, into `files\<area>\...`:

| area | default root (parameter) | what |
|---|---|---|
| `settings` | `%LOCALAPPDATA%\EA SPORTS FC 27\settings` (`-SettingsDir`) | every file, subfolders included: `Settings<17 digits>`, `CmMgrC*` career saves (TORINO, EVERTON, LB, Southampton, LEICESTER ...), `Squads*`, `ProfileOptions*` |
| `turbo_output` | `C:\FC 27 Live Editor\turbo_output` (`-TurboOutput`) | every `*.json` and `*.ini` (`-OutputPatterns`) at any depth, except under the top-level folders in `-ExcludeDirs` (default `cache`): `gui_settings.json`, `tactic_profiles.json`, `tactic_preset_last_load.json`, `tactic_presets\*.json`, `settings_presets\`, `tuning_presets\`, `turbo_gui_layout_v2.ini`, `callnames\*.json`, `kits\*.json`, ... |
| `turbo` | `C:\FC 27 Live Editor\turbo\Turbo.dll` (`-TurboDll`) | the installed DLL |

`MANIFEST.txt` lists `root|<area>|<folder>` and `file|<area>|<relative path>|<bytes>|<md5>` per file. Refuses when the settings
folder is missing/empty or a process named in `-ProcessNames` runs. Writes nothing outside the new folder; a failure midway leaves
the partial folder without a manifest (Restore refuses it). Not covered (by design, listed so nobody assumes otherwise):
`C:\FC 27 Live Editor\turbo_config.json`, non-json/ini files in `turbo_output` (logs, PNG caches, `*.cache`), `turbo_output\cache\`.

## Restore-TurboFiles.ps1 -BackupDir <protect_ folder>

Refuses unless every backup copy matches `MANIFEST.txt`. Plans `overwrite` / `create` / `same` per file (`-DryRun` stops here).
Before writing anything it copies every file it will overwrite into `<BackupDir>\pre_restore\<stamp>\` with its own `MANIFEST.txt`
in the same format, so `Restore-TurboFiles.ps1 -BackupDir <BackupDir>\pre_restore\<stamp>` undoes the restore (files the restore
*created* are only reported there, never removed: nothing is ever deleted). Then copies each manifest file over its destination
(`Copy-Item -Force`), verifies the md5 and writes `RESTORE_RESULT.txt` (`OK|` / `FAIL|` / `SAME|` per file). It writes only
destinations derived from manifest lines (relative paths are checked: no `..`, `/`, `:` or rooted paths) plus files under
`<BackupDir>`. `-Areas settings` restores the game folder only; `-SettingsDir` / `-TurboOutput` / `-TurboDir` override the roots
recorded in the manifest.

## Record-BuildIdentity.ps1 -Commit <hash> -Source <zip or branch> -PackagedDll <path> [-Note <text>]

Computes the md5 of the packaged DLL and of `C:\FC 27 Live Editor\turbo\Turbo.dll` (`-InstalledDll`), prints `MATCH` / `MISMATCH`
and appends `| <timestamp> | <commit> | <source> | <packaged md5> | <installed md5> | <note> [MATCH|MISMATCH] |` to
`C:\FC 27 Live Editor\turbo_output\BUILD_IDENTITY.md` (`-LogPath`; created with a header when absent). Pre-history rows from the
2026-10-31 review pack are in `docs/review-2026-10-31/BUILD_IDENTITY.md`.

## Process names checked

`FC27`, `Launcher`, `LauncherNoAuth` - the names the repo's other scripts use (`Install-Turbo.ps1`, `Undo-Turbo.ps1` check all three;
`g3_deploy.ps1` / `compdata_profile.ps1` check the first and third). That the Live Editor really shows up as `Launcher` on this PC is
**not verified**; if Task Manager shows a different image name, add it: `-ProcessNames FC27,Launcher,LauncherNoAuth,<Name>`.

Quoting: a path argument ending in a backslash inside double quotes (`"C:\x\"`) swallows the quote on the command line; pass
`"C:\x"` without the trailing backslash.

## Exit codes

0 done (or dry run) | 1 refused, nothing written | 2 failed midway / one or more restore files FAIL (see the message or `RESTORE_RESULT.txt`).
