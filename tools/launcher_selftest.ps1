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
  $r = Run $Prof (PArgs $s @('-Set', 'stock') 'NoSuchProcA,powershell' 'none')
  Check ($r.code -eq 10 -and (CountDir $s.aside) -eq 0) 'a comma-joined list is split (NoSuchProcA,powershell): the second name still refuses -> exit 10'
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

if ($Section -in 'L2', 'All') {
  Write-Host '== L2 turbo_launch.ps1'
  $realGui = 'C:\FC 27 Live Editor\turbo_output\gui_settings.json'
  $realLaunchJson = 'C:\FC 27 Live Editor\turbo_output\launch_profile.json'
  $realLnk = Join-Path $env:USERPROFILE 'OneDrive\Desktop\LaunchFC27.lnk'
  $guiMd5 = Md5 $realGui
  $lnkMd5 = $(if (Test-Path -LiteralPath $realLnk) { Md5 $realLnk } else { 'ABSENT' })
  $ljExisted = Test-Path -LiteralPath $realLaunchJson
  $taskInfo = Get-ScheduledTaskInfo -TaskName 'LaunchFC27' -ErrorAction SilentlyContinue
  $lastRun = $(if ($taskInfo) { $taskInfo.LastRunTime.ToString('o') } else { 'none' })

  function New-L2([string]$name) {
    $s = New-Scratch $name
    $out = Join-Path $s.dir 'turbo_output'; New-Item -ItemType Directory -Force -Path $out | Out-Null
    Copy-Item -LiteralPath $realGui -Destination (Join-Path $out 'gui_settings.json')
    $s.out = $out; $s.gui = (Join-Path $out 'gui_settings.json'); $s.lj = (Join-Path $out 'launch_profile.json'); $s.bk = (Join-Path $s.dir 'backups')
    $s
  }
  function LArgs($s, [string[]]$more) {
    @('-NoGui', '-NoStart', '-TaskName', $NoTask, '-OutputDir', $s.out, '-BackupRoot', $s.bk, '-LiveDir', $s.live, '-AsideDir', $s.aside, '-ProcessNames', 'none', '-TaskNames', 'none') + $more
  }
  function Get-Json([string]$p) { Get-Content -LiteralPath $p -Raw | ConvertFrom-Json }
  $n = $rec.compdata.files.Count

  # --- England: stock + hint
  $s = New-L2 'l2_england'
  $origGuiText = Get-Content -LiteralPath $s.gui -Raw
  $r = Run $Launch (LArgs $s @('-Country', 'England'))
  Check ($r.code -eq 0) "-Country England -NoGui -NoStart: exit 0 ($($r.code))"
  Check ($r.out -match ('would run schtasks /run /tn ' + $NoTask)) 'prints the final step "would run schtasks /run /tn <TaskName>" and does not run it'
  Check ((CountDir $s.live) -eq 0 -and (CountDir $s.aside) -eq $n) 'England = compdata stock: 11 files aside'
  $lj = Get-Json $s.lj
  Check ($lj.country -eq 'England' -and $lj.compdata -eq 'stock' -and $lj.language -eq 'eng_us' -and $lj.set_at -match '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$' -and $lj.pack_state -and $lj.backup_state) 'launch_profile.json {country, set_at, compdata, language, pack_state, backup_state}'
  Write-Host ('        pack_state = ' + $lj.pack_state + ' | ' + $lj.backup_state)
  $g = Get-Json $s.gui
  Check ($g.callnames.launcher_language -eq 'eng_us' -and $g.callnames.launcher_stamp -eq $lj.set_at) 'gui_settings.json: callnames.launcher_language = eng_us, launcher_stamp = set_at'
  Check ($g.callnames.override_language -eq '' -and $g.callnames.bank_team -eq '*') 'override_language and bank_team untouched'
  $orig = $origGuiText | ConvertFrom-Json
  $g.callnames.PSObject.Properties.Remove('launcher_language'); $g.callnames.PSObject.Properties.Remove('launcher_stamp')
  Check ((($orig | ConvertTo-Json -Depth 32 -Compress) -eq ($g | ConvertTo-Json -Depth 32 -Compress))) 'gui_settings.json deep-equal to the original except the two new keys (depth 32)'
  $newText = Get-Content -LiteralPath $s.gui -Raw
  $m1 = [regex]::Match($origGuiText, '"callnames"\s*:\s*\{[^{}]*\}'); $m2 = [regex]::Match($newText, '"callnames"\s*:\s*\{[^{}]*\}')
  Check ($origGuiText.Substring(0, $m1.Index) -eq $newText.Substring(0, $m2.Index) -and $origGuiText.Substring($m1.Index + $m1.Length) -eq $newText.Substring($m2.Index + $m2.Length)) 'textual patch: every byte outside the callnames object is identical'

  # --- Netherlands: eerste, runs twice (hint keys appear once), then no-country = last pick
  $r = Run $Launch (LArgs $s @('-Country', 'Netherlands'))
  Check ($r.code -eq 0 -and (CountDir $s.live) -eq $n -and (CountDir $s.aside) -eq 0) 'Netherlands = compdata eerste: 11 files live again'
  $lj = Get-Json $s.lj
  Check ($lj.compdata -eq 'eerste' -and $lj.language -eq 'dut_nl' -and $lj.pack_state -match '^dut_nl (full present \(\d+ cas, .+\), launch subset .+|not installed in Data\\Win32)$') 'launch_profile.json for Netherlands: eerste, dut_nl, pack_state in the reported format'
  Write-Host ('        pack_state = ' + $lj.pack_state + ' | ' + $lj.backup_state)
  $txt = Get-Content -LiteralPath $s.gui -Raw
  Check (([regex]::Matches($txt, 'launcher_language')).Count -eq 1 -and ([regex]::Matches($txt, 'launcher_stamp')).Count -eq 1 -and $txt -match '"launcher_language": "dut_nl"') 'second run replaces the hint keys (each appears once), now dut_nl'
  $r = Run $Launch (LArgs $s @())
  Check ($r.code -eq 0 -and $r.out -match 'country = Netherlands') '-NoGui without -Country uses the last pick (Netherlands)'
  $r = Run $Launch (LArgs $s @('-SelfTestPicker'))
  Check ($r.code -eq 0 -and $r.out -match 'picker built: 5 radio buttons, preselected Netherlands, buttons Start FC 27/Cancel') 'picker form builds (not shown): 5 radio buttons, last pick Netherlands preselected, Start/Cancel'

  # --- WhatIf changes nothing
  $h0 = (Md5 $s.gui) + (Md5 $s.lj)
  $r = Run $Launch (LArgs $s @('-Country', 'Italy', '-WhatIf'))
  Check ($r.code -eq 0 -and $r.out -match 'STEP 3' -and $r.out -match 'would move live -> aside' -and ((Md5 $s.gui) + (Md5 $s.lj)) -eq $h0 -and (CountDir $s.live) -eq $n) '-WhatIf: prints the steps, exit 0, no file moved or written'
  Check ($r.out -match ('would run schtasks /run /tn ' + $NoTask)) '-WhatIf prints "would run schtasks /run /tn <TaskName>"'

  # --- error paths: profile untouched, the final step is still announced, exit 30
  $h0 = (Md5 $s.gui) + (Md5 $s.lj)
  $r = Run $Launch (LArgs $s @('-Country', 'England', '-RecordPath', (Join-Path $Scratch 'no_such_record.json')))
  Check ($r.code -eq 30 -and $r.out -match 'ERROR: launcher step failed' -and $r.out -match ('would run schtasks /run /tn ' + $NoTask)) 'forced error (missing record): message printed, exit 30, final "would run schtasks" step still printed'
  Check ((CountDir $s.live) -eq $n -and (CountDir $s.aside) -eq 0 -and ((Md5 $s.gui) + (Md5 $s.lj)) -eq $h0) 'forced error: profile and state files untouched'
  $r = Run $Launch (LArgs $s @('-Country', 'England', '-WhatIf', '-RecordPath', (Join-Path $Scratch 'no_such_record.json')))
  Check ($r.code -eq 30 -and $r.out -match ('would run schtasks /run /tn ' + $NoTask)) 'forced error under -WhatIf: still prints the would-run step'
  $r = Run $Launch (LArgs $s @('-Country', 'Atlantis'))
  Check ($r.code -eq 30 -and $r.out -match 'unknown country' -and ((Md5 $s.gui) + (Md5 $s.lj)) -eq $h0) 'unknown country: refused, exit 30, nothing written'
  $a2 = @('-NoGui', '-NoStart', '-TaskName', $NoTask, '-OutputDir', $s.out, '-BackupRoot', $s.bk, '-LiveDir', $s.live, '-AsideDir', $s.aside, '-ProcessNames', 'powershell', '-TaskNames', 'none', '-Country', 'Italy')
  $r = Run $Launch $a2
  Check ($r.code -eq 30 -and $r.out -match 'running' -and (CountDir $s.live) -eq $n) 'a running guarded process: profile step refuses, exit 30, nothing moved'

  # --- override_language is never written; hint patch edge cases on synthetic gui_settings
  $variants = @(
    @('empty_callnames', "{`r`n  `"callnames`": {},`r`n  `"x`": 1`r`n}`r`n"),
    @('override_set', "{`n  `"callnames`": {`n    `"override_language`": `"ita_it`"`n  }`n}`n"),
    @('hint_first', "{`n  `"callnames`": {`n    `"launcher_language`": `"old`",`n    `"launcher_stamp`": `"2000-01-01T00:00:00`",`n    `"bank_team`": `"*`"`n  }`n}`n"),
    @('hint_last', "{`n  `"callnames`": {`n    `"bank_team`": `"*`",`n    `"launcher_language`": `"old`",`n    `"launcher_stamp`": `"2000-01-01T00:00:00`"`n  }`n}`n"),
    @('no_callnames', "{`n  `"gui`": { `"a`": 1 }`n}`n"))
  foreach ($v in $variants) {
    $sv = New-L2 ('l2_var_' + $v[0])
    [IO.File]::WriteAllText($sv.gui, $v[1], (New-Object Text.UTF8Encoding $false))
    $r = Run $Launch (LArgs $sv @('-Country', 'Spain'))
    $t = [IO.File]::ReadAllText($sv.gui)
    if ($v[0] -eq 'no_callnames') {
      Check ($r.code -eq 0 -and $t -eq $v[1] -and $r.out -match 'hint not written') 'no callnames object: file untouched, "hint not written", launch still fine (exit 0)'
    } else {
      $j = $null; try { $j = $t | ConvertFrom-Json } catch { }
      $okv = $j -and $r.code -eq 0 -and $j.callnames.launcher_language -eq 'spa_es' -and ([regex]::Matches($t, 'launcher_language')).Count -eq 1
      if ($v[0] -eq 'override_set') { $okv = $okv -and $j.callnames.override_language -eq 'ita_it' }
      if ($v[0] -eq 'empty_callnames') { $okv = $okv -and $j.x -eq 1 -and $t.Contains("`r`n") -and -not ($t -match "[^\r]\n") }
      if ($v[0].StartsWith('hint_')) { $okv = $okv -and $j.callnames.bank_team -eq '*' }
      Check $okv ('hint patch edge case ' + $v[0] + ': valid JSON, one hint pair, other keys and line endings kept')
    }
  }

  # --- -Status is read only
  $r = Run $Launch @('-Status', '-TaskName', $NoTask, '-OutputDir', $s.out, '-BackupRoot', $s.bk, '-LiveDir', $s.live, '-AsideDir', $s.aside, '-LnkPath', (Join-Path $Scratch 'nolnk.lnk'))
  Check ($r.code -eq 0 -and $r.out -match '== compdata profile' -and $r.out -match 'PROFILE: eerste' -and $r.out -match '== commentary' -and $r.out -match 'dut_nl full present' -and $r.out -match 'backup') '-Status: compdata profile, last pick, commentary (Data\Win32 + backup store), hint keys, shortcut, tasks'

  # --- .lnk -Install / -Undo round trip on a scratch shortcut
  $sl = New-L2 'l2_lnk'
  $lnk = Join-Path $sl.dir 'Scratch.lnk'
  $sh = New-Object -ComObject WScript.Shell
  $c = $sh.CreateShortcut($lnk); $c.TargetPath = Join-Path $env:SystemRoot 'System32\notepad.exe'; $c.Arguments = '"a b.txt"'; $c.WorkingDirectory = $env:TEMP; $c.Description = 'scratch'; $c.Save()
  $md5Orig = Md5 $lnk
  $bkArgs = @('-TaskName', $NoTask, '-OutputDir', $sl.out, '-BackupRoot', $sl.bk)
  $r = Run $Launch ($bkArgs + @('-Install'))
  Check ($r.code -eq 40 -and $r.out -match 'explicit -LnkPath') '-Install without -LnkPath: refused (40)'
  $r = Run $Launch ($bkArgs + @('-Install', '-LnkPath', $realLnk, '-WhatIf'))
  Check ($r.code -eq 40 -and $r.out -match 'real desktop shortcut') '-Install on the real desktop shortcut without -CassioOk: refused (40), even under -WhatIf'
  $r = Run $Launch ($bkArgs + @('-Undo', '-LnkPath', $lnk))
  Check ($r.code -eq 40 -and $r.out -match 'no launcher_') '-Undo with no backup: refused (40)'
  $r = Run $Launch ($bkArgs + @('-Install', '-LnkPath', $lnk, '-WhatIf'))
  Check ($r.code -eq 0 -and (Md5 $lnk) -eq $md5Orig -and -not (Test-Path $sl.bk)) '-Install -WhatIf: exit 0, scratch .lnk unchanged, no backup folder'
  $r = Run $Launch ($bkArgs + @('-Install', '-LnkPath', $lnk))
  $info = $sh.CreateShortcut($lnk)
  Check ($r.code -eq 0 -and $info.TargetPath -match 'powershell\.exe$' -and $info.Arguments -match 'turbo_launch\.ps1' -and $info.Arguments -match '-STA' -and (Md5 $lnk) -ne $md5Orig) '-Install on the scratch .lnk: re-pointed at powershell.exe ... turbo_launch.ps1'
  $bd = @(Get-ChildItem -LiteralPath $sl.bk -Directory -Filter 'launcher_*')
  Check ($bd.Count -eq 1 -and (Test-Path (Join-Path $bd[0].FullName 'manifest.json')) -and (Md5 (Join-Path $bd[0].FullName 'Scratch.lnk')) -eq $md5Orig) 'backup folder launcher_<stamp> holds a byte copy + manifest.json with the md5'
  $r = Run $Launch ($bkArgs + @('-Undo', '-LnkPath', $lnk))
  Check ($r.code -eq 0 -and (Md5 $lnk) -eq $md5Orig -and $r.out -match 'byte-equal') '-Undo: scratch .lnk byte-equal to the original (md5 round trip)'
  $info = $sh.CreateShortcut($lnk)
  Check ($info.TargetPath -match 'notepad\.exe$' -and $info.Arguments -eq '"a b.txt"') 'after the undo the target and arguments are the original ones'
  [IO.File]::WriteAllText((Join-Path $bd[0].FullName 'Scratch.lnk'), 'tampered')
  $r = Run $Launch ($bkArgs + @('-Undo', '-LnkPath', $lnk))
  Check ($r.code -eq 40 -and $r.out -match 'md5 differs') '-Undo with a tampered backup: refused (40), nothing copied'

  # --- nothing live was touched
  Check ((Md5 $realGui) -eq $guiMd5) 'live turbo_output\gui_settings.json md5 unchanged'
  Check ((Test-Path -LiteralPath $realLaunchJson) -eq $ljExisted) 'live turbo_output\launch_profile.json not created'
  Check ($(if (Test-Path -LiteralPath $realLnk) { Md5 $realLnk } else { 'ABSENT' }) -eq $lnkMd5) 'Cassio''s real desktop shortcut md5 unchanged'
  $taskInfo2 = Get-ScheduledTaskInfo -TaskName 'LaunchFC27' -ErrorAction SilentlyContinue
  Check (($(if ($taskInfo2) { $taskInfo2.LastRunTime.ToString('o') } else { 'none' })) -eq $lastRun) 'the real launcher task was never run (LastRunTime unchanged)'
}

$snapAfter = LiveSnapshot
Check ($snapBefore -eq $snapAfter) 'live mods\legacy compdata md5s unchanged by the whole self-test'
Write-Host ('SELF-TEST {0} ({1} cases, {2} failed)' -f $(if ($script:fails) { 'FAILED' } else { 'PASSED' }), $script:cases, $script:fails)
if ($script:fails) { exit 1 } else { exit 0 }
