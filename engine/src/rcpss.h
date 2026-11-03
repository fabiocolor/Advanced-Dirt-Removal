// A few engine divisions use the x86 approximate reciprocal (RCPSS), whose low
// bits differ between CPU vendors. The engine uses the table selected by
// ADR_RECIPROCAL_TABLE: "host" (default on x86: this CPU's RCPSS), "amd"
// (default elsewhere) or "intel". Pin one value on render farms that mix CPUs
// so every machine renders identical frames.
#pragma once
#include <cstdint>
#include <string>

namespace adr {

constexpr int kRcpssCount = 2295;  // weight sums of nine byte confidences

struct RcpssTable {
  uint32_t bits[kRcpssCount];  // float32 bit pattern of rcpss(float(W)), W = 1..kRcpssCount
  float rcp8192;                // rcpss(8192.f): detection's Gaussian scale
  std::string source;          // e.g. "host (amd)", "intel", "amd (default, no x86 rcpss)"
};

const RcpssTable& rcpss_table();  // selected once per process; throws Error on a bad ADR_RECIPROCAL_TABLE

}  // namespace adr
