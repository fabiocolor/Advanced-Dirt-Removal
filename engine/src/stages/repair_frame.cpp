// repair_float_frame and finish_rgb assemblies. They call the detect group's
// byte-domain functions (compensate_analysis, combine_bytes, repair masks,
// contrast_blend).
#include <algorithm>
#include <array>
#include <chrono>

#include "kernels/params_repair.h"
#include "stages/repair.h"

namespace adr {

// repair.cpp
int repair_vector_block(int h, int w, const Tensor& vectors);
Tensor repair_source_bounds(Device& device, const Buffer& original, int h, int w);
void repair_finish_codes(Device& device, const Tensor& features, const Buffer& original, const Tensor* table,
                         Tensor* codes, Tensor* vblur, const Buffer& output, int h, int w, bool hdr, bool smoothing,
                         const Tensor* bounds, int channel);
void repair_finish_blend(Device& device, const Tensor& features, const Tensor& vblur, const Tensor* blends,
                         const Buffer& output, int h, int w, const Tensor& bounds);

namespace {

// One channel of an interleaved (H,W,C) byte image as an (H,W) plane.
Tensor channel_plane(Device& device, const Tensor& image, int channel) {
  const int h = image.dim(0), w = image.dim(1), c = image.dim(2);
  Tensor out = empty(device, DType::u8, {h, w});
  RepairChannelParams p{h * w, channel, c};
  device.launch("repair_channel_bytes", grid1(size_t(h) * w), kGroup1, {image.b(), out.b()}, p);
  return out;
}

int offset_rank(int offset) {
  const int order[4] = {-1, 1, -2, 2};
  for (int i = 0; i < 4; ++i)
    if (order[i] == offset) return i;
  throw Error("repair_float_frame: expected unique temporal offsets -2, -1, 1 or 2");
}

}  // namespace

FloatRepair repair_float_frame(Device& device, const Tensor& current_features,
                               const std::vector<Tensor>& reference_features, const std::vector<Tensor>& refined,
                               const std::vector<int>& reference_offsets, const FrameDetection& detection,
                               const Prepared& current, const std::vector<const Prepared*>& references,
                               const Controls& controls) {
  require(current_features.dtype == DType::f32 && current_features.shape.size() == 3 && current_features.dim(0) == 3,
          "repair_float_frame: expected three float32 feature planes");
  const int count = int(reference_offsets.size());
  require(count <= 4 && int(reference_features.size()) == count && int(refined.size()) == count &&
              int(references.size()) == count,
          "repair_float_frame: expected zero to four matching references");
  const int h = current_features.dim(1), w = current_features.dim(2);
  for (int k = 0; k < count; ++k) {
    offset_rank(reference_offsets[k]);
    for (int j = 0; j < k; ++j)
      require(reference_offsets[j] != reference_offsets[k], "repair_float_frame: duplicate temporal offset");
    require(reference_features[k].shape == current_features.shape && reference_features[k].dtype == DType::f32,
            "repair_float_frame: reference features must match current");
  }
  std::vector<int> combine_order(count), repair_order(count);
  for (int k = 0; k < count; ++k) combine_order[k] = repair_order[k] = k;
  std::stable_sort(combine_order.begin(), combine_order.end(),
                   [&](int a, int b) { return offset_rank(reference_offsets[a]) < offset_rank(reference_offsets[b]); });
  std::stable_sort(repair_order.begin(), repair_order.end(),
                   [&](int a, int b) { return reference_offsets[a] < reference_offsets[b]; });

  const std::pair<float, float> weights = controls.spread_weights(count);
  FloatRepair out;
  Tensor spread;
  if (controls.mode == 1) {
    Tensor spreads[3], masks[3];
    for (int c = 0; c < 3; ++c) {
      std::vector<Tensor> aligned;
      for (int k : combine_order) aligned.push_back(compensate_analysis(device, references[k]->rgb_planes, c, refined[k]));
      TemporalCombine combined =
          combine_bytes(device, channel_plane(device, current.rgb_planes, c), aligned, weights.first, weights.second);
      Tensor noise = select0(device, detection.noise.noise, c);
      Tensor mean = select0(device, detection.noise.trimmed_mean, c);
      masks[c] = build_repair_mask(device, combined.centered_residual, noise, count, controls);
      spreads[c] = condition_repair_spread(device, combined.temporal_spread, &mean, count);
    }
    spread = empty(device, DType::u8, {h, w});
    out.repair_mask = empty(device, DType::u8, {h, w});
    SizeParams p{h * w};
    device.launch("repair_rgb_masks", grid1(size_t(h) * w), kGroup1,
                  {spreads[0].b(), spreads[1].b(), spreads[2].b(), masks[0].b(), masks[1].b(), masks[2].b(),
                   spread.b(), out.repair_mask.b()},
                  p);
  } else {
    std::vector<Tensor> aligned;
    for (int k : combine_order) aligned.push_back(compensate_analysis(device, references[k]->planes, 0, refined[k]));
    TemporalCombine combined =
        combine_bytes(device, channel_plane(device, current.planes, 0), aligned, weights.first, weights.second);
    spread = condition_repair_spread(device, combined.temporal_spread, &detection.noise.trimmed_mean, count);
    out.repair_mask = build_repair_mask(device, combined.centered_residual, detection.noise.noise, count, controls);
  }
  out.preservation_mask = preservation_mask(device, spread, count, controls);

  RepairFrameParams p{h, w, count, 2, 2, 2, 2};
  int* blocks[4] = {&p.block0, &p.block1, &p.block2, &p.block3};
  const Buffer* refs[4];
  const Buffer* vecs[4];
  for (int i = 0; i < 4; ++i) {
    if (i < count) {
      const int k = repair_order[i];
      *blocks[i] = repair_vector_block(h, w, refined[k]);
      refs[i] = reference_features[k].b();
      vecs[i] = refined[k].b();
    } else {
      // Unused bindings: the kernel never reads them.
      refs[i] = current_features.b();
      vecs[i] = out.repair_mask.b();
    }
  }
  out.output = empty(device, DType::f32, {3, h, w});
  device.launch("repair_frame", grid2(w, h), kGroup2,
                {current_features.b(), refs[0], refs[1], refs[2], refs[3], vecs[0], vecs[1], vecs[2], vecs[3],
                 out.repair_mask.b(), out.preservation_mask.b(), out.output.b()},
                p);
  return out;
}

void finish_rgb(Device& device, const Tensor& repaired_features, const Buffer& original, int h, int w,
                const Controls& controls, const Buffer& output) {
  require(repaired_features.dtype == DType::f32 && repaired_features.shape.size() == 3 &&
              repaired_features.dim(0) == 3 && repaired_features.dim(1) == h && repaired_features.dim(2) == w,
          "finish_rgb: expected (3,H,W) float32 features");
  const Tensor bounds = repair_source_bounds(device, original, h, w);
  if (!controls.blend_repairs) {
    repair_finish_codes(device, repaired_features, original, nullptr, nullptr, nullptr, output, h, w, false, false,
                        &bounds, -1);
    return;
  }
  require(h >= 10 && w >= 10, "finish_rgb: post-repair requires dimensions of at least ten");
  Tensor table = controls.extended_range ? hdr_curve_device(device, false) : Tensor();
  Tensor codes[6], vblur, blends[3];
  // One channel at a time: two code planes live instead of six.
  for (int c = 0; c < 3; ++c) {
    repair_finish_codes(device, repaired_features, original, controls.extended_range ? &table : nullptr, codes, &vblur,
                        output, h, w, controls.extended_range, true, &bounds, c);
    blends[c] = contrast_blend(device, codes[c], codes[3 + c], controls);
    codes[c] = codes[3 + c] = Tensor();
  }
  repair_finish_blend(device, repaired_features, vblur, blends, output, h, w, bounds);
}

namespace {

// Debug-stage arguments of repair_float_frame.
// inputs: current features (3,H,W) f32, reference features (N,3,H,W) f32,
// refined vectors (N,BH,BW,2) i16, current analysis (H,W,3) u8 (planes, or
// rgb_planes in RGB mode), reference analysis (N,H,W,3) u8, detection noise,
// detection noise mean. scalars: 17 controls then N offsets.
struct FrameArgs {
  Controls controls;
  std::vector<int> offsets;
  std::vector<Tensor> features, vectors;
  std::vector<Prepared> prepared;
  std::vector<const Prepared*> pointers;
  Prepared current;
  FrameDetection detection;
  Tensor current_features;

  FloatRepair run(Device& device) const {
    return repair_float_frame(device, current_features, features, vectors, offsets, detection, current, pointers,
                              controls);
  }
};

FrameArgs frame_args(Device& device, const std::vector<Tensor>& in, const double* s, int scalar_count) {
  require(in.size() >= 7, "repair_float_frame: seven inputs");
  require(scalar_count >= 17, "repair_float_frame: 17 control scalars then offsets");
  FrameArgs a;
  a.controls = controls_from_scalars(s);
  const int count = scalar_count - 17;
  require(in[1].dim(0) == count && in[2].dim(0) == count && in[4].dim(0) == count,
          "repair_float_frame: one offset per reference");
  a.prepared.resize(count);
  for (int k = 0; k < count; ++k) {
    a.offsets.push_back(int(s[17 + k]));
    a.features.push_back(select0(device, in[1], k));
    a.vectors.push_back(select0(device, in[2], k));
    (a.controls.mode ? a.prepared[k].rgb_planes : a.prepared[k].planes) = select0(device, in[4], k);
  }
  for (auto& p : a.prepared) a.pointers.push_back(&p);
  (a.controls.mode ? a.current.rgb_planes : a.current.planes) = in[3];
  a.detection.offsets = a.offsets;
  a.detection.noise.noise = in[5];
  a.detection.noise.trimmed_mean = in[6];
  a.current_features = in[0];
  return a;
}

// outputs: output (3,H,W), repair mask, preservation mask.
std::vector<Tensor> debug_repair_float_frame(Device& device, const std::vector<Tensor>& in,
                                             const std::vector<double>& s) {
  require(in.size() == 7, "repair_float_frame: seven inputs");
  FloatRepair r = frame_args(device, in, s.data(), int(s.size())).run(device);
  return {r.output, r.repair_mask, r.preservation_mask};
}

// inputs: repaired features (3,H,W) bottom-up, original (H,W,3) top-down.
// scalars: 17 controls. output: (H,W,3) top-down.
std::vector<Tensor> debug_finish_rgb(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 2 && s.size() == 17, "repair_finish_rgb: features, original; 17 controls");
  const Controls controls = controls_from_scalars(s.data());
  const int h = in[0].dim(1), w = in[0].dim(2);
  require(in[1].dtype == DType::f32 && in[1].dim(0) == h && in[1].dim(1) == w && in[1].dim(2) == 3,
          "repair_finish_rgb: original (H,W,3) float32");
  Tensor out = empty(device, DType::f32, {h, w, 3});
  finish_rgb(device, in[0], in[1].buf, h, w, controls, out.buf);
  return {out};
}

// Timing: inputs as repair_float_frame plus the top-down source (H,W,3).
// scalars: iterations, 17 controls, offsets. output: float32 {3} mean ms for
// repair_float_frame, finish_rgb and both back to back (device synchronized).
std::vector<Tensor> debug_time(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 8 && s.size() >= 18, "repair_time_frame: eight inputs, iterations, controls, offsets");
  const int iterations = int(s[0]);
  const FrameArgs args = frame_args(device, in, s.data() + 1, int(s.size()) - 1);
  const int h = in[0].dim(1), w = in[0].dim(2);
  Tensor out = empty(device, DType::f32, {h, w, 3});
  Tensor repaired = args.run(device).output;
  finish_rgb(device, repaired, in[7].buf, h, w, args.controls, out.buf);
  device.synchronize();
  using clock = std::chrono::steady_clock;
  auto ms = [](clock::time_point a, clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  double t_repair = 0, t_finish = 0, t_both = 0;
  for (int i = 0; i < iterations; ++i) {
    auto a = clock::now();
    repaired = args.run(device).output;
    device.synchronize();
    auto b = clock::now();
    finish_rgb(device, repaired, in[7].buf, h, w, args.controls, out.buf);
    device.synchronize();
    auto c = clock::now();
    repaired = args.run(device).output;
    finish_rgb(device, repaired, in[7].buf, h, w, args.controls, out.buf);
    device.synchronize();
    auto d = clock::now();
    t_repair += ms(a, b);
    t_finish += ms(b, c);
    t_both += ms(c, d);
  }
  const float result[3] = {float(t_repair / iterations), float(t_finish / iterations), float(t_both / iterations)};
  return {upload(device, DType::f32, {3}, result)};
}

ADR_STAGE("repair_float_frame", debug_repair_float_frame);
ADR_STAGE("repair_finish_rgb", debug_finish_rgb);
ADR_STAGE("repair_time_frame", debug_time);

}  // namespace
}  // namespace adr
