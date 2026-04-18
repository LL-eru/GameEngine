#pragma once
#include "IRenderer.hxx"

extern "C"
{
    __declspec(dllexport)
        Render::IRenderer* CreateRenderer(Render::GraphicsAPI api);
    __declspec(dllexport)
        void DestroyRenderer(Render::IRenderer* renderer);
}