@echo off
rem Attach-Turbo.cmd - run after every FCM 27 / Live Editor / mod-manager update: makes sure Turbo is present and intact in every
rem Live Editor folder (design: turbo_dev\turbox_20_assessment_20261101\handover\16_FCM27_COEXISTENCE.md section 3).
rem Never touches le_config.json, FCLiveEditor.DLL, CryptBase.dll, launchers, mods\, the game folder, FIFAModData or .fifamod files.
rem Usage: double-click, or Attach-Turbo.cmd [-DryRun] [-LeRoot "C:\other\Live Editor"]
rem Every path below comes from %~dp0 (this file's folder) or %SystemRoot%; it works from any current folder (also C:\Windows\System32).
setlocal
set "PS1=%~dp0Attach-Turbo.ps1"
set "PSEXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "RC=1"
if not exist "%PS1%" goto :nops1
if not exist "%PSEXE%" goto :nopsexe
echo Attach-Turbo: checking Turbo in every Live Editor folder. Do not close this window.
echo.
"%PSEXE%" -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" goto :ok
echo Attach-Turbo finished with exit code %RC% (1 = refused, nothing written; 2 = a folder failed). Read the lines above
echo and the Attach-Turbo_*.log file in %~dp0
goto :end
:ok
echo Attach-Turbo finished OK.
goto :end
:nops1
echo ERROR: Attach-Turbo.ps1 not found next to this file: "%PS1%"
goto :end
:nopsexe
echo ERROR: Windows PowerShell not found: "%PSEXE%"
goto :end
:end
if not "%TURBO_NOPAUSE%"=="1" pause
exit /b %RC%
