# Undo-Turbo.ps1 [-Backup <dir>] [-LeRoot <dir>] [-DryRun]   (Windows PowerShell 5.1, ASCII only)
# Rolls back one Install-Turbo run with the game CLOSED. Lives in the backup folder (<LE>\turbo_backups\install_<stamp>\) and defaults to it.
# Verifies the backup md5s first, restores every backed-up file, removes the files the install ADDED (only when they still equal the
# installed bytes: a file you changed afterwards is left alone), removes folders the install created if they are empty, then re-verifies.
# User data is never part of the record (the installer never overwrote it), so undo never touches it.
# Exit 0 = PASS, 1 = refused (nothing changed), 2 = FAIL.
param([string]$Backup = '', [string]$LeRoot = '', [switch]$DryRun)
$ErrorActionPreference = 'Stop'
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash } else { 'ABSENT' } }
function Has([string]$p) { Test-Path -LiteralPath $p }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; exit 1 }
if ($Backup -eq '') { $Backup = Split-Path -Parent $MyInvocation.MyCommand.Path }
if ($DryRun) { Write-Host '*** DRY RUN: nothing is changed ***' -ForegroundColor Yellow }
$run = @(Get-Process -Name FC27, Launcher, LauncherNoAuth -ErrorAction SilentlyContinue)
if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ') + ' - close it first') }
$RecF = Join-Path $Backup 'install_record.txt'
if (-not (Has $RecF)) { Fail ('no install_record.txt in ' + $Backup) }
$back = @(); $new = @(); $inst = @{}; $dirs = @(); $LE = $LeRoot
foreach ($l in (Get-Content -LiteralPath $RecF)) {
  $a = $l.Split('|')
  switch ($a[0]) {
    'backup' { $back += [pscustomobject]@{ Rel = $a[1]; Md5 = $a[2] } }
    'new' { $new += [pscustomobject]@{ Rel = $a[1]; Md5 = $a[2] } }
    'installed' { $inst[$a[1]] = $a[2] }
    'newdir' { $dirs += $a[1] }
    'le' { if ($LE -eq '') { $LE = $a[1] } }
  }
}
if ($LE -eq '' -or -not (Has (Join-Path $LE 'FCLiveEditor.DLL'))) { Fail ('Live Editor folder from the record not found (pass -LeRoot): ' + $LE) }
if ($back.Count + $new.Count -eq 0) { Fail 'the record lists no files' }
$bad = @()
foreach ($f in $back) { if ((Md5 (Join-Path (Join-Path $Backup 'files') $f.Rel)) -ne $f.Md5) { $bad += ('backup file damaged: ' + $f.Rel) } }
if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Host ('  ' + $_) -ForegroundColor Red }; Fail 'the backup does not match its record; nothing restored' }
Write-Host ('Backup           : ' + $Backup + ' (' + $back.Count + ' files verified)')
Write-Host ('Live Editor      : ' + $LE)
Write-Host ('To restore       : ' + $back.Count + ' files; to remove (added by the install): ' + $new.Count + ' files')
if ($DryRun) { Write-Host 'DRY RUN done: nothing changed.' -ForegroundColor Yellow; exit 0 }
try {
  foreach ($f in $back) {
    $dst = Join-Path $LE $f.Rel
    if ($inst.ContainsKey($f.Rel) -and (Md5 $dst) -ne $inst[$f.Rel]) { Write-Host ('  note: changed since the install, restored anyway: ' + $f.Rel) -ForegroundColor Yellow }
    Copy-Item -LiteralPath (Join-Path (Join-Path $Backup 'files') $f.Rel) -Destination $dst -Force
  }
  foreach ($n in $new) {
    $p = Join-Path $LE $n.Rel
    if (-not (Has $p)) { continue }
    if ((Md5 $p) -eq $n.Md5) { Remove-Item -LiteralPath $p -Force } else { Write-Host ('  note: changed since the install, left in place: ' + $n.Rel) -ForegroundColor Yellow }
  }
  foreach ($d in $dirs) {   # deepest first as recorded
    $p = Join-Path $LE $d
    if ((Has $p) -and @(Get-ChildItem -LiteralPath $p -Force).Count -eq 0) { Remove-Item -LiteralPath $p -Force }
  }
} catch { Write-Host ('UNDO FAILED midway: ' + $_.Exception.Message) -ForegroundColor Red; exit 2 }
$bad = @()
foreach ($f in $back) { if ((Md5 (Join-Path $LE $f.Rel)) -ne $f.Md5) { $bad += ('not restored: ' + $f.Rel) } }
foreach ($n in $new) { $p = Join-Path $LE $n.Rel; if ((Has $p) -and (Md5 $p) -eq $n.Md5) { $bad += ('still present: ' + $n.Rel) } }
if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Host ('  FAIL ' + $_) -ForegroundColor Red }; Write-Host 'UNDO FAIL' -ForegroundColor Red; exit 2 }
Write-Host ('UNDO PASS: ' + $back.Count + ' files restored (md5 = record), ' + $new.Count + ' added files removed.') -ForegroundColor Green
exit 0
