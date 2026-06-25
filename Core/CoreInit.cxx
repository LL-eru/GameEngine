#include "Public/CoreInit.hxx"
#include "Public/Logger.hxx"
#include "Public/Debugger.hxx"
#include "Public/EngineAllocator.hxx"
#include <cstring>

static HostServices g_hostServices{};

static void FillMemoryServices(HostServices& services) {
    services.AllocHeap        = CoreAllocHeap;
    services.FreeHeap         = CoreFreeHeap;
    services.AllocFrame       = CoreAllocFrame;
    services.AllocGpu         = CoreAllocGpu;
    services.ResetFrameArenas = CoreResetFrameArenas;
    services.CreatePool       = CoreCreatePool;
    services.DestroyPool      = CoreDestroyPool;
    services.AllocPool        = CoreAllocPool;
    services.FreePool         = CoreFreePool;
}

static void FillFullHostServices() {
    g_hostServices.Log = CoreLog;
    g_hostServices.DebugOutput = DebugStringOutput;
    g_hostServices.Assert = DebugAssert;
    FillMemoryServices(g_hostServices);
}

static void FillAllocOnlyHostServices() {
    std::memset(&g_hostServices, 0, sizeof(g_hostServices));
    FillMemoryServices(g_hostServices);
}

void CoreInitEditor() {
    Logger::Init();
    EngineAllocator::Initialize();
    FillFullHostServices();
}

void CoreInitGame() {
    Logger::Init();
    EngineAllocator::Initialize();
    FillAllocOnlyHostServices();
}

void CoreShutdown() {
    Logger::Uninit();
    EngineAllocator::Shutdown();
    std::memset(&g_hostServices, 0, sizeof(g_hostServices));
}

HostServices* CoreGetHostServices() {
    return &g_hostServices;
}
