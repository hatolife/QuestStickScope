param(
	[string]$BuildDir = "build/windows-debug",
	[string]$SteamVRDir = "${env:ProgramFiles(x86)}\Steam\steamapps\common\SteamVR"
)

$ErrorActionPreference = "Stop"

$driverRoot = Join-Path $BuildDir "steamvr-driver\queststickscope"
$driverRoot = (Resolve-Path $driverRoot).Path
$vrPathReg = Join-Path $SteamVRDir "bin\win64\vrpathreg.exe"

if (-not (Test-Path $vrPathReg)) {
	throw "vrpathreg.exe が見つかりません: $vrPathReg"
}

if (-not (Test-Path (Join-Path $driverRoot "driver.vrdrivermanifest"))) {
	throw "SteamVR Probe のビルド成果物が見つかりません: $driverRoot"
}

& $vrPathReg adddriver $driverRoot
if ($LASTEXITCODE -ne 0) {
	throw "vrpathreg.exe adddriver が失敗しました。終了コード: $LASTEXITCODE"
}

Write-Host "QuestStickScope SteamVR driver registered: $driverRoot"
