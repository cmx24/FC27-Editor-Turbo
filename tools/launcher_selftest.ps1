# launcher_selftest.ps1 [-Section L1|L2|All] [-Scratch <dir>]   (Windows PowerShell 5.1, ASCII only)
# Offline self-test of compdata_profile.ps1 (L1) and turbo_launch.ps1 (L2) on SCRATCH copies. It never moves a live file, never starts
# the game, never runs a scheduled task: the scripts under test are only given scratch paths, an unknown task name and -NoStart / -WhatIf.
# Guard: the live mods\legacy compdata md5s are read (not written) before and after and must be identical.
# Prints one line per case and "SELF-TEST PASSED|FAILED (n cases, m failed)". Exit 0 = pass.
param(
  [ValidateSet('L1', 'L2', 'All')][string]$Section = 'All',
  [string]$Scratch = (Join-Path ([IO.Path]::GetTempPath()) ('launcher_selftest_' + (Get-Date -Format 'yyyyMMdd_HHmmss')))
)
$ErrorActionPreference = 'Stop'
$Tools = $PSScriptRoot
$Prof = Join-Path $Tools 'compdata_profile.ps1'
$Launch = Join-Path $Tools 'turbo_launch.ps1'
$Rec = 'C:\FC 27 Live Editor\turbo_dev\squads_pack\install_eerste_state.json'
$NoTask = 'TurboSelfTestNoSuchTask'
$script:cases = 0; $script:fails = 0
function Md5([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLower() }
function Check([bool]$cond, [string]$what) {
  $script:cases++
  if ($cond) { Write-Host ('  ok    ' + $what) } else { $script:fails++; Write-Host ('  FAIL  ' + $what) -ForegroundColor Red }
}
# run a script in a child powershell (so its exit codes do not end this test); returns @{ code; out }
function Run([string]$script, [string[]]$a) {
  $ErrorActionPreference = 'Continue'
  $o = & powershell -NoProfile -ExecutionPolicy Bypass -File $script @a 2>&1 | Out-String
  $c = $LASTEXITCODE
  $ErrorActionPreference = 'Stop'
  @{ code = $c; out = $o }
}
$rec = Get-Content -LiteralPath $Rec -Raw | ConvertFrom-Json
$liveReal = [string]$rec.compdata.dst
function LiveSnapshot { ($rec.compdata.files | ForEach-Object { $p = Join-Path $liveReal $_.rel; $_.rel + '=' + $(if (Test-Path -LiteralPath $p) { Md5 $p } else { '-' }) }) -join ';' }
$snapBefore = LiveSnapshot
New-Item -ItemType Directory -Force -Path $Scratch | Out-Null
Write-Host ('scratch: ' + $Scratch)
Write-Host ('live mods\legacy compdata (read-only look): ' + $snapBefore.Substring(0, [Math]::Min(80, $snapBefore.Length)) + '...')

function New-Scratch([string]$name) {
  $d = Join-Path $Scratch $name
  if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
  $live = Join-Path $d 'live'; New-Item -ItemType Directory -Force -Path $live | Out-Null
  foreach ($f in $rec.compdata.files) {
    $dst = Join-Path $live $f.rel; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Copy-Item -LiteralPath (Join-Path $rec.compdata_dir $f.rel) -Destination $dst
  }
  @{ dir = $d; live = $live; aside = (Join-Path $d 'aside'); state = (Join-Path $d 'compdata_profile.json') }
}
function PArgs($s, [string[]]$more, [string]$proc = 'none', [string]$task = 'none') {
  @('-LiveDir', $s.live, '-AsideDir', $s.aside, '-StateOut', $s.state, '-ProcessNames', $proc, '-TaskNames', $task) + $more
}
function CountDir([string]$d) { if (Test-Path -LiteralPath $d) { @(Get-ChildItem -LiteralPath $d -Recurse -File).Count } else { 0 } }

if ($Section -in 'L1', 'All') {
  Write-Host '== L1 compdata_profile.ps1'
  $n = $rec.compdata.files.Count
  $s = New-Scratch 'l1_roundtrip'
  $r = Run $Prof (PArgs $s @('-Status'))
  Check ($r.code -eq 0 -and $r.out -match 'PROFILE: eerste') '-Status on 11 live files = eerste, exit 0'
  $r = Run $Prof (PArgs $s @('-Status', '-Json'))
  $j = $null; try { $j = $r.out | ConvertFrom-Json } catch { }
  Check ($j -and $j.profile -eq 'eerste' -and @($j.files).Count -eq $n) '-Status -Json parses, profile eerste, 11 files'
  $r = Run $Prof (PArgs $s @('-Set', 'stock', '-WhatIf'))
  Check ($r.code -eq 0 -and (CountDir $s.live) -eq $n -and -not (Test-Path $s.state)) '-Set stock -WhatIf: exit 0, nothing moved, no state file'
  $r = Run $Prof (PArgs $s @('-Set', 'stock'))
  Check ($r.code -eq 0 -and $r.out -match 'SET stock PASS' -and (CountDir $s.live) -eq 0 -and (CountDir $s.aside) -eq $n) '-Set stock: exit 0, 11 files aside, 0 live'
  $sj = Get-Content -LiteralPath $s.state -Raw | ConvertFrom-Json
  Check ($sj.profile -eq 'stock' -and $sj.set_at -match '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$' -and $sj.set_at_epoch -gt 1700000000 -and @($sj.files).Count -eq $n -and $sj.files[0].md5.Length -eq 32) 'state file: profile stock, set_at ISO, set_at_epoch, 11 {rel, md5}'
  $ok = $true; foreach ($f in $rec.compdata.files) { if ((Md5 (Join-Path $s.aside $f.rel)) -ne $f.md5) { $ok = $false } }
  Check $ok 'aside files byte-equal to the install record md5 (moves only)'
  $r = Run $Prof (PArgs $s @('-Status'))
  Check ($r.code -eq 0 -and $r.out -match 'PROFILE: stock') '-Status after the set = stock'
  $r = Run $Prof (PArgs $s @('-Set', 'stock'))
  Check ($r.code -eq 0 -and $r.out -match 'already stock' -and (Test-Path $s.state)) '-Set stock again = no-op, exit 0, state file still written'
  $r = Run $Prof (PArgs $s @('-Set', 'eerste'))
  Check ($r.code -eq 0 -and (CountDir $s.live) -eq $n -and (CountDir $s.aside) -eq 0) '-Set eerste: 11 live again, 0 aside'
  $ok = $true; foreach ($f in $rec.compdata.files) { if ((Md5 (Join-Path $s.live $f.rel)) -ne $f.md5) { $ok = $false } }
  Check $ok 'restored files byte-equal to the record md5 (round trip)'
  $sj = Get-Content -LiteralPath $s.state -Raw | ConvertFrom-Json
  Check ($sj.profile -eq 'eerste') 'state file profile eerste'

  # refusals: nothing may move
  $s = New-Scratch 'l1_md5'
  Add-Content -LiteralPath (Join-Path $s.live 'compobj.txt') -Value 'x' -Encoding ASCII
  $r = Run $Prof (PArgs $s @('-Set', 'stock'))
  Check ($r.code -eq 13 -and $r.out -match 'compobj.txt' -and (CountDir $s.aside) -eq 0 -and (CountDir $s.live) -eq $n) 'md5 refusal: a changed live file -> exit 13, names compobj.txt, nothing moved'
  $s = New-Scratch 'l1_missing'
  Remove-Item -LiteralPath (Join-Path $s.live 'tasks.txt')
  $r = Run $Prof (PArgs $s @('-Set', 'stock'))
  Check ($r.code -eq 13 -and $r.out -match 'missing on both sides: tasks.txt' -and (CountDir $s.aside) -eq 0) 'partial-state refusal: a missing file -> exit 13, nothing moved'
  $s = New-Scratch 'l1_both'
  New-Item -ItemType Directory -Force -Path $s.aside | Out-Null
  Copy-Item -LiteralPath (Join-Path $s.live 'tasks.txt') -Destination (Join-Path $s.aside 'tasks.txt')
  $r = Run $Prof (PArgs $s @('-Set', 'stock'))
  Check ($r.code -eq 13 -and $r.out -match 'both copies present: tasks.txt' -and (CountDir $s.live) -eq $n) 'second-copy refusal: file on both sides -> exit 13'
  $s = New-Scratch 'l1_partial'
  foreach ($f in @($rec.compdata.files)[0..4]) {
    $dst = Join-Path $s.aside $f.rel; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Move-Item -LiteralPath (Join-Path $s.live $f.rel) -Destination $dst
  }
  $r = Run $Prof (PArgs $s @('-Set', 'eerste'))
  Check ($r.code -eq 13 -and $r.out -match 'partial set: 6 live, 5 aside') 'partial-set refusal: 6 live + 5 aside (each md5-valid) -> exit 13'
  $r = Run $Prof (PArgs $s @('-Status'))
  Check ($r.code -eq 13 -and $r.out -match 'INVALID') '-Status on a partial set: INVALID, exit 13'

  $s = New-Scratch 'l1_guards'
  $r = Run $Prof (PArgs $s @('-Set', 'stock') 'powershell' 'none')
  Check ($r.code -eq 10 -and (CountDir $s.aside) -eq 0) 'running-process refusal: a running process of a guarded name -> exit 10, nothing moved'
  $r = Run $Prof (PArgs $s @('-Set', 'stock') 'none' $NoTask)
  Check ($r.code -eq 11 -and $r.out -match 'MISSING' -and (CountDir $s.aside) -eq 0) 'task refusal: a task that is not Ready (here: missing) -> exit 11, nothing moved'
  $r = Run $Prof ((PArgs $s @('-Set', 'stock')) + @('-RecordPath', (Join-Path $Scratch 'no_such_record.json')))
  Check ($r.code -eq 12 -and (CountDir $s.aside) -eq 0) 'missing install record -> exit 12, nothing moved'
  $r = Run $Prof @('-Set')
  Check ($r.code -ne 0) '-Set without a value is an error'

  # move failure: a FILE where the aside subfolder must be -> the second-to-last moves fail, everything already moved is rolled back
  $s = New-Scratch 'l1_rollback'
  New-Item -ItemType Directory -Force -Path $s.aside | Out-Null
  Set-Content -LiteralPath (Join-Path $s.aside 'schedules') -Value 'blocker' -Encoding ASCII
  $r = Run $Prof (PArgs $s @('-Set', 'stock'))
  Check ($r.code -eq 14 -and $r.out -match 'rolled back' -and (CountDir $s.live) -eq $n) 'move failure -> exit 14, files already moved are rolled back (11 live again)'
  $ok = $true; foreach ($f in $rec.compdata.files) { if ((Md5 (Join-Path $s.live $f.rel)) -ne $f.md5) { $ok = $false } }
  Check $ok 'after the rollback every live file still equals the record md5'

  # capability parity with the old tool: same record, same aside folder, same 3 capabilities
  $old = Get-Content -LiteralPath (Join-Path 'C:\FC 27 Live Editor\turbo_dev\tools' 'eerste_compdata_aside.ps1') -Raw
  $new = Get-Content -LiteralPath $Prof -Raw
  $m = [regex]::Match($old, "eerste_compdata_aside_\d{8}")
  Check ($m.Success -and $new.Contains($m.Value) -and $new.Contains('install_eerste_state.json') -and $new.Contains("'FC27'")) 'parity: same aside folder and record as eerste_compdata_aside.ps1; -Set stock = -Aside, -Set eerste = -Restore, -Status = -Status'
}

$snapAfter = LiveSnapshot
Check ($snapBefore -eq $snapAfter) 'live mods\legacy compdata md5s unchanged by the whole self-test'
Write-Host ('SELF-TEST {0} ({1} cases, {2} failed)' -f $(if ($script:fails) { 'FAILED' } else { 'PASSED' }), $script:cases, $script:fails)
if ($script:fails) { exit 1 } else { exit 0 }
