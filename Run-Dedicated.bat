@echo off
rem Copyright Francesco Di Blasi 2026 - All Rights Reserved
setlocal DisableDelayedExpansion
set "CRIMSON_LAUNCH_PORT=%~1"
if defined CRIMSON_LAUNCH_PORT (
    "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0framework\scripts\Run-LocalDemo.ps1" -Mode Dedicated -Port "%CRIMSON_LAUNCH_PORT%"
) else (
    "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0framework\scripts\Run-LocalDemo.ps1" -Mode Dedicated
)
set "CRIMSON_LAUNCH_RESULT=%ERRORLEVEL%"
if not "%CRIMSON_LAUNCH_RESULT%"=="0" pause
exit /b %CRIMSON_LAUNCH_RESULT%
