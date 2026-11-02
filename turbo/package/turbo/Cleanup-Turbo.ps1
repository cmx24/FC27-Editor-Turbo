# Cleanup-Turbo.ps1 [-LeRoot <dir>] [-Apply] [-KeepInstalls <n>] [-LogMaxMB <n>] [-CurrentBackup <dir>]   (Windows PowerShell 5.1, ASCII only)
# Keeps the Live Editor folder lean after Turbo updates (Turbo X 2.0, Cassio 2026-11-01). Without -Apply it only LISTS what it would
# remove: nothing changes. With -Apply everything goes to the Recycle Bin (recoverable), nothing is deleted outright. The game must be closed.
# Install-Turbo.ps1 runs it with -Apply after every INSTALL PASS; Cleanup-Turbo.cmd (next to this file) runs it by hand (list, then ask).
# What it removes - Turbo's own leftovers only:
#   1. turbo_backups\install_<stamp> folders beyond the newest -KeepInstalls (default 2). Each is the Undo of one install; the newest
#      ones always stay, so the last update can always be undone.
#   2. stale Turbo files: files an OLDER install record lists as installed or added that the newest install record does not list, inside
#      Turbo's own program folders (turbo\, lua\autorun\turbo*, lua\libs\v2\imports\turbo\; never turbo\kits\, turbo\callnames\masters\,
#      turbo_output\, kits\), and only while they still hold the bytes Turbo installed (a file you changed stays). Before the removal each
#      one is copied into the newest install's backup and added to its install_record.txt: that install's Undo puts it back.
#   3. old launchers: _INSTALL_TURBOX_*.cmd in the Live Editor folder, except the newest one.
#   4. Turbo logs larger than -LogMaxMB (default 25) in turbo_output (turbo_gui.log, player_capture.log, kit_swap_log.txt, asset_writes.log):
#      renamed to <name>.old (the previous .old goes to the Recycle Bin); Turbo starts a new log at the next game start.
#   5. leftover temp folders of the installer (%TEMP%\turbo_src_*) older than one day.
# Never touched: saves, the game's settings, Live Editor's own files, turbo_output data (presets, careers, pictures, kept edits), kits,
# turbo_dev\, and every other folder in turbo_backups\.
# Exit 0 = done (or only listed), 1 = refused (nothing changed), 2 = something could not be removed (the rest was done; it is listed).
param(
  [string]$LeRoot = '',
  [switch]$Apply,
  [int]$KeepInstalls = 2,
  [int]$LogMaxMB = 25,
  [string]$CurrentBackup = ''
)
$ErrorActionPreference = 'Stop'
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash } else { 'ABSENT' } }
function Has([string]$p) { Test-Path -LiteralPath $p }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; exit 1 }
function Size([string]$p) {
  if (-not (Has $p)) { return [int64]0 }
  $it = Get-Item -LiteralPath $p -Force
  if (-not $it.PSIsContainer) { return [int64]$it.Length }
  $s = [int64]0
  foreach ($f in @(Get-ChildItem -LiteralPath $p -Recurse -File -Force -ErrorAction SilentlyContinue)) { $s += $f.Length }
  return $s
}
function MB([int64]$b) { return ([Math]::Round($b / 1MB, 1)).ToString() + ' MB' }

# ---- where (absolute paths only: a double-click starts in C:\Windows\System32) ----
$Self = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($LeRoot -eq '') { $LeRoot = Split-Path -Parent $Self }   # this file lives in <Live Editor>\turbo\
if (-not (Has (Join-Path $LeRoot 'FCLiveEditor.DLL'))) { Fail ('not the Live Editor folder (no FCLiveEditor.DLL): ' + $LeRoot) }
$LE = (Resolve-Path -LiteralPath $LeRoot).ProviderPath.TrimEnd('\')
if ($KeepInstalls -lt 1) { Fail '-KeepInstalls must be 1 or more (the newest install keeps its Undo)' }
$run = @(Get-Process -Name FC27, Launcher, LauncherNoAuth -ErrorAction SilentlyContinue)
if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ') + ' - close it first') }
if ($Apply) { Add-Type -AssemblyName Microsoft.VisualBasic }
Write-Host ('Turbo cleanup    : ' + $LE + $(if ($Apply) { ' (removals go to the Recycle Bin)' } else { ' (LIST ONLY: nothing is changed; run with -Apply to clean up)' }))

$items = @()      # { What; Path; Bytes }
$problems = @()
function Recycle([string]$p) {
  if ((Get-Item -LiteralPath $p -Force).PSIsContainer) {
    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($p, [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs, [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin)
  } else {
    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($p, [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs, [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin)
  }
}

# ---- the install records, newest first ----
$BRoot = Join-Path $LE 'turbo_backups'
$installs = @()
if (Has $BRoot) { $installs = @(Get-ChildItem -LiteralPath $BRoot -Directory -Force | Where-Object { $_.Name -match '^install_\d{8}_\d{6}$' } | Sort-Object Name -Descending) }
$newest = $null
if ($CurrentBackup -ne '') {
  if (-not (Has (Join-Path $CurrentBackup 'install_record.txt'))) { Fail ('no install_record.txt in -CurrentBackup ' + $CurrentBackup) }
  $newest = Get-Item -LiteralPath $CurrentBackup
} elseif ($installs.Count -gt 0) { $newest = $installs[0] }
function ReadRecord([string]$dir) {
  $r = @{ files = @{}; ok = $false }
  $f = Join-Path $dir 'install_record.txt'
  if (-not (Has $f)) { return $r }
  foreach ($l in (Get-Content -LiteralPath $f)) {
    $a = $l.Split('|')
    if ($a.Count -ge 3 -and ($a[0] -eq 'installed' -or $a[0] -eq 'new') -and $a[1] -ne '') { $r.files[$a[1]] = $a[2].ToUpperInvariant() }
  }
  $r.ok = $true
  return $r
}
# Turbo's program folders (where an older package's files may be left behind); user data folders are never in scope
function IsProgramFile([string]$rel) {
  $r = $rel.ToLowerInvariant()
  foreach ($no in 'turbo\kits\', 'turbo\callnames\masters\', 'turbo_output\', 'kits\', 'turbo_dev\', 'turbo_backups\') { if ($r.StartsWith($no)) { return $false } }
  if ($r -eq 'turbo_config.json') { return $false }
  return $r.StartsWith('turbo\') -or $r.StartsWith('lua\libs\v2\imports\turbo\') -or ($r.StartsWith('lua\autorun\turbo') -and $r.EndsWith('.lua'))
}

# ---- 2. stale Turbo files (computed before any old backup is removed: their records are the evidence) ----
$stale = @()
if ($null -ne $newest) {
  $cur = ReadRecord $newest.FullName
  if (-not $cur.ok) { Fail ('the newest install has no readable record: ' + $newest.FullName) }
  $seen = @{}
  foreach ($old in $installs) {
    if ($old.FullName -ieq $newest.FullName) { continue }
    $rec = ReadRecord $old.FullName
    foreach ($rel in $rec.files.Keys) {
      if ($seen.ContainsKey($rel) -or $cur.files.ContainsKey($rel) -or -not (IsProgramFile $rel)) { continue }
      $seen[$rel] = 1   # the newest older record that lists it decides (installs are sorted newest first)
      $p = Join-Path $LE $rel
      if ((Has $p) -and (Md5 $p) -eq $rec.files[$rel]) { $stale += [pscustomobject]@{ Rel = $rel; Path = $p; Md5 = $rec.files[$rel] } }
    }
  }
}
foreach ($s in $stale) { $items += [pscustomobject]@{ What = 'stale Turbo file (an older version''s; kept in the newest backup)'; Path = $s.Path; Bytes = (Size $s.Path) } }

# ---- 1. old install backups ----
$oldInstalls = @($installs | Select-Object -Skip $KeepInstalls)
if ($null -ne $newest) { $oldInstalls = @($oldInstalls | Where-Object { $_.FullName -ine $newest.FullName }) }
foreach ($d in $oldInstalls) { $items += [pscustomobject]@{ What = 'old install backup (Undo of an older update)'; Path = $d.FullName; Bytes = (Size $d.FullName) } }

# ---- 3. old launchers ----
$launchers = @(Get-ChildItem -LiteralPath $LE -File -Force -Filter '_INSTALL_TURBOX_*.cmd' | Sort-Object LastWriteTime -Descending)
foreach ($l in @($launchers | Select-Object -Skip 1)) { $items += [pscustomobject]@{ What = 'old install launcher'; Path = $l.FullName; Bytes = [int64]$l.Length } }

# ---- 4. oversized logs (rotated, not deleted: the current one becomes <name>.old) ----
$rot = @()
foreach ($n in 'turbo_gui.log', 'player_capture.log', 'kit_swap_log.txt', 'asset_writes.log') {
  $p = Join-Path (Join-Path $LE 'turbo_output') $n
  if ((Has $p) -and (Size $p) -gt ([int64]$LogMaxMB * 1MB)) {
    $rot += $p
    $o = $p + '.old'
    if (Has $o) { $items += [pscustomobject]@{ What = 'previous rotated log'; Path = $o; Bytes = (Size $o) } }
  }
}

# ---- 5. installer temp folders ----
$tmp = [IO.Path]::GetTempPath()
foreach ($t in @(Get-ChildItem -LiteralPath $tmp -Directory -Force -Filter 'turbo_src_*' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-1) })) {
  $items += [pscustomobject]@{ What = 'installer temp folder'; Path = $t.FullName; Bytes = (Size $t.FullName) }
}

# ---- report ----
$total = [int64]0
foreach ($i in $items) { $total += $i.Bytes; Write-Host ('  ' + $(if ($Apply) { 'remove ' } else { 'would remove ' }) + $i.Path + '  (' + (MB $i.Bytes) + ', ' + $i.What + ')') }
foreach ($p in $rot) { Write-Host ('  ' + $(if ($Apply) { 'rotate ' } else { 'would rotate ' }) + $p + '  (' + (MB (Size $p)) + ' -> ' + (Split-Path -Leaf $p) + '.old)') }
if ($items.Count -eq 0 -and $rot.Count -eq 0) { Write-Host 'Nothing to clean up: the folder is already lean.' -ForegroundColor Green; exit 0 }
Write-Host ('Total            : ' + $items.Count + ' items, ' + (MB $total) + $(if ($rot.Count) { '; ' + $rot.Count + ' log(s) rotated' } else { '' }))
if (-not $Apply) { Write-Host 'LIST ONLY: nothing changed. Run again with -Apply (Cleanup-Turbo.cmd asks first).' -ForegroundColor Yellow; exit 0 }

# ---- apply ----
# 2 first: each stale file goes into the newest backup (verified) and its record, so that install's Undo restores it; then the file goes
if ($stale.Count -gt 0) {
  $files = Join-Path $newest.FullName 'files'
  $recF = Join-Path $newest.FullName 'install_record.txt'
  foreach ($s in $stale) {
    try {
      $b = Join-Path $files $s.Rel
      New-Item -ItemType Directory -Path (Split-Path $b) -Force | Out-Null
      Copy-Item -LiteralPath $s.Path -Destination $b -Force
      if ((Md5 $b) -ne $s.Md5) { throw ('backup md5 mismatch for ' + $s.Rel) }
      [IO.File]::AppendAllText($recF, ('backup|' + $s.Rel + '|' + $s.Md5 + "`r`n"))
      Recycle $s.Path
    } catch { $problems += ('stale file kept: ' + $s.Rel + ' (' + $_.Exception.Message + ')') }
  }
}
foreach ($i in $items) {
  if ($stale | Where-Object { $_.Path -ieq $i.Path }) { continue }   # done above
  try { if (Has $i.Path) { Recycle $i.Path } } catch { $problems += ('not removed: ' + $i.Path + ' (' + $_.Exception.Message + ')') }
}
foreach ($p in $rot) {
  try { Move-Item -LiteralPath $p -Destination ($p + '.old') -Force } catch { $problems += ('log not rotated: ' + $p + ' (' + $_.Exception.Message + ')') }
}
if ($problems.Count -gt 0) { $problems | ForEach-Object { Write-Host ('  PROBLEM ' + $_) -ForegroundColor Yellow }; Write-Host 'CLEANUP PARTLY DONE (the rest is in the Recycle Bin).' -ForegroundColor Yellow; exit 2 }
Write-Host ('CLEANUP DONE: ' + $items.Count + ' items (' + (MB $total) + ') moved to the Recycle Bin' + $(if ($rot.Count) { ', ' + $rot.Count + ' log(s) rotated' } else { '' }) + '.') -ForegroundColor Green
exit 0
