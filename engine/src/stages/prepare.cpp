// Analysis preparation: float colour analysis and the extended-range analysis curve.
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

#include "stages/stages.h"

namespace adr {

const std::vector<int>& hdr_curve(bool byte) {
  static std::once_flag once[2];
  static std::vector<int> tables[2];
  std::call_once(once[byte ? 1 : 0], [byte] {
    // Generated from the transfer curve, evaluated in
    // double with float32 rounding at each step.
    const double scale = byte ? .25 : 1., maximum = byte ? 255 : 1023;
    const float step = float(1. / 7366), gamma = float(1 / .45), toe = float(1 / 4.5);
    std::vector<int>& values = tables[byte ? 1 : 0];
    values.resize(32768);
    for (int index = 0; index < 32768; ++index) {
      const float encoded = float(double(index - 154) * double(step));
      double linear;
      if (encoded >= .081)
        linear = std::pow((double(encoded) + .099) / 1.099, double(gamma));
      else
        linear = double(float(double(encoded) * double(toe)));
      const double log = linear > 0 ? double(float(std::log10(linear))) : -100.;
      const float value = float(((log + 2.28333) * .6 / .002) * scale);
      values[index] = int(std::max(0., std::min(maximum, double(value))) + .5);
    }
  });
  return tables[byte ? 1 : 0];
}

Tensor hdr_curve_device(Device& device, bool byte) {
  return *device.cached<Tensor>(byte ? "prepare_hdr_bytes" : "prepare_hdr_codes", [&] {
    const auto& values = hdr_curve(byte);
    if (!byte) return upload(device, DType::i32, {32768}, values.data());
    std::vector<uint8_t> bytes(values.begin(), values.end());
    return upload(device, DType::u8, {32768}, bytes.data());
  });
}

int pyramid_levels(int width) { return width < 800 ? 4 : width < 1350 ? 5 : width < 3000 ? 6 : 7; }

Prepared prepare_float(Device& device, const Buffer& rgb, int h, int w, int in_stride_floats, bool flip,
                       int levels, bool rgb_mode, bool hdr, Tensor* features) {
  Prepared out;
  out.planes = empty(device, DType::u8, {h, w, 3});
  Tensor packed0 = empty(device, DType::u8, {h, w, 4});
  Tensor feat = features ? empty(device, DType::f32, {3, h, w}) : Tensor();
  if (rgb_mode) out.rgb_planes = empty(device, DType::u8, {h, w, 3});
  // Unused outputs bind the planes buffer; the kernel does not write them.
  PrepareParams p{h, w, flip ? 1 : 0, in_stride_floats, rgb_mode ? 1 : 0, hdr ? 1 : 0};
  Tensor table = hdr ? hdr_curve_device(device, false) : out.planes;
  Tensor table_bytes = hdr && rgb_mode ? hdr_curve_device(device, true) : out.planes;
  device.launch("prepare_float", grid2(w, h), kGroup2,
                {&rgb, features ? feat.b() : out.planes.b(), out.planes.b(), packed0.b(),
                 rgb_mode ? out.rgb_planes.b() : out.planes.b(), table.b(), table_bytes.b()},
                p);
  if (features) *features = feat;
  out.packed.push_back(packed0);
  int ch = h, cw = w;
  for (int level = 1; level < levels; ++level) {
    const int oh = (ch + 1) / 2, ow = (cw + 1) / 2;
    ReduceParams r{ch, cw, oh, ow, level == 1 ? 2 : 1};
    Tensor tmp = empty(device, DType::u8, {ch, ow, 3});
    device.launch("reduce_packed_h", Dim3{ow, ch, 3}, Dim3{64, 4, 1}, {out.packed.back().b(), tmp.b()}, r);
    Tensor next = empty(device, DType::u8, {oh, ow, 4});
    device.launch("reduce_packed_v", grid2(ow, oh), kGroup2, {tmp.b(), next.b()}, r);
    out.packed.push_back(next);
    ch = oh;
    cw = ow;
  }
  Tensor wide = empty(device, DType::u8, {h, 2 * w});
  DoubleParams d{h, w, -2, 10};
  device.launch("double_h", grid2(w, h), kGroup2, {packed0.b(), wide.b()}, d);
  out.half_luma = empty(device, DType::u8, {2 * h, 2 * w});
  DoubleParams v{h, 2 * w, -2, 10};
  device.launch("double_v", grid2(2 * w, h), kGroup2, {wide.b(), out.half_luma.b()}, v);
  return out;
}

namespace {

// inputs: rgb (H,W,3) f32 already bottom-up. scalars: levels, extended_range, rgb_mode.
// outputs: planes, half_luma, rgb_planes (or empty), features (3,H,W), packed levels...
std::vector<Tensor> debug_prepare(Device& device, const std::vector<Tensor>& in, const std::vector<double>& s) {
  require(in.size() == 1 && in[0].dtype == DType::f32 && in[0].shape.size() == 3, "prepare_float: rgb");
  const int h = in[0].dim(0), w = in[0].dim(1);
  const int levels = int(s.at(0));
  const bool hdr = s.at(1) != 0;
  const bool rgb_mode = s.at(2) != 0;
  Tensor features;
  Prepared p = prepare_float(device, in[0].buf, h, w, w * 3, false, levels, rgb_mode, hdr, &features);
  std::vector<Tensor> out{p.planes, p.half_luma, rgb_mode ? p.rgb_planes : empty(device, DType::u8, {0}), features};
  for (auto& t : p.packed) out.push_back(t);
  return out;
}

ADR_STAGE("prepare_float", debug_prepare);

std::vector<Tensor> debug_hdr_curve(Device& device, const std::vector<Tensor>&, const std::vector<double>& s) {
  const auto& v = hdr_curve(s.at(0) != 0);
  return {upload(device, DType::i32, {int64_t(v.size())}, v.data())};
}
ADR_STAGE("prepare_hdr_curve", debug_hdr_curve);

}  // namespace
}  // namespace adr
