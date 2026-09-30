param(
	[string]$BuildDir = "",
	[string]$SteamVRDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-DriverRoot {
	$candidates = @()
	if ($BuildDir) {
		$candidates += (Join-Path $BuildDir "steamvr-driver\queststickscope")
	}

	$candidates += (Join-Path $PSScriptRoot "steamvr-driver\queststickscope")
	$parent = Split-Path $PSScriptRoot -Parent
	if ($parent) {
		$candidates += (Join-Path $parent "steamvr-driver\queststickscope")
		$candidates += (Join-Path $parent "build\windows-debug\steamvr-driver\queststickscope")
		$candidates += (Join-Path $parent "build\windows-release\steamvr-driver\queststickscope")
	}

	foreach ($candidate in $candidates | Select-Object -Unique) {
		$manifest = Join-Path $candidate "driver.vrdrivermanifest"
		if (Test-Path $manifest) {
			return (Resolve-Path $candidate).Path
		}
	}

	throw "QuestStickScope SteamVR driver was not found. Extract the release ZIP first, or specify -BuildDir."
}

function Resolve-SteamVRRoot {
	if ($SteamVRDir) {
		if (Test-Path (Join-Path $SteamVRDir "bin\win64\vrpathreg.exe")) {
			return (Resolve-Path $SteamVRDir).Path
		}
		throw "vrpathreg.exe was not found under the specified -SteamVRDir: $SteamVRDir"
	}

	$registryKeys = @(
		"HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820",
		"HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820"
	)
	foreach ($key in $registryKeys) {
		try {
			$installLocation = (Get-ItemProperty -Path $key -Name InstallLocation -ErrorAction Stop).InstallLocation
			if ($installLocation -and (Test-Path (Join-Path $installLocation "bin\win64\vrpathreg.exe"))) {
				return (Resolve-Path $installLocation).Path
			}
		} catch {
		}
	}

	$default = "${env:ProgramFiles(x86)}\Steam\steamapps\common\SteamVR"
	if (Test-Path (Join-Path $default "bin\win64\vrpathreg.exe")) {
		return (Resolve-Path $default).Path
	}

	throw "SteamVR was not found. Specify the SteamVR installation directory with -SteamVRDir."
}

$driverRoot = Resolve-DriverRoot
$steamVRRoot = Resolve-SteamVRRoot
$vrPathReg = Join-Path $steamVRRoot "bin\win64\vrpathreg.exe"

Write-Host "QuestStickScope driver: $driverRoot"
Write-Host "SteamVR: $steamVRRoot"

& $vrPathReg adddriver $driverRoot
if ($LASTEXITCODE -ne 0) {
	throw "vrpathreg.exe adddriver failed with exit code $LASTEXITCODE."
}

$registered = (& $vrPathReg show | Out-String)
if ($LASTEXITCODE -ne 0) {
	throw "vrpathreg.exe show failed with exit code $LASTEXITCODE."
}
if ($registered -notmatch [regex]::Escape($driverRoot)) {
	throw "Driver registration verification failed. QuestStickScope is not listed by vrpathreg.exe show."
}

Write-Host "QuestStickScope SteamVR driver registered."
if (Get-Process -Name vrserver -ErrorAction SilentlyContinue) {
	Write-Warning "SteamVR is running. Fully exit SteamVR and start it again so the QuestStickScope Probe can be loaded."
} else {
	Write-Host "Start SteamVR next."
}
