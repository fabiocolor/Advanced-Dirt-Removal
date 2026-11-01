// Engine stages.
//
// Every stage is deterministic: identical inputs give identical bytes on every
// backend. Analysis planes are stored bottom-up.
#pragma once

#include <vector>

#include "kernels/params.h"
#include "tensor.h"

namespace adr {

int pyramid_levels(int width);

// hdr_analysis_curve(byte): 32768 entries, generated on the host once. Device copy cached per device.
const std::vector<int>& hdr_curve(bool byte);
Tensor hdr_curve_device(Device& device, bool byte);

struct Prepared {
  Tensor planes;               // (H,W,3) u8
  std::vector<Tensor> packed;  // (H>>l,W>>l,4) u8, finest first
  Tensor half_luma;            // (2H,2W) u8
  Tensor rgb_planes;           // (H,W,3) u8, RGB mode only
};

// rgb: float32 RGB rows (top-down when flip). Writes signed-chroma features
// (3,H,W), unclamped, when features is non-null.
Prepared prepare_float(Device& device, const Buffer& rgb, int h, int w, int in_stride_floats, bool flip,
                       int levels, bool rgb_mode, bool hdr, Tensor* features);

}  // namespace adr
