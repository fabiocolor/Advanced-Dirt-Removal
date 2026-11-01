// Detection group host orchestration.
#include "stages/detect.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "kernels/params_detect.h"
#include "rcpss.h"

namespace adr {
namespace detect {

struct Remap {
  int in_low = 0, span = 1, out_low = 0, coarse = 0, fractional = 0;
};

std::vector<int> gaussian_coefficients(int window) {
  require(window >= 1 && window <= 255 && window % 2 == 1, "gaussian window must be odd in [1,255]");
  const double sigma = double(float(window / 6.0));
  const int radius = window / 2;
  std::vector<double> values;
  double total = 0;
  for (int x = -radius; x <= radius; ++x) {
    const double v = std::exp(-double(x * x) / (2 * sigma * sigma));
    values.push_back(v);
  }
  for (double v : values) total += v;
  std::vector<int> weights;
  int sum = 0;
  for (double v : values) {
    const int q = int(std::floor(v * 8192 / total + 0.5));
    weights.push_back(q);
    sum += q;
  }
  weights[radius] += 8192 - sum;
  return weights;
}

Tensor gauss_weights(Device& device, int window) {
  return *device.cached<Tensor>("detect_gauss_" + std::to_string(window), [&] {
    std::vector<int> w = gaussian_coefficients(window);
    return upload(device, DType::i32, {int64_t(w.size())}, w.data());
  });
}

Remap remap_coefficients(int input_low, int input_high, int output_low, int output_high) {
  for (int v : {input_low, input_high, output_low, output_high})
    require(v >= 0 && v <= 255, "remap endpoints must be in [0,255]");
  require(input_high > input_low, "remap input high must exceed input low");
  Remap r;
  r.in_low = input_low;
  r.span = input_high - input_low;
  r.out_low = output_low;
  const int delta = output_high - output_low;
  r.coarse = int(std::trunc(double(delta) / double(2 * r.span)));
  r.fractional = int(std::trunc(double(delta - 2 * r.coarse * r.span) * 8192 / double(r.span)));
  return r;
}

// Vertical then horizontal Gaussian; nchan planes stacked planar in src.
Tensor gauss_v(Device& device, const Tensor& src, int h, int w, int nchan, int window) {
  Tensor tmp = empty(device, DType::u8, src.shape);
  DetectGaussParams p{h, w, window, nchan, 0, 0, 0, 0, 0, 0, 0.f, rcpss_table().rcp8192};
  Tensor weights = gauss_weights(device, window);
  device.launch("detect_gauss_v", Dim3{w, h, nchan}, kGroup2, {src.b(), weights.b(), tmp.b()}, p);
  return tmp;
}

void gauss_h(Device& device, const Tensor& tmp, const Tensor* aux, Tensor& out, Tensor* out2, int h, int w,
             int nchan, int window, int mode, float scale, const Remap& r) {
  DetectGaussParams p{h, w, window, nchan, mode, r.in_low, r.span, r.out_low, r.coarse, r.fractional, scale,
                      rcpss_table().rcp8192};
  Tensor weights = gauss_weights(device, window);
  device.launch("detect_gauss_h", Dim3{w, h, mode <= 1 ? nchan : 1}, kGroup2,
                {tmp.b(), weights.b(), aux ? aux->b() : tmp.b(), out.b(), out2 ? out2->b() : out.b()}, p);
}

void weighted(Device& device, const Tensor& src, const Tensor& lut, const Tensor& divisor, const Tensor& rounding,
              const Tensor* aux, Tensor& out, Tensor* out2, int h, int w, int nchan, int window, int mode, int lo,
              int hi) {
  DetectWeightedParams p{h, w, window, 128, nchan, lo, hi, mode};
  device.launch("detect_weighted", Dim3{w, h, nchan}, kGroup2,
                {src.b(), lut.b(), divisor.b(), rounding.b(), aux ? aux->b() : src.b(), out.b(),
                 out2 ? out2->b() : out.b()},
                p);
}

int vector_block(int h, int w, const Tensor& vectors) {
  require(vectors.dtype == DType::i16 && vectors.shape.size() == 3 && vectors.dim(2) == 2,
          "compensate: expected int16 (H',W',2) vectors");
  if (vectors.dim(0) == (h + 1) / 2 && vectors.dim(1) == (w + 1) / 2) return 2;
  require(vectors.dim(0) == (h + 3) / 4 && vectors.dim(1) == (w + 3) / 4,
          "compensate: expected one vector per 2x2 or 4x4 block");
  return 4;
}

// Compensate channels [c0, c0+nchan) of an interleaved byte image into planar (nchan,H,W).
Tensor compensate_planes(Device& device, const Tensor& image, int cstride, int c0, int nchan, const Tensor& vectors,
                         int h, int w) {
  const int block = vector_block(h, w, vectors);
  std::vector<int64_t> shape = nchan == 1 ? std::vector<int64_t>{h, w} : std::vector<int64_t>{nchan, h, w};
  Tensor out = empty(device, DType::u8, shape);
  DetectCompensateParams p{h, w, block, cstride, c0, nchan};
  device.launch("detect_compensate", Dim3{w, h, nchan}, kGroup2, {image.b(), vectors.b(), out.b()}, p);
  return out;
}

// Combine with an interleaved current image; references planar (nchan,n).
TemporalCombine combine_planes(Device& device, const Tensor& current, int cstride, int c0, int nchan, int n,
                               const std::vector<Tensor>& refs, float third, float fourth,
                               const std::vector<int64_t>& shape) {
  require(refs.size() <= 4, "combine: zero to four references");
  require(n > 0, "combine: empty planes");
  require(std::isfinite(third) && third >= 0 && third <= 1 && std::isfinite(fourth) && fourth >= 0 && fourth <= 1,
          "combine: spread weights must be in [0,1]");
  TemporalCombine c;
  c.base = empty(device, DType::u8, shape);
  c.centered_residual = empty(device, DType::u8, shape);
  c.temporal_spread = empty(device, DType::u8, shape);
  const Buffer* r[4];
  for (int k = 0; k < 4; ++k) r[k] = k < int(refs.size()) ? refs[k].b() : current.b();
  DetectCombineParams p{n, int(refs.size()), nchan, cstride, c0, third, fourth};
  device.launch("detect_combine", Dim3{n, nchan, 1}, kGroup1,
                {current.b(), r[0], r[1], r[2], r[3], c.base.b(), c.centered_residual.b(), c.temporal_spread.b()},
                p);
  return c;
}

NoiseAnalysis noise_planes(Device& device, const Tensor& base, const Tensor& current, int cstride, int c0, int nchan,
                           int h, int w, int valid_references, const Controls& controls) {
  require(valid_references >= 0 && valid_references <= 4, "noise: zero to four references");
  const int rows = (h + 7) / 8, cols = (w + 7) / 8;
  Tensor hist = zeros(device, DType::i32, {nchan, 256});
  DetectBlocksParams bp{h, w, rows, cols, cstride, c0, nchan};
  device.launch("detect_noise_blocks", Dim3{cols, rows, nchan}, Dim3{16, 8, 1}, {base.b(), current.b(), hist.b()}, bp);
  NoiseAnalysis out;
  const std::vector<int64_t> scalar = nchan == 1 ? std::vector<int64_t>{} : std::vector<int64_t>{nchan};
  out.trimmed_mean = empty(device, DType::f32, scalar);
  out.noise = empty(device, DType::f32, scalar);
  out.lut = empty(device, DType::u8, nchan == 1 ? std::vector<int64_t>{256} : std::vector<int64_t>{nchan, 256});
  out.divisor = empty(device, DType::i32, scalar);
  out.rounding = empty(device, DType::i32, scalar);
  DetectScalarParams sp{nchan,
                        controls.mode == 1 ? 1 : 0,
                        controls.remove_dark ? 1 : 0,
                        controls.remove_light ? 1 : 0,
                        controls.noise_multiplier(valid_references),
                        controls.analysis_scale(),
                        0,
                        0,
                        0.f,
                        0.f};
  device.launch("detect_noise_scalars", Dim3{nchan, 1, 1}, Dim3{4, 1, 1},
                {hist.b(), out.trimmed_mean.b(), out.noise.b(), out.lut.b(), out.divisor.b(), out.rounding.b()}, sp);
  return out;
}

// Weighted passes, clamps, energy, confidence and the dilated mask.
DetectionRefinement refine_planes(Device& device, const Tensor& centered, const NoiseAnalysis& noise, int h, int w,
                                  int nchan, const Controls& controls) {
  DetectionRefinement d;
  const std::vector<int64_t> shape = centered.shape;
  Tensor first = empty(device, DType::u8, shape);
  weighted(device, centered, noise.lut, noise.divisor, noise.rounding, nullptr, first, nullptr, h, w, nchan, 3, 0, 0,
           255);
  d.residual = empty(device, DType::u8, shape);
  Tensor absolute = empty(device, DType::u8, shape);
  weighted(device, first, noise.lut, noise.divisor, noise.rounding, nullptr, d.residual, &absolute, h, w, nchan, 3, 1,
           controls.remove_dark ? 0 : 128, controls.remove_light ? 255 : 128);
  Tensor tmp = gauss_v(device, absolute, h, w, nchan, 45);
  d.confidence = empty(device, DType::u8, {h, w});
  Tensor mask1 = empty(device, DType::u8, shape);
  gauss_h(device, tmp, &absolute, d.confidence, &mask1, h, w, nchan, 45, 2, 6.0f, remap_coefficients(0, 255, 255, 1));
  Tensor tmp2 = gauss_v(device, mask1, h, w, nchan, 35);
  d.mask = empty(device, DType::u8, {h, w});
  gauss_h(device, tmp2, &mask1, d.mask, nullptr, h, w, nchan, 35, 3, 2.5f, Remap{});
  return d;
}

Tensor repair_weights(Device& device, const Tensor& noise, int valid_references, float repair_weight,
                      const Controls& controls, Tensor& lut, Tensor& divisor, Tensor& rounding) {
  require(repair_weight >= 0 && repair_weight <= 1, "repair: repair_weight must be in [0,1]");
  const float one_minus = 1.f - repair_weight;
  const float power = float(std::pow(double(one_minus), double(1.934999942779541)));
  const float tripled = power * 3.f;
  const float scale = tripled + 0.17499999701976776f;
  DetectScalarParams sp{1,
                        0,
                        controls.remove_dark ? 1 : 0,
                        controls.remove_light ? 1 : 0,
                        controls.noise_multiplier(valid_references),
                        scale,
                        double(repair_weight) > .5 ? 1 : 0,
                        0,
                        repair_weight,
                        0.f};
  Tensor modified = empty(device, DType::f32, {});
  lut = empty(device, DType::u8, {256});
  divisor = empty(device, DType::i32, {});
  rounding = empty(device, DType::i32, {});
  device.launch("detect_repair_scalars", Dim3{1, 1, 1}, Dim3{1, 1, 1},
                {noise.b(), modified.b(), lut.b(), divisor.b(), rounding.b()}, sp);
  return modified;
}

Tensor repair_residual(Device& device, const Tensor& source, const Tensor& noise, int valid_references,
                       float repair_weight, const Controls& controls) {
  const int h = source.dim(0), w = source.dim(1);
  Tensor lut, divisor, rounding;
  repair_weights(device, noise, valid_references, repair_weight, controls, lut, divisor, rounding);
  Tensor first = empty(device, DType::u8, {h, w});
  weighted(device, source, lut, divisor, rounding, nullptr, first, nullptr, h, w, 1, 3, 0, 0, 255);
  Tensor gated = empty(device, DType::u8, {h, w});
  weighted(device, first, lut, divisor, rounding, &source, gated, nullptr, h, w, 1, 3, 2,
           controls.remove_dark ? 0 : 128, controls.remove_light ? 255 : 128);
  return gated;
}

// Union-find components; mode 0 returns the excluded plane, mode 1 clears
// excluded pixels of target.
Tensor components(Device& device, const Tensor& mask, int maximum_area, const Tensor* target) {
  require(mask.dtype == DType::u8 && mask.shape.size() == 2 && mask.numel(), "components: (H,W) byte mask");
  require(maximum_area >= 1, "components: maximum area must be positive");
  const int h = mask.dim(0), w = mask.dim(1), n = h * w;
  Tensor parent = empty(device, DType::i32, {n});
  Tensor counts = empty(device, DType::i32, {n});
  DetectPlaneParams pp{h, w};
  device.launch("detect_cc_init", grid1(n), kGroup1, {parent.b(), counts.b()}, pp);
  device.launch("detect_cc_merge", grid2(w, h), kGroup2, {mask.b(), parent.b()}, pp);
  device.launch("detect_cc_count", grid1(n), kGroup1, {mask.b(), parent.b(), counts.b()}, pp);
  Tensor out = empty(device, DType::u8, {h, w});
  DetectComponentParams cp{h, w, maximum_area, target ? 1 : 0};
  device.launch("detect_cc_output", grid1(n), kGroup1,
                {mask.b(), parent.b(), counts.b(), target ? target->b() : mask.b(), out.b()}, cp);
  return out;
}

Tensor contrast_blend_impl(Device& device, const Tensor& repaired, const Tensor& original, const Controls& controls,
                           Tensor* contrast_mask, Tensor* noise_out) {
  require(repaired.dtype == DType::i32 && original.dtype == DType::i32 && repaired.shape.size() == 2 &&
              repaired.shape == original.shape && repaired.dim(0) >= 10 && repaired.dim(1) >= 10,
          "contrast: matching int32 planes with dimensions at least ten");
  const int h = repaired.dim(0), w = repaired.dim(1);
  const int blocks = (w - 2) / 8;
  const long long denominator = (long long)(w - 2) * 2 + (long long)(blocks + 1) * 8 * (h - 2);
  const float factor = float(1.5 - std::max(.5, double(controls.repair_weight())));
  const auto thresholds = controls.blend_thresholds();
  DetectContrastParams p{h, w, float(denominator), factor, thresholds.first, thresholds.second};
  Tensor mask = empty(device, DType::u8, {h, w});
  Tensor sums = empty(device, DType::i32, {h, w});
  device.launch("detect_contrast", grid2(w, h), kGroup2, {repaired.b(), original.b(), mask.b(), sums.b()}, p);
  Tensor total = zeros(device, DType::i32, {});
  device.launch("detect_contrast_rows", Dim3{256, h, 1}, Dim3{256, 1, 1}, {sums.b(), total.b()}, p);
  Tensor out = empty(device, DType::u8, {h, w});
  Tensor noise = empty(device, DType::f32, {});
  device.launch("detect_contrast_blend", grid2(w, h), kGroup2, {mask.b(), total.b(), out.b(), noise.b()}, p);
  if (contrast_mask) *contrast_mask = mask;
  if (noise_out) *noise_out = noise;
  return out;
}

}  // namespace detect

using namespace detect;

Tensor compensate_analysis(Device& device, const Tensor& image, int channel, const Tensor& vectors) {
  require(image.dtype == DType::u8 && (image.shape.size() == 2 || image.shape.size() == 3) && image.numel(),
          "compensate: expected a (H,W) or (H,W,C) byte image");
  const int h = image.dim(0), w = image.dim(1);
  const int cstride = image.shape.size() == 3 ? image.dim(2) : 1;
  require(channel >= 0 && channel < cstride, "compensate: channel out of range");
  return compensate_planes(device, image, cstride, channel, 1, vectors, h, w);
}

Tensor gaussian_bytes(Device& device, const Tensor& source, int window) {
  require(source.dtype == DType::u8 && source.shape.size() == 2 && source.numel(), "gaussian: (H,W) byte plane");
  const int h = source.dim(0), w = source.dim(1);
  Tensor tmp = gauss_v(device, source, h, w, 1, window);
  Tensor out = empty(device, DType::u8, {h, w});
  gauss_h(device, tmp, nullptr, out, nullptr, h, w, 1, window, 0, 0.f, Remap{});
  return out;
}

Tensor weighted_residual(Device& device, const Tensor& source, const Tensor& lut, int window, const Tensor& divisor,
                         const Tensor& rounding) {
  require(source.dtype == DType::u8 && source.shape.size() == 2 && source.numel(), "weighted: (H,W) byte plane");
  require(window >= 1 && window <= 181 && window % 2 == 1, "weighted: odd window in [1,181]");
  require(lut.dtype == DType::u8 && lut.numel() == 256, "weighted: 256-entry byte LUT");
  require(divisor.dtype == DType::i32 && divisor.numel() == 1 && rounding.dtype == DType::i32 && rounding.numel() == 1,
          "weighted: int32 scalar normalization");
  const int h = source.dim(0), w = source.dim(1);
  Tensor out = empty(device, DType::u8, {h, w});
  weighted(device, source, lut, divisor, rounding, nullptr, out, nullptr, h, w, 1, window, 0, 0, 255);
  return out;
}

Tensor remap_bytes(Device& device, const Tensor& source, int input_low, int input_high, int output_low,
                   int output_high) {
  require(source.dtype == DType::u8 && source.numel(), "remap: byte plane");
  const Remap r = remap_coefficients(input_low, input_high, output_low, output_high);
  Tensor out = empty(device, DType::u8, source.shape);
  DetectRemapParams p{int(source.numel()), r.in_low, r.span, r.out_low, r.coarse, r.fractional};
  device.launch("detect_remap_bytes", grid1(source.numel()), kGroup1, {source.b(), out.b()}, p);
  return out;
}

TemporalCombine combine_bytes(Device& device, const Tensor& current, const std::vector<Tensor>& references,
                              float third_weight, float fourth_weight) {
  require(current.dtype == DType::u8 && current.numel(), "combine: nonempty byte planes");
  for (auto& r : references)
    require(r.dtype == DType::u8 && r.shape == current.shape, "combine: references must match current");
  return combine_planes(device, current, 1, 0, 1, int(current.numel()), references, third_weight, fourth_weight,
                        current.shape);
}

FrameDetection detect_frame(Device& device, const Prepared& current, const std::vector<const Prepared*>& references,
                            const std::vector<Tensor>& vectors, const std::vector<int>& offsets,
                            const Controls& controls) {
  require(references.size() == vectors.size() && references.size() == offsets.size() && references.size() <= 4,
          "detect_frame: references, vectors and offsets must match (at most four)");
  const bool rgb = controls.mode == 1;
  const Tensor& image = rgb ? current.rgb_planes : current.planes;
  require(image.buf && image.dtype == DType::u8 && image.shape.size() == 3 && image.dim(2) == 3,
          rgb ? "detect_frame: RGB mode requires rgb_planes" : "detect_frame: planes (H,W,3) required");
  const int h = image.dim(0), w = image.dim(1), nchan = rgb ? 3 : 1, n = h * w;
  const std::vector<int64_t> shape = rgb ? std::vector<int64_t>{3, h, w} : std::vector<int64_t>{h, w};
  FrameDetection out;
  out.offsets = offsets;
  std::vector<Tensor> refs;
  for (size_t k = 0; k < references.size(); ++k) {
    const Tensor& ref = rgb ? references[k]->rgb_planes : references[k]->planes;
    require(ref.buf && ref.shape == image.shape, "detect_frame: reference planes must match current");
    refs.push_back(compensate_planes(device, ref, 3, 0, nchan, vectors[k], h, w));
  }
  const int count = int(refs.size());
  const auto weights = controls.spread_weights(count);
  out.combined = combine_planes(device, image, 3, 0, nchan, n, refs, weights.first, weights.second, shape);
  out.noise = noise_planes(device, out.combined.base, image, 3, 0, nchan, h, w, count, controls);
  out.detection = refine_planes(device, out.combined.centered_residual, out.noise, h, w, nchan, controls);
  return out;
}

Tensor condition_repair_spread(Device& device, const Tensor& spread, const Tensor* noise_mean, int valid_references) {
  require(spread.dtype == DType::u8 && spread.shape.size() == 2 && spread.numel(), "condition: (H,W) byte plane");
  const int h = spread.dim(0), w = spread.dim(1), n = h * w;
  Tensor mapped;
  if (!noise_mean) {
    mapped = remap_bytes(device, spread, 0, 166, 0, 255);
  } else {
    require(noise_mean->dtype == DType::f32 && noise_mean->numel() == 1, "condition: float32 noise scalar");
    mapped = empty(device, DType::u8, {h, w});
    const float divisor = valid_references <= 2 ? 1.75f : valid_references == 3 ? 1.25f : 1.f;
    DetectConditionParams p{n, divisor};
    device.launch("detect_condition_map", grid1(n), kGroup1, {spread.b(), noise_mean->b(), mapped.b()}, p);
  }
  Tensor tmp = gauss_v(device, mapped, h, w, 1, 13);
  Tensor averaged = empty(device, DType::u8, {h, w});
  gauss_h(device, tmp, &spread, averaged, nullptr, h, w, 1, 13, 1, 0.f, Remap{});
  Tensor maximum = empty(device, DType::u8, {h, w});
  DetectPlaneParams pp{h, w};
  device.launch("detect_max3", grid2(w, h), kGroup2, {averaged.b(), maximum.b()}, pp);
  return gaussian_bytes(device, maximum, 31);
}

Tensor build_repair_mask(Device& device, const Tensor& centered_residual, const Tensor& noise, int valid_references,
                         const Controls& controls) {
  require(centered_residual.dtype == DType::u8 && centered_residual.shape.size() == 2 && centered_residual.numel(),
          "repair mask: (H,W) byte residual");
  require(noise.dtype == DType::f32 && noise.numel() == 1, "repair mask: float32 noise scalar");
  require(valid_references >= 0 && valid_references <= 4, "repair mask: zero to four references");
  const float repair_weight = controls.repair_weight();
  if (!controls.limit_spot_size)
    return repair_residual(device, centered_residual, noise, valid_references, repair_weight, controls);
  const float capped = float(double(1.1f) * .5);
  Tensor mask = repair_residual(device, centered_residual, noise, valid_references, std::min(repair_weight, capped),
                                controls);
  Tensor target = repair_weight > capped
                      ? repair_residual(device, centered_residual, noise, valid_references, repair_weight, controls)
                      : mask;
  return components(device, mask, controls.largest_spot_area(), &target);
}

Tensor excluded_regions(Device& device, const Tensor& mask, int maximum_area) {
  return components(device, mask, maximum_area, nullptr);
}

Tensor preservation_mask(Device& device, const Tensor& conditioned_spread, int valid_references,
                         const Controls& controls) {
  require(valid_references >= 0 && valid_references <= 4, "preservation: zero to four references");
  if (controls.extra_sensitivity && (valid_references == 4 || controls.shot_changes == 0))
    return zeros(device, DType::u8, conditioned_spread.shape);
  if (valid_references == 2 && controls.shot_changes == 2) {
    Tensor out = empty(device, DType::u8, conditioned_spread.shape);
    device.fill(out.buf, 255, out.bytes());
    return out;
  }
  const auto t = controls.preservation_thresholds(valid_references);
  return remap_bytes(device, conditioned_spread, t.first, t.second, 0, 255);
}

Tensor contrast_blend(Device& device, const Tensor& repaired_codes, const Tensor& original_codes,
                      const Controls& controls) {
  return contrast_blend_impl(device, repaired_codes, original_codes, controls, nullptr, nullptr);
}

}  // namespace adr
