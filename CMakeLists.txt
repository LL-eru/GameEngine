cmake_minimum_required(VERSION 3.24)

project(GameEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Core.dll : hosts the single process-wide rpmalloc instance + engine arenas.
# -----------------------------------------------------------------------------
add_library(Core SHARED
    Core/CoreInit.cxx
    Core/Debugger.cxx
    Core/EngineArenas.cxx
    Core/EngineAllocator.cxx
    Core/Logger.cxx
    Core/MemoryAPI.cxx
    Core/RpmallocNew.cxx
    Core/ThreadPool.cxx
    Core/CoroutineFrameAllocator.cxx
    Core/WorkerRenderContext.cxx
    third_party/rpmalloc/rpmalloc.c
)

# rpmalloc.c is C and carries the C/C++ override + auto-init machinery.
set_source_files_properties(third_party/rpmalloc/rpmalloc.c PROPERTIES
    COMPILE_DEFINITIONS "ENABLE_OVERRIDE=1;ENABLE_PRELOAD=1;BUILD_DYNAMIC_LINK=1;ENABLE_STATISTICS=1;_CRT_SECURE_NO_WARNINGS"
)

target_include_directories(Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Interface
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rpmalloc
)

target_compile_definitions(Core
    PRIVATE
        GE_BUILD_CORE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_compile_definitions(Core
    PUBLIC
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)

# Export the rpmalloc C entry points so other modules share this one heap.
# /FORCE:MULTIPLE lets rpmalloc's malloc/free (and the _*_base CRT hooks) win
# over the ucrt definitions so the whole module routes to the single heap.
if (MSVC)
    set_target_properties(Core PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/Core/Rpmalloc.def\" /FORCE:MULTIPLE /IGNORE:4006 /IGNORE:4088"
    )
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
enable_testing()

add_executable(MemoryTest Tests/MemoryTest.cxx Core/EngineArenas.cxx)
target_link_libraries(MemoryTest PRIVATE Core)
add_test(NAME MemoryTest COMMAND MemoryTest)

# Cross-module plugin DLL + the Phase 3/4 API test executable.
add_library(CrossModulePlugin SHARED Tests/CrossModulePlugin.cxx)
target_link_libraries(CrossModulePlugin PRIVATE Core)

add_executable(EngineApiTest Tests/EngineApiTest.cxx)
target_link_libraries(EngineApiTest PRIVATE Core CrossModulePlugin)
add_test(NAME EngineApiTest COMMAND EngineApiTest)

add_executable(ThreadPoolTest Tests/ThreadPoolTest.cxx Core/ThreadPool.cxx Core/WorkerRenderContext.cxx)
target_include_directories(ThreadPoolTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Interface
)
target_compile_definitions(ThreadPoolTest
    PRIVATE
        GE_BUILD_CORE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)
add_test(NAME ThreadPoolTest COMMAND ThreadPoolTest)

add_executable(TaskTest
    Tests/TaskTest.cxx
    Core/ThreadPool.cxx
    Core/CoroutineFrameAllocator.cxx
    Core/WorkerRenderContext.cxx
)
target_include_directories(TaskTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Interface
)
target_compile_definitions(TaskTest
    PRIVATE
        GE_BUILD_CORE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)
add_test(NAME TaskTest COMMAND TaskTest)

add_executable(SenderTest
    Tests/SenderTest.cxx
    Core/ThreadPool.cxx
    Core/CoroutineFrameAllocator.cxx
    Core/WorkerRenderContext.cxx
)
target_include_directories(SenderTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Interface
)
target_compile_definitions(SenderTest
    PRIVATE
        GE_BUILD_CORE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)
add_test(NAME SenderTest COMMAND SenderTest)

add_executable(ContextTest
    Tests/ContextTest.cxx
    Core/ThreadPool.cxx
    Core/CoroutineFrameAllocator.cxx
    Core/WorkerRenderContext.cxx
)
target_include_directories(ContextTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Interface
)
target_compile_definitions(ContextTest
    PRIVATE
        GE_BUILD_CORE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)
add_test(NAME ContextTest COMMAND ContextTest)

# Also link ThreadPool into Core.dll for engine use.
# ThreadPoolTest compiles ThreadPool.cxx directly to avoid DLL boundary issues in tests.

# Optional Thread Sanitizer (Clang/GCC). Example:
#   cmake -B build/tsan -DENABLE_TSAN=ON .
#   cmake --build build/tsan --target ThreadPoolTest
#   ctest --test-dir build/tsan -R ThreadPoolTest
option(ENABLE_TSAN "Enable Thread Sanitizer for tests" OFF)
if (ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
