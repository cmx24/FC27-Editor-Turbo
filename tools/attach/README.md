# Attach-Turbo

Run **Attach-Turbo.cmd** after every FCM 27 update (and after every Live Editor or mod-manager update), once the mod manager has
rebuilt FIFAModData and before you play. It makes sure Turbo is present and intact in every Live Editor folder.
Design: `turbo_dev\turbox_20_assessment_20261101\handover\16_FCM27_COEXISTENCE.md`, section 3.

Routine: update FCM (and/or LE) -> run the mod manager -> **Attach-Turbo.cmd** -> play.

## What it does

1. Refuses (exit 1, writes nothing except its own log) while FC27, Launcher, LauncherNoAuth, FIFA Mod Manager, FIFA Editor Tool or an
   FCM patch runs. Note: any process called `Launcher` blocks it, as with Install-Turbo.
2. Picks the Turbo package from `Attach-Turbo.config.json`. Default `"packageZip": "auto"` = the newest `FC27_LE_Turbo_X_*.zip` under
   `C:\FC_Tools\TurboX` (any subfolder) that has a `.sha256` beside it (`<zip>.sha256` or `<name>.sha256`). Its SHA-256 must match.
   Create the sidecar once per package (PowerShell):
   `(Get-FileHash -Algorithm SHA256 "C:\FC_Tools\TurboX\x101\FC27_LE_Turbo_X_1.0.1.zip").Hash | Set-Content -Encoding ASCII "C:\FC_Tools\TurboX\x101\FC27_LE_Turbo_X_1.0.1.zip.sha256"`
   Or set `packageZip` to an absolute path, plus `packageSha256` when no sidecar exists. `expectedDllMd5` empty = taken from the verified zip.
3. Finds every Live Editor folder (FCLiveEditor.DLL + le_config.json) in `scanRoots` (default: `C:\FC 27 Live Editor`, Install-Turbo's
   other candidates, the game folder to depth 2, `C:\FC_Tools\FCM27` to depth 5), plus `extraLeRoots` and `-LeRoot`. It prints each
   folder's le_config `dlls` / `params` / `log_level` (read only) and **flags any `dlls` entry whose file is missing** (it never fixes it).
   A Live Editor inside the game folder is reported and skipped: the game folder is never written.
4. Per folder it compares the Turbo files with the package, using the same rules as Install-Turbo.ps1: program files must match the
   package byte for byte; user data (`turbo_config.json`, `turbo_output\`, `kits\`, `turbo\kits\`, `turbo\callnames\masters\`) only has to
   exist. Everything equal -> **ATTACHED, nothing to do**. Otherwise it runs the package's own `install\Install-Turbo.ps1 -LeRoot <folder>
   -Source <zip> -ExpectedDllMd5 <md5> -Yes` (backup first, md5 verify, self-rollback), then compares again.
5. Logs to `Attach-Turbo_<yyyyMMdd_HHmmss>.log` next to the scripts and appends one line to `<folder>\turbo_output\attach_log.txt`.

Never touched: le_config.json, le_*.json, FCLiveEditor.DLL, CryptBase.dll, Launcher*.exe, FakeEAACLauncher\, mods\, loc\, the game
folder, FIFAModData, .fifamod files, mod-manager folders, saves.

## Options

- `Attach-Turbo.cmd -DryRun` - report only, changes nothing.
- `Attach-Turbo.cmd -LeRoot "D:\Other Live Editor"` - one extra folder (absolute path). For several, use `extraLeRoots` in the config.
- `TURBO_NOPAUSE=1` in the environment skips the final pause (automation only).

Exit codes: 0 = all folders attached (or dry run), 1 = refused (nothing written), 2 = at least one folder failed (Install-Turbo rolled it back).

## Undo

Each attach that copied files leaves `<Live Editor>\turbo_backups\install_<stamp>\Undo-Turbo.cmd`. Run it to roll that attach back.
Full detach = run the Undo of the first install. Instant off without undo: delete `lua\autorun\turbo_boot.lua` or create
`turbo_output\turbo_gui_disable.txt`.

## Validation status

Not executed on Windows. Checked on Linux only: PowerShell 7.4 parser (0 errors), PSScriptAnalyzer 5.1 syntax/command compatibility
(no findings), and full runs of Attach-Turbo.ps1 under pwsh 7.4 against a fake Live Editor tree (dry run, attach, second run "nothing to
do", tamper -> re-attach, SHA mismatch / no sidecar / relative path / blocking process / bad config -> refused, game-folder LE skipped,
missing CryptBase flagged). Install-Turbo.ps1 needed a Linux-only path shim on that bench, so the real Windows Install-Turbo call path,
Windows PowerShell 5.1 itself and Attach-Turbo.cmd are NOT executed.
