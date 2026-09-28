# QuestStickScope 開発計画

## 1. 目的

QuestStickScope は、Meta Quest 3 の Touch Plus Controller のスティック入力を Windows PC 側で観測・記録・解析し、入力経路のどこで値が変化しているかを切り分けた上で補正するための専用ツールである。

対象環境は次に固定する。

- Windows 11
- Meta Quest 3
- Touch Plus Controller
- Virtual Desktop / Virtual Desktop Streamer
- SteamVR
- VRChat

汎用的なVR入力補正ツールは目指さない。対象環境で「原因を特定できること」と「実際に補正できること」を優先する。

---

## 2. 完成条件

以下をすべて満たした状態を QuestStickScope の完成形とする。

### 2.1 観測・診断

- 左右スティックの X/Y 入力をリアルタイム表示できる。
- PC側で観測可能な複数レイヤーを同一時間軸で比較できる。
- 隣接レイヤー間の差分を表示できる。
- 無操作時の中心ずれ、ノイズ、方向ごとの外周不足、入力範囲の歪みを確認できる。
- 更新頻度、サンプル欠落、重複、タイミングの揺れを確認できる。
- 入力を記録し、後から同じデータを再生・解析できる。
- 直接観測できない区間は「未観測」と明示し、推測値を実測値として表示しない。

### 2.2 SteamVR / VRChat

- Virtual Desktop から SteamVR へ渡される左右スティック入力を観測できる。
- 補正前と補正後の値を同時表示できる。
- 補正を有効にすると VRChat の移動・旋回へ補正結果が反映される。
- 補正を無効にすると元の入力を変更せず通過させる。
- GUIが終了・異常終了しても、入力経路を壊さず安全にパススルーへ戻る。

### 2.3 Virtual Desktop 仮想デスクトップ

- Quest コントローラーによる仮想デスクトップ操作のうち、スティック由来の入力を Windows 側で観測できる。
- 無操作時のドリフトによる意図しないスクロール等を抑制できる。
- 意図したスティック操作は維持する。
- SteamVR を起動していない状態でも補正が機能する。

### 2.4 キャリブレーション

- 左右を個別にキャリブレーションできる。
- 中心位置と中心付近のノイズを自動計測できる。
- 方向ごとの最大入力範囲を自動計測できる。
- キャリブレーション結果から補正パラメータを生成できる。
- 結果を保存し、次回起動時に自動適用できる。
- 同じ記録データに補正前後を適用して比較できる。

---

## 3. 対象外

本プロジェクトでは次を対象外とする。

- Quest 2、Quest Pro、Quest 3S 等への対応保証
- Index、Vive、Windows Mixed Reality 等の別コントローラー対応
- Air Link、Steam Link 等の別ストリーミング経路への対応
- OpenXR アプリ全般への汎用対応
- VRChat プロセスへの DLL 注入や改変
- Quest OS 内部やコントローラー物理 ADC 値の直接取得
- 一般配布を前提とした自動アップデーター、多言語化、インストーラー

対象外項目は、対象環境で実測上必要になった場合だけ再検討する。

---

## 4. 入力経路

想定する入力経路は大きく2系統ある。

```text
Quest 3
  │
Touch Plus Controller
  │
Quest Runtime
  │
Virtual Desktop Quest App
  │
Network
  │
Virtual Desktop Streamer
  │
  ├──────────────────────────────┐
  │                              │
  ▼                              ▼
仮想デスクトップ系              PCVR系
  │                              │
Windows Input                    Virtual Desktop OpenVR Driver
  │                              │
Mouse / Wheel 等                 IVRDriverInput
                                 │
                                 ▼
                               SteamVR
                                 │
                                 ▼
                            SteamVR Input
                                 │
                                 ▼
                               VRChat
```

QuestStickScope は補正位置を先に固定しない。

各観測点の値を比較し、以下を分離して判断する。

1. 最初に異常が現れる地点
2. 値が意図せず変換される地点
3. 安定して補正を挿入できる地点

---

## 5. 観測レイヤー

| ID | レイヤー | 観測 | 補正 | 役割 |
|---|---|---:|---:|---|
| Q0 | コントローラー物理入力 | 不可 | 不可 | PCから直接取得できない起点 |
| Q1 | Quest Runtime 入力 | 不可 | 不可 | PCから直接観測できない |
| V0 | Virtual Desktop Quest 側 | 不可 | 不可 | Quest側アプリ内部 |
| V1 | Virtual Desktop Streamer が扱うコントローラー状態 | 調査して対応 | 調査して対応 | Desktop/PCVR 分岐前後の切り分け |
| W0 | Windows へ出力された Mouse / Wheel 等 | 可 | 可 | 仮想デスクトップ操作の最終境界 |
| S0 | Virtual Desktop OpenVR Driver → SteamVR | 可 | 可 | PCVR側の主要観測・補正点 |
| S1 | SteamVR Input 変換後 | 可能な範囲で可 | 原則設定確認のみ | SteamVR側変換の確認 |
| A0 | VRChat 内の結果 | 挙動で確認 | 不可 | 最終受け入れ確認 |

GUIでは Q0～V0 のような未観測区間もパイプライン上に表示するが、値は表示しない。

---

## 6. 技術構成

### 6.1 基本環境

- C++20
- Visual Studio 2022
- MSVC
- CMake
- Ninja
- x64 のみ

対象が Windows 11 + Quest 3 に固定されるため、32bit、他コンパイラ、他OSへの移植性は要求しない。

### 6.2 使用ライブラリ

| 用途 | 採用 | 理由 |
|---|---|---|
| SteamVR Driver API | Valve OpenVR | SteamVR の driver/input 境界を扱うために必要 |
| 関数フック | MinHook | Windows x64 で必要最小限の関数フックを行うため |
| GUI | Dear ImGui | 計測器型UIを小さな実装で構築しやすい |
| グラフ | ImPlot | 時系列・XY・散布図を ImGui と同じ描画系で扱える |
| 描画 | Direct3D 11 | Windows 固定で依存が少なく、ImGuiとの組み合わせが安定している |
| JSON | nlohmann/json | 設定・キャリブレーション保存に十分 |
| ログ | spdlog | GUIとDLL双方で軽量にログを扱える |
| テスト | GoogleTest | 補正・キャリブレーション・記録再生の単体テスト用 |

Eigen 等の線形代数ライブラリは導入しない。必要な処理は2次元ベクトルと補間が中心であり、標準ライブラリだけで十分だからである。

依存は CMake から固定したタグまたはコミットを取得し、再現可能なビルドにする。依存バージョンの自動追従は行わない。

---

## 7. 実行コンポーネント

### 7.1 QuestStickScope.exe

常駐するメインアプリケーション。

担当:

- GUI
- 入力データ集約
- Windows Input の観測
- 補正設定
- キャリブレーション
- Record / Replay
- ログ表示
- Probe の状態監視

### 7.2 SteamVR Probe

`vrserver.exe` 内で動作するモジュール。

担当:

- SteamVR Driver Input の入力コンポーネント把握
- 左右スティック X/Y の特定
- 補正前値の記録
- 設定された補正の適用
- 補正後値の記録
- GUIが存在しない場合の安全なパススルー

SteamVR の公開APIだけで他ドライバーの入力を十分に観測できない部分については、必要最小限のフックを使用する。

### 7.3 Virtual Desktop Probe

Windows公開APIの観測だけでは仮想デスクトップ系のスティック値を十分に特定・補正できない場合に使用する。

担当:

- Virtual Desktop Streamer 内で扱われる対象入力の特定
- Desktop入力へ変換される前のスティック値の観測
- 必要な場合の補正適用

Virtual Desktop の内部実装へ依存するため、Streamer の実行ファイルまたは対象モジュールのバージョン・ハッシュを確認し、認識していないビルドにはフックしない。

### 7.4 IPC

`QuestStickScope.exe` と各 Probe の通信には Windows の共有メモリを使用する。

用途:

- リアルタイムサンプル転送
- 補正設定の配布
- Probe 状態通知
- バージョン・互換性情報通知

高頻度サンプルは固定長リングバッファとし、GUI側が遅れても入力処理側を待たせない。

---

## 8. データモデル

最低限、各サンプルは以下を持つ。

```text
timestamp
layer
hand
axis
raw_value
output_value
sequence
flags
```

XYを一組として扱う解析では、同一レイヤー・同一手・近接 timestamp の X/Y を組み合わせる。

timestamp は Windows PC 上では `QueryPerformanceCounter()` を共通基準とする。

観測値には状態を付与する。

- `Observed`: そのレイヤーで直接取得した値
- `Derived`: 観測値から算出した値
- `Corrected`: 補正後の値
- `Unavailable`: 観測不能
- `Dropped`: サンプル欠落を検出
- `UnknownComponent`: 入力コンポーネントを特定できない

実測値と算出値をGUIで混同しない。

---

## 9. 可視化

### 9.1 Pipeline View

入力経路をパイプラインとして表示する。

各ノードに以下を表示する。

- レイヤー名
- 観測可否
- 現在の X/Y
- 更新Hz
- 補正有無
- Probe状態

ノードを選択すると詳細表示を切り替える。

### 9.2 XY View

以下を重ねて表示する。

- 現在位置
- 最近の軌跡
- キャリブレーションで計測した中心領域
- 計測した外周
- 補正後の外周
- 単位円

左右スティックは個別表示する。

### 9.3 Time View

同一時間軸に以下を重ねる。

- X
- Y
- 半径
- 補正前
- 補正後
- 選択した複数レイヤー

ズーム、パン、区間選択を可能にする。

### 9.4 Layer Difference View

隣接する観測レイヤーについて以下を表示する。

```text
ΔX
ΔY
Δradius
timestamp差
```

どの境界で値が変化したかを直接確認するための画面とする。

### 9.5 Statistics

選択区間について最低限以下を表示する。

- X/Y 平均
- X/Y 最小・最大
- 中心からの平均半径
- 最大半径
- 標準偏差
- 更新Hz
- 欠落サンプル数

---

## 10. 補正モデル

補正は必要な機能に限定する。

```text
RAW
 │
 ├─ Center Offset
 │
 ├─ Inner Deadzone
 │
 ├─ Directional Outer Normalization
 │
 └─ Clamp
      │
      ▼
OUTPUT
```

各処理は左右独立して有効・無効を切り替えられる。

### 10.1 Center Offset

無操作時の中心位置を原点へ移す。

```text
x1 = x - centerX
y1 = y - centerY
```

### 10.2 Inner Deadzone

中心付近のドリフトとノイズを除去する。

X/Y別ではなく原則として2次元の半径で判定し、デッドゾーン外の入力範囲が不自然に縮まないよう再スケールする。

中心ノイズに明確な方向差がある場合は、キャリブレーション結果から方向別境界を生成できるようにする。

### 10.3 Directional Outer Normalization

スティックを外周まで倒した際の到達半径を角度ごとに記録し、その方向の最大値が1.0になるよう正規化する。

外周テーブルは64方向を初期値とし、隣接点を補間して使用する。64方向が実測上過剰または不足なら、記録データを基に変更する。

### 10.4 Clamp

最終出力を有効範囲へ制限する。

### 10.5 初期完成要件に含めない補正

以下は初期の完成要件に含めない。

- ローパスフィルター
- 移動平均
- 加速度ベースの予測
- 複雑なレスポンスカーブ

これらは入力遅延や操作感の変化を生み、原因分析を難しくする。実測で必要性が確認された場合のみ追加する。

---

## 11. キャリブレーション

キャリブレーションは左右別に行う。

### 11.1 Center

スティックから指を離した状態で数秒間取得する。

算出対象:

- 中心 X/Y
- ノイズ分布
- 無操作時最大半径

単純平均だけに依存せず、外れ値の影響を受けにくい中央値等も比較し、記録データで採用方法を決定する。

### 11.2 Outer Range

スティックを外周へ押し付けた状態で数周回してもらう。

角度ごとに最大到達半径を蓄積し、外れ値を除いた外周テーブルを生成する。

GUI上では取得済み方向と不足方向をリアルタイム表示する。

### 11.3 Validation

生成した補正をその場で適用し、以下を確認する。

- 中心が安定して0になる。
- 全方向で外周がおおむね1.0へ到達する。
- 斜め入力が不自然に変形しない。
- 小さい意図入力を消しすぎない。

問題があれば保存せず再計測できる。

---

## 12. Record / Replay

実機を装着したまま試行錯誤しなくて済むよう、観測機能と早い段階で実装する。

### 12.1 Record

記録対象:

- 選択した全観測レイヤー
- 補正前後
- timestamp
- sequence
- Probe状態
- 使用中の補正設定

### 12.2 Replay

記録データを入力源として読み込み、実機なしで以下を行えるようにする。

- グラフ再生
- 区間選択
- 補正パラメータ変更
- キャリブレーションアルゴリズム再実行
- 補正前後比較

Replay は SteamVR や Virtual Desktop へ入力を送信しない。

---

## 13. SteamVR / VRChat 経路の実装方針

実装は次の順で進める。

1. SteamVR Probe を `vrserver.exe` でロードできる状態にする。
2. SteamVR Driver Input の component 作成・更新を観測する。
3. Virtual Desktop が作成する左右スティック X/Y component を特定する。
4. 生値を QuestStickScope.exe へ転送する。
5. 何も変更しないパススルー状態で長時間動作確認する。
6. テスト用の明確な単純変換を適用し、VRChat の挙動が変わることを確認する。
7. 単純変換を補正パイプラインへ置き換える。
8. SteamVR再起動、Virtual Desktop再接続、コントローラー再接続を確認する。

VRChat.exe 自体にはフック、DLL注入、メモリ書き換えを行わない。

---

## 14. Virtual Desktop 仮想デスクトップ経路の実装方針

### 14.1 Windows境界の観測

まず Virtual Desktop Streamer を変更せず、以下を調べる。

- Low Level Mouse Hook
- Mouse Wheel
- Raw Input
- XInput
- GameInput
- `SendInput` 等の入力生成API

スティック操作と Windows 側イベントの対応を記録する。

### 14.2 Windows境界での補正

Windows側で元の入力を十分に識別でき、意図入力とドリフトを正しく分離できる場合は、その境界で補正する。

この方法で成立する場合、不要な Streamer 内部フックは導入しない。

### 14.3 Virtual Desktop Probe

Windows側へ出力された後ではアナログ値が失われ、正しい補正ができない場合のみ Virtual Desktop Probe を使用する。

対象バージョンを明示的に検証し、GUIに以下を表示する。

- 対応済みビルド
- 未対応ビルド
- Probe無効
- Probe動作中

認識していない Virtual Desktop Streamer には補正を適用しない。

---

## 15. 設定と保存先

設定は `%LOCALAPPDATA%\QuestStickScope\` 以下へ保存する。

```text
%LOCALAPPDATA%\QuestStickScope\
├─ config.json
├─ calibration.json
├─ recordings\
└─ logs\
```

保存対象:

- 左右の補正有効状態
- Center Offset
- Inner Deadzone
- Directional Outer Table
- 最後に選択した表示レイヤー
- Probe設定
- ログ設定

設定ファイルが破損した場合は安全なデフォルト値で起動し、補正を自動有効化しない。

---

## 16. フェイルセーフ

入力系ツールであるため、補正精度より先に「壊れたとき元入力を妨げない」ことを保証する。

原則:

- 補正設定を取得できない場合はパススルーする。
- GUIと通信できない場合はパススルーへ戻る。
- 未知の SteamVR / Virtual Desktop バージョンへ無理にフックしない。
- NaN / Inf / 範囲外値を出力しない。
- 例外を入力更新処理の外へ伝播させない。
- ログ出力やIPCが遅延しても入力スレッドを待たせない。
- Probe のアンロード中は新しい補正処理を開始しない。

GUIには常に以下を表示する。

```text
SteamVR Probe: Active / Pass-through / Unsupported / Error
VD Probe:      Active / Pass-through / Unsupported / Not required / Error
Correction:    Active / Disabled
```

---

## 17. GUI構成

画面は用途に絞って4つに分ける。

### 17.1 Live

通常利用と原因調査の中心画面。

- Pipeline
- 左右 XY
- 現在値
- 補正前後
- レイヤー差分
- Probe状態
- 補正ON/OFF

### 17.2 Calibration

- Center 計測
- Outer Range 計測
- 結果確認
- 保存
- 左右個別再計測

### 17.3 Recording

- Record開始/停止
- 記録一覧
- Replay
- 区間選択
- 補正比較

### 17.4 Diagnostics

- SteamVR / Virtual Desktop バージョン情報
- Probe状態
- Component一覧
- 更新Hz
- ドロップ数
- IPC状態
- ログ

設定だけの独立した大画面は作らず、必要な設定は関連画面へ配置する。

---

## 18. リポジトリ構成

```text
QuestStickScope/
├─ app/
│  ├─ gui/
│  ├─ views/
│  └─ main.cpp
│
├─ core/
│  ├─ calibration/
│  ├─ correction/
│  ├─ recording/
│  └─ model/
│
├─ probes/
│  ├─ steamvr/
│  └─ virtual_desktop/
│
├─ platform/
│  └─ windows/
│
├─ ipc/
│
├─ tests/
│
├─ docs/
│
├─ CMakeLists.txt
└─ README.md
```

試作中はレイヤーを過度に細分化せず、責務が実際に分かれた時点で分割する。

---

## 19. 開発段階

主要機能が揃うまではバージョン番号を先に固定せず、コミットとタイムスタンプで成果物を識別する。

### Phase 1: 観測基盤

- CMake / Visual Studio ビルド
- QuestStickScope.exe
- ImGui / ImPlot
- QPC timestamp
- Shared Memory IPC
- 基本ログ
- SteamVR Probe のロード
- SteamVR 入力の観測
- Live画面

**終了条件:** Virtual Desktop 経由の左右スティック X/Y を補正せずリアルタイム表示できる。

### Phase 2: SteamVR補正

- component特定の安定化
- パススルー
- Center Offset
- Inner Deadzone
- Directional Outer Normalization
- 補正前後表示
- VRChat確認

**終了条件:** VRChat 内で補正が効き、無効化すると元入力へ戻る。

### Phase 3: Virtual Desktop Desktop経路

- Windows入力観測
- Desktop操作の入力経路特定
- Windows境界での補正可否判定
- 必要なら Virtual Desktop Probe
- SteamVR未起動時の動作

**終了条件:** Virtual Desktop の仮想デスクトップでもドリフト由来の誤操作を抑制できる。

### Phase 4: Calibration

- Center自動計測
- Outer Range自動計測
- 補正パラメータ生成
- Validation
- 永続化

**終了条件:** 手入力で数値調整しなくても実用的な補正値を生成できる。

### Phase 5: Record / Replay と診断

- 記録
- 再生
- Layer Difference
- Statistics
- Probe診断情報
- 長時間ログ

**終了条件:** 原因調査と補正調整を再現可能な記録データで行える。

### Phase 6: 安定化

以下を確認する。

- SteamVR再起動
- Virtual Desktop再接続
- Quest再接続
- Controller sleep / wake
- GUI異常終了
- 設定破損
- Probe不一致
- 長時間連続動作

**終了条件:** 通常使用中に QuestStickScope 自体が入力経路の新しい不安定要因にならない。

---

## 20. テスト方針

### 20.1 Unit Test

SteamVRやQuestなしで検証する。

対象:

- Center Offset
- Deadzone
- Outer Normalization
- 角度補間
- Clamp
- Calibration
- Record / Replay
- 設定読み書き
- リングバッファ

### 20.2 Replay Test

固定した記録データをテスト入力として使用する。

確認:

- 同じ設定で同じ出力になる。
- パラメータ変更時の差分が期待通りになる。
- 境界角度で不連続が出ない。
- デッドゾーン境界で大きなジャンプが出ない。

### 20.3 実機受け入れテスト

#### VRChat

- 無操作で意図しない移動・旋回が発生しない。
- 微小な意図入力を過度に消さない。
- 最大入力が各方向で到達する。
- 補正ON/OFFを比較できる。
- 左右を独立して補正できる。

#### Virtual Desktop

- 無操作で意図しないスクロール等が発生しない。
- 意図した操作は維持される。
- SteamVRなしで動作する。
- DesktopとSteamVRを行き来しても状態が破綻しない。

---

## 21. 性能目標

入力経路へ挟む処理は軽量に保つ。

- Probe 内で動的メモリ確保を常用しない。
- 高頻度ログを同期書き込みしない。
- IPC送信で入力処理をブロックしない。
- 補正処理は1サンプルあたり定数時間とする。
- GUI描画負荷が入力処理へ影響しない構造にする。

追加遅延は実測し、Diagnostics から確認できるようにする。

---

## 22. 互換性管理

SteamVR と Virtual Desktop の内部境界へ依存する部分は、更新で動作しなくなる可能性がある。

そのため以下を記録する。

- QuestStickScope build
- SteamVR build
- Virtual Desktop Streamer build
- 対象モジュールのハッシュ
- Probe の互換性判定結果

既知でない対象には安全側に倒して補正を行わない。

バージョン差を吸収するための複雑な汎用互換レイヤーは先に作らず、実際に必要になった差分だけ対応する。

---

## 23. 完成時の利用フロー

通常利用時は以下で完結することを目標とする。

```text
QuestStickScope 起動
  ↓
SteamVR / Virtual Desktop の状態を自動検出
  ↓
対応する Probe の状態確認
  ↓
保存済みキャリブレーションを読み込み
  ↓
左右スティックの Live 表示
  ↓
補正有効
```

初回またはスティック状態が変化した場合のみ、以下を行う。

```text
Calibration
  ↓
Center計測
  ↓
Outer Range計測
  ↓
Validation
  ↓
保存
```

問題が再発した場合は以下の流れで調査する。

```text
Recording開始
  ↓
問題を再現
  ↓
Replay / Layer Difference
  ↓
異常が最初に現れるレイヤーを特定
  ↓
必要な補正または互換性対応
```

---

## 24. 実装上の判断基準

機能追加時は次の順で判断する。

1. Quest 3 + Virtual Desktop + SteamVR + VRChat で実際に必要か。
2. 原因の可視化または補正精度を直接改善するか。
3. 入力遅延や互換性リスクを増やさないか。
4. Record / Replay で効果を比較できるか。
5. より単純な方法で同じ目的を達成できないか。

「将来使うかもしれない」だけの抽象化や設定項目は追加しない。一方で、原因特定に必要な観測データとフェイルセーフは省略しない。