#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace Engine {

// Lock-free Chase-Lev deque backed by a fixed-size ring buffer.
// Owner pushes/pops from the bottom (LIFO). Thieves steal from the top (FIFO).
// Capacity must be a power of two. Push returns false when full.
template<typename T, std::size_t Capacity>
class WorkStealingDeque {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_move_constructible_v<T>);

public:
    WorkStealingDeque() = default;
    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    [[nodiscard]] bool Push(T item) {
        const std::size_t bottom = bottom_.load(std::memory_order_relaxed);
        const std::size_t top = top_.load(std::memory_order_acquire);
        if (bottom - top >= Capacity) {
            return false;
        }
        buffer_[bottom & kMask].emplace(std::move(item));
        bottom_.store(bottom + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::optional<T> Pop() {
        const std::size_t bottom = bottom_.load(std::memory_order_relaxed);
        const std::size_t top = top_.load(std::memory_order_acquire);
        if (top >= bottom) {
            return std::nullopt;
        }

        const std::size_t new_bottom = bottom - 1;
        bottom_.store(new_bottom, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::size_t top_after = top_.load(std::memory_order_relaxed);
        if (top_after > new_bottom) {
            bottom_.store(bottom, std::memory_order_relaxed);
            return std::nullopt;
        }
        T item = std::move(*buffer_[new_bottom & kMask]);
        buffer_[new_bottom & kMask].reset();
        if (top_after == new_bottom) {
            std::size_t top_snapshot = top_after;
            if (!top_.compare_exchange_strong(
                    top_snapshot, top_snapshot + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                bottom_.store(bottom, std::memory_order_relaxed);
                return std::nullopt;
            }
            bottom_.store(bottom, std::memory_order_relaxed);
        }
        return item;
    }

    [[nodiscard]] std::optional<T> Steal() {
        while (true) {
            const std::size_t top = top_.load(std::memory_order_acquire);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            const std::size_t bottom = bottom_.load(std::memory_order_acquire);
            if (top >= bottom) {
                return std::nullopt;
            }
            std::size_t expected = top;
            if (!top_.compare_exchange_strong(
                    expected, expected + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                continue;
            }
            return std::move(*buffer_[top & kMask]);
        }
    }

    [[nodiscard]] bool EmptyApprox() const noexcept {
        const std::size_t bottom = bottom_.load(std::memory_order_relaxed);
        const std::size_t top = top_.load(std::memory_order_relaxed);
        return top >= bottom;
    }

    [[nodiscard]] std::size_t SizeApprox() const noexcept {
        const std::size_t bottom = bottom_.load(std::memory_order_relaxed);
        const std::size_t top = top_.load(std::memory_order_relaxed);
        return bottom > top ? bottom - top : 0;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> top_{0};
    alignas(64) std::atomic<std::size_t> bottom_{0};
    alignas(64) std::array<std::optional<T>, Capacity> buffer_{};
};

} // namespace Engine
