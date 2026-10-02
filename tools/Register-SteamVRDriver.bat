@echo off
setlocal

set "script=%~dp0Register-SteamVRDriver.ps1"
if not exist "%script%" (
	echo ERROR: Register-SteamVRDriver.ps1 was not found.
	echo Expected: %script%
	echo.
	pause
	exit /b 1
)

echo Registering QuestStickScope SteamVR driver...
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%script%" %*
set "exitCode=%ERRORLEVEL%"

echo.
if "%exitCode%"=="0" (
	echo Registration completed successfully.
) else (
	echo Registration failed. Exit code: %exitCode%
)
echo.
echo Press any key to close.
pause >nul
exit /b %exitCode%
