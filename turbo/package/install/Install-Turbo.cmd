@echo off
rem One-click Turbo installer: double-click, or: Install-Turbo.cmd -LeRoot "C:\path\to\FC 27 Live Editor" [-DryRun] [-Yes]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Turbo.ps1" %*
set RC=%ERRORLEVEL%
echo.
if "%RC%"=="0" (echo Done.) else (echo Finished with exit code %RC%.)
pause
exit /b %RC%
