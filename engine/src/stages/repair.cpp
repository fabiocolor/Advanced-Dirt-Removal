// Float repair primitives: compensation, reconstruction, blends, RGB conversion,
// float Gaussian and the finish_rgb passes.
#include <array>
#include <cstring>

#include "kernels/params_repair.h"
#include "stages/repair.h"

namespace adr {

// Five-tap Gaussian (n2, n1, n0, n1, n2) as exact float32 bits.
std::array<float, 5> repair_gaussian_coefficients() {
  const uint32_t bits[5] = {0x3cdc78fau, 0x3e6ef7f1u, 0x3ef578f1u, 0x3e6ef7f1u, 0x3cdc78fau};
  std::array<float, 5> g;
  for (int i = 0; i < 5; ++i) std::memcpy(&g[i], &bits[i], 4);
  return g;
}

int repair_vector_block(int h, int w, const Tensor& vectors) {
  require(vectors.dtype == DType::i16 && vectors.shape.size() == 3 && vectors.dim(2) == 2,
          "repair: expected int16 (H,W,2) vectors");
  if (vectors.dim(0) == (h + 1) / 2 && vectors.dim(1) == (w + 1) / 2) return 2;
  require(vectors.dim(0) == (h + 3) / 4 && vectors.dim(1) == (w + 3) / 4,
          "repair: expected one vector per 2x2 or 4x4 block");
  return 4;
}

Tensor compensate_float(Device& device, const Tensor& features, int channel, const Tensor& vectors) {
  require(features.dtype == DType::f32 && features.shape.size() == 3, "compensate_float: expected (C,H,W) float32");
  const int c = features.dim(0), h = features.dim(1), w = features.dim(2);
  require(channel >= 0 && channel < c, "compensate_float: channel");
  const int block = repair_vector_block(h, w, vectors);
  Tensor out = empty(device, DType::f32, {h, w});
  RepairCompensateParams p{h, w, block, channel, 1};
  device.launch("repair_compensate_float", grid2(w, h), kGroup2, {features.b(), vectors.b(), out.b()}, p);
  return out;
}

// reconstruct_float_plane: references (N,H,W) float32, N in 0..4.
Tensor repair_reconstruct(Device& device, const Tensor& current, const Tensor* references, int count,
                          const Tensor& mask, bool force) {
  const int pixels = int(current.numel());
  Tensor out = empty(device, DType::f32, current.shape);
  RepairReconstructParams p{pixels, count, force ? 1 : 0};
  const Buffer* refs = count ? references->b() : current.b();
  device.launch("repair_reconstruct", grid1(pixels), kGroup1, {current.b(), refs, mask.b(), out.b()}, p);
  return out;
}

Tensor repair_blend(Device& device, const Tensor& current, const Tensor& repaired, const Tensor& mask,
                    bool in_place) {
  const int h = current.dim(0), w = current.dim(1);
  Tensor out = empty(device, DType::f32, {h, w});
  RepairBlendParams p{h, w, in_place ? 1 : 0};
  device.launch("repair_blend", grid2(w, h), kGroup2, {current.b(), repaired.b(), mask.b(), out.b()}, p);
  return out;
}

Tensor repair_gaussian(Device& device, const Tensor& source) {
  const int h = source.dim(0), w = source.dim(1);
  const auto g = repair_gaussian_coefficients();
  RepairGaussianParams p{h, w, g[0], g[1], g[2], g[3], g[4]};
  Tensor tmp = empty(device, DType::f32, {h, w});
  Tensor out = empty(device, DType::f32, {h, w});
  device.launch("repair_gaussian_v", grid2(w, h), kGroup2, {source.b(), tmp.b()}, p);
  device.launch("repair_gaussian_h", grid2(w, h), kGroup2, {tmp.b(), out.b()}, p);
  return out;
}

RepairFinishParams repair_finish_params(int h, int w, bool hdr, bool smoothing) {
  const auto g = repair_gaussian_coefficients();
  return RepairFinishParams{h, w, hdr ? 1 : 0, smoothing ? 1 : 0, 0, 3, g[0], g[1], g[2], g[3], g[4]};
}

// finish_rgb pass 1 (codes + vertical blur) or, without smoothing, the final output.
// codes: six (H,W) int32 planes (repaired RGB, original RGB). table: HDR codes or unused.
// channel >= 0 computes only codes[channel], codes[3 + channel] and that vblur plane.
Tensor repair_source_bounds(Device& device, const Buffer& original, int h, int w) {
  const int32_t init[2] = {0x7fffffff, int32_t(0x80000000)};
  Tensor bounds = upload(device, DType::i32, {2}, init);
  SizeParams p{h * w * 3};
  device.launch("repair_source_bounds", grid1(256 * 1024), kGroup1, {&original, bounds.b()}, p);
  return bounds;
}

void repair_finish_codes(Device& device, const Tensor& features, const Buffer& original, const Tensor* table,
                         Tensor* codes, Tensor* vblur, const Buffer& output, int h, int w, bool hdr, bool smoothing,
                         const Tensor* bounds_in, int channel) {
  RepairFinishParams p = repair_finish_params(h, w, hdr, smoothing);
  const Tensor bounds = bounds_in ? *bounds_in : repair_source_bounds(device, original, h, w);
  const Buffer* tb = table ? table->b() : features.b();
  if (!smoothing) {
    const Buffer* f = features.b();
    device.launch("repair_finish_codes", grid2(w, h), kGroup2, {f, &original, tb, bounds.b(), f, f, f, f, f, f, f, &output},
                  p);
    return;
  }
  if (!vblur->buf) *vblur = empty(device, DType::f32, {3, h, w});
  if (channel >= 0) {
    p.c0 = channel;
    p.c1 = channel + 1;
    const Tensor& rc = codes[channel] = empty(device, DType::i32, {h, w});
    const Tensor& oc = codes[3 + channel] = empty(device, DType::i32, {h, w});
    device.launch("repair_finish_codes", grid2(w, h), kGroup2,
                  {features.b(), &original, tb, bounds.b(), rc.b(), rc.b(), rc.b(), oc.b(), oc.b(), oc.b(), vblur->b(),
                   &output},
                  p);
    return;
  }
  for (int i = 0; i < 6; ++i) codes[i] = empty(device, DType::i32, {h, w});
  device.launch("repair_finish_codes", grid2(w, h), kGroup2,
                {features.b(), &original, tb, bounds.b(), codes[0].b(), codes[1].b(), codes[2].b(), codes[3].b(), codes[4].b(),
                 codes[5].b(), vblur->b(), &output},
                p);
}

void repair_finish_blend(Device& device, const Tensor& features, const Tensor& vblur, const Tensor* blends,
                         const Buffer& output, int h, int w, const Tensor& bounds) {
  RepairFinishParams p = repair_finish_params(h, w, false, true);
  device.launch("repair_finish_blend", grid2(w, h), kGroup2,
                {features.b(), vblur.b(), bounds.b(), blends[0].b(), blends[1].b(), blends[2].b(), &output}, p);
}

namespace {

void require_plane(const Tensor& t, DType dtype, const char* what) {
  require(t.dtype == dtype && t.shape.size() == 2 && t.numel() > 0, what);
}

// inputs: features (C,H,W) f32, vectors (BH,BW,2) i16. scalars: channel.
std::vector<Tensor> debug_compensate(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 2, "repair_compensate_float: features, vectors");
  return {compensate_float(device, in[0], int(s.at(0)), in[1])};
}

// inputs: current (H,W), references (N,H,W), mask (H,W) u8. scalars: force.
std::vector<Tensor> debug_reconstruct(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 3, "repair_reconstruct: current, references, mask");
  require_plane(in[0], DType::f32, "repair_reconstruct: current");
  require_plane(in[2], DType::u8, "repair_reconstruct: mask");
  const int count = in[1].dim(0);
  require(count >= 0 && count <= 4, "repair_reconstruct: zero to four references");
  return {repair_reconstruct(device, in[0], &in[1], count, in[2], s.size() > 0 && s[0] != 0)};
}

// inputs: current, repaired (H,W) f32, mask u8. scalars: in_place.
std::vector<Tensor> debug_blend(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 3, "repair_blend: current, repaired, mask");
  require_plane(in[0], DType::f32, "repair_blend: current");
  return {repair_blend(device, in[0], in[1], in[2], s.at(0) != 0)};
}

// inputs: features (3,H,W) -> (H,W,3) RGB.
std::vector<Tensor> debug_features_to_rgb(Device& device, const std::vector<Tensor>& in, const std::vector<double>&) {
  require(in.size() == 1 && in[0].dtype == DType::f32 && in[0].dim(0) == 3, "repair_features_to_rgb: (3,H,W)");
  const int h = in[0].dim(1), w = in[0].dim(2);
  Tensor out = empty(device, DType::f32, {h, w, 3});
  SizeParams p{h * w};
  device.launch("repair_features_to_rgb", grid1(size_t(h) * w), kGroup1, {in[0].b(), out.b()}, p);
  return {out};
}

std::vector<Tensor> debug_gaussian(Device& device, const std::vector<Tensor>& in, const std::vector<double>&) {
  require(in.size() == 1, "repair_gaussian: plane");
  require_plane(in[0], DType::f32, "repair_gaussian: plane");
  return {repair_gaussian(device, in[0])};
}

// Finish passes with externally supplied contrast blend planes.
// inputs: features (3,H,W) bottom-up, original (H,W,3) top-down, blends (3,H,W) u8.
// scalars: hdr, smoothing. outputs: output (H,W,3), codes (6,H,W) i32, vblur (3,H,W).
std::vector<Tensor> debug_finish_parts(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 3, "repair_finish_parts: features, original, blends");
  const int h = in[0].dim(1), w = in[0].dim(2);
  const bool hdr = s.at(0) != 0, smoothing = s.at(1) != 0;
  Tensor out = empty(device, DType::f32, {h, w, 3});
  Tensor table = hdr ? hdr_curve_device(device, false) : Tensor();
  Tensor codes[6], vblur;
  const Tensor bounds = repair_source_bounds(device, in[1].buf, h, w);
  repair_finish_codes(device, in[0], in[1].buf, hdr ? &table : nullptr, codes, &vblur, out.buf, h, w, hdr, smoothing,
                      &bounds, -1);
  if (!smoothing) return {out};
  Tensor blends[3];
  for (int c = 0; c < 3; ++c) blends[c] = select0(device, in[2], c);
  repair_finish_blend(device, in[0], vblur, blends, out.buf, h, w, bounds);
  std::vector<Tensor> parts(codes, codes + 6);
  return {out, stack0(device, parts), vblur};
}

ADR_STAGE("repair_compensate_float", debug_compensate);
ADR_STAGE("repair_reconstruct", debug_reconstruct);
ADR_STAGE("repair_blend", debug_blend);
ADR_STAGE("repair_features_to_rgb", debug_features_to_rgb);
ADR_STAGE("repair_gaussian", debug_gaussian);
ADR_STAGE("repair_finish_parts", debug_finish_parts);

}  // namespace
}  // namespace adr
