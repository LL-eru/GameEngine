#include "Public/CoreInit.hxx"
#include "Public/Logger.hxx"
#include "Public/Debugger.hxx"
#include "Public/EngineAllocator.hxx"
#include <cstring>

static HostServices g_hostServices{};

static void FillFullHostServices() {
    g_hostServices.Log = CoreLog;
    g_hostServices.DebugOutput = DebugStringOutput;
    g_hostServices.Assert = DebugAssert;
    g_hostServices.Alloc = CoreAlloc;
    g_hostServices.Free = CoreFree;
    g_hostServices.FrameArenaReset = CoreFrameArenaReset;
}

static void FillAllocOnlyHostServices() {
    std::memset(&g_hostServices, 0, sizeof(g_hostServices));
    g_hostServices.Alloc = CoreAlloc;
    g_hostServices.Free = CoreFree;
    g_hostServices.FrameArenaReset = CoreFrameArenaReset;
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
