# メモリ管理

## 現行方針

メモリ管理は、Core.dll が所有する単一の rpmalloc heap と、用途別の軽量アリーナで構成する。

- **一般 heap（長寿命・可変サイズ）**: `Engine::Allocate` / `Engine::Free`、または `HostServices::AllocHeap` / `FreeHeap` で Core.dll 内の rpmalloc に集約する。
- **フレーム一時データ**: `HostServices::AllocFrame`（内部は `FrameArena`）。個別解放は不要。`ResetFrameArenas()` で巻き戻す。
- **GPU アップロード用一時データ**: `HostServices::AllocGpu`（内部は `GPUArena`）。同様に `ResetFrameArenas()` で巻き戻す。
- **固定サイズの高頻度オブジェクト**: `HostServices::CreatePool` でサイズ・容量を契約し、`PoolHandle` 経由で `AllocPool` / `FreePool` を使う。
- **DLL 境界**では `Engine::Allocate/Free` と `HostServices` のみを公開 API とする。`FrameArena` 等の C++ クラスは Core 内部専用（`GE_API` なし）。

`ArenaId` と `Alloc(size, alignment, uint32_t arenaId)` は廃止済み。用途は関数名と `PoolHandle` で区別する。

---

## 一般 heap: Core.dll の rpmalloc

公開 API は `Interface/MemoryAPI.hxx`。

```cpp
[[nodiscard]] ENGINE_API void* Engine::Allocate(std::size_t size,
                                                std::size_t alignment = alignof(std::max_align_t));
ENGINE_API void Engine::Free(void* ptr) noexcept;
ENGINE_API void Engine::FlushThreadCache() noexcept;
[[nodiscard]] ENGINE_API MemoryStatsView Engine::QueryMemoryStats() noexcept;
```

- zero-byte request は `nullptr` を返す。
- `alignment` は 2 のべき乗必須。違反時は `ENGINE_VM_VERIFY` で trap し `nullptr` を返す。
- モジュール間・スレッド間で allocate/free してよい（単一 rpmalloc インスタンス）。

各 EXE/DLL の C++ `operator new/delete` も共有 heap に載せたい場合は、そのモジュール内の **ちょうど 1 つの翻訳単位**で `Interface/RpmallocOverride.hxx` を include する。

### 第三者ライブラリとの所有権

| 確保元 | 解放方法 |
|--------|----------|
| `Engine::Allocate` / `operator new`（override 済み） | `Engine::Free` / `operator delete` |
| Qt / GLFW 等の raw `malloc` | 同ライブラリの `free`（CRT heap） |

**エンジンが確保したメモリを第三者 lib に渡して `free` させない。** 必要ならコピーするか、lib 内で確保させる。

---

## HostServices（プラグイン向け型付き API）

`Interface/HostServices.hxx` で定義。C-ABI 互換の関数ポインタのみ（STL 非露出）。

```cpp
struct ObjectPool_T;
using PoolHandle = ObjectPool_T*;  // 不透明ハンドル

struct HostServices {
    void (*Log)(HostLogLevel level, const char* category, const char* message);
    void (*DebugOutput)(const char* text);
    bool (*Assert)(const char* expr, bool condition);

    void* (*AllocHeap)(size_t size, size_t alignment);
    void  (*FreeHeap)(void* ptr);

    void* (*AllocFrame)(size_t size, size_t alignment);
    void* (*AllocGpu)(size_t size, size_t alignment);
    void  (*ResetFrameArenas)();

    PoolHandle (*CreatePool)(size_t objectSize, size_t capacity);
    void       (*DestroyPool)(PoolHandle pool);
    void*      (*AllocPool)(PoolHandle pool);
    void       (*FreePool)(PoolHandle pool, void* ptr);
};
```

### 使用例

```cpp
HostServices* host = CoreGetHostServices();

// フレーム一時（Reset で一括回収）
void* scratch = host->AllocFrame(1024, alignof(std::max_align_t));

// ヒープ（任意モジュール / スレッドから Free 可）
void* heapData = host->AllocHeap(4096, 64);
host->FreeHeap(heapData);

// 固定サイズプール（CreatePool 時にサイズ・容量を契約）
PoolHandle particles = host->CreatePool(32, 4096);
void* p = host->AllocPool(particles);
host->FreePool(particles, p);
host->DestroyPool(particles);

host->ResetFrameArenas();
```

### API 対応表

| 関数 | 用途 | 解放 |
|------|------|------|
| `AllocHeap` / `FreeHeap` | 長寿命 rpmalloc heap | `FreeHeap` |
| `AllocFrame` | フレーム scratch | `ResetFrameArenas` |
| `AllocGpu` | GPU ステージング（デフォルト 256 B 整列） | `ResetFrameArenas` |
| `CreatePool` / `DestroyPool` | プール生成・破棄 | `DestroyPool` |
| `AllocPool` / `FreePool` | 固定サイズスロット | `FreePool`（同一 `PoolHandle`） |

---

## 一時 arena とプール（Core 内部）

`FrameArena` / `GPUArena` / `ObjectPool` は `Core/Public/EngineAllocator.hxx` にあり、**プラグインから include しない**（`EngineMemory.hxx` は Interface のみを pull する）。

- **容量**: `AllocatorConfig` で初期化時に指定（既定: Frame 16 MiB、GPU 64 MiB）。
- **プール**: シングルトンは廃止。`CreatePool(objectSize, capacity)` で動的生成。
- **スレッドセーフではない**（Frame / GPU / Pool）。現状はメインスレッド前提。
- **将来**: ジョブシステム導入時は worker ごとに `FrameArena` を 1 つ持つ方針。

---

## 注意点

- Frame / GPU は per-pointer `Free` しない。フレーム末に `ResetFrameArenas()` を呼ぶ。
- `AllocPool` は size 引数を取らない。オブジェクトサイズは `CreatePool` 時に固定される。
- 無効な `PoolHandle` や破棄済みプールへの `AllocPool` / `FreePool` は `nullptr` / no-op（Debug では verify）。
- `Engine::Allocate` と `HostServices::AllocHeap` は同じ rpmalloc heap を指す。どちらで確保しても、対応する Free で解放できる。

---

## テスト

| 実行ファイル | 内容 |
|-------------|------|
| `MemoryTest` | FrameArena / GPUArena / ObjectPool 単体、HostServices 型付き API、rpmalloc stress |
| `EngineApiTest` | モジュール境界越しの `Engine::Allocate/Free`、`operator new/delete` 共有 heap |

---

## 参照

- 設計メモ: `.cursor/memory/allocator_design.md`
- アーキテクチャ: [architecture.md](architecture.md)
