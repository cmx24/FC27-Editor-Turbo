# register_fc27_profile_task.ps1 [-WhatIf] [-Show] [-Unregister]   (Windows PowerShell 5.1, ASCII only)
# 2.21 phase 2 (brief 2.21p2-registry-locale.md, chunk R1). Run ONCE by Cassio from an ELEVATED PowerShell; no script of ours runs it, and it
# never elevates itself (it refuses when not elevated). It registers the scheduled task FC27_Profile:
#   action    powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "<abs>\launch_profile_apply.ps1"   (fixed absolute path, no arguments)
#   principal the current user, interactive token, highest run level (like his LaunchFC27 task)
#   settings  MultipleInstances IgnoreNew, ExecutionTimeLimit 10 minutes, runs on battery, no trigger (started only by "schtasks /run /tn FC27_Profile")
# The one XML template below is both what -WhatIf prints and what is registered, so what you read is what gets installed.
#   -WhatIf      print the task XML, register nothing (works without elevation)
#   -Show        read-only: the task's state, action and principal if it exists
#   -Unregister  remove ONLY the task FC27_Profile (needs elevation)
param(
  [switch]$WhatIf,
  [switch]$Show,
  [switch]$Unregister,
  [string]$TaskName = 'FC27_Profile',
  [string]$ApplyPath = 'C:\FC 27 Live Editor\turbo_dev\tools\launch_profile_apply.ps1'
)
$ErrorActionPreference = 'Stop'
function Esc([string]$s) { [Security.SecurityElement]::Escape($s) }
function Is-Admin { ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
function Get-TaskXml {
  $psExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
  $args2 = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $ApplyPath + '"'
  @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>FC 27 Turbo country launcher: sets the game's Locale (registry) for the picked country's commentary language. Started only by schtasks /run; reads turbo_output\launch_profile_request.json.</Description>
    <URI>\$(Esc $TaskName)</URI>
  </RegistrationInfo>
  <Principals>
    <Principal id="Author">
      <UserId>$(Esc $user)</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <ExecutionTimeLimit>PT10M</ExecutionTimeLimit>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
  </Settings>
  <Triggers />
  <Actions Context="Author">
    <Exec>
      <Command>$(Esc $psExe)</Command>
      <Arguments>$(Esc $args2)</Arguments>
    </Exec>
  </Actions>
</Task>
"@
}

if ($Show) {
  $t = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
  if (-not $t) { Write-Host ($TaskName + ': not registered'); exit 0 }
  Write-Host ('{0}: {1}' -f $TaskName, $t.State)
  Write-Host ('  principal: {0} {1} {2}' -f $t.Principal.UserId, $t.Principal.LogonType, $t.Principal.RunLevel)
  foreach ($a in $t.Actions) { Write-Host ('  action: ' + $a.Execute + ' ' + $a.Arguments) }
  Write-Host ('  policy: ' + $t.Settings.MultipleInstances + ', limit ' + $t.Settings.ExecutionTimeLimit + ', triggers ' + @($t.Triggers).Count)
  exit 0
}
if ($WhatIf) {
  Write-Host ('WHATIF: would register the task ' + $TaskName + ' with this XML (nothing is registered):')
  Write-Host (Get-TaskXml)
  Write-Host ('WHATIF: the action file must exist at registration: ' + $ApplyPath + ' (' + $(if (Test-Path -LiteralPath $ApplyPath) { 'present' } else { 'NOT FOUND' }) + ')')
  exit 0
}
if (-not (Is-Admin)) { Write-Host 'REFUSED: run this from an elevated PowerShell (Run as administrator). It never elevates itself.' -ForegroundColor Red; exit 1 }
if ($Unregister) {
  if ($TaskName -ne 'FC27_Profile') { Write-Host 'REFUSED: only the task FC27_Profile is ever unregistered' -ForegroundColor Red; exit 1 }
  if (-not (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue)) { Write-Host ($TaskName + ' was not registered'); exit 0 }
  Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
  Write-Host ($TaskName + ' unregistered') -ForegroundColor Green
  exit 0
}
if (-not (Test-Path -LiteralPath $ApplyPath)) { Write-Host ('REFUSED: the action file does not exist: ' + $ApplyPath) -ForegroundColor Red; exit 1 }
if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) { Write-Host ('REFUSED: ' + $TaskName + ' is already registered (use -Unregister first, or -Show)') -ForegroundColor Red; exit 1 }
$xml = Get-TaskXml
Register-ScheduledTask -TaskName $TaskName -Xml $xml | Out-Null
Write-Host ('REGISTERED ' + $TaskName + '; action file md5 ' + (Get-FileHash -LiteralPath $ApplyPath -Algorithm MD5).Hash.ToLower()) -ForegroundColor Green
exit 0
