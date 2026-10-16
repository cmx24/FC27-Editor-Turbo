# g3_deploy.ps1 [-DryRun] [-BuiltMd5 <md5>]   (Windows PowerShell 5.1, ASCII only)
# Launch 3 (attribulator EBX patch, branch 2.10/attrib-ebx): backs up the installed Turbo.dll (md5 recorded), installs the built DLL and
# turbo_output\attrib_preset.json (the sentinel preset). Run with the game CLOSED. Never launches or touches the game.
# Refuses: game/launcher running, turbo_output\kits3d_on.txt present (the 3D kit swap owns the Oodle hook), attrib_ebx_off.txt present,
# a deploy record already there, an attrib_preset.json already there, wrong built-DLL md5.
# Exit 0 = deployed, 1 = refused (nothing written), 2 = failed midway (run g3_undo.ps1).
param([switch]$DryRun, [string]$BuiltMd5 = '7999BEF5057B234C8C4884D04D8942CD')
$ErrorActionPreference = 'Stop'
$LE     = 'C:\FC 27 Live Editor'
$Day    = '2026-10-15'
$Out    = Join-Path $LE 'turbo_output'
$Inst   = Join-Path $LE 'turbo\Turbo.dll'
$WT     = Join-Path $LE 'turbo_dev\2.0\wt-30-ebx'
$Built  = Join-Path $WT 'turbogui\build\win\Turbo.dll'
$Preset = Join-Path $WT 'tools\attrib_preset_sentinel.json'
$Dest   = Join-Path $Out 'attrib_preset.json'
$Kits   = Join-Path $Out 'kits3d_on.txt'
$Off    = Join-Path $Out 'attrib_ebx_off.txt'
$Ev     = Join-Path $LE ('turbo_dev\evidence\{0}\launchG3' -f $Day)
$Pre    = Join-Path $Ev 'pre'
$Rec    = Join-Path $Ev 'g3_md5.txt'
$BuiltMd5 = $BuiltMd5.ToUpperInvariant()
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash } else { 'ABSENT' } }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; exit 1 }
function Has([string]$p) { Test-Path -LiteralPath $p }
if ($BuiltMd5 -notmatch '^[0-9A-F]{32}$') { Fail ('-BuiltMd5 is not an md5: ' + $BuiltMd5) }
if ($DryRun) { Write-Host '*** DRY RUN: nothing is changed ***' -ForegroundColor Yellow }

$run = @(Get-Process -Name FC27, LauncherNoAuth -ErrorAction SilentlyContinue)
if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ')) }
foreach ($f in @($Inst, $Built, $Preset)) { if (-not (Has $f)) { Fail ('missing: ' + $f) } }
if (Has $Kits) { Fail ('kits3d_on.txt exists (' + $Kits + '): the 3D kit swap owns the Oodle hook and the attrib patch would be off; remove it yourself') }
if (Has $Off)  { Fail ('attrib_ebx_off.txt exists (' + $Off + '): the patch would be off; remove it yourself') }
if ((Md5 $Built) -ne $BuiltMd5) { Fail ('built DLL md5 ' + (Md5 $Built) + ' is not ' + $BuiltMd5) }
if (Has $Rec)  { Fail ('a deploy record already exists (' + $Rec + '): run g3_undo.ps1 first, backups are never overwritten') }
if (Has $Dest) { Fail ('attrib_preset.json already exists (' + $Dest + '): move it away yourself') }
if ((Md5 $Inst) -eq $BuiltMd5) { Fail 'the built DLL is already installed and there is no record of the original' }

$instMd5 = Md5 $Inst
$preMd5  = Md5 $Preset
Write-Host ('Original installed Turbo.dll md5 : ' + $instMd5)
Write-Host ('Built Turbo.dll md5              : ' + $BuiltMd5)
Write-Host ('Sentinel preset md5              : ' + $preMd5)
Write-Host ('Evidence folder                  : ' + $Ev)
if ($DryRun) { Write-Host 'DRY RUN done: nothing changed.' -ForegroundColor Yellow; exit 0 }
try {
  New-Item -ItemType Directory -Path $Pre -Force | Out-Null
  Copy-Item -LiteralPath $Inst -Destination (Join-Path $Pre 'Turbo.dll') -Force
  if ((Md5 (Join-Path $Pre 'Turbo.dll')) -ne $instMd5) { throw 'DLL backup md5 mismatch' }
  [IO.File]::WriteAllLines($Rec, [string[]]@(('turbo_dll_original|' + $instMd5), ('turbo_dll_built|' + $BuiltMd5), ('preset_md5|' + $preMd5), ('time|' + (Get-Date).ToString('s'))))
  Copy-Item -LiteralPath $Built -Destination $Inst -Force
  if ((Md5 $Inst) -ne $BuiltMd5) { throw 'installed DLL md5 mismatch after copy' }
  Copy-Item -LiteralPath $Preset -Destination $Dest -Force
  if ((Md5 $Dest) -ne $preMd5) { throw 'preset md5 mismatch after copy' }
} catch {
  Write-Host ('DEPLOY FAILED midway: ' + $_.Exception.Message + ' - run g3_undo.ps1') -ForegroundColor Red
  exit 2
}
Write-Host ('G3 DEPLOYED. Installed ' + (Md5 $Inst) + ', preset ' + (Md5 $Dest) + '. Undo: g3_undo.ps1 -Evidence <dir> after the test (kill the game first). Record: ' + $Rec) -ForegroundColor Green
exit 0
