#pragma once

// Stable memory surface for plugin / host modules. No STL types, no Core-internal
// allocator classes. For general heap use Engine::Allocate/Free; for arena and
// pool routing use HostServices (AllocHeap, AllocFrame, CreatePool, Åc).

#include "../../Interface/MemoryAPI.hxx"
#include "../../Interface/HostServices.hxx"
