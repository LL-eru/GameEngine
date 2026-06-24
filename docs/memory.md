# メモリ管理

## 設計思想

汎用の `malloc/free` に頼るのではなく、**用途別のメモリアリーナ**を用意することで：

- アロケーションのパターンをコードの意図として明示できる
- フレームごとの一時データはフレーム末尾に一括解放でき、断片化が起きない
- GPU 用とCPU 用で別枠を持つことで、リソースのライフタイムを管理しやすくする

アリーナの種別は `ArenaId` として定義され、`Alloc(size, arenaId)` の形でホスト/プラグイン両側から統一的に使用できる。

---

## ArenaId ? アリーナ識別子

```cpp
// Interface/HostServices.hxx
enum class ArenaId : uint32_t {
    Frame      = 0,  // フレーム単位の一時バッファ
    ObjectPool = 1,  // 固定サイズオブジェクトのプール
    Segregated = 2,  // 汎用（サイズ別フリーリスト）
    GPU        = 3,  // GPU フレームアリーナ
    Count      = 4,
};
```

---

## 各アリーナの実装

### FrameArena（フレームアリーナ）

```
容量: 16 MB（デフォルト）
戦略: バンプアロケータ（オフセットをインクリメントするだけ）
解放: フレーム末尾に CoreFrameArenaReset() で一括リセット
```

**バンプアロケータとは**  
ポインタを前方にズラすだけでアロケーションが完了する最速の手法。  
解放を個別に追跡しない代わりに、フレーム末尾でオフセットを 0 に戻すことで全てを一括解放する。  
毎フレーム使い捨てる一時データ（変換行列、描画コマンドのリスト等）に最適。

### ObjectPool（オブジェクトプール）

```
ブロックサイズ: 64 バイト固定
容量: 1024 オブジェクト
戦略: フリーリスト（解放されたスロットを再利用）
```

同じ型のオブジェクトを大量に生成/破棄するユースケース向け。  
アロケーションサイズが固定なので断片化が起きない。

### SegregatedFreeList（分離フリーリスト）

```
バケット: 8 種（16, 32, 64, 128, 256, 512, 1024, 2048 バイト）
フォールバック: サイズ超過時は malloc
```

汎用アロケータ。サイズに応じたバケットからメモリを取り出す。  
サイズが固定でない一般的なヒープアロケーションに使用する。

### GPUArena（GPU アリーナ）

```
容量: 64 MB（デフォルト）
戦略: FrameArena と同様のバンプアロケータ
用途: GPU リソース（バッファ、テクスチャ等）のフレーム単位一時アロケーション
```

CPU 側の FrameArena と対応する GPU 版。Vulkan のステージングバッファ等に使用することを想定。

---

## C API エクスポート

`HostServices` を通じてプラグインからも使用できる：

```cpp
// Core 側の実装（CoreInit.cxx でバインド）
g_hostServices.Alloc            = CoreAlloc;
g_hostServices.Free             = CoreFree;
g_hostServices.FrameArenaReset  = CoreFrameArenaReset;

// プラグイン側での使用例
void* buf = GetHostServices()->Alloc(1024, (uint32_t)ArenaId::Frame);
GetHostServices()->Free(buf, (uint32_t)ArenaId::Segregated);
```

---

## EngineAllocator クラス

全アリーナのシングルトン管理クラス：

```cpp
class EngineAllocator {
public:
    static void Initialize();
    static void Shutdown();
    static FrameArena&          GetFrameArena();
    static ObjectPool&          GetObjectPool();
    static SegregatedFreeList&  GetSegregatedFreeList();
    static GPUArena&            GetGPUArena();
};
```

`CoreInitEditor()` / `CoreInitGame()` から `EngineAllocator::Initialize()` が呼ばれ、  
`CoreShutdown()` で `EngineAllocator::Shutdown()` が呼ばれる。

---

## 設計上のトレードオフ

| 選択 | 採用した方針 | 理由 |
|------|------------|------|
| アロケータの多様性 | 用途別に 4 種類のアリーナ | 用途が明示的になり、パフォーマンス特性が予測しやすい |
| API の統一 | `Alloc(size, arenaId)` で統一 | プラグインから ArenaId を指定するだけで適切なアロケータに振られる |
| Game では HostServices を alloc-only に | Log/Debug/Assert を null にする | ゲーム出荷時の実行時オーバーヘッドをゼロにする |

---

## 参照

- [アーキテクチャ概要](architecture.md)
- [レンダラープラグインシステム](renderer.md)
- [ビルドシステムとツール](build.md)
