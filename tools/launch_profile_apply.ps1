# launch_profile_apply.ps1   (Windows PowerShell 5.1, ASCII only)
# 2.21 phase 2 (brief 2.21p2-registry-locale.md, chunk R1). The ELEVATED side of the country launcher's language step.
# It is run ONLY by the scheduled task FC27_Profile (registered once by Cassio with register_fc27_profile_task.ps1); nothing of ours elevates.
# Input: turbo_output\launch_profile_request.json {version, action: "set"|"undo", language, locale, request_id, requested_at}.
# Everything that matters is validated against the FIXED TABLE below, never taken from the request (the request file is user-writable, this
# script runs elevated): the locale must be the table's locale of the language; no path comes from the request.
# set  : (a) pack presence in Data\Win32, (b) only when absent: copy-in of the language's pack from <game>\commentary (tmp + rename,
#        md5-verified, NEVER overwriting a non-empty file, NEVER deleting or moving anything), (c) record the prior HKLM Locale of both views
#        (only when no set of ours is active, so undo always lands on the real original) and write Locale (REG_SZ) in both views, read back.
# undo : restore the recorded prior in both views (read back) and clear "set" in the state file.
# Registry views: real = HKLM\SOFTWARE\EA Sports\EA SPORTS FC 27 in the 64-bit view and the 32-bit view (written only if the key exists there).
#   -RegRoot HKCU:\Software\TurboTest\<name> = the self-test model: subkeys view64 and view32 under that scratch key (nothing else is accepted).
# Writes: turbo_output\launch_profile_state.json, launch_profile_result.json, launch_profile_apply.log (one line per run).
# Exit codes: 0 done (also pack_absent: no Locale change) | 1 refused (named in the result) | 2 bad request.
[CmdletBinding()]
param(
  [string]$OutputDir = 'C:\FC 27 Live Editor\turbo_output',
  [string]$GameDir = 'C:\Program Files\EA Games\EA SPORTS FC 27',
  [string]$RegRoot = '',
  [string[]]$ProcessNames = @('FC27', 'LauncherNoAuth'),
  [string]$ExpectRequestId = '',
  [int]$MaxAgeSeconds = 300,
  [int64]$FreeMarginBytes = 2147483648
)
$ErrorActionPreference = 'Stop'
$ProcessNames = @($ProcessNames | ForEach-Object { $_ -split ',' } | Where-Object { $_ -and $_ -ne 'none' })
$Utf8 = New-Object Text.UTF8Encoding $false
$ReqPath = Join-Path $OutputDir 'launch_profile_request.json'
$StatePath = Join-Path $OutputDir 'launch_profile_state.json'
$ResPath = Join-Path $OutputDir 'launch_profile_result.json'
$LogPath = Join-Path $OutputDir 'launch_profile_apply.log'
$Win32 = Join-Path $GameDir 'Data\Win32'
$Backup = Join-Path $GameDir 'commentary'
$KeyPath = 'SOFTWARE\EA Sports\EA SPORTS FC 27'

# the exe's own locale table (PLAN H.1 item 4; same as callnames.cpp commentary_lang_for_locale); en_US = the installer's value
$Table = [ordered]@{ eng_us = 'en_US'; dut_nl = 'nl_NL'; ita_it = 'it_IT'; spa_es = 'es_ES'; fre_fr = 'fr_FR'; ger_de = 'de_DE'; por_br = 'pt_BR' }
$Kinds = @('full', 'launch')

function Stamp { $n = Get-Date; @{ iso = $n.ToString('yyyy-MM-ddTHH:mm:ss'); epoch = [int64]([DateTimeOffset]$n).ToUnixTimeSeconds() } }
function Md5([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLower() }
function Write-JsonAtomic([string]$path, $obj) {
  $dir = Split-Path -Parent $path
  if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  $tmp = $path + '.tmp'
  [IO.File]::WriteAllText($tmp, (($obj | ConvertTo-Json -Depth 8) + "`n"), $Utf8)
  Move-Item -LiteralPath $tmp -Destination $path -Force
}

# ------------------------------------------------------------------ registry views
if ($RegRoot -and $RegRoot -notmatch '^HKCU:\\Software\\TurboTest(\\[A-Za-z0-9_. -]+)+$') {
  Write-Host ('REFUSED: -RegRoot must be HKCU:\Software\TurboTest\<name> (scratch self-test key), got ' + $RegRoot) -ForegroundColor Red
  exit 2
}
function Open-View([string]$view, [bool]$writable) {
  # returns the open key or $null when the key does not exist in that view
  if ($RegRoot) {
    $sub = $RegRoot.Substring('HKCU:\'.Length) + '\' + $view
    return [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($sub, $writable)
  }
  $rv = $(if ($view -eq 'view64') { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 })
  $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $rv)
  try { return $base.OpenSubKey($KeyPath, $writable) } finally { $base.Dispose() }
}
function Get-Locale([string]$view) {
  $k = Open-View $view $false
  if (-not $k) { return $null }
  try {
    $v = $k.GetValue('Locale', $null)
    if ($null -eq $v) { return $null }
    [string]$v
  } finally { $k.Dispose() }
}
function Set-Locale([string]$view, [string]$value) {
  $k = Open-View $view $true
  if (-not $k) { throw ('registry key of ' + $view + ' does not exist') }
  try { $k.SetValue('Locale', $value, [Microsoft.Win32.RegistryValueKind]::String) } finally { $k.Dispose() }
}
function Both-Locales { @{ view64 = (Get-Locale 'view64'); view32 = (Get-Locale 'view32') } }

# ------------------------------------------------------------------ pack tests
# one rule for live and backup: full toc > 4096 B AND >= 1 non-empty cas in the folder named like the toc AND the launch toc present
function Test-Pack([string]$tocDir, [string]$casRoot, [string]$lang) {
  $ft = Join-Path $tocDir ('commentaryfull_' + $lang + '.toc')
  if (-not (Test-Path -LiteralPath $ft -PathType Leaf)) { return @{ ok = $false; why = 'no commentaryfull_' + $lang + '.toc' } }
  $fl = (Get-Item -LiteralPath $ft).Length
  if ($fl -le 4096) { return @{ ok = $false; why = ('commentaryfull_' + $lang + '.toc is ' + $fl + ' B (stub, needs > 4096)') } }
  $cd = Join-Path $casRoot ('commentaryfull_' + $lang)
  $cas = @(); if (Test-Path -LiteralPath $cd) { $cas = @(Get-ChildItem -LiteralPath $cd -Filter 'cas_*.cas' -File | Where-Object { $_.Length -gt 0 }) }
  if ($cas.Count -eq 0) { return @{ ok = $false; why = 'no non-empty cas_*.cas in commentaryfull_' + $lang } }
  if (-not (Test-Path -LiteralPath (Join-Path $tocDir ('commentarylaunch_' + $lang + '.toc')) -PathType Leaf)) { return @{ ok = $false; why = 'no commentarylaunch_' + $lang + '.toc' } }
  @{ ok = $true; why = ('full toc ' + $fl + ' B, ' + $cas.Count + ' cas') }
}
function Test-LivePack([string]$lang) { Test-Pack $Win32 (Join-Path $Win32 'superbundlelayout') $lang }
function Test-BackupPack([string]$lang) { Test-Pack $Backup $Backup $lang }

# the files to copy in: { rel (under Data\Win32), src, dst, bytes }
function Get-CopyPlan([string]$lang) {
  $plan = @()
  foreach ($k in $Kinds) {
    $n = 'commentary' + $k + '_' + $lang
    $t = Join-Path $Backup ($n + '.toc')
    if (Test-Path -LiteralPath $t -PathType Leaf) { $plan += [pscustomobject]@{ rel = ($n + '.toc'); src = $t; dst = (Join-Path $Win32 ($n + '.toc')); bytes = (Get-Item -LiteralPath $t).Length } }
    $d = Join-Path $Backup $n
    if (Test-Path -LiteralPath $d) {
      foreach ($f in @(Get-ChildItem -LiteralPath $d -Filter 'cas_*.cas' -File | Sort-Object Name)) {
        $rel = 'superbundlelayout\' + $n + '\' + $f.Name
        $plan += [pscustomobject]@{ rel = $rel; src = $f.FullName; dst = (Join-Path $Win32 $rel); bytes = $f.Length }
      }
    }
  }
  $plan
}

# ------------------------------------------------------------------ result / log
$script:reqId = ''
$script:action = ''
function Finish([int]$code, [bool]$ok, [string]$step, [string]$msg, $before, $after, [string]$packState, $copies, [bool]$writeResult = $true) {
  $r = [ordered]@{ request_id = $script:reqId; ok = $ok; step = $step; message = $msg; locale_before = $before; locale_after = $after
    pack_state = $packState; copies = @($copies); finished_at = (Stamp).iso }
  if ($writeResult) { try { Write-JsonAtomic $ResPath $r } catch { Write-Host ('could not write the result file: ' + $_.Exception.Message) -ForegroundColor Red } }
  $line = '{0} request={1} action={2} step={3} ok={4} pack={5} exit={6} msg={7}' -f (Stamp).iso, $script:reqId, $script:action, $step, $ok, $packState, $code, ($msg -replace '[\r\n]+', ' ')
  try {
    if (-not (Test-Path -LiteralPath $OutputDir)) { New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null }
    [IO.File]::AppendAllText($LogPath, $line + "`n", $Utf8)
  } catch { }
  Write-Host ($(if ($ok) { 'OK: ' } else { 'NOT DONE: ' }) + $msg) -ForegroundColor $(if ($ok) { 'Green' } else { 'Yellow' })
  exit $code
}

# ------------------------------------------------------------------ 1. read + validate the request
$noLoc = @{ view64 = $null; view32 = $null }
if (-not (Test-Path -LiteralPath $ReqPath)) { Finish 2 $false 'request' ('no request file: ' + $ReqPath) $noLoc $noLoc '' @() }
$req = $null
try { $req = [IO.File]::ReadAllText($ReqPath, $Utf8) | ConvertFrom-Json } catch { Finish 2 $false 'request' 'the request is not valid JSON' $noLoc $noLoc '' @() }
if ($req.request_id -is [string] -and $req.request_id -match '^[A-Za-z0-9_-]{4,64}$') { $script:reqId = [string]$req.request_id }
$script:action = [string]$req.action
if (-not $script:reqId) { Finish 2 $false 'request' 'bad or missing request_id (4-64 of A-Z a-z 0-9 _ -)' $noLoc $noLoc '' @() }
if ([string]$req.version -ne '1') { Finish 2 $false 'request' ('unknown request version ' + $req.version) $noLoc $noLoc '' @() }
if (@('set', 'undo') -notcontains $script:action) { Finish 2 $false 'request' ('action must be set or undo, got "' + $script:action + '"') $noLoc $noLoc '' @() }
$lang = [string]$req.language
$loc = [string]$req.locale
if ($script:action -eq 'set' -or $lang -or $loc) {
  if ($lang -notmatch '^[a-z]{3}_[a-z]{2}$' -or -not $Table.Contains($lang)) { Finish 2 $false 'request' ('unknown language "' + $lang + '" (known: ' + ($Table.Keys -join ', ') + ')') $noLoc $noLoc '' @() }
  if ($loc -cne $Table[$lang]) { Finish 2 $false 'request' ('locale "' + $loc + '" is not the table locale of ' + $lang + ' (' + $Table[$lang] + ')') $noLoc $noLoc '' @() }
}
$reqAt = [datetime]::MinValue
if (-not [datetime]::TryParseExact([string]$req.requested_at, 'yyyy-MM-ddTHH:mm:ss', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$reqAt)) {
  Finish 2 $false 'request' 'bad requested_at (want yyyy-MM-ddTHH:mm:ss local time)' $noLoc $noLoc '' @()
}
$before = Both-Locales

# ------------------------------------------------------------------ 2. refusals (nothing is written before these pass)
if ($ExpectRequestId -and $ExpectRequestId -ne $script:reqId) { Finish 1 $false 'refused' ('request_id mismatch: expected ' + $ExpectRequestId + ', the file holds ' + $script:reqId) $before $before '' @() }
$age = ((Get-Date) - $reqAt).TotalSeconds
if ($age -gt $MaxAgeSeconds) { Finish 1 $false 'refused' ('stale request: requested ' + [int]$age + ' s ago (limit ' + $MaxAgeSeconds + ' s)') $before $before '' @() }
if ($age -lt -60) { Finish 1 $false 'refused' ('request is dated in the future (' + [int](-$age) + ' s)') $before $before '' @() }
$prevRes = $null
if (Test-Path -LiteralPath $ResPath) { try { $prevRes = Get-Content -LiteralPath $ResPath -Raw | ConvertFrom-Json } catch { } }
if ($prevRes -and [string]$prevRes.request_id -eq $script:reqId) { Finish 1 $false 'refused' ('request_id ' + $script:reqId + ' was already processed (replay); its result stays in place') $before $before '' @() $false }
if ($ProcessNames.Count) {
  $running = @(Get-Process -Name $ProcessNames -ErrorAction SilentlyContinue)
  if ($running.Count) { Finish 1 $false 'refused' ('refused while running: ' + (($running | ForEach-Object { $_.Name + ' (pid ' + $_.Id + ')' }) -join ', ')) $before $before '' @() }
}
$state = $null
if (Test-Path -LiteralPath $StatePath) { try { $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json } catch { Finish 1 $false 'refused' 'launch_profile_state.json exists but does not parse; not touching the registry' $before $before '' @() } }
if ($null -eq (Open-View 'view64' $false)) { Finish 1 $false 'refused' 'the registry key of the 64-bit view does not exist (the game is not installed through its installer?)' $before $before '' @() }

# ------------------------------------------------------------------ 3a. undo
if ($script:action -eq 'undo') {
  if (-not $state -or -not $state.set) { Finish 1 $false 'refused' 'nothing to undo: the state file records no active set' $before $before '' @() }
  $p64 = [string]$state.prior.view64
  $p32 = $state.prior.view32
  if (-not $p64) { Finish 1 $false 'refused' 'the state file holds no prior value for the 64-bit view' $before $before '' @() }
  try {
    Set-Locale 'view64' $p64
    if ($null -ne $p32 -and $null -ne (Open-View 'view32' $false)) { Set-Locale 'view32' ([string]$p32) }
  } catch { Finish 1 $false 'restore' ('registry write failed: ' + $_.Exception.Message) $before (Both-Locales) '' @() }
  $after = Both-Locales
  $want32 = $(if ($null -ne $p32 -and $null -ne $before.view32) { [string]$p32 } else { $before.view32 })
  if ($after.view64 -cne $p64 -or $after.view32 -cne $want32) { Finish 1 $false 'restore' ('read-back differs: ' + $after.view64 + '/' + $after.view32 + ' wanted ' + $p64 + '/' + $want32) $before $after '' @() }
  $s = Stamp
  $ns = [ordered]@{ version = 1; prior = [ordered]@{ view64 = $p64; view32 = $p32 }; set = $null; set_at = $state.set_at; set_at_epoch = $state.set_at_epoch
    undone_at = $s.iso; undone_at_epoch = $s.epoch; copies = @($state.copies); request_id = $script:reqId }
  Write-JsonAtomic $StatePath $ns
  Finish 0 $true 'undo' ('Locale restored to ' + $p64 + '/' + $(if ($null -ne $p32) { $p32 } else { 'none' }) + ' (read back equal); state "set" cleared') $before $after '' @($state.copies)
}

# ------------------------------------------------------------------ 3b. set: pack presence, copy-in
$copies = @()
$packState = 'present'
if ($lang -eq 'eng_us') { $packState = 'base' }
else {
  $live = Test-LivePack $lang
  if (-not $live.ok) {
    $bk = Test-BackupPack $lang
    if (-not $bk.ok) { Finish 0 $false 'pack' ('pack_absent: ' + $lang + ' is not live in Data\Win32 (' + $live.why + ') and the backup store cannot supply it (' + $bk.why + '); Locale unchanged') $before $before 'pack_absent' @() }
    $plan = @(Get-CopyPlan $lang)
    # pre-flight: every target before the first copy, so a refusal leaves nothing half-copied
    $todo = @(); $need = [int64]0
    foreach ($it in $plan) {
      if (Test-Path -LiteralPath ($it.dst + '.turbo_tmp')) { Finish 1 $false 'copy' ('a leftover ' + $it.rel + '.turbo_tmp exists; not touching it (report it, never auto-deleted)') $before $before 'copy_refused' @() }
      if (Test-Path -LiteralPath $it.dst) {
        $dl = (Get-Item -LiteralPath $it.dst).Length
        if ($dl -eq 0) { $todo += $it; $need += $it.bytes; continue }
        if ($dl -eq $it.bytes -and (Md5 $it.dst) -eq (Md5 $it.src)) { continue }
        Finish 1 $false 'copy' ('refused: ' + $it.rel + ' already exists in Data\Win32 (' + $dl + ' B, not identical to the backup copy of ' + $it.bytes + ' B); never overwriting a non-empty file; Locale unchanged') $before $before 'copy_refused' @()
      }
      $todo += $it; $need += $it.bytes
    }
    $drive = (Get-Item -LiteralPath $Win32).PSDrive
    if ($drive -and $drive.Free -lt ($need + $FreeMarginBytes)) { Finish 1 $false 'copy' ('refused: needs ' + $need + ' B plus a ' + $FreeMarginBytes + ' B margin, the drive has ' + $drive.Free + ' B free') $before $before 'copy_refused' @() }
    foreach ($it in $todo) {
      $dir = Split-Path -Parent $it.dst
      if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
      $tmp = $it.dst + '.turbo_tmp'
      try {
        Copy-Item -LiteralPath $it.src -Destination $tmp
        $sm = Md5 $it.src; $tm = Md5 $tmp
        if ($sm -ne $tm -or (Get-Item -LiteralPath $tmp).Length -ne $it.bytes) { Finish 1 $false 'copy' ('md5/length mismatch after copying ' + $it.rel + '; the .turbo_tmp is left in place for inspection') $before $before 'copy_failed' $copies }
        Move-Item -LiteralPath $tmp -Destination $it.dst -Force
        $copies += [ordered]@{ rel = $it.rel; bytes = $it.bytes; md5 = $sm }
      } catch { Finish 1 $false 'copy' ('copy of ' + $it.rel + ' failed: ' + $_.Exception.Message + '; any .turbo_tmp is left in place') $before $before 'copy_failed' $copies }
    }
    $live = Test-LivePack $lang
    if (-not $live.ok) { Finish 1 $false 'copy' ('after the copy the pack is still not live: ' + $live.why) $before $before 'copy_failed' $copies }
    $packState = 'copied'
  }
}

# ------------------------------------------------------------------ 3c. set: the registry
$active = $state -and $state.set -and ([string]$state.set.locale -ceq [string]$before.view64) -and ([string]$state.prior.view64)
if ($active) { $prior = [ordered]@{ view64 = [string]$state.prior.view64; view32 = $state.prior.view32 } }
else { $prior = [ordered]@{ view64 = $before.view64; view32 = $before.view32 } }
$has32 = ($null -ne (Open-View 'view32' $false))
try {
  Set-Locale 'view64' $loc
  if ($has32) { Set-Locale 'view32' $loc }
} catch {
  # put back what this run changed
  try { if ($null -ne $before.view64) { Set-Locale 'view64' $before.view64 }; if ($has32 -and $null -ne $before.view32) { Set-Locale 'view32' $before.view32 } } catch { }
  Finish 1 $false 'registry' ('registry write failed (rolled back): ' + $_.Exception.Message) $before (Both-Locales) $packState $copies
}
$after = Both-Locales
if ($after.view64 -cne $loc -or ($has32 -and $after.view32 -cne $loc)) {
  try { if ($null -ne $before.view64) { Set-Locale 'view64' $before.view64 }; if ($has32 -and $null -ne $before.view32) { Set-Locale 'view32' $before.view32 } } catch { }
  Finish 1 $false 'registry' ('read-back differs (' + $after.view64 + '/' + $after.view32 + ' wanted ' + $loc + '); rolled back') $before (Both-Locales) $packState $copies
}
$s = Stamp
$allCopies = @(); if ($state -and $state.copies) { $allCopies += @($state.copies) }; $allCopies += @($copies)
$ns = [ordered]@{ version = 1; prior = $prior; set = [ordered]@{ locale = $loc; language = $lang }; set_at = $s.iso; set_at_epoch = $s.epoch
  copies = $allCopies; request_id = $script:reqId }
Write-JsonAtomic $StatePath $ns
Finish 0 $true 'set' ('Locale set to ' + $loc + ' (' + $lang + '; pack ' + $packState + '); prior ' + $prior.view64 + '/' + $(if ($null -ne $prior.view32) { $prior.view32 } else { 'none' })) $before $after $packState $copies
