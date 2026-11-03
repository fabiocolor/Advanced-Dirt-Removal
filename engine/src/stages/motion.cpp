// Group "motion": multilevel pair motion estimation.
#include "stages/motion.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "kernels/params_motion.h"

namespace adr {
namespace {

constexpr int kGroupThreads = 256;

// Development profiling: when set, estimate_pair synchronizes after each step
// and records wall time per step label (debug stage motion_profile only).
std::vector<std::pair<std::string, double>>* g_profile = nullptr;
std::chrono::steady_clock::time_point g_profile_start;

void profile_step(Device& device, const std::string& label) {
  if (!g_profile) return;
  device.synchronize();
  const auto now = std::chrono::steady_clock::now();
  g_profile->push_back({label, std::chrono::duration<double, std::milli>(now - g_profile_start).count()});
  g_profile_start = now;
}

int grid_size(int n, int block) { return (n + block - 1) / block; }

// motion_penalty_table(width, height, repair_amount, falloff). Host float32 with
// correctly rounded sqrt; for every default configuration the values sit at
// least 1e-3 away from an integer truncation boundary.
std::vector<int> penalty_host(int width, int height, float repair_amount, double falloff) {
  std::vector<int> table(size_t(width) * height);
  const float scale = float(falloff / double(width / 2));
  for (int i = 0; i < height; ++i) {
    const float y = float(i - height / 2);
    for (int j = 0; j < width; ++j) {
      const float x = float(j - width / 2);
      const float yy = y * y, xx = x * x;
      const float radius = std::sqrt(yy + xx) * scale;
      const float r2 = radius * radius;
      const float curve = 2.0f / (std::exp2(-r2) + 1.0f) - 1.0f;
      const float product = curve * repair_amount;
      const int v = int(product + 0.5f);
      table[size_t(i) * width + j] = v < 1 ? 1 : v;
    }
  }
  table[size_t(height / 2) * width + width / 2] = 0;
  return table;
}

Tensor penalty_table(Device& device, int width, int height, float repair_amount, double falloff = 2.5) {
  uint32_t bits;
  std::memcpy(&bits, &repair_amount, 4);
  char key[96];
  std::snprintf(key, sizeof key, "motion_penalty_%dx%d_%08x_%.17g", width, height, bits, falloff);
  return *device.cached<Tensor>(key, [&] {
    std::vector<int> host = penalty_host(width, height, repair_amount, falloff);
    return upload(device, DType::i32, {height, width}, host.data());
  });
}

struct Search {
  Tensor vectors, error, texture;
};

// Strip geometry shared by the register-blocked searches.
void strip_layout(MotionSearchParams& p, int strip) {
  p.nsx = (p.pw + strip - 1) / strip;
  p.tpb = p.nsx * p.ph;
  require(p.tpb <= kGroupThreads, "motion: search window too large");
  p.bpg = kGroupThreads / p.tpb;
}

MotionSearchParams search_params(const Tensor& current, const Tensor& penalty, int block) {
  MotionSearchParams p{};
  p.h = current.dim(0), p.w = current.dim(1), p.block = block;
  p.gh = (p.h + block - 1) / block, p.gw = (p.w + block - 1) / block;
  p.ph = penalty.dim(0), p.pw = penalty.dim(1);
  return p;
}

// Per-block clamped texture of a packed level (shared by search, regularize and retest).
Tensor packed_texture(Device& device, const Tensor& current) {
  MotionSearchParams p{};
  p.h = current.dim(0), p.w = current.dim(1), p.block = 8, p.gh = (p.h + 7) / 8, p.gw = (p.w + 7) / 8;
  Tensor out = empty(device, DType::u8, {p.gh, p.gw});
  device.launch("motion_packed_texture", grid1(size_t(p.gh) * p.gw), Dim3{64, 1, 1}, {current.b(), out.b()}, p);
  return out;
}

Tensor byte_texture(Device& device, const Tensor& current, int block) {
  MotionSearchParams p{};
  p.h = current.dim(0), p.w = current.dim(1), p.block = block;
  p.gh = (p.h + block - 1) / block, p.gw = (p.w + block - 1) / block;
  Tensor out = empty(device, DType::u8, {p.gh, p.gw});
  device.launch("motion_byte_texture", grid1(size_t(p.gh) * p.gw), Dim3{64, 1, 1}, {current.b(), out.b()}, p);
  return out;
}

// coarse_motion_search. gate: blocks whose gate is zero keep the prior. list:
// only blocks list[0..count) are searched and written into *into.
Search packed_search(Device& device, const Tensor& current, const Tensor& reference, const Tensor& prior,
                     const Tensor& penalty, const Tensor& texture, const Tensor* gate, int scale, int blend_amount,
                     const Tensor* list = nullptr, const Tensor* count = nullptr, Tensor* into = nullptr) {
  MotionSearchParams p = search_params(current, penalty, 8);
  require(p.h >= 8 && p.w >= 8, "motion: packed search requires complete 8x8 blocks");
  require(prior.dim(0) == p.gh && prior.dim(1) == p.gw, "motion: prior does not match the 8x8 block grid");
  require(p.ph / 2 <= p.h - 8 && p.pw / 2 <= p.w - 8, "motion: search window too large for the analysis image");
  require(p.ph * p.pw <= 1024, "motion: search window too large");
  p.scale = scale, p.blend_amount = blend_amount, p.gated = list ? 2 : gate ? 1 : 0, p.threshold = -1;
  // Strip width 4 (motion_packed_search_4); 5 for 13/25-wide windows measured
  // equal overall (Metal slightly faster, OpenCL slightly slower).
  const int strip = 4;
  strip_layout(p, strip);
  Search out;
  out.vectors = into ? *into : empty(device, DType::i16, {p.gh, p.gw, 2});
  out.error = empty(device, DType::u8, {p.gh, p.gw});
  out.texture = texture;
  device.launch("motion_packed_search_" + std::to_string(strip), grid1(size_t(grid_size(p.gh * p.gw, p.bpg)) * p.tpb * p.bpg),
          Dim3{p.tpb * p.bpg, 1, 1},
          {current.b(), reference.b(), prior.b(), penalty.b(), gate ? gate->b() : prior.b(), texture.b(),
           list ? list->b() : prior.b(), count ? count->b() : prior.b(), out.vectors.b(), out.error.b()},
          p);
  return out;
}

Search packed_search(Device& device, const Tensor& current, const Tensor& reference, const Tensor& prior,
                     const Tensor& penalty, const Tensor* gate, int scale, int blend_amount) {
  return packed_search(device, current, reference, prior, penalty, packed_texture(device, current), gate, scale,
                       blend_amount);
}

// search_byte_motion. With gate/error_before, blocks whose gate does not exceed
// thresholds[threshold_index] keep the prior vectors and error_before.
Search byte_search(Device& device, const Tensor& current, const Tensor& reference, const Tensor& prior,
                   const Tensor& penalty, int block, int blend_amount, const Tensor* error_before, const Tensor* gate,
                   const Tensor* thresholds, int threshold_index, bool want_texture = true) {
  MotionSearchParams p = search_params(current, penalty, block);
  require(block == 4 || block == 8 || block == 16, "motion: byte search block must be 4, 8 or 16");
  require(p.h >= block && p.w >= block, "motion: byte search requires complete blocks");
  require(prior.dim(0) == p.gh && prior.dim(1) == p.gw, "motion: prior does not match the byte block grid");
  require(p.ph / 2 <= p.h - block && p.pw / 2 <= p.w - block, "motion: search window too large for the analysis image");
  require(p.ph * p.pw <= 1024, "motion: search window too large");
  require(!error_before == !gate, "motion: gated byte search needs both the gate and prior errors");
  p.scale = 1, p.blend_amount = blend_amount, p.gated = gate ? 1 : 0, p.threshold = gate ? threshold_index : -1;
  const int strip = p.pw <= 3 ? 3 : p.pw <= 5 ? 5 : 9;
  strip_layout(p, strip);
  Search out;
  out.vectors = empty(device, DType::i16, {p.gh, p.gw, 2});
  out.error = empty(device, DType::u8, {p.gh, p.gw});
  // Only the 16-pixel search uses texture (penalty factor); the others compute it on request.
  const bool texture_needed = block == 16 || want_texture;
  out.texture = texture_needed ? byte_texture(device, current, block) : empty(device, DType::u8, {p.gh, p.gw});
  const std::string kernel = "motion_byte_search_" + std::to_string(block) + "_" + std::to_string(strip);
  device.launch(kernel, grid1(size_t(grid_size(p.gh * p.gw, p.bpg)) * p.tpb * p.bpg), Dim3{p.tpb * p.bpg, 1, 1},
                {current.b(), reference.b(), prior.b(), penalty.b(), error_before ? error_before->b() : prior.b(),
                 gate ? gate->b() : prior.b(), thresholds ? thresholds->b() : penalty.b(), out.texture.b(),
                 out.vectors.b(), out.error.b()},
                p);
  return out;
}

// Threads per block, tuned on Apple M5 Pro (HD): small segments let many
// blocks share a work-group, which matters more than intra-block parallelism.
int upsample_tpb(int block, int channels) { return channels == 4 ? 4 : block == 8 ? 2 : 1; }

// upsample_motion_candidates (channels 4) or upsample_byte_motion (channels 1).
// texture: packed only; also returns the level's block texture (fused kernel).
Search upsample(Device& device, const Tensor& current, const Tensor& reference, const Tensor& coarse, int block,
                int channels, int units, int agreement, int early, Tensor* texture = nullptr) {
  const int h = current.dim(0), w = current.dim(1), gh = (h + block - 1) / block, gw = (w + block - 1) / block;
  require(h >= block && w >= block, "motion: upsampling requires complete blocks");
  require(coarse.dim(0) == (h + 2 * block - 1) / (2 * block) && coarse.dim(1) == (w + 2 * block - 1) / (2 * block),
          "motion: coarse vectors must be half the block grid");
  MotionUpsampleParams p{};
  p.h = h, p.w = w, p.gh = gh, p.gw = gw, p.ch = coarse.dim(0), p.cw = coarse.dim(1);
  p.block = block, p.channels = channels, p.units = units, p.agreement = agreement, p.early = early;
  p.tpb = upsample_tpb(block, channels);
  p.bpg = kGroupThreads / p.tpb;
  Search out;
  out.vectors = empty(device, DType::i16, {gh, gw, 2});
  out.error = empty(device, DType::u8, {gh, gw});
  if (texture) {
    require(channels == 4 && block == 8, "motion: fused texture is for packed upsampling");
    *texture = empty(device, DType::u8, {gh, gw});
    device.launch("motion_upsample_packed", grid1(size_t(grid_size(gh * gw, 32)) * kGroupThreads), kGroup1,
            {current.b(), reference.b(), coarse.b(), out.vectors.b(), out.error.b(), texture->b()}, p);
    return out;
  }
  require(channels == 1, "motion: packed upsampling requires the fused texture output");
  if ((block == 8 || block == 4) && w % 4 == 0) {
    device.launch(block == 8 ? "motion_upsample_byte_8" : "motion_upsample_byte_4", grid1(size_t(gh) * gw),
                  Dim3{64, 1, 1}, {current.b(), reference.b(), coarse.b(), out.vectors.b(), out.error.b()}, p);
    return out;
  }
  device.launch("motion_upsample", grid1(size_t(grid_size(gh * gw, p.bpg)) * kGroupThreads), kGroup1,
                {current.b(), reference.b(), coarse.b(), out.vectors.b(), out.error.b()}, p);
  return out;
}

// global_motion_mode; with rows/count also the regularization row candidates
// and the retest counter reset.
Tensor global_mode(Device& device, const Tensor& vectors, int qx, int qy, Tensor* rows = nullptr,
                   Tensor* count = nullptr) {
  Tensor out = empty(device, DType::i16, {2});
  MotionGlobalParams p{int(vectors.numel() / 2), qx, qy, 0, 0};
  require(int64_t(p.n) * 5 < (int64_t(1) << 31), "motion: global mode field too large");
  if (rows) {
    p.gh = vectors.dim(0), p.gw = vectors.dim(1);
    *rows = empty(device, DType::i16, {p.gh, 2});
    *count = empty(device, DType::i32, {1});
  }
  device.launch("motion_global_mode", grid1(kGroupThreads), kGroup1,
          {vectors.b(), out.b(), rows ? rows->b() : out.b(), count ? count->b() : out.b()}, p);
  return out;
}

struct Regularized {
  Tensor vectors, retested, selected;
};

// regularize_motion_candidates with dilate_motion_error(error) as the envelope.
// rows/count come from global_mode(prior, ..., &rows, &count).
Regularized regularize(Device& device, const Tensor& current, const Tensor& reference, const Tensor& prior,
                       const Tensor& block_error, const Tensor& global_vector, const Tensor& rows,
                       const Tensor& count, const Tensor& penalty, const Tensor& texture, int scale, float repair_amount,
                       int blend_amount) {
  const int h = current.dim(0), w = current.dim(1), gh = (h + 7) / 8, gw = (w + 7) / 8;
  require(prior.dim(0) == gh && prior.dim(1) == gw, "motion: regularization grid mismatch");
  Regularized out;
  out.selected = empty(device, DType::i16, {gh, gw, 2});
  out.retested = empty(device, DType::u8, {gh, gw});
  out.vectors = empty(device, DType::i16, {gh, gw, 2});
  Tensor list = empty(device, DType::i32, {gh * gw});
  MotionRegularizeParams p{h, w, gh, gw, scale, 40, 20, 2, penalty.dim(0) + penalty.dim(1), repair_amount, 2, 128};
  p.bpg = kGroupThreads / p.tpb;
  require(p.bpg <= 128 && 64 % p.tpb == 0, "motion: regularize segment size");
  device.launch("motion_regularize", grid1(size_t(grid_size(gh * gw, p.bpg)) * kGroupThreads), kGroup1,
          {current.b(), reference.b(), prior.b(), block_error.b(), global_vector.b(), rows.b(), texture.b(),
           out.selected.b(), out.retested.b(), out.vectors.b(), list.b(), count.b()},
          p);
  profile_step(device, "  regularize kernel");
  // Retest search of the listed blocks, written in place into out.vectors.
  packed_search(device, current, reference, out.selected, penalty, texture, nullptr, scale, blend_amount, &list, &count,
                &out.vectors);
  return out;
}

Tensor filter_error(Device& device, const Tensor& error) {
  Tensor out = empty(device, DType::u8, error.shape);
  MotionGridParams p{error.dim(0), error.dim(1)};
  device.launch("motion_filter_error", grid2(p.gw, p.gh), kGroup2, {error.b(), out.b()}, p);
  return out;
}

// motion_error_mean / supplied noise, then adaptive_motion_thresholds.
void noise_thresholds(Device& device, const Tensor& error, const Tensor* noise_in, Tensor& noise, Tensor& thresholds) {
  noise = empty(device, DType::f32, {});
  thresholds = empty(device, DType::i32, {2});
  MotionNoiseParams p{int(error.numel()), noise_in ? 1 : 0};
  device.launch("motion_noise", grid1(kGroupThreads), kGroup1,
                {error.b(), noise_in ? noise_in->b() : noise.b(), noise.b(), thresholds.b()}, p);
}

struct Config {
  std::vector<std::pair<int, int>> windows;
  std::vector<float> penalties, regularization;
  std::vector<std::pair<int, int>> byte_windows;
  std::vector<float> byte_penalties;
};

Config default_config(int levels) {
  Config c;
  switch (levels) {
    case 4: c.penalties = {154, 82, 10, 10}; break;
    case 5: c.penalties = {154, 106, 58, 10, 10}; break;
    case 6: c.penalties = {154, 118, 82, 46, 10, 10}; break;
    default:
      c.penalties = {154.0f, float(127.19999694824219), float(100.4000015258789), float(73.5999984741211),
                     float(46.80000305175781), 20.0f, 20.0f};
  }
  c.windows.push_back({25, 17});
  for (int i = 0; i < levels - (levels == 7 ? 3 : 2); ++i) c.windows.push_back({13, 9});
  for (int i = 0; i < (levels == 7 ? 2 : 1); ++i) c.windows.push_back({7, 5});
  c.regularization.assign(levels, 8.0f);
  c.regularization[0] = 4.0f;
  if (levels == 7) {
    c.byte_windows = {{3, 3}, {5, 5}};
    c.byte_penalties = {16, 40};
  } else {
    c.byte_windows = {{3, 3}, {9, 9}, {5, 5}};
    c.byte_penalties = {16, 20, 10};
  }
  return c;
}

}  // namespace

PairMotion estimate_pair(Device& device, const Prepared& current, const Prepared& reference, const Tensor& seed,
                         const Tensor* motion_noise) {
  const int levels = int(current.packed.size());
  require(levels >= 4 && levels <= 7 && int(reference.packed.size()) == levels,
          "motion: the pair search requires four to seven pyramid levels");
  require(current.half_luma.shape == reference.half_luma.shape, "motion: analysis dimensions must agree");
  require(seed.dtype == DType::i16, "motion: seed must be int16");
  const Config config = default_config(levels);
  PairMotion out;
  Tensor flow = seed;
  for (int index = 0; index < levels; ++index) {
    const int level = levels - 1 - index;
    const Tensor& a = current.packed[level];
    const Tensor& b = reference.packed[level];
    require(a.shape == b.shape, "motion: analysis dimensions must agree");
    const int scale = 1 << level;
    const std::string tag = "L" + std::to_string(level) + " ";
    profile_step(device, "start");
    Tensor texture;
    if (index) flow = upsample(device, a, b, flow, 8, 4, 2 * scale, 16, 40, &texture).vectors;
    else texture = packed_texture(device, a);
    profile_step(device, tag + "upsample");
    Tensor table = penalty_table(device, config.windows[index].first, config.windows[index].second,
                                 config.penalties[index]);
    Search search = packed_search(device, a, b, flow, table, texture, nullptr, scale, 4);
    profile_step(device, tag + "search");
    Tensor rows, count;
    Tensor global_vector = global_mode(device, search.vectors, 2 * scale, 2 * scale, &rows, &count);
    profile_step(device, tag + "global");
    Regularized regular = regularize(device, a, b, search.vectors, search.error, global_vector, rows, count, table,
                                     texture, scale, config.regularization[index], 4);
    profile_step(device, tag + "regularize+retest");
    if (index == 2) out.history = regular.vectors;
    if (index == 3 && levels != 4) out.scene_error = search.error;
    if (index < levels - 1) out.levels.push_back(regular.vectors);
    flow = regular.vectors;
  }
  const Tensor& a = current.half_luma;
  const Tensor& b = reference.half_luma;
  const int byte_count = levels == 7 ? 2 : 3;
  const int blocks[3] = {16, 8, 4};
  Tensor thresholds;
  for (int index = 0; index < byte_count; ++index) {
    const int block = blocks[index];
    Tensor table = penalty_table(device, config.byte_windows[index].first, config.byte_windows[index].second,
                                 config.byte_penalties[index]);
    Search search;
    if (index == 0) {
      search = byte_search(device, a, b, flow, table, block, 4, nullptr, nullptr, nullptr, -1, false);
      profile_step(device, "B16 search");
      noise_thresholds(device, search.error, motion_noise, out.motion_noise, thresholds);
      profile_step(device, "noise");
      if (levels == 4) out.scene_error = search.error;
    } else {
      const std::string tag = "B" + std::to_string(block) + " ";
      Search up = upsample(device, a, b, flow, block, 1, 1, 8, 52);
      profile_step(device, tag + "upsample");
      Tensor gate = filter_error(device, up.error);
      search = byte_search(device, a, b, up.vectors, table, block, 4, &up.error, &gate, &thresholds, index - 1, false);
      profile_step(device, tag + "search");
    }
    out.levels.push_back(search.vectors);
    flow = search.vectors;
  }
  out.vectors = flow;
  return out;
}

Tensor reduce_motion_vectors(Device& device, const Tensor& source) {
  require(source.shape.size() == 3 && source.dim(2) == 2 && source.dtype == DType::i16 && source.numel(),
          "reduce_motion_vectors: expected a nonempty HxWx2 int16 field");
  MotionReduceParams p{source.dim(0), source.dim(1), (source.dim(0) + 3) / 4, (source.dim(1) + 3) / 4};
  Tensor out = empty(device, DType::i16, {p.oh, p.ow, 2});
  device.launch("motion_reduce_vectors", grid2(p.ow, p.oh), kGroup2, {source.b(), out.b()}, p);
  return out;
}

Tensor error_mean(Device& device, const Tensor& error) {
  require(error.dtype == DType::u8 && error.numel(), "error_mean: expected a nonempty byte plane");
  Tensor out = empty(device, DType::f32, {});
  SizeParams p{int(error.numel())};
  device.launch("motion_error_mean", grid1(kGroupThreads), kGroup1, {error.b(), out.b()}, p);
  return out;
}

namespace {

// ---- Development stages ----

const Tensor* optional(const std::vector<Tensor>& in, size_t i) {
  return i < in.size() && in[i].numel() ? &in[i] : nullptr;
}

// in: current, reference (H,W,4) u8, prior i16, penalty i32, [gate u8]. s: scale, blend_amount.
std::vector<Tensor> debug_coarse(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  Search r = packed_search(device, in.at(0), in.at(1), in.at(2), in.at(3), optional(in, 4), int(s.at(0)), int(s.at(1)));
  return {r.vectors, r.error, r.texture};
}

// in: current, reference, coarse. s: block, channels, units, agreement, early.
std::vector<Tensor> debug_upsample(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  Tensor texture;
  const bool packed = int(s.at(1)) == 4 && int(s.at(0)) == 8;
  Search r = upsample(device, in.at(0), in.at(1), in.at(2), int(s.at(0)), int(s.at(1)), int(s.at(2)), int(s.at(3)),
                      int(s.at(4)), packed ? &texture : nullptr);
  if (packed) return {r.vectors, r.error, texture};
  return {r.vectors, r.error};
}

std::vector<Tensor> debug_global(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  return {global_mode(device, in.at(0), int(s.at(0)), int(s.at(1)))};
}

// in: current, reference, prior, raw block error, global, penalty. s: scale, repair_amount, blend_amount.
std::vector<Tensor> debug_regularize(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  Tensor rows, count;
  global_mode(device, in.at(2), 2, 2, &rows, &count);
  Regularized r = regularize(device, in.at(0), in.at(1), in.at(2), in.at(3), in.at(4), rows, count, in.at(5),
                             packed_texture(device, in.at(0)), int(s.at(0)), float(s.at(1)), int(s.at(2)));
  return {r.vectors, r.retested, r.selected};
}

std::vector<Tensor> debug_penalty(Device& device, const std::vector<Tensor>&, const std::vector<double>& s) {
  return {penalty_table(device, int(s.at(0)), int(s.at(1)), float(s.at(2)), s.at(3))};
}

// in: current, reference, prior, penalty, [error_before, gate, thresholds i32[2]]. s: block, blend_amount, index.
std::vector<Tensor> debug_byte(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  const Tensor* gate = optional(in, 5);
  Search r = byte_search(device, in.at(0), in.at(1), in.at(2), in.at(3), int(s.at(0)), int(s.at(1)), optional(in, 4),
                         gate, optional(in, 6), gate ? int(s.at(2)) : -1);
  return {r.vectors, r.error, r.texture};
}

std::vector<Tensor> debug_filter(Device& device, const std::vector<Tensor>& in, const std::vector<double>&) {
  return {filter_error(device, in.at(0))};
}

// in: error u8, [noise f32 {}]. out: noise, thresholds (2,) i32.
std::vector<Tensor> debug_noise(Device& device, const std::vector<Tensor>& in, const std::vector<double>&) {
  Tensor noise, thresholds;
  noise_thresholds(device, in.at(0), optional(in, 1), noise, thresholds);
  return {noise, thresholds};
}

std::vector<Tensor> debug_reduce(Device& device, const std::vector<Tensor>& in, const std::vector<double>&) {
  return {reduce_motion_vectors(device, in.at(0))};
}

std::vector<Tensor> debug_error_mean(Device& device, const std::vector<Tensor>& in, const std::vector<double>&) {
  return {error_mean(device, in.at(0))};
}

// in: current half luma, reference half luma, seed, noise (empty for none),
// current packed levels..., reference packed levels... s: levels.
void pair_inputs(const std::vector<Tensor>& in, const std::vector<double>& s, Prepared& cur, Prepared& ref,
                 const Tensor*& noise) {
  const int levels = int(s.at(0));
  require(int(in.size()) == 4 + 2 * levels, "motion_estimate_pair: wrong number of inputs");
  cur.half_luma = in[0];
  ref.half_luma = in[1];
  noise = optional(in, 3);
  for (int i = 0; i < levels; ++i) {
    cur.packed.push_back(in[4 + i]);
    ref.packed.push_back(in[4 + levels + i]);
  }
}

// out: vectors, history, scene_error, motion_noise, levels...
std::vector<Tensor> debug_pair(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  Prepared cur, ref;
  const Tensor* noise;
  pair_inputs(in, s, cur, ref, noise);
  PairMotion m = estimate_pair(device, cur, ref, in[2], noise);
  std::vector<Tensor> out{m.vectors, m.history, m.scene_error, m.motion_noise};
  for (auto& t : m.levels) out.push_back(t);
  return out;
}

// Same inputs plus s[1] = iterations. Returns per-iteration milliseconds of
// queued GPU work (inputs already resident; each iteration synchronizes).
std::vector<Tensor> debug_pair_time(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  Prepared cur, ref;
  const Tensor* noise;
  pair_inputs(in, s, cur, ref, noise);
  const int iterations = int(s.at(1));
  device.synchronize();
  std::vector<float> times;
  for (int i = 0; i < iterations; ++i) {
    const auto start = std::chrono::steady_clock::now();
    PairMotion m = estimate_pair(device, cur, ref, in[2], noise);
    device.synchronize();
    const auto end = std::chrono::steady_clock::now();
    times.push_back(float(std::chrono::duration<double, std::milli>(end - start).count()));
  }
  return {upload(device, DType::f32, {int64_t(times.size())}, times.data())};
}

// Same inputs as motion_estimate_pair plus s[1] = iterations. Prints the
// fastest per-step time over the iterations to stderr; returns the step times
// of the fastest iteration (ms).
std::vector<Tensor> debug_profile(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  Prepared cur, ref;
  const Tensor* noise;
  pair_inputs(in, s, cur, ref, noise);
  std::vector<std::pair<std::string, double>> best;
  for (int i = 0; i < int(s.at(1)); ++i) {
    std::vector<std::pair<std::string, double>> steps;
    device.synchronize();
    g_profile = &steps;
    g_profile_start = std::chrono::steady_clock::now();
    estimate_pair(device, cur, ref, in[2], noise);
    g_profile = nullptr;
    if (best.empty()) best = steps;
    for (size_t k = 0; k < steps.size() && k < best.size(); ++k) best[k].second = std::min(best[k].second, steps[k].second);
  }
  std::vector<float> times;
  for (auto& step : best) {
    if (step.first != "start") std::fprintf(stderr, "%-24s %8.3f ms\n", step.first.c_str(), step.second);
    times.push_back(float(step.second));
  }
  return {upload(device, DType::f32, {int64_t(times.size())}, times.data())};
}

ADR_STAGE("motion_profile", debug_profile);
ADR_STAGE("motion_coarse_search", debug_coarse);
ADR_STAGE("motion_upsample", debug_upsample);
ADR_STAGE("motion_global_mode", debug_global);
ADR_STAGE("motion_regularize", debug_regularize);
ADR_STAGE("motion_penalty_table", debug_penalty);
ADR_STAGE("motion_search_byte", debug_byte);
ADR_STAGE("motion_filter_error", debug_filter);
ADR_STAGE("motion_noise", debug_noise);
ADR_STAGE("motion_reduce_vectors", debug_reduce);
ADR_STAGE("motion_error_mean", debug_error_mean);
ADR_STAGE("motion_estimate_pair", debug_pair);
ADR_STAGE("motion_estimate_pair_time", debug_pair_time);

}  // namespace
}  // namespace adr
