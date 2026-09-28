# SteamVR Probe

SteamVR Probe は OpenVR の server driver として `vrserver.exe` にロードされ、`IVRDriverInput` の scalar component 作成と更新を観測する。

現段階では入力値を変更しない。観測に失敗した場合も元の `IVRDriverInput::UpdateScalarComponent` をそのまま呼び、SteamVR の入力経路を維持する。

## 仕組み

1. SteamVR が `driver_queststickscope.dll` を server driver としてロードする。
2. `HmdDriverFactory` が `IServerTrackedDeviceProvider` を返す。
3. `Init` で OpenVR server driver context を初期化する。
4. runtime が提供する `IVRDriverInput` の vtable から `CreateScalarComponent` と `UpdateScalarComponent` の実装アドレスを取得する。
5. MinHook で両関数を観測する。
6. component 作成時に path、handle、property container、scalar type、units を固定長テーブルへ登録する。
7. property container の `Prop_ControllerRoleHint_Int32` から Left / Right を判定する。取得できない場合は `Unknown` のまま保持する。
8. `/input/joystick/x`、`/input/joystick/y`、`/input/thumbstick/x`、`/input/thumbstick/y` をスティック X/Y として分類する。それ以外は `Unknown` のまま保持する。
9. scalar 更新時は QPC timestamp と値を共有メモリの固定長リングへ書く。
10. `QuestStickScope.exe --steamvr-status` が共有メモリを読み、観測状態を表示する。

共有メモリ名は `Local\QuestStickScope.SteamVR.v1` とする。

## ビルド

Visual Studio 2022 の Developer PowerShell で実行する。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
```

Probe の driver root は次に生成される。

```text
build/windows-debug/steamvr-driver/queststickscope/
├─ driver.vrdrivermanifest
└─ bin/
   └─ win64/
      └─ driver_queststickscope.dll
```

## SteamVR へ登録

SteamVR を終了した状態で実行する。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Register-SteamVRDriver.ps1
```

Steam ライブラリが標準位置でない場合は `-SteamVRDir` を指定する。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Register-SteamVRDriver.ps1 `
	-SteamVRDir "D:\SteamLibrary\steamapps\common\SteamVR"
```

登録後に SteamVR を起動する。

## 観測確認

```powershell
build/windows-debug/QuestStickScope.exe --steamvr-status
```

Probe が正常にロードされている場合、状態と scalar component 一覧が表示される。

Virtual Desktop 接続後に左右スティックを操作し、左右と X/Y が期待どおり分類されるか確認する。`Unknown` が残る場合は、その component path と property 情報を Diagnostics の対象として扱い、推測で分類しない。

## 登録解除

SteamVR を終了してから実行する。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Unregister-SteamVRDriver.ps1
```

## フェイルセーフ

- Hook 導入前はパススルー状態とする。
- Hook 導入に失敗しても driver 自体の初期化は成功扱いとし、既存入力を妨げない。
- scalar 更新 hook は元関数を無加工の値で呼ぶ。
- 高頻度更新では同期ログを出力しない。
- 共有メモリが利用できない場合は hook を導入しない。
- `Cleanup` では hook を先に解除してから共有メモリと OpenVR context を破棄する。
