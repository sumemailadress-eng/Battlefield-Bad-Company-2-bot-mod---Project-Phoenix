@echo off
setlocal
set "run=%~dp0reports\gunner-training-%RANDOM%-%RANDOM%"
mkdir "%run%" || exit /b 1
pushd "%run%" || exit /b 1
set "NAVDIR=."
set "QUIET=1"
set "ROADS=1"
set "HQBOX=5"
set "RUSH="
set "TANKS="
"%~dp0bin\PhoenixOffline.exe" 600 1 12345 >training.log
if errorlevel 1 goto failed
if exist navcache_hmmwv-seat1.aim (
  ren navcache_hmmwv-seat1.aim hmmwv-seat1.aim
  echo Trained profile: "%run%\hmmwv-seat1.aim"
  echo To test it in BC2, copy the profile into the active Phoenix navcache before starting the server.
) else echo No trial improved enough to save a profile. See "%run%\training.log".
popd
pause
exit /b 0
:failed
echo Simulation failed. See "%run%\training.log".
popd
pause
exit /b 1
