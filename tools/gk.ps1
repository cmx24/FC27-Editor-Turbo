param([string]$cmd, [string]$arg = "", [int]$ms = 90, [int]$count = 1, [int]$mode = 2)
# Game helper (coordinator only, with Cassio's chat "go" at session start; brief C.1 rule 9):
#   shot <path> | front | key <name> [ms] [count] | click x,y | type <text> | selall | clickhere | seq <steps> | wheel x,y,n | move x,y
# Escape is never sent (it tears down the Claude session): refused by name here AND for VK 0x1B in the SendInput layer; `type` refuses
# control characters. Every command except `shot` refuses (exit 3) unless TOOLS\GAME_STATE.txt names a live coordinator state
# (LAUNCHING / HUB / MATCH / MATCH-AB): while it says CLOSED the game on this PC may be Cassio's own (WP-C, tools/ in git).
$gsPath = if ($env:TURBO_GAME_STATE) { $env:TURBO_GAME_STATE } else { "C:\FC 27 Live Editor\turbo_dev\tools\GAME_STATE.txt" }
if ($cmd -ne "shot") {
  $line = if (Test-Path $gsPath) { (Get-Content $gsPath -TotalCount 1) } else { "" }
  $tok = if ($line -match '^\s*state:\s*(\S+)') { $Matches[1].ToUpper() } else { "MISSING" }
  if (@("LAUNCHING", "HUB", "MATCH", "MATCH-AB") -notcontains $tok) {
    "gk.ps1: refused - GAME_STATE is $tok ($gsPath): no input is sent outside a coordinator session"
    "VERDICT INCONCLUSIVE"
    exit 3
  }
}
$src = @"
using System;
using System.Runtime.InteropServices;
public static class GK {
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U u; }
  [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] i, int cb);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  public static void Uni(char c) {
    if (c < (char)0x20 || c == (char)0x7F) return;  // no control characters (ESC is 0x1B)
    INPUT[] a = new INPUT[2];
    a[0].type = 1; a[0].u.ki.wScan = c; a[0].u.ki.dwFlags = 4u;
    a[1].type = 1; a[1].u.ki.wScan = c; a[1].u.ki.dwFlags = 4u | 2u;
    SendInput(2, a, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void VkRaw(ushort vk, bool up) {
    if (vk == 0x1B) return;  // Escape is never sent
    INPUT[] a = new INPUT[1];
    a[0].type = 1; a[0].u.ki.wVk = vk; a[0].u.ki.wScan = (ushort)MapVirtualKey(vk, 0); a[0].u.ki.dwFlags = up ? 2u : 0u;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
  public static uint Key(ushort vk, bool up, bool ext, int mode) {
    if (vk == 0x1B) return 0;  // Escape is never sent
    INPUT[] a = new INPUT[1];
    a[0].type = 1;
    ushort sc = (ushort)MapVirtualKey(vk, 0);
    uint f = up ? 2u : 0u;
    if (mode == 1) { a[0].u.ki.wVk = vk; a[0].u.ki.wScan = 0; if (ext) f |= 1u; }
    else if (mode == 2) { a[0].u.ki.wVk = 0; a[0].u.ki.wScan = sc; f |= 8u; if (ext) f |= 1u; }
    else if (mode == 3) { a[0].u.ki.wVk = vk; a[0].u.ki.wScan = sc; }
    else { a[0].u.ki.wVk = vk; a[0].u.ki.wScan = sc; if (ext) f |= 1u; }
    a[0].u.ki.dwFlags = f;
    return SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
Add-Type -TypeDefinition $src -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
[GK]::SetProcessDPIAware() | Out-Null
function Front {
  $p = Get-Process FC27 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $p) { "no FC27 window"; return $false }
  [GK]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
  Start-Sleep -Milliseconds 250
  return ([GK]::GetForegroundWindow() -eq $p.MainWindowHandle)
}
$vks = @{ "return"=0x0D; "enter"=0x0D; "up"=0x26; "down"=0x28; "left"=0x25; "right"=0x27; "space"=0x20; "x"=0x58; "c"=0x43; "q"=0x51; "s"=0x53; "d"=0x44; "backspace"=0x08; "f8"=0x77; "add"=0x6B; "tab"=0x09; "shift"=0x10 }
switch ($cmd) {
  "front" { "front: " + (Front) }
  "shot" {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $w = 1400; $h = [int]($b.Height * $w / $b.Width)
    $s = New-Object System.Drawing.Bitmap $w, $h
    $g2 = [System.Drawing.Graphics]::FromImage($s)
    $g2.InterpolationMode = 'HighQualityBicubic'
    $g2.DrawImage($bmp, 0, 0, $w, $h)
    $s.Save($arg, [System.Drawing.Imaging.ImageFormat]::Jpeg)
    "saved $arg ($($b.Width)x$($b.Height) -> ${w}x$h)"
  }
  "key" {
    $k = $arg.ToLower()
    if ($k -eq "escape" -or $k -eq "esc") { "refused: Escape is never sent"; break }
    if (-not $vks.ContainsKey($k)) { "unknown key $k"; break }
    $f = Front
    if (-not $f) { "game not in front: no key sent"; break }
    $vk = [uint16]$vks[$k]
    $ext = @(0x26,0x28,0x25,0x27) -contains $vk
    for ($i = 0; $i -lt $count; $i++) {
      [GK]::Key($vk, $false, $ext, $mode) | Out-Null
      Start-Sleep -Milliseconds $ms
      [GK]::Key($vk, $true, $ext, $mode) | Out-Null
      Start-Sleep -Milliseconds 350
    }
    "sent $k x$count (${ms} ms, mode $mode)"
  }
  "click" {
    # arg = "x,y" in 1400-wide screenshot coordinates
    $f = Front
    if (-not $f) { "game not in front: no click"; break }
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $xy = $arg.Split(',')
    $x = [int]([double]$xy[0] * $b.Width / 1400); $y = [int]([double]$xy[1] * $b.Width / 1400)
    [GK]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 200
    [GK]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    [GK]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero)
    "clicked $x,$y"
  }
  "type" {
    if ($arg -match '[\x00-\x1F\x7F]') { "refused: control characters are never typed"; break }
    $f = Front
    if (-not $f) { "game not in front: nothing typed"; break }
    foreach ($ch in $arg.ToCharArray()) { [GK]::Uni($ch); Start-Sleep -Milliseconds 60 }
    "typed '$arg'"
  }
  "selall" {
    $f = Front
    if (-not $f) { "game not in front"; break }
    [GK]::VkRaw(0x11, $false); Start-Sleep -Milliseconds 60; [GK]::VkRaw(0x41, $false); Start-Sleep -Milliseconds 80
    [GK]::VkRaw(0x41, $true); Start-Sleep -Milliseconds 60; [GK]::VkRaw(0x11, $true)
    "ctrl+a"
  }
  "clickhere" {
    $f = Front
    if (-not $f) { "game not in front: no click"; break }
    [GK]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    [GK]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero)
    "clicked in place"
  }
  "seq" {
    # arg = comma list of steps: key:<name>[:mode[:ms]] | wait:<ms> | click:<x>:<y> | here (click in place)
    $f = Front
    if (-not $f) { "game not in front: nothing sent"; break }
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $out = @()
    # the game must still be in front before EVERY input step: if the user switched to another window, stop at once
    # (2026-10-13: keys of a long sequence reached Cassio's browser after he switched windows mid-sequence)
    $gh = (Get-Process FC27 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
    foreach ($st in $arg.Split(',')) {
      $p = $st.Trim().Split(':')
      if ($p[0] -ne "wait" -and [GK]::GetForegroundWindow() -ne $gh) { $out += "STOPPED: game not in front before '$st'"; break }
      switch ($p[0]) {
        "wait" { Start-Sleep -Milliseconds ([int]$p[1]) }
        "here" { [GK]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 150; [GK]::mouse_event(4,0,0,0,[IntPtr]::Zero); $out += "here" }
        "click" { $x = [int]([double]$p[1] * $b.Width / 1400); $y = [int]([double]$p[2] * $b.Width / 1400); [GK]::SetCursorPos($x,$y) | Out-Null; Start-Sleep -Milliseconds 120; [GK]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 150; [GK]::mouse_event(4,0,0,0,[IntPtr]::Zero); $out += "click" }
        "key" {
          $k = $p[1].ToLower()
          if ($k -eq "escape" -or $k -eq "esc") { $out += "REFUSED escape"; continue }
          if (-not $vks.ContainsKey($k)) { $out += "unknown $k"; continue }
          $md = 2; if ($p.Length -gt 2) { $md = [int]$p[2] }
          $hold = 100; if ($p.Length -gt 3) { $hold = [int]$p[3] }
          $vk = [uint16]$vks[$k]; $ext = @(0x26,0x28,0x25,0x27) -contains $vk
          [GK]::Key($vk, $false, $ext, $md) | Out-Null; Start-Sleep -Milliseconds $hold; [GK]::Key($vk, $true, $ext, $md) | Out-Null
          $out += $k
        }
      }
    }
    "seq: " + ($out -join ' ')
  }
  "wheel" {
    # arg = "x,y,notches" (negative = scroll down), 1400-wide coordinates
    $f = Front
    if (-not $f) { "game not in front: no wheel"; break }
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $xy = $arg.Split(',')
    $x = [int]([double]$xy[0] * $b.Width / 1400); $y = [int]([double]$xy[1] * $b.Width / 1400); $n = [int]$xy[2]
    [GK]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 150
    $step = if ($n -lt 0) { [uint32]4294967176 } else { [uint32]120 }
    for ($i = 0; $i -lt [math]::Abs($n); $i++) { [GK]::mouse_event(0x800, 0, 0, $step, [IntPtr]::Zero); Start-Sleep -Milliseconds 80 }
    "wheel $n at $x,$y"
  }
  "move" {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $xy = $arg.Split(',')
    $x = [int]([double]$xy[0] * $b.Width / 1400); $y = [int]([double]$xy[1] * $b.Width / 1400)
    [GK]::SetCursorPos($x, $y) | Out-Null
    "moved $x,$y"
  }
  default { "usage: shot <path> | key <name> [ms] [count] | front" }
}


