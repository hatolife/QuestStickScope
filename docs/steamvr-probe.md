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
 +-- Max-zone Scale
 |
 +-- Response Curve
 |
 +-- Clamp
 |
 +-- Smoothing
 |
OUTPUT
```

GUIから左右それぞれのCenter Offset、1°刻み360方向のInner/Outer半径テーブル、Max-zone Scale、Response Curve、Smoothing、Clamp設定を共有メモリへ配布する。

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

GUIの `QuestStickScope.exe` が起動時に登録状態を自動管理する。

1. 実行中の `QuestStickScope.exe` と同じディレクトリにある `steamvr-driver/queststickscope` を現在のdriver rootとして解決する。
2. SteamVR付属の `vrpathreg.exe finddriver queststickscope` で登録状態を確認する。
3. 現在のdriver rootだけが登録されていれば変更しない。
4. 未登録なら現在のdriver rootを登録する。
5. 別の展開先、重複登録、旧 `queststickscope` path が残っていれば削除してから現在のdriver rootを登録する。
6. 登録内容を変更した場合はGUIにSteamVR再起動案内を表示する。

これによりRelease ZIPを別フォルダへ展開して更新した場合も、旧Releaseの登録先を手作業で解除する必要はない。
SteamVRが既に起動している状態で登録が更新された場合は、SteamVRを完全終了して再起動する。

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

通常利用では登録解除操作は不要。
QuestStickScopeを削除する場合は、GUIを終了した後にSteamVR付属の `vrpathreg.exe removedriverswithname queststickscope` を1回実行してからファイルを削除する。

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
