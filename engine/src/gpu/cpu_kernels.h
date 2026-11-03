// Kernel table for the CPU backend (generated from the kernel source at build time).
#pragma once

#include <string>

#include "gpu/cpu_runtime.h"

namespace adr {
namespace cpu {

struct LaunchView {
  void* const* buffers;
  const void* params;
  int lsz[3];
};
// Runs one whole work-group as a plain loop (kernels without barriers).
using GroupFn = void (*)(const LaunchView&, const int grp[3]);
// Runs one work item (barrier kernels, each item on its own fiber).
using ItemFn = void (*)(const LaunchView&, const Item&);

struct KernelEntry {
  const char* name;
  GroupFn group;
  ItemFn item;
  bool barrier;
};

const KernelEntry* find_kernel(const std::string& name);

}  // namespace cpu
}  // namespace adr
