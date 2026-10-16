# g3_undo.ps1 -Evidence <dir> [-DryRun]   (Windows PowerShell 5.1, ASCII only)
# Undoes g3_deploy.ps1 with the game CLOSED: copies turbo_output\attrib_ebx_log.txt, attrib_ebx_locs.json and attrib_preset.json into <dir>
# (md5-verified), restores the original Turbo.dll from launchG3\pre (md5-verified against the record), removes attrib_preset.json and the
# attrib_ebx log / locs cache (only after their evidence copy verified), then prints PASS/FAIL. Exit 0 = PASS, 1 = refused, 2 = FAIL.
param([Parameter(Mandatory = $true)][string]$Evidence, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
$LE   = 'C:\FC 27 Live Editor'
$Day  = '2026-10-15'
$Out  = Join-Path $LE 'turbo_output'
$Inst = Join-Path $LE 'turbo\Turbo.dll'
$Ev   = Join-Path $LE ('turbo_dev\evidence\{0}\launchG3' -f $Day)
$Bak  = Join-Path $Ev 'pre\Turbo.dll'
$Rec  = Join-Path $Ev 'g3_md5.txt'
$Gone = @('attrib_preset.json', 'attrib_ebx_log.txt', 'attrib_ebx_locs.json')
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash } else { 'ABSENT' } }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; exit 1 }
function Has([string]$p) { Test-Path -LiteralPath $p }
if ($DryRun) { Write-Host '*** DRY RUN: nothing is changed ***' -ForegroundColor Yellow }
$run = @(Get-Process -Name FC27, LauncherNoAuth -ErrorAction SilentlyContinue)
if ($run.Count -gt 0) { Fail ('game/launcher is running: ' + (($run | ForEach-Object { $_.Name + ' pid ' + $_.Id }) -join ', ') + ' (kill it first)') }
if (-not (Has $Rec)) { Fail ('no deploy record at ' + $Rec + ' - nothing to undo') }
$recd = @{}
foreach ($l in (Get-Content -LiteralPath $Rec)) { $i = $l.IndexOf('|'); if ($i -gt 0) { $recd[$l.Substring(0, $i)] = $l.Substring($i + 1) } }
if ((Md5 $Bak) -ne $recd['turbo_dll_original']) { Fail ('DLL backup md5 ' + (Md5 $Bak) + ' != recorded ' + $recd['turbo_dll_original']) }
Write-Host ('Installed now: ' + (Md5 $Inst) + ', restoring ' + $recd['turbo_dll_original'] + '; evidence -> ' + $Evidence)
foreach ($g in $Gone) { Write-Host ('  ' + $g + ': ' + (Md5 (Join-Path $Out $g))) }
$ok = $true; $notes = @()
if (-not $DryRun) {
  $copied = @{}
  try { New-Item -ItemType Directory -Path $Evidence -Force | Out-Null } catch { $notes += ('evidence dir: ' + $_.Exception.Message); $ok = $false }
  foreach ($g in $Gone) {
    $src = Join-Path $Out $g
    if (-not (Has $src)) { continue }
    try {
      $dst = Join-Path $Evidence $g
      Copy-Item -LiteralPath $src -Destination $dst -Force
      if ((Md5 $dst) -ne (Md5 $src)) { throw 'copy md5 mismatch' }
      $copied[$g] = $true
    } catch { $notes += ('evidence copy of ' + $g + ' failed (left in place): ' + $_.Exception.Message); $ok = $false }
  }
  try { Copy-Item -LiteralPath $Bak -Destination $Inst -Force } catch { $notes += ('DLL restore: ' + $_.Exception.Message); $ok = $false }
  foreach ($g in $Gone) {
    $src = Join-Path $Out $g
    if ((Has $src) -and $copied[$g]) { try { Remove-Item -LiteralPath $src -Force } catch { $notes += ('remove ' + $g + ': ' + $_.Exception.Message); $ok = $false } }
  }
}
$checks = @()
$checks += [pscustomobject]@{ Check = 'Turbo.dll == recorded original'; Result = $(if ((Md5 $Inst) -eq $recd['turbo_dll_original']) { 'PASS' } else { 'FAIL' }); Detail = ((Md5 $Inst) + ' vs ' + $recd['turbo_dll_original']) }
foreach ($g in $Gone) { $checks += [pscustomobject]@{ Check = ($g + ' absent'); Result = $(if (-not (Has (Join-Path $Out $g))) { 'PASS' } else { 'FAIL' }); Detail = '' } }
$checks | Format-Table -AutoSize | Out-String -Width 250 | Write-Host
$notes | ForEach-Object { Write-Host ('NOTE: ' + $_) -ForegroundColor Yellow }
if ($DryRun) { Write-Host 'DRY RUN done: nothing changed (FAIL rows are expected before an undo).' -ForegroundColor Yellow; exit 0 }
$bad = @($checks | Where-Object { $_.Result -eq 'FAIL' })
if ($bad.Count -eq 0 -and $ok) { Write-Host 'UNDO: PASS' -ForegroundColor Green; exit 0 }
Write-Host 'UNDO: FAIL' -ForegroundColor Red
exit 2
