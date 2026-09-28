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
- SteamVR Probe DLL の安全な最小スケルトン
- 補正処理とリングバッファのテスト

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

## 方針

QuestStickScope は汎用VR入力ツールを目指しません。未観測区間を推測値で埋めず、直接観測できる境界を増やしながら原因の切り分けと補正を進めます。
