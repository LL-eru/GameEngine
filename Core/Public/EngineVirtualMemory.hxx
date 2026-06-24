#pragma once

// =============================================================================
// EngineVirtualMemory.hxx
//
// Phase 1: OS-direct virtual memory abstraction layer (zero external deps).
//
//   * RawVirtualAllocator        : C++20 concept describing the OS contract.
//   * WindowsVirtualAllocator    : VirtualAlloc / VirtualFree implementation.
//   * PosixVirtualAllocator      : mmap / munmap / mprotect implementation.
//   * PlatformVirtualAllocator   : compile-time selected concrete type.
//   * Alignment helpers          : <bit> based, adapt to the system page size.
//
// Design constraints honoured here:
//   - No virtual dispatch. Everything is resolved at compile time via concepts
//     and templates, so the central manager can be monomorphised per platform.
//   - No dependency outside the standard library and the OS system headers.
// =============================================================================

#include <bit>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <type_traits>

// ---- Diagnostics back-ends ---------------------------------------------------
// std::println (C++23) is preferred for the error channel; std::breakpoint
// (C++26 <debugging>) is preferred for the trap. Both gracefully degrade.
#if defined(__has_include)
#  if __has_include(<print>)
#    include <print>
#    define ENGINE_VM_HAS_PRINT 1
#  endif
#  if __has_include(<debugging>)
#    include <debugging>
#  endif
#endif

#if defined(__cpp_lib_debugging)
#  define ENGINE_VM_DEBUG_BREAK() ::std::breakpoint()
#elif defined(_MSC_VER)
#  define ENGINE_VM_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#  define ENGINE_VM_DEBUG_BREAK() __builtin_trap()
#else
#  include <cstdlib>
#  define ENGINE_VM_DEBUG_BREAK() ::std::abort()
#endif

// ---- AddressSanitizer manual instrumentation --------------------------------
// When built with ASan we manually poison free/cached memory so that any
// use-after-free or out-of-bounds access against our custom allocator is caught
// exactly like a system-heap bug. Outside ASan builds these compile to nothing.
#if defined(__SANITIZE_ADDRESS__)
#  define ENGINE_VM_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define ENGINE_VM_ASAN 1
#  endif
#endif

#if defined(ENGINE_VM_ASAN)
#  include <sanitizer/asan_interface.h>
#  define ENGINE_ASAN_POISON(addr, size)   __asan_poison_memory_region((addr), (size))
#  define ENGINE_ASAN_UNPOISON(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#  define ENGINE_ASAN_POISON(addr, size)   ((void)0)
#  define ENGINE_ASAN_UNPOISON(addr, size) ((void)0)
#endif

// ---- OS headers --------------------------------------------------------------
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace Engine::Memory {

// =============================================================================
// Alignment helpers (powered by <bit>)
// =============================================================================

// True when value is a non-zero power of two.
[[nodiscard]] constexpr bool IsPowerOfTwo(std::size_t value) noexcept {
    return value != 0 && std::has_single_bit(value);
}

// Smallest power of two that is >= value (1 when value == 0).
[[nodiscard]] constexpr std::size_t CeilToPowerOfTwo(std::size_t value) noexcept {
    return value <= 1 ? std::size_t{1} : std::bit_ceil(value);
}

// Round value up to the next multiple of alignment. alignment must be pow2.
[[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    // Mask trick is only valid for power-of-two alignments.
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

[[nodiscard]] inline void* AlignUp(void* ptr, std::size_t alignment) noexcept {
    return reinterpret_cast<void*>(AlignUp(reinterpret_cast<std::uintptr_t>(ptr), alignment));
}

// True when ptr is aligned to alignment (pow2).
[[nodiscard]] inline bool IsAligned(const void* ptr, std::size_t alignment) noexcept {
    return (reinterpret_cast<std::uintptr_t>(ptr) & (alignment - 1)) == 0;
}

namespace Detail {
// Centralised failure reporting used by the assert macro below. Header-only and
// inline so every translation unit shares the same definition.
inline void ReportFailure(const char* message,
                          const char* expr,
                          const char* file,
                          int line) noexcept {
#if defined(ENGINE_VM_HAS_PRINT)
    std::println(stderr,
                 "[EngineVirtualMemory] FATAL: {}\n  expr : {}\n  where: {}:{}",
                 message ? message : "(null)",
                 expr ? expr : "(null)",
                 file ? file : "(null)",
                 line);
#else
    (void)message; (void)expr; (void)file; (void)line;
#endif
    ENGINE_VM_DEBUG_BREAK();
}
} // namespace Detail

// Trap-on-failure verification. Active in every configuration because a broken
// virtual-memory layer is never acceptable; the cost is a single branch.
#define ENGINE_VM_VERIFY(cond, msg)                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::Engine::Memory::Detail::ReportFailure((msg), #cond, __FILE__,    \
                                                    __LINE__);                 \
        }                                                                      \
    } while (0)

// =============================================================================
// RawVirtualAllocator concept
//
//   void* Reserve(size_t size)        : reserve address space only.
//   bool  Commit(void* ptr, size_t)   : back a reserved range with pages.
//   void  Decommit(void* ptr, size_t) : return physical pages to the OS.
//   void  Release(void* ptr, size_t)  : free the address space entirely.
//   size_t PageSize()                 : system page size (adaptive).
// =============================================================================
template <typename T>
concept RawVirtualAllocator = requires(T allocator, void* ptr, std::size_t size) {
    { allocator.Reserve(size) }        -> std::same_as<void*>;
    { allocator.Commit(ptr, size) }    -> std::same_as<bool>;
    { allocator.Decommit(ptr, size) }  -> std::same_as<void>;
    { allocator.Release(ptr, size) }   -> std::same_as<void>;
    { allocator.PageSize() }           -> std::convertible_to<std::size_t>;
    { allocator.AllocationGranularity() } -> std::convertible_to<std::size_t>;
};

#if defined(_WIN32)

// =============================================================================
// WindowsVirtualAllocator  (VirtualAlloc / VirtualFree)
// =============================================================================
class WindowsVirtualAllocator {
public:
    WindowsVirtualAllocator() noexcept {
        SYSTEM_INFO info{};
        ::GetSystemInfo(&info);
        m_pageSize = static_cast<std::size_t>(info.dwPageSize);
        m_granularity = static_cast<std::size_t>(info.dwAllocationGranularity);
        ENGINE_VM_VERIFY(IsPowerOfTwo(m_pageSize), "page size is not a power of two");
        ENGINE_VM_VERIFY(IsPowerOfTwo(m_granularity), "allocation granularity is not a power of two");
    }

    // Reserve address space (no physical backing) rounded up to the page size.
    [[nodiscard]] void* Reserve(std::size_t size) noexcept {
        const std::size_t rounded = AlignUp(size, m_pageSize);
        void* ptr = ::VirtualAlloc(nullptr, rounded, MEM_RESERVE, PAGE_NOACCESS);
        return ptr;
    }

    // Commit physical pages over a previously reserved range.
    [[nodiscard]] bool Commit(void* ptr, std::size_t size) noexcept {
        if (ptr == nullptr || size == 0) return false;
        return ::VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
    }

    // Release physical pages but keep the address space reserved.
    void Decommit(void* ptr, std::size_t size) noexcept {
        if (ptr == nullptr || size == 0) return;
        (void)::VirtualFree(ptr, size, MEM_DECOMMIT);
    }

    // Release the whole reservation. Windows requires size == 0 with MEM_RELEASE.
    void Release(void* ptr, std::size_t /*size*/) noexcept {
        if (ptr == nullptr) return;
        (void)::VirtualFree(ptr, 0, MEM_RELEASE);
    }

    [[nodiscard]] std::size_t PageSize() const noexcept { return m_pageSize; }
    [[nodiscard]] std::size_t AllocationGranularity() const noexcept { return m_granularity; }

private:
    std::size_t m_pageSize = 4096;
    std::size_t m_granularity = 65536;
};

using PlatformVirtualAllocator = WindowsVirtualAllocator;

#else // POSIX (Linux / macOS)

// =============================================================================
// PosixVirtualAllocator  (mmap / munmap / mprotect)
// =============================================================================
class PosixVirtualAllocator {
public:
    PosixVirtualAllocator() noexcept {
        const long page = ::sysconf(_SC_PAGESIZE);
        m_pageSize = page > 0 ? static_cast<std::size_t>(page) : std::size_t{4096};
        // POSIX has no separate "allocation granularity"; the page size serves.
        m_granularity = m_pageSize;
        ENGINE_VM_VERIFY(IsPowerOfTwo(m_pageSize), "page size is not a power of two");
    }

    // Reserve address space with PROT_NONE so touching it faults until committed.
    [[nodiscard]] void* Reserve(std::size_t size) noexcept {
        const std::size_t rounded = AlignUp(size, m_pageSize);
        void* ptr = ::mmap(nullptr, rounded, PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return ptr == MAP_FAILED ? nullptr : ptr;
    }

    // Commit by making the range readable/writable; pages fault in on first use.
    [[nodiscard]] bool Commit(void* ptr, std::size_t size) noexcept {
        if (ptr == nullptr || size == 0) return false;
        return ::mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
    }

    // Decommit: drop the physical pages and re-arm the guard.
    void Decommit(void* ptr, std::size_t size) noexcept {
        if (ptr == nullptr || size == 0) return;
#if defined(MADV_DONTNEED)
        (void)::madvise(ptr, size, MADV_DONTNEED);
#endif
        (void)::mprotect(ptr, size, PROT_NONE);
    }

    // Release the address space. munmap needs the exact size that was reserved.
    void Release(void* ptr, std::size_t size) noexcept {
        if (ptr == nullptr || size == 0) return;
        (void)::munmap(ptr, AlignUp(size, m_pageSize));
    }

    [[nodiscard]] std::size_t PageSize() const noexcept { return m_pageSize; }
    [[nodiscard]] std::size_t AllocationGranularity() const noexcept { return m_granularity; }

private:
    std::size_t m_pageSize = 4096;
    std::size_t m_granularity = 4096;
};

using PlatformVirtualAllocator = PosixVirtualAllocator;

#endif // _WIN32

// Compile-time guarantee that the selected platform type honours the contract.
static_assert(RawVirtualAllocator<PlatformVirtualAllocator>,
              "PlatformVirtualAllocator must satisfy RawVirtualAllocator");

} // namespace Engine::Memory
