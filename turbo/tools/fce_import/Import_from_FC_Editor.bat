@echo off
rem Drag FC Editor ".player" files (or a folder of them) onto this file, or run it with no arguments to pick up the
rem player ids you type. Writes Turbo player presets to turbo_output\players; then in game use Turbo >
rem Players > Create player... (or Import) and pick the preset.
setlocal
if "%~1"=="" (
  set /p IDS=FC Editor workspace player ids, separated by spaces:
  set ARGS=
  for %%i in (%IDS%) do call set ARGS=%%ARGS%% --playerid %%i
  python "%~dp0fce_to_preset.py" %ARGS%
) else (
  python "%~dp0fce_to_preset.py" %*
)
pause
