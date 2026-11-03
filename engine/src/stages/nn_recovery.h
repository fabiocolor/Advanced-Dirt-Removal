// Group "nn": auto-recovery classifier and per-frame recovery probability maps.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tensor.h"

#define ADR_RECOVERY_VIDEO 0
#define ADR_RECOVERY_CINEON_LINEAR 1

namespace adr {

struct RecoveryLayer {
  int kind = 0, co = 0, ci = 0, k = 0, stride = 1, pad = 0, flags = 0;
  Tensor weight, bias;
  float fc_bias = 0;
};

struct RecoveryModel {
  std::vector<RecoveryLayer> layers;
};

struct RecoverySpot {
  float x, y;  // centroid, native pixels
  int area;    // pixels at analysis scale
  float probability;
};

struct RecoveryFrame {
  int spots = 0, scored = 0;
  std::vector<RecoverySpot> spot_probability;
};

std::shared_ptr<RecoveryModel> load_recovery_model(Device& device, const std::string& path);
// crops: count x 13 x 160 x 160 bytes. Returns one probability per crop.
std::vector<float> recovery_forward(Device& device, const RecoveryModel& model, const uint8_t* crops, int count);
// RGB float rows (top-down, row_floats apart) for the previous, current and next source frames and the
// cleaned current frame. probability: w*h bytes, 0 = not a repaired spot, else 1 + round(p * 254).
RecoveryFrame recovery_frame(Device& device, const RecoveryModel& model, const float* prev, const float* current,
                             const float* next, const float* cleaned, int w, int h, size_t row_floats, int encoding,
                             uint8_t* probability);

}  // namespace adr
