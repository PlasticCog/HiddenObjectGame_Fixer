cmake_minimum_required(VERSION 3.20)
project(HOGFixer VERSION 0.1.0 LANGUAGES C CXX)

# The games are all 32-bit, and the launcher injects into them directly, so everything
# is built for x86. Configure from an "x86 Native Tools" / vcvarsamd64_x86 environment.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "HOGFixer must be built as 32-bit x86 (the games are 32-bit).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the target PC needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
                        _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W3 /utf-8 /MP)

# --- third party -------------------------------------------------------------------------
add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

# --- our code ------------------------------------------------------------------------------
add_subdirectory(src/common)
add_subdirectory(src/hook)
add_subdirectory(src/loader)

option(HOGFIX_BUILD_LAUNCHER "Build the SDL3 + Dear ImGui launcher" ON)
if(HOGFIX_BUILD_LAUNCHER)
    add_subdirectory(src/launcher)
endif()
