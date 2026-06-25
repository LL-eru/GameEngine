# GameEngine

Windows 向け C++ ゲームエンジン。  
レンダラーをプラグイン DLL として差し替え可能な設計を採用している。

---

## 概要

| 項目 | 内容 |
|------|------|
| 言語 / 標準 | C++26（MSVC） |
| プラットフォーム | Windows x64 |
| ビルドシステム | MSBuild（Visual Studio 2022） |
| レンダラー | Vulkan 1.3（実装済）/ DirectX11, DirectX12（スタブ） |
| ウィンドウ | GLFW 3.4 |
| デバッガー | LLDB（`vadimcn.vscode-lldb`） |

---

## 設計の柱

### 1. ホストとプラグインの分離

レンダラー（Vulkan/DX11/DX12）は実行時にロードされる DLL プラグインであり、  
ホスト（`Editor.exe` / `Game.exe`）はインターフェースのみに依存する。  
バックエンドはホストを再コンパイルせずに交換できる。

### 2. HostServices ? 関数ポインタテーブルによるサービス注入

プラグインは `Core.dll` に直接リンクしない。  
代わりにログ・アサート・アロケータをまとめた `HostServices` テーブルを受け取り、  
DLL 境界をまたいだ ABI 安定性を確保する。

### 3. Editor / Game で異なるサービスセット

`Editor.exe` はログ・デバッグ・アサートを含む全サービスを受け取る。  
`Game.exe` はアロケータのみで、ログ/デバッグのオーバーヘッドを排除する。

### 4. 用途別メモリアリーナ

Core.dll の共有 rpmalloc heap、フレーム用バンプアリーナ（`AllocFrame`）、GPU ステージング（`AllocGpu`）、固定サイズプール（`CreatePool` + `PoolHandle`）を、`HostServices` の型付き関数ポインタで提供する。詳細は [docs/memory.md](docs/memory.md)。

---

## モジュール構成

```
Core.dll        エンジン基盤（Logger, Allocator, Debugger）
Interface/      レンダラー抽象（IRenderer, ICommandBuffer, HostServices）
Vulkan.dll      Vulkan 1.3 レンダラー実装
Editor.exe      GLFW ホスト（開発・デバッグ用）
Game.exe        ゲームホスト（出荷用）
Qt.dll          Qt UI フロントエンド（開発中）
ShaderTool.exe  シェーダーコンパイルツール（開発中）
```

---

## クイックスタート

### 前提

- Visual Studio 2022
- Vulkan SDK 1.4（`VULKAN_SDK` 環境変数）
- GLFW 3.4（`C:\GLFW\glfw-3.4` に配置）

### ビルドと実行

VS Code / Cursor のタスクから実行できる（`Ctrl+Shift+B`）：

```
Build: Editor + Vulkan   # Editor と Vulkan DLL をビルド
Run: Editor              # Editor を起動
```

PowerShell から直接実行する場合：

```powershell
.\scripts\msbuild.ps1 -Target Editor;Vulkan -Configuration Debug
.\bin\x64\Debug\Editor.exe
```

---

## ドキュメント

| ページ | 内容 |
|--------|------|
| [アーキテクチャ概要](docs/architecture.md) | 全体設計・HostServices・プラグインロードフロー |
| [レンダラープラグインシステム](docs/renderer.md) | IRenderer/ICommandBuffer・Vulkan 実装詳細 |
| [メモリ管理](docs/memory.md) | 4 種類のアリーナと設計上のトレードオフ |
| [ビルドシステムとツール](docs/build.md) | MSBuild・タスク・clangd・依存関係 |
