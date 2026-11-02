@echo off
rem _INSTALL_TURBOX_<VERSION>.cmd - template (Turbo X 2.0). Copy to the Live Editor folder as _INSTALL_TURBOX_<VERSION>.cmd and fill in
rem PKG (the unzipped package folder) and MD5 (its turbo\Turbo.dll). Close FC 27 first. Shows a progress bar for every step and writes
rem the same lines to the log next to the package; backs up every replaced file (Undo-Turbo.cmd), verifies md5s, rolls back by itself on
rem any failure, then cleans up old Turbo leftovers (turbo\Cleanup-Turbo.ps1, to the Recycle Bin). All paths are absolute.
setlocal
set "PKG=C:\FC 27 Live Editor\turbo_dev\releases\<VERSION>\pkg"
set "MD5=<32 hex md5 of pkg\turbo\Turbo.dll>"
set "LOG=C:\FC 27 Live Editor\turbo_dev\releases\<VERSION>\install_log.txt"
if not exist "%PKG%\install\Install-Turbo.ps1" ( echo  Package not found: %PKG% & pause & exit /b 2 )
echo.
echo  Installing Turbo ... the bar at the top of this window shows each step. Do NOT close this window.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PKG%\install\Install-Turbo.ps1" -LeRoot "C:\FC 27 Live Editor" -Source "%PKG%" -Yes -ExpectedDllMd5 %MD5% -LogFile "%LOG%"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" ( echo  INSTALL FAILED - nothing is left half-installed; see the lines above. The log: %LOG% & pause & exit /b %RC% )
echo  Done. The log: %LOG%
pause
exit /b 0
