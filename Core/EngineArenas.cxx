#include "Public/EngineAllocator.hxx"
#include "Public/EngineVirtualMemory.hxx"

#include <cstdint>

using Engine::Memory::AlignUp;
using Engine::Memory::IsPowerOfTwo;
using Engine::Memory::PlatformVirtualAllocator;

// Core-internal arena implementations (not exported across the DLL boundary).
// MemoryTest links this translation unit directly for unit tests.

void FrameArena::Initialize(size_t capacityBytes) {
    Shutdown();
    if (capacityBytes == 0) return;

    PlatformVirtualAllocator os;
    const size_t committed = AlignUp(capacityBytes, os.PageSize());
    void* base = os.Reserve(committed);
    ENGINE_VM_VERIFY(base != nullptr, "FrameArena failed to reserve address space");
    if (base == nullptr) return;

    if (!os.Commit(base, committed)) {
        ENGINE_VM_VERIFY(false, "FrameArena failed to commit pages");
        os.Release(base, committed);
        return;
    }

    m_base = static_cast<unsigned char*>(base);
    m_capacity = committed;
    m_offset = 0;
    ENGINE_ASAN_POISON(m_base, m_capacity);
}

void FrameArena::Shutdown() {
    if (m_base != nullptr) {
        PlatformVirtualAllocator os;
        os.Release(m_base, m_capacity);
    }
    m_base = nullptr;
    m_capacity = 0;
    m_offset = 0;
}

void FrameArena::Reset() {
    if (m_base != nullptr) {
        ENGINE_ASAN_POISON(m_base, m_capacity);
    }
    m_offset = 0;
}

void* FrameArena::Allocate(size_t size, size_t alignment) {
    if (size == 0 || m_base == nullptr) return nullptr;
    ENGINE_VM_VERIFY(IsPowerOfTwo(alignment), "FrameArena alignment must be a power of two");
    if (!IsPowerOfTwo(alignment)) return nullptr;

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(m_base);
    const std::uintptr_t cursor = base + m_offset;
    const std::uintptr_t aligned = AlignUp(static_cast<size_t>(cursor), alignment);
    if (aligned < cursor) return nullptr;

    const size_t alignedOffset = static_cast<size_t>(aligned - base);
    if (alignedOffset + size < alignedOffset) return nullptr;
    if (alignedOffset + size > m_capacity) return nullptr;

    m_offset = alignedOffset + size;
    void* result = reinterpret_cast<void*>(aligned);
    ENGINE_ASAN_UNPOISON(result, size);
    return result;
}

void GPUArena::Initialize(size_t capacityBytes) { m_arena.Initialize(capacityBytes); }
void GPUArena::Shutdown() { m_arena.Shutdown(); }
void GPUArena::Reset() { m_arena.Reset(); }
void* GPUArena::Allocate(size_t size, size_t alignment) { return m_arena.Allocate(size, alignment); }

void ObjectPool::Initialize(size_t objectSize, size_t capacity, size_t alignment) {
    Shutdown();
    if (capacity == 0) return;

    if (alignment < alignof(void*)) alignment = alignof(void*);
    ENGINE_VM_VERIFY(IsPowerOfTwo(alignment), "ObjectPool alignment must be a power of two");
    if (!IsPowerOfTwo(alignment)) return;

    size_t stride = objectSize < sizeof(void*) ? sizeof(void*) : objectSize;
    stride = AlignUp(stride, alignment);

    m_objectSize = stride;
    m_alignment = alignment;
    m_capacity = capacity;

    m_storage.assign(stride * capacity + alignment, 0);
    m_base = static_cast<unsigned char*>(AlignUp(m_storage.data(), alignment));
    m_inUse.assign((capacity + 63) / 64, 0);

    m_freeHead = nullptr;
    for (size_t i = capacity; i-- > 0;) {
        void* slot = m_base + i * stride;
        *reinterpret_cast<void**>(slot) = m_freeHead;
        m_freeHead = slot;
    }
    m_freeCount = capacity;
}

void ObjectPool::Shutdown() {
    m_storage.clear();
    m_storage.shrink_to_fit();
    m_inUse.clear();
    m_inUse.shrink_to_fit();
    m_objectSize = 0;
    m_alignment = 0;
    m_capacity = 0;
    m_freeCount = 0;
    m_base = nullptr;
    m_freeHead = nullptr;
}

void* ObjectPool::Allocate() {
    if (m_freeHead == nullptr) return nullptr;
    void* slot = m_freeHead;
    m_freeHead = *reinterpret_cast<void**>(slot);

    const size_t index = IndexOf(slot);
    ENGINE_VM_VERIFY(index != SIZE_MAX, "ObjectPool free list corruption detected");
    SetBit(index);
    --m_freeCount;
    return slot;
}

void ObjectPool::Free(void* ptr) {
    if (!ptr) return;

    const size_t index = IndexOf(ptr);
    ENGINE_VM_VERIFY(index != SIZE_MAX,
                     "ObjectPool::Free received a foreign or misaligned pointer");
    if (index == SIZE_MAX) return;

    ENGINE_VM_VERIFY(TestBit(index), "ObjectPool double free detected");
    if (!TestBit(index)) return;

    ClearBit(index);
    *reinterpret_cast<void**>(ptr) = m_freeHead;
    m_freeHead = ptr;
    ++m_freeCount;
}

bool ObjectPool::Contains(const void* ptr) const {
    if (!ptr || m_base == nullptr) return false;
    const auto* p = static_cast<const unsigned char*>(ptr);
    return p >= m_base && p < m_base + m_objectSize * m_capacity;
}

size_t ObjectPool::IndexOf(const void* ptr) const {
    if (!Contains(ptr) || m_objectSize == 0) return SIZE_MAX;
    const auto offset =
        static_cast<size_t>(static_cast<const unsigned char*>(ptr) - m_base);
    if ((offset % m_objectSize) != 0) return SIZE_MAX;
    return offset / m_objectSize;
}

bool ObjectPool::TestBit(size_t index) const {
    return (m_inUse[index >> 6] >> (index & 63)) & 1ull;
}

void ObjectPool::SetBit(size_t index) {
    m_inUse[index >> 6] |= (1ull << (index & 63));
}

void ObjectPool::ClearBit(size_t index) {
    m_inUse[index >> 6] &= ~(1ull << (index & 63));
}
