#pragma once

namespace Render
{
    class ICommandBuffer
    {
    public:
        virtual ~ICommandBuffer() = default;

        virtual void Begin() = 0;
        virtual void End() = 0;

        virtual void Clear(float r, float g, float b, float a) = 0;

        virtual void Draw(unsigned long vertexCount) = 0;
    };
}