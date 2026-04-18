//	ファイル名	：Plugin.hxx
//	  概  要		：
//	作	成	者	：daigo
//	 作成日時	：2026/03/30 17:51:51
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

// =-=-= インクルードガード部 =-=-=
#pragma once

// =-=-= インクルード部 =-=-=
#include <windows.h>
#include <filesystem>
#include "GameAPI.hxx"

class Plugin {
public:
    bool Load(const char* path);
    void Unload();

    void Update(GameState* state);
    bool CheckAndReload(GameState* state);

private:
    HMODULE dll = nullptr;
    GameAPI api{};

    const char* originalPath = nullptr;
    const char* tempPath = "temp.dll";

    std::filesystem::file_time_type lastWriteTime;
};