# Approved launch (Cassio 2026-10-12): the two tasks of LaunchFC27.bat, with a wait for Live Editor's anti-cheat swap in between.
# 1. wait until no LauncherNoAuth / FC27 is running (an old elevated launcher window must be closed by Cassio)
# 2. schtasks /run /tn LE_NoUAC
# 3. wait for "InstallFakeAnticheat] Done" in today's launcher log after this start, refuse if "error remove" was logged
# 4. schtasks /run /tn FC_NoUAC, wait for the game window
# WP-C (tools/ in git): refuses (exit 6, nothing started) unless TOOLS\GAME_STATE.txt says LAUNCHING - write it first (brief C.3).
# Exit: 0 game window | 2 an old launcher / game still running | 3 LE could not install its swap | 4 LE launcher timeout |
#       5 game window timeout | 6 refused (GAME_STATE is not LAUNCHING)
param([int]$WaitCloseMin = 40)
$gsPath = if ($env:TURBO_GAME_STATE) { $env:TURBO_GAME_STATE } else { "C:\FC 27 Live Editor\turbo_dev\tools\GAME_STATE.txt" }
$gsLine = if (Test-Path $gsPath) { (Get-Content $gsPath -TotalCount 1) } else { "" }
if ($gsLine -notmatch '^\s*state:\s*LAUNCHING\b') {
  "launch_fc27.ps1: refused - GAME_STATE is not LAUNCHING (${gsPath}: '$gsLine'). Write it first (brief C.3)."
  exit 6
}
$log = "C:\FC 27 Live Editor\Logs\live_editor_launcher_{0}.log" -f (Get-Date -Format "dd-MM-yyyy")
$deadline = (Get-Date).AddMinutes($WaitCloseMin)
while (Get-Process LauncherNoAuth, FC27 -ErrorAction SilentlyContinue) {
  if ((Get-Date) -gt $deadline) { "TIMEOUT: an old launcher / game is still running"; exit 2 }
  Start-Sleep 10
}
"old launcher closed at $(Get-Date -Format HH:mm:ss)"
$before = if (Test-Path $log) { (Get-Content $log).Count } else { 0 }
schtasks /run /tn "LE_NoUAC" | Out-Null
$ok = $false; $t = (Get-Date).AddMinutes(5)
while ((Get-Date) -lt $t) {
  Start-Sleep 2
  if (-not (Test-Path $log)) { continue }
  $new = Get-Content $log | Select-Object -Skip $before
  if ($new -match "InstallFakeAnticheat\] error") { "LE could not install its anti-cheat swap: " + (($new -match "InstallFakeAnticheat\] error") | Select-Object -First 1); exit 3 }
  if ($new -match "InstallFakeAnticheat\] Done") { $ok = $true; break }
}
if (-not $ok) { "TIMEOUT waiting for the LE launcher"; exit 4 }
"LE launcher ready at $(Get-Date -Format HH:mm:ss)"
Start-Sleep 3
schtasks /run /tn "FC_NoUAC" | Out-Null
$t = (Get-Date).AddMinutes(4)
while ((Get-Date) -lt $t) {
  $p = Get-Process FC27 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -like '*FC 27*' }
  if ($p) { "game window at $(Get-Date -Format HH:mm:ss)"; exit 0 }
  Start-Sleep 3
}
"TIMEOUT waiting for the game window"; exit 5
