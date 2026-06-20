// 各レンダラープラグイン DLL にコンパイルする共有実装。Core.dll / Interface.dll には含めない。

#include "HostContext.hxx"
#include <cstring>

static HostServices g_pluginHostServices{};

HostServices* GetHostServices() {
    return &g_pluginHostServices;
}

extern "C" __declspec(dllexport) void SetHostServices(const HostServices* services) {
    if (services) {
        g_pluginHostServices = *services;
    } else {
        std::memset(&g_pluginHostServices, 0, sizeof(g_pluginHostServices));
    }
}
