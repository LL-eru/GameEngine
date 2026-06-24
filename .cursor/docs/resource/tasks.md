# モデル読み込み タスクリスト

FBX 等の DCC 形式を **Editor / オフラインでインポート**し、**ランタイムはエンジン形式のみ**読み込む二段構成を前提とする。

- インポート: Assimp（Editor 専用）
- ランタイム: エンジン独自バイナリ（将来 glTF 直読も検討）

---

## 進捗サマリー

| Phase | 内容 | 状態 |
|-------|------|------|
| 0 | 設計・方針確定 | ? 未着手 |
| 1 | 型定義（Interface） | ? 未着手 |
| 2 | Assimp 組み込み（Editor） | ? 未着手 |
| 3 | インポート処理 | ? 未着手 |
| 4 | エンジン形式シリアライズ | ? 未着手 |
| 5 | ランタイムローダー | ? 未着手 |
| 6 | レンダラー連携（Vulkan） | ? 未着手 |
| 7 | 検証・ドキュメント | ? 未着手 |

状態: ? 未着手 / ? 作業中 / ? 完了

---

## Phase 0: 設計・方針確定

- [ ] **0-1** 採用ライブラリを確定する（Assimp を Editor 専用で使用）
- [ ] **0-2** サポートする入力形式の初期スコープを決める（FBX, OBJ, glTF 等）
- [ ] **0-3** 座標系・単位の規約を文書化する
  - [ ] エンジン内部: 左手/右手、Y-up/Z-up、メートル単位など
  - [ ] Assimp インポート時の変換ルール（スケール正規化、法線再計算の要否）
- [ ] **0-4** 頂点レイアウトを決める
  - [ ] Position, Normal, Tangent, UV0, Color, BoneWeights/Indices の有無
  - [ ] インターリーブ vs 分離（初期はインターリーブ推奨）
- [ ] **0-5** エンジンアセット形式のファイル命名・配置を決める
  - [ ] 例: `Assets/Meshes/character.mesh`
  - [ ] ソース FBX との対応関係（同ディレクトリ / `Source/` サブフォルダ等）
- [ ] **0-6** モジュール依存方向を確認する
  - [ ] Assimp → Editor のみ（Core / Interface / Game / Vulkan に載せない）
  - [ ] `decisions.md` に記録する

---

## Phase 1: 型定義（Interface / Core）

API 非依存のデータ型を `Interface` に置く（Vulkan/DX 型は含めない）。

- [ ] **1-1** `Interface/MeshTypes.hxx` を新規作成
  - [ ] `Vec3`, `Vec2`, `Vec4`（または既存型の再利用方針を決める）
  - [ ] `VertexAttribute` 列挙（Position, Normal, UV, …）
  - [ ] `VertexLayout`（属性リスト + stride）
- [ ] **1-2** `MeshData` 構造体を定義
  - [ ] 頂点バッファ（`std::vector<uint8_t>` または typed view）
  - [ ] インデックス（`uint16_t` / `uint32_t` 切り替え条件）
  - [ ] サブメッシュ（materialIndex, indexOffset, indexCount）
  - [ ] AABB（バウンディングボックス）
- [ ] **1-3** `MaterialDesc` 構造体を定義（初期は最小）
  - [ ] 名前、diffuse テクスチャパス、baseColor
  - [ ] PBR 拡張は後続タスクとして TODO 化
- [ ] **1-4** `MeshAsset` 構造体を定義
  - [ ] `MeshData` + `std::vector<MaterialDesc>`
  - [ ] ソースファイルパス（デバッグ / 再インポート用、Game では省略可）
- [ ] **1-5** （任意・後回し可）スキニング用型のスタブ
  - [ ] `Bone`, `Skeleton`, `SkinWeights` のプレースホルダ定義のみ

---

## Phase 2: Assimp 組み込み（Editor）

- [ ] **2-1** Assimp の取得方法を決める
  - [ ] vcpkg / ソースビルド / プリビルド DLL のいずれか
- [ ] **2-2** `Editor.vcxproj` に Assimp の include / lib を追加
- [ ] **2-3** Assimp が Editor.exe にのみリンクされることを確認（Game.exe に漏れない）
- [ ] **2-4** 最小スモークテスト: Assimp で FBX を `aiImportFile` し、メッシュ数をログ出力
- [ ] **2-5** Assimp 後処理フラグの初期セットを決める
  - [ ] `Triangulate`, `GenNormals`, `FlipUVs`, `CalcTangentSpace` 等

---

## Phase 3: インポート処理（Editor）

Assimp → `MeshAsset` への変換。

- [ ] **3-1** `Editor/AssetImporter.hxx/.cxx`（または `Resource/` モジュール）を新規作成
- [ ] **3-2** `ImportMesh(const char* path) -> MeshAsset` API を実装
- [ ] **3-3** Assimp シーン走査
  - [ ] 全メッシュを統合 vs サブメッシュ保持（初期: サブメッシュ保持）
  - [ ] 頂点重複排除（`aiProcess_JoinIdenticalVertices` の利用検討）
- [ ] **3-4** 頂点属性の抽出
  - [ ] Position, Normal, UV を Assimp から `MeshData` へコピー
  - [ ] 欠損属性のデフォルト値（Normal なし → GenNormals 前提 等）
- [ ] **3-5** インデックスバッファ構築
  - [ ] 65535 超えで `uint32_t` に昇格
- [ ] **3-6** マテリアル情報の抽出
  - [ ] 名前、diffuse カラー、diffuse テクスチャパス
  - [ ] テクスチャパスをプロジェクト相対パスに正規化
- [ ] **3-7** 座標系・スケール変換を適用
  - [ ] Phase 0 で決めた規約に従う変換行列
- [ ] **3-8** AABB 計算
- [ ] **3-9** エラーハンドリング
  - [ ] ファイル不存在、Assimp エラー文字列、空メッシュの扱い
  - [ ] `LOG_ERROR` / `LOG_WARN` で報告

---

## Phase 4: エンジン形式シリアライズ

ランタイムが読む `.mesh`（名称は Phase 0 で確定）形式。

- [ ] **4-1** ファイルフォーマット仕様を `overview.md` または `decisions.md` に記述
  - [ ] マジックナンバー、バージョン、チャンク構造
- [ ] **4-2** `MeshAssetWriter` を実装（Editor 側）
  - [ ] `MeshAsset` → バイナリファイル
- [ ] **4-3** `MeshAssetReader` を実装（Core または Interface、Assimp 非依存）
  - [ ] バイナリファイル → `MeshAsset`
- [ ] **4-4** バージョン互換方針を決める（v1 のみで開始可）
- [ ] **4-5** Editor コマンド / 初期 UI
  - [ ] 指定 FBX をインポートして `.mesh` を出力する CLI または Editor メニュー
  - [ ] 出力先: `Assets/Meshes/` 等

---

## Phase 5: ランタイムローダー

Game.exe / Editor 共通で Assimp なしに読み込めるようにする。

- [ ] **5-1** `LoadMeshAsset(const char* path) -> MeshAsset` を Core または専用 Resource モジュールに配置
- [ ] **5-2** ファイル I/O は `HostServices` 経由か std::ifstream か方針を決める
- [ ] **5-3** メモリ確保方針
  - [ ] `ObjectPool` / `SegregatedFreeList` のどちらで `MeshAsset` を保持するか
  - [ ] ロード失敗時のリソース解放
- [ ] **5-4** アセットキャッシュ（同一パス再ロード防止）? 初期は省略可、TODO として記載
- [ ] **5-5** Game.exe で Assimp シンボルがリンクされていないことをビルドで確認

---

## Phase 6: レンダラー連携（Vulkan）

`MeshAsset` を GPU バッファに載せ、描画する。

- [ ] **6-1** `Interface` に GPU 非依存のメッシュハンドル型を検討
  - [ ] 例: `Render::MeshHandle`（uint64_t）+ レンダラー側テーブル
  - [ ] または `IRenderer::CreateMesh(MeshAsset const&)` の追加
- [ ] **6-2** `VulkanRenderer` のハードコード頂点（`s_vertices`）を `MeshAsset` 駆動に置き換え
- [ ] **6-3** 頂点バッファ / インデックスバッファ作成
  - [ ] 既存 `UploadBufferWithStaging` を再利用
- [ ] **6-4** `VertexInput` / パイプラインの頂点属性を `VertexLayout` に合わせて更新
- [ ] **6-5** シェーダー更新
  - [ ] Normal / UV を使う基本ライティング or テクスチャサンプリング（最小）
  - [ ] `ShaderTool` でコンパイル → `Assets/Shader/` に配置
- [ ] **6-6** 描画 API
  - [ ] `IRenderer::DrawMesh(MeshHandle, transform)` または相当メソッド
  - [ ] サブメッシュ単位の draw call
- [ ] **6-7** Editor 起動時にテスト用 `.mesh` をロードして描画確認
- [ ] **6-8** 深度バッファ + 3D カメラ（現在は 2D 矩形描画）? メッシュ表示に必要なら別タスク化

---

## Phase 7: 検証・ドキュメント

- [ ] **7-1** テスト用アセットを用意
  - [ ] 単一メッシュ OBJ（最小）
  - [ ] マルチマテリアル FBX
  - [ ] UV + Normal 付き glTF（Assimp 経由）
- [ ] **7-2** インポート → 保存 → ロード → 描画の E2E 確認手順を記述
- [ ] **7-3** 既知の制限事項を `decisions.md` に記録
  - [ ] アニメーション・スキニングは未対応（将来 Phase）
  - [ ] FBX 非公式パーサに起因する互換性リスク
- [ ] **7-4** README または `docs/` に Resource セクションへのリンクを追加（必要なら）

---

## 将来タスク（スコープ外・メモ）

以下は初回実装完了後に着手。

- [ ] スキニング / スケルタルアニメーション（Assimp ボーン → Animation モジュール）
- [ ] glTF ランタイム直読（tinygltf / cgltf、Assimp バイパス）
- [ ] テクスチャローダー（stb_image 等）+ PBR マテリアル
- [ ] アセットホットリロード（Editor）
- [ ] メッシュ LOD / インスタンシング
- [ ] オフライン一括コンバータ（CI / ビルド前処理）
- [ ] DirectX11 / DirectX12 バックエンドへの Mesh 対応

---

## 推奨実装順序

```
0（設計）→ 1（型）→ 2（Assimp 組込）→ 3（インポート）→ 4（シリアライズ）
    → 5（ランタイムロード）→ 6（Vulkan 描画）→ 7（検証）
```

Phase 4 と 5 は並行可能だが、**4 の Writer が先**（テスト用 `.mesh` を作るため）。

---

## 最初のマイルストーン（MVP）

以下が動けば MVP 完了:

1. Editor で FBX/OBJ をインポートし `.mesh` を出力できる
2. Editor が `.mesh` をロードし Vulkan で 3D メッシュを表示できる
3. Game.exe に Assimp 依存がない
