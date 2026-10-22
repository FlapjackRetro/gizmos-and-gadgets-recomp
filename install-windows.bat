@echo off
rem Double-click to install: runs install-windows.ps1 without changing the system's
rem PowerShell execution policy. Arguments are passed through, e.g. -SsgExe C:\DOS\Gizmos\ssg.exe
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-windows.ps1" %*
echo.
pause
