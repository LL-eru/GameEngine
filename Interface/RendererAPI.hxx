#pragma once
#include "IRenderer.hxx"
#include "HostServices.hxx"

extern "C"
{
    __declspec(dllexport)
        Render::IRenderer* CreateRenderer(Render::GraphicsAPI api);
    __declspec(dllexport)
        void DestroyRenderer(Render::IRenderer* renderer);
    __declspec(dllexport)
        void SetHostServices(const HostServices* services);
}