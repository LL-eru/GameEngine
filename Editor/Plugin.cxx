#include "Plugin.hxx"

// Plugin.cpp

#include "Plugin.hxx"
#include <iostream>

typedef GameAPI(*GetAPIFunc)();

bool Plugin::Load(const char* path) {
    originalPath = path;

    // 更新時間記録
    lastWriteTime = std::filesystem::last_write_time(originalPath);

    // コピー
    std::filesystem::copy_file(originalPath, tempPath,
        std::filesystem::copy_options::overwrite_existing);

    dll = LoadLibrary(tempPath);
    if (!dll) return false;

    auto getAPI = (GetAPIFunc)GetProcAddress(dll, "GetGameAPI");
    if (!getAPI) return false;

    api = getAPI();
    return true;
}

void Plugin::Unload() {
    if (dll) {
        FreeLibrary(dll);
        dll = nullptr;
    }
}

void Plugin::Update(GameState* state) {
    if (api.Update)
        api.Update(state);
}

bool Plugin::CheckAndReload(GameState* state) {
    auto current = std::filesystem::last_write_time(originalPath);

    if (current != lastWriteTime) {
        lastWriteTime = current;

        std::cout << "Hot Reload!\n";

        if (api.Uninit)
            api.Uninit(state);

        Unload();

        Sleep(100); // ビルド待ち

        if (Load(originalPath)) {
            if (api.Init)
                api.Init(state);
            return true;
        }
    }
    return false;
}