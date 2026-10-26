@echo off
setlocal
cd /d "%~dp0"
rem Adds the bots' accounts to the BF-Portable backend database (bf.a_emu_playerinfo). MySQL must be running.
set "MYSQL="
for %%P in ("D:\BF-Portable\xampp\mysql\bin\mysql.exe" "C:\BF-Portable\xampp\mysql\bin\mysql.exe" "C:\xampp\mysql\bin\mysql.exe") do if exist %%P set "MYSQL=%%~P"
if not defined MYSQL ( echo Could not find xampp\mysql\bin\mysql.exe - edit this file and set MYSQL. & pause & exit /b 1 )
echo Using %MYSQL%
echo Enter the MySQL root password when asked (just press Enter if you never set one).
"%MYSQL%" -u root -p < "%~dp0Phoenix-bot-accounts.sql"
if errorlevel 1 ( echo. & echo FAILED - see the message above. & pause & exit /b 1 )
echo.
echo Done. Set BotAccounts=1 in phoenix.ini (it is the default) and start the bots.
pause
