@echo off
rem Usage: build.bat [app|test|all]   (default: all)
rem Double-click friendly: the window stays open at the end. Set NOPAUSE=1 to disable (make.ps1 does).
setlocal
set "ERR=0"
set "DOPAUSE=0"
echo %cmdcmdline% | find /i "/c" >nul && set "DOPAUSE=1"
if defined NOPAUSE set "DOPAUSE=0"

set "PF86=C:\Program Files (x86)"
set "VSROOT=%PF86%\Microsoft Visual Studio\18\BuildTools"
if exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" goto havevs
for /f "delims=" %%i in ('"%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath') do set "VSROOT=%%i"
:havevs
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
  echo Visual Studio Build Tools not found. Install the "Desktop development with C++" workload.
  set "ERR=1"
  goto done
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
if not exist build mkdir build

set CORE=core\stats.cpp core\hitgrid.cpp core\renderer.cpp core\fileio.cpp core\png.cpp
set CFLAGS=/nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN

set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=all"
if /i "%WHAT%"=="test" goto test
if /i "%WHAT%"=="all" goto app
if /i "%WHAT%"=="app" goto app
echo Unknown target "%WHAT%" - use app, test or all.
set "ERR=1"
goto done

:app
cl %CFLAGS% /Fo:build\ /Fe:build\LineTracer.exe app\main_win32.cpp app\app_state.cpp app\status.cpp app\canvas.cpp app\session.cpp app\settings.cpp app\ui_theme.cpp app\ui_controls.cpp platform\win32\capture_win32.cpp %CORE% ^
  /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app\app.manifest ^
  user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib winmm.lib dwmapi.lib gdiplus.lib advapi32.lib
if errorlevel 1 ( set "ERR=1" & goto done )
if /i "%WHAT%"=="app" goto done

:test
cl %CFLAGS% /Fo:build\ /Fe:build\test_core.exe tests\test_core.cpp %CORE%
if errorlevel 1 ( set "ERR=1" & goto done )
build\test_core.exe build
if errorlevel 1 set "ERR=1"

:done
echo.
if "%ERR%"=="0" (echo BUILD OK) else (echo BUILD FAILED)
if "%DOPAUSE%"=="1" pause
exit /b %ERR%
