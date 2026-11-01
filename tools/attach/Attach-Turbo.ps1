# Attach-Turbo.ps1 [-LeRoot <dir>[,<dir>...]] [-DryRun] [-ConfigPath <file>]   (Windows PowerShell 5.1, ASCII only)
# Re-attaches Turbo after an FCM 27 / Live Editor / mod-manager update (design: handover\16_FCM27_COEXISTENCE.md section 3).
# Normally started through Attach-Turbo.cmd (which pauses at the end). Every path is absolute or built from $PSScriptRoot.
#   1. refuses (exit 1, writes nothing but its own log) while FC27 / Launcher / LauncherNoAuth / a mod manager runs
#   2. picks the Turbo package zip from Attach-Turbo.config.json (default: newest FC27_LE_Turbo_X_*.zip under C:\FC_Tools\TurboX
#      that has a .sha256 beside it) and verifies its SHA-256
#   3. finds every Live Editor folder (FCLiveEditor.DLL + le_config.json), prints its le_config dlls/params/log_level (READ ONLY)
#      and flags dlls entries whose file is missing (never fixed)
#   4. per folder: compares the Turbo files with the package (same policy as Install-Turbo.ps1: user data such as turbo_config.json,
#      turbo_output\, kits\ is only required to exist). All equal -> "ATTACHED, nothing to do". Else -> runs the package's own
#      install\Install-Turbo.ps1 -LeRoot <folder> -Source <zip> -ExpectedDllMd5 <md5> -Yes (backup + Undo-Turbo.cmd + self-rollback),
#      then re-compares.
#   5. writes <folder>\turbo_output\attach_log.txt (one line per run) and Attach-Turbo_<stamp>.log next to this script.
# Never touched: le_config.json, le_*.json, FCLiveEditor.DLL, CryptBase.dll, Launcher*.exe, FakeEAACLauncher\, mods\, loc\, the game
# folder, FIFAModData, .fifamod files, mod-manager folders, saves. A Live Editor found inside the game folder is reported, not written.
# Exit 0 = every Live Editor folder attached (or dry run), 1 = refused (nothing written), 2 = at least one folder failed.
param(
  [string[]]$LeRoot = @(),
  [switch]$DryRun,
  [string]$ConfigPath = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Here = $PSScriptRoot
if (-not $Here) { $Here = Split-Path -Parent $MyInvocation.MyCommand.Path }
$Stamp = (Get-Date).ToString('yyyyMMdd_HHmmss')
$LogFile = Join-Path $Here ('Attach-Turbo_' + $Stamp + '.log')
$script:LogOk = $true
$Sep = [IO.Path]::DirectorySeparatorChar

# ---------------------------------------------------------------- helpers (pure where possible; dot-source to test)
function Say([string]$m, [string]$c = 'Gray') {
  Write-Host $m -ForegroundColor $c
  if ($script:LogOk) {
    try { Add-Content -LiteralPath $LogFile -Value $m -Encoding ASCII } catch { $script:LogOk = $false; Write-Host ('(log file not writable: ' + $LogFile + ')') -ForegroundColor Yellow }
  }
}
function Has([string]$p) { ($p -ne '') -and (Test-Path -LiteralPath $p) }
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash } else { 'ABSENT' } }
function NativeRel([string]$rel) { $rel.Replace('\', $Sep).Replace('/', $Sep) }
function BackRel([string]$rel) { $rel.Replace('/', '\') }
function FullPath([string]$p) { [IO.Path]::GetFullPath($p).TrimEnd('\', '/') }
function IsUnderDir([string]$path, [string]$dir) {
  if ($dir -eq '') { return $false }
  $p = (FullPath $path); $d = (FullPath $dir)
  ($p -ieq $d) -or $p.StartsWith($d + $Sep, [StringComparison]::OrdinalIgnoreCase)
}

# Install-Turbo.ps1 policy (copied 1:1 from Install-Turbo.ps1 @ md5 470f7e23..., the copy in Turbo X 1.0 final12 and 1.0.1):
$ProtectedDirs = @('turbo_output', 'kits', 'turbo\kits', 'turbo\callnames\masters')
$ProtectedFiles = @('turbo_config.json')
$Blocked = @('FCLiveEditor*', 'le_offsets.json', 'turbo_dev\*', 'turbo_backups\*', 'careersave*', 'saves\*', 'settings\*', 'install\*')
function IsProtectedRel([string]$rel) {
  foreach ($f in $ProtectedFiles) { if ($rel -ieq $f) { return $true } }
  foreach ($d in $ProtectedDirs) { if ($rel.StartsWith($d + '\', [StringComparison]::OrdinalIgnoreCase)) { return $true } }
  return $false
}
function IsBlockedRel([string]$rel) { foreach ($b in $Blocked) { if ($rel -ilike $b) { return $true } } return $false }

function Read-AttachConfig([string]$path) {
  # defaults; the json overrides any key it has
  $cfg = [ordered]@{
    packageZip         = 'auto'
    packageSearchRoot  = 'C:\FC_Tools\TurboX'
    packagePattern     = 'FC27_LE_Turbo_X_*.zip'
    packageSha256      = ''
    expectedDllMd5     = ''
    scanRoots          = @(
      @{ path = 'C:\FC 27 Live Editor'; depth = 0 },
      @{ path = 'C:\FC27 Live Editor'; depth = 0 },
      @{ path = 'C:\FC 27 Live Editor 27'; depth = 0 },
      @{ path = '%USERPROFILE%\Desktop\FC 27 Live Editor'; depth = 0 },
      @{ path = 'C:\Program Files\EA Games\EA SPORTS FC 27'; depth = 2 },
      @{ path = 'C:\FC_Tools\FCM27'; depth = 5 })
    extraLeRoots       = @()
    gameFolder         = 'C:\Program Files\EA Games\EA SPORTS FC 27'
    blockingProcesses  = @('FC27', 'Launcher', 'LauncherNoAuth', 'FIFA Mod Manager', 'FIFAModManager', 'FrostyModManager', 'FIFA Editor Tool', 'Patch FCM*')
    fcmPatchDir        = 'C:\FC_Tools\FCM27'
  }
  if (Has $path) {
    $j = [IO.File]::ReadAllText($path) | ConvertFrom-Json
    foreach ($k in @($cfg.Keys)) {
      $prop = $j.PSObject.Properties[$k]
      if ($null -ne $prop -and $null -ne $prop.Value) { $cfg[$k] = $prop.Value }
    }
  }
  return $cfg
}
function ExpandPath([string]$p) { [Environment]::ExpandEnvironmentVariables($p).Trim().Trim('"').TrimEnd('\', '/') }

function Read-Sha256Sidecar([string]$zip) {
  # <zip>.sha256 or <name-without-.zip>.sha256; returns @{ File; Hash } or $null
  $cands = @(($zip + '.sha256'), ([IO.Path]::ChangeExtension($zip, '.sha256')))
  foreach ($c in $cands) {
    if (Test-Path -LiteralPath $c -PathType Leaf) {
      $t = Get-Content -LiteralPath $c -Raw
      $m = [regex]::Match([string]$t, '\b[0-9A-Fa-f]{64}\b')
      if ($m.Success) { return @{ File = $c; Hash = $m.Value.ToUpperInvariant() } }
      return @{ File = $c; Hash = '' }
    }
  }
  return $null
}
function Find-PackageZip($cfg) {
  # returns @{ Zip; Sha; ShaFile; Note } or @{ Error }
  $pz = ExpandPath ([string]$cfg.packageZip)
  if ($pz -ne '' -and $pz -ine 'auto') {
    if (-not [IO.Path]::IsPathRooted($pz)) { return @{ Error = ('packageZip must be an absolute path: ' + $pz) } }
    if (-not (Test-Path -LiteralPath $pz -PathType Leaf)) { return @{ Error = ('package zip not found: ' + $pz) } }
    $sc = Read-Sha256Sidecar $pz
    $want = ([string]$cfg.packageSha256).Trim().ToUpperInvariant()
    if ($want -eq '' -and $null -ne $sc) { $want = $sc.Hash; $shaFile = $sc.File } else { $shaFile = 'Attach-Turbo.config.json packageSha256' }
    if ($want -notmatch '^[0-9A-F]{64}$') { return @{ Error = ('no SHA-256 for ' + $pz + ': put <zip>.sha256 beside it or set packageSha256 in the config') } }
    return @{ Zip = (FullPath $pz); Sha = $want; ShaFile = $shaFile; Note = 'from config' }
  }
  $root = ExpandPath ([string]$cfg.packageSearchRoot)
  if (-not [IO.Path]::IsPathRooted($root) -or -not (Has $root)) { return @{ Error = ('packageSearchRoot missing or not absolute: ' + $root) } }
  $all = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter ([string]$cfg.packagePattern) -ErrorAction SilentlyContinue | Where-Object { $_.Extension -ieq '.zip' })
  $withSha = @()
  foreach ($z in $all) { $sc = Read-Sha256Sidecar $z.FullName; if ($null -ne $sc -and $sc.Hash -ne '') { $withSha += [pscustomobject]@{ Z = $z; S = $sc } } }
  if ($withSha.Count -eq 0) {
    $names = ($all | ForEach-Object { $_.FullName }) -join '; '
    return @{ Error = ('no ' + $cfg.packagePattern + ' with a .sha256 beside it under ' + $root + '. Zips seen without one: ' + $(if ($names) { $names } else { 'none' })) }
  }
  $best = $withSha | Sort-Object { $_.Z.LastWriteTimeUtc }, { $_.Z.Name } -Descending | Select-Object -First 1
  return @{ Zip = $best.Z.FullName; Sha = $best.S.Hash; ShaFile = $best.S.File; Note = ('newest of ' + $withSha.Count + ' zip(s) with a .sha256') }
}

function Get-PackageSet([string]$src) {
  # files Install-Turbo.ps1 would consider (install\* and other blocked entries left out); Rel in '\' form
  $out = @()
  $base = (FullPath $src)
  foreach ($f in @(Get-ChildItem -LiteralPath $base -Recurse -File -Force)) {
    $rel = BackRel ($f.FullName.Substring($base.Length + 1))
    if (IsBlockedRel $rel) { continue }
    $out += [pscustomobject]@{ Rel = $rel; Src = $f.FullName; Md5 = (Md5 $f.FullName); Protected = (IsProtectedRel $rel) }
  }
  return $out
}
function Compare-TurboSet([string]$le, $set) {
  # returns the list of differences; empty = attached
  $diff = @()
  foreach ($p in $set) {
    $dst = Join-Path $le (NativeRel $p.Rel)
    $exists = Test-Path -LiteralPath $dst -PathType Leaf
    if ($p.Protected) { if (-not $exists) { $diff += ('missing (user file, only created if absent): ' + $p.Rel) }; continue }
    if (-not $exists) { $diff += ('missing: ' + $p.Rel); continue }
    if ((Md5 $dst) -ne $p.Md5) { $diff += ('differs from the package: ' + $p.Rel) }
  }
  return ,$diff
}

function IsLeRoot([string]$p) { (Has $p) -and (Test-Path -LiteralPath (Join-Path $p 'FCLiveEditor.DLL') -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $p 'le_config.json') -PathType Leaf) }
function Find-LeRoots($cfg, [string[]]$extra) {
  $seen = @{}; $roots = @()
  $cands = @()
  foreach ($e in @($extra) + @($cfg.extraLeRoots)) { if ($e) { $cands += @{ path = [string]$e; depth = 0 } } }
  foreach ($s in @($cfg.scanRoots)) { $cands += @{ path = [string]$s.path; depth = [int]$s.depth } }
  foreach ($c in $cands) {
    $p = ExpandPath $c.path
    if ($p -eq '' -or -not [IO.Path]::IsPathRooted($p) -or -not (Has $p)) { continue }
    $dirs = @($p)
    if ($c.depth -gt 0) {
      $dirs += @(Get-ChildItem -LiteralPath $p -Directory -Recurse -Depth ($c.depth - 1) -Force -ErrorAction SilentlyContinue | Where-Object {
          -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) } | ForEach-Object { $_.FullName })
    }
    foreach ($d in $dirs) {
      if (-not (IsLeRoot $d)) { continue }
      $k = (FullPath $d).ToUpperInvariant()
      if ($seen.ContainsKey($k)) { continue }
      $seen[$k] = 1; $roots += (FullPath $d)
    }
  }
  return ,$roots
}

function Read-LeConfigReport([string]$le) {
  # READ ONLY. returns @{ Dlls; Params; LogLevel; Missing; Error }
  $r = @{ Dlls = @(); Params = ''; LogLevel = ''; Missing = @(); Error = '' }
  try {
    $j = [IO.File]::ReadAllText((Join-Path $le 'le_config.json')) | ConvertFrom-Json
    if ($null -ne $j.launcher) {
      $r.Dlls = @($j.launcher.dlls | Where-Object { $_ -ne $null } | ForEach-Object { [string]$_ })
      $r.Params = [string]$j.launcher.params
    }
    if ($null -ne $j.logger) { $r.LogLevel = [string]$j.logger.log_level }
    foreach ($d in $r.Dlls) {
      $dp = $d
      if (-not [IO.Path]::IsPathRooted($dp)) { $dp = Join-Path $le (NativeRel $d) }
      if (-not (Test-Path -LiteralPath $dp -PathType Leaf)) { $r.Missing += $d }
    }
  } catch { $r.Error = $_.Exception.Message }
  return $r
}

function Get-BlockingProcesses($cfg) {
  $names = @($cfg.blockingProcesses | ForEach-Object { [string]$_ } | Where-Object { $_ -ne '' })
  if ($names.Count -eq 0) { return @() }
  return @(Get-Process -Name $names -ErrorAction SilentlyContinue)
}

function Invoke-InstallTurbo([string]$installer, [string]$le, [string]$zip, [string]$md5) {
  # runs the package's own Install-Turbo.ps1 in a child Windows PowerShell; returns its exit code (output goes to the log)
  $exe = Join-Path $PSHOME 'powershell.exe'
  if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { $exe = Join-Path $PSHOME 'pwsh' }   # pwsh: only for the Linux test bench
  $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  try {
    & $exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $installer -LeRoot $le -Source $zip -ExpectedDllMd5 $md5 -Yes 2>&1 |
      ForEach-Object { Say ('    | ' + [string]$_) }
    $rc = $LASTEXITCODE
  } finally { $ErrorActionPreference = $old }
  if ($null -eq $rc) { $rc = 2 }
  return [int]$rc
}

function Write-RootAttachLog([string]$le, [string]$line) {
  $d = Join-Path $le 'turbo_output'
  if (-not (Test-Path -LiteralPath $d -PathType Container)) { return }
  try { Add-Content -LiteralPath (Join-Path $d 'attach_log.txt') -Value $line -Encoding ASCII } catch { Say ('  (could not write ' + (Join-Path $d 'attach_log.txt') + ': ' + $_.Exception.Message + ')') 'Yellow' }
}

# ---------------------------------------------------------------- main
function Invoke-AttachMain {
  $tmp = ''
  try {
    if ($ConfigPath -eq '') { $script:ConfigPath = Join-Path $Here 'Attach-Turbo.config.json' }
    Say ('Attach-Turbo ' + (Get-Date).ToString('s') + $(if ($DryRun) { '   *** DRY RUN: nothing is changed ***' } else { '' })) 'Cyan'
    Say ('Script folder    : ' + $Here)
    Say ('Log              : ' + $LogFile)
    Say ('Config           : ' + $ConfigPath + $(if (Has $ConfigPath) { '' } else { ' (absent: defaults)' }))
    try { $cfg = Read-AttachConfig $ConfigPath } catch { Say ('REFUSED: config unreadable: ' + $_.Exception.Message) 'Red'; return 1 }

    # 1. nothing may run
    $run = @(Get-BlockingProcesses $cfg)
    if ($run.Count -gt 0) {
      Say ('REFUSED: close these first (nothing written): ' + (($run | ForEach-Object { $_.ProcessName + ' pid ' + $_.Id }) -join ', ')) 'Red'
      return 1
    }

    # 2. the package
    $pk = Find-PackageZip $cfg
    if ($pk.Error) { Say ('REFUSED: ' + $pk.Error) 'Red'; Say '  Make one with: (Get-FileHash -Algorithm SHA256 "<zip>").Hash | Set-Content -Encoding ASCII "<zip>.sha256"' 'Yellow'; return 1 }
    $real = (Get-FileHash -LiteralPath $pk.Zip -Algorithm SHA256).Hash
    Say ('Package          : ' + $pk.Zip + ' (' + $pk.Note + ')')
    if ($real -ne $pk.Sha) { Say ('REFUSED: package SHA-256 ' + $real + ' differs from ' + $pk.Sha + ' (' + $pk.ShaFile + ')') 'Red'; return 1 }
    Say ('SHA-256          : ' + $real + ' = ' + $pk.ShaFile)
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('turbo_attach_' + [Guid]::NewGuid().ToString('N'))
    [IO.Compression.ZipFile]::ExtractToDirectory($pk.Zip, $tmp)
    $installer = Join-Path (Join-Path $tmp 'install') 'Install-Turbo.ps1'
    $pkgDll = Join-Path (Join-Path $tmp 'turbo') 'Turbo.dll'
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { Say 'REFUSED: the package has no install\Install-Turbo.ps1' 'Red'; return 1 }
    if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $tmp 'install') 'Undo-Turbo.ps1') -PathType Leaf)) { Say 'REFUSED: the package has no install\Undo-Turbo.ps1' 'Red'; return 1 }
    if (-not (Test-Path -LiteralPath $pkgDll -PathType Leaf)) { Say 'REFUSED: the package has no turbo\Turbo.dll' 'Red'; return 1 }
    $dllMd5 = Md5 $pkgDll
    $want = ([string]$cfg.expectedDllMd5).Trim().ToUpperInvariant()
    if ($want -ne '' -and $want -ne $dllMd5) { Say ('REFUSED: Turbo.dll in the package is ' + $dllMd5 + ', config expects ' + $want) 'Red'; return 1 }
    $set = Get-PackageSet $tmp
    Say ('Turbo.dll        : ' + $dllMd5 + '; ' + $set.Count + ' package files checked per folder')

    # FCM seen (names only, nothing opened)
    $fcm = 'none seen'
    $fd = ExpandPath ([string]$cfg.fcmPatchDir)
    if ($fd -ne '' -and (Has $fd)) {
      $fx = @(Get-ChildItem -LiteralPath $fd -File -Filter 'Patch FCM*' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | ForEach-Object { $_.Name + ' (' + $_.LastWriteTime.ToString('yyyy-MM-dd') + ')' })
      if ($fx.Count -gt 0) { $fcm = $fx -join '; ' }
    }
    Say ('FCM patch files  : ' + $fcm)

    # 3. Live Editor folders
    foreach ($e in @($LeRoot) + @($cfg.extraLeRoots)) {
      if ($e -and -not [IO.Path]::IsPathRooted((ExpandPath ([string]$e)))) { Say ('  WARNING: ignored (not an absolute path): ' + $e) 'Yellow' }
      elseif ($e -and -not (IsLeRoot (ExpandPath ([string]$e)))) { Say ('  WARNING: not a Live Editor folder (needs FCLiveEditor.DLL + le_config.json): ' + $e) 'Yellow' }
    }
    $roots = Find-LeRoots $cfg $LeRoot
    if ($roots.Count -eq 0) { Say 'REFUSED: no Live Editor folder found (FCLiveEditor.DLL + le_config.json). Pass -LeRoot or add extraLeRoots to the config.' 'Red'; return 1 }
    $game = ExpandPath ([string]$cfg.gameFolder)
    $fails = 0; $results = @()
    foreach ($le in $roots) {
      Say ''
      Say ('== ' + $le) 'Cyan'
      $lc = Read-LeConfigReport $le
      $leDll = Get-Item -LiteralPath (Join-Path $le 'FCLiveEditor.DLL')
      $leInfo = 'FCLiveEditor.DLL ' + $leDll.LastWriteTime.ToString('yyyy-MM-dd HH:mm') + ' ' + $leDll.Length + ' B'
      Say ('  ' + $leInfo)
      if ($lc.Error) { Say ('  le_config.json  : unreadable (' + $lc.Error + ')') 'Yellow' }
      else {
        Say ('  le_config dlls  : ' + $(if ($lc.Dlls.Count) { $lc.Dlls -join ', ' } else { '(none)' }) + '   params: "' + $lc.Params + '"   log_level: ' + $lc.LogLevel)
        foreach ($m in $lc.Missing) { Say ('  FLAG: le_config.json lists "' + $m + '" but that file is missing - the launcher injects nothing until it exists (reported only, not fixed)') 'Yellow' }
      }
      if (IsUnderDir $le $game) {
        Say '  SKIPPED: this Live Editor is inside the game folder, which Attach-Turbo never writes. Attach it by hand if you want Turbo there.' 'Yellow'
        $results += ($le + ' : skipped (game folder)'); continue
      }
      $diff = Compare-TurboSet $le $set
      if ($diff.Count -eq 0) {
        Say '  ATTACHED, nothing to do' 'Green'
        $results += ($le + ' : attached, nothing to do')
        if (-not $DryRun) { Write-RootAttachLog $le ((Get-Date).ToString('s') + ' | ' + $le + ' | ATTACHED nothing to do | ' + $pk.Zip + ' | Turbo.dll ' + $dllMd5 + ' | ' + $leInfo + ' | FCM ' + $fcm) }
        continue
      }
      Say ('  NOT ATTACHED: ' + $diff.Count + ' difference(s)') 'Yellow'
      $diff | Select-Object -First 15 | ForEach-Object { Say ('    ' + $_) }
      if ($diff.Count -gt 15) { Say ('    ... ' + ($diff.Count - 15) + ' more') }
      if ($DryRun) { $results += ($le + ' : would attach (' + $diff.Count + ' differences)'); continue }
      Say '  Running the package Install-Turbo.ps1 (backs up first; 1 to 5 minutes; do not close this window)...'
      $rc = Invoke-InstallTurbo $installer $le $pk.Zip $dllMd5
      $after = Compare-TurboSet $le $set
      if ($rc -eq 0 -and $after.Count -eq 0) {
        Say '  ATTACHED (installed and re-verified against the package)' 'Green'
        $res = 'ATTACHED installed'
      } else {
        $fails++
        Say ('  FAILED: Install-Turbo exit ' + $rc + ', ' + $after.Count + ' difference(s) left' + $(if ($rc -eq 1) { ' (it refused: nothing written)' } elseif ($rc -eq 2) { ' (it rolled back; see above)' } else { '' })) 'Red'
        $after | Select-Object -First 15 | ForEach-Object { Say ('    ' + $_) }
        $res = 'FAILED exit ' + $rc
      }
      $results += ($le + ' : ' + $res)
      Write-RootAttachLog $le ((Get-Date).ToString('s') + ' | ' + $le + ' | ' + $res + ' | ' + $pk.Zip + ' | Turbo.dll ' + $dllMd5 + ' | ' + $leInfo + ' | FCM ' + $fcm)
    }
    Say ''
    Say 'Summary:' 'Cyan'
    $results | ForEach-Object { Say ('  ' + $_) }
    Say 'Undo: run Undo-Turbo.cmd in <Live Editor>\turbo_backups\install_<stamp>\ of the install you want to roll back.'
    if ($fails -gt 0) { Say ('RESULT: ' + $fails + ' folder(s) FAILED') 'Red'; return 2 }
    Say $(if ($DryRun) { 'RESULT: dry run done, nothing changed' } else { 'RESULT: OK' }) 'Green'
    return 0
  } catch {
    Say ('ERROR: ' + $_.Exception.Message + ' (line ' + $_.InvocationInfo.ScriptLineNumber + ')') 'Red'
    return 2
  } finally {
    if ($tmp -ne '' -and (Test-Path -LiteralPath $tmp)) { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue }
  }
}

if ($MyInvocation.InvocationName -ne '.') { $rcAll = @(Invoke-AttachMain); exit ([int]$rcAll[-1]) }
