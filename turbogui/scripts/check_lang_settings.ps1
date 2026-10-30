# 4.0 lang-settings: scripted live check for the Tester (about 10 s, READ ONLY, prints PASS / FAIL).
#
# What it proves: the commentary language Turbo logged agrees with the commentary language field (u32 at file offset 0x21AE) of the
# newest Settings<17 digits> file the game saved. It never writes, never launches the game, never touches the registry.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File check_lang_settings.ps1 [-SettingsDir <dir>] [-Log <turbo_gui.log>]
#
# Defaults: SettingsDir = %LOCALAPPDATA%\EA SPORTS FC 27\settings, Log = C:\FC 27 Live Editor\turbo_output\turbo_gui.log
# Value table (core/callnames.cpp settings_lang_for_value): 1 dut_nl, 2 eng_us, 3 fre_fr. Any other value is unknown: Turbo must log
# "Settings file value N: language not known yet" and the check passes only if it does.
# Exit code: 0 = PASS, 1 = FAIL, 2 = nothing to check (no Settings file / no Turbo log line yet).
param(
    [string]$SettingsDir = (Join-Path $env:LOCALAPPDATA 'EA SPORTS FC 27\settings'),
    [string]$Log = 'C:\FC 27 Live Editor\turbo_output\turbo_gui.log'
)
$ErrorActionPreference = 'Stop'
$table = @{ 1 = 'dut_nl'; 2 = 'eng_us'; 3 = 'fre_fr' }
$kFileSize = 772790
$kLangOff = 0x21AE

function Out-Result([string]$verdict, [string]$why, [int]$code) {
    Write-Host ("{0}: {1}" -f $verdict, $why)
    exit $code
}

if (-not (Test-Path -LiteralPath $SettingsDir)) { Out-Result 'NOTHING' "no settings folder $SettingsDir" 2 }
$files = @(Get-ChildItem -LiteralPath $SettingsDir -File | Where-Object { $_.Name -match '^Settings\d{17}$' } |
    Sort-Object @{ Expression = { $_.LastWriteTimeUtc }; Descending = $true }, @{ Expression = { $_.Name }; Descending = $true })
if ($files.Count -eq 0) { Out-Result 'NOTHING' "no Settings<17 digits> file in $SettingsDir" 2 }
$f = $files[0]
if ($f.Length -ne $kFileSize) { Out-Result 'FAIL' ("{0} is {1} bytes, expected {2}: not a Settings file Turbo can read" -f $f.Name, $f.Length, $kFileSize) 1 }
$fs = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
try { $b = New-Object byte[] 4; $fs.Position = $kLangOff; [void]$fs.Read($b, 0, 4) } finally { $fs.Dispose() }
$value = [BitConverter]::ToUInt32($b, 0)
$expect = $table[[int]$value]
Write-Host ("newest Settings file {0} ({1}): commentary field at 0x21AE = {2} -> {3}" -f $f.Name, $f.LastWriteTime.ToString('HH:mm:ss'), $value, $(if ($expect) { $expect } else { 'not in the table' }))

if (-not (Test-Path -LiteralPath $Log)) { Out-Result 'NOTHING' "no Turbo log $Log (start the game with Turbo first)" 2 }
$lines = @(Select-String -LiteralPath $Log -Pattern 'callnames: language ' | ForEach-Object { $_.Line })
if ($lines.Count -eq 0) { Out-Result 'NOTHING' "no 'callnames: language' line in $Log yet" 2 }
# the newest line that names this file; else the newest line (the file may be newer than Turbo's last refresh)
$mine = @($lines | Where-Object { $_ -match [regex]::Escape($f.Name) })
$line = if ($mine.Count -gt 0) { $mine[-1] } else { $lines[-1] }
Write-Host ("Turbo log: {0}" -f $line)
if ($mine.Count -eq 0) {
    Out-Result 'FAIL' "Turbo has not read $($f.Name) yet (its newest language line names another file or none: wait 5 s after the game saved, then run again)" 1
}
if ($expect) {
    if ($line -match ('callnames: language ' + $expect + ' ') -and $line -match ('field = ' + $value + ' -> ' + $expect)) {
        Out-Result 'PASS' "Turbo answers $expect from $($f.Name) (field = $value)" 0
    }
    Out-Result 'FAIL' "the file says $value = $expect, Turbo logged something else" 1
}
if ($line -match ('Settings file value ' + $value + ': language not known yet')) {
    Out-Result 'PASS' "value $value is not in the table and Turbo says so (fallback named in the log line)" 0
}
Out-Result 'FAIL' "value $value is not in the table but Turbo did not log 'language not known yet'" 1
