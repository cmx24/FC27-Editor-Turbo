# test_installer.ps1 -Work <scratch dir> [-Package <turbo\package>]   (Windows PowerShell 5.1, ASCII only)
# Round-trip test of Install-Turbo.ps1 / Undo-Turbo.ps1 on a FAKE Live Editor folder built under -Work. Never touches a real install.
# Asserts: dry run writes nothing; install verifies; user data + saves + masters + kits identical; blocked entries skipped;
# undo restores the md5 of EVERY file of the fake folder (incl. the file set) except the turbo_backups folder.
param([Parameter(Mandatory = $true)][string]$Work, [string]$Package = '')
$ErrorActionPreference = 'Stop'
if ($Package -eq '') { $Package = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\package' }
$Package = (Resolve-Path $Package).ProviderPath
if ($Work -match '^C:\\FC 27 Live Editor(\\|$)' -and $Work -notmatch 'scratchpad') { throw 'refusing to run in the real install folder' }
$fake = Join-Path $Work 'fake_le'; $src = Join-Path $Work 'pkg'
foreach ($d in $fake, $src) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
function W([string]$root, [string]$rel, [string]$txt) { $p = Join-Path $root $rel; New-Item -ItemType Directory -Path (Split-Path $p) -Force | Out-Null; [IO.File]::WriteAllText($p, $txt) }
function Tree([string]$root) {
  $h = @{}
  foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File -Force) {
    $rel = $f.FullName.Substring($root.Length + 1)
    if ($rel -like 'turbo_backups\*') { continue }
    $h[$rel] = (Get-FileHash -LiteralPath $f.FullName -Algorithm MD5).Hash
  }
  return $h
}
$fails = 0
function Check([bool]$ok, [string]$m) { if ($ok) { Write-Host ('  ok   ' + $m) } else { Write-Host ('  FAIL ' + $m) -ForegroundColor Red; $script:fails++ } }

# package under test: the real package tree + a fake Turbo.dll + hostile entries that must be skipped
Copy-Item -LiteralPath $Package -Destination $src -Recurse
W $src 'turbo\Turbo.dll' 'NEW-DLL-BYTES'
W $src 'FCLiveEditor.DLL' 'EVIL-REPLACEMENT'
W $src 'careersave\Career1' 'EVIL-SAVE'
W $src 'settings\Settings' 'EVIL-SETTINGS'
W $src 'turbo_output\reapply_edits.json' '{"package":"default"}'
W $src 'turbo_output\new_in_package.txt' 'added'
W $src 'kits\pack\kit.zip' 'package-kit'
# fake install: old Turbo, user data, saves, masters, kits, LE files
W $fake 'FCLiveEditor.DLL' 'LE-DLL'; W $fake 'le_offsets.json' '{"le":1}'
W $fake 'turbo\Turbo.dll' 'OLD-DLL-BYTES'; W $fake 'turbo\settings\old_preset.json' 'old'
W $fake 'turbo\tuning\knobs.json' 'old-knobs'
W $fake 'lua\autorun\old_script.lua' '-- old'
W $fake 'turbo_config.json' '{"user":"custom"}'
W $fake 'turbo_output\reapply_edits.json' '{"user":"my kits rows"}'
W $fake 'turbo_output\turbo_config.json' '{"user":"out cfg"}'
W $fake 'turbo_output\user_file.txt' 'mine'
W $fake 'turbo\callnames\masters\en.json' '{"names":"local"}'
W $fake 'kits\pack\kit.zip' 'user-kit'
W $fake 'careersave\Career1' 'MY-SAVE'; W $fake 'settings\Settings' 'MY-SETTINGS'
W $fake 'turbo_dev\notes.txt' 'dev'
# an older Turbo: 25 package files already installed with other bytes (the upgrade overwrites them)
$old = @(Get-ChildItem -LiteralPath $Package -Recurse -File | Where-Object { $_.FullName.Substring($Package.Length + 1) -notmatch '^(turbo_output|install|turbo_config)' } | Select-Object -First 25)
foreach ($f in $old) { W $fake $f.FullName.Substring($Package.Length + 1) ('OLD-VERSION ' + $f.Name) }
$before = Tree $fake
$dirsBefore = @(Get-ChildItem -LiteralPath $fake -Recurse -Directory | ForEach-Object { $_.FullName.Substring($fake.Length + 1) } | Sort-Object)
$inst = Join-Path $src 'install\Install-Turbo.ps1'

Write-Host 'dry run'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inst -LeRoot $fake -DryRun | Out-Host
Check ($LASTEXITCODE -eq 0) 'dry run exit 0'
$t = Tree $fake; Check ((($t.Keys | Sort-Object) -join '|') -eq (($before.Keys | Sort-Object) -join '|')) 'dry run: file set unchanged'
Check (-not (Test-Path (Join-Path $fake 'turbo_backups'))) 'dry run: no backup folder'

Write-Host 'refuses a folder that is not a Live Editor'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inst -LeRoot $Work -Yes | Out-Host
Check ($LASTEXITCODE -eq 1) 'wrong folder refused (exit 1)'

Write-Host 'install'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inst -LeRoot $fake -Yes -ExpectedDllMd5 (Get-FileHash (Join-Path $src 'turbo\Turbo.dll') -Algorithm MD5).Hash | Out-Host
Check ($LASTEXITCODE -eq 0) 'install exit 0'
$after = Tree $fake
Check ($after['turbo\Turbo.dll'] -eq (Get-FileHash (Join-Path $src 'turbo\Turbo.dll') -Algorithm MD5).Hash) 'Turbo.dll updated'
foreach ($k in 'turbo_config.json', 'turbo_output\reapply_edits.json', 'turbo_output\turbo_config.json', 'turbo_output\user_file.txt', 'turbo\callnames\masters\en.json', 'kits\pack\kit.zip', 'careersave\Career1', 'settings\Settings', 'FCLiveEditor.DLL', 'le_offsets.json', 'turbo_dev\notes.txt') {
  Check ($after[$k] -eq $before[$k]) ('untouched: ' + $k)
}
Check ($after.ContainsKey('turbo_output\new_in_package.txt')) 'new file in turbo_output added'
Check (-not $after.ContainsKey('install\Install-Turbo.ps1')) 'installer itself is not copied into the Live Editor folder'
$bd = @(Get-ChildItem (Join-Path $fake 'turbo_backups') -Directory)
Check ($bd.Count -eq 1) 'one backup folder'
Check ((Test-Path (Join-Path $bd[0].FullName 'Undo-Turbo.ps1')) -and (Test-Path (Join-Path $bd[0].FullName 'Undo-Turbo.cmd')) -and (Test-Path (Join-Path $bd[0].FullName 'install_record.txt'))) 'undo script + record written'

Write-Host 'undo (the script inside the backup folder)'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $bd[0].FullName 'Undo-Turbo.ps1') | Out-Host
Check ($LASTEXITCODE -eq 0) 'undo exit 0'
$restored = Tree $fake
$diff = @(); foreach ($k in ($before.Keys + $restored.Keys | Sort-Object -Unique)) { if ($before[$k] -ne $restored[$k]) { $diff += $k } }
Check ($diff.Count -eq 0) ('round trip: all ' + $before.Count + ' files md5-identical, file set identical' + $(if ($diff.Count) { ' (differs: ' + ($diff -join ', ') + ')' }))
$dirsNow = @(Get-ChildItem -LiteralPath $fake -Recurse -Directory | ForEach-Object { $_.FullName.Substring($fake.Length + 1) } | Where-Object { $_ -notlike 'turbo_backups*' } | Sort-Object)
Check (($dirsNow -join '|') -eq ($dirsBefore -join '|')) 'folder set identical (folders created by the install removed)'

Write-Host 'second install over the restored state (history stays separate)'
Start-Sleep -Seconds 1
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inst -LeRoot $fake -Yes | Out-Host
Check ($LASTEXITCODE -eq 0) 'reinstall exit 0'
Check (@(Get-ChildItem (Join-Path $fake 'turbo_backups') -Directory).Count -eq 2) 'two backup folders'

Write-Host 'third install (X 2.0): the log file, then the cleanup: 2 backups kept, a stale older-version file kept in the newest backup, big log rotated, newest launcher kept'
$stalerel = 'turbo\old_feature.dll'
W $fake $stalerel 'OLD-FEATURE'
$oldest = @(Get-ChildItem (Join-Path $fake 'turbo_backups') -Directory | Sort-Object Name)[0].FullName
Add-Content -LiteralPath (Join-Path $oldest 'install_record.txt') -Value ('installed|' + $stalerel + '|' + (Get-FileHash (Join-Path $fake $stalerel) -Algorithm MD5).Hash)
[IO.File]::WriteAllBytes((Join-Path $fake 'turbo_output\turbo_gui.log'), (New-Object byte[] (26MB)))
W $fake '_INSTALL_TURBOX_A.cmd' 'old'
(Get-Item (Join-Path $fake '_INSTALL_TURBOX_A.cmd')).LastWriteTime = (Get-Date).AddDays(-2)
W $fake '_INSTALL_TURBOX_B.cmd' 'new'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fake 'turbo\Cleanup-Turbo.ps1') -LeRoot $fake | Out-Host
Check ($LASTEXITCODE -eq 0 -and (Test-Path (Join-Path $fake $stalerel)) -and (Test-Path (Join-Path $fake '_INSTALL_TURBOX_A.cmd'))) 'cleanup without -Apply lists only (nothing changed)'
Start-Sleep -Seconds 1
$log = Join-Path $Work 'install_log.txt'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inst -LeRoot $fake -Yes -LogFile $log | Out-Host
Check ($LASTEXITCODE -eq 0) 'third install exit 0'
Check ((Test-Path $log) -and ((Get-Content -LiteralPath $log -Raw) -match 'INSTALL PASS') -and ((Get-Content -LiteralPath $log -Raw) -match '\[6/7\]')) 'the log file has the steps and INSTALL PASS'
$bd3 = @(Get-ChildItem (Join-Path $fake 'turbo_backups') -Directory | Sort-Object Name -Descending)
Check ($bd3.Count -eq 2) 'cleanup kept the 2 newest install backups'
Check (-not (Test-Path (Join-Path $fake $stalerel))) 'stale older-version file removed'
Check ((Test-Path (Join-Path $bd3[0].FullName ('files\' + $stalerel))) -and ((Get-Content (Join-Path $bd3[0].FullName 'install_record.txt')) -contains ('backup|' + $stalerel + '|' + (Get-FileHash (Join-Path $bd3[0].FullName ('files\' + $stalerel)) -Algorithm MD5).Hash))) 'stale file kept in the newest backup and its record'
Check ((Test-Path (Join-Path $fake 'turbo_output\turbo_gui.log.old')) -and -not (Test-Path (Join-Path $fake 'turbo_output\turbo_gui.log'))) 'big log rotated to .old'
Check ((Test-Path (Join-Path $fake '_INSTALL_TURBOX_B.cmd')) -and -not (Test-Path (Join-Path $fake '_INSTALL_TURBOX_A.cmd'))) 'only the newest launcher kept'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $bd3[0].FullName 'Undo-Turbo.ps1') | Out-Host
Check ($LASTEXITCODE -eq 0 -and (Test-Path (Join-Path $fake $stalerel))) 'undo of the newest install puts the stale file back'

Write-Host ''
if ($fails -eq 0) { Write-Host 'ALL PASS' -ForegroundColor Green; exit 0 } else { Write-Host ($fails.ToString() + ' FAILED') -ForegroundColor Red; exit 1 }
