// User controls and their float32 parameter mappings. Host code is compiled
// with floating-point contraction disabled so every mapping rounds identically
// on every compiler and CPU.
#pragma once

#include <utility>

#include "adr/adr.h"

namespace adr {

struct Controls {
  int mode = 0;
  bool enabled = true;
  bool extended_range = false;
  bool flash_guard = false;
  bool limit_spot_size = false;
  int largest_spot = 25;
  bool extra_sensitivity = false;
  bool remove_dark = true;
  bool remove_light = true;
  float repair_amount = 1.f;
  float analysis_detail = .5f;
  float sensitivity = .75f;
  float motion_guard = .25f;
  float shot_guard = .75f;
  int shot_changes = 1;
  bool blend_repairs = true;
  float blend_amount = .5f;

  static Controls from_c(const adr_controls& c);
  // Validates ranges; throws adr::Error.
  void validate() const;

  float repair_weight() const;
  std::pair<float, float> spread_weights(int count) const;
  float noise_multiplier(int count) const;
  float analysis_scale() const;
  std::pair<int, int> preservation_thresholds(int count) const;
  int largest_spot_area() const;
  std::pair<int, int> blend_thresholds() const;
};

// Linear interpolation with float32 rounding at every step.
float interpolate(float x, float a, float b, float low, float high);

// Debug-stage encoding: the 17 controls as doubles in adr_controls field order.
Controls controls_from_scalars(const double* values);

}  // namespace adr
