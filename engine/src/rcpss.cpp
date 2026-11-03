#include "rcpss.h"

#include <cstdlib>
#include <cstring>

#include "gpu/device.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define ADR_HAVE_RCPSS 1
#endif

namespace adr {

namespace {

#include "rcpss_tables.inc"

// RCPSS of a power of two is the RCPSS of 1 rescaled exactly.
void copy(RcpssTable& t, const uint32_t* bits) {
  std::memcpy(t.bits, bits, sizeof t.bits);
  float one;
  std::memcpy(&one, &bits[0], 4);
  t.rcp8192 = one / 8192.f;
}

#ifdef ADR_HAVE_RCPSS
void measure(RcpssTable& t) {
  for (int w = 1; w <= kRcpssCount; ++w) {
    volatile float in = float(w);  // keep the compiler from folding the instruction
    const float r = _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(in)));
    std::memcpy(&t.bits[w - 1], &r, 4);
  }
  volatile float in = 8192.f;
  t.rcp8192 = _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(in)));
}
#endif

RcpssTable select() {
  RcpssTable t;
  const char* env = std::getenv("ADR_RECIPROCAL_TABLE");
  const std::string mode = env && *env ? env : "host";
  if (mode == "amd") {
    copy(t, kRcpssAmd);
    t.source = "amd";
  } else if (mode == "intel") {
    copy(t, kRcpssIntel);
    t.source = "intel";
  } else if (mode == "host") {
#ifdef ADR_HAVE_RCPSS
    measure(t);
    const bool amd = !std::memcmp(t.bits, kRcpssAmd, sizeof t.bits);
    const bool intel = !std::memcmp(t.bits, kRcpssIntel, sizeof t.bits);
    t.source = amd ? "host (amd)" : intel ? "host (intel)" : "host (other)";
#else
    copy(t, kRcpssAmd);
    t.source = "amd (default, no x86 rcpss)";
#endif
  } else {
    throw Error("ADR_RECIPROCAL_TABLE must be host, amd or intel");
  }
  return t;
}

}  // namespace

const RcpssTable& rcpss_table() {
  static const RcpssTable table = select();
  return table;
}

}  // namespace adr
