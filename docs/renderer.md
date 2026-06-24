# レンダラープラグインシステム

## 設計思想

レンダラーは「**差し替え可能なプラグイン**」として設計されている。  
`IRenderer` / `ICommandBuffer` インターフェースを契約とし、Vulkan・DirectX12 等の実装はそれぞれ独立した DLL として提供される。  
ホスト（Editor/Game）はインターフェースのみに依存し、実装の詳細を一切知らない。

---

## インターフェース定義（Interface/）

### IRenderer

```cpp
// Interface/IRenderer.hxx
namespace Render {
    class IRenderer {
    public:
        virtual ~IRenderer() = default;
        virtual bool Initialize(const WindowHandle& window, uint32_t width, uint32_t height) = 0;
        virtual void Shutdown() = 0;
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual ICommandBuffer* BeginCommandBuffer() = 0;
        virtual void Submit(ICommandBuffer* commandBuffer) = 0;
    };
}
```

### ICommandBuffer

```cpp
// Interface/ICommandBuffer.hxx
namespace Render {
    class ICommandBuffer {
    public:
        virtual ~ICommandBuffer() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void Clear(float r, float g, float b, float a) = 0;
        virtual void Draw(uint32_t vertexCount) = 0;
    };
}
```

### C ABI エクスポート（RendererAPI.hxx）

DLL 境界を安全に越えるため、レンダラーの生成/破棄は C リンケージの関数で公開される：

```cpp
// Interface/RendererAPI.hxx
extern "C" {
    __declspec(dllexport) Render::IRenderer* CreateRenderer();
    __declspec(dllexport) void DestroyRenderer(Render::IRenderer* renderer);
    __declspec(dllexport) void SetHostServices(const HostServices* services);
}
```

C++ の仮想関数テーブル（vtable）はインターフェースポインタ経由で使えるが、  
**オブジェクトの生成と破棄だけは C リンケージに限定**することで ABI の安定性を保つ。

---

## Vulkan バックエンド（Vulkan/）

現時点で唯一の実装は Vulkan 1.3 ベースのバックエンド。

### 主要コンポーネント

| クラス | ファイル | 役割 |
|--------|---------|------|
| `VulkanRenderer` | VulkanRenderer.cxx/hxx | IRenderer 実装。Vulkan 全状態を管理 |
| `VulkanCommandBuffer` | VulkanCommandBuffer.cxx/hxx | ICommandBuffer 実装。描画コマンドを発行 |

### VulkanRenderer が管理するリソース

```
インスタンス / デバイス / キュー
  ├── スワップチェーン（リサイズ時再生成）
  ├── レンダーパス / フレームバッファ
  ├── グラフィクスパイプライン（SPIR-V シェーダーから生成）
  ├── 頂点バッファ / インデックスバッファ（ステージングバッファ経由）
  ├── ユニフォームバッファ（SceneData × フレーム数）
  └── 同期プリミティブ（セマフォ / フェンス）
```

### SceneData ユニフォームバッファ

```cpp
struct SceneData {
    float rectCenter[2];  // アニメーションする矩形の中心座標
};
```

フレームごとに `rectCenter` を更新することで、矩形がアニメーションする。  
各フレームバッファに対応するユニフォームバッファを用意し、CPU/GPU の同期を取りながら更新する。

### フレームループ

```
BeginFrame()
  → vkWaitForFences (前フレーム完了待ち)
  → vkAcquireNextImageKHR (スワップチェーン画像取得)

BeginCommandBuffer() → VulkanCommandBuffer*
  → vkBeginCommandBuffer

commandBuffer->Clear(r, g, b, a)
  → vkCmdBeginRenderPass (クリアカラー指定)
  → vkCmdBindPipeline
  → vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
  → vkCmdBindDescriptorSets (ユニフォームバッファ)

commandBuffer->Draw(vertexCount)
  → vkCmdDrawIndexed × 2 (2 つの矩形、異なるユニフォームデータ)

commandBuffer->End()
  → vkCmdEndRenderPass
  → vkEndCommandBuffer

Submit(commandBuffer)
  → vkQueueSubmit

EndFrame()
  → vkQueuePresentKHR
```

### スワップチェーン再生成

ウィンドウリサイズ時（GLFW の framebuffer resize コールバック）にフラグが立ち、  
次フレームの `BeginFrame()` 先頭でスワップチェーンを再生成する。  
これにより描画ループを止めずにリサイズに対応している。

### デバッグ設定

`_DEBUG` 定義時は Validation Layer (`VK_LAYER_KHRONOS_validation`) を有効化する。  
出荷時（非デバッグ）ではバリデーションを無効化し、オーバーヘッドをゼロにする。

---

## スタブバックエンド

`DirectX11/`, `DirectX12/`, `Audio/` は `vcxproj` を持つが、ソースファイルを持たないスタブ。  
新しいバックエンドを実装する際は：

1. 対応する `vcxproj` にソースを追加する
2. `IRenderer` / `ICommandBuffer` を実装するクラスを作成する
3. `RendererAPI.hxx` の `CreateRenderer` / `DestroyRenderer` / `SetHostServices` を C リンケージで実装する
4. `PluginHostContext.cxx` をプロジェクトに含める（`GE_PLUGIN` マクロを定義）

---

## 型定義（RenderTypes.hxx）

```cpp
enum class GraphicsAPI { Vulkan, DirectX12 };

struct WindowHandle {
    void* glfwWindow;  // GLFWwindow*
};

struct Extent2D {
    uint32_t width;
    uint32_t height;
};
```

`WindowHandle` は `void*` でラップされており、GLFW 依存を Interface に持ち込まない。  
プラグイン側が内部でキャストして使用する。

---

## 参照

- [アーキテクチャ概要](architecture.md)
- [メモリ管理](memory.md)
- [ビルドシステムとツール](build.md)
