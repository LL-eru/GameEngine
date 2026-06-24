# ビルドシステムとツール

## 設計思想

Visual Studio ソリューション（MSBuild）をビルドの主体としながら、  
**Cursor/VS Code の Task からコマンド一発でビルドできる**環境を整えている。  
また clangd による IntelliSense と静的解析のために `compile_commands.json` を管理する。

---

## ソリューション構成

`GameEngine.sln` は 10 プロジェクトを持ち、2 フォルダで分類される：

```
GameEngine.sln
├── Engine/
│   ├── Core         (DLL)
│   ├── Editor       (EXE)
│   ├── Game         (EXE)
│   ├── Qt           (DLL)
│   ├── ShaderTool   (EXE)
│   └── Audio        (DLL)
└── Renderer/
    ├── Vulkan       (DLL)
    ├── DirectX11    (DLL)
    ├── DirectX12    (DLL)
    └── Interface    (DLL)
```

全プロジェクトは `x64` プラットフォーム、`Debug` / `Release` 構成をサポートする。

---

## Directory.Build.props ? 共有ビルドプロパティ

全プロジェクトが自動的に読み込む共通設定：

| プロパティ | 値 |
|-----------|-----|
| `OutDir` | `$(SolutionDir)bin\$(Platform)\$(Configuration)\` |
| `IntDir` | `$(ProjectDir)obj\$(Platform)\$(Configuration)\` |
| `LanguageStandard` | `stdcpplatest`（C++26） |
| `GLFWRoot` | `C:\GLFW\glfw-3.4` |
| `VulkanSDK14` | `$(VULKAN_SDK)` 環境変数 |
| `InterfaceDir` | `$(SolutionDir)Interface` |
| `CorePublicDir` | `$(SolutionDir)Core\Public` |

出力先をソリューションルート直下の `bin/` に統一することで、  
Editor.exe から `Vulkan.dll` を相対パスでロードできる。

---

## ビルドスクリプト（scripts/msbuild.ps1）

MSBuild を PowerShell から呼び出すラッパー。`vswhere` で Visual Studio のパスを自動検出する。

```powershell
# 使用例
.\scripts\msbuild.ps1                             # ソリューション全体
.\scripts\msbuild.ps1 -Target Editor;Vulkan       # 指定プロジェクトのみ
.\scripts\msbuild.ps1 -Configuration Release      # Release ビルド
.\scripts\msbuild.ps1 -Target Core -Platform x64
```

---

## VS Code タスク（.vscode/tasks.json）

Ctrl+Shift+B で利用できるタスク一覧：

| タスク名 | 内容 |
|---------|------|
| `Build: All (Debug)` | ソリューション全体のデバッグビルド（デフォルト） |
| `Build: Editor + Vulkan` | Editor と Vulkan のみビルド（最も頻繁に使用） |
| `Build: Game` | Game のみ |
| `Build: Core` | Core のみ |
| `Build: Vulkan` | Vulkan のみ |
| `Build: Qt` | Qt のみ |
| `Build: DirectX11 / DirectX12` | 各 DX バックエンド |
| `Build: Audio` | Audio |
| `Build: Interface` | Interface |
| `Build: ShaderTool` | ShaderTool |
| `Generate: compile_commands.json` | clangd 用 DB 再生成 |
| `Run: Editor` | Editor.exe を実行（Vulkan SDK を PATH に追加） |

---

## デバッグ構成（.vscode/launch.json）

デバッガーは **LLDB**（`vadimcn.vscode-lldb`）を使用する。  
MSVC のデバッガーではなく LLDB を採用しているのは、Cursor IDE との親和性のためである。

| 構成名 | 対象 |
|--------|------|
| `Debug: Editor` | Editor.exe の起動デバッグ |
| `Debug: Game` | Game.exe の起動デバッグ |
| `Debug: ShaderTool` | ShaderTool.exe の起動デバッグ |
| `Attach: Vulkan` | 実行中の Vulkan.dll にアタッチ |
| `Attach: Core / Interface / Qt / DX11 / DX12 / Audio` | 各 DLL へのアタッチ |
| **Compound: Editor + Vulkan Plugin** | Editor 起動 + Vulkan アタッチ を同時実行 |

---

## clangd 設定（.clangd）

clangd による正確な IntelliSense のため、プロジェクト別にコンパイルフラグを設定：

```yaml
# .clangd
If:
  PathMatch: Editor/.*
CompileFlags:
  Add: [-DGE_HOST_EDITOR, -include, Editor/absuse.hxx, -DGLFW_DLL, ...]

If:
  PathMatch: Core/.*
CompileFlags:
  Add: [-DGE_BUILD_CORE, ...]

If:
  PathMatch: Vulkan/.*
CompileFlags:
  Add: [-DGE_PLUGIN, -DGLFW_DLL, ...]

If:
  PathMatch: Game/.*
CompileFlags:
  Add: [-DGE_HOST_GAME, ...]
```

グローバルフラグ：`-std=c++26`, `-m64`, GLFW/Vulkan/Core/Interface のインクルードパス

---

## compile_commands.json の管理

`scripts/generate-compile-commands.ps1` で自動生成する。  
vcxproj の設定からコンパイルコマンドを逆算し、12 エントリ（全コンパイル対象ソース）を生成する。

clangd はこのファイルを参照してインクルードパスや定義マクロを解決する。  
ソースを追加・削除した際は `Generate: compile_commands.json` タスクを再実行する。

---

## エンコーディング

ソースファイルは **Shift-JIS (CP932)** で保存されている。  
`scripts/convert-encoding.ps1` および `scripts/convert-to-shiftjis.ps1` でエンコーディング変換が可能。  
VS Code の設定（`files.encoding: shiftjis`）でこれに対応している。

---

## 必要な外部依存

| 依存 | パス / 環境変数 |
|------|--------------|
| Vulkan SDK 1.4 | `VULKAN_SDK` 環境変数 |
| GLFW 3.4 | `C:\GLFW\glfw-3.4`（固定パス） |
| Visual Studio 2022 | vswhere で自動検出 |
| Qt（オプション） | Qt プロジェクトビルド時のみ |

---

## 参照

- [アーキテクチャ概要](architecture.md)
- [レンダラープラグインシステム](renderer.md)
- [メモリ管理](memory.md)
