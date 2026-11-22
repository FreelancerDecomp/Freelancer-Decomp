# DPNet: a DirectPlay 8 protocol implementation -- PORT(dpnet), not part of retail. See docs/dpnet.md.
#
# Plain portable C++20 with no Windows headers, so it builds on its own anywhere:
#     cmake -S src/DPNet -B build/dpnet && cmake --build build/dpnet && ctest --test-dir build/dpnet
# The root CMake build includes it on Linux and macOS (the portable modules); on Windows its
# sources are compiled into DALib.dll for x64 (src/DALib/CMakeLists.txt).

cmake_minimum_required(VERSION 3.20)
project(DPNet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DPNET_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)

add_library(DPNet STATIC
    dpnet_wire.cpp
    dpnet_link.cpp
    dpnet_session.cpp
)
target_include_directories(DPNet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(DPNet PRIVATE /W4 /permissive-)
else()
    target_compile_options(DPNet PRIVATE -Wall -Wextra -Wshadow)
endif()

if(DPNET_SANITIZE AND NOT MSVC)
    target_compile_options(DPNet PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(DPNet PUBLIC -fsanitize=address,undefined)
endif()

enable_testing()
add_executable(dpnet_tests tests/dpnet_tests.cpp)
target_link_libraries(dpnet_tests PRIVATE DPNet)
add_test(NAME dpnet_tests COMMAND dpnet_tests)
add_test(NAME dpnet_stress COMMAND dpnet_tests --stress 200)
