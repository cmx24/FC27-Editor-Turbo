# g3_deploy.ps1 [-DryRun] [-Dll <path>] [-ExpectedMd5 <md5>] [-SettingsWrite] [-Preset <path>]   (Windows PowerShell 5.1, ASCII only)
# Launch 3 (attribulator EBX patch, branch 2.10/attrib-ebx): backs up the installed Turbo.dll (md5 recorded), installs the built DLL and
# turbo_output\attrib_preset.json (the sentinel preset). Run with the game CLOSED. Never launches or touches the game.
# -Dll          the Turbo.dll to install (default: the 2.10/release build output, turbo_dev\2.0\wt-30-release\turbogui\build\win\Turbo.dll,
#               where turbogui\scripts\build_win.sh writes it)
# -ExpectedMd5  the md5 that DLL must have. Required for a real deploy (a dry run without it only prints the computed md5). -BuiltMd5 is the old name.
# -Preset       (optional) the attrib preset to install instead of the sentinel. Must be JSON with the same top-level keys and the same patch
#               knobs/keys as the sentinel (else refused, checked first, before any state check). The deploy record gets preset_path|<path> too.
# -SettingsWrite (off by default) ALSO writes the 3 verified Custom sliders (Sprint Speed User 37, Acceleration User 41, Sprint Speed CPU 63;
#               u8 block at file 0xB8DDC, TEST_31 G-S1) into the game's Settings file, through the Turbo tool turbo_settings_apply.exe --apply
#               (it owns the CRC, the backup in turbo_output\settings_backup and the write). Before that the Settings file is copied to
#               <evidence>\pre\settings and its name + md5 go into the deploy record. g3_undo.ps1 restores it (md5-verified).
#               With -DryRun the tool runs --dry-run against a COPY of the Settings file in a temp folder: the live install is not written.
# Refuses: game/launcher running, attrib_ebx_off.txt present,
# a deploy record already there, an attrib_preset.json already there, wrong DLL md5, installed Turbo.dll not the known original
# (877B33A2... 2.8.0 or 62600CED... 2.10.0), a pending settings file already there / not exactly one Settings* file (with -SettingsWrite).
# turbo_output\kits3d_on.txt present is NOT a refusal since 2.11 (shared Oodle hook: the 3D kit swap and the attrib patch run together, so a
# combined kits3d + attrib preset test can run); the deploy only prints a note and leaves the file alone.
# Exit 0 = deployed, 1 = refused (nothing written), 2 = failed midway (run g3_undo.ps1).
param(
  [switch]$DryRun,
  [string]$Dll = '',
  [Alias('BuiltMd5')][string]$ExpectedMd5 = '',
  [switch]$SettingsWrite,
  [string]$Preset = '',
  [string]$SettingsExe = '',
  [string]$SettingsMap = ''
)
$ErrorActionPreference = 'Stop'
$LE     = 'C:\FC 27 Live Editor'
$Day    = '2026-10-15'
$Out    = Join-Path $LE 'turbo_output'
$Inst   = Join-Path $LE 'turbo\Turbo.dll'
$OrigMd5s = @('877B33A262D54C03743BE7B411D8E991', '62600CEDCE6C022CA5856F3310B80AF4')     # installed Turbo.dll builds this script may replace: 2.8.0 and 2.10.0 (exact full md5s, no wildcard)
$WR     = Join-Path $LE 'turbo_dev\2.0\wt-30-release'
$WT     = Join-Path $LE 'turbo_dev\2.0\wt-30-ebx'
$Built  = if ($Dll) { $Dll } else { Join-Path $WR 'turbogui\build\win\Turbo.dll' }
# sentinel preset: tools copy first, then the release worktree, then the ebx worktree
$Sentinel = $null
foreach ($c in @((Join-Path $PSScriptRoot 'attrib_preset_sentinel.json'), (Join-Path $WR 'tools\attrib_preset_sentinel.json'), (Join-Path $WT 'tools\attrib_preset_sentinel.json'))) { if (Test-Path -LiteralPath $c) { $Sentinel = $c; break } }
$Dest   = Join-Path $Out 'attrib_preset.json'
$Kits   = Join-Path $Out 'kits3d_on.txt'
$Off    = Join-Path $Out 'attrib_ebx_off.txt'
$Ev     = Join-Path $LE ('turbo_dev\evidence\{0}\launchG3' -f $Day)
$Pre    = Join-Path $Ev 'pre'
$Rec    = Join-Path $Ev 'g3_md5.txt'
$Apply  = if ($SettingsExe) { $SettingsExe } else { Join-Path $WR 'turbogui\build\win\turbo_settings_apply.exe' }
$Map    = if ($SettingsMap) { $SettingsMap } else { Join-Path $WR 'turbo\package\turbo\settings\slider_map_6AC07E31.json' }
$SetDir = Join-Path $env:LOCALAPPDATA 'EA SPORTS FC 27\settings'
$Pending = Join-Path $Out 'settings_pending.json'
$Sliders = [ordered]@{ 'user.sprint_speed' = 37; 'user.acceleration' = 41; 'cpu.sprint_speed' = 63 }
$ExpectedMd5 = $ExpectedMd5.ToUpperInvariant()
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash } else { 'ABSENT' } }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; exit 1 }
function Has([string]$p) { Test-Path -LiteralPath $p }
function PendingJson() {
  $v = ($Sliders.Keys | ForEach-Object { '  "' + $_ + '": ' + $Sliders[$_] }) -join ",`n"
  return ("{`n ""turbo_settings_pending"": 1,`n ""preset"": ""g3-custom-37-41-63"",`n ""staged"": """ + (Get-Date).ToString('s') + """,`n ""source_md5"": """",`n ""values"": {`n" + $v + "`n }`n}`n")
}
if ($ExpectedMd5 -ne '' -and $ExpectedMd5 -notmatch '^[0-9A-F]{32}$') { Fail ('-ExpectedMd5 is not an md5: ' + $ExpectedMd5) }
if ($DryRun) { Write-Host '*** DRY RUN: nothing is changed ***' -ForegroundColor Yellow }

# ---- -Preset validation (read-only; runs first so a bad preset is refused whatever the state of the game / deploy record) ----
$PresetSrc = $Sentinel
if ($Preset -ne '') {
  if (-not (Has $Preset)) { Fail ('-Preset: missing: ' + $Preset) }
  if (-not $Sentinel) { Fail '-Preset: missing: attrib_preset_sentinel.json (the key reference; looked in tools\, wt-30-release\tools, wt-30-ebx\tools)' }
  try { $pj = Get-Content -LiteralPath $Preset -Raw | ConvertFrom-Json } catch { Fail ('-Preset: not JSON: ' + $_.Exception.Message) }
  try { $sj = Get-Content -LiteralPath $Sentinel -Raw | ConvertFrom-Json } catch { Fail ('-Preset: the sentinel is not JSON: ' + $_.Exception.Message) }
  if ($null -eq $pj -or $null -eq $sj) { Fail '-Preset: empty JSON' }
  $kp = (@($pj.PSObject.Properties.Name) | Sort-Object) -join ','
  $ks = (@($sj.PSObject.Properties.Name) | Sort-Object) -join ','
  if ($kp -ne $ks) { Fail ('-Preset: top-level keys differ from the sentinel: [' + $kp + '] vs [' + $ks + ']') }
  foreach ($grp in @('locations', 'patches')) {
    $pa = @($pj.$grp); $sa = @($sj.$grp)
    if ($pa.Count -ne $sa.Count -or $pa.Count -eq 0) { Fail ('-Preset: ' + $grp + ' has ' + $pa.Count + ' entries, the sentinel ' + $sa.Count) }
    for ($i = 0; $i -lt $sa.Count; $i++) {
      $ek = (@($pa[$i].PSObject.Properties.Name) | Sort-Object) -join ','
      $es = (@($sa[$i].PSObject.Properties.Name) | Sort-Object) -join ','
      if ($ek -ne $es) { Fail ('-Preset: ' + $grp + '[' + $i + '] keys [' + $ek + '] differ from the sentinel [' + $es + ']') }
    }
  }
  for ($i = 0; $i -lt @($sj.patches).Count; $i++) {
    $pp = @($pj.patches)[$i]; $sp = @($sj.patches)[$i]
    if ($pp.knob -ne $sp.knob -or $pp.asset -ne $sp.asset -or $pp.path -ne $sp.path -or $pp.offset -ne $sp.offset -or $pp.kind -ne $sp.kind) { Fail ('-Preset: patches[' + $i + '] (' + $pp.knob + ') does not target the same knob/asset/path/offset/kind as the sentinel (' + $sp.knob + ')') }
    $dv = 0.0
    if (-not [double]::TryParse([string]$pp.value, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$dv)) { Fail ('-Preset: patches[' + $i + '] value is not a number: ' + $pp.value) }
  }
  $PresetSrc = (Resolve-Path -LiteralPath $Preset).Path
  Write-Host ('Preset (-Preset) checked         : ' + $PresetSrc + ' md5 ' + (Md5 $PresetSrc) + ' (same keys as ' + $Sentinel + ')')
}

$run = @(Get-Process -Name FC27, LauncherNoAuth -ErrorAction SilentlyContinue)
if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ')) }
foreach ($f in @($Inst, $Built)) { if (-not (Has $f)) { Fail ('missing: ' + $f) } }
if (-not $PresetSrc) { Fail ('missing: attrib_preset_sentinel.json (looked in tools\, wt-30-release\tools, wt-30-ebx\tools)') }
if (Has $Kits) { Write-Host ('NOTE: kits3d_on.txt exists (' + $Kits + '): the 3D kit swap runs together with the attrib patch (shared Oodle hook, 2.11); the file is left as it is') -ForegroundColor Yellow }
if (Has $Off)  { Fail ('attrib_ebx_off.txt exists (' + $Off + '): the patch would be off; remove it yourself') }
$builtNow = Md5 $Built
if ($ExpectedMd5 -eq '') {
  if (-not $DryRun) { Fail ('-ExpectedMd5 is required for a real deploy (DLL ' + $Built + ' has md5 ' + $builtNow + ')') }
  Write-Host ('NOTE: no -ExpectedMd5 given; the DLL md5 is ' + $builtNow + ' (a real deploy needs it passed in)') -ForegroundColor Yellow
} elseif ($builtNow -ne $ExpectedMd5) { Fail ('DLL ' + $Built + ' md5 ' + $builtNow + ' is not ' + $ExpectedMd5) }
if (Has $Rec)  { Fail ('a deploy record already exists (' + $Rec + '): run g3_undo.ps1 first, backups are never overwritten') }
if (Has $Dest) { Fail ('attrib_preset.json already exists (' + $Dest + '): move it away yourself') }
if ((Md5 $Inst) -eq $builtNow) { Fail 'the built DLL is already installed and there is no record of the original' }
if ($OrigMd5s -notcontains (Md5 $Inst)) { Fail ('installed Turbo.dll md5 ' + (Md5 $Inst) + ' is not a known original (' + ($OrigMd5s -join ', ') + ') (a previous deploy not undone?)') }

# ---- -SettingsWrite checks (read-only) ----
$setFile = $null; $setMd5 = ''
if ($SettingsWrite) {
  foreach ($f in @($Apply, $Map)) { if (-not (Has $f)) { Fail ('-SettingsWrite: missing: ' + $f) } }
  if (Has $Pending) { Fail ('-SettingsWrite: ' + $Pending + ' already exists (a staged preset would be applied too): move it away yourself') }
  if (-not (Has $SetDir)) { Fail ('-SettingsWrite: settings folder missing: ' + $SetDir) }
  $sf = @(Get-ChildItem -LiteralPath $SetDir -File -Filter 'Settings*')
  if ($sf.Count -ne 1) { Fail ('-SettingsWrite: expected exactly one Settings* file, found ' + $sf.Count + ': ' + (($sf | ForEach-Object Name) -join ', ')) }
  $setFile = $sf[0]; $setMd5 = Md5 $setFile.FullName
  try { $mj = Get-Content -LiteralPath $Map -Raw | ConvertFrom-Json } catch { Fail ('-SettingsWrite: slider map is not JSON: ' + $_.Exception.Message) }
  if (-not $mj.custom_block) { Fail '-SettingsWrite: the slider map has no custom_block (the tool cannot write the Custom u8 block)' }
  foreach ($k in $Sliders.Keys) {
    $e = @($mj.entries | Where-Object { $_.id -eq $k })
    if ($e.Count -ne 1 -or -not $e[0].verified -or $e[0].custom_index -lt 0) { Fail ('-SettingsWrite: map entry ' + $k + ' is missing / not verified / has no custom_index') }
  }
}

$instMd5 = Md5 $Inst
$preMd5  = Md5 $PresetSrc
Write-Host ('Original installed Turbo.dll md5 : ' + $instMd5)
Write-Host ('DLL to install                   : ' + $builtNow + '  (' + $Built + ')')
Write-Host ('Preset md5 (sentinel unless -Preset): ' + $preMd5 + '  (' + $PresetSrc + ')')
Write-Host ('Evidence folder                  : ' + $Ev)
if ($SettingsWrite) {
  Write-Host ('Settings file                    : ' + $setFile.Name + ' md5 ' + $setMd5)
  Write-Host ('Settings tool                    : ' + $Apply)
  Write-Host ('Sliders to write (Custom u8)     : ' + (($Sliders.Keys | ForEach-Object { $_ + '=' + $Sliders[$_] }) -join ', '))
} else { Write-Host 'Settings                         : not touched (-SettingsWrite not given)' }

if ($DryRun) {
  if ($SettingsWrite) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('g3_settings_dryrun_' + [guid]::NewGuid().ToString('N'))
    try {
      New-Item -ItemType Directory -Path (Join-Path $tmp 'turbo_output') -Force | Out-Null
      New-Item -ItemType Directory -Path (Join-Path $tmp 'settings') -Force | Out-Null
      Copy-Item -LiteralPath $setFile.FullName -Destination (Join-Path $tmp ('settings\' + $setFile.Name)) -Force
      [IO.File]::WriteAllText((Join-Path $tmp 'turbo_output\settings_pending.json'), (PendingJson), (New-Object System.Text.UTF8Encoding($false)))
      Write-Host ('running (temp copy, --dry-run): turbo_settings_apply.exe --apply --dry-run --le-root <temp> --settings-dir <temp>\settings --map ' + $Map)
      & $Apply --apply --dry-run --le-root $tmp --settings-dir (Join-Path $tmp 'settings') --map $Map | ForEach-Object { Write-Host ('  ' + $_) }
      if ($LASTEXITCODE -ne 0) { Write-Host ('SETTINGS DRY RUN: the tool exited ' + $LASTEXITCODE + ' (a real run would be refused)') -ForegroundColor Red }
      else { Write-Host 'SETTINGS DRY RUN: the tool accepts the 3 sliders' -ForegroundColor Green }
    } finally { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue }
  }
  Write-Host 'DRY RUN done: nothing changed.' -ForegroundColor Yellow; exit 0
}
try {
  New-Item -ItemType Directory -Path $Pre -Force | Out-Null
  Copy-Item -LiteralPath $Inst -Destination (Join-Path $Pre 'Turbo.dll') -Force
  if ((Md5 (Join-Path $Pre 'Turbo.dll')) -ne $instMd5) { throw 'DLL backup md5 mismatch' }
  $lines = @(('turbo_dll_original|' + $instMd5), ('turbo_dll_built|' + $builtNow), ('preset_md5|' + $preMd5), ('preset_path|' + $PresetSrc))
  if ($SettingsWrite) {
    New-Item -ItemType Directory -Path (Join-Path $Pre 'settings') -Force | Out-Null
    $bkSet = Join-Path $Pre ('settings\' + $setFile.Name)
    Copy-Item -LiteralPath $setFile.FullName -Destination $bkSet -Force
    if ((Md5 $bkSet) -ne $setMd5) { throw 'Settings backup md5 mismatch' }
    $lines += @(('settings_name|' + $setFile.Name), ('settings_md5|' + $setMd5), ('settings_write|' + (($Sliders.Keys | ForEach-Object { $_ + '=' + $Sliders[$_] }) -join ',')))
  }
  $lines += ('time|' + (Get-Date).ToString('s'))
  [IO.File]::WriteAllLines($Rec, [string[]]$lines)
  Copy-Item -LiteralPath $Built -Destination $Inst -Force
  if ((Md5 $Inst) -ne $builtNow) { throw 'installed DLL md5 mismatch after copy' }
  Copy-Item -LiteralPath $PresetSrc -Destination $Dest -Force
  if ((Md5 $Dest) -ne $preMd5) { throw 'preset md5 mismatch after copy' }
  if ($SettingsWrite) {
    [IO.File]::WriteAllText($Pending, (PendingJson), (New-Object System.Text.UTF8Encoding($false)))
    & $Apply --apply --le-root $LE --map $Map | ForEach-Object { Write-Host ('  ' + $_) }
    if ($LASTEXITCODE -ne 0) { throw ('turbo_settings_apply.exe --apply exit code ' + $LASTEXITCODE) }
    $sfa = @(Get-ChildItem -LiteralPath $SetDir -File -Filter 'Settings*')
    [IO.File]::AppendAllText($Rec, ('settings_after|' + (($sfa | ForEach-Object { $_.Name + ':' + (Md5 $_.FullName) }) -join ',') + "`r`n"))
    Write-Host ('Settings after the write: ' + (($sfa | ForEach-Object { $_.Name + ' md5 ' + (Md5 $_.FullName) }) -join ', '))
    & $Apply --status --le-root $LE --map $Map | Select-Object -First 3 | ForEach-Object { Write-Host ('  ' + $_) }
  }
} catch {
  Write-Host ('DEPLOY FAILED midway: ' + $_.Exception.Message + ' - run g3_undo.ps1') -ForegroundColor Red
  exit 2
}
Write-Host ('G3 DEPLOYED. Installed ' + (Md5 $Inst) + ', preset ' + (Md5 $Dest) + $(if ($SettingsWrite) { ', settings written' } else { '' }) + '. Undo: g3_undo.ps1 -Evidence <dir> after the test (kill the game first). Record: ' + $Rec) -ForegroundColor Green
exit 0
