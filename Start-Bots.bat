@echo off
setlocal
pushd "%~dp0"
title Project Phoenix BC2 - bots
echo Project Phoenix BC2 - Battlefield Bad Company 2 bots
echo.
echo Waiting for the BC2 server (Frost.Game.Main_Win32_Final.exe) to start...
:wait
tasklist /FI "IMAGENAME eq Frost.Game.Main_Win32_Final.exe" 2>nul | find /I "Frost.Game.Main" >nul
if errorlevel 1 ( timeout /t 3 /nobreak >nul & goto wait )
bin\PhoenixAttach.exe --start %*
set "rc=%ERRORLEVEL%"
if "%rc%"=="5" (
  echo.
  echo The server runs as administrator, so the bots need it too. Windows will ask for permission...
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~dp0bin\PhoenixAttach.exe' -ArgumentList '--start --pause %*' -WorkingDirectory '%~dp0bin' -Verb RunAs"
  popd
  exit /b 0
)
echo.
pause
popd
exit /b %rc%
