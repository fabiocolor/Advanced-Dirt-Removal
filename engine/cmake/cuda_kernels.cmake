# CUDA backend: compile the single-source kernels with nvcc into a fatbin
# (cubins for the listed architectures plus PTX for forward compatibility) and
# embed it. Only nvcc is required at build time; the plugin links no CUDA
# libraries and loads the driver dynamically.
# ADR_CUDA_PREBUILT_FATBIN: reuse a fatbin built by nvcc elsewhere (it is
# OS-independent) when this machine has no CUDA toolkit. It must come from
# the identical kernel source; the embedded kernel_source_sha256 records it.
set(ADR_CUDA_PREBUILT_FATBIN "" CACHE FILEPATH "Prebuilt CUDA fatbin (optional)")
if(NOT ADR_CUDA_PREBUILT_FATBIN)
  find_program(ADR_NVCC nvcc HINTS ENV CUDA_PATH PATH_SUFFIXES bin REQUIRED)
endif()
set(ADR_CUDA_ARCHS "75;80;86;89;90;120" CACHE STRING "CUDA SM architectures for cubins")
set(ADR_CUDA_PTX "75" CACHE STRING "PTX architecture embedded for JIT on newer GPUs")

set(ADR_CUDA_CU ${CMAKE_CURRENT_BINARY_DIR}/adr_kernels.cu)
file(WRITE ${ADR_CUDA_CU}.in "#define ADR_CUDA 1\n#include \"${ADR_KERNEL_CPP}.txt\"\n")
file(COPY_FILE ${ADR_CUDA_CU}.in ${ADR_CUDA_CU} ONLY_IF_DIFFERENT)
set(ADR_CUDA_GENCODE "")
foreach(arch IN LISTS ADR_CUDA_ARCHS)
  list(APPEND ADR_CUDA_GENCODE -gencode=arch=compute_${arch},code=sm_${arch})
endforeach()
list(APPEND ADR_CUDA_GENCODE -gencode=arch=compute_${ADR_CUDA_PTX},code=compute_${ADR_CUDA_PTX})

set(ADR_CUDA_FATBIN ${CMAKE_CURRENT_BINARY_DIR}/adr_kernels.fatbin)
if(ADR_CUDA_PREBUILT_FATBIN)
  configure_file(${ADR_CUDA_PREBUILT_FATBIN} ${ADR_CUDA_FATBIN} COPYONLY)
else()
add_custom_command(
  OUTPUT ${ADR_CUDA_FATBIN}
  COMMAND ${ADR_NVCC} -fatbin -O3 -fmad=false -prec-div=true -prec-sqrt=true -ftz=false
          -Xptxas -O3 ${ADR_CUDA_GENCODE} -o ${ADR_CUDA_FATBIN} ${ADR_CUDA_CU}
  DEPENDS ${ADR_CUDA_CU} ${ADR_KERNEL_CPP}.txt
  COMMENT "Compiling CUDA kernels")
endif()

set(ADR_CUDA_EMBED ${CMAKE_CURRENT_BINARY_DIR}/cuda_fatbin.cpp)
add_custom_command(
  OUTPUT ${ADR_CUDA_EMBED}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${ADR_CUDA_FATBIN} -DOUTPUT=${ADR_CUDA_EMBED}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
  DEPENDS ${ADR_CUDA_FATBIN} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
  COMMENT "Embedding CUDA fatbin")

target_sources(adr_engine_objects PRIVATE src/gpu/cuda_device.cpp ${ADR_CUDA_EMBED})
target_compile_definitions(adr_engine_objects PUBLIC ADR_HAVE_CUDA=1)
if(NOT WIN32)
  list(APPEND ADR_LINK_LIBS ${CMAKE_DL_LIBS})
endif()
