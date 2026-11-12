@echo off
rem Copyright Francesco Di Blasi 2026 - All Rights Reserved
setlocal DisableDelayedExpansion
set "CRIMSON_LAUNCH_INSTANCES=%~1"
set "CRIMSON_LAUNCH_PORT=%~2"
if not defined CRIMSON_LAUNCH_INSTANCES set "CRIMSON_LAUNCH_INSTANCES=4"
if not defined CRIMSON_LAUNCH_PORT set "CRIMSON_LAUNCH_PORT=24830"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0framework\scripts\Run-LocalDemo.ps1" -Mode Lan -Instances "%CRIMSON_LAUNCH_INSTANCES%" -Port "%CRIMSON_LAUNCH_PORT%"
set "CRIMSON_LAUNCH_RESULT=%ERRORLEVEL%"
if not "%CRIMSON_LAUNCH_RESULT%"=="0" pause
exit /b %CRIMSON_LAUNCH_RESULT%
