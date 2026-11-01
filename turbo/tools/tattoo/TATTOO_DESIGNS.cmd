@echo off
rem Turbo X: tattoo design tiles from the FC 27 game files (READ-ONLY on the game; the game may stay open).
rem Writes only: turbo_output\pictures\assets\tattoo_design\<id>.png and turbo_output\cache\tattoo_design_dds\.
setlocal
set "HERE=C:\FC 27 Live Editor\turbo_dev\tattoo"
set "LOG=C:\FC 27 Live Editor\turbo_dev\tattoo\tattoo_designs.log"
set "GAME=C:\Program Files\EA Games\EA SPORTS FC 27"
set "LE=C:\FC 27 Live Editor"
set "OUT=C:\FC 27 Live Editor\turbo_output"
if not exist "%HERE%\tattoo_designs.py" (
  echo Missing "%HERE%\tattoo_designs.py"
  pause
  exit /b 2
)
if not exist "%HERE%\kit_chunks.py" (
  echo Missing "%HERE%\kit_chunks.py"
  pause
  exit /b 2
)
if not exist "%LE%\turbo\tools\asset_catalog\frostbite.py" (
  echo Missing "%LE%\turbo\tools\asset_catalog\frostbite.py"
  pause
  exit /b 2
)
if not exist "%GAME%\FC27.exe" (
  echo Missing "%GAME%\FC27.exe"
  pause
  exit /b 2
)
cd /d "%HERE%"
if errorlevel 1 (
  echo Cannot enter "%HERE%"
  pause
  exit /b 2
)
echo started %date% %time% > "%LOG%"
set "PYRUN="
where py >nul 2>&1 && set "PYRUN=py -3"
if not defined PYRUN where python >nul 2>&1 && set "PYRUN=python"
if not defined PYRUN (
  echo NO WINDOWS PYTHON found ^(py or python^) >> "%LOG%"
  echo NO WINDOWS PYTHON found ^(py or python^)
  pause
  exit /b 9
)
echo python: %PYRUN% >> "%LOG%"
%PYRUN% -I "%HERE%\tattoo_designs.py" extract --game "%GAME%" --le "%LE%" --out "%OUT%" >> "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
echo exit=%RC% >> "%LOG%"
type "%LOG%" | findstr /b "Turbo TEXTURES CATALOG TILES NO DONE exit"
echo.
echo Full log: "%LOG%"
if not "%RC%"=="0" (
  echo FAILED with exit code %RC%
  pause
  exit /b %RC%
)
exit /b 0
