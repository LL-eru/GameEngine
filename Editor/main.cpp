//	ファイル名	：main.cpp
//	  概  要		：Editor エントリーポイント
//	作	成	者	：daigo
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

#include <crtdbg.h>
#include "Glfw.hxx"
#include "CoreInit.hxx"

#include <windows.h>
#include "../Interface/RendererAPI.hxx"
#include "../Interface/RenderTypes.hxx"
#include "../Interface/IRenderer.hxx"
#include "../Interface/ICommandBuffer.hxx"
#include "../Interface/HostServices.hxx"

using CreateRendererFn  = Render::IRenderer* (*)(Render::GraphicsAPI);
using DestroyRendererFn = void (*)(Render::IRenderer*);
using SetHostServicesFn = void (*)(const HostServices*);

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    CoreInitEditor();
    Logger::AddSink(std::make_shared<ConsoleSink>());
    Logger::AddSink(std::make_shared<FileSink>("log.txt"));

    GLFW mainWindow(1280, 720, "GameEngine Editor");

    HMODULE vulkanDll = LoadLibraryA("Vulkan.dll");
    if (!vulkanDll) {
        LOG_FATAL("Main", "Vulkan.dll のロードに失敗しました。");
        CoreShutdown();
        return 1;
    }

    auto setHostServices = reinterpret_cast<SetHostServicesFn>(GetProcAddress(vulkanDll, "SetHostServices"));
    if (setHostServices) {
        setHostServices(CoreGetHostServices());
    }

    auto createRenderer  = reinterpret_cast<CreateRendererFn> (GetProcAddress(vulkanDll, "CreateRenderer"));
    auto destroyRenderer = reinterpret_cast<DestroyRendererFn>(GetProcAddress(vulkanDll, "DestroyRenderer"));

    if (!createRenderer || !destroyRenderer) {
        LOG_FATAL("Main", "Vulkan.dll からエクスポート関数を取得できませんでした。");
        FreeLibrary(vulkanDll);
        CoreShutdown();
        return 1;
    }

    Render::IRenderer* renderer = createRenderer(Render::GraphicsAPI::Vulkan);
    if (!renderer) {
        LOG_FATAL("Main", "CreateRenderer が nullptr を返しました。");
        FreeLibrary(vulkanDll);
        CoreShutdown();
        return 1;
    }

    Render::WindowHandle windowHandle;
    windowHandle.handle = mainWindow.GetNativeHandle();

    if (!renderer->Initialize(windowHandle)) {
        LOG_FATAL("Main", "Vulkan 初期化に失敗しました。");
        destroyRenderer(renderer);
        FreeLibrary(vulkanDll);
        CoreShutdown();
        return 1;
    }

    LOG_INFO("Main", "Vulkan レンダラー初期化完了。メインループ開始。");

    while (!mainWindow.ShouldClose())
    {
        glfwPollEvents();

        renderer->BeginFrame();

        Render::ICommandBuffer* cmd = renderer->BeginCommandBuffer();
        if (cmd) {
            cmd->Begin();
            cmd->Clear(0.1f, 0.1f, 0.15f, 1.0f);
            cmd->Draw(6);
            cmd->End();
            renderer->Submit(cmd);
        }

        renderer->EndFrame();
    }

    LOG_INFO("Main", "メインループ終了。シャットダウン中...");

    renderer->Shutdown();
    destroyRenderer(renderer);
    FreeLibrary(vulkanDll);

    CoreShutdown();

    return 0;
}
