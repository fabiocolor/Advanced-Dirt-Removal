// Group "refine": nine-level motion masks and dirt-aware motion refinement.
#include "stages/refine.h"

#include <algorithm>
#include <chrono>

#include "kernels/params_refine.h"
#include "rcpss.h"

namespace adr {

namespace {

constexpr int kRefineMaxGroups = 4096;  // grid-stride work-groups over active blocks

int mask_block(int level) { return level == 8 ? 2 : level == 7 ? 4 : 8; }
int ceil_div(int a, int b) { return (a + b - 1) / b; }

const Tensor& rcpss_bits(Device& device) {
  return *device.cached<Tensor>("refine_rcpss", [&] {
    return upload(device, DType::i32, {kRcpssCount}, rcpss_table().bits);
  });
}

Tensor classify(Device& device, const Tensor& source, int block, const Tensor* finer) {
  const int sh = source.dim(0), sw = source.dim(1), gh = ceil_div(sh, block), gw = ceil_div(sw, block);
  Tensor out = empty(device, DType::u8, {gh, gw});
  RefineClassifyParams p{sh, sw, gh, gw, block, finer ? finer->dim(0) : 0, finer ? finer->dim(1) : 0, finer ? 1 : 0};
  if (finer) require(ceil_div(p.fh, 2) == gh && ceil_div(p.fw, 2) == gw, "prepare_motion_masks: finer grid mismatch");
  device.launch("refine_mask_classify", grid2(gw, gh), kGroup2, {source.b(), finer ? finer->b() : source.b(), out.b()},
                p);
  return out;
}

// Active-block list of a class grid: list (int32, capacity gh*gw) and counter {}.
struct ActiveBlocks {
  Tensor list, counter;
};

ActiveBlocks compact(Device& device, const Tensor& classes) {
  const int n = int(classes.numel());
  ActiveBlocks a;
  a.list = empty(device, DType::i32, {std::max(n, 1)});
  a.counter = zeros(device, DType::i32, {});
  RefineCompactParams p{n};
  device.launch("refine_compact", grid1(size_t(n)), kGroup1, {classes.b(), a.counter.b(), a.list.b()}, p);
  return a;
}

std::array<Tensor, 2> refine_level(Device& device, const std::array<Tensor, 2>& initial,
                                   const std::array<Tensor, 2>* coarse, const Tensor& classes,
                                   const Tensor& block_confidence, const Tensor& pixel, const Tensor& current,
                                   const std::array<Tensor, 2>& references, int level, int threshold, int mode,
                                   std::array<bool, 2> valid, const ActiveBlocks& active) {
  require(level >= 0 && level <= 8 && (mode == 1 || mode == 2), "refine_motion_pair: unsupported level/mode");
  const int block = mask_block(level), units = 2 * std::max(1, level <= 6 ? 1 << (6 - level) : 1);
  const int h = current.dim(0), w = current.dim(1), gh = classes.dim(0), gw = classes.dim(1);
  require(current.dtype == DType::u8 && current.shape.size() == 3 && current.dim(2) == 4 &&
              ceil_div(h, block) == gh && ceil_div(w, block) == gw,
          "refine_motion_pair: expected packed byte images matching the class grid");
  require(pixel.dim(0) == h && pixel.dim(1) == w && block_confidence.shape == classes.shape,
          "refine_motion_pair: mask shape mismatch");
  for (int k = 0; k < 2; ++k) {
    require(initial[k].dtype == DType::i16 && initial[k].shape == std::vector<int64_t>{gh, gw, 2},
            "refine_motion_pair: vector shape mismatch");
    require(references[k].shape == current.shape, "refine_motion_pair: reference shape mismatch");
  }
  std::array<Tensor, 2> out;
  for (int k = 0; k < 2; ++k) {
    out[k] = empty(device, DType::i16, initial[k].shape);
    device.copy(out[k].buf, initial[k].buf, initial[k].bytes());
  }
  if (!valid[0] && !valid[1]) return out;
  const int ch = coarse ? (*coarse)[0].dim(0) : 1, cw = coarse ? (*coarse)[0].dim(1) : 1;
  const int groups = std::max(1, std::min(gh * gw, kRefineMaxGroups));
  RefineParams p{h,     w,    gh,        gw,   ch, cw, block, units, level, threshold, mode, coarse ? 1 : 0,
                 valid[0] ? 1 : 0, valid[1] ? 1 : 0, groups};
  const Tensor& c0 = coarse ? (*coarse)[0] : initial[0];
  const Tensor& c1 = coarse ? (*coarse)[1] : initial[1];
  // Work-group size: at least 64 (phase-1 task slices); measured best on M5 Pro.
  const int threads = block == 8 ? 128 : 64;
  device.launch("refine_blocks", Dim3{groups * threads, 1, 1}, Dim3{threads, 1, 1},
                {current.b(), references[0].b(), references[1].b(), pixel.b(), classes.b(), block_confidence.b(),
                 initial[0].b(), initial[1].b(), c0.b(), c1.b(), active.list.b(), active.counter.b(), out[0].b(),
                 out[1].b(), rcpss_bits(device).b()},
                p);
  return out;
}

}  // namespace

MotionMasks prepare_motion_masks(Device& device, const Tensor& mask, const Tensor& confidence) {
  require(mask.dtype == DType::u8 && confidence.dtype == DType::u8 && mask.shape.size() == 2 &&
              mask.shape == confidence.shape && std::min(mask.dim(0), mask.dim(1)) >= 64,
          "prepare_motion_masks: expected matching byte masks with minimum dimension 64");
  std::vector<Tensor> reduced_binary, reduced_confidence;
  Tensor a = mask, b = confidence;
  for (int level = 0; level < 6; ++level) {
    const int h = a.dim(0), w = a.dim(1), oh = (h + 1) / 2, ow = (w + 1) / 2;
    Tensor na = empty(device, DType::u8, {oh, ow}), nb = empty(device, DType::u8, {oh, ow});
    Tensor bin = empty(device, DType::u8, {oh, ow});
    RefineReduceParams p{h, w, oh, ow, (8 << level) & 255};
    device.launch("refine_mask_reduce", grid2(ow, oh), kGroup2, {a.b(), b.b(), na.b(), nb.b(), bin.b()}, p);
    reduced_binary.push_back(bin);
    reduced_confidence.push_back(nb);
    a = na;
    b = nb;
  }
  MotionMasks m;
  for (int level = 0; level < 9; ++level) {
    m.binary.push_back(level < 6 ? reduced_binary[5 - level] : mask);
    m.confidence.push_back(level < 6 ? reduced_confidence[5 - level] : confidence);
  }
  std::vector<Tensor> gated(9);
  gated[8] = classify(device, mask, 2, nullptr);
  for (int level = 7; level >= 0; --level) gated[level] = classify(device, m.binary[level], mask_block(level), &gated[level + 1]);
  m.classes.resize(9);
  m.block_confidence.resize(9);
  for (int level = 0; level < 9; ++level) {
    const int gh = gated[level].dim(0), gw = gated[level].dim(1);
    m.classes[level] = empty(device, DType::u8, {gh, gw});
    m.block_confidence[level] = empty(device, DType::u8, {gh, gw});
    const Tensor& parent = level ? gated[level - 1] : gated[level];
    RefineFinalizeParams p{m.confidence[level].dim(0), m.confidence[level].dim(1), gh, gw, mask_block(level),
                           parent.dim(0), parent.dim(1), level ? 1 : 0};
    device.launch("refine_mask_finalize", grid2(gw, gh), kGroup2,
                  {gated[level].b(), parent.b(), m.confidence[level].b(), m.classes[level].b(),
                   m.block_confidence[level].b()},
                  p);
  }
  return m;
}

std::array<Tensor, 2> refine_motion_pair(Device& device, const std::array<Tensor, 2>& initial,
                                         const std::array<Tensor, 2>* coarse, const Tensor& classes,
                                         const Tensor& block_confidence, const Tensor& pixel_confidence,
                                         const Tensor& current, const std::array<Tensor, 2>& references, int level,
                                         int threshold, int mode, std::array<bool, 2> valid) {
  return refine_level(device, initial, coarse, classes, block_confidence, pixel_confidence, current, references, level,
                      threshold, mode, valid, compact(device, classes));
}

std::map<int, Tensor> refine_frame_motion(Device& device, const MotionMasks& masks,
                                          const std::map<int, std::vector<Tensor>>& motion_levels,
                                          const std::vector<int>& reference_offsets,
                                          const std::function<const Prepared&(int)>& prepared, int width) {
  const int first_level = width < 800 ? 3 : width < 1350 ? 2 : width < 3000 ? 1 : 0;
  const int finest_level = first_level == 0 ? 7 : 8;
  auto enabled = [&](int k) {
    return std::find(reference_offsets.begin(), reference_offsets.end(), k) != reference_offsets.end();
  };
  std::vector<ActiveBlocks> active(9);
  std::map<int, Tensor> refined;
  const Prepared& self = prepared(0);
  for (int distance = 1; distance <= 2; ++distance) {
    const int offsets[2] = {-distance, distance};
    const std::array<bool, 2> valid{enabled(offsets[0]), enabled(offsets[1])};
    if (!valid[0] && !valid[1]) continue;
    std::array<Tensor, 2> previous;
    bool has_previous = false;
    for (int level = first_level, index = 0; level <= finest_level; ++level, ++index) {
      const Tensor& classes = masks.classes[level];
      std::array<Tensor, 2> initial, references;
      const int image_level = std::max(0, 6 - level);
      for (int k = 0; k < 2; ++k) {
        if (valid[k]) {
          const auto& levels = motion_levels.at(offsets[k]);
          require(index < int(levels.size()), "refine_frame_motion: missing motion level");
          initial[k] = levels[index];
          references[k] = prepared(offsets[k]).packed.at(image_level);
        } else {
          initial[k] = zeros(device, DType::i16, {classes.dim(0), classes.dim(1), 2});
          references[k] = self.packed.at(image_level);
        }
      }
      if (!active[level].list.buf) active[level] = compact(device, classes);
      const int threshold = level <= 6 ? std::max(175, 162 + 15 * level) : level == 7 ? 250 : 255;
      previous = refine_level(device, initial, has_previous ? &previous : nullptr, classes,
                              masks.block_confidence[level], masks.confidence[level], self.packed.at(image_level),
                              references, level, threshold, level <= 6 ? 1 : 2, valid, active[level]);
      has_previous = true;
    }
    for (int k = 0; k < 2; ++k)
      if (valid[k]) refined[offsets[k]] = previous[k];
  }
  return refined;
}

namespace {

// inputs: mask, confidence. outputs: binary[9], confidence[9], classes[9], block_confidence[9].
std::vector<Tensor> debug_masks(Device& device, const std::vector<Tensor>& in, const std::vector<double>&) {
  require(in.size() == 2, "refine_motion_masks: mask, confidence");
  MotionMasks m = prepare_motion_masks(device, in[0], in[1]);
  std::vector<Tensor> out;
  for (auto* v : {&m.binary, &m.confidence, &m.classes, &m.block_confidence})
    out.insert(out.end(), v->begin(), v->end());
  return out;
}

// inputs: initial0, initial1, coarse0, coarse1 (empty: none), classes, block_confidence,
// pixel, current, reference0, reference1. scalars: level, threshold, mode, valid0, valid1.
std::vector<Tensor> debug_pair(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 10 && s.size() == 5, "refine_motion_pair: 10 inputs, 5 scalars");
  const bool has_coarse = in[2].numel() != 0;
  std::array<Tensor, 2> coarse{in[2], in[3]};
  auto out = refine_motion_pair(device, {in[0], in[1]}, has_coarse ? &coarse : nullptr, in[4], in[5], in[6], in[7],
                                {in[8], in[9]}, int(s[0]), int(s[1]), int(s[2]), {s[3] != 0, s[4] != 0});
  return {out[0], out[1]};
}

// inputs: mask, confidence, then packed pyramids of frames -2..2 (L levels each,
// empty tensors for absent frames), then motion levels for each reference
// offset in order. scalars: width, L, motion level count, repeat, offsets...
// outputs: refined vectors per reference offset, then float32 {2} timing
// (ms per refine_frame_motion, ms per masks + refinement) when repeat > 0.
std::vector<Tensor> debug_frame(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(s.size() >= 4, "refine_frame_motion: scalars");
  const int width = int(s[0]), levels = int(s[1]), motion_count = int(s[2]), repeat = int(s[3]);
  std::vector<int> offsets;
  for (size_t i = 4; i < s.size(); ++i) offsets.push_back(int(s[i]));
  require(in.size() == 2 + 5 * size_t(levels) + offsets.size() * size_t(motion_count), "refine_frame_motion: inputs");
  std::vector<Prepared> frames(5);
  for (int f = 0; f < 5; ++f)
    for (int l = 0; l < levels; ++l) frames[f].packed.push_back(in[2 + f * levels + l]);
  std::map<int, std::vector<Tensor>> motion;
  size_t at = 2 + 5 * size_t(levels);
  for (int k : offsets)
    for (int i = 0; i < motion_count; ++i) motion[k].push_back(in[at++]);
  auto get = [&](int k) -> const Prepared& { return frames.at(size_t(k + 2)); };
  auto run = [&](const MotionMasks& masks) { return refine_frame_motion(device, masks, motion, offsets, get, width); };
  MotionMasks masks = prepare_motion_masks(device, in[0], in[1]);
  auto result = run(masks);
  std::vector<Tensor> out;
  for (int k : offsets) out.push_back(result.at(k));
  if (repeat > 0) {
    using clock = std::chrono::steady_clock;
    device.synchronize();
    auto t0 = clock::now();
    for (int r = 0; r < repeat; ++r) run(masks);
    device.synchronize();
    auto t1 = clock::now();
    for (int r = 0; r < repeat; ++r) run(prepare_motion_masks(device, in[0], in[1]));
    device.synchronize();
    auto t2 = clock::now();
    float ms[2] = {float(std::chrono::duration<double, std::milli>(t1 - t0).count() / repeat),
                   float(std::chrono::duration<double, std::milli>(t2 - t1).count() / repeat)};
    out.push_back(upload(device, DType::f32, {2}, ms));
  }
  return out;
}

ADR_STAGE("refine_motion_masks", debug_masks);
ADR_STAGE("refine_motion_pair", debug_pair);
ADR_STAGE("refine_frame_motion", debug_frame);

}  // namespace
}  // namespace adr
