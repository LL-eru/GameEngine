#pragma once

#ifdef EDITORCORE_EXPORTS
#define EDITOR_API __declspec(dllexport)
#else
#define EDITOR_API __declspec(dllimport)
#endif

extern "C"
{
    EDITOR_API void StartEditor(int argc, char** argv);
}