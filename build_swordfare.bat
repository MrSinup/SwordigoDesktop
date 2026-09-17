@echo off
rem =============================================================================
rem build_swordfare.bat — Consumer Build Launcher for SwordigoDesktop (Windows)
rem
rem Default: Boots the native Qt6 SwordigoDesktop Build Studio GUI.
rem CLI Mode: Pass --nogui to compile directly from the command prompt.
rem =============================================================================

setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "BUILD_DIR=%ROOT_DIR%\build-win64"

set NOGUI=0
set CLEAN=0
set STRIP=0
set USE_DYNARMIC=0
set HOST_COMPILER=gcc

:parse_args
if "%~1"=="" goto after_args
if /i "%~1"=="--nogui" (
    set NOGUI=1
    shift
    goto parse_args
)
if /i "%~1"=="--clean" (
    set CLEAN=1
    shift
    goto parse_args
)
if /i "%~1"=="--strip" (
    set STRIP=1
    shift
    goto parse_args
)
if /i "%~1"=="--compiler-host" (
    set "HOST_COMPILER=%~2"
    shift
    shift
    goto parse_args
)
shift
goto parse_args

:after_args

rem ─── 1. GUI Mode (Default) ──────────────────────────────────────────────────
if %NOGUI%==0 (
    set "STUDIO_BIN=%ROOT_DIR%\binw\swordigo_build_studio.exe"
    if not exist "!STUDIO_BIN!" (
        echo Bootstrapping SwordigoDesktop Build Studio...
        cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DSWORDIGO_BUILD_STUDIO=ON
        cmake --build "%BUILD_DIR%" --target swordigo_build_studio
    )
    if exist "!STUDIO_BIN!" (
        start "" "!STUDIO_BIN!" --project-dir "%ROOT_DIR%"
        exit /b 0
    )
)

rem ─── 2. CLI Mode (--nogui) ───────────────────────────────────────────────────
echo ==================================================
echo   SwordigoDesktop — Consumer Build Engine (CLI)
echo ==================================================

if %CLEAN%==1 (
    echo Cleaning %BUILD_DIR%...
    rmdir /s /q "%BUILD_DIR%" 2>nul
)

set CMAKE_OPTS=-S "%ROOT_DIR%" -B "%BUILD_DIR%" -DSWORDIGO_BUILD_STUDIO=ON
if %STRIP%==1 (
    set CMAKE_OPTS=!CMAKE_OPTS! -DSWORDIGO_STRIP_RELEASE=ON
)

cmake !CMAKE_OPTS!
cmake --build "%BUILD_DIR%" --target swordfare

echo Build finished.
endlocal
