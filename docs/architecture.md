# アーキテクチャ概要

## 設計思想

このエンジンは「**ホストとプラグインの分離**」を根本原則として設計されている。  
レンダラー（Vulkan, DirectX12 など）を DLL プラグインとして扱い、ホスト実行ファイル（Editor/Game）とは実行時にのみ結合する。これにより：

- **レンダラーバックエンドの交換**がホストの再コンパイルなしに可能
- **インターフェース定義**を変えない限り、各バックエンドは独立して開発できる
- **Editor と Game で異なるサービスセット**を提供し、開発時と出荷時の挙動を明確に分けられる

---

## モジュール構成

```
Editor.exe / Game.exe   ← ホスト実行ファイル
      │
      ├── Core.dll      ← エンジン基盤サービス（Logger, Allocator, Debugger）
      │
      ├── Interface/    ← レンダラー抽象インターフェース（ヘッダーのみ）
      │
      └── Vulkan.dll    ← レンダラープラグイン（IRenderer 実装）
```

各モジュールの責務は下表の通り：

| モジュール | 種別 | 責務 |
|-----------|------|------|
| `Core` | DLL | Logger / Allocator / Debugger。エンジン基盤の実装を一手に担う |
| `Interface` | ヘッダー群 | `IRenderer`, `ICommandBuffer`, `HostServices` 等の契約定義 |
| `Vulkan` | DLL プラグイン | Vulkan 1.3 ベースのレンダラー実装。Core とは直接リンクしない |
| `DirectX11/12` | DLL プラグイン（スタブ） | 将来のレンダラー拡張用枠組み |
| `Audio` | DLL プラグイン（スタブ） | 将来のオーディオ拡張用枠組み |
| `Editor` | EXE | GLFW ウィンドウホスト。開発・デバッグ用途 |
| `Game` | EXE | ゲーム本体ホスト。出荷用途 |
| `Qt` | DLL | Qt ベースの UI（Editor とは別系統。現在スタブ） |
| `ShaderTool` | EXE | シェーダーコンパイルツール（現在プレースホルダー） |

---

## HostServices ? プラグイン間サービス注入

プラグインは Core.dll に**直接リンクしない**。代わりに `HostServices` という関数ポインタテーブルを受け取る。

```
// Interface/HostServices.hxx（抜粋）
struct HostServices {
    void (*Log)(HostLogLevel, const char* category, const char* message);
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

ホストが起動時に `SetHostServices(CoreGetHostServices())` を呼ぶことで、プラグイン側に Core のサービスが渡される。

### なぜ直接リンクしないのか

DLL 境界をまたいで Core.dll をリンクすると、ビルド構成や MSVC ランタイムのバージョン違いで壊れやすい。  
関数ポインタテーブルを介すことで ABI の安定性を確保し、プラグイン DLL を独立してビルド・配布できる。

### Editor と Game でサービスセットが異なる

| ホスト | サービスセット |
|--------|--------------|
| `CoreInitEditor()` | Log + DebugOutput + Assert + メモリ API 一式（全機能） |
| `CoreInitGame()` | メモリ API のみ（Log/Debug/Assert は null） |

メモリ API とは `AllocHeap` / `FreeHeap` / `AllocFrame` / `AllocGpu` / `ResetFrameArenas` / `CreatePool` / `DestroyPool` / `AllocPool` / `FreePool` を指す。詳細は [memory.md](memory.md)。

Game ビルドではログや assert のオーバーヘッドをゼロにできる。これは出荷時のパフォーマンス要件に応える設計上の選択である。

---

## プラグインのロードフロー

```
Editor/main.cpp:

1. CoreInitEditor()                          // Core 初期化
2. HMODULE dll = LoadLibrary("Vulkan.dll")   // プラグイン DLL ロード
3. SetHostServices(CoreGetHostServices())    // サービス注入
4. CreateRenderer() → IRenderer*            // レンダラー生成
5. renderer->Initialize(windowHandle, ...)  // 初期化
6. // メインループ
7. renderer->BeginFrame()
   commandBuffer->Clear / Draw
   renderer->Submit(commandBuffer)
   renderer->EndFrame()
8. DestroyRenderer(renderer)
9. FreeLibrary(dll)
10. CoreShutdown()
```

---

## PluginHostContext ? プラグイン側の HostServices 管理

プラグイン DLL 内には `Interface/PluginHostContext.cxx` がコンパイルされる（Interface.dll 自体には含まれない）。  
これにより各プラグインが独立して `g_pluginHostServices` を保持し、`GetHostServices()` を通じてアクセスできる。

```cpp
// プラグイン内部での使用例（EngineLog.hxx マクロ経由）
GetHostServices()->Log(HostLogLevel::Info, "Vulkan", "Initialized");
```

---

## ホスト種別を示すマクロ

| マクロ | 定義されるホスト | 用途 |
|--------|----------------|------|
| `GE_BUILD_CORE` | Core.dll ビルド時 | `GE_API` を dllexport に切り替え |
| `GE_HOST_EDITOR` | Editor.exe | デバッグ・ログ機能を有効化 |
| `GE_HOST_GAME` | Game.exe | ログ/デバッグを排除した出荷構成 |
| `GE_PLUGIN` | 各プラグイン DLL | Core に直接リンクせず HostServices を使用 |

---

## 強制インクルード（absuse.hxx）

Editor プロジェクトでは `/FI absuse.hxx` によって全ソースファイルに以下が自動挿入される：

```cpp
// Editor/absuse.hxx
#include <Core/Public/EngineDefines.hxx>
#include <Core/Public/EngineLog.hxx>
#include <Core/Public/EngineDebug.hxx>
#include <Core/Public/EngineMemory.hxx>
```

個々のソースファイルで `#include` を書かずともエンジン基盤の型・マクロが使える。  
これは利便性のための選択であり、依存を暗黙にする代わりに記述量を大幅に削減している。

---

## 将来の拡張性

- `DirectX11`, `DirectX12`, `Audio` は `vcxproj` を持つスタブとして存在しており、新しいバックエンドを追加する際の枠組みが用意されている
- `Plugin.cxx` にはゲーム DLL のホットリロード機構（ファイル変更検知 → temp.dll へコピー → 再ロード）が実装されており、Editor メインループへの統合待ちとなっている
- `Qt` DLL は GLFW ベースの Editor とは別系統の UI フロントエンドとして設計されており、将来的に両者を組み合わせることが想定されている

---

## 参照

- [レンダラープラグインシステム](renderer.md)
- [メモリ管理](memory.md)
- [ビルドシステムとツール](build.md)
