// Group "motion": multilevel pair motion estimation.
#pragma once

#include "stages/stages.h"

namespace adr {

// RetainedPairMotion. Every tensor stays on the device.
struct PairMotion {
  Tensor vectors;      // final byte-level vectors, int16 (gh,gw,2)
  Tensor history;      // packed_levels[2].regularized.vectors, int16
  Tensor scene_error;  // uint8 error plane used for cut/flash statistics
  std::vector<Tensor> levels;  // regularized packed levels[:-1] then byte searches
  Tensor motion_noise;         // float32 scalar, shape {}
};

// estimate_prepared_pair with the default MotionSearchConfig for the pyramid
// depth. seed: int16 (sh,sw,2) from reduce_motion_vectors(history).
// motion_noise: optional float32 scalar from the nearest reference (|offset|==2).
PairMotion estimate_pair(Device& device, const Prepared& current, const Prepared& reference, const Tensor& seed,
                         const Tensor* motion_noise);

// reduce_motion_vectors(source, quantum=1).
Tensor reduce_motion_vectors(Device& device, const Tensor& source);

// Device float32 scalar: mean of a uint8 plane (integer sum, one division).
Tensor error_mean(Device& device, const Tensor& error);

}  // namespace adr
