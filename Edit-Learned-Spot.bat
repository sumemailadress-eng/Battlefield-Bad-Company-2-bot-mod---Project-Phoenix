@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0Edit-Learned-Spot.ps1"
if errorlevel 1 pause
