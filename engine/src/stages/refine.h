// Group "refine": nine-level motion masks and dirt-aware motion refinement.
#pragma once

#include <array>
#include <functional>
#include <map>

#include "stages/stages.h"

namespace adr {

struct MotionMasks {
  std::vector<Tensor> binary, confidence, classes, block_confidence;  // 9 levels each
};
MotionMasks prepare_motion_masks(Device& device, const Tensor& mask, const Tensor& confidence);

// refine_motion_pair for one level. initial/coarse are int16 fields; coarse
// may be null for the first level. Returns the refined pair.
std::array<Tensor, 2> refine_motion_pair(Device& device, const std::array<Tensor, 2>& initial,
                                         const std::array<Tensor, 2>* coarse, const Tensor& classes,
                                         const Tensor& block_confidence, const Tensor& pixel_confidence,
                                         const Tensor& current, const std::array<Tensor, 2>& references, int level,
                                         int threshold, int mode, std::array<bool, 2> valid);

// The complete per-frame refinement before repair.
// motion_levels[offset] are PairMotion::levels for frame -> frame+offset.
// prepared(k) returns the Prepared frame at frame+k (k in -2..2).
// Returns refined full-resolution vectors per enabled offset.
std::map<int, Tensor> refine_frame_motion(Device& device, const MotionMasks& masks,
                                          const std::map<int, std::vector<Tensor>>& motion_levels,
                                          const std::vector<int>& reference_offsets,
                                          const std::function<const Prepared&(int)>& prepared, int width);

}  // namespace adr
