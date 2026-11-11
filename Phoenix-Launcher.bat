@echo off
rem Project Phoenix Launcher (BC2 bots): starts the launcher window without a console.
start "" powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0Phoenix-Launcher.ps1" %*
