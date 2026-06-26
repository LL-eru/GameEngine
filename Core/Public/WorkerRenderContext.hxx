#pragma once

#include "CoreExport.hxx"

#include <cstddef>
#include <cstdint>

namespace Engine {

// Per-worker render resources (VkCommandPool / scratch buffers stand-in).
// One instance lives on each pool worker thread for the thread's entire lifetime.
class GE_API WorkerRenderContext {
public:
    explicit WorkerRenderContext(std::size_t worker_index) noexcept;

    [[nodiscard]] std::size_t WorkerIndex() const noexcept { return worker_index_; }
    [[nodiscard]] std::uint64_t CommandPoolId() const noexcept { return command_pool_id_; }
    [[nodiscard]] std::uint32_t FrameIndex() const noexcept { return frame_index_; }

    void BeginFrame(std::uint32_t frame_index) noexcept;
    void RecordDrawCall() noexcept;
    [[nodiscard]] int RecordedDrawCalls() const noexcept { return recorded_draw_calls_; }

    [[nodiscard]] static WorkerRenderContext* Current() noexcept;

private:
    friend GE_API void BindWorkerRenderContext(WorkerRenderContext* context) noexcept;

    std::size_t worker_index_{0};
    std::uint64_t command_pool_id_{0};
    std::uint32_t frame_index_{0};
    int recorded_draw_calls_{0};
};

GE_API void BindWorkerRenderContext(WorkerRenderContext* context) noexcept;

} // namespace Engine
