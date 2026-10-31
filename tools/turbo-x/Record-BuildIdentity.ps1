# Record-BuildIdentity.ps1 -Commit <hash> -Source <zip path or branch> -PackagedDll <path to the packaged Turbo.dll> [-Note <text>]
#                          [-InstalledDll <path>] [-LogPath <path>] [-DryRun] [-NoPause]
# (Windows PowerShell 5.1, ASCII only; read-only on the game: it only appends one line to the log file)
#
# Appends ONE row to C:\FC 27 Live Editor\turbo_output\BUILD_IDENTITY.md (created with a header when absent):
#   | <timestamp> | <git commit> | <source zip or branch> | <md5 of the packaged Turbo.dll> | <md5 of the installed Turbo.dll> | <note> |
# Both md5s are computed here (Get-FileHash -Algorithm MD5, lower case); the installed one defaults to C:\FC 27 Live Editor\turbo\Turbo.dll
# and reads ABSENT when that file is missing. The note gets ' [MATCH]' or ' [MISMATCH]' appended, and the verdict is printed.
# MATCH    = the packaged DLL is byte-identical to the installed one (what is on disk is this commit's build)
# MISMATCH = it is not (not installed yet, an older install, or a different build) - the row is still recorded.
# -DryRun prints the row and writes nothing. Exit 0 = row written (or dry run; MISMATCH is not an error), 1 = refused (bad arguments),
# 2 = the log file could not be written.
param(
  [string]$Commit = '',
  [string]$Source = '',
  [string]$PackagedDll = '',
  [string]$Note = '',
  [string]$InstalledDll = 'C:\FC 27 Live Editor\turbo\Turbo.dll',
  [string]$LogPath = 'C:\FC 27 Live Editor\turbo_output\BUILD_IDENTITY.md',
  [switch]$DryRun,
  [switch]$NoPause
)
$ErrorActionPreference = 'Stop'
$Utf8 = New-Object System.Text.UTF8Encoding($false)

function Pause-Exit([int]$code) { if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }; exit $code }
function Fail([string]$m) { Write-Host ('REFUSED: ' + $m) -ForegroundColor Red; Pause-Exit 1 }
function Md5([string]$p) { if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLowerInvariant() } else { 'ABSENT' } }
function Cell([string]$s) { ($s -replace '[\r\n]+', ' ') -replace '\|', '/' }   # a table cell: no newlines, no pipes

try {
  if ($DryRun) { Write-Host '*** DRY RUN: nothing is written ***' -ForegroundColor Yellow }
  if ($Commit.Trim() -eq '') { Fail 'pass -Commit <git commit hash of the build>' }
  if ($Source.Trim() -eq '') { Fail 'pass -Source <the zip file or branch the DLL was built from>' }
  if ($PackagedDll.Trim() -eq '') { Fail 'pass -PackagedDll <path to the Turbo.dll in the package / build output>' }
  if (-not (Test-Path -LiteralPath $PackagedDll -PathType Leaf)) { Fail ('-PackagedDll not found: ' + $PackagedDll) }
  if (-not [System.IO.Path]::IsPathRooted($LogPath)) { Fail ('-LogPath must be absolute: ' + $LogPath) }
  if (-not [System.IO.Path]::IsPathRooted($InstalledDll)) { Fail ('-InstalledDll must be absolute: ' + $InstalledDll) }
  $PackagedDll = (Resolve-Path -LiteralPath $PackagedDll).ProviderPath

  $packagedMd5 = Md5 $PackagedDll
  $installedMd5 = Md5 $InstalledDll
  $verdict = if ($installedMd5 -ne 'ABSENT' -and $installedMd5 -eq $packagedMd5) { 'MATCH' } else { 'MISMATCH' }
  $stamp = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
  $noteCell = (Cell $Note).Trim()
  if ($noteCell -ne '') { $noteCell = $noteCell + ' ' }
  $noteCell = $noteCell + '[' + $verdict + ']'
  $row = '| ' + $stamp + ' | ' + (Cell $Commit.Trim()) + ' | ' + (Cell $Source.Trim()) + ' | ' + $packagedMd5 + ' | ' + $installedMd5 + ' | ' + $noteCell + ' |'

  Write-Host ('packaged  : ' + $PackagedDll)
  Write-Host ('            md5 ' + $packagedMd5)
  Write-Host ('installed : ' + $InstalledDll)
  Write-Host ('            md5 ' + $installedMd5)
  Write-Host ('row       : ' + $row)
  if ($verdict -eq 'MATCH') { Write-Host 'MATCH: the installed Turbo.dll is byte-identical to the packaged one.' -ForegroundColor Green }
  else { Write-Host ('MISMATCH: the installed Turbo.dll ' + $(if ($installedMd5 -eq 'ABSENT') { 'is absent' } else { 'differs from the packaged one' }) + ' (recorded as such).') -ForegroundColor Yellow }
  if ($DryRun) { Write-Host ('DRY RUN done: nothing written to ' + $LogPath) -ForegroundColor Yellow; Pause-Exit 0 }

  $nl = "`r`n"
  if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
    $existing = [System.IO.File]::ReadAllText($LogPath)
    if ($existing.Length -gt 0 -and -not $existing.Contains("`r`n") -and $existing.Contains("`n")) { $nl = "`n" }
    $prefix = ''
    if ($existing.Length -gt 0 -and -not $existing.EndsWith("`n")) { $prefix = $nl }
    [System.IO.File]::AppendAllText($LogPath, $prefix + $row + $nl, $Utf8)
  } else {
    [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $LogPath))
    $header = @(
      '# Turbo build identity log',
      '',
      'One row per packaged build checked against the installed Turbo.dll (Record-BuildIdentity.ps1, tools\turbo-x). MD5s are lower case.',
      '[MATCH] in the note = the packaged DLL was byte-identical to the installed one when the row was written; [MISMATCH] = it was not.',
      '',
      '| timestamp | git commit | source (zip or branch) | packaged Turbo.dll MD5 | installed Turbo.dll MD5 | note |',
      '|---|---|---|---|---|---|'
    )
    [System.IO.File]::WriteAllText($LogPath, (($header -join $nl) + $nl + $row + $nl), $Utf8)
  }
  Write-Host ('RECORDED in ' + $LogPath) -ForegroundColor Green
  Pause-Exit 0
} catch {
  Write-Host ('RECORD FAILED: ' + $_.Exception.Message) -ForegroundColor Red
  Pause-Exit 2
}
