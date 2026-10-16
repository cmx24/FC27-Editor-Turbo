@echo off
rem Rolls back the Turbo install whose backup folder holds this file (the folder is the default).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Undo-Turbo.ps1" %*
set RC=%ERRORLEVEL%
echo.
if "%RC%"=="0" (echo Undo done.) else (echo Finished with exit code %RC%.)
pause
exit /b %RC%
