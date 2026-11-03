// Development stages for the detect group.
#include <chrono>

#include "stages/detect.h"

namespace adr {
namespace detect {
Tensor repair_weights(Device& device, const Tensor& noise, int valid_references, float repair_weight,
                      const Controls& controls, Tensor& lut, Tensor& divisor, Tensor& rounding);
Tensor contrast_blend_impl(Device& device, const Tensor& repaired, const Tensor& original, const Controls& controls,
                           Tensor* contrast_mask, Tensor* noise_out);
}  // namespace detect

namespace {

Tensor empty_u8(Device& device) { return empty(device, DType::u8, {0}); }

Controls controls_at(const std::vector<double>& s, size_t offset) {
  require(s.size() >= offset + 17, "expected 17 control scalars");
  return controls_from_scalars(s.data() + offset);
}

// in: image (H,W,C) u8, vectors i16. scalars: channel.
std::vector<Tensor> stage_compensate(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 2, "detect_compensate: image, vectors");
  return {compensate_analysis(d, in[0], int(s.at(0)), in[1])};
}

std::vector<Tensor> stage_gaussian(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  return {gaussian_bytes(d, in.at(0), int(s.at(0)))};
}

// in: source, lut, divisor, rounding. scalars: window.
std::vector<Tensor> stage_weighted(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 4, "detect_weighted: source, lut, divisor, rounding");
  return {weighted_residual(d, in[0], in[1], int(s.at(0)), in[2], in[3])};
}

std::vector<Tensor> stage_remap(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  return {remap_bytes(d, in.at(0), int(s.at(0)), int(s.at(1)), int(s.at(2)), int(s.at(3)))};
}

// in: current (H,W), references (N,H,W). scalars: third, fourth.
std::vector<Tensor> stage_combine(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 2, "detect_combine: current, references");
  std::vector<Tensor> refs;
  for (int k = 0; k < in[1].dim(0); ++k) refs.push_back(select0(d, in[1], k));
  TemporalCombine c = combine_bytes(d, in[0], refs, float(s.at(0)), float(s.at(1)));
  return {c.base, c.centered_residual, c.temporal_spread};
}

// in: planes (H,W,3), rgb_planes (H,W,3) or empty, ref planes (N,H,W,3), ref rgb (N,H,W,3) or empty,
// vectors (N,VH,VW,2). scalars: 17 controls, then N offsets.
std::vector<Tensor> stage_frame(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 5, "detect_frame: planes, rgb, ref planes, ref rgb, vectors");
  const Controls controls = controls_at(s, 0);
  const int count = in[2].dim(0);
  Prepared current;
  current.planes = in[0];
  if (controls.mode == 1) current.rgb_planes = in[1];
  std::vector<Prepared> refs(count);
  std::vector<const Prepared*> ref_ptrs;
  std::vector<Tensor> vectors;
  std::vector<int> offsets;
  for (int k = 0; k < count; ++k) {
    refs[k].planes = select0(d, in[2], k);
    if (controls.mode == 1) refs[k].rgb_planes = select0(d, in[3], k);
    vectors.push_back(select0(d, in[4], k));
    offsets.push_back(int(s.at(17 + k)));
  }
  for (auto& r : refs) ref_ptrs.push_back(&r);
  FrameDetection f = detect_frame(d, current, ref_ptrs, vectors, offsets, controls);
  return {f.combined.base, f.combined.centered_residual, f.combined.temporal_spread, f.noise.trimmed_mean,
          f.noise.noise, f.noise.lut, f.noise.divisor, f.noise.rounding, f.detection.residual,
          f.detection.confidence, f.detection.mask};
}

// Same inputs as detect_frame plus scalar 21: repeat count. Returns ms per frame (f32 {}).
std::vector<Tensor> stage_frame_bench(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  const Controls controls = controls_at(s, 0);
  const int count = in[2].dim(0);
  const int repeats = int(s.at(17 + count));
  Prepared current;
  current.planes = in[0];
  if (controls.mode == 1) current.rgb_planes = in[1];
  std::vector<Prepared> refs(count);
  std::vector<const Prepared*> ref_ptrs;
  std::vector<Tensor> vectors;
  std::vector<int> offsets;
  for (int k = 0; k < count; ++k) {
    refs[k].planes = select0(d, in[2], k);
    if (controls.mode == 1) refs[k].rgb_planes = select0(d, in[3], k);
    vectors.push_back(select0(d, in[4], k));
    offsets.push_back(int(s.at(17 + k)));
  }
  for (auto& r : refs) ref_ptrs.push_back(&r);
  detect_frame(d, current, ref_ptrs, vectors, offsets, controls);
  d.synchronize();
  const auto start = std::chrono::steady_clock::now();
  for (int r = 0; r < repeats; ++r) {
    detect_frame(d, current, ref_ptrs, vectors, offsets, controls);
    d.flush();
  }
  d.synchronize();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  const float per = float(ms / repeats);
  return {upload(d, DType::f32, {}, &per)};
}

// in: spread, [noise_mean f32 {}]. scalars: valid_references.
std::vector<Tensor> stage_condition(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  const Tensor* noise = in.size() > 1 ? &in[1] : nullptr;
  return {condition_repair_spread(d, in.at(0), noise, int(s.at(0)))};
}

// in: noise f32 {}. scalars: valid, repair_weight, 17 controls.
std::vector<Tensor> stage_repair_weights(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  const Controls controls = controls_at(s, 2);
  Tensor lut, divisor, rounding;
  Tensor modified =
      detect::repair_weights(d, in.at(0), int(s.at(0)), float(s.at(1)), controls, lut, divisor, rounding);
  return {modified, lut, divisor, rounding};
}

// in: centered residual, noise {}. scalars: valid, 17 controls.
std::vector<Tensor> stage_repair_mask(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  return {build_repair_mask(d, in.at(0), in.at(1), int(s.at(0)), controls_at(s, 1))};
}

std::vector<Tensor> stage_excluded(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  return {excluded_regions(d, in.at(0), int(s.at(0)))};
}

// in: conditioned spread. scalars: valid, 17 controls.
std::vector<Tensor> stage_preservation(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  return {preservation_mask(d, in.at(0), int(s.at(0)), controls_at(s, 1))};
}

// in: repaired, original int32. scalars: 17 controls. Out: blend, raw contrast mask, noise.
std::vector<Tensor> stage_contrast(Device& d, const std::vector<Tensor>& in, const std::vector<double>& s) {
  Tensor mask, noise;
  Tensor blend = detect::contrast_blend_impl(d, in.at(0), in.at(1), controls_at(s, 0), &mask, &noise);
  return {blend, mask, noise};
}

ADR_STAGE("detect_compensate", stage_compensate);
ADR_STAGE("detect_gaussian", stage_gaussian);
ADR_STAGE("detect_weighted", stage_weighted);
ADR_STAGE("detect_remap", stage_remap);
ADR_STAGE("detect_combine", stage_combine);
ADR_STAGE("detect_frame", stage_frame);
ADR_STAGE("detect_frame_bench", stage_frame_bench);
ADR_STAGE("detect_condition", stage_condition);
ADR_STAGE("detect_repair_weights", stage_repair_weights);
ADR_STAGE("detect_repair_mask", stage_repair_mask);
ADR_STAGE("detect_excluded", stage_excluded);
ADR_STAGE("detect_preservation", stage_preservation);
ADR_STAGE("detect_contrast", stage_contrast);

}  // namespace
}  // namespace adr
