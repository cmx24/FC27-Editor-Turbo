# launcher_selftest.ps1 [-Section L1|L2|L3|All] [-Scratch <dir>]   (Windows PowerShell 5.1, ASCII only)
# Offline self-test of compdata_profile.ps1 (L1), turbo_launch.ps1 (L2) and, phase 2 (L3), launch_profile_apply.ps1 + register_fc27_profile_task.ps1 +
# the launcher's STEP 4b on a scratch game tree and a scratch registry key HKCU:\Software\TurboTest\... (the real HKLM is only READ, before and after).
# Scratch copies for L1/L2. It never moves a live file, never starts
# the game, never runs a scheduled task: the scripts under test are only given scratch paths, an unknown task name and -NoStart / -WhatIf.
# Guard: the live mods\legacy compdata md5s are read (not written) before and after and must be identical.
# Prints one line per case and "SELF-TEST PASSED|FAILED (n cases, m failed)". Exit 0 = pass.
param(
  [ValidateSet('L1', 'L2', 'L3', 'All')][string]$Section = 'All',
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
    @('-NoGui', '-NoStart', '-NoLocale', '-TaskName', $NoTask, '-ProfileTaskName', $NoTask, '-OutputDir', $s.out, '-BackupRoot', $s.bk, '-LiveDir', $s.live, '-AsideDir', $s.aside, '-ProcessNames', 'none', '-TaskNames', 'none') + $more
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
  $orig.callnames.PSObject.Properties.Remove('launcher_language'); $orig.callnames.PSObject.Properties.Remove('launcher_stamp')   # the live file may already hold the pick's hint keys
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
  $a2 = @('-NoGui', '-NoStart', '-NoLocale', '-TaskName', $NoTask, '-ProfileTaskName', $NoTask, '-OutputDir', $s.out, '-BackupRoot', $s.bk, '-LiveDir', $s.live, '-AsideDir', $s.aside, '-ProcessNames', 'powershell', '-TaskNames', 'none', '-Country', 'Italy')
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
  $r = Run $Launch @('-Status', '-TaskName', $NoTask, '-ProfileTaskName', $NoTask, '-OutputDir', $s.out, '-BackupRoot', $s.bk, '-LiveDir', $s.live, '-AsideDir', $s.aside, '-LnkPath', (Join-Path $Scratch 'nolnk.lnk'))
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

if ($Section -in 'L3', 'All') {
  Write-Host '== L3 phase 2: launch_profile_apply.ps1, register_fc27_profile_task.ps1, turbo_launch.ps1 STEP 4b / -Undo -Locale (scratch tree + scratch HKCU key only)'
  $Apply = Join-Path $Tools 'launch_profile_apply.ps1'
  $Reg = Join-Path $Tools 'register_fc27_profile_task.ps1'
  $realGame = 'C:\Program Files\EA Games\EA SPORTS FC 27'
  $realOut = 'C:\FC 27 Live Editor\turbo_output'
  $regSub = 'SOFTWARE\EA Sports\EA SPORTS FC 27'
  function HklmLocale([string]$view) {
    $rv = $(if ($view -eq 'view64') { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 })
    $b = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $rv)
    $k = $b.OpenSubKey($regSub, $false); $v = '(absent)'
    if ($k) { $x = $k.GetValue('Locale', $null); $v = $(if ($null -eq $x) { '(none)' } else { [string]$x }); $k.Dispose() }
    $b.Dispose(); $v
  }
  function RealSnap {
    $w = Join-Path $realGame 'Data\Win32'; $c = Join-Path $realGame 'commentary'
    $l1 = (Get-ChildItem -LiteralPath $w -Recurse -File -ErrorAction SilentlyContinue | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($w.Length) + ':' + $_.Length }) -join '|'
    $l2 = (Get-ChildItem -LiteralPath $c -Recurse -File -ErrorAction SilentlyContinue | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($c.Length) + ':' + $_.Length }) -join '|'
    $f = @('launch_profile_request.json', 'launch_profile_result.json', 'launch_profile_state.json', 'launch_profile_apply.log') | ForEach-Object { $p = Join-Path $realOut $_; if (Test-Path -LiteralPath $p) { Md5 $p } else { '-' } }
    $t = $(if (Get-ScheduledTask -TaskName 'FC27_Profile' -ErrorAction SilentlyContinue) { 'task' } else { 'notask' })
    (HklmLocale 'view64') + ';' + (HklmLocale 'view32') + ';' + (Md5Str $l1) + ';' + (Md5Str $l2) + ';' + ($f -join ',') + ';' + $t
  }
  function Md5Str([string]$s) { $m = [Security.Cryptography.MD5]::Create(); ([BitConverter]::ToString($m.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)))).Replace('-', '').ToLower() }
  $realBefore = RealSnap
  $realGui = 'C:\FC 27 Live Editor\turbo_output\gui_settings.json'
  function Get-Json([string]$p) { Get-Content -LiteralPath $p -Raw | ConvertFrom-Json }
  function New-L2([string]$name) {
    $s = New-Scratch $name
    $out = Join-Path $s.dir 'turbo_output'; New-Item -ItemType Directory -Force -Path $out | Out-Null
    Copy-Item -LiteralPath $realGui -Destination (Join-Path $out 'gui_settings.json')
    $s.out = $out; $s.gui = (Join-Path $out 'gui_settings.json'); $s.lj = (Join-Path $out 'launch_profile.json'); $s.bk = (Join-Path $s.dir 'backups')
    $s
  }
  $guiMd5L3 = Md5 $realGui
  Write-Host ('real HKLM Locale (read-only look): ' + (HklmLocale 'view64') + ' / ' + (HklmLocale 'view32'))

  $regRootName = 'selftest_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
  $regRootAll = 'HKCU:\Software\TurboTest\' + $regRootName
  $rnd = New-Object Random 27
  function Rand-File([string]$p, [int]$n) { New-Item -ItemType Directory -Force -Path (Split-Path -Parent $p) | Out-Null; $b = New-Object byte[] $n; $rnd.NextBytes($b); [IO.File]::WriteAllBytes($p, $b) }
  # a scratch game: Data\Win32 with the live packs named in $live, commentary\ with the backup packs named in $bak
  function Add-Pack([string]$toc, [string]$casRoot, [string]$lang, [int]$tocSize = 6000) {
    Rand-File (Join-Path $toc ('commentaryfull_' + $lang + '.toc')) $tocSize
    Rand-File (Join-Path $toc ('commentarylaunch_' + $lang + '.toc')) 900
    Rand-File (Join-Path $casRoot ('commentaryfull_' + $lang + '\cas_01.cas')) 3000
    Rand-File (Join-Path $casRoot ('commentaryfull_' + $lang + '\cas_02.cas')) 2000
    Rand-File (Join-Path $casRoot ('commentarylaunch_' + $lang + '\cas_01.cas')) 700
  }
  function New-P2([string]$name, [string[]]$live, [string[]]$bak, [bool]$with32 = $true) {
    $d = Join-Path $Scratch $name
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    $g = Join-Path $d 'game'; $w = Join-Path $g 'Data\Win32'; $b = Join-Path $g 'commentary'; $out = Join-Path $d 'turbo_output'
    New-Item -ItemType Directory -Force -Path $w, $b, $out | Out-Null
    foreach ($l in $live) { Add-Pack $w (Join-Path $w 'superbundlelayout') $l }
    foreach ($l in $bak) { Add-Pack $b $b $l }
    $root = $regRootAll + '\' + $name
    New-Item -Path ($root + '\view64') -Force | Out-Null; Set-ItemProperty -Path ($root + '\view64') -Name Locale -Value 'nl_NL'
    if ($with32) { New-Item -Path ($root + '\view32') -Force | Out-Null; Set-ItemProperty -Path ($root + '\view32') -Name Locale -Value 'nl_NL' }
    @{ dir = $d; game = $g; win32 = $w; bak = $b; out = $out; reg = $root; req = (Join-Path $out 'launch_profile_request.json'); res = (Join-Path $out 'launch_profile_result.json'); st = (Join-Path $out 'launch_profile_state.json') }
  }
  function RegGet($s, [string]$view) { $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($s.reg.Substring('HKCU:\'.Length) + '\' + $view); if (-not $k) { return '(absent)' }; $v = [string]$k.GetValue('Locale'); $k.Dispose(); $v }
  function RegSet($s, [string]$view, [string]$v) { Set-ItemProperty -Path ($s.reg + '\' + $view) -Name Locale -Value $v }
  function Both($s) { (RegGet $s 'view64') + '/' + (RegGet $s 'view32') }
  function Manifest([string]$dir) { (Get-ChildItem -LiteralPath $dir -Recurse -File | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($dir.Length) + '=' + (Md5 $_.FullName) }) -join ';' }
  $script:rid = 0
  function Write-Req($s, [string]$action, [string]$lang, [string]$loc, [string]$id = '', [int]$ageSec = 0, [string]$raw = '') {
    $script:rid++
    if (-not $id) { $id = 'st' + (Get-Date -Format 'HHmmss') + '-' + $script:rid }
    if ($raw) { [IO.File]::WriteAllText($s.req, $raw); return $id }
    $o = [ordered]@{ version = 1; action = $action; language = $lang; locale = $loc; request_id = $id; requested_at = (Get-Date).AddSeconds(-$ageSec).ToString('yyyy-MM-ddTHH:mm:ss') }
    [IO.File]::WriteAllText($s.req, (($o | ConvertTo-Json) + "`n"), (New-Object Text.UTF8Encoding $false))
    $id
  }
  function Do-Apply($s, [string[]]$more = @()) {
    $r = Run $Apply (@('-OutputDir', $s.out, '-GameDir', $s.game, '-RegRoot', $s.reg, '-ProcessNames', 'none') + $more)
    $r.res = $(if (Test-Path -LiteralPath $s.res) { Get-Json $s.res } else { $null })
    $r.state = $(if (Test-Path -LiteralPath $s.st) { Get-Json $s.st } else { $null })
    $r
  }
  function Get-Json([string]$p) { Get-Content -LiteralPath $p -Raw | ConvertFrom-Json }

  # --- set / undo round trip, both views
  $s = New-P2 'p2_round' @('ita_it', 'spa_es') @()
  [void](Write-Req $s 'set' 'ita_it' 'it_IT')
  $r = Do-Apply $s
  Check ($r.code -eq 0 -and (Both $s) -eq 'it_IT/it_IT') 'set ita_it/it_IT: exit 0, both views it_IT (read back)'
  Check ($r.res.ok -eq $true -and $r.res.pack_state -eq 'present' -and @($r.res.copies).Count -eq 0 -and $r.res.locale_before.view64 -eq 'nl_NL' -and $r.res.locale_after.view32 -eq 'it_IT') 'result json: ok, pack present, no copies, locale_before nl_NL, locale_after it_IT'
  Check ($r.state.set.locale -eq 'it_IT' -and $r.state.set.language -eq 'ita_it' -and $r.state.prior.view64 -eq 'nl_NL' -and $r.state.prior.view32 -eq 'nl_NL' -and $r.state.set_at_epoch -gt 1700000000 -and $r.state.request_id) 'state json: set {it_IT, ita_it}, prior nl_NL/nl_NL, set_at_epoch, request_id'
  Check ((Get-Content -LiteralPath (Join-Path $s.out 'launch_profile_apply.log') -Raw) -match 'action=set step=set ok=True') 'apply log: one line per run'
  # a second set (es_ES) without an undo: the prior stays the real original
  [void](Write-Req $s 'set' 'spa_es' 'es_ES')
  $r = Do-Apply $s
  Check ($r.code -eq 0 -and (Both $s) -eq 'es_ES/es_ES' -and $r.state.prior.view64 -eq 'nl_NL' -and $r.state.set.locale -eq 'es_ES') 'set es_ES after it_IT: prior is still nl_NL (not it_IT)'
  [void](Write-Req $s 'undo' '' '')
  $r = Do-Apply $s
  Check ($r.code -eq 0 -and (Both $s) -eq 'nl_NL/nl_NL' -and $null -eq $r.state.set -and $r.state.prior.view64 -eq 'nl_NL' -and $r.res.step -eq 'undo') 'undo: nl_NL/nl_NL restored (read back equal), state set cleared, prior kept'
  [void](Write-Req $s 'undo' '' '')
  $r = Do-Apply $s
  Check ($r.code -eq 1 -and $r.res.message -match 'nothing to undo' -and (Both $s) -eq 'nl_NL/nl_NL') 'undo with no active set: refused (exit 1), registry unchanged'
  # the registry changed behind our back while a set is active: that value becomes the new prior
  [void](Write-Req $s 'set' 'ita_it' 'it_IT'); $r = Do-Apply $s
  RegSet $s 'view64' 'fr_FR'; RegSet $s 'view32' 'fr_FR'
  [void](Write-Req $s 'set' 'spa_es' 'es_ES'); $r = Do-Apply $s
  Check ($r.code -eq 0 -and $r.state.prior.view64 -eq 'fr_FR' -and $r.state.prior.view32 -eq 'fr_FR') 'a changed Locale under an active set (EA app rewrote it) becomes the new prior'
  [void](Write-Req $s 'undo' '' ''); $r = Do-Apply $s
  Check ((Both $s) -eq 'fr_FR/fr_FR') 'undo then restores that prior (fr_FR/fr_FR)'

  # --- the 32-bit view only when its key exists
  $s = New-P2 'p2_no32' @('ita_it') @() $false
  [void](Write-Req $s 'set' 'ita_it' 'it_IT'); $r = Do-Apply $s
  Check ($r.code -eq 0 -and (RegGet $s 'view64') -eq 'it_IT' -and (RegGet $s 'view32') -eq '(absent)' -and $null -eq $r.state.prior.view32) 'no 32-bit key: only the 64-bit view is written, the 32-bit key is not created, prior.view32 null'
  [void](Write-Req $s 'undo' '' ''); $r = Do-Apply $s
  Check ($r.code -eq 0 -and (RegGet $s 'view64') -eq 'nl_NL' -and (RegGet $s 'view32') -eq '(absent)') 'undo without a 32-bit key: 64-bit restored, still no 32-bit key'
  $s = New-P2 'p2_eng' @() @()
  [void](Write-Req $s 'set' 'eng_us' 'en_US'); $r = Do-Apply $s
  Check ($r.code -eq 0 -and (Both $s) -eq 'en_US/en_US' -and $r.res.pack_state -eq 'base') 'eng_us / en_US: base game, no pack test, no copy'

  # --- refusals: registry and trees untouched
  $s = New-P2 'p2_refuse' @('ita_it') @('fre_fr')
  $m0 = Manifest $s.dir
  [void](Write-Req $s 'set' 'xyz_zz' 'it_IT'); $r = Do-Apply $s
  Check ($r.code -eq 2 -and $r.res.message -match 'unknown language' -and (Both $s) -eq 'nl_NL/nl_NL') 'unknown language: bad request (exit 2), registry unchanged'
  [void](Write-Req $s 'set' 'ita_it' 'nl_NL'); $r = Do-Apply $s
  Check ($r.code -eq 2 -and $r.res.message -match 'not the table locale' -and (Both $s) -eq 'nl_NL/nl_NL') 'locale that is not the table locale of the language: exit 2'
  [void](Write-Req $s 'set' 'ita_it' 'xx_XX'); $r = Do-Apply $s
  Check ($r.code -eq 2 -and (Both $s) -eq 'nl_NL/nl_NL') 'locale not in the table at all: exit 2'
  [void](Write-Req $s 'set' 'ita_it' 'it_it'); $r = Do-Apply $s
  Check ($r.code -eq 2) 'locale compared case-sensitively (it_it refused): exit 2'
  [void](Write-Req $s 'set' '' '' '' 0 'not json {'); $r = Do-Apply $s
  Check ($r.code -eq 2 -and $r.res.message -match 'not valid JSON') 'a request that is not JSON: exit 2'
  [void](Write-Req $s 'wipe' 'ita_it' 'it_IT'); $r = Do-Apply $s
  Check ($r.code -eq 2 -and $r.res.message -match 'action must be') 'an action other than set/undo: exit 2'
  $id = Write-Req $s 'set' 'ita_it' 'it_IT'; $r = Do-Apply $s @('-ExpectRequestId', 'someone-else')
  Check ($r.code -eq 1 -and $r.res.message -match 'request_id mismatch' -and (Both $s) -eq 'nl_NL/nl_NL') 'request_id mismatch: refused (exit 1)'
  [void](Write-Req $s 'set' 'ita_it' 'it_IT' '' 3600); $r = Do-Apply $s
  Check ($r.code -eq 1 -and $r.res.message -match 'stale request' -and (Both $s) -eq 'nl_NL/nl_NL') 'stale request (an hour old): refused (exit 1)'
  [void](Write-Req $s 'set' 'ita_it' 'it_IT' '' -3600); $r = Do-Apply $s
  Check ($r.code -eq 1 -and $r.res.message -match 'future') 'a request dated an hour ahead: refused (exit 1)'
  [void](Write-Req $s 'set' 'ita_it' 'it_IT'); $r = Run $Apply @('-OutputDir', $s.out, '-GameDir', $s.game, '-RegRoot', $s.reg, '-ProcessNames', 'powershell')
  Check ($r.code -eq 1 -and (Get-Json $s.res).message -match 'refused while running' -and (Both $s) -eq 'nl_NL/nl_NL') 'FC27 / launcher running (mocked process list): refused (exit 1), registry unchanged'
  $id = Write-Req $s 'set' 'ita_it' 'it_IT' 'replay-1'; $r = Do-Apply $s
  Check ($r.code -eq 0 -and (Both $s) -eq 'it_IT/it_IT') 'a fresh request is applied'
  RegSet $s 'view64' 'nl_NL'; RegSet $s 'view32' 'nl_NL'
  $id = Write-Req $s 'set' 'ita_it' 'it_IT' 'replay-1'; $r = Do-Apply $s
  Check ($r.code -eq 1 -and $r.out -match 'already processed' -and (Both $s) -eq 'nl_NL/nl_NL') 'the same request_id again (replay): refused, registry unchanged'
  $r = Run $Apply @('-OutputDir', $s.out, '-GameDir', $s.game, '-RegRoot', 'HKCU:\Software\NotTheScratchKey', '-ProcessNames', 'none')
  Check ($r.code -eq 2 -and $r.out -match 'must be HKCU:\\Software\\TurboTest') '-RegRoot outside HKCU:\Software\TurboTest is refused (exit 2)'
  $r = Run $Apply @('-OutputDir', $s.out, '-GameDir', $s.game, '-RegRoot', 'HKLM:\SOFTWARE\EA Sports\EA SPORTS FC 27', '-ProcessNames', 'none')
  Check ($r.code -eq 2) '-RegRoot pointing at HKLM is refused (exit 2)'
  $gameMan = Manifest $s.game
  [void](Write-Req $s 'set' 'ita_it' 'it_IT'); $r = Do-Apply $s
  Check ((Manifest $s.game) -eq $gameMan) 'a set with the pack present changes nothing in the game tree (md5 manifest)'

  # --- pack cases
  $s = New-P2 'p2_copy' @('ita_it') @('fre_fr')
  $manW = Manifest $s.win32; $manB = Manifest $s.bak
  [void](Write-Req $s 'set' 'fre_fr' 'fr_FR'); $r = Do-Apply $s
  $after = Manifest $s.win32
  Check ($r.code -eq 0 -and $r.res.pack_state -eq 'copied' -and (Both $s) -eq 'fr_FR/fr_FR' -and @($r.res.copies).Count -eq 5) 'absent + backup present: copied (5 files: 2 tocs + 3 cas), Locale set'
  $bad = 0
  foreach ($c in @($r.res.copies)) { $dst = Join-Path $s.win32 $c.rel; $src = $(if ($c.rel -like 'superbundlelayout\*') { Join-Path $s.bak ($c.rel -replace '^superbundlelayout\\', '') } else { Join-Path $s.bak $c.rel }); if (-not (Test-Path -LiteralPath $dst) -or (Md5 $dst) -ne $c.md5 -or (Md5 $src) -ne $c.md5) { $bad++ } }
  Check ($bad -eq 0) 'every copied file md5 equals its source and the md5 recorded in the result'
  Check (@(Get-ChildItem -LiteralPath $s.win32 -Recurse -Filter '*.turbo_tmp').Count -eq 0) 'no .turbo_tmp left behind (tmp renamed)'
  $added = @(($after -split ';') | Where-Object { ($manW -split ';') -notcontains $_ })
  $removed = @(($manW -split ';') | Where-Object { ($after -split ';') -notcontains $_ })
  Check ($added.Count -eq 5 -and $removed.Count -eq 0 -and (Manifest $s.bak) -eq $manB) 'tree manifest before/after: exactly 5 files added, none changed or removed in Data\Win32, backup store unchanged'
  Check (@($r.state.copies).Count -eq 5) 'state json records the copies {rel, bytes, md5}'
  # pack absent on both sides
  $s = New-P2 'p2_absent' @('ita_it') @()
  $m0 = Manifest $s.game
  [void](Write-Req $s 'set' 'ger_de' 'de_DE'); $r = Do-Apply $s
  Check ($r.code -eq 0 -and $r.res.pack_state -eq 'pack_absent' -and $r.res.ok -eq $false -and (Both $s) -eq 'nl_NL/nl_NL' -and $null -eq $r.state) 'absent + backup absent: pack_absent, exit 0, Locale unchanged, no state written'
  Check ((Manifest $s.game) -eq $m0) 'pack_absent: game tree unchanged'
  # an existing non-empty target (a stub toc) is never overwritten
  $s = New-P2 'p2_stub' @('ita_it') @('por_br')
  Rand-File (Join-Path $s.win32 'commentaryfull_por_br.toc') 1000
  $m0 = Manifest $s.game
  [void](Write-Req $s 'set' 'por_br' 'pt_BR'); $r = Do-Apply $s
  Check ($r.code -eq 1 -and $r.res.pack_state -eq 'copy_refused' -and $r.res.message -match 'never overwriting' -and (Both $s) -eq 'nl_NL/nl_NL') 'existing non-empty (stub) target: copy refused (exit 1), Locale unchanged'
  Check ((Manifest $s.game) -eq $m0) 'copy refused: nothing in the game tree changed (pre-flight, nothing half-copied)'
  # a leftover tmp from an earlier half copy is reported and never touched
  $s = New-P2 'p2_tmp' @() @('fre_fr')
  Rand-File (Join-Path $s.win32 'commentaryfull_fre_fr.toc.turbo_tmp') 50
  $m0 = Manifest $s.game
  [void](Write-Req $s 'set' 'fre_fr' 'fr_FR'); $r = Do-Apply $s
  Check ($r.code -eq 1 -and $r.res.message -match 'leftover' -and (Manifest $s.game) -eq $m0 -and (Both $s) -eq 'nl_NL/nl_NL') 'a leftover .turbo_tmp: refused, left in place, nothing else copied'
  # resume: one file already identical in Data\Win32 is skipped, the rest copied
  $s = New-P2 'p2_resume' @() @('spa_es')
  Copy-Item -LiteralPath (Join-Path $s.bak 'commentarylaunch_spa_es.toc') -Destination (Join-Path $s.win32 'commentarylaunch_spa_es.toc')
  [void](Write-Req $s 'set' 'spa_es' 'es_ES'); $r = Do-Apply $s
  Check ($r.code -eq 0 -and $r.res.pack_state -eq 'copied' -and @($r.res.copies).Count -eq 4 -and (Both $s) -eq 'es_ES/es_ES') 'resume: an identical file already in place is skipped, the other 4 are copied'
  # disk space guard
  $s = New-P2 'p2_space' @() @('fre_fr')
  $m0 = Manifest $s.game
  [void](Write-Req $s 'set' 'fre_fr' 'fr_FR'); $r = Do-Apply $s @('-FreeMarginBytes', '999999999999999')
  Check ($r.code -eq 1 -and $r.res.message -match 'margin' -and (Manifest $s.game) -eq $m0) 'not enough free space: refused before the first copy'
  # table equals the json map (the guard's need: hint language and registry Locale come from one source)
  $txt = Get-Content -LiteralPath $Apply -Raw
  $tm = [regex]::Match($txt, '\$Table = \[ordered\]@\{([^}]*)\}').Groups[1].Value
  $tbl = @{}; foreach ($m in [regex]::Matches($tm, '(\w+)\s*=\s*''([A-Za-z_]+)''')) { $tbl[$m.Groups[1].Value] = $m.Groups[2].Value }
  $pj = Get-Content -LiteralPath (Join-Path $Tools 'launch_profiles.json') -Raw | ConvertFrom-Json
  $same = ($tbl.Count -eq 7)
  foreach ($k in $tbl.Keys) { if ([string]$pj.locales.$k -ne $tbl[$k]) { $same = $false } }
  Check ($same -and @($pj.locales.PSObject.Properties).Count -eq 7) 'launch_profiles.json "locales" equals the apply script''s fixed 7-row table'
  Check ($tbl['ita_it'] -eq 'it_IT' -and $tbl['dut_nl'] -eq 'nl_NL' -and $tbl['eng_us'] -eq 'en_US' -and $tbl['spa_es'] -eq 'es_ES' -and $tbl['fre_fr'] -eq 'fr_FR' -and $tbl['ger_de'] -eq 'de_DE' -and $tbl['por_br'] -eq 'pt_BR') 'the table rows are the exe''s (eng_us en_US, dut_nl nl_NL, ita_it it_IT, spa_es es_ES, fre_fr fr_FR, ger_de de_DE, por_br pt_BR)'

  # --- register_fc27_profile_task.ps1 -WhatIf prints the XML it would register
  $r = Run $Reg @('-WhatIf')
  Check ($r.code -eq 0 -and $r.out -match '<URI>\\FC27_Profile</URI>' -and $r.out -match 'IgnoreNew' -and $r.out -match 'PT10M' -and $r.out -match '<Triggers />' -and $r.out -match 'HighestAvailable' -and $r.out -match 'InteractiveToken' -and $r.out -match 'launch_profile_apply\.ps1&quot;' -and $r.out -match 'powershell\.exe') 'register script -WhatIf: prints the task XML (FC27_Profile, IgnoreNew, PT10M, no trigger, Highest, Interactive, the apply script) and registers nothing'
  Check (-not (Get-ScheduledTask -TaskName 'FC27_Profile' -ErrorAction SilentlyContinue) -or $realBefore -match ';task$') 'the task FC27_Profile exists only if it did before this self-test (never registered by it)'
  $r = Run $Reg @()
  $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if (-not $isAdmin) { Check ($r.code -eq 1 -and $r.out -match 'elevated') 'register script without elevation: refused (exit 1), never self-elevates' }
  else { Write-Host '  skip  register-without-elevation case (this shell is elevated; the script would register the real task)' }

  # --- the launcher: STEP 4b through a fake task (a job that runs the REAL apply script on the scratch tree the moment the request file appears)
  function Start-FakeTask($s) {
    if (Test-Path -LiteralPath $s.req) { Remove-Item -LiteralPath $s.req -Force }
    Start-Job -ArgumentList $Apply, $s.out, $s.game, $s.reg, $s.req -ScriptBlock {
      param($apply, $out, $game, $reg, $req)
      $end = (Get-Date).AddSeconds(90)
      while ((Get-Date) -lt $end) {
        if (Test-Path -LiteralPath $req) { Start-Sleep -Milliseconds 300; & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $apply -OutputDir $out -GameDir $game -RegRoot $reg -ProcessNames none | Out-Null; return }
        Start-Sleep -Milliseconds 200
      }
    }
  }
  function P4Args($s, $l2, [string[]]$more) {
    @('-NoGui', '-NoStart', '-TaskName', $NoTask, '-ProfileNoRun', '-ProfileTimeoutSec', '60', '-OutputDir', $l2.out, '-GameDir', $s.game, '-BackupRoot', $l2.bk, '-LiveDir', $l2.live, '-AsideDir', $l2.aside, '-ProcessNames', 'none', '-TaskNames', 'none') + $more
  }
  $s = New-P2 'p2_launch' @('ita_it', 'dut_nl') @()
  $l2 = New-L2 'p2_launch_l2'
  $s.out = $l2.out; $s.req = (Join-Path $l2.out 'launch_profile_request.json'); $s.res = (Join-Path $l2.out 'launch_profile_result.json'); $s.st = (Join-Path $l2.out 'launch_profile_state.json')
  $job = Start-FakeTask $s
  $r = Run $Launch (P4Args $s $l2 @('-Country', 'Italy'))
  Wait-Job $job -Timeout 30 | Out-Null; Remove-Job $job -Force
  Check ($r.code -eq 0 -and $r.out -match 'STEP 4b: Locale it_IT \(ita_it\)' -and $r.out -match 'result: ok=True pack=present' -and (Both $s) -eq 'it_IT/it_IT') 'launcher Italy: STEP 4b writes the request, the task applies it (scratch registry it_IT/it_IT), result printed, exit 0'
  $lj = Get-Json $l2.lj
  Check ($lj.locale_requested -eq 'it_IT' -and $lj.locale_result -eq 'ok present' -and $lj.language -eq 'ita_it' -and $lj.country -eq 'Italy') 'launch_profile.json gains locale_requested it_IT and locale_result "ok present" (other keys kept)'
  Check ((Get-Json $l2.gui).callnames.launcher_language -eq 'ita_it') 'the hint language (ita_it) is the language the Locale maps to'
  Check ($r.out -match ('would run schtasks /run /tn ' + $NoTask + ' \(-NoStart\)')) 'STEP 5 is still announced after STEP 4b'
  $stj = Get-Json $s.st
  Check ($stj.set.locale -eq 'it_IT' -and $stj.prior.view64 -eq 'nl_NL') 'state json written by the task: prior nl_NL, set it_IT'
  # -Undo -Locale
  $job = Start-FakeTask $s
  $r = Run $Launch (P4Args $s $l2 @('-Undo', '-Locale'))
  Wait-Job $job -Timeout 30 | Out-Null; Remove-Job $job -Force
  Check ($r.code -eq 0 -and $r.out -match 'LOCALE UNDO PASS' -and (Both $s) -eq 'nl_NL/nl_NL' -and $null -eq (Get-Json $s.st).set) '-Undo -Locale: the task restores nl_NL/nl_NL, state set cleared, exit 0'
  # -WhatIf: prints the request, runs nothing
  $h0 = (Both $s)
  if (Test-Path -LiteralPath $s.req) { Remove-Item -LiteralPath $s.req }
  $r = Run $Launch (@('-NoGui', '-NoStart', '-WhatIf', '-Country', 'Italy', '-TaskName', $NoTask, '-ProfileTaskName', $NoTask, '-OutputDir', $l2.out, '-GameDir', $s.game, '-BackupRoot', $l2.bk, '-LiveDir', $l2.live, '-AsideDir', $l2.aside, '-ProcessNames', 'none', '-TaskNames', 'none'))
  Check ($r.code -eq 0 -and $r.out -match 'STEP 4b: WHATIF would write' -and $r.out -match '"locale":"it_IT"' -and $r.out -match ('WHATIF would run schtasks /run /tn ' + $NoTask) -and -not (Test-Path -LiteralPath $s.req) -and (Both $s) -eq $h0) '-WhatIf: STEP 4b prints the request and the task run it would make; no request file written, registry unchanged'
  $r = Run $Launch @('-Undo', '-Locale', '-WhatIf', '-ProfileTaskName', $NoTask, '-OutputDir', $l2.out)
  Check ($r.code -eq 0 -and $r.out -match 'WHATIF: would write' -and $r.out -match '"action":"undo"' -and -not (Test-Path -LiteralPath $s.req)) '-Undo -Locale -WhatIf: prints the undo request, runs nothing'
  # forced timeout: no task answers
  $r = Run $Launch (@('-NoGui', '-NoStart', '-Country', 'Italy', '-TaskName', $NoTask, '-ProfileTaskName', $NoTask, '-ProfileNoRun', '-ProfileTimeoutSec', '2', '-OutputDir', $l2.out, '-GameDir', $s.game, '-BackupRoot', $l2.bk, '-LiveDir', $l2.live, '-AsideDir', $l2.aside, '-ProcessNames', 'none', '-TaskNames', 'none'))
  Check ($r.code -eq 30 -and $r.out -match 'no result from' -and $r.out -match ('would run schtasks /run /tn ' + $NoTask) -and (Both $s) -eq 'nl_NL/nl_NL') 'forced timeout: message, exit 30, "would run schtasks /run /tn <TaskName>" still printed, Locale as it was'
  Check ((Get-Json $l2.lj).locale_result -eq 'failed' -and (Get-Json $l2.lj).country -eq 'Italy') 'timeout: launch_profile.json still holds the pick, locale_result "failed"'
  # task not registered (a task name that does not exist, no -ProfileNoRun): fast failure, game still starts
  $r = Run $Launch (@('-NoGui', '-NoStart', '-Country', 'Italy', '-TaskName', $NoTask, '-ProfileTaskName', $NoTask, '-OutputDir', $l2.out, '-GameDir', $s.game, '-BackupRoot', $l2.bk, '-LiveDir', $l2.live, '-AsideDir', $l2.aside, '-ProcessNames', 'none', '-TaskNames', 'none'))
  Check ($r.code -eq 30 -and $r.out -match 'is not registered' -and $r.out -match ('would run schtasks /run /tn ' + $NoTask) -and (Both $s) -eq 'nl_NL/nl_NL') 'task not registered: message, Locale untouched, exit 30, the game start still announced'
  # pack absent through the launcher (Brazil, por_br in neither place)
  $job = Start-FakeTask $s
  $r = Run $Launch (P4Args $s $l2 @('-Country', 'Brazil'))
  Wait-Job $job -Timeout 30 | Out-Null; Remove-Job $job -Force
  Check ($r.code -eq 30 -and $r.out -match 'pack_absent' -and (Both $s) -eq 'nl_NL/nl_NL' -and (Get-Json $l2.lj).locale_result -eq 'pack_absent') 'pack_absent through the launcher: message names it, Locale unchanged, locale_result pack_absent, STEP 5 still reached'
  # -Status shows both views
  $r = Run $Launch @('-Status', '-TaskName', $NoTask, '-ProfileTaskName', $NoTask, '-OutputDir', $l2.out, '-BackupRoot', $l2.bk, '-LiveDir', $l2.live, '-AsideDir', $l2.aside, '-LnkPath', (Join-Path $Scratch 'nolnk.lnk'))
  Check ($r.code -eq 0 -and $r.out -match 'HKLM 64-bit view: ' -and $r.out -match 'HKLM 32-bit view: ' -and $r.out -match 'launch_profile_state.json') '-Status prints both HKLM Locale views (read only) and the profile state'
  # the launcher refuses a profiles file without a locales entry
  $nj = Get-Content -LiteralPath (Join-Path $Tools 'launch_profiles.json') -Raw | ConvertFrom-Json
  $nj.locales.PSObject.Properties.Remove('ita_it')
  $badProf = Join-Path $Scratch 'profiles_no_ita_locale.json'
  [IO.File]::WriteAllText($badProf, ($nj | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))
  $r = Run $Launch (P4Args $s $l2 @('-Country', 'England', '-ProfilesPath', $badProf))
  Check ($r.code -eq 30 -and $r.out -match 'locales map') 'a profiles file without a locales entry for a country''s language is rejected (exit 30, names the locales map)'

  # --- scratch registry gone, real state untouched
  Remove-Item -Path $regRootAll -Recurse -Force
  Check (-not (Test-Path -LiteralPath $regRootAll)) 'the scratch HKCU key is removed'
  Check ((Md5 $realGui) -eq $guiMd5L3) 'live turbo_output\gui_settings.json md5 unchanged'
  Check ((RealSnap) -eq $realBefore) 'real HKLM Locale (both views), Data\Win32 + backup listings, real turbo_output request/result/state/log and the FC27_Profile task state are unchanged by the whole L3 section'
}

$snapAfter = LiveSnapshot
Check ($snapBefore -eq $snapAfter) 'live mods\legacy compdata md5s unchanged by the whole self-test'
Write-Host ('SELF-TEST {0} ({1} cases, {2} failed)' -f $(if ($script:fails) { 'FAILED' } else { 'PASSED' }), $script:cases, $script:fails)
if ($script:fails) { exit 1 } else { exit 0 }
