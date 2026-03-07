@echo off
setlocal

set MSYS2_MINGW=%MSYS2_MINGW%
if "%MSYS2_MINGW%"=="" set MSYS2_MINGW=C:\msys64\mingw64

if not exist "%MSYS2_MINGW%\bin\ninja.exe" (
    echo ERROR: ninja.exe not found under %MSYS2_MINGW%\bin
    echo Set MSYS2_MINGW to your mingw64 prefix if it is not C:\msys64\mingw64
    exit /b 1
)

set PATH=%MSYS2_MINGW%\bin;%MSYS2_MINGW%\..\usr\bin;%PATH%

:: ── Parse arguments ──────────────────────────────────────────────────────────
::  build.bat            →  build only
::  build.bat run        →  build then launch Tony.exe
::  build.bat launch     →  launch Tony.exe without building
::  build.bat clean      →  wipe build_mingw and reconfigure
::  build.bat help       →  print this help
:: ─────────────────────────────────────────────────────────────────────────────

set BUILD_DIR=%~dp0build_mingw
set ACTION=%1
if "%ACTION%"=="" set ACTION=build

if /i "%ACTION%"=="help" goto :help
if /i "%ACTION%"=="clean" goto :clean
if /i "%ACTION%"=="launch" goto :launch
if /i "%ACTION%"=="run" goto :build
if /i "%ACTION%"=="build" goto :build

echo ERROR: Unknown action "%ACTION%". Run "build.bat help" for usage.
exit /b 1

:: ── clean ────────────────────────────────────────────────────────────────────
:clean
echo Removing %BUILD_DIR% ...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
echo Reconfiguring with meson ...
meson setup "%BUILD_DIR%" --buildtype=debugoptimized
if errorlevel 1 exit /b %errorlevel%
echo Done. Run build.bat to compile.
exit /b 0

:: ── build ────────────────────────────────────────────────────────────────────
:build
if not exist "%BUILD_DIR%\build.ninja" (
    echo Build directory not configured. Running meson setup ...
    meson setup "%BUILD_DIR%" --buildtype=debugoptimized
    if errorlevel 1 exit /b %errorlevel%
)

echo Building Tony.exe ...
ninja -C "%BUILD_DIR%" Tony.exe
if errorlevel 1 (
    echo.
    echo BUILD FAILED. See errors above.
    exit /b %errorlevel%
)

echo.
echo Build succeeded: %BUILD_DIR%\Tony.exe
if /i "%ACTION%"=="run" goto :launch
exit /b 0

:: ── launch ───────────────────────────────────────────────────────────────────
:launch
set EXE=%BUILD_DIR%\Tony.exe
if not exist "%EXE%" (
    echo ERROR: %EXE% not found. Run "build.bat" first.
    exit /b 1
)
echo Launching %EXE% ...
start "" "%EXE%"
exit /b 0

:: ── help ─────────────────────────────────────────────────────────────────────
:help
echo.
echo   build.bat [action]
echo.
echo   Actions:
echo     (none)   Build Tony.exe  (default)
echo     run      Build Tony.exe then launch it
echo     launch   Launch Tony.exe without rebuilding
echo     clean    Delete the build directory and reconfigure from scratch
echo     help     Show this message
echo.
echo   Environment:
echo     MSYS2_MINGW   Path to the mingw64 prefix (default: C:\msys64\mingw64)
echo                   The bin\ subdirectory must contain ninja.exe, gcc.exe etc.
echo.
echo   The script prepends %%MSYS2_MINGW%%\bin and the msys2 usr\bin to PATH
echo   before invoking ninja, so moc.exe and the GCC runtime DLLs are found
echo   automatically regardless of which shell (PowerShell, cmd, git bash) you
echo   call this from.
echo.
exit /b 0
