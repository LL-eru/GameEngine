#include "Public/CoroutineFrameAllocator.hxx"

#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <new>

namespace Engine::detail {
namespace {

alignas(64) std::byte g_coroutineArenaStorage[4 * 1024 * 1024]{};
std::pmr::monotonic_buffer_resource g_coroutineArena{
    g_coroutineArenaStorage, sizeof(g_coroutineArenaStorage), std::pmr::null_memory_resource()};
std::pmr::unsynchronized_pool_resource g_coroutinePool{&g_coroutineArena};

std::atomic<std::size_t> g_liveAllocations{0};
std::atomic<std::size_t> g_totalBytes{0};
std::atomic<std::size_t> g_peakLiveAllocations{0};
std::atomic<std::size_t> g_peakTotalBytes{0};

void UpdatePeak(std::size_t live, std::size_t bytes) {
    std::size_t peak_live = g_peakLiveAllocations.load(std::memory_order_relaxed);
    while (live > peak_live
        && !g_peakLiveAllocations.compare_exchange_weak(
            peak_live, live, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }

    std::size_t peak_bytes = g_peakTotalBytes.load(std::memory_order_relaxed);
    while (bytes > peak_bytes
        && !g_peakTotalBytes.compare_exchange_weak(
            peak_bytes, bytes, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

} // namespace

void* CoroutineFrameAllocator::Allocate(std::size_t size) {
    void* ptr = g_coroutinePool.allocate(size, alignof(std::max_align_t));
    const std::size_t live = g_liveAllocations.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::size_t bytes = g_totalBytes.fetch_add(size, std::memory_order_relaxed) + size;
    UpdatePeak(live, bytes);
    return ptr;
}

void CoroutineFrameAllocator::Deallocate(void* ptr, std::size_t size) noexcept {
    if (!ptr) {
        return;
    }
    g_coroutinePool.deallocate(ptr, size, alignof(std::max_align_t));
    g_liveAllocations.fetch_sub(1, std::memory_order_relaxed);
    g_totalBytes.fetch_sub(size, std::memory_order_relaxed);
}

std::size_t CoroutineFrameAllocator::LiveAllocations() noexcept {
    return g_liveAllocations.load(std::memory_order_relaxed);
}

std::size_t CoroutineFrameAllocator::TotalBytesAllocated() noexcept {
    return g_totalBytes.load(std::memory_order_relaxed);
}

void CoroutineFrameAllocator::ResetPeakStats() noexcept {
    const std::size_t live = g_liveAllocations.load(std::memory_order_relaxed);
    const std::size_t bytes = g_totalBytes.load(std::memory_order_relaxed);
    g_peakLiveAllocations.store(live, std::memory_order_relaxed);
    g_peakTotalBytes.store(bytes, std::memory_order_relaxed);
}

} // namespace Engine::detail
