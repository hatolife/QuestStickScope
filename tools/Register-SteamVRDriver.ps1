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

	throw "QuestStickScope SteamVR driver が見つかりません。Release ZIPを展開した状態で実行するか、-BuildDir を指定してください。"
}

function Resolve-SteamVRRoot {
	if ($SteamVRDir) {
		if (Test-Path (Join-Path $SteamVRDir "bin\win64\vrpathreg.exe")) {
			return (Resolve-Path $SteamVRDir).Path
		}
		throw "指定されたSteamVRDirに vrpathreg.exe が見つかりません: $SteamVRDir"
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

	throw "SteamVR が見つかりません。-SteamVRDir でSteamVRのインストール先を指定してください。"
}

$driverRoot = Resolve-DriverRoot
$steamVRRoot = Resolve-SteamVRRoot
$vrPathReg = Join-Path $steamVRRoot "bin\win64\vrpathreg.exe"

Write-Host "QuestStickScope driver: $driverRoot"
Write-Host "SteamVR: $steamVRRoot"

& $vrPathReg adddriver $driverRoot
if ($LASTEXITCODE -ne 0) {
	throw "vrpathreg.exe adddriver が失敗しました。終了コード: $LASTEXITCODE"
}

$registered = (& $vrPathReg show | Out-String)
if ($LASTEXITCODE -ne 0) {
	throw "vrpathreg.exe show が失敗しました。終了コード: $LASTEXITCODE"
}
if ($registered -notmatch [regex]::Escape($driverRoot)) {
	throw "登録後の確認に失敗しました。vrpathreg.exe show にQuestStickScopeのdriver pathがありません。"
}

Write-Host "QuestStickScope SteamVR driver registered."
if (Get-Process -Name vrserver -ErrorAction SilentlyContinue) {
	Write-Warning "SteamVR は現在起動中です。QuestStickScope ProbeをロードするにはSteamVRを完全終了してから再起動してください。"
} else {
	Write-Host "次にSteamVRを起動してください。"
}
