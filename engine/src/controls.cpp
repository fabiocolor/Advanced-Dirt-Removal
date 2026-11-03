#include "controls.h"

#include <cmath>
#include <string>

#include "gpu/device.h"

namespace adr {

float interpolate(float x, float a, float b, float low, float high) {
  if (x <= a) return low;
  if (x >= b) return high;
  const float span = high - low;
  const float offset = x - a;
  const float product = span * offset;
  const float ratio = product / (b - a);
  return low + ratio;
}

Controls Controls::from_c(const adr_controls& c) {
  Controls k;
  k.enabled = c.enabled != 0;
  k.mode = c.mode;
  k.remove_dark = c.remove_dark != 0;
  k.remove_light = c.remove_light != 0;
  k.repair_amount = c.repair_amount;
  k.analysis_detail = c.analysis_detail;
  k.sensitivity = c.sensitivity;
  k.extra_sensitivity = c.extra_sensitivity != 0;
  k.motion_guard = c.motion_guard;
  k.shot_changes = c.shot_changes;
  k.shot_guard = c.shot_guard;
  k.blend_repairs = c.blend_repairs != 0;
  k.blend_amount = c.blend_amount;
  k.extended_range = c.extended_range != 0;
  k.flash_guard = c.flash_guard != 0;
  k.limit_spot_size = c.limit_spot_size != 0;
  k.largest_spot = c.largest_spot;
  return k;
}

Controls controls_from_scalars(const double* v) {
  adr_controls c;
  c.enabled = int32_t(v[0]);
  c.mode = int32_t(v[1]);
  c.remove_dark = int32_t(v[2]);
  c.remove_light = int32_t(v[3]);
  c.repair_amount = float(v[4]);
  c.analysis_detail = float(v[5]);
  c.sensitivity = float(v[6]);
  c.extra_sensitivity = int32_t(v[7]);
  c.motion_guard = float(v[8]);
  c.shot_changes = int32_t(v[9]);
  c.shot_guard = float(v[10]);
  c.blend_repairs = int32_t(v[11]);
  c.blend_amount = float(v[12]);
  c.extended_range = int32_t(v[13]);
  c.flash_guard = int32_t(v[14]);
  c.limit_spot_size = int32_t(v[15]);
  c.largest_spot = int32_t(v[16]);
  Controls k = Controls::from_c(c);
  k.validate();
  return k;
}

void Controls::validate() const {
  auto in = [](float v, float hi) { return std::isfinite(v) && v >= 0 && v <= hi; };
  if (largest_spot < 1 || largest_spot > 256) throw Error("largest_spot must be in [1,256]");
  if (shot_changes < 0 || shot_changes > 2) throw Error("shot_changes must be in [0,2]");
  if (mode < 0 || mode > 1) throw Error("mode must be 0 or 1");
  if (!in(repair_amount, 2)) throw Error("repair_amount must be in [0,2]");
  for (float v : {analysis_detail, sensitivity, motion_guard, shot_guard, blend_amount})
    if (!in(v, 1)) throw Error("controls must be in [0,1]");
}

float Controls::repair_weight() const { return repair_amount * .5f; }

std::pair<float, float> Controls::spread_weights(int count) const {
  if (count < 0 || count > 4) throw Error("Expected zero to four references");
  float third = interpolate(motion_guard, 0, 1, 0, .5f);
  const float fourth = interpolate(motion_guard, 0, 1, 0, .2f);
  if (count == 3 && shot_changes) {
    const float scaled = shot_guard * fourth;
    third = third + scaled;
  }
  return {third, fourth};
}

float Controls::noise_multiplier(int count) const {
  const float cut = shot_changes ? interpolate(shot_guard, 0, 1, -.25f, .75f) : 0.f;
  if (count >= 0 && count <= 2) return interpolate(motion_guard, 0, 1, 1.25f, 2.25f) + cut;
  if (count == 3) {
    const float half = cut * .5f;
    return interpolate(motion_guard, 0, 1, 1.1f, 1.5f) + half;
  }
  if (count == 4) return interpolate(motion_guard, .75f, 1, 0, .25f) + 1.f;
  throw Error("Expected zero to four references");
}

float Controls::analysis_scale() const {
  const float product = .57f * interpolate(analysis_detail, 0, 1, 1, -1);
  return product + 1.f;
}

std::pair<int, int> Controls::preservation_thresholds(int count) const {
  if (count < 0 || count > 4) throw Error("Expected zero to four references");
  float amount = sensitivity;
  if (count == 2 && shot_changes)
    amount = amount * interpolate(shot_guard, 0, 1, 1, .25f);
  else if (count == 3 && shot_changes)
    amount = amount * interpolate(shot_guard, 0, 1, 1, .8f);
  const int low = int(interpolate(amount, 0, 1, 10, 45));
  const int high = int(interpolate(amount, 0, 1, 35, 70));
  if (extra_sensitivity && count != 4) return {low * 2, high + low};
  return {low, high};
}

int Controls::largest_spot_area() const {
  const double half = largest_spot * .5;
  return int(half * half * 3.141592653589793 + .5);
}

std::pair<int, int> Controls::blend_thresholds() const {
  const float amount = 1.f - blend_amount;
  const float low = interpolate(amount, 0, 1, 5, 40) + .5f;
  const float high = interpolate(amount, 0, 1, 30, 70) + .5f;
  return {int(low), int(high)};
}

}  // namespace adr
