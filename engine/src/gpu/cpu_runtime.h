// Runtime used by kernels compiled for the CPU backend (dialect ADR_CPU).
//
// Each work-group runs on one worker thread. Kernels without barriers run as a
// plain loop over work items. Kernels with barriers run every work item as a
// fiber (its own small stack); BARRIER yields to the group scheduler, which
// resumes each unfinished item in turn, so all items reach a barrier before any
// continues. LOCAL arrays are thread_local and therefore shared by the items of
// the group running on that thread.
#pragma once

#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace adr {
namespace cpu {

struct Item {
  int gid[3] = {0, 0, 0};
  int lid[3] = {0, 0, 0};
  int grp[3] = {0, 0, 0};
  int lsz[3] = {1, 1, 1};
};

// Yields the current work item to the group scheduler. Only valid inside a
// kernel launched in barrier mode; elsewhere it records an error.
void barrier();

inline float as_float(int v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
inline float as_float(unsigned v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
inline int as_int(float f) {
  int v;
  std::memcpy(&v, &f, 4);
  return v;
}

#if defined(_MSC_VER)
inline int atomic_add(int* p, int v) { return _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(p), v); }
inline int atomic_load(const int* p) { return *reinterpret_cast<const volatile int*>(p); }
inline int atomic_cas(int* p, int expected, int desired) {
  return _InterlockedCompareExchange(reinterpret_cast<volatile long*>(p), desired, expected);
}
#else
inline int atomic_add(int* p, int v) { return __atomic_fetch_add(p, v, __ATOMIC_RELAXED); }
inline int atomic_load(const int* p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
inline int atomic_cas(int* p, int expected, int desired) {
  __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  return expected;
}
#endif
inline int atomic_min(int* p, int v) {
  int old = atomic_load(p);
  while (v < old) {
    const int seen = atomic_cas(p, old, v);
    if (seen == old) break;
    old = seen;
  }
  return old;
}
inline int atomic_max(int* p, int v) {
  int old = atomic_load(p);
  while (v > old) {
    const int seen = atomic_cas(p, old, v);
    if (seen == old) break;
    old = seen;
  }
  return old;
}

}  // namespace cpu
}  // namespace adr
