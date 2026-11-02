@echo off
rem Cleanup-Turbo.cmd - keeps the Live Editor folder lean after Turbo updates (Turbo X 2.0). Close FC 27 first.
rem Lists what it would remove (nothing changes), asks, then moves it to the Recycle Bin. Details: Cleanup-Turbo.ps1 next to this file.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Cleanup-Turbo.ps1"
if not "%ERRORLEVEL%"=="0" ( echo. & echo  Cleanup refused - see the line above. & pause & exit /b 1 )
echo.
set /p ANS=Move the items above to the Recycle Bin? Type YES and press Enter: 
if /i not "%ANS%"=="YES" ( echo  Nothing changed. & pause & exit /b 0 )
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Cleanup-Turbo.ps1" -Apply
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo Done.) else (echo Finished with exit code %RC%.)
pause
exit /b %RC%
