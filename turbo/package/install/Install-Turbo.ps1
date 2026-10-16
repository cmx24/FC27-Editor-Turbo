# Install-Turbo.ps1 [-LeRoot <dir>] [-Source <dir|zip>] [-DryRun] [-Yes] [-ExpectedDllMd5 <md5>]   (Windows PowerShell 5.1, ASCII only)
# One-click Turbo installer with rollback. Normally started through Install-Turbo.cmd from the unzipped Turbo package.
#   - finds the Live Editor folder (the one with FCLiveEditor.DLL): -LeRoot, else this package's parent folders, common paths, else asks
#   - refuses (writes nothing) while FC27 / Launcher / LauncherNoAuth runs
#   - backs up every file it would overwrite (md5-verified) to <LE>\turbo_backups\install_<stamp>\, writes install_record.txt and
#     copies Undo-Turbo.ps1 / Undo-Turbo.cmd there: that folder alone is the rollback
#   - never overwrites user data: turbo_config.json, everything under turbo_output\ (reapply_edits.json, user files), kits\, turbo\kits\,
#     turbo\callnames\masters\ is KEPT when it already exists (new files in those folders may be added). Never touches saves, careersave,
#     settings\ (game), FCLiveEditor*, le_offsets.json, turbo_dev\, turbo_backups\ (such package entries are skipped).
#   - after the copy verifies the md5 of every installed file against the source and re-hashes the protected user data against its
#     before-snapshot; on any failure it rolls back by itself
# Exit 0 = installed and verified (or dry run), 1 = refused (nothing written), 2 = failed and rolled back / rollback needed.
param(
  [string]$LeRoot = '',
  [string]$Source = '',
  [switch]$DryRun,
  [switch]$Yes,
  [string]$ExpectedDllMd5 = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Self = Split-Path -Parent $MyInvocation.MyCommand.Path
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash } else { 'ABSENT' } }
function Has([string]$p) { Test-Path -LiteralPath $p }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; exit 1 }
function IsLE([string]$p) { ($p -ne '') -and (Has $p) -and (Has (Join-Path $p 'FCLiveEditor.DLL')) }

# ---- running game ----
$run = @(Get-Process -Name FC27, Launcher, LauncherNoAuth -ErrorAction SilentlyContinue)
if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ') + ' - close it first') }

# ---- the Live Editor folder ----
if ($LeRoot -eq '') {
  $cands = @()
  $d = $Self; for ($i = 0; $i -lt 4 -and $d; $i++) { $d = Split-Path -Parent $d; if ($d) { $cands += $d } }
  $cands += @('C:\FC 27 Live Editor', 'C:\FC27 Live Editor', 'C:\FC 27 Live Editor 27', (Join-Path $env:USERPROFILE 'Desktop\FC 27 Live Editor'))
  foreach ($c in $cands) { if (IsLE $c) { $LeRoot = $c; break } }
  if ($LeRoot -ne '') { Write-Host ('Found the Live Editor folder: ' + $LeRoot) }
}
if ($LeRoot -eq '') {
  if ($Yes -or $DryRun) { Fail 'Live Editor folder not found: pass -LeRoot <folder with FCLiveEditor.DLL>' }
  $LeRoot = (Read-Host 'Path of the FC 27 Live Editor folder (the one with FCLiveEditor.DLL)').Trim('" ')
}
if (-not (IsLE $LeRoot)) { Fail ('not the Live Editor folder (no FCLiveEditor.DLL): ' + $LeRoot) }
$LE = (Resolve-Path -LiteralPath $LeRoot).ProviderPath.TrimEnd('\')

# ---- the package source (folder, or a zip that is unpacked to a temp folder) ----
if ($Source -eq '') { $Source = Split-Path -Parent $Self }
if (-not (Has $Source)) { Fail ('package source missing: ' + $Source) }
$TmpSrc = ''
if ((Get-Item -LiteralPath $Source).PSIsContainer -eq $false) {
  $TmpSrc = Join-Path ([IO.Path]::GetTempPath()) ('turbo_src_' + [Guid]::NewGuid().ToString('N'))
  [IO.Compression.ZipFile]::ExtractToDirectory((Resolve-Path -LiteralPath $Source).ProviderPath, $TmpSrc)
  $Src = $TmpSrc
} else { $Src = (Resolve-Path -LiteralPath $Source).ProviderPath.TrimEnd('\') }
function Cleanup { if ($TmpSrc -ne '' -and (Has $TmpSrc)) { Remove-Item -LiteralPath $TmpSrc -Recurse -Force -ErrorAction SilentlyContinue } }
if (-not (Has (Join-Path $Src 'turbo\Turbo.dll'))) { Cleanup; Fail ('not a Turbo package (no turbo\Turbo.dll): ' + $Src) }
$ExpectedDllMd5 = $ExpectedDllMd5.ToUpperInvariant()
if ($ExpectedDllMd5 -ne '' -and $ExpectedDllMd5 -notmatch '^[0-9A-F]{32}$') { Cleanup; Fail ('not an md5: ' + $ExpectedDllMd5) }

# ---- policy ----
$ProtectedDirs = @('turbo_output', 'kits', 'turbo\kits', 'turbo\callnames\masters')   # user data: existing files are never overwritten
$ProtectedFiles = @('turbo_config.json')
$Blocked = @('FCLiveEditor*', 'le_offsets.json', 'turbo_dev\*', 'turbo_backups\*', 'careersave*', 'saves\*', 'settings\*', 'install\*')   # never installed
function IsUnder([string]$rel, [string]$dir) { $rel.StartsWith($dir + '\', [StringComparison]::OrdinalIgnoreCase) }
function IsProtected([string]$rel) {
  foreach ($f in $ProtectedFiles) { if ($rel -ieq $f) { return $true } }
  foreach ($d in $ProtectedDirs) { if (IsUnder $rel $d) { return $true } }
  return $false
}
function IsBlocked([string]$rel) { foreach ($b in $Blocked) { if ($rel -ilike $b) { return $true } } return $false }

# ---- plan (read-only) ----
$plan = @(); $skipped = @(); $kept = @()
foreach ($f in @(Get-ChildItem -LiteralPath $Src -Recurse -File -Force)) {
  $rel = $f.FullName.Substring($Src.Length + 1)
  if ($rel -match '(^|\\)\.\.(\\|$)' -or $rel -match '^[A-Za-z]:') { Cleanup; Fail ('unsafe path in the package: ' + $rel) }
  if (IsBlocked $rel) { $skipped += $rel; continue }
  $dst = Join-Path $LE $rel
  if (-not ([IO.Path]::GetFullPath($dst)).StartsWith($LE + '\', [StringComparison]::OrdinalIgnoreCase)) { Cleanup; Fail ('path escapes the Live Editor folder: ' + $rel) }
  $existed = Has $dst
  if ($existed -and (IsProtected $rel)) { $kept += $rel; continue }
  $plan += [pscustomobject]@{ Rel = $rel; Src = $f.FullName; Dst = $dst; Existed = $existed; SrcMd5 = (Md5 $f.FullName); OldMd5 = (Md5 $dst) }
}
$dll = @($plan | Where-Object { $_.Rel -ieq 'turbo\Turbo.dll' })
if ($dll.Count -ne 1) { Cleanup; Fail 'the package has no turbo\Turbo.dll to install' }
if ($ExpectedDllMd5 -ne '' -and $dll[0].SrcMd5 -ne $ExpectedDllMd5) { Cleanup; Fail ('Turbo.dll in the package has md5 ' + $dll[0].SrcMd5 + ', not ' + $ExpectedDllMd5) }
$over = @($plan | Where-Object { $_.Existed }); $add = @($plan | Where-Object { -not $_.Existed })

# snapshot of the protected user data (hashed again after the install; must be identical)
function Snapshot {
  $h = @{}
  foreach ($f in $ProtectedFiles) { $p = Join-Path $LE $f; if (Has $p) { $h[$f] = Md5 $p } }
  foreach ($d in $ProtectedDirs) {
    $dp = Join-Path $LE $d
    if (Has $dp) { foreach ($x in @(Get-ChildItem -LiteralPath $dp -Recurse -File -Force)) { $h[$x.FullName.Substring($LE.Length + 1)] = Md5 $x.FullName } }
  }
  return $h
}
$snapBefore = Snapshot
$Stamp = (Get-Date).ToString('yyyyMMdd_HHmmss')
$BDir = Join-Path $LE ('turbo_backups\install_' + $Stamp)
Write-Host ('Live Editor      : ' + $LE)
Write-Host ('Package          : ' + $Source + ' (' + $plan.Count + ' files to install)')
Write-Host ('Turbo.dll        : package ' + $dll[0].SrcMd5 + ', installed now ' + $dll[0].OldMd5)
Write-Host ('Overwrites       : ' + $over.Count + ' existing files (backed up first); adds ' + $add.Count + ' new files')
Write-Host ('Kept (user data) : ' + $kept.Count + ' package files not copied because they exist; ' + $snapBefore.Count + ' protected files snapshotted')
if ($skipped.Count -gt 0) { Write-Host ('Skipped (blocked): ' + ($skipped -join ', ')) }
Write-Host ('Backup / rollback: ' + $BDir)
if (Has $BDir) { Cleanup; Fail ('backup folder exists: ' + $BDir) }
if ($DryRun) { Cleanup; Write-Host 'DRY RUN done: nothing changed.' -ForegroundColor Yellow; exit 0 }
if (-not $Yes) {
  $a = Read-Host 'Install now? Type YES to continue'
  if ($a -ne 'YES') { Cleanup; Fail 'cancelled by the user' }
}

# ---- backup + record ----
$FilesDir = Join-Path $BDir 'files'
$rec = @()
try {
  New-Item -ItemType Directory -Path $FilesDir -Force | Out-Null
  foreach ($p in $over) {
    $b = Join-Path $FilesDir $p.Rel
    New-Item -ItemType Directory -Path (Split-Path $b) -Force | Out-Null
    Copy-Item -LiteralPath $p.Dst -Destination $b -Force
    if ((Md5 $b) -ne $p.OldMd5) { throw ('backup md5 mismatch: ' + $p.Rel) }
    $rec += ('backup|' + $p.Rel + '|' + $p.OldMd5)
  }
  foreach ($p in $add) { $rec += ('new|' + $p.Rel + '|' + $p.SrcMd5) }
  foreach ($p in $over) { $rec += ('installed|' + $p.Rel + '|' + $p.SrcMd5) }
  $newDirs = @{}
  foreach ($p in $plan) {
    $d = Split-Path $p.Dst
    while ($d.Length -gt $LE.Length -and -not (Has $d) -and -not $newDirs.ContainsKey($d)) { $newDirs[$d] = 1; $d = Split-Path $d }
  }
  foreach ($d in ($newDirs.Keys | Sort-Object { $_.Length } -Descending)) { $rec += ('newdir|' + $d.Substring($LE.Length + 1) + '|') }
  $rec += ('le|' + $LE + '|')
  $rec += ('source|' + $Source + '|')
  $rec += ('time|' + (Get-Date).ToString('s') + '|')
  [IO.File]::WriteAllLines((Join-Path $BDir 'install_record.txt'), [string[]]$rec)
  foreach ($u in 'Undo-Turbo.ps1', 'Undo-Turbo.cmd') {
    if (Has (Join-Path $Self $u)) { Copy-Item -LiteralPath (Join-Path $Self $u) -Destination (Join-Path $BDir $u) -Force }
  }
  if (-not (Has (Join-Path $BDir 'Undo-Turbo.ps1'))) { throw 'Undo-Turbo.ps1 is missing next to the installer' }
  Write-Host ('Backup written and verified: ' + $over.Count + ' files; undo script: ' + (Join-Path $BDir 'Undo-Turbo.cmd'))
} catch {
  Write-Host ('BACKUP FAILED, nothing installed: ' + $_.Exception.Message) -ForegroundColor Red
  Cleanup; exit 1
}

# ---- install, verify, auto-rollback ----
function Rollback([string]$why) {
  Write-Host ($why + ' - rolling back') -ForegroundColor Red
  & (Join-Path $BDir 'Undo-Turbo.ps1') -Backup $BDir
  Write-Host ('Rollback exit code ' + $LASTEXITCODE + '. Undo again any time: ' + (Join-Path $BDir 'Undo-Turbo.cmd')) -ForegroundColor Yellow
  Cleanup; exit 2
}
try {
  foreach ($p in $plan) {
    New-Item -ItemType Directory -Path (Split-Path $p.Dst) -Force | Out-Null
    Copy-Item -LiteralPath $p.Src -Destination $p.Dst -Force
  }
} catch { Rollback ('INSTALL FAILED midway: ' + $_.Exception.Message) }

$bad = @()
foreach ($p in $plan) { if ((Md5 $p.Dst) -ne $p.SrcMd5) { $bad += ('md5 differs from the package: ' + $p.Rel) } }
$snapAfter = Snapshot
foreach ($k in $snapBefore.Keys) {
  if (-not $snapAfter.ContainsKey($k)) { $bad += ('user file lost: ' + $k) }
  elseif ($snapAfter[$k] -ne $snapBefore[$k]) { $bad += ('user file changed: ' + $k) }
}
if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Host ('  FAIL ' + $_) -ForegroundColor Red }; Rollback 'VERIFY FAILED' }
Cleanup
Write-Host ('INSTALL PASS: ' + $plan.Count + ' files installed (md5 = package), Turbo.dll ' + (Md5 (Join-Path $LE 'turbo\Turbo.dll')) + ', ' + $snapBefore.Count + ' user files unchanged.') -ForegroundColor Green
Write-Host ('Undo: ' + (Join-Path $BDir 'Undo-Turbo.cmd')) -ForegroundColor Green
exit 0
