@echo off
setlocal

set "script=%~dp0Unregister-SteamVRDriver.ps1"
if not exist "%script%" (
	echo ERROR: Unregister-SteamVRDriver.ps1 was not found.
	echo Expected: %script%
	echo.
	pause
	exit /b 1
)

echo Unregistering QuestStickScope SteamVR driver...
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%script%" %*
set "exitCode=%ERRORLEVEL%"

echo.
if "%exitCode%"=="0" (
	echo Unregistration completed successfully.
) else (
	echo Unregistration failed. Exit code: %exitCode%
)
echo.
echo Press any key to close.
pause >nul
exit /b %exitCode%
