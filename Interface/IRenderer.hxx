#pragma once
#include "RenderTypes.hxx"

namespace Render
{
    class ICommandBuffer;

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        virtual bool Initialize(const WindowHandle& window) = 0;
        virtual void Shutdown() = 0;

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        virtual ICommandBuffer* BeginCommandBuffer() = 0;
        virtual void Submit(ICommandBuffer* cmd) = 0;
    };
}