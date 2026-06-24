cmake_minimum_required(VERSION 3.24)

project(GameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Core SHARED
    Core/CentralMemoryManager.cxx
    Core/CoreInit.cxx
    Core/Debugger.cxx
    Core/EngineAllocator.cxx
    Core/Logger.cxx
    Core/MemoryAPI.cxx
    Core/ThreadLocalAllocator.cxx
)

target_include_directories(Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Interface
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
