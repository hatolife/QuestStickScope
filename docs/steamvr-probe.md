# SteamVR Probe

SteamVR Probe は OpenVR の server driver として `vrserver.exe` にロードされ、`IVRDriverInput` の scalar component 作成と更新を観測・必要に応じて補正する。

VRChat.exe 自体には注入・フック・メモリ書き換えを行わない。

## 仕組み

1. SteamVR が `driver_queststickscope.dll` を server driver としてロードする。
2. `HmdDriverFactory` が `IServerTrackedDeviceProvider` を返す。
3. `Init` で OpenVR server driver context を初期化する。
4. runtime が提供する `IVRDriverInput` の vtable から `CreateScalarComponent` と `UpdateScalarComponent` の実装アドレスを取得する。
5. MinHook で両関数をフックする。
6. component 作成時に path、handle、property container、scalar type、units を固定長テーブルへ登録する。
7. property container の `Prop_ControllerRoleHint_Int32` から Left / Right を判定する。取得できない場合は `Unknown` とする。
8. `/input/joystick/x`、`/input/joystick/y`、`/input/thumbstick/x`、`/input/thumbstick/y` をスティック X/Y として分類する。
9. component作成を捕捉できなかったhandleは、最初の更新時に `<unknown:handle>` として登録する。
10. scalar 更新時に raw 値を記録する。
11. GUI heartbeat と有効な補正設定がある場合だけ補正を計算する。
12. 元の `UpdateScalarComponent` へ補正後値を渡す。
13. raw/output、QPC timestamp、sequence、flags を共有メモリへ書く。

共有メモリは固定長で、入力更新処理をGUI待ちにしない。

## 補正

左右ごとに次を持つ。

```text
RAW
 |
 +-- Center Offset
 |
 +-- 360-direction Inner Deadzone
 |
 +-- 360-direction Outer Normalization
 |
 +-- Clamp
 |
OUTPUT
```

GUIから左右それぞれのCenter Offset、1°刻み360方向のInner/Outer半径テーブル、Clamp設定を共有メモリへ配布する。

方向別補正は、中心補正後の入力角度について隣接する1°テーブル値を補間して使用する。

GUI heartbeat が2秒以上途絶えた場合、Probe は補正設定が有効でも raw 値をそのまま通す。

## Unknown component

SteamVR のdriverロード順によっては、QuestStickScope がロードされる前に別driverが scalar component を作成済みの可能性がある。

その場合でも `UpdateScalarComponent` は観測できるため、未知handleを自動登録して値を失わない。

Unknown component には左右・意味を推測して割り当てない。Diagnostics で値変化を確認し、実測で識別する。

## ビルド

Visual Studio 2022 の Developer PowerShell で実行する。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
```

Probe の driver root:

```text
build/windows-debug/steamvr-driver/queststickscope/
├─ driver.vrdrivermanifest
└─ bin/
   └─ win64/
      └─ driver_queststickscope.dll
```

## SteamVR へ登録

SteamVR を終了してから実行する。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Register-SteamVRDriver.ps1
```

Steam ライブラリが標準位置でない場合:

```powershell
powershell -ExecutionPolicy Bypass -File tools/Register-SteamVRDriver.ps1 `
	-SteamVRDir "D:\SteamLibrary\steamapps\common\SteamVR"
```

登録後に SteamVR を再起動する。

## 観測確認

GUI:

```powershell
build/windows-debug/QuestStickScope.exe
```

CLI:

```powershell
build/windows-debug/QuestStickScopeCli.exe --steamvr-status
```

Diagnostics では component path、左右、semantic、raw/output、sequence を確認できる。

## 登録解除

SteamVR を終了してから実行する。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Unregister-SteamVRDriver.ps1
```

## フェイルセーフ

- Hook 導入前はパススルー。
- Hook 導入失敗時も既存入力を妨げない。
- GUI heartbeat がない場合はパススルー。
- 補正設定を正常に読めない場合はパススルー。
- Unknown component へ補正を適用しない。
- NaN / Inf は補正パイプライン外へ出さない。
- 高頻度更新で同期ログを出さない。
- 共有メモリ送信で入力スレッドを待たせない。
- `Cleanup` では Hook を先に解除してから共有メモリと OpenVR context を破棄する。
