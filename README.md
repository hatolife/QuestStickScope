# QuestStickScope

QuestStickScope は、Meta Quest 3 + Touch Plus Controller + Virtual Desktop + SteamVR + VRChat に対象を絞り、Windows PC 側でスティック入力を観測・記録・解析し、入力経路上の変化点を特定して補正するためのツールです。

現在は開発初期段階です。仕様と開発順序は [PLAN.md](PLAN.md) を基準にします。

## 現在の実装

- C++20 / CMake / Ninja のビルド基盤
- スティック入力の共通データ型
- Center Offset
- 2次元 Inner Deadzone
- 方向別 Outer Normalization
- Clamp
- 固定長 SPSC リングバッファ
- Windows の QPC を使う単調増加クロック
- SteamVR server driver としてロード可能な Probe
- `IVRDriverInput` scalar component の作成・更新観測
- Controller role と component path による左右/X/Y分類
- Probe → GUI 用の固定長共有メモリプロトコル
- `--steamvr-status` による Probe/component/最新値確認
- 補正処理とリングバッファの GoogleTest

SteamVR Probe の詳細は [docs/steamvr-probe.md](docs/steamvr-probe.md) を参照してください。

## ビルド

Visual Studio 2022 の Developer PowerShell で実行します。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Release:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

## SteamVR Probe の確認

ビルド後、SteamVR を終了した状態で driver を登録します。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Register-SteamVRDriver.ps1
```

SteamVR と Virtual Desktop を起動した後、観測状態を表示します。

```powershell
build/windows-debug/QuestStickScope.exe --steamvr-status
```

現在の Probe は観測専用です。入力値は変更しません。

## 方針

QuestStickScope は汎用VR入力ツールを目指しません。未観測区間を推測値で埋めず、直接観測できる境界を増やしながら原因の切り分けと補正を進めます。
