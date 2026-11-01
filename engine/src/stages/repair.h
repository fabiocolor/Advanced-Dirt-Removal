// Group "repair": float repair, reconstruction, RGB conversion and the
// float post-repair contrast limiter.
#pragma once

#include <map>

#include "controls.h"
#include "stages/detect.h"
#include "stages/stages.h"

namespace adr {

// compensate_float_plane with unbounded limits (+-FLT_MAX), one channel of a
// (3,H,W) float32 tensor -> (H,W) float32.
Tensor compensate_float(Device& device, const Tensor& features, int channel, const Tensor& vectors);

struct FloatRepair {
  Tensor output;             // (3,H,W) float32 repaired features
  Tensor repair_mask;        // (H,W) uint8
  Tensor preservation_mask;  // (H,W) uint8
};

// repair_float_frame. reference_features/refined follow reference_offsets.
FloatRepair repair_float_frame(Device& device, const Tensor& current_features,
                               const std::vector<Tensor>& reference_features, const std::vector<Tensor>& refined,
                               const std::vector<int>& reference_offsets, const FrameDetection& detection,
                               const Prepared& current, const std::vector<const Prepared*>& references,
                               const Controls& controls);

// float_features_to_rgb + limit_float_repair_contrast per channel (when
// blend_repairs) and the final vertical flip. original: top-down source
// RGB (H,W,3) float32. Writes top-down (H,W,3) float32 into output.
void finish_rgb(Device& device, const Tensor& repaired_features, const Buffer& original, int h, int w,
                const Controls& controls, const Buffer& output);

}  // namespace adr
