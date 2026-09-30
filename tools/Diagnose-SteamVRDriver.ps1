param(
	[string]$SteamVRDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-DriverRoot {
	$candidates = @(
		(Join-Path $PSScriptRoot "steamvr-driver\queststickscope")
	)
	$parent = Split-Path $PSScriptRoot -Parent
	if ($parent) {
		$candidates += (Join-Path $parent "steamvr-driver\queststickscope")
		$candidates += (Join-Path $parent "build\windows-debug\steamvr-driver\queststickscope")
		$candidates += (Join-Path $parent "build\windows-release\steamvr-driver\queststickscope")
	}

	foreach ($candidate in $candidates | Select-Object -Unique) {
		if (Test-Path (Join-Path $candidate "driver.vrdrivermanifest")) {
			return (Resolve-Path $candidate).Path
		}
	}
	throw "QuestStickScope SteamVR driver was not found."
}

function Resolve-SteamVRRoot {
	if ($SteamVRDir) {
		$vrPathReg = Join-Path $SteamVRDir "bin\win64\vrpathreg.exe"
		if (Test-Path $vrPathReg) {
			return (Resolve-Path $SteamVRDir).Path
		}
		throw "vrpathreg.exe was not found under -SteamVRDir."
	}

	$registryKeys = @(
		"HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820",
		"HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820"
	)
	foreach ($key in $registryKeys) {
		try {
			$location = (Get-ItemProperty -Path $key -Name InstallLocation -ErrorAction Stop).InstallLocation
			if ($location -and (Test-Path (Join-Path $location "bin\win64\vrpathreg.exe"))) {
				return (Resolve-Path $location).Path
			}
		} catch {
		}
	}

	$default = "${env:ProgramFiles(x86)}\Steam\steamapps\common\SteamVR"
	if (Test-Path (Join-Path $default "bin\win64\vrpathreg.exe")) {
		return (Resolve-Path $default).Path
	}
	throw "SteamVR was not found."
}

function Write-Check {
	param(
		[string]$Name,
		[bool]$Ok,
		[string]$Detail = ""
	)

	$status = if ($Ok) { "OK" } else { "FAIL" }
	if ($Detail) {
		Write-Host ("[{0}] {1}: {2}" -f $status, $Name, $Detail)
	} else {
		Write-Host ("[{0}] {1}" -f $status, $Name)
	}
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class QssNativeLoader
{
	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	public static extern IntPtr LoadLibraryExW(string fileName, IntPtr file, uint flags);

	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
	public static extern IntPtr GetProcAddress(IntPtr module, string name);

	[DllImport("kernel32.dll", SetLastError = true)]
	public static extern bool FreeLibrary(IntPtr module);
}
'@

$driverRoot = Resolve-DriverRoot
$steamVRRoot = Resolve-SteamVRRoot
$manifestPath = Join-Path $driverRoot "driver.vrdrivermanifest"
$driverDll = Join-Path $driverRoot "bin\win64\driver_queststickscope.dll"
$openVrDll = Join-Path $driverRoot "bin\win64\openvr_api.dll"
$vrPathReg = Join-Path $steamVRRoot "bin\win64\vrpathreg.exe"
$vrServerLog = Join-Path (Split-Path $steamVRRoot -Parent | Split-Path -Parent) "logs\vrserver.txt"

Write-Host "QuestStickScope SteamVR diagnostics"
Write-Host "Driver root: $driverRoot"
Write-Host "SteamVR root: $steamVRRoot"
Write-Host ""

Write-Check "driver.vrdrivermanifest exists" (Test-Path $manifestPath) $manifestPath
Write-Check "driver_queststickscope.dll exists" (Test-Path $driverDll) $driverDll
Write-Check "openvr_api.dll exists" (Test-Path $openVrDll) $openVrDll

try {
	$manifest = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
	Write-Check "manifest name" ($manifest.name -eq "queststickscope") ("name=" + $manifest.name)
	Write-Check "manifest resourceOnly" (-not [bool]$manifest.resourceOnly) ("resourceOnly=" + $manifest.resourceOnly)
	Write-Check "manifest alwaysActivate" ([bool]$manifest.alwaysActivate) ("alwaysActivate=" + $manifest.alwaysActivate)
} catch {
	Write-Check "manifest parse" $false $_.Exception.Message
}

Write-Host ""
Write-Host "vrpathreg show:"
$paths = (& $vrPathReg show | Out-String)
Write-Host $paths.TrimEnd()
$registered = $paths -match [regex]::Escape($driverRoot)
Write-Check "QuestStickScope registered path" $registered $driverRoot

Write-Host ""
if (Test-Path $driverDll) {
	$module = [QssNativeLoader]::LoadLibraryExW($driverDll, [IntPtr]::Zero, 0x00000008)
	if ($module -eq [IntPtr]::Zero) {
		$errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
		$message = (New-Object ComponentModel.Win32Exception($errorCode)).Message
		Write-Check "Windows LoadLibraryEx" $false ("error=" + $errorCode + " " + $message)
	} else {
		Write-Check "Windows LoadLibraryEx" $true
		$factory = [QssNativeLoader]::GetProcAddress($module, "HmdDriverFactory")
		Write-Check "HmdDriverFactory export" ($factory -ne [IntPtr]::Zero)
		[QssNativeLoader]::FreeLibrary($module) | Out-Null
	}
}

Write-Host ""
$vrserver = Get-Process -Name vrserver -ErrorAction SilentlyContinue
if ($vrserver) {
	Write-Check "vrserver.exe running" $true ("PID=" + $vrserver.Id)
	try {
		$loaded = $vrserver.Modules | Where-Object {
			$_.ModuleName -ieq "driver_queststickscope.dll"
		}
		Write-Check "driver DLL loaded in vrserver.exe" ($null -ne $loaded) (
			if ($loaded) { $loaded.FileName } else { "not present in module list" }
		)
	} catch {
		Write-Host "[INFO] Could not enumerate vrserver.exe modules: $($_.Exception.Message)"
	}
} else {
	Write-Check "vrserver.exe running" $false "SteamVR is not running."
}

Write-Host ""
if (Test-Path $vrServerLog) {
	Write-Host "SteamVR log: $vrServerLog"
	Write-Host "Last QuestStickScope-related log lines:"
	$matches = Get-Content -Path $vrServerLog -Tail 10000 |
		Select-String -Pattern "queststickscope|driver_queststickscope" -CaseSensitive:$false
	if ($matches) {
		$matches | Select-Object -Last 80 | ForEach-Object { Write-Host $_.Line }
	} else {
		Write-Host "<no QuestStickScope-related lines found>"
	}
} else {
	Write-Host "[INFO] vrserver.txt was not found at: $vrServerLog"
}

Write-Host ""
Write-Host "Diagnostics complete."
