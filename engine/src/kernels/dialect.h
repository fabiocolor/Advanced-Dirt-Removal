/* Single-source kernel dialect for Metal, CUDA, OpenCL and the CPU backend.
 *
 * Kernels are written once against these macros and compiled for each backend.
 * Floating-point contraction is disabled everywhere so every backend and the
 * CPU evaluate float32 expressions in the same order and give identical bytes.
 *
 * Kernel signature convention:
 *   KERNEL void name(GLOBAL const uchar* in, GLOBAL uchar* out, PARAMS(MyParams) KARGS)
 * Buffers come first, in order, followed by exactly one parameter struct.
 * Global work sizes are rounded up to whole groups: always bounds-check.
 */
#ifndef ADR_DIALECT_H
#define ADR_DIALECT_H

#if defined(ADR_METAL)
#include <metal_stdlib>
using namespace metal;
#pragma METAL fp math_mode(safe)
#pragma METAL fp contract(off)
#define KERNEL kernel
#define DEVICE_FN static inline
#define GLOBAL device
#define LOCAL threadgroup
#define LPTR threadgroup /* qualifier for pointers to LOCAL memory */
#define PARAMS(T) constant T& p
#define KARGS , uint3 adr_gid [[thread_position_in_grid]], uint3 adr_lid [[thread_position_in_threadgroup]], \
    uint3 adr_grp [[threadgroup_position_in_grid]], uint3 adr_lsz [[threads_per_threadgroup]]
#define GID_X int(adr_gid.x)
#define GID_Y int(adr_gid.y)
#define GID_Z int(adr_gid.z)
#define LID_X int(adr_lid.x)
#define LID_Y int(adr_lid.y)
#define GROUP_X int(adr_grp.x)
#define GROUP_Y int(adr_grp.y)
#define GROUP_Z int(adr_grp.z)
#define LSIZE_X int(adr_lsz.x)
#define BARRIER threadgroup_barrier(mem_flags::mem_threadgroup)
#define GBARRIER threadgroup_barrier(mem_flags::mem_device | mem_flags::mem_threadgroup)
#define ATOMIC_INT atomic_int
#define ATOMIC_ADD(ptr, value) atomic_fetch_add_explicit((ptr), (value), memory_order_relaxed)
#define ATOMIC_MIN(ptr, value) atomic_fetch_min_explicit((ptr), (value), memory_order_relaxed)
#define ATOMIC_MAX(ptr, value) atomic_fetch_max_explicit((ptr), (value), memory_order_relaxed)
#define adr_rint(x) rint(x)
#define adr_floor(x) floor(x)
#define adr_sqrt(x) sqrt(x)
#define adr_fabs(x) fabs(x)
#define adr_as_float(x) as_type<float>(x)
#define ADR_CONST constant
#define adr_as_int(x) as_type<int>(x)

#elif defined(ADR_CUDA)
#define KERNEL extern "C" __global__
#define DEVICE_FN static __device__ __forceinline__
#define GLOBAL
#define LOCAL __shared__
#define LPTR
#define PARAMS(T) const T p
#define KARGS
#define GID_X int(blockIdx.x * blockDim.x + threadIdx.x)
#define GID_Y int(blockIdx.y * blockDim.y + threadIdx.y)
#define GID_Z int(blockIdx.z * blockDim.z + threadIdx.z)
#define LID_X int(threadIdx.x)
#define LID_Y int(threadIdx.y)
#define GROUP_X int(blockIdx.x)
#define GROUP_Y int(blockIdx.y)
#define GROUP_Z int(blockIdx.z)
#define LSIZE_X int(blockDim.x)
#define BARRIER __syncthreads()
#define GBARRIER __syncthreads()
#define ATOMIC_INT int
#define ATOMIC_ADD(ptr, value) atomicAdd((ptr), (value))
#define ATOMIC_MIN(ptr, value) atomicMin((ptr), (value))
#define ATOMIC_MAX(ptr, value) atomicMax((ptr), (value))
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
DEVICE_FN int clamp(int v, int lo, int hi) { return min(max(v, lo), hi); }
DEVICE_FN float clamp(float v, float lo, float hi) { return fminf(fmaxf(v, lo), hi); }
#define adr_rint(x) rintf(x)
#define adr_floor(x) floorf(x)
#define adr_sqrt(x) __fsqrt_rn(x)
#define adr_fabs(x) fabsf(x)
#define adr_as_float(x) __int_as_float(x)
#define ADR_CONST __constant__
#define adr_as_int(x) __float_as_int(x)

#elif defined(ADR_OPENCL)
#pragma OPENCL FP_CONTRACT OFF
#define KERNEL __kernel
#define DEVICE_FN static inline
#define GLOBAL __global
#define LOCAL __local
#define LPTR __local
#define PARAMS(T) const T p
#define KARGS
#define GID_X ((int)get_global_id(0))
#define GID_Y ((int)get_global_id(1))
#define GID_Z ((int)get_global_id(2))
#define LID_X ((int)get_local_id(0))
#define LID_Y ((int)get_local_id(1))
#define GROUP_X ((int)get_group_id(0))
#define GROUP_Y ((int)get_group_id(1))
#define GROUP_Z ((int)get_group_id(2))
#define LSIZE_X ((int)get_local_size(0))
#define BARRIER barrier(CLK_LOCAL_MEM_FENCE)
#define GBARRIER barrier(CLK_LOCAL_MEM_FENCE | CLK_GLOBAL_MEM_FENCE)
#define ATOMIC_INT int
#define ATOMIC_ADD(ptr, value) atomic_add((ptr), (value))
#define ATOMIC_MIN(ptr, value) atomic_min((ptr), (value))
#define ATOMIC_MAX(ptr, value) atomic_max((ptr), (value))
#define adr_rint(x) rint(x)
#define adr_floor(x) floor(x)
#define adr_sqrt(x) sqrt(x)
#define adr_fabs(x) fabs(x)
#define adr_as_float(x) as_float(x)
#define ADR_CONST __constant
#define adr_as_int(x) as_int(x)
#elif defined(ADR_CPU)
/* Compiled as C++ inside namespace adr::cpu_kernels (src/gpu/cpu_kernels.cpp),
 * which includes <cmath> and gpu/cpu_runtime.h first. */
#define KERNEL static
#define DEVICE_FN static inline
#define GLOBAL
#define LOCAL static thread_local
#define LPTR
#define PARAMS(T) const T& p
#define KARGS , const ::adr::cpu::Item& adr_it
#define GID_X (adr_it.gid[0])
#define GID_Y (adr_it.gid[1])
#define GID_Z (adr_it.gid[2])
#define LID_X (adr_it.lid[0])
#define LID_Y (adr_it.lid[1])
#define GROUP_X (adr_it.grp[0])
#define GROUP_Y (adr_it.grp[1])
#define GROUP_Z (adr_it.grp[2])
#define LSIZE_X (adr_it.lsz[0])
#define BARRIER ::adr::cpu::barrier()
#define GBARRIER ::adr::cpu::barrier()
#define ATOMIC_INT int
#define ATOMIC_ADD(ptr, value) ::adr::cpu::atomic_add((ptr), (value))
#define ATOMIC_MIN(ptr, value) ::adr::cpu::atomic_min((ptr), (value))
#define ATOMIC_MAX(ptr, value) ::adr::cpu::atomic_max((ptr), (value))
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
#define adr_rint(x) std::rint(x)
#define adr_floor(x) std::floor(x)
#define adr_sqrt(x) std::sqrt(x)
#define adr_fabs(x) std::fabs(x)
#define adr_as_float(x) ::adr::cpu::as_float(x)
#define ADR_CONST static const
#define adr_as_int(x) ::adr::cpu::as_int(x)
#else
#error "Define ADR_METAL, ADR_CUDA, ADR_OPENCL or ADR_CPU"
#endif

/* Portable helpers with C semantics (truncating division, arithmetic shifts). */
DEVICE_FN int adr_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
DEVICE_FN int adr_absi(int v) { return v < 0 ? -v : v; }
DEVICE_FN int adr_mini(int a, int b) { return a < b ? a : b; }
DEVICE_FN int adr_maxi(int a, int b) { return a > b ? a : b; }
DEVICE_FN int adr_sign(int v) { return (v > 0) - (v < 0); }
/* Floor division for a positive divisor. */
DEVICE_FN int adr_floordiv(int a, int b) { int q = a / b; return (q * b != a && ((a < 0) != (b < 0))) ? q - 1 : q; }
/* sign(v) * floor((|v| + d/2) / d): symmetric rounding of signed integers. */
DEVICE_FN int adr_round_div(int v, int d) { return adr_sign(v) * ((adr_absi(v) + d / 2) / d); }
DEVICE_FN float adr_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

#endif
