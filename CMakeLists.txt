# Advanced Dirt Removal: engine, OFX plugin and tools.
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DADR_WITH_CUDA=ON] [-DADR_WITH_OPENCL=ON]
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.21)
# macOS: universal (Apple silicon + Intel) binaries for macOS 12 and later.
# Must be set before project() so the engine and the plugin agree.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS")
endif()
# Self-contained Windows binaries: no MSVC redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(AdvancedDirtRemoval VERSION 2.0.0 LANGUAGES C CXX)
enable_testing()
add_subdirectory(engine)
add_subdirectory(ofx)
