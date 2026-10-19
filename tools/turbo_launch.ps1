# turbo_launch.ps1   (Windows PowerShell 5.1 + System.Windows.Forms, ASCII only)
# 2.21 country launcher, chunk L2 (brief 2.21-compdata-per-save.md, G1 signed 10-19; phase 1).
# ONE launcher: a country picker (last pick preselected); the pick applies that country's profile with the game CLOSED, then starts the game
# exactly as today (schtasks /run /tn <TaskName>, Cassio's no-UAC task). Phase 1 profile =
#   compdata  : Netherlands = the 11 Eerste Divisie files in place, every other country = the files aside (compdata_profile.ps1, md5 undo)
#   hint      : turbo_output\launch_profile.json + gui_settings.json callnames.launcher_language / launcher_stamp for Turbo's language
#               detection (NEVER override_language, which switches detection off)
#   commentary: report only (pack_state of Data\Win32, backup store separately); phase 1 writes nothing in Program Files.
# Phase 2 (brief 2.21p2-registry-locale.md, STEP 4b): the pick also asks the elevated scheduled task FC27_Profile (registered once by Cassio,
#   register_fc27_profile_task.ps1) to set the game's registry Locale to the pick's language: this script only writes
#   turbo_output\launch_profile_request.json, starts the task (schtasks /run, no elevation here) and polls turbo_output\launch_profile_result.json
#   by request_id (90 s, 300 s when a pack copy is due). Any failure or timeout -> message, Locale left as it was, the game STILL starts.
# Modes:
#   (no switch)        picker -> profile -> start the game
#   -NoGui [-Country X] no dialog: X, else the last pick, else the default country
#   -NoStart           do everything except the final task start (profile + state files really run)
#   -WithLocale        run STEP 4b (the registry Locale step); OFF by default since 2.21.0: the EA app passes its own per-game locale at launch (disproven 10-19)
#   -NoLocale          skip STEP 4b (kept for old command lines; the default already skips it)
#   -WhatIf            print every step, change and run NOTHING (the profile step runs with -WhatIf), exit 0
#   -Status            read-only report: compdata profile, last pick, pack states, both registry Locale views + the profile state, launcher .lnk, backups, task states
#   -Install           chunk I ONLY (needs Cassio's OK): back up the .lnk (byte copy + md5 manifest) and re-point it at this script;
#                      needs an explicit -LnkPath; for the real desktop .lnk it also needs -CassioOk
#   -Undo              copy the backed-up .lnk back, verify the md5 (explicit -LnkPath required)
#   -Undo -Locale      ask the task FC27_Profile to restore the recorded prior Locale (request action "undo"); -WhatIf prints the request only
#   -SelfTestPicker    build the picker form without showing it, print what it holds, exit 0
# Safety: the script never elevates, never edits the task or the game's .bat, never WRITES the registry (it reads Locale in -Status; the write is
# the task's), never touches fcsetup.ini or Program Files.
# Any error in the profile/picker steps -> message (box in GUI mode, text with -NoGui), the profile is left as it was, and the game
# STILL starts (the unconditional final task start) unless -NoStart / -WhatIf.
# Exit codes: 0 ok | 20 cancelled in the picker (nothing changed, nothing started) | 30 an error was handled (see the message; the game was
#             still started unless -NoStart / -WhatIf) | 31 the final task start failed | 40 -Install/-Undo refused | 2 bad arguments
[CmdletBinding()]
param(
  [string]$Country = '',
  [switch]$NoGui,
  [switch]$NoStart,
  [switch]$WhatIf,
  [switch]$Status,
  [switch]$Install,
  [switch]$Undo,
  [switch]$CassioOk,
  [switch]$SelfTestPicker,
  [switch]$Locale,
  [switch]$NoLocale,
  [switch]$WithLocale,
  [string]$ProfileTaskName = 'FC27_Profile',
  [int]$ProfileTimeoutSec = 90,
  [int]$ProfileCopyTimeoutSec = 300,
  [switch]$ProfileNoRun,
  [string]$TaskName = 'LaunchFC27',
  [string]$LnkPath = '',
  [string]$ProfilesPath = '',
  [string]$ProfileTool = '',
  [string]$OutputDir = 'C:\FC 27 Live Editor\turbo_output',
  [string]$BackupRoot = 'C:\FC 27 Live Editor\turbo_dev\backups',
  [string]$GameDir = 'C:\Program Files\EA Games\EA SPORTS FC 27',
  # pass-through to compdata_profile.ps1 (left empty = the tool's own defaults); 'none' = no such guard (offline self-test only)
  [string]$RecordPath = '',
  [string]$LiveDir = '',
  [string]$AsideDir = '',
  [string[]]$ProcessNames = @(),
  [string[]]$TaskNames = @()
)
$ErrorActionPreference = 'Stop'
if (-not $ProfilesPath) { $ProfilesPath = Join-Path $PSScriptRoot 'launch_profiles.json' }
if (-not $ProfileTool) { $ProfileTool = Join-Path $PSScriptRoot 'compdata_profile.ps1' }
$ThisScript = $MyInvocation.MyCommand.Path
$DefaultLnk = Join-Path $env:USERPROFILE 'OneDrive\Desktop\LaunchFC27.lnk'
$LaunchJson = Join-Path $OutputDir 'launch_profile.json'
$GuiSettings = Join-Path $OutputDir 'gui_settings.json'
$ReqJson = Join-Path $OutputDir 'launch_profile_request.json'
$ResJson = Join-Path $OutputDir 'launch_profile_result.json'
$StateJson = Join-Path $OutputDir 'launch_profile_state.json'
$Utf8 = New-Object Text.UTF8Encoding $false
$script:Messages = @()

function Say([string]$m, [string]$c = '') { if ($c) { Write-Host $m -ForegroundColor $c } else { Write-Host $m } }
function Md5([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLower() }
function Stamp { $n = Get-Date; @{ iso = $n.ToString('yyyy-MM-ddTHH:mm:ss'); epoch = [int64]([DateTimeOffset]$n).ToUnixTimeSeconds() } }

# ------------------------------------------------------------------ profiles
function Get-Profiles {
  if (-not (Test-Path -LiteralPath $ProfilesPath)) { throw ('launch_profiles.json missing: ' + $ProfilesPath) }
  $p = Get-Content -LiteralPath $ProfilesPath -Raw | ConvertFrom-Json
  foreach ($c in @($p.order)) {
    $e = $p.countries.$c
    if (-not $e) { throw ('country in "order" without an entry: ' + $c) }
    if (@('stock', 'eerste') -notcontains [string]$e.compdata) { throw ('country ' + $c + ': unknown compdata profile ' + $e.compdata) }
    if (-not $p.languages.([string]$e.language)) { throw ('country ' + $c + ': language ' + $e.language + ' is not in the languages map') }
    if (-not $p.locales.([string]$e.language)) { throw ('country ' + $c + ': language ' + $e.language + ' is not in the locales map') }
  }
  $p
}
function Get-LastPick($profiles) {
  $last = ''
  try { if (Test-Path -LiteralPath $LaunchJson) { $last = [string]((Get-Content -LiteralPath $LaunchJson -Raw | ConvertFrom-Json).country) } } catch { }
  if ($last -and (@($profiles.order) -contains $last)) { return $last }
  [string]$profiles.default_country
}

# ------------------------------------------------------------------ commentary pack state (report only)
function Fmt-Size([int64]$b) { if ($b -ge 1GB) { '{0:N2} GB' -f ($b / 1GB) } elseif ($b -ge 1MB) { '{0:N1} MB' -f ($b / 1MB) } else { '{0:N0} B' -f $b } }
function Get-PackInfo([string]$lang) {
  # what the game plays from: <game>\Data\Win32\commentaryfull_<lang>.toc + superbundlelayout\commentaryfull_<lang>\cas_NN.cas (Pit wall 15:53)
  $w = Join-Path $GameDir 'Data\Win32'
  $toc = Join-Path $w ('commentaryfull_' + $lang + '.toc')
  $cas = @(); $lau = @()
  $cd = Join-Path $w ('superbundlelayout\commentaryfull_' + $lang); if (Test-Path -LiteralPath $cd) { $cas = @(Get-ChildItem -LiteralPath $cd -Filter 'cas_*.cas' -File) }
  $ld = Join-Path $w ('superbundlelayout\commentarylaunch_' + $lang); if (Test-Path -LiteralPath $ld) { $lau = @(Get-ChildItem -LiteralPath $ld -Filter 'cas_*.cas' -File) }
  $tocOk = (Test-Path -LiteralPath $toc) -and ((Get-Item -LiteralPath $toc).Length -gt 4096)
  $casBytes = 0; foreach ($f in $cas) { $casBytes += $f.Length }
  $lauBytes = 0; foreach ($f in $lau) { $lauBytes += $f.Length }
  $full = $tocOk -and $cas.Count -gt 0 -and (@($cas | Where-Object { $_.Length -eq 0 }).Count -eq 0)
  if ($full) {
    $state = '{0} full present ({1} cas, {2}), launch subset {3}' -f $lang, $cas.Count, (Fmt-Size $casBytes), $(if ($lau.Count) { (Fmt-Size $lauBytes) } else { 'absent' })
  } else { $state = $lang + ' not installed in Data\Win32' }
  # his backup store (<game>\commentary), reported separately
  $bt = Join-Path $GameDir ('commentary\commentaryfull_' + $lang + '.toc'); $bd = Join-Path $GameDir ('commentary\commentaryfull_' + $lang)
  $bcas = @(); if (Test-Path -LiteralPath $bd) { $bcas = @(Get-ChildItem -LiteralPath $bd -Filter 'cas_*.cas' -File) }
  $bBytes = 0; foreach ($f in $bcas) { $bBytes += $f.Length }
  if ((Test-Path -LiteralPath $bt) -and $bcas.Count -gt 0) { $backup = 'backup present ({0} cas, {1})' -f $bcas.Count, (Fmt-Size $bBytes) } else { $backup = 'backup absent' }
  [pscustomobject]@{ lang = $lang; full = $full; pack_state = $state; backup_state = $backup }
}

# ------------------------------------------------------------------ compdata profile (child process, so its exit codes are data)
function Invoke-ProfileTool([string[]]$a) {
  $all = @($a)
  if ($RecordPath) { $all += @('-RecordPath', $RecordPath) }
  if ($LiveDir) { $all += @('-LiveDir', $LiveDir) }
  if ($AsideDir) { $all += @('-AsideDir', $AsideDir) }
  $st = Join-Path $OutputDir 'compdata_profile.json'; $all += @('-StateOut', $st)
  if ($ProcessNames.Count) { $all += @('-ProcessNames', ($ProcessNames -join ',')) }
  if ($TaskNames.Count) { $all += @('-TaskNames', ($TaskNames -join ',')) }
  $ea = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $o = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ProfileTool @all 2>&1 | Out-String
  $c = $LASTEXITCODE; $ErrorActionPreference = $ea
  @{ code = $c; out = $o.TrimEnd() }
}
function Get-CompdataState {
  $r = Invoke-ProfileTool @('-Status', '-Json')
  try { return ($r.out | ConvertFrom-Json).profile } catch { return 'unknown' }
}

# ------------------------------------------------------------------ state files
function Write-LaunchProfile($c, $e, $pk) {
  $s = Stamp
  $o = [ordered]@{ version = 1; country = $c; set_at = $s.iso; set_at_epoch = $s.epoch; compdata = [string]$e.compdata; language = [string]$e.language
    pack_state = $pk.pack_state; backup_state = $pk.backup_state; set_by = 'turbo_launch.ps1' }
  if (-not (Test-Path -LiteralPath $OutputDir)) { New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null }
  $tmp = $LaunchJson + '.tmp'
  [IO.File]::WriteAllText($tmp, (($o | ConvertTo-Json -Depth 6) + "`r`n"), $Utf8)
  if (Test-Path -LiteralPath $LaunchJson) { Remove-Item -LiteralPath $LaunchJson -Force }
  Move-Item -LiteralPath $tmp -Destination $LaunchJson
  $s
}
# Textual patch of ONLY the callnames object in gui_settings.json (Cassio's live file): everything else stays byte-identical.
function Write-Hint([string]$lang, [string]$iso) {
  if (-not (Test-Path -LiteralPath $GuiSettings)) { return 'gui_settings.json not found: hint not written (Turbo falls back to its own detection)' }
  $bytes = [IO.File]::ReadAllBytes($GuiSettings)
  $bom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
  $txt = [Text.Encoding]::UTF8.GetString($bytes); if ($bom) { $txt = $txt.Substring(1) }
  $m = [regex]::Match($txt, '("callnames"\s*:\s*\{)([^{}]*)(\})')
  if (-not $m.Success) { return 'gui_settings.json has no flat "callnames" object: hint not written' }
  $nl = $(if ($txt.Contains("`r`n")) { "`r`n" } else { "`n" })
  $inner = $m.Groups[2].Value
  $trail = [regex]::Match($inner, '\s*$').Value
  $core = $inner.Substring(0, $inner.Length - $trail.Length)
  $core = [regex]::Replace($core, ',?\s*"launcher_(language|stamp)"\s*:\s*"[^"]*"', '')
  $core = [regex]::Replace($core, '^\s*,', '')
  $ind = '    '; $im = [regex]::Matches($inner, '\n([ \t]+)"'); if ($im.Count) { $ind = $im[$im.Count - 1].Groups[1].Value }
  if ($core.Trim().Length -gt 0) { $core = $core.TrimEnd() + ',' } else { $core = '' }
  $core += $nl + $ind + '"launcher_language": "' + $lang + '",' + $nl + $ind + '"launcher_stamp": "' + $iso + '"'
  if ($trail -eq '') { $trail = $nl }
  $new = $txt.Substring(0, $m.Groups[2].Index) + $core + $trail + $txt.Substring($m.Groups[2].Index + $m.Groups[2].Length)
  # validate: parses, and every other value is deep-equal to the original
  try { $oj = $txt | ConvertFrom-Json; $nj = $new | ConvertFrom-Json } catch { return 'patched gui_settings.json does not parse: hint not written' }
  $nj.callnames.PSObject.Properties.Remove('launcher_language'); $nj.callnames.PSObject.Properties.Remove('launcher_stamp')
  $oj.callnames.PSObject.Properties.Remove('launcher_language'); $oj.callnames.PSObject.Properties.Remove('launcher_stamp')
  if (($oj | ConvertTo-Json -Depth 32 -Compress) -ne ($nj | ConvertTo-Json -Depth 32 -Compress)) { return 'patched gui_settings.json differs in other keys: hint not written' }
  $nb = $Utf8.GetBytes($new); if ($bom) { $nb = [byte[]](@(0xEF, 0xBB, 0xBF) + $nb) }
  $tmp = $GuiSettings + '.launcher.tmp'
  [IO.File]::WriteAllBytes($tmp, $nb)
  Move-Item -LiteralPath $tmp -Destination $GuiSettings -Force
  'hint written: callnames.launcher_language=' + $lang + ' launcher_stamp=' + $iso
}

# ------------------------------------------------------------------ phase 2: the registry Locale through the elevated task FC27_Profile
# read-only look at the game's Locale in both views (HKLM, KEY_QUERY_VALUE only)
function Read-LocaleView([string]$view) {
  try {
    $rv = $(if ($view -eq 'view64') { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 })
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $rv)
    $k = $base.OpenSubKey('SOFTWARE\EA Sports\EA SPORTS FC 27', $false)
    if (-not $k) { return '(key absent)' }
    $v = $k.GetValue('Locale', $null); $k.Dispose(); $base.Dispose()
    if ($null -eq $v) { return '(no Locale value)' }
    [string]$v
  } catch { '(unreadable: ' + $_.Exception.Message + ')' }
}
function New-RequestId { 'tl' + [string]([DateTimeOffset](Get-Date)).ToUnixTimeSeconds() + '-' + (Get-Random -Minimum 1000 -Maximum 9999) }
function New-ProfileRequest([string]$action, [string]$lang, [string]$loc) {
  [ordered]@{ version = 1; action = $action; language = $lang; locale = $loc; request_id = (New-RequestId); requested_at = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss') }
}
# Writes the request, starts the task, polls the result by request_id. Never throws; returns @{ ran; ok; message; result }
function Invoke-ProfileTask($req, [int]$timeout) {
  $out = @{ ran = $false; ok = $false; message = ''; result = $null }
  if (-not $ProfileNoRun) {
    $ex = Get-ScheduledTask -TaskName $ProfileTaskName -ErrorAction SilentlyContinue
    if (-not $ex) { $out.message = ('the task ' + $ProfileTaskName + ' is not registered (Cassio registers it once with register_fc27_profile_task.ps1 from an elevated PowerShell); Locale left as it was'); return $out }
    if ([string]$ex.State -ne 'Ready') { $out.message = ('the task ' + $ProfileTaskName + ' is ' + $ex.State + ' (IgnoreNew: a second start would be dropped); Locale left as it was'); return $out }
  }
  try {
    if (-not (Test-Path -LiteralPath $OutputDir)) { New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null }
    $tmp = $ReqJson + '.tmp'
    [IO.File]::WriteAllText($tmp, (($req | ConvertTo-Json -Depth 4) + "`n"), $Utf8)
    Move-Item -LiteralPath $tmp -Destination $ReqJson -Force
  } catch { $out.message = 'could not write the request file: ' + $_.Exception.Message; return $out }
  if (-not $ProfileNoRun) {
    $ea = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $o = & schtasks.exe /run /tn $ProfileTaskName 2>&1 | Out-String
    $c = $LASTEXITCODE; $ErrorActionPreference = $ea
    if ($c -ne 0) { $out.message = ('schtasks /run /tn ' + $ProfileTaskName + ' failed (exit ' + $c + ' ' + $o.Trim() + '); Locale left as it was'); return $out }
    $out.ran = $true
  }
  $end = (Get-Date).AddSeconds($timeout)
  while ((Get-Date) -lt $end) {
    if (Test-Path -LiteralPath $ResJson) {
      try {
        $r = Get-Content -LiteralPath $ResJson -Raw | ConvertFrom-Json
        if ([string]$r.request_id -eq [string]$req.request_id) { $out.result = $r; $out.ok = [bool]$r.ok; $out.message = [string]$r.message; return $out }
      } catch { }
    }
    Start-Sleep -Milliseconds 500
  }
  $out.message = ('no result from ' + $ProfileTaskName + ' after ' + $timeout + ' s (request ' + $req.request_id + '); Locale left as it was unless the task is still running')
  $out
}
function Fmt-Result($r) {
  if ($r.result) { 'result: ok={0} pack={1} Locale {2}/{3} -> {4}/{5}; {6}' -f $r.result.ok, $r.result.pack_state, $r.result.locale_before.view64, $r.result.locale_before.view32, $r.result.locale_after.view64, $r.result.locale_after.view32, $r.message }
  else { 'no result: ' + $r.message }
}
# STEP 4b for a pick; $copyDue = the pack is not live in Data\Win32 (copy-in expected, longer timeout)
function Do-LocaleStep($profiles, $e, [bool]$copyDue) {
  $lang = [string]$e.language; $loc = [string]$profiles.locales.$lang
  $req = New-ProfileRequest 'set' $lang $loc
  if ($WhatIf) {
    Say ('STEP 4b: WHATIF would write ' + $ReqJson + ':')
    Say ('         ' + ($req | ConvertTo-Json -Compress))
    Say ('STEP 4b: WHATIF would run schtasks /run /tn ' + $ProfileTaskName + ' and wait for ' + $ResJson)
    return @{ requested = $loc; result = 'whatif'; failed = '' }
  }
  $t = $(if ($copyDue) { $ProfileCopyTimeoutSec } else { $ProfileTimeoutSec })
  Say ('STEP 4b: Locale ' + $loc + ' (' + $lang + ') through the task ' + $ProfileTaskName + ' (request ' + $req.request_id + ', wait up to ' + $t + ' s)')
  $r = Invoke-ProfileTask $req $t
  Say ('         ' + (Fmt-Result $r))
  if (-not $r.ok) {
    $msg = 'Locale step: ' + $r.message
    return @{ requested = $loc; result = $(if ($r.result) { [string]$r.result.pack_state } else { 'failed' }); failed = $msg }
  }
  @{ requested = $loc; result = ('ok ' + [string]$r.result.pack_state); failed = '' }
}
# add locale_requested / locale_result to launch_profile.json (FORMAT only; the other keys stay as written by STEP 4)
function Add-LocaleFields([string]$requested, [string]$result) {
  if (-not (Test-Path -LiteralPath $LaunchJson)) { return }
  $j = Get-Content -LiteralPath $LaunchJson -Raw | ConvertFrom-Json
  $o = [ordered]@{}
  foreach ($p in $j.PSObject.Properties) { if (@('locale_requested', 'locale_result') -notcontains $p.Name) { $o[$p.Name] = $p.Value } }
  $o['locale_requested'] = $requested; $o['locale_result'] = $result
  $tmp = $LaunchJson + '.tmp'
  [IO.File]::WriteAllText($tmp, (($o | ConvertTo-Json -Depth 6) + "`r`n"), $Utf8)
  Move-Item -LiteralPath $tmp -Destination $LaunchJson -Force
}
function Do-LocaleUndo {
  $req = New-ProfileRequest 'undo' '' ''
  if ($WhatIf) {
    Say ('WHATIF: would write ' + $ReqJson + ':'); Say ('  ' + ($req | ConvertTo-Json -Compress))
    Say ('WHATIF: would run schtasks /run /tn ' + $ProfileTaskName + ' and wait for ' + $ResJson)
    return 0
  }
  Say ('LOCALE UNDO: asking ' + $ProfileTaskName + ' to restore the recorded prior Locale (request ' + $req.request_id + ')')
  $r = Invoke-ProfileTask $req $ProfileTimeoutSec
  Say ('  ' + (Fmt-Result $r))
  if ($r.ok) { Say ('LOCALE UNDO PASS: ' + $r.message) 'Green'; return 0 }
  Say ('LOCALE UNDO FAILED: ' + $r.message) 'Red'
  30
}

# ------------------------------------------------------------------ picker
function New-Picker($profiles, [string]$preselect, [string]$compState) {
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -AssemblyName System.Drawing
  $f = New-Object Windows.Forms.Form
  $f.Text = 'FC 27 Turbo - pick the country'; $f.StartPosition = 'CenterScreen'; $f.FormBorderStyle = 'FixedDialog'
  $f.MaximizeBox = $false; $f.MinimizeBox = $false; $f.TopMost = $true; $f.ClientSize = New-Object Drawing.Size(640, (150 + 44 * @($profiles.order).Count))
  $hdr = New-Object Windows.Forms.Label; $hdr.Left = 16; $hdr.Top = 12; $hdr.Width = 608; $hdr.Height = 40
  $hdr.Text = "Compdata now in place: $compState`nThe pick is applied with the game closed, then FC 27 starts."
  $f.Controls.Add($hdr)
  $radios = @{}; $y = 62
  foreach ($c in @($profiles.order)) {
    $e = $profiles.countries.$c; $pk = Get-PackInfo ([string]$e.language)
    $r = New-Object Windows.Forms.RadioButton; $r.Left = 20; $r.Top = $y; $r.Width = 600; $r.Height = 38
    $r.Text = ('{0}   (compdata: {1}; commentary: {2})' -f $c, $e.compdata, $pk.pack_state)
    if ($c -eq $preselect) { $r.Checked = $true }
    $f.Controls.Add($r); $radios[$c] = $r; $y += 44
  }
  $ok = New-Object Windows.Forms.Button; $ok.Text = 'Start FC 27'; $ok.Left = 380; $ok.Top = $y + 12; $ok.Width = 120; $ok.Height = 32; $ok.DialogResult = 'OK'
  $cn = New-Object Windows.Forms.Button; $cn.Text = 'Cancel'; $cn.Left = 510; $cn.Top = $y + 12; $cn.Width = 110; $cn.Height = 32; $cn.DialogResult = 'Cancel'
  $f.Controls.Add($ok); $f.Controls.Add($cn); $f.AcceptButton = $ok; $f.CancelButton = $cn
  @{ form = $f; radios = $radios }
}
function Show-Picker($profiles, [string]$preselect, [string]$compState) {
  $p = New-Picker $profiles $preselect $compState
  $res = $p.form.ShowDialog()
  $pick = ''
  if ($res -eq 'OK') { foreach ($c in @($profiles.order)) { if ($p.radios[$c].Checked) { $pick = $c } } }
  $p.form.Dispose()
  $pick
}
function Show-ErrorBox([string]$msg) {
  if ($NoGui) { return }
  try { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($msg + "`n`nThe profile was left as it was. FC 27 still starts.", 'FC 27 Turbo launcher', 'OK', 'Warning') } catch { }
}

# ------------------------------------------------------------------ .lnk install / undo (chunk I; scratch .lnk in self-tests)
function Get-LnkInfo([string]$p) {
  $sh = New-Object -ComObject WScript.Shell
  $s = $sh.CreateShortcut($p)
  [pscustomobject]@{ target = $s.TargetPath; args = $s.Arguments; workdir = $s.WorkingDirectory; icon = $s.IconLocation; style = $s.WindowStyle; desc = $s.Description }
}
function Find-Backup([string]$lnk) {
  $best = $null
  if (Test-Path -LiteralPath $BackupRoot) {
    foreach ($d in (Get-ChildItem -LiteralPath $BackupRoot -Directory -Filter 'launcher_*' | Sort-Object Name -Descending)) {
      $mf = Join-Path $d.FullName 'manifest.json'
      if (-not (Test-Path -LiteralPath $mf)) { continue }
      try { $m = Get-Content -LiteralPath $mf -Raw | ConvertFrom-Json } catch { continue }
      if (([string]$m.lnk_path).ToLower() -eq $lnk.ToLower()) { $best = [pscustomobject]@{ dir = $d.FullName; manifest = $m }; break }
    }
  }
  $best
}
function Do-Install {
  if (-not $LnkPath) { Say 'REFUSED: -Install needs an explicit -LnkPath' 'Red'; return 40 }
  if (-not (Test-Path -LiteralPath $LnkPath)) { Say ('REFUSED: no such .lnk: ' + $LnkPath) 'Red'; return 40 }
  if ($LnkPath.ToLower() -eq $DefaultLnk.ToLower() -and -not $CassioOk) { Say 'REFUSED: that is Cassio''s real desktop shortcut; chunk I needs his OK in chat (-CassioOk)' 'Red'; return 40 }
  $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
  $bd = Join-Path $BackupRoot ('launcher_' + $stamp)
  $old = Get-LnkInfo $LnkPath
  $omd5 = Md5 $LnkPath
  $leaf = Split-Path -Leaf $LnkPath
  if ($WhatIf) { Say ('WHATIF: would back up ' + $LnkPath + ' (md5 ' + $omd5 + ') to ' + $bd + ' and re-point it at ' + $ThisScript); return 0 }
  New-Item -ItemType Directory -Force -Path $bd | Out-Null
  Copy-Item -LiteralPath $LnkPath -Destination (Join-Path $bd $leaf)
  if ((Md5 (Join-Path $bd $leaf)) -ne $omd5) { Say 'REFUSED: backup copy md5 differs' 'Red'; return 40 }
  $man = [ordered]@{ version = 1; lnk_path = $LnkPath; leaf = $leaf; md5 = $omd5; backed_up_at = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
    original = [ordered]@{ target = $old.target; arguments = $old.args; working_dir = $old.workdir; icon = $old.icon; window_style = $old.style } }
  [IO.File]::WriteAllText((Join-Path $bd 'manifest.json'), (($man | ConvertTo-Json -Depth 6) + "`r`n"), $Utf8)
  $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $sh = New-Object -ComObject WScript.Shell
  $s = $sh.CreateShortcut($LnkPath)
  $s.TargetPath = $ps
  $s.Arguments = '-NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "' + $ThisScript + '"'
  $s.WorkingDirectory = Split-Path -Parent $ThisScript
  if ($old.icon -and $old.icon -ne ',0') { $s.IconLocation = $old.icon }
  $s.Save()
  $chk = Get-LnkInfo $LnkPath
  if ($chk.target -ne $ps -or $chk.args -notlike ('*' + $ThisScript + '*')) { Say 'INSTALL FAILED the read-back; run -Undo' 'Red'; return 40 }
  Say ('INSTALL PASS: ' + $LnkPath + ' now runs ' + $ThisScript + '; original saved in ' + $bd + ' (md5 ' + $omd5 + '); undo = -Undo -LnkPath ...') 'Green'
  0
}
function Do-Undo {
  if (-not $LnkPath) { Say 'REFUSED: -Undo needs an explicit -LnkPath' 'Red'; return 40 }
  $b = Find-Backup $LnkPath
  if (-not $b) { Say ('REFUSED: no launcher_* backup with a manifest for ' + $LnkPath + ' under ' + $BackupRoot) 'Red'; return 40 }
  $src = Join-Path $b.dir ([string]$b.manifest.leaf)
  if (-not (Test-Path -LiteralPath $src)) { Say ('REFUSED: backup file missing: ' + $src) 'Red'; return 40 }
  if ((Md5 $src) -ne [string]$b.manifest.md5) { Say 'REFUSED: backup file md5 differs from its manifest' 'Red'; return 40 }
  if ($WhatIf) { Say ('WHATIF: would copy ' + $src + ' back over ' + $LnkPath); return 0 }
  Copy-Item -LiteralPath $src -Destination $LnkPath -Force
  if ((Md5 $LnkPath) -ne [string]$b.manifest.md5) { Say 'UNDO FAILED: md5 after the copy differs' 'Red'; return 40 }
  Say ('UNDO PASS: ' + $LnkPath + ' restored from ' + $b.dir + ' (md5 ' + $b.manifest.md5 + ' byte-equal)') 'Green'
  0
}

# ------------------------------------------------------------------ -Status
function Do-Status {
  $profiles = Get-Profiles
  Say '== compdata profile'
  $r = Invoke-ProfileTool @('-Status'); Say $r.out
  Say '== last pick (launch_profile.json)'
  if (Test-Path -LiteralPath $LaunchJson) { Say (Get-Content -LiteralPath $LaunchJson -Raw) } else { Say '(none yet)' }
  Say '== commentary (Data\Win32 = what the game plays; backup store reported separately)'
  foreach ($l in ($profiles.languages.PSObject.Properties.Name)) { $pk = Get-PackInfo $l; Say ('  {0}; {1}' -f $pk.pack_state, $pk.backup_state) }
  Say '== registry Locale (read only; the write is the task''s)'
  Say ('  HKLM 64-bit view: ' + (Read-LocaleView 'view64')); Say ('  HKLM 32-bit view: ' + (Read-LocaleView 'view32'))
  if (Test-Path -LiteralPath $StateJson) { Say '  launch_profile_state.json:'; Say (Get-Content -LiteralPath $StateJson -Raw) } else { Say '  launch_profile_state.json: (none)' }
  Say '== gui_settings.json hint keys'
  try { $g = Get-Content -LiteralPath $GuiSettings -Raw | ConvertFrom-Json; Say ('  launcher_language=' + $g.callnames.launcher_language + ' launcher_stamp=' + $g.callnames.launcher_stamp + ' override_language="' + $g.callnames.override_language + '"') } catch { Say '  (unreadable or absent)' }
  Say '== launcher shortcut'
  $l = $(if ($LnkPath) { $LnkPath } else { $DefaultLnk })
  if (Test-Path -LiteralPath $l) { $i = Get-LnkInfo $l; Say ('  {0}  md5 {1}' -f $l, (Md5 $l)); Say ('  target: ' + $i.target); Say ('  args:   ' + $i.args) } else { Say ('  not found: ' + $l) }
  $b = Find-Backup $l
  if ($b) { Say ('  backup: ' + $b.dir + ' (md5 ' + $b.manifest.md5 + ')') } else { Say '  backup: none (the shortcut is not re-pointed yet)' }
  Say '== tasks (read only)'
  foreach ($t in @('LE_NoUAC', 'FC_NoUAC', $TaskName, $ProfileTaskName)) { $x = Get-ScheduledTask -TaskName $t -ErrorAction SilentlyContinue; Say ('  {0,-12} {1}' -f $t, $(if ($x) { [string]$x.State } else { 'MISSING' })) }
  0
}

# ------------------------------------------------------------------ main
if ($SelfTestPicker) {
  $pr = Get-Profiles; $pre = Get-LastPick $pr
  $p = New-Picker $pr $pre 'self-test'
  $checked = @($pr.order | Where-Object { $p.radios[$_].Checked })
  Say ('picker built: {0} radio buttons, preselected {1}, buttons {2}' -f $p.radios.Count, ($checked -join ','), (@($p.form.Controls | Where-Object { $_ -is [Windows.Forms.Button] } | ForEach-Object Text) -join '/'))
  $p.form.Dispose(); exit 0
}
if ($Install) { exit (Do-Install) }
if ($Undo -and $Locale) { exit (Do-LocaleUndo) }
if ($Undo) { exit (Do-Undo) }
if ($Status) { try { exit (Do-Status) } catch { Say ('STATUS ERROR: ' + $_.Exception.Message) 'Red'; exit 30 } }

$handled = 0
try {
  $profiles = Get-Profiles
  $pick = $Country
  if ($pick -and (@($profiles.order) -notcontains $pick)) { throw ('unknown country "' + $pick + '" (known: ' + (@($profiles.order) -join ', ') + ')') }
  if (-not $pick) {
    $pre = Get-LastPick $profiles
    if ($NoGui -or $WhatIf) { $pick = $pre; Say ('STEP 1: no picker, country = ' + $pick + ' (last pick or default)') }
    else {
      $pick = Show-Picker $profiles $pre (Get-CompdataState)
      if (-not $pick) { Say 'CANCELLED: nothing changed, nothing started.' 'Yellow'; exit 20 }
    }
  } else { Say ('STEP 1: country = ' + $pick) }
  $e = $profiles.countries.$pick
  $pk = Get-PackInfo ([string]$e.language)
  Say ('STEP 2: profile ' + $pick + ' = compdata ' + $e.compdata + ', language ' + $e.language)
  Say ('         commentary: ' + $pk.pack_state + '; ' + $pk.backup_state)
  if (-not $pk.full -and [string]$e.language -ne 'eng_us') { Say ('         NOTE: ' + $e.language + ' is not installed; the commentary step is skipped, the launch is not blocked') 'Yellow' }
  $args2 = @('-Set', [string]$e.compdata); if ($WhatIf) { $args2 += '-WhatIf' }
  $r = Invoke-ProfileTool $args2
  Say ('STEP 3: compdata_profile.ps1 ' + ($args2 -join ' ') + ' -> exit ' + $r.code)
  if ($r.out) { Say ('         ' + ($r.out -replace "`r?`n", "`n         ")) }
  if ($r.code -ne 0) { throw ('the compdata profile step refused (exit ' + $r.code + '): ' + ($r.out -split "`n" | Select-Object -Last 1)) }
  if ($WhatIf) {
    Say ('STEP 4: WHATIF would write ' + $LaunchJson + ' and the hint keys in ' + $GuiSettings)
  } else {
    $s = Write-LaunchProfile $pick $e $pk
    Say ('STEP 4: wrote ' + $LaunchJson)
    Say ('         ' + (Write-Hint ([string]$e.language) $s.iso))
  }
} catch {
  $handled = 30
  $msg = 'launcher step failed: ' + $_.Exception.Message
  $script:Messages += $msg
  Say ('ERROR: ' + $msg) 'Red'
  Show-ErrorBox $msg
}

# STEP 4b (phase 2): the registry Locale through the elevated task; its own try, so a failure here leaves the pick as made and the game still starts
if ($NoLocale -or -not $WithLocale) { Say 'STEP 4b: skipped (off by default since 2.21.0; -WithLocale turns it on): the registry Locale is left as it is' }
elseif ($handled -eq 0) {
  try {
    $lr = Do-LocaleStep $profiles $e ((-not $pk.full) -and [string]$e.language -ne 'eng_us')
    if (-not $WhatIf) { Add-LocaleFields $lr.requested $lr.result }
    if ($lr.failed) {
      $handled = 30
      $script:Messages += $lr.failed
      Say ('NOTE: ' + $lr.failed + ' (the game still starts)') 'Yellow'
      Show-ErrorBox $lr.failed
    }
  } catch {
    $handled = 30
    $msg = 'Locale step failed: ' + $_.Exception.Message
    $script:Messages += $msg
    Say ('ERROR: ' + $msg) 'Red'
    Show-ErrorBox $msg
  }
}

# the unconditional last step: the game starts exactly as today
if ($NoStart -or $WhatIf) {
  Say ('STEP 5: would run schtasks /run /tn ' + $TaskName + ' (' + $(if ($WhatIf) { '-WhatIf' } else { '-NoStart' }) + ')')
  exit $handled
}
$ea = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
$o = & schtasks.exe /run /tn $TaskName 2>&1 | Out-String
$c = $LASTEXITCODE; $ErrorActionPreference = $ea
Say ('STEP 5: schtasks /run /tn ' + $TaskName + ' -> exit ' + $c + ' ' + $o.Trim())
if ($c -ne 0) { Show-ErrorBox ('Could not start the task ' + $TaskName + ': ' + $o.Trim()); exit 31 }
exit $handled
