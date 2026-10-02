# QuestStickScope

QuestStickScope は、Meta Quest 3 + Touch Plus Controller + Virtual Desktop + SteamVR + VRChat に対象を絞り、Windows PC 側でスティック入力を観測・記録・解析し、入力経路上の変化点を特定して補正するためのツールです。

仕様と開発順序は [PLAN.md](PLAN.md) を基準にします。

## 現在の実装

### SteamVR S0

- OpenVR server driver として `vrserver.exe` にロードする SteamVR Probe
- `IVRDriverInput::CreateScalarComponent` / `UpdateScalarComponent` の観測
- Controller role と component path による Left / Right / X / Y 分類
- component 作成を取り逃した場合の unknown handle 観測
- QPC timestamp、sequence、raw/output を共有メモリへ転送
- Center Offset
- 360方向（1°刻み）Directional Inner Deadzone
- 360方向（1°刻み）Directional Outer Normalization
- Max-zone 全体スケール
- 半径ベース Response Curve（-1.0〜+1.0）
- 時間ベース Smoothing（0〜100%）
- Clamp
- GUI heartbeat が途絶えた場合の自動パススルー
- 補正前後の同時記録

### GUI / Diagnostics

- Dear ImGui + ImPlot + Direct3D 11
- 左右スティック XY の raw / output 同時表示
- raw / output の直近軌跡
- Deadzone / Max zone の360方向境界オーバーレイ
- Deadzone / Max zone / Curve / Smooth の左右個別調整
- 10秒時系列 Raw X/Y / Output X/Y
- 平均、最小最大、標準偏差、半径、更新Hz
- scalar component 一覧
- componentごとの最新 raw/output/sequence
- sample gap 検出
- Windows W0 の Low Level Mouse Hook 観測
- Mouse move / wheel / button / injected flag の診断

### Calibration

- ボタン押下後5秒待機し、その後10秒間を自動計測
- Centerは中央値で中心を推定
- Center計測から1°ごとの内周 min/max と有効境界を生成
- Outer計測から1°ごとの外周 min/max と有効境界を生成
- 内周は近傍±2°のP99を基準に安全マージンを加えて決定
- 外周は近傍±2°のP95を採用
- 未取得方向は円周方向に補間
- 0〜359°の実測min/max/effective値をGUIで確認可能
- 左右個別適用
- `%LOCALAPPDATA%\QuestStickScope\calibration.json` への保存・復元
- 設定破損時は補正OFFの安全な既定値へフォールバック

### Record / Replay

- version付き `.qssrec` バイナリ形式
- component table
- timestamp / sequence / raw / output / flags
- 記録開始時の補正設定とProbe状態
- 録画開始時の左右XY状態
- `%LOCALAPPDATA%\QuestStickScope\recordings\` への自動保存
- 保存済み記録のGUI読込
- Replay位置の移動
- 現在の補正設定を同じ記録へ再適用
- Smoothing は記録済み timestamp / QPC frequency を使って再現

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

Windows Debug では主に次が生成されます。

```text
build/windows-debug/
├─ QuestStickScope.exe
├─ QuestStickScopeCli.exe
└─ steamvr-driver/
   └─ queststickscope/
      ├─ driver.vrdrivermanifest
      └─ bin/
         └─ win64/
            └─ driver_queststickscope.dll
```

## SteamVR Probe の登録

`QuestStickScope.exe` は起動時に SteamVR driver の登録状態を自動確認します。

- 配布ZIPまたはビルドディレクトリの `steamvr-driver/queststickscope` を現在のdriver rootとして扱います。
- SteamVRのインストール先は公式のアンインストールレジストリ情報から検出し、標準インストール先もフォールバックとして確認します。
- `vrpathreg finddriver queststickscope` で現在の登録先を確認します。
- 未登録なら自動登録します。
- 別の展開先を指している旧登録、重複登録、残存している旧 `queststickscope` driver path があれば削除してから現在のdriver rootを登録します。
- 既に現在のdriver rootだけが登録済みなら何もしません。

登録内容を変更した起動ではGUI上部とDiagnosticsに再起動案内を表示します。
SteamVRが起動中だった場合は、一度SteamVRを完全終了して再起動すると新しいProbeが読み込まれます。

登録・解除用のBAT/PowerShellスクリプトは不要です。
GUIを起動せず手動で登録解除する必要がある場合だけ、SteamVR付属の `vrpathreg.exe removedriverswithname queststickscope` を使用してください。

SteamVR と Virtual Desktop を起動した後、GUIを起動します。

```powershell
build/windows-debug/QuestStickScope.exe
```

CLIで Probe 状態を確認する場合:

```powershell
build/windows-debug/QuestStickScopeCli.exe --steamvr-status
```

## 最初の実機確認

1. QuestStickScope を起動し、SteamVR driver の登録状態が `Current` または `Updated` になることを確認する。
2. `Updated` になった場合は SteamVR を完全終了して再起動する。
3. Virtual Desktop で Quest 3 を接続する。
4. Diagnostics で `SteamVR Probe: Observing` を確認する。
5. 左右スティックを動かし、Left / Right / Joystick X/Y が更新されるか確認する。
6. 分類できない scalar がある場合は `<unknown:handle>` の値変化を確認する。
7. Live で左右XYと時系列が更新されることを確認する。
8. 補正を有効化し、Raw と Output が変化することを確認する。
9. QuestStickScope を終了し、入力が元のパススルーへ戻ることを確認する。

Virtual Desktop の仮想デスクトップ側は、SteamVRなしでも Diagnostics の Windows W0 / Mouse でイベントを観測できます。

## 方針

QuestStickScope は汎用VR入力ツールを目指しません。

未観測区間を推測値で埋めず、直接観測できる境界を増やしながら原因の切り分けと補正を進めます。


## CI / Release

`main` への push または GitHub Actions の手動実行で Windows Release ビルドとテストを行います。

成功した場合だけ、対象コミットへ次の形式でタグを作成して GitHub Release を公開します。

```text
v0.0.1.<JSTコミット時刻 yyyyMMddHHmmss>.<7桁コミットハッシュ>
```

例:

```text
v0.0.1.20260930223015.1a2b3c4
```

Release には次の ZIP を添付します。

```text
QuestStickScope-<tag>-windows-amd64.zip
```

ZIP には GUI、CLI、SteamVR driver、`openvr_api.dll`、診断スクリプト、README、PLAN を含めます。


## SteamVR Probe diagnostics

If `SteamVR S0: Offline` remains after registration and a complete SteamVR restart, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Diagnose-SteamVRDriver.ps1
```

The script checks the manifest, packaged DLLs, `vrpathreg` registration, Windows DLL loading, the `HmdDriverFactory` export, whether the driver module is loaded into `vrserver.exe`, and relevant lines from `vrserver.txt`.
