# Restore-TurboFiles.ps1 -BackupDir <protect_ folder> [-DryRun] [-NoPause] [-Areas settings,turbo_output,turbo]
#                        [-SettingsDir <dir>] [-TurboOutput <dir>] [-TurboDir <dir>] [-ProcessNames FC27,Launcher,LauncherNoAuth]
# (Windows PowerShell 5.1, ASCII only; run with the game and the Live Editor CLOSED)
#
# Puts back every file listed in <BackupDir>\MANIFEST.txt (written by Protect-TurboFiles.ps1) to the folder recorded there for its
# area ('root|<area>|<folder>' lines; -SettingsDir / -TurboOutput / -TurboDir override them). Nothing that is not a manifest line is
# ever written outside <BackupDir>. Order:
#   1. refuse while FC27 / Launcher / LauncherNoAuth runs; refuse when the manifest is missing or any backup copy does not match
#      its manifest md5 (nothing restored in that case)
#   2. plan: per file 'same' (already identical), 'overwrite' (exists, differs) or 'create' (absent); -DryRun prints the plan and stops
#   3. snapshot: every file that will be overwritten is first copied (md5-verified) to <BackupDir>\pre_restore\<stamp>\files\<area>\<rel>
#      with its own MANIFEST.txt, so this restore is itself undoable with
#         Restore-TurboFiles.ps1 -BackupDir <BackupDir>\pre_restore\<stamp>
#      (files this restore CREATED are listed there as 'absent|...' lines: the undo cannot remove them, it only reports them)
#   4. copy each manifest file over its destination (Copy-Item -Force), verify the md5, write <BackupDir>\RESTORE_RESULT.txt
#      with one 'OK|...' / 'FAIL|...' / 'SAME|...' line per file (a copy also goes next to the pre_restore manifest)
# Never deletes anything. Exit 0 = every file OK (or dry run), 1 = refused (nothing changed), 2 = one or more files FAIL (see the result file).
param(
  [string]$BackupDir = '',
  [switch]$DryRun,
  [switch]$NoPause,
  [string[]]$Areas = @('settings', 'turbo_output', 'turbo'),
  [string]$SettingsDir = '',
  [string]$TurboOutput = '',
  [string]$TurboDir = '',
  [string[]]$ProcessNames = @('FC27', 'Launcher', 'LauncherNoAuth')
)
$ErrorActionPreference = 'Stop'
$Utf8 = New-Object System.Text.UTF8Encoding($false)

function Pause-Exit([int]$code) { if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }; exit $code }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; Pause-Exit 1 }
function Has([string]$p) { Test-Path -LiteralPath $p }
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLowerInvariant() } else { 'ABSENT' } }
function Full([string]$p) { (Resolve-Path -LiteralPath $p).ProviderPath.TrimEnd('\') }
function Fmt-Size([int64]$b) { if ($b -ge 1GB) { '{0:N2} GB' -f ($b / 1GB) } elseif ($b -ge 1MB) { '{0:N1} MB' -f ($b / 1MB) } elseif ($b -ge 1KB) { '{0:N0} KB' -f ($b / 1KB) } else { '{0:N0} B' -f $b } }

try {
  if ($DryRun) { Write-Host '*** DRY RUN: nothing is changed ***' -ForegroundColor Yellow }
  if ($BackupDir -eq '') { Fail 'pass -BackupDir <the protect_<stamp> folder written by Protect-TurboFiles.ps1>' }
  $Areas = @($Areas | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
  $ProcessNames = @($ProcessNames | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
  foreach ($a in $Areas) { if (@('settings', 'turbo_output', 'turbo') -notcontains $a) { Fail ('-Areas: unknown area ' + $a + ' (settings, turbo_output, turbo)') } }

  # ---- running game / Live Editor ----
  $run = @()
  if ($ProcessNames.Count -gt 0) { $run = @(Get-Process -Name $ProcessNames -ErrorAction SilentlyContinue) }
  if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ') + ' - close it first') }

  # ---- the backup and its manifest ----
  if (-not (Has $BackupDir)) { Fail ('backup folder missing: ' + $BackupDir) }
  $BackupDir = Full $BackupDir
  $Manifest = Join-Path $BackupDir 'MANIFEST.txt'
  $Files = Join-Path $BackupDir 'files'
  if (-not (Has $Manifest)) { Fail ('no MANIFEST.txt in ' + $BackupDir + ' (not a Protect-TurboFiles backup, or Protect failed midway)') }
  $mlines = @(Get-Content -LiteralPath $Manifest)
  if ($mlines.Count -eq 0 -or $mlines[0] -ne '# Protect-TurboFiles manifest v1') { Fail ('MANIFEST.txt does not start with "# Protect-TurboFiles manifest v1": ' + $Manifest) }
  $roots = @{}; $entries = @(); $absentNotes = @()
  foreach ($l in $mlines) {
    if ($l -eq '' -or $l.StartsWith('#')) { continue }
    $a = $l.Split('|')
    switch ($a[0]) {
      'root' { if ($a.Count -ne 3) { Fail ('bad manifest line: ' + $l) }; $roots[$a[1]] = $a[2] }
      'file' {
        if ($a.Count -ne 5) { Fail ('bad manifest line: ' + $l) }
        $entries += [pscustomobject]@{ Area = $a[1]; Rel = $a[2]; Size = [int64]$a[3]; Md5 = $a[4].ToLowerInvariant() }
      }
      'absent' { $absentNotes += ($a[1] + '\' + $a[2]) }
      default { Fail ('unknown manifest line kind "' + $a[0] + '": ' + $l) }
    }
  }
  $entries = @($entries)
  if ($entries.Count -eq 0) { Fail 'the manifest lists no files' }
  foreach ($e in $entries) {
    if (@('settings', 'turbo_output', 'turbo') -notcontains $e.Area) { Fail ('manifest: unknown area ' + $e.Area) }
    if ($e.Rel -eq '' -or $e.Rel.StartsWith('\') -or $e.Rel.Contains('/') -or $e.Rel.Contains(':') -or ($e.Rel.Split('\') -contains '..') -or ($e.Rel.Split('\') -contains '.')) { Fail ('manifest: unsafe relative path: ' + $e.Rel) }
    if ($e.Md5 -notmatch '^[0-9a-f]{32}$') { Fail ('manifest: not an md5 for ' + $e.Rel + ': ' + $e.Md5) }
  }
  if ($SettingsDir -ne '') { $roots['settings'] = $SettingsDir }
  if ($TurboOutput -ne '') { $roots['turbo_output'] = $TurboOutput }
  if ($TurboDir -ne '') { $roots['turbo'] = $TurboDir }
  $work = @($entries | Where-Object { $Areas -contains $_.Area })
  if ($work.Count -eq 0) { Fail ('no manifest file falls in -Areas ' + ($Areas -join ',')) }
  foreach ($a in @($work | ForEach-Object { $_.Area } | Sort-Object -Unique)) {
    if (-not $roots.ContainsKey($a) -or $roots[$a] -eq '') { Fail ('the manifest names no root folder for area ' + $a + ' (pass -SettingsDir / -TurboOutput / -TurboDir)') }
    if (-not [System.IO.Path]::IsPathRooted($roots[$a])) { Fail ('root for area ' + $a + ' is not an absolute path: ' + $roots[$a]) }
  }

  # ---- 1. the backup copies must match the manifest ----
  $bad = @()
  foreach ($e in $work) {
    $src = Join-Path (Join-Path $Files $e.Area) $e.Rel
    $m = Md5 $src
    if ($m -ne $e.Md5) { $bad += ('  ' + $e.Area + '\' + $e.Rel + ': backup copy md5 ' + $m + ' != manifest ' + $e.Md5) }
  }
  if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Host $_ -ForegroundColor Red }; Fail ('the backup does not match its manifest (' + $bad.Count + ' files); nothing restored') }

  # ---- 2. plan ----
  $plan = @()
  foreach ($e in $work) {
    $dst = Join-Path $roots[$e.Area] $e.Rel
    $cur = Md5 $dst
    $state = if ($cur -eq 'ABSENT') { 'create' } elseif ($cur -eq $e.Md5) { 'same' } else { 'overwrite' }
    $plan += [pscustomobject]@{ Area = $e.Area; Rel = $e.Rel; Src = (Join-Path (Join-Path $Files $e.Area) $e.Rel); Dst = $dst; Md5 = $e.Md5; Size = $e.Size; Cur = $cur; State = $state }
  }
  $plan = @($plan)
  $nSame = @($plan | Where-Object { $_.State -eq 'same' }).Count
  $nOver = @($plan | Where-Object { $_.State -eq 'overwrite' }).Count
  $nNew = @($plan | Where-Object { $_.State -eq 'create' }).Count
  Write-Host ('Backup          : ' + $BackupDir + ' (' + $work.Count + ' files verified against MANIFEST.txt)')
  foreach ($a in @($plan | ForEach-Object { $_.Area } | Sort-Object -Unique)) { Write-Host (('{0,-16}: {1}' -f $a, $roots[$a])) }
  Write-Host ('Plan            : ' + $nOver + ' overwrite, ' + $nNew + ' create, ' + $nSame + ' already identical (skipped)')
  if ($absentNotes.Count -gt 0) { Write-Host ('Note            : ' + $absentNotes.Count + ' files were absent when this snapshot was taken (created by the restore it undoes); they are NOT removed: ' + ($absentNotes -join ', ')) -ForegroundColor Yellow }
  foreach ($p in @($plan | Where-Object { $_.State -ne 'same' })) { Write-Host ('  ' + $p.State.PadRight(9) + ' ' + $p.Dst + '  (' + (Fmt-Size $p.Size) + ')') }
  if ($DryRun) { Write-Host 'DRY RUN done: nothing changed.' -ForegroundColor Yellow; Pause-Exit 0 }
  if ($nOver + $nNew -eq 0) {
    Write-Host 'Nothing to do: every file already equals its backup copy.' -ForegroundColor Green
    Pause-Exit 0
  }

  # ---- 3. pre_restore snapshot of every file about to be overwritten ----
  $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
  $Pre = Join-Path (Join-Path $BackupDir 'pre_restore') $stamp
  $PreFiles = Join-Path $Pre 'files'
  if (Has $Pre) { Fail ('pre_restore folder already exists (run again in a second): ' + $Pre) }
  $preLines = @('# Protect-TurboFiles manifest v1', ('# created|' + (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')), ('# pre_restore snapshot taken by Restore-TurboFiles.ps1 before restoring ' + $BackupDir))
  foreach ($a in @($plan | ForEach-Object { $_.Area } | Sort-Object -Unique)) { $preLines += ('root|' + $a + '|' + $roots[$a]) }
  [void][System.IO.Directory]::CreateDirectory($PreFiles)
  foreach ($p in $plan) {
    if ($p.State -eq 'create') { $preLines += ('absent|' + $p.Area + '|' + $p.Rel); continue }
    if ($p.State -ne 'overwrite') { continue }
    $snap = Join-Path (Join-Path $PreFiles $p.Area) $p.Rel
    [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $snap))
    [System.IO.File]::Copy($p.Dst, $snap, $false)   # literal paths, never overwrites (throws if $snap exists)
    $sm = Md5 $snap
    if ($sm -ne $p.Cur) { throw ('pre_restore copy of ' + $p.Dst + ' does not verify (' + $sm + ' != ' + $p.Cur + '); nothing restored') }
    $preLines += ('file|' + $p.Area + '|' + $p.Rel + '|' + (Get-Item -LiteralPath $snap).Length + '|' + $sm)
  }
  [System.IO.File]::WriteAllLines((Join-Path $Pre 'MANIFEST.txt'), [string[]]$preLines, $Utf8)
  Write-Host ('pre_restore     : ' + $nOver + ' current files snapshotted (md5-verified) to ' + $Pre)

  # ---- 4. restore + verify ----
  $results = @(('# Restore-TurboFiles result ' + (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')), ('# backup|' + $BackupDir), ('# pre_restore|' + $Pre), '# lines: <OK|FAIL|SAME>|<area>|<relative path>|<destination>|<detail>')
  $fails = 0; $ok = 0
  foreach ($p in $plan) {
    if ($p.State -eq 'same') { $results += ('SAME|' + $p.Area + '|' + $p.Rel + '|' + $p.Dst + '|already ' + $p.Md5); continue }
    try {
      [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $p.Dst))
      Copy-Item -LiteralPath $p.Src -Destination $p.Dst -Force
      $after = Md5 $p.Dst
      if ($after -eq $p.Md5) { $ok++; $results += ('OK|' + $p.Area + '|' + $p.Rel + '|' + $p.Dst + '|' + $p.State + ' md5 ' + $after) }
      else { $fails++; $results += ('FAIL|' + $p.Area + '|' + $p.Rel + '|' + $p.Dst + '|md5 after copy ' + $after + ' != ' + $p.Md5) }
    } catch {
      $fails++; $results += ('FAIL|' + $p.Area + '|' + $p.Rel + '|' + $p.Dst + '|' + $_.Exception.Message)
    }
  }
  $results += ('# summary|ok=' + $ok + '|fail=' + $fails + '|same=' + $nSame)
  $ResultFile = Join-Path $BackupDir 'RESTORE_RESULT.txt'
  [System.IO.File]::WriteAllLines($ResultFile, [string[]]$results, $Utf8)
  [System.IO.File]::WriteAllLines((Join-Path $Pre 'RESTORE_RESULT.txt'), [string[]]$results, $Utf8)
  foreach ($r in $results) { if ($r.StartsWith('FAIL|')) { Write-Host ('  ' + $r) -ForegroundColor Red } }
  Write-Host ('Result file     : ' + $ResultFile)
  Write-Host ('Undo this restore: powershell -NoProfile -ExecutionPolicy Bypass -File Restore-TurboFiles.ps1 -BackupDir "' + $Pre + '"')
  if ($fails -eq 0) { Write-Host ('RESTORE PASS: ' + $ok + ' files restored and md5-verified, ' + $nSame + ' already identical.') -ForegroundColor Green; Pause-Exit 0 }
  Write-Host ('RESTORE FAIL: ' + $fails + ' files failed, ' + $ok + ' restored. Fix the cause (file in use? permissions?) and run again; the same backup can be restored any number of times.') -ForegroundColor Red
  Pause-Exit 2
} catch {
  Write-Host ('RESTORE FAILED midway: ' + $_.Exception.Message) -ForegroundColor Red
  Write-Host 'If the message came from the pre_restore step, no game file was written yet. Otherwise see RESTORE_RESULT.txt in the backup folder.' -ForegroundColor Red
  Pause-Exit 2
}
