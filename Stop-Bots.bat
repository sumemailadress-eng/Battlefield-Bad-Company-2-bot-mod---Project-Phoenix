@echo off
setlocal
pushd "%~dp0"
bin\PhoenixAttach.exe --stop %*
if "%ERRORLEVEL%"=="5" (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~dp0bin\PhoenixAttach.exe' -ArgumentList '--stop --pause %*' -WorkingDirectory '%~dp0bin' -Verb RunAs"
  popd
  exit /b 0
)
echo.
pause
popd
