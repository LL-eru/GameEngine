#include "Public/WorkerRenderContext.hxx"

namespace Engine {
namespace {

thread_local WorkerRenderContext* tls_worker_render_context = nullptr;

} // namespace

WorkerRenderContext::WorkerRenderContext(std::size_t worker_index) noexcept
    : worker_index_(worker_index)
    , command_pool_id_(0xC0FFEE00000000ull | static_cast<std::uint64_t>(worker_index)) {}

void WorkerRenderContext::BeginFrame(std::uint32_t frame_index) noexcept {
    frame_index_ = frame_index;
    recorded_draw_calls_ = 0;
}

void WorkerRenderContext::RecordDrawCall() noexcept {
    ++recorded_draw_calls_;
}

WorkerRenderContext* WorkerRenderContext::Current() noexcept {
    return tls_worker_render_context;
}

void BindWorkerRenderContext(WorkerRenderContext* context) noexcept {
    tls_worker_render_context = context;
}

} // namespace Engine
