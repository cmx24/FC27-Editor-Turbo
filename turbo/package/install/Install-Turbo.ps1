# Install-Turbo.ps1 [-LeRoot <dir>] [-Source <dir|zip>] [-DryRun] [-Yes] [-ExpectedDllMd5 <md5>] [-LogFile <file>] [-NoCleanup] [-KeepInstalls <n>]
#   (Windows PowerShell 5.1, ASCII only)
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
#   - X 2.0: a progress bar for every long step (hashing the package, checking your files, backup, copy, verify, checking your files again)
#     and a numbered step line; -LogFile writes everything shown to that file too (the console keeps the progress bar)
#   - X 2.0: after INSTALL PASS, turbo\Cleanup-Turbo.ps1 -Apply (unless -NoCleanup): old install backups beyond the newest -KeepInstalls
#     (default 2), Turbo files older versions installed that this one no longer has (kept in this install's backup: its Undo puts them
#     back), old _INSTALL_TURBOX_*.cmd launchers, oversized Turbo logs rotated; all of it to the Recycle Bin. A cleanup problem never
#     fails the install.
# Exit 0 = installed and verified (or dry run), 1 = refused (nothing written), 2 = failed and rolled back / rollback needed.
param(
  [string]$LeRoot = '',
  [string]$Source = '',
  [switch]$DryRun,
  [switch]$Yes,
  [string]$ExpectedDllMd5 = '',
  [string]$LogFile = '',
  [switch]$NoCleanup,
  [int]$KeepInstalls = 2
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
if ($LogFile -ne '') {
  # everything shown also goes to the log file; the transcript ends when this PowerShell process ends (every exit path).
  # A relative -LogFile would land in the caller's folder (C:\Windows\System32 for a double-click): made absolute against this script's folder
  if (-not [IO.Path]::IsPathRooted($LogFile)) { $LogFile = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) $LogFile }
  New-Item -ItemType Directory -Path (Split-Path -Parent $LogFile) -Force | Out-Null
  Start-Transcript -LiteralPath $LogFile -Force | Out-Null
}
# ---- progress (X 2.0): one bar for the step in hand, refreshed every 200 items and at the end ----
$StepCount = 7
function Step([int]$n, [string]$what) { Write-Host ('[' + $n + '/' + $StepCount + '] ' + $what) -ForegroundColor Cyan }
function Prog([string]$act, [int]$i, [int]$n) {
  if ($n -le 0) { return }
  if ($i -ne $n -and ($i % 200) -ne 0) { return }
  $pct = [int][Math]::Floor(100.0 * $i / $n)
  Write-Progress -Id 1 -Activity $act -Status ($i.ToString() + ' of ' + $n.ToString() + ' files (' + $pct.ToString() + '%)') -PercentComplete $pct
}
function ProgDone([string]$act) { Write-Progress -Id 1 -Activity $act -Completed }
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
Step 1 'Reading the package'
$srcFiles = @(Get-ChildItem -LiteralPath $Src -Recurse -File -Force); $iF = 0
foreach ($f in $srcFiles) {
  $iF++; Prog 'Reading the package' $iF $srcFiles.Count
  $rel = $f.FullName.Substring($Src.Length + 1)
  if ($rel -match '(^|\\)\.\.(\\|$)' -or $rel -match '^[A-Za-z]:') { Cleanup; Fail ('unsafe path in the package: ' + $rel) }
  if (IsBlocked $rel) { $skipped += $rel; continue }
  $dst = Join-Path $LE $rel
  if (-not ([IO.Path]::GetFullPath($dst)).StartsWith($LE + '\', [StringComparison]::OrdinalIgnoreCase)) { Cleanup; Fail ('path escapes the Live Editor folder: ' + $rel) }
  $existed = Has $dst
  if ($existed -and (IsProtected $rel)) { $kept += $rel; continue }
  $plan += [pscustomobject]@{ Rel = $rel; Src = $f.FullName; Dst = $dst; Existed = $existed; SrcMd5 = (Md5 $f.FullName); OldMd5 = (Md5 $dst) }
}
ProgDone 'Reading the package'
$dll = @($plan | Where-Object { $_.Rel -ieq 'turbo\Turbo.dll' })
if ($dll.Count -ne 1) { Cleanup; Fail 'the package has no turbo\Turbo.dll to install' }
if ($ExpectedDllMd5 -ne '' -and $dll[0].SrcMd5 -ne $ExpectedDllMd5) { Cleanup; Fail ('Turbo.dll in the package has md5 ' + $dll[0].SrcMd5 + ', not ' + $ExpectedDllMd5) }
$over = @($plan | Where-Object { $_.Existed }); $add = @($plan | Where-Object { -not $_.Existed })

# snapshot of the protected user data (hashed again after the install; must be identical)
function Snapshot([string]$act) {
  $h = @{}
  foreach ($f in $ProtectedFiles) { $p = Join-Path $LE $f; if (Has $p) { $h[$f] = Md5 $p } }
  # X 2.0: the file list first (so the bar knows the total), then the hashes with the bar
  $all = @()
  foreach ($d in $ProtectedDirs) {
    $dp = Join-Path $LE $d
    if (Has $dp) { $all += @(Get-ChildItem -LiteralPath $dp -Recurse -File -Force) }
  }
  $i = 0
  foreach ($x in $all) {
    $i++; Prog $act $i $all.Count
    $h[$x.FullName.Substring($LE.Length + 1)] = Md5 $x.FullName
  }
  ProgDone $act
  return $h
}
# a dry run reads and hashes ONLY the package files it would copy (and the files they would replace): the protected user data (turbo_output, kits,
# callnames\masters: tens of thousands of files) is neither enumerated nor hashed; the real install snapshots it as before
$snapBefore = @{}
if (-not $DryRun) { Step 2 'Checking your files before the install (user data is never changed)'; $snapBefore = Snapshot 'Checking your files (before)' }
$Stamp = (Get-Date).ToString('yyyyMMdd_HHmmss')
$BDir = Join-Path $LE ('turbo_backups\install_' + $Stamp)
Write-Host ('Live Editor      : ' + $LE)
Write-Host ('Package          : ' + $Source + ' (' + $plan.Count + ' files to install)')
Write-Host ('Turbo.dll        : package ' + $dll[0].SrcMd5 + ', installed now ' + $dll[0].OldMd5)
Write-Host ('Overwrites       : ' + $over.Count + ' existing files (backed up first); adds ' + $add.Count + ' new files')
Write-Host ('Kept (user data) : ' + $kept.Count + ' package files not copied because they exist; ' + $(if ($DryRun) { 'protected user data not scanned in a dry run' } else { [string]$snapBefore.Count + ' protected files snapshotted' }))
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
  Step 3 'Backing up the files the install replaces'
  $iB = 0
  foreach ($p in $over) {
    $iB++; Prog 'Backing up' $iB $over.Count
    $b = Join-Path $FilesDir $p.Rel
    New-Item -ItemType Directory -Path (Split-Path $b) -Force | Out-Null
    Copy-Item -LiteralPath $p.Dst -Destination $b -Force
    if ((Md5 $b) -ne $p.OldMd5) { throw ('backup md5 mismatch: ' + $p.Rel) }
    $rec += ('backup|' + $p.Rel + '|' + $p.OldMd5)
  }
  ProgDone 'Backing up'
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
Step 4 'Installing Turbo files'
try {
  $iI = 0
  foreach ($p in $plan) {
    $iI++; Prog 'Installing' $iI $plan.Count
    New-Item -ItemType Directory -Path (Split-Path $p.Dst) -Force | Out-Null
    Copy-Item -LiteralPath $p.Src -Destination $p.Dst -Force
  }
  ProgDone 'Installing'
} catch { Rollback ('INSTALL FAILED midway: ' + $_.Exception.Message) }

Step 5 'Verifying the installed files'
$bad = @()
$iV = 0
foreach ($p in $plan) { $iV++; Prog 'Verifying' $iV $plan.Count; if ((Md5 $p.Dst) -ne $p.SrcMd5) { $bad += ('md5 differs from the package: ' + $p.Rel) } }
ProgDone 'Verifying'
Step 6 'Checking your files again (they must be unchanged)'
$snapAfter = Snapshot 'Checking your files (after)'
foreach ($k in $snapBefore.Keys) {
  if (-not $snapAfter.ContainsKey($k)) { $bad += ('user file lost: ' + $k) }
  elseif ($snapAfter[$k] -ne $snapBefore[$k]) { $bad += ('user file changed: ' + $k) }
}
if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Host ('  FAIL ' + $_) -ForegroundColor Red }; Rollback 'VERIFY FAILED' }
Cleanup
Write-Host ('INSTALL PASS: ' + $plan.Count + ' files installed (md5 = package), Turbo.dll ' + (Md5 (Join-Path $LE 'turbo\Turbo.dll')) + ', ' + $snapBefore.Count + ' user files unchanged.') -ForegroundColor Green
Write-Host ('Undo: ' + (Join-Path $BDir 'Undo-Turbo.cmd')) -ForegroundColor Green
# ---- cleanup (X 2.0): keeps the Live Editor folder lean after the update; never fails the install ----
$Cl = Join-Path $LE 'turbo\Cleanup-Turbo.ps1'
if ($NoCleanup) { Write-Host 'Cleanup skipped (-NoCleanup).' }
elseif (-not (Has $Cl)) { Write-Host ('Cleanup skipped: ' + $Cl + ' not found.') -ForegroundColor Yellow }
else {
  Step 7 'Cleaning up old Turbo files (to the Recycle Bin)'
  try {
    & $Cl -LeRoot $LE -Apply -KeepInstalls $KeepInstalls -CurrentBackup $BDir
    if ($LASTEXITCODE -ne 0) { Write-Host ('Cleanup ended with code ' + $LASTEXITCODE + ' (the install itself PASSED).') -ForegroundColor Yellow }
  } catch { Write-Host ('Cleanup problem (the install itself PASSED): ' + $_.Exception.Message) -ForegroundColor Yellow }
}
exit 0
