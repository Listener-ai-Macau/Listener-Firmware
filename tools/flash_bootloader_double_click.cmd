@echo off
setlocal EnableExtensions

set "NO_PAUSE=0"
for %%A in (%*) do (
    if /I "%%~A"=="-NoPause" set "NO_PAUSE=1"
    if /I "%%~A"=="--no-pause" set "NO_PAUSE=1"
)

cd /d "%~dp0.."
set "PS_EXE=pwsh.exe"
where pwsh.exe >nul 2>nul
if errorlevel 1 set "PS_EXE=powershell.exe"

"%PS_EXE%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash_bootloader_double_click.ps1" %*
set "EXITCODE=%ERRORLEVEL%"

echo.
if "%EXITCODE%"=="0" (
    echo Bootloader tool finished successfully.
) else (
    echo Bootloader tool failed with exit code %EXITCODE%.
)

if not "%NO_PAUSE%"=="1" (
    echo Press any key to close this window.
    pause >nul
)

exit /b %EXITCODE%
