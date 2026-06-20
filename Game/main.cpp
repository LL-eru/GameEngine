//	ファイル名	：main.cpp
//	  概  要		：Game エントリーポイント
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

#include "CoreInit.hxx"
#include "../Interface/HostServices.hxx"

#include <windows.h>
#include <cstdio>

using SetHostServicesFn = void (*)(const HostServices*);

int main()
{
    CoreInitGame();
    Logger::AddSink(std::make_shared<ConsoleSink>());

    LOG_INFO("Game", "Game.exe 起動（DLL ログ/デバッグは HostServices 経由で無効）。");

    HMODULE vulkanDll = LoadLibraryA("Vulkan.dll");
    if (vulkanDll) {
        auto setHostServices = reinterpret_cast<SetHostServicesFn>(
            GetProcAddress(vulkanDll, "SetHostServices"));
        if (setHostServices) {
            setHostServices(CoreGetHostServices());
        }
        LOG_INFO("Game", "Vulkan.dll を alloc-only HostServices でロードしました。");
        FreeLibrary(vulkanDll);
    } else {
        LOG_WARN("Game", "Vulkan.dll のロードに失敗しました（テスト用に続行）。");
    }

    CoreShutdown();
    return 0;
}
