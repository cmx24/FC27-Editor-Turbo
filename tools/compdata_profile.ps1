# compdata_profile.ps1 -Set stock|eerste | -Status [-Json]   (Windows PowerShell 5.1, ASCII only)
# 2.21 country launcher, chunk L1 (brief 2.21-compdata-per-save.md, G1 signed 10-19).
# Decides which of the two compdata sets is in mods\legacy BEFORE the game starts:
#   eerste = the 11 Eerste Divisie override files live in <LiveDir>   (Netherlands careers, e.g. SPARTA)
#   stock  = the same 11 files moved to <AsideDir>                     (every other career: the game's own schedules)
# It generalises eerste_compdata_aside.ps1 (-Aside = -Set stock, -Restore = -Set eerste, -Status = -Status); the old tool and the same
# aside folder are kept, so files set aside by the old tool are understood by this one.
# Rules (all checked BEFORE anything moves):
#   - the record turbo_dev\squads_pack\install_eerste_state.json is read, never written; its 11 md5s are the truth for both sides;
#   - the state must be exactly "all 11 live with the record md5, none aside" or "all 11 aside with the record md5, none live";
#     a partial set, a second copy, a changed md5 or a missing record refuses and names the file;
#   - refuses while FC27 / LauncherNoAuth run or while a launcher task is not Ready (IgnoreNew trap, nouac_tasks.ps1);
#   - files are only MOVED, never rewritten; every file is md5-checked again at its destination;
#   - on success writes the state file {profile, set_at, files:[{rel, md5}]} (also for a no-op set), atomically.
# Exit codes: 0 ok | 2 bad arguments | 10 game/launcher running | 11 a launcher task not Ready | 12 record missing or invalid |
#             13 state refused (partial / md5 / both copies) | 14 move failed (rolled back where possible)
# Every path and guard is a parameter so the self-test runs on scratch copies and never touches mods\legacy.
[CmdletBinding()]
param(
  [ValidateSet('stock', 'eerste')][string]$Set,
  [switch]$Status,
  [switch]$Json,
  [switch]$WhatIf,
  [string]$RecordPath = 'C:\FC 27 Live Editor\turbo_dev\squads_pack\install_eerste_state.json',
  [string]$LiveDir = '',      # default: the record's compdata.dst
  [string]$AsideDir = 'C:\FC 27 Live Editor\turbo_dev\backups\eerste_compdata_aside_20261019',
  [string]$StateOut = 'C:\FC 27 Live Editor\turbo_output\compdata_profile.json',
  [string[]]$ProcessNames = @('FC27', 'LauncherNoAuth'),
  [string[]]$TaskNames = @('LE_NoUAC', 'FC_NoUAC', 'LaunchFC27')
)
$ErrorActionPreference = 'Stop'
# the value 'none' means "no such guard" (offline self-test only: a scratch round trip must not depend on whether Cassio's launcher tasks run)
$ProcessNames = @($ProcessNames | Where-Object { $_ -and $_ -ne 'none' })
$TaskNames = @($TaskNames | Where-Object { $_ -and $_ -ne 'none' })
function Md5([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLower() }
function Say([string]$m, [string]$c = '') { if ($c) { Write-Host $m -ForegroundColor $c } else { Write-Host $m } }
function Fail([int]$code, [string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; exit $code }

if (-not $Set -and -not $Status) { Say 'usage: compdata_profile.ps1 -Set stock|eerste | -Status [-Json]  (see the header)' 'Yellow'; exit 2 }

# ---- the record (known-good md5 list)
if (-not (Test-Path -LiteralPath $RecordPath)) { Fail 12 ('install record missing: ' + $RecordPath) }
try { $rec = Get-Content -LiteralPath $RecordPath -Raw | ConvertFrom-Json } catch { Fail 12 ('install record unreadable: ' + $_.Exception.Message) }
if (-not $rec.compdata -or -not $rec.compdata.files -or @($rec.compdata.files).Count -eq 0) { Fail 12 'install record has no compdata.files list' }
$files = @($rec.compdata.files)
foreach ($f in $files) { if (-not $f.rel -or -not $f.md5) { Fail 12 'install record: a compdata file has no rel/md5' } }
if (-not $LiveDir) { $LiveDir = [string]$rec.compdata.dst }
if (-not $LiveDir) { Fail 12 'no live directory (record compdata.dst empty and no -LiveDir)' }

# ---- classify the current state (read-only)
function Get-State {
  $rows = @()
  foreach ($f in $files) {
    $live = Join-Path $LiveDir $f.rel; $side = Join-Path $AsideDir $f.rel
    $lm = $null; $sm = $null
    if (Test-Path -LiteralPath $live) { $lm = Md5 $live }
    if (Test-Path -LiteralPath $side) { $sm = Md5 $side }
    $rows += [pscustomobject]@{ rel = [string]$f.rel; want = ([string]$f.md5).ToLower(); live = $lm; aside = $sm }
  }
  $problems = @()
  $nLiveOk = 0; $nSideOk = 0
  foreach ($r in $rows) {
    if ($r.live -and $r.aside) { $problems += ('both copies present: ' + $r.rel) }
    if ($r.live) { if ($r.live -eq $r.want) { $nLiveOk++ } else { $problems += ('live md5 differs from the record: ' + $r.rel + ' (' + $r.live + ' want ' + $r.want + ')') } }
    if ($r.aside) { if ($r.aside -eq $r.want) { $nSideOk++ } else { $problems += ('aside md5 differs from the record: ' + $r.rel + ' (' + $r.aside + ' want ' + $r.want + ')') } }
    if (-not $r.live -and -not $r.aside) { $problems += ('missing on both sides: ' + $r.rel) }
  }
  $n = $rows.Count
  $profile = 'invalid'
  if (-not $problems.Count) {
    if ($nLiveOk -eq $n -and $nSideOk -eq 0) { $profile = 'eerste' }
    elseif ($nSideOk -eq $n -and $nLiveOk -eq 0) { $profile = 'stock' }
    else { $problems += ('partial set: {0} live, {1} aside of {2}' -f $nLiveOk, $nSideOk, $n) }
  }
  [pscustomobject]@{ profile = $profile; rows = $rows; problems = $problems }
}

function Show-State($st) {
  if ($Json) {
    $o = [ordered]@{ profile = $st.profile; live_dir = $LiveDir; aside_dir = $AsideDir; problems = @($st.problems)
      files = @($st.rows | ForEach-Object { [ordered]@{ rel = $_.rel; md5 = $_.want; live = $_.live; aside = $_.aside } }) }
    ($o | ConvertTo-Json -Depth 6)
  } else {
    foreach ($r in $st.rows) { '{0,-28} live={1} aside={2}' -f $r.rel, $(if ($r.live) { $r.live } else { '-' }), $(if ($r.aside) { $r.aside } else { '-' }) }
    $word = $st.profile; if ($word -eq 'invalid') { $word = 'INVALID (' + ($st.problems -join '; ') + ')' }
    'PROFILE: ' + $word + '  (record md5s: ' + $files.Count + ' files)'
  }
}

$st = Get-State
if ($Status -and -not $Set) {
  Show-State $st
  if ($st.profile -eq 'invalid') { exit 13 } else { exit 0 }
}

# ---- -Set: guards first, then the state check, then the moves
$running = @(); if ($ProcessNames.Count) { $running = @(Get-Process -Name $ProcessNames -ErrorAction SilentlyContinue) }
if ($running.Count) { Fail 10 ('game/launcher running: ' + (($running | ForEach-Object { $_.Name + ' ' + $_.Id }) -join ', ')) }
$badTasks = @()
foreach ($tn in $TaskNames) {
  $t = Get-ScheduledTask -TaskName $tn -ErrorAction SilentlyContinue
  $s = $(if ($t) { [string]$t.State } else { 'MISSING' })
  if ($s -ne 'Ready') { $badTasks += ($tn + '=' + $s) }
}
if ($badTasks.Count) { Fail 11 ('launcher task(s) not Ready (IgnoreNew would swallow the next launch): ' + ($badTasks -join ', ')) }
if ($st.profile -eq 'invalid') { Fail 13 ('state is not exactly all-live or all-aside; nothing moved: ' + ($st.problems -join ' | ')) }

$want = [string]$Set
$moved = @()
if ($st.profile -eq $want) {
  Say ('profile already ' + $want + ': nothing to move')
} elseif ($WhatIf) {
  foreach ($r in $st.rows) { Say ('would move ' + $(if ($want -eq 'stock') { 'live -> aside ' } else { 'aside -> live ' }) + $r.rel) }
  Say ('WHATIF: would set profile ' + $want + ', moves nothing, writes no state file') 'Yellow'
  exit 0
} else {
  try {
    foreach ($r in $st.rows) {
      $live = Join-Path $LiveDir $r.rel; $side = Join-Path $AsideDir $r.rel
      if ($want -eq 'stock') { $from = $live; $to = $side } else { $from = $side; $to = $live }
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
      Move-Item -LiteralPath $from -Destination $to
      $moved += [pscustomobject]@{ from = $from; to = $to }
      if ((Md5 $to) -ne $r.want) { throw ('md5 changed after the move: ' + $r.rel) }
    }
  } catch {
    $why = $_.Exception.Message
    foreach ($m in $moved) { try { Move-Item -LiteralPath $m.to -Destination $m.from -Force } catch { Say ('ROLLBACK FAILED for ' + $m.to + ': ' + $_.Exception.Message) 'Red' } }
    Fail 14 ('move failed (' + $why + '); rolled back ' + $moved.Count + ' file(s)')
  }
}

# ---- verify, then write the state file atomically
$after = Get-State
if ($after.profile -ne $want) { Fail 14 ('after the set the state is ' + $after.profile + ': ' + ($after.problems -join ' | ')) }
$now = Get-Date
$epoch = [int64]([DateTimeOffset]$now).ToUnixTimeSeconds()
$o = [ordered]@{
  version = 1; profile = $want; set_at = $now.ToString('yyyy-MM-ddTHH:mm:ss'); set_at_epoch = $epoch; set_by = 'compdata_profile.ps1'
  live_dir = $LiveDir; record = $RecordPath
  files = @($files | ForEach-Object { [ordered]@{ rel = [string]$_.rel; md5 = ([string]$_.md5).ToLower() } })
}
$dir = Split-Path -Parent $StateOut
if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$tmp = $StateOut + '.tmp'
[IO.File]::WriteAllText($tmp, (($o | ConvertTo-Json -Depth 6) + "`r`n"), (New-Object Text.UTF8Encoding $false))
if (Test-Path -LiteralPath $StateOut) { Remove-Item -LiteralPath $StateOut -Force }
Move-Item -LiteralPath $tmp -Destination $StateOut
Say ('SET {0} PASS: {1} files {2}, md5 = install record; state file {3}' -f $want, $files.Count, $(if ($want -eq 'stock') { 'in ' + $AsideDir } else { 'in ' + $LiveDir }), $StateOut) 'Green'
exit 0
