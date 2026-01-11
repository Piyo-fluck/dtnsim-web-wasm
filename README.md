DTNSim Web
=========

3D Delay/Disruption Tolerant Networking (DTN) simulator compiled to WebAssembly, with a custom WebGL front‑end.

ブラウザ上で、ノードグラフ上を移動するエージェントと、その間を伝播していくメッセージ（Carry‑Only / Epidemic ルーティング）を可視化します。

こちらのサイトでシミュレーションできます.
https://piyo-fluck.github.io/dtnsim-web-wasm/

Features
--------

- C++ / Emscripten で実装した DTN シミュレータを WASM としてビルド
- WebGL による 3D 可視化
	- グラフノードとエッジ
	- ノード間を移動するエージェント
	- 通信レンジの 3 軸リング表示
	- 背景のスター・フィールド
- ルーティングアルゴリズムの切り替え（起動時に選択）
	- Carry Only
	- Epidemic
- 統計表示
	- Delivered agents (ever) : 初期メッセージを一度でも受け取ったエージェント数
	- TX / RX / Duplicates
	- Full spread time : 全エージェントに初期メッセージが行き渡るまでのシミュレーション時間

Directory structure
-------------------

- `wasm/`
	- `bindings.cpp` : DTN シミュレータ本体（グラフ生成、エージェント移動、遭遇検出、ルーティング）
	- `dtnsim_api.h` : JS / WASM 間の C ABI
	- `CMakeLists.txt` : Emscripten 用ビルド設定
	- `build/` など : CMake / Emscripten のビルド成果物（gitignore 対象）
- `docs/`
	- `index.html` : UI + WebGL レンダラ + WASM ローダ
	- `dtnsim.js` / `dtnsim.wasm` : Emscripten で生成した WASM モジュール
	- `main.js` など : 補助的な JS（必要に応じて利用）

Simulation model
----------------

### 空間モデル

- エージェントは 3D 空間上の位置 \((x, y, z)\) を持ちます
- グラフノードはランダムに 3D に配置され、k 最近傍（k‑NN）でエッジを張った静的グラフになります
- エージェントはグラフのエッジ上を等速で移動し、ノードに到達すると次の隣接ノードをランダムに選択して歩き続けます

### 通信レンジと遭遇判定

- 通信レンジは球（距離 \(\le \text{COMM\_RANGE}\)）として扱います
- 3D 一様グリッド（セルサイズ ≒ 通信レンジ）で空間を分割し、
	- 自セル + 26 近傍セルのみを探索してペア距離を計算することで、全ペアの二乗オーダーを避けています

### ルーティング

起動時に 2 種類のルーティングアルゴリズムから 1 つを選びます（シミュレーション開始後は変更不可）。

- **Carry Only**
	- メッセージは常に 1 コピーのみ
	- 遭遇した相手がメッセージの宛先だったときだけ転送
	- 宛先に届いたメッセージはそのルート上から削除されます

- **Epidemic**
	- 遭遇した相手がまだ持っていないメッセージは、すべて複製して配布
	- 初期メッセージは宛先に届いても削除せず、ネットワーク全体に伝播し続けます

統計の `delivered` は「少なくとも一度初期メッセージを保持した distinct なエージェント数」として定義されています。

Build (WASM)
-------------

前提:

- Emscripten SDK がインストール済みで、`emcmake` / `em++` が使えること

```bash
cd wasm

# 1. CMake で Emscripten 用のビルドディレクトリを生成
emcmake cmake -B build -S .

# 2. ビルド
cmake --build build --config Release

# 結果として build/ 以下に dtnsim.js / dtnsim.wasm が生成されます
# 必要に応じて docs/ に配置します（本リポジトリでは docs/ にコミット済み）
```

Run (development)
-----------------

シンプルに静的ファイルサーバを立ててブラウザで開きます。

```bash
cd docs

# Python
python -m http.server 8080

# もしくは Node (serve 等)
# npx serve . -l 8080
```

ブラウザで次を開きます:

- http://localhost:8080/

Usage
-----

1. ブラウザでページを開く
2. 左上パネルで以下を設定
	 - Routing: `Carry Only` / `Epidemic`
	 - Nodes: エージェント数（例: 50）
	 - Speed: シミュレーション速度倍率
	 - Show edges / Auto‑rotate / Show stats の各種トグル
3. `Start` を押すと、選択したルーティングで新しいシミュレーションが開始されます
	 - ルーティングを変えたい場合は、一度 `Reset` してから `Start` し直します

Camera controls
---------------

- 左ドラッグ: カメラ回転
- 右ドラッグ or Shift + ドラッグ: 平行移動
- ホイール: ズーム
- キーボード:
	- `F` : Fit view（ネットワーク全体を画面に収める）
	- `R` : ビューのリセット
	- `Space` : 一時停止 / 再開
	- `A` : 自動回転の ON/OFF

Visual cues
-----------

- **ノード (graph nodes)**
	- 小さめの点として表示
	- エッジとともに、静的なトポロジを表現
- **エージェント (agents)**
	- グラフ上を動く大きめの点
	- 初期メッセージを一度でも受信したエージェントは暖色系（オレンジ寄り）で強調
- **通信レンジ (communication range)**
	- 各エージェントの周囲に、XY / XZ / YZ の 3 平面上のリングとして表示
	- 初期メッセージを受信済みのエージェントのみ表示することで、視覚的なノイズを軽減
- **背景 (starfield)**
	- 半径 ≒ 3000 の球面上にランダム配置した星を描画し、奥行き感を演出

Stats panel
-----------

左上パネルの Stats には、WASM 側から取得した統計が表示されます。

- Delivered agents (ever)
	- 初期メッセージを一度でも持ったことのあるエージェントの総数
- TX / RX
	- メッセージ送信 / 受信回数の累計
- Duplicates
	- （将来拡張用）重複メッセージに関する統計
- Full spread time
	- `Delivered agents (ever) == agent_count` になった瞬間のシミュレーション時間 [秒]

右側の「Agents」ログには、各エージェントについて以下がフレームごとに表示されます。

- `#ID  状態  pos=(x, y, z)`
	- 状態: `DELIVERED` / `pending`
	- pos: 現在位置（0 桁小数で丸め）

Notes
-----

- TTL は初期メッセージについては事実上無限とし、宛先到達後も削除せず拡散を継続させています

