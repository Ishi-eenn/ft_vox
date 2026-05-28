# ft_minecraft (Pimp My World) 実装ステータス

`en.subject.pdf` (Version 2.2) に基づいた実装すべき項目のリストと現状の進捗。

凡例: `[x]` 実装済み / `[~]` 部分実装 / `[ ]` 未実装

---

## IV. General Instructions（一般要件）

- [x] プログラミング言語の選定（C++17 / OpenGL 4.1 使用）
- [x] GPU API: Vulkan / Metal / WebGPU / OpenGL のいずれか（OpenGL 4.1 採用）
- [x] 地形・バイオーム生成ライブラリの不使用（独自 `srcs/world/terrain_gen.cpp`, `noise.cpp`）
- [ ] 最低 25 FPS の安定したレンダリング（要動作検証）
- [ ] クラッシュ無し（要動作検証）
- [ ] 1080p 以上の解像度で動作（要動作検証）

---

## V.1 The World（世界）

- [x] オンデマンドでのワールド生成（`srcs/streaming/chunk_manager.cpp` チャンクストリーミング）
- [x] XZ 平面で 5,000,000 キューブ以上を移動可能（32-bit 座標、無限生成）
- [x] 均一でない地形（山・キャニオン・島など複数バイオーム）
- [x] **最低 5 種類のユニークなバイオーム**（実装数 9: Plains, Desert, Tundra, Rocky, Swamp, Mountain, Canyon, Spring, Autumn）`srcs/world/terrain_gen.cpp:44-56`
- [x] 各バイオームに独自の地形・標高・植生・特徴
- [x] バイオーム間のスムーズな遷移（bilinear interpolation、`terrain_gen.cpp:105-141`）
- [x] 小さな植物・花・キノコの散布（`BlockType::ShortGrass, Flower, Mushroom`、`terrain_gen.cpp:1616-1676`）
- [x] パラメータ可変なプロシージャル生成木（trunk_height, crown_radius, leaf_density、`placeTree`, `placePineTree`, `placeSwampTree`, `placeSpringTree`, `placeAutumnTree`）
- [x] 湖と曲がりくねった川（`computeRiver()` FBM ノイズ、`terrain_gen.cpp:213-339`）
- [x] 地表から見える自然な洞窟入口（`carveSurfaceCaveEntrances()`、`terrain_gen.cpp:1357-1431`）
- [x] ワームホール形状の洞窟（単純ノイズではない / `terrain_gen.cpp:1523-1533` spaghetti caves）
- [x] 鉱石のクラスター（金・ダイヤモンド、楕円体クラスター、`OreClusterSpec`、`terrain_gen.cpp:1244-1306`）
- [x] モンスター（ゾンビ・クリーパー）が湧き、近づくと追跡（`srcs/mob/mob_manager.cpp:91-150`、DETECT_RANGE=16）
- [x] 3D 雲がワールドを漂う（`srcs/renderer/cloud.cpp`、64×64 グリッド、Y=150）
- [x] ブロックの破壊と設置（`srcs/world/world.cpp:282-295`）
- [x] 改変ブロックの永続化（チャンク再ロード後も保持、`world.cpp:62-125` saves/chunk_X_Z.bin）

---

## V.2 Graphic Rendering（グラフィック描画）

- [x] 最低レンダリング距離 260 ブロック（17 chunks × 16 = 272、`include/types.hpp:79`）
- [x] スカイボックスまたはスカイシェーダー（`srcs/renderer/skybox.cpp`、Y ベースのグラデーション）
- [x] Directional Lighting（`renderer.cpp:1308-1312` `uSunDir`, `uSunStrength`、時間帯で太陽方向が回転）
- [x] Shadows（シャドウマップ、`renderer.cpp:123-149` `shadow_fbo_`、`assets/shaders/shadow.*`）
- [x] Screen Space Ambient Occlusion (SSAO)（`gbuffer.*`, `ssao.*`, `ssao_blur.frag`、`renderer.cpp:68-81`）
- [x] 透明な水面（GL_BLEND、Water face indices を別 EBO に分離）
- [x] 遠距離フォグ（`setFogUniforms()`、水中時は青化）

---

## V.3 Camera（カメラ）

- [x] WASD による前後左右移動（カメラ方向相対、`input_handler.cpp:96-116`）
- [x] ジャンプとスプリント（Space ジャンプ / Shift ダッシュ、`player.cpp:113-136`）
- [x] マウスによる Y 軸 360 度視点操作（`camera.cpp:31-35` Yaw は無制限）
- [x] 歩行速度 ≒ 1 cube/sec、スプリント ≒ 2 cube/sec（`PLAYER_SPEED` 系定数）
- [x] 飛行モードのトグル＋飛行時 ×20 速度（`PLAYER_SPEED_FLY = 20.0f`、Space ダブルタップ）

---

## V.4 Sounds（サウンド）

- [ ] **バイオームごとの環境音楽（スムーズな遷移付き）**
- [ ] **プレイヤー・モンスターの効果音（歩行・攻撃・水泳）**
- [ ] **音源からの距離による音量調整**

> オーディオサブシステム自体が未実装。OpenAL/miniaudio 等の依存も無し、`assets/` に音源ファイルも無し。

---

## V.5 Multiplayer and Server（マルチプレイヤー）

- [x] 最低 4 人同時接続可能なサーバー（`MAX_PLAYERS = 8`、`network/packet.hpp:63`）
- [x] 他プレイヤーが歩行・攻撃・破壊・死亡などの動作で見える（`drawRemotePlayer()`、`renderer.cpp:2000-2085`）
- [x] ワールド改変（設置・破壊）の同期＆永続化（`PktBlockChange`、`server.cpp:212` broadcast）
- [x] エンティティ状態（モンスター）の同期（`PktMobUpdate`）

---

## V.6 Interface（インターフェース）

- [x] FPS / triangles / cube / chunk 数のオンスクリーン表示とキー切替（F3、`engine.cpp:235, 567`、`drawStats()`）
- [x] 接続プレイヤー一覧のキー切替表示（`drawPlayerList()`、`renderer.cpp:772`、`show_player_list_` トグル）

---

## V.7 Other（その他）

- [x] ブロック衝突を扱うシンプルな重力システム（`player.cpp:175-210` GRAVITY=28.0f、AABB X/Y/Z 分離衝突）
- [x] 水中での泳ぎ・潜り（任意で減速、`WATER_GRAVITY=6.0f`, `WATER_SWIM_SPEED=4.5f`）
- [x] 水中視覚適応（カラーフィルタ / 視程低下、`setFogUniforms(underwater=true)` で青化）
- [x] 歩行・攻撃のシンプルなアニメーション（リモートプレイヤー手足スイング ±30°、攻撃 -85°、ゾンビ脚 ±18°）

---

## VI. Bonus Part（ボーナス）

- [x] プロシージャル生成の村（`terrain_gen.cpp:18-32, 682-1053+` 96 ブロックグリッド配置、Classic/Compact/Street の 3 レイアウト、平原/ツンドラで素材差し替え、地形ならし対応）
- [ ] クラフトシステム
- [x] リアルな水シミュレーション（`world.cpp:1-19, 200-209, 375-494+` 水源/流動水の区別、深さ 0〜7 の伝播、垂直落下、隣接ブロック改変による再流動）
- [ ] 植物の成長（種→成熟）
- [x] 弓矢システム（`mob/arrow.hpp`, `mob/arrow_manager.cpp` — チャージ式右クリック発射、重力＋空気抵抗、ブロック貫通防止サブステップ、ゾンビ命中ダメージ）
- [ ] Nether ポータル（別次元へのテレポート）
- [ ] クロスプラットフォーム対応（Windows / Mac / Linux 全動作）
- [ ] ステレオサウンド実装
- [ ] オンラインマップインターフェース（Dynmap 風）

---

## VII. Submission（提出）

- [ ] アセットを Git に push（または 42MB 超ならダウンロードスクリプト同梱）

---

## サマリ

| カテゴリ | 実装済 | 部分 | 未実装 | 合計 |
| --- | --- | --- | --- | --- |
| V.1 The World | 15 | 0 | 0 | 15 |
| V.2 Graphic Rendering | 7 | 0 | 0 | 7 |
| V.3 Camera | 5 | 0 | 0 | 5 |
| V.4 Sounds | 0 | 0 | 3 | 3 |
| V.5 Multiplayer | 4 | 0 | 0 | 4 |
| V.6 Interface | 2 | 0 | 0 | 2 |
| V.7 Other | 4 | 0 | 0 | 4 |
| **Mandatory 合計** | **37** | **0** | **3** | **40** |
| VI Bonus | 3 | 0 | 6 | 9 |

**Mandatory 達成率: 37/40 = 92.5%**

### 残タスク（優先度順）

1. **オーディオサブシステム全般**（V.4） — 最大の未実装ブロック
   - オーディオライブラリ導入（OpenAL / miniaudio / SoLoud 等）
   - バイオーム別 BGM とクロスフェード
   - プレイヤー・モンスターの効果音（足音 / 攻撃 / 水泳）
   - 3D 距離減衰
2. **要動作検証**（IV General）
   - 1080p で 25 FPS 維持の確認
   - 長時間プレイでクラッシュ無し確認
3. **ボーナス検討**
