// CPU backend kernels: the single-source kernel text compiled as C++ with the
// ADR_CPU dialect, plus a typed launcher for each kernel.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "gpu/cpu_kernels.h"

#define ADR_CPU 1
namespace adr {
namespace cpu_kernels {
#include "kernel_source.cpp.txt"  // generated: dialect, params and every kernel
}  // namespace cpu_kernels

namespace cpu {
namespace {

template <auto F>
struct Kernel;

template <class... A, void (*F)(A...)>
struct Kernel<F> {
  static constexpr size_t kArgs = sizeof...(A);
  static_assert(kArgs >= 2, "kernel needs a parameter struct and an item");
  using Args = std::tuple<A...>;
  using Params = std::remove_cv_t<std::remove_reference_t<std::tuple_element_t<kArgs - 2, Args>>>;

  template <size_t... I>
  static inline void call(const LaunchView& v, const Item& it, std::index_sequence<I...>) {
    F(static_cast<std::tuple_element_t<I, Args>>(v.buffers[I])..., *static_cast<const Params*>(v.params), it);
  }
  static void item(const LaunchView& v, const Item& it) { call(v, it, std::make_index_sequence<kArgs - 2>()); }
  static void group(const LaunchView& v, const int grp[3]) {
    Item it;
    for (int d = 0; d < 3; ++d) {
      it.grp[d] = grp[d];
      it.lsz[d] = v.lsz[d];
    }
    for (int z = 0; z < v.lsz[2]; ++z)
      for (int y = 0; y < v.lsz[1]; ++y)
        for (int x = 0; x < v.lsz[0]; ++x) {
          it.lid[0] = x;
          it.lid[1] = y;
          it.lid[2] = z;
          it.gid[0] = grp[0] * v.lsz[0] + x;
          it.gid[1] = grp[1] * v.lsz[1] + y;
          it.gid[2] = grp[2] * v.lsz[2] + z;
          call(v, it, std::make_index_sequence<kArgs - 2>());
        }
  }
};

#define ADR_CPU_KERNEL(name, barrier) {#name, &Kernel<&cpu_kernels::name>::group, &Kernel<&cpu_kernels::name>::item, barrier},
const KernelEntry kKernels[] = {
#include "cpu_kernel_list.inc"
};
#undef ADR_CPU_KERNEL

}  // namespace

const KernelEntry* find_kernel(const std::string& name) {
  for (const auto& k : kKernels)
    if (name == k.name) return &k;
  return nullptr;
}

}  // namespace cpu
}  // namespace adr
