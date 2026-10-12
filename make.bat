@echo off
rem Double-click or run: builds, tests and packages dist\LineTracer-Setup.exe. Extra args go to make.ps1 (-SkipTests, -Clean).
set "DOPAUSE=0"
echo %cmdcmdline% | find /i "/c" >nul && set "DOPAUSE=1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0make.ps1" %*
set "RC=%errorlevel%"
echo.
if "%DOPAUSE%"=="1" pause
exit /b %RC%
