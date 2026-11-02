cmake_minimum_required(VERSION 3.21)
project(AdvancedDirtRemovalEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(ADR_WITH_METAL "Build the Metal backend" ${APPLE})
option(ADR_WITH_CUDA "Build the CUDA backend" OFF)
option(ADR_WITH_OPENCL "Build the OpenCL backend" OFF)
option(ADR_WITH_CPU "Build the CPU fallback backend" ON)

if(APPLE)
  enable_language(OBJCXX)
endif()

# Stage groups: prepare (always), motion, detect, refine, repair, sequence.
# Files are named <group>.kern / <group>_*.kern and src/stages/<group>*.cpp so a
# partial build can exclude groups that are still under development.
set(ADR_GROUPS "all" CACHE STRING "Stage groups to build (all, or a list such as prepare;motion)")
if(ADR_GROUPS STREQUAL "all")
  set(ADR_GROUP_REGEX ".*")
else()
  set(ADR_GROUP_LIST prepare ${ADR_GROUPS})
  if("repair" IN_LIST ADR_GROUP_LIST)
    list(APPEND ADR_GROUP_LIST detect)  # repair_float_frame uses detect's byte stages
  endif()
  list(REMOVE_DUPLICATES ADR_GROUP_LIST)
  string(REPLACE ";" "|" ADR_GROUP_ALT "${ADR_GROUP_LIST}")
  set(ADR_GROUP_REGEX "/(${ADR_GROUP_ALT})[_.][^/]*$")
endif()

if(MSVC)
  # /GT: fiber-safe thread-local storage (the CPU backend runs kernels on fibers).
  add_compile_options(/fp:precise /utf-8 /GT)
else()
  # Deterministic float32 results: never contract a*b+c into fused multiply-add.
  add_compile_options(-ffp-contract=off)
endif()

file(GLOB ADR_KERNEL_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/*.kern)
list(FILTER ADR_KERNEL_FILES INCLUDE REGEX "${ADR_GROUP_REGEX}")
list(SORT ADR_KERNEL_FILES)
file(GLOB ADR_KERNEL_PARAMS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/params_*.h)
list(SORT ADR_KERNEL_PARAMS)
set(ADR_KERNEL_INPUTS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/dialect.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/params.h
    ${ADR_KERNEL_PARAMS}
    ${ADR_KERNEL_FILES})
set(ADR_KERNEL_CPP ${CMAKE_CURRENT_BINARY_DIR}/kernel_source.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/kernel_inputs.txt "${ADR_KERNEL_INPUTS}")
add_custom_command(
  OUTPUT ${ADR_KERNEL_CPP} ${ADR_KERNEL_CPP}.txt
  COMMAND ${CMAKE_COMMAND} -DOUTPUT=${ADR_KERNEL_CPP} -DINPUT_LIST=${CMAKE_CURRENT_BINARY_DIR}/kernel_inputs.txt
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_kernels.cmake
  DEPENDS ${ADR_KERNEL_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_kernels.cmake
  COMMENT "Embedding GPU kernels")

file(GLOB ADR_STAGE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/*.cpp)
string(REPLACE "[_.]" "[_.a-z]*" ADR_STAGE_REGEX "${ADR_GROUP_REGEX}")
list(FILTER ADR_STAGE_SOURCES INCLUDE REGEX "${ADR_STAGE_REGEX}")
set(ADR_SOURCES
    src/api.cpp
    src/controls.cpp
    src/rcpss.cpp
    src/gpu/device.cpp
    ${ADR_STAGE_SOURCES}
    ${ADR_KERNEL_CPP})

add_library(adr_engine_objects OBJECT ${ADR_SOURCES})
if(ADR_GROUPS STREQUAL "all")
  target_compile_definitions(adr_engine_objects PRIVATE ADR_HAVE_SEQUENCE=1)
endif()
target_include_directories(adr_engine_objects PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(adr_engine_objects PRIVATE ADR_BUILDING_SHARED)
set(ADR_LINK_LIBS "")
# The engine and the plugin use threads; glibc before 2.34 keeps them in libpthread.
find_package(Threads REQUIRED)
list(APPEND ADR_LINK_LIBS Threads::Threads)

if(ADR_WITH_METAL)
  target_sources(adr_engine_objects PRIVATE src/gpu/metal_device.mm)
  set_source_files_properties(src/gpu/metal_device.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_compile_definitions(adr_engine_objects PUBLIC ADR_HAVE_METAL=1)
  list(APPEND ADR_LINK_LIBS "-framework Metal" "-framework Foundation")
endif()

if(ADR_WITH_OPENCL)
  target_sources(adr_engine_objects PRIVATE src/gpu/opencl_device.cpp)
  target_compile_definitions(adr_engine_objects PUBLIC ADR_HAVE_OPENCL=1)
  if(NOT WIN32)
    list(APPEND ADR_LINK_LIBS ${CMAKE_DL_LIBS})
  endif()
endif()

if(ADR_WITH_CUDA)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cuda_kernels.cmake)
endif()

if(ADR_WITH_CPU)
  # Host tool that lists the kernels (and their barrier use) for the CPU table.
  add_executable(adr_cpu_kernel_list tools/gen/cpu_kernel_list.cpp)
  set(ADR_CPU_LIST ${CMAKE_CURRENT_BINARY_DIR}/cpu_kernel_list.inc)
  add_custom_command(
    OUTPUT ${ADR_CPU_LIST}
    COMMAND adr_cpu_kernel_list ${ADR_KERNEL_CPP}.txt ${ADR_CPU_LIST}
    DEPENDS adr_cpu_kernel_list ${ADR_KERNEL_CPP}
    COMMENT "Listing kernels for the CPU backend")
  target_sources(adr_engine_objects PRIVATE src/gpu/cpu_device.cpp src/gpu/cpu_kernels.cpp ${ADR_CPU_LIST})
  target_include_directories(adr_engine_objects PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  set_source_files_properties(src/gpu/cpu_kernels.cpp PROPERTIES OBJECT_DEPENDS "${ADR_KERNEL_CPP};${ADR_CPU_LIST}")
  if(NOT MSVC)
    # Kernels assume GPU integer semantics (wrapping) and reinterpret buffers.
    set_source_files_properties(src/gpu/cpu_kernels.cpp PROPERTIES COMPILE_OPTIONS "-fwrapv;-fno-strict-aliasing")
  endif()
  target_compile_definitions(adr_engine_objects PUBLIC ADR_HAVE_CPU=1)
endif()

# Shared library: C API for host integration and tools.
add_library(adr_engine SHARED $<TARGET_OBJECTS:adr_engine_objects>)
target_link_libraries(adr_engine PRIVATE ${ADR_LINK_LIBS})
target_include_directories(adr_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Static library for the self-contained OFX plugin.
add_library(adr_engine_static STATIC $<TARGET_OBJECTS:adr_engine_objects>)
target_link_libraries(adr_engine_static PUBLIC ${ADR_LINK_LIBS})
target_include_directories(adr_engine_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(ADR_GROUPS STREQUAL "all")
  file(GLOB ADR_TOOL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp)
else()
  set(ADR_TOOL_SOURCES "")
endif()
foreach(tool_source IN LISTS ADR_TOOL_SOURCES)
  get_filename_component(tool_name ${tool_source} NAME_WE)
  add_executable(${tool_name} ${tool_source})
  target_link_libraries(${tool_name} PRIVATE adr_engine)
endforeach()
