# Protect-TurboFiles.ps1 [-DryRun] [-NoPause] [-BackupRoot <dir>] [-SettingsDir <dir>] [-TurboOutput <dir>] [-TurboDll <file>]
#                        [-ExcludeDirs cache,...] [-OutputPatterns *.json,*.ini] [-ProcessNames FC27,Launcher,LauncherNoAuth]
# (Windows PowerShell 5.1, ASCII only; run with the game and the Live Editor CLOSED)
#
# Copies, into one new timestamped folder <BackupRoot>\protect_<yyyyMMdd_HHmmss>\files\<area>\<relative path>:
#   area settings     every file under the game's settings folder (%LOCALAPPDATA%\EA SPORTS FC 27\settings: Settings<17 digits>,
#                     CmMgrC* career saves, Squads*, ProfileOptions*, anything else there, subfolders included)
#   area turbo_output every file under <TurboOutput> whose name matches -OutputPatterns (default *.json and *.ini: gui_settings.json,
#                     tactic_profiles.json, tactic_preset_last_load.json, tactic_presets\*.json, settings_presets\, tuning_presets\,
#                     turbo_gui_layout_v2.ini, ...) EXCEPT the top-level subfolders named in -ExcludeDirs (default: cache)
#   area turbo        the installed <TurboDll> (default C:\FC 27 Live Editor\turbo\Turbo.dll)
# and writes MANIFEST.txt there: one 'file|<area>|<relative path>|<size>|<md5>' line per copied file (Get-FileHash -Algorithm MD5),
# 'root|<area>|<folder>' lines naming where each area came from. Every copy is md5-verified against its source.
# Restore-TurboFiles.ps1 -BackupDir <that folder> puts the files back.
#
# Refuses (nothing written) while a process named in -ProcessNames runs (default FC27, Launcher, LauncherNoAuth: the names the other
# Turbo scripts check; that the Live Editor really runs as "Launcher" is NOT verified on this PC - add names with -ProcessNames if
# Task Manager shows another one), when the settings folder is missing or empty, or when the backup folder already exists.
# This script never deletes, moves or overwrites anything: it only creates the new backup folder and files inside it.
# -DryRun lists every file that would be copied and writes nothing. -NoPause skips the final 'Press Enter to close' (for automation).
# Exit 0 = backup written and verified (or dry run), 1 = refused (nothing written), 2 = failed midway (the partial folder is left in place).
param(
  [switch]$DryRun,
  [switch]$NoPause,
  [string]$BackupRoot = 'C:\FC 27 Live Editor\turbo_backups',
  [string]$SettingsDir = '',
  [string]$TurboOutput = 'C:\FC 27 Live Editor\turbo_output',
  [string]$TurboDll = 'C:\FC 27 Live Editor\turbo\Turbo.dll',
  [string[]]$ExcludeDirs = @('cache'),
  [string[]]$OutputPatterns = @('*.json', '*.ini'),
  [string[]]$ProcessNames = @('FC27', 'Launcher', 'LauncherNoAuth')
)
$ErrorActionPreference = 'Stop'
$Utf8 = New-Object System.Text.UTF8Encoding($false)

function Pause-Exit([int]$code) { if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }; exit $code }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; Pause-Exit 1 }
function Has([string]$p) { Test-Path -LiteralPath $p }
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLowerInvariant() } else { 'ABSENT' } }
function Fmt-Size([int64]$b) { if ($b -ge 1GB) { '{0:N2} GB' -f ($b / 1GB) } elseif ($b -ge 1MB) { '{0:N1} MB' -f ($b / 1MB) } elseif ($b -ge 1KB) { '{0:N0} KB' -f ($b / 1KB) } else { '{0:N0} B' -f $b } }
function Full([string]$p) { (Resolve-Path -LiteralPath $p).ProviderPath.TrimEnd('\') }
function Rel([string]$root, [string]$full) { $full.Substring($root.Length).TrimStart('\') }

try {
  if ($DryRun) { Write-Host '*** DRY RUN: nothing is written ***' -ForegroundColor Yellow }

  # ---- parameters ----
  if ($SettingsDir -eq '') {
    if (-not $env:LOCALAPPDATA) { Fail 'LOCALAPPDATA is not set: pass -SettingsDir <...\EA SPORTS FC 27\settings>' }
    $SettingsDir = Join-Path $env:LOCALAPPDATA 'EA SPORTS FC 27\settings'
  }
  $ExcludeDirs = @($ExcludeDirs | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().Trim('\') } | Where-Object { $_ -ne '' })
  $OutputPatterns = @($OutputPatterns | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
  $ProcessNames = @($ProcessNames | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
  if ($OutputPatterns.Count -eq 0) { Fail '-OutputPatterns is empty' }

  # ---- running game / Live Editor ----
  $run = @()
  if ($ProcessNames.Count -gt 0) { $run = @(Get-Process -Name $ProcessNames -ErrorAction SilentlyContinue) }
  if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ') + ' - close it first') }

  # ---- the three sources ----
  if (-not (Has $SettingsDir)) { Fail ('settings folder missing: ' + $SettingsDir) }
  $SettingsDir = Full $SettingsDir
  $outputOk = Has $TurboOutput
  if ($outputOk) { $TurboOutput = Full $TurboOutput } else { Write-Host ('WARNING: turbo_output folder missing, that area is skipped: ' + $TurboOutput) -ForegroundColor Yellow }
  $dllOk = Test-Path -LiteralPath $TurboDll -PathType Leaf
  if ($dllOk) { $TurboDll = Full $TurboDll } else { Write-Host ('WARNING: Turbo.dll missing, that area is skipped: ' + $TurboDll) -ForegroundColor Yellow }
  $TurboDir = Split-Path -Parent $TurboDll

  # ---- the file list: one record per file (Area, Rel, Src, Size) ----
  $items = @()
  foreach ($f in @(Get-ChildItem -LiteralPath $SettingsDir -File -Recurse -Force)) {
    $items += [pscustomobject]@{ Area = 'settings'; Rel = (Rel $SettingsDir $f.FullName); Src = $f.FullName; Size = [int64]$f.Length }
  }
  if (@($items).Count -eq 0) { Fail ('the settings folder holds no files (wrong -SettingsDir?): ' + $SettingsDir) }
  $skipped = 0
  if ($outputOk) {
    foreach ($f in @(Get-ChildItem -LiteralPath $TurboOutput -File -Recurse -Force)) {
      $name = $f.Name; $match = $false
      foreach ($pat in $OutputPatterns) { if ($name -like $pat) { $match = $true; break } }
      if (-not $match) { continue }
      $rel = Rel $TurboOutput $f.FullName
      $top = $rel.Split('\')[0]
      if ($rel.Contains('\') -and ($ExcludeDirs -contains $top)) { $skipped++; continue }
      $items += [pscustomobject]@{ Area = 'turbo_output'; Rel = $rel; Src = $f.FullName; Size = [int64]$f.Length }
    }
  }
  if ($dllOk) { $items += [pscustomobject]@{ Area = 'turbo'; Rel = (Split-Path -Leaf $TurboDll); Src = $TurboDll; Size = [int64](Get-Item -LiteralPath $TurboDll).Length } }
  $items = @($items)
  foreach ($it in $items) {
    if ($it.Rel -eq '' -or $it.Rel.StartsWith('\') -or ($it.Rel.Split('\') -contains '..')) { Fail ('unexpected relative path for ' + $it.Src) }
  }

  # ---- the backup folder ----
  $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
  $Backup = Join-Path $BackupRoot ('protect_' + $stamp)
  $Files = Join-Path $Backup 'files'
  $Manifest = Join-Path $Backup 'MANIFEST.txt'
  if (Has $Backup) { Fail ('backup folder already exists (run again in a second): ' + $Backup) }

  # ---- plan ----
  $total = [int64]0; foreach ($it in $items) { $total += $it.Size }
  Write-Host ('Backup folder   : ' + $Backup)
  Write-Host ('settings        : ' + $SettingsDir + '  (' + @($items | Where-Object { $_.Area -eq 'settings' }).Count + ' files)')
  Write-Host ('turbo_output    : ' + $TurboOutput + '  (' + @($items | Where-Object { $_.Area -eq 'turbo_output' }).Count + ' files matching ' + ($OutputPatterns -join ',') + '; ' + $skipped + ' skipped under ' + ($ExcludeDirs -join ',') + '\)')
  Write-Host ('turbo           : ' + $TurboDll + '  (' + @($items | Where-Object { $_.Area -eq 'turbo' }).Count + ' file)')
  Write-Host ('Total           : ' + $items.Count + ' files, ' + (Fmt-Size $total))
  if ($DryRun) {
    foreach ($it in $items) { Write-Host ('  would copy  ' + $it.Area + '\' + $it.Rel + '  (' + (Fmt-Size $it.Size) + ')') }
    Write-Host ('DRY RUN done: nothing written. A real run would create ' + $Backup + ' with ' + $items.Count + ' files + MANIFEST.txt') -ForegroundColor Yellow
    Pause-Exit 0
  }

  # ---- copy + verify ----
  $lines = @()
  $lines += '# Protect-TurboFiles manifest v1'
  $lines += ('# created|' + (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss'))
  $lines += ('# host|' + $env:COMPUTERNAME + '|' + $env:USERNAME)
  $lines += '# lines: root|<area>|<source folder>   file|<area>|<relative path>|<bytes>|<md5>   (the copy lives at files\<area>\<relative path>)'
  $lines += ('root|settings|' + $SettingsDir)
  if ($outputOk) { $lines += ('root|turbo_output|' + $TurboOutput) } else { $lines += ('# missing|turbo_output|' + $TurboOutput) }
  if ($dllOk) { $lines += ('root|turbo|' + $TurboDir) } else { $lines += ('# missing|turbo|' + $TurboDll) }
  [void][System.IO.Directory]::CreateDirectory($Files)
  $n = 0
  foreach ($it in $items) {
    $dst = Join-Path (Join-Path $Files $it.Area) $it.Rel
    if (Has $dst) { throw ('destination already exists inside the new backup folder (should never happen): ' + $dst) }
    [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $dst))
    $srcMd5 = Md5 $it.Src
    [System.IO.File]::Copy($it.Src, $dst, $false)   # literal paths, never overwrites (throws if $dst exists)
    $dstMd5 = Md5 $dst
    if ($dstMd5 -ne $srcMd5) { throw ('md5 mismatch after copying ' + $it.Src + ' (source ' + $srcMd5 + ', copy ' + $dstMd5 + ')') }
    $lines += ('file|' + $it.Area + '|' + $it.Rel + '|' + $it.Size + '|' + $srcMd5)
    $n++
    if (($n % 25) -eq 0) { Write-Host ('  ' + $n + ' / ' + $items.Count + ' copied') }
  }
  [System.IO.File]::WriteAllLines($Manifest, [string[]]$lines, $Utf8)
  if (-not (Has $Manifest)) { throw ('MANIFEST.txt was not written: ' + $Manifest) }
  Write-Host ('PROTECTED: ' + $n + ' files (' + (Fmt-Size $total) + ') copied and md5-verified into') -ForegroundColor Green
  Write-Host ('  ' + $Backup) -ForegroundColor Green
  Write-Host ('  manifest: ' + $Manifest)
  Write-Host ('  restore : powershell -NoProfile -ExecutionPolicy Bypass -File Restore-TurboFiles.ps1 -BackupDir "' + $Backup + '"')
  Pause-Exit 0
} catch {
  Write-Host ('PROTECT FAILED midway: ' + $_.Exception.Message) -ForegroundColor Red
  Write-Host 'Nothing outside the new backup folder was touched; the partial folder (if any) is left in place and has no MANIFEST.txt, so Restore refuses it.' -ForegroundColor Red
  Pause-Exit 2
}
