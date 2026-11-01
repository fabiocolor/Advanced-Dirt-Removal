// Auto recovery: a small convolutional classifier decides, for every spot the
// cleaning changed, whether it was real picture detail that should be given
// back. The result is a per-pixel probability map; the host thresholds it into
// a recovery matte the user can edit.
//
// Inputs follow the classifier's training conditions: RGB as 10-bit codes,
// frames narrower than 3000 pixels enlarged 2x (bicubic) so spot sizes keep
// their meaning, a spot = 8-connected pixels whose largest channel change is
// more than 8 codes (at least 3 pixels), and per spot 160 x 160 crops of the
// source, the cleaned frame, the previous and next source frames (code >> 2)
// and the spot's own mask: 13 channels.
#include "stages/nn_recovery.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <thread>

#include "kernels/params.h"
#include "kernels/params_nn.h"

namespace adr {
namespace {

constexpr int kCrop = 160;
constexpr int kChannels = 13;
constexpr int kBatch = 64;
constexpr int kRedCodes = 8;
constexpr int kMinArea = 3;

void parallel_rows(int n, const std::function<void(int, int)>& fn) {
  const int threads = std::max(1, std::min(32, int(std::thread::hardware_concurrency())));
  const int chunk = (n + threads - 1) / threads;
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) {
    const int a = t * chunk, b = std::min(n, a + chunk);
    if (a < b) pool.emplace_back(fn, a, b);
  }
  for (auto& th : pool) th.join();
}

float half_to_float(uint16_t h) {
  const uint32_t sign = uint32_t(h & 0x8000) << 16, exp = (h >> 10) & 0x1f, man = h & 0x3ff;
  uint32_t bits;
  if (exp == 0) {
    if (man == 0) {
      bits = sign;
    } else {  // subnormal
      int e = -1;
      uint32_t m = man;
      do { m <<= 1; ++e; } while (!(m & 0x400));
      bits = sign | uint32_t(127 - 15 - e) << 23 | (m & 0x3ff) << 13;
    }
  } else if (exp == 31) {
    bits = sign | 0x7f800000u | man << 13;
  } else {
    bits = sign | (exp + 127 - 15) << 23 | man << 13;
  }
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// Cineon log (black 95, white 685, film gamma 0.6) from linear, as 10-bit code / 1023.
float cineon_from_linear(float lin) {
  static const double offset = std::pow(10.0, (95.0 - 685.0) * 0.002 / 0.6), gain = 1.0 / (1.0 - offset);
  const double v = std::max((double(lin) + gain - 1.0) / gain, 1e-10);
  return float((685.0 + std::log10(v) * 0.6 / 0.002) / 1023.0);
}

// RGB rows (top-down) -> 10-bit codes, interleaved, w*h*3.
std::vector<uint16_t> to_codes(const float* rgb, int w, int h, size_t row_floats, int encoding) {
  std::vector<uint16_t> out(size_t(w) * h * 3);
  parallel_rows(h, [&](int a, int b) {
    for (int y = a; y < b; ++y) {
      const float* s = rgb + size_t(y) * row_floats;
      uint16_t* d = out.data() + size_t(y) * w * 3;
      for (int i = 0; i < w * 3; ++i) {
        float v = s[i];
        if (!std::isfinite(v)) v = 0.f;
        if (encoding == ADR_RECOVERY_CINEON_LINEAR) v = cineon_from_linear(v);
        d[i] = uint16_t(std::min(1023.f, std::max(0.f, std::nearbyint(v * 1023.f))));
      }
    }
  });
  return out;
}

// Bicubic enlargement (a = -0.75, pixel centres, edge samples repeated), rounded and clipped to [0, 1023].
std::vector<uint16_t> enlarge(const std::vector<uint16_t>& src, int w, int h, int scale) {
  if (scale == 1) return src;
  const int W = w * scale, H = h * scale;
  struct Tap { int i[4]; float c[4]; };
  auto taps = [&](int n, int size) {
    std::vector<Tap> t(size_t(n * scale));
    for (int o = 0; o < n * scale; ++o) {
      const float f = (o + .5f) / float(scale) - .5f;
      const int s = int(std::floor(f));
      const float x = f - float(s), A = -.75f;
      const float c0 = ((A * (x + 1) - 5 * A) * (x + 1) + 8 * A) * (x + 1) - 4 * A;
      const float c1 = ((A + 2) * x - (A + 3)) * x * x + 1;
      const float c2 = ((A + 2) * (1 - x) - (A + 3)) * (1 - x) * (1 - x) + 1;
      const float c3 = 1.f - c0 - c1 - c2;
      Tap& tp = t[size_t(o)];
      const float c[4] = {c0, c1, c2, c3};
      for (int k = 0; k < 4; ++k) {
        tp.i[k] = std::min(size - 1, std::max(0, s - 1 + k));
        tp.c[k] = c[k];
      }
    }
    return t;
  };
  const auto tx = taps(w, w), ty = taps(h, h);
  std::vector<float> mid(size_t(W) * h * 3);
  parallel_rows(h, [&](int a, int b) {
    for (int y = a; y < b; ++y)
      for (int x = 0; x < W; ++x) {
        const Tap& t = tx[size_t(x)];
        for (int c = 0; c < 3; ++c) {
          float v = 0;
          for (int k = 0; k < 4; ++k) v += t.c[k] * float(src[(size_t(y) * w + t.i[k]) * 3 + c]);
          mid[(size_t(y) * W + x) * 3 + c] = v;
        }
      }
  });
  std::vector<uint16_t> out(size_t(W) * H * 3);
  parallel_rows(H, [&](int a, int b) {
    for (int y = a; y < b; ++y) {
      const Tap& t = ty[size_t(y)];
      for (int x = 0; x < W * 3; ++x) {
        float v = 0;
        for (int k = 0; k < 4; ++k) v += t.c[k] * mid[size_t(t.i[k]) * W * 3 + x];
        out[size_t(y) * W * 3 + x] = uint16_t(std::min(1023.f, std::max(0.f, std::nearbyint(v))));
      }
    }
  });
  return out;
}

struct Spot {
  int area = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
  double sx = 0, sy = 0;
};

int find(std::vector<int>& parent, int a) {
  while (parent[size_t(a)] != a) {
    parent[size_t(a)] = parent[size_t(parent[size_t(a)])];
    a = parent[size_t(a)];
  }
  return a;
}

}  // namespace

std::shared_ptr<RecoveryModel> load_recovery_model(Device& device, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  require(bool(in), "auto recovery: model file not found");
  std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  require(data.size() > 12 && std::memcmp(data.data(), "ADRRM001", 8) == 0, "auto recovery: not a recovery model file");
  size_t o = 8;
  auto u32 = [&] {
    require(o + 4 <= data.size(), "auto recovery: truncated model file");
    uint32_t v;
    std::memcpy(&v, data.data() + o, 4);
    o += 4;
    return v;
  };
  auto model = std::make_shared<RecoveryModel>();
  const uint32_t count = u32();
  for (uint32_t l = 0; l < count; ++l) {
    RecoveryLayer layer;
    layer.kind = int(u32());
    layer.co = int(u32()), layer.ci = int(u32()), layer.k = int(u32());
    layer.stride = int(u32()), layer.pad = int(u32()), layer.flags = int(u32());
    const size_t nw = size_t(layer.co) * layer.ci * layer.k * layer.k;
    require(o + nw * 2 + size_t(layer.co) * 4 <= data.size(), "auto recovery: truncated model file");
    std::vector<float> w(nw);
    for (size_t i = 0; i < nw; ++i) {
      uint16_t h;
      std::memcpy(&h, data.data() + o + i * 2, 2);
      w[i] = half_to_float(h);
    }
    o += nw * 2;
    std::vector<float> b(size_t(layer.co));
    std::memcpy(b.data(), data.data() + o, b.size() * 4);
    o += b.size() * 4;
    layer.weight = upload(device, DType::f32, {int64_t(nw)}, w.data());
    layer.bias = upload(device, DType::f32, {layer.co}, b.data());
    layer.fc_bias = b[0];
    model->layers.push_back(layer);
  }
  require(!model->layers.empty() && model->layers.back().kind == 1, "auto recovery: model has no classifier layer");
  return model;
}

std::vector<float> recovery_forward(Device& device, const RecoveryModel& model, const uint8_t* crops, int count) {
  std::vector<float> probs(static_cast<size_t>(count));
  for (int start = 0; start < count; start += kBatch) {
    const int B = std::min(kBatch, count - start);
    const size_t n = size_t(B) * kChannels * kCrop * kCrop;
    Tensor bytes = upload(device, DType::u8, {int64_t(n)}, crops + size_t(start) * kChannels * kCrop * kCrop);
    Tensor x = empty(device, DType::f32, {B, kChannels, kCrop, kCrop});
    SizeParams sp{int(n)};
    device.launch("nn_bytes_to_float", grid1(n), kGroup1, {bytes.b(), x.b()}, sp);
    int c = kChannels, hgt = kCrop, wid = kCrop;
    Tensor residual;
    int rh = 0, rw = 0;  // residual geometry (the block input, or its downsampled shortcut)
    bool first = true;
    for (const RecoveryLayer& L : model.layers) {
      if (L.kind == 1) {
        Tensor logits = empty(device, DType::f32, {B});
        NnHeadParams hp{B, c, hgt * wid, L.fc_bias};
        device.launch("nn_head", grid1(size_t(B)), Dim3{64, 1, 1}, {x.b(), L.weight.b(), logits.b()}, hp);
        const std::vector<float> lg = download<float>(device, logits);
        for (int b = 0; b < B; ++b) probs[size_t(start + b)] = float(1.0 / (1.0 + std::exp(-double(lg[size_t(b)]))));
        break;
      }
      const bool downsample = L.flags & 8;
      const Tensor& input = downsample ? residual : x;
      const int ih = downsample ? rh : hgt, iw = downsample ? rw : wid;
      const int ho = (ih + 2 * L.pad - L.k) / L.stride + 1, wo = (iw + 2 * L.pad - L.k) / L.stride + 1;
      Tensor out = empty(device, DType::f32, {B, L.co, ho, wo});
      const int add = (L.flags & 2) ? 1 : 0;
      NnConvParams cp{B, L.ci, ih, iw, L.co, ho, wo, L.k, L.stride, L.pad, (L.flags & 1) ? 1 : 0, add};
      const int N = B * ho * wo;
      device.launch("nn_conv", Dim3{((N + 63) / 64) * 256, (L.co + 63) / 64, 1}, Dim3{256, 1, 1},
                    {input.b(), L.weight.b(), L.bias.b(), add ? residual.b() : out.b(), out.b()}, cp);
      if (downsample) {
        residual = out, rh = ho, rw = wo;
        continue;
      }
      if (L.flags & 4) residual = x, rh = hgt, rw = wid;
      x = out;
      c = L.co, hgt = ho, wid = wo;
      if (first) {
        first = false;
        const int ph = (hgt + 2 - 3) / 2 + 1, pw = (wid + 2 - 3) / 2 + 1;
        Tensor pooled = empty(device, DType::f32, {B, c, ph, pw});
        NnPoolParams pp{B, c, hgt, wid, ph, pw};
        device.launch("nn_maxpool", grid1(size_t(B) * c * ph * pw), kGroup1, {x.b(), pooled.b()}, pp);
        x = pooled;
        hgt = ph, wid = pw;
      }
    }
  }
  return probs;
}

RecoveryFrame recovery_frame(Device& device, const RecoveryModel& model, const float* prev, const float* current,
                             const float* next, const float* cleaned, int w, int h, size_t row_floats, int encoding,
                             uint8_t* probability) {
  RecoveryFrame result;
  const int scale = w < 3000 ? 2 : 1, W = w * scale, H = h * scale;
  const auto base = enlarge(to_codes(current, w, h, row_floats, encoding), w, h, scale);
  const auto dry = enlarge(to_codes(cleaned, w, h, row_floats, encoding), w, h, scale);
  const auto before = enlarge(to_codes(prev, w, h, row_floats, encoding), w, h, scale);
  const auto after = enlarge(to_codes(next, w, h, row_floats, encoding), w, h, scale);
  // Changed pixels and 8-connected spots (union-find, raster order).
  std::vector<uint8_t> red(size_t(W) * H);
  parallel_rows(H, [&](int a, int b) {
    for (size_t i = size_t(a) * W; i < size_t(b) * W; ++i) {
      int m = 0;
      for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(base[i * 3 + c]) - int(dry[i * 3 + c])));
      red[i] = m > kRedCodes;
    }
  });
  std::vector<int> label(size_t(W) * H, -1), parent;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const size_t i = size_t(y) * W + x;
      if (!red[i]) continue;
      int l = -1;
      const int nb[4][2] = {{-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
      for (auto& d : nb) {
        const int nx = x + d[0], ny = y + d[1];
        if (nx < 0 || ny < 0 || nx >= W) continue;
        const int q = label[size_t(ny) * W + nx];
        if (q < 0) continue;
        if (l < 0) {
          l = find(parent, q);
        } else {
          const int a = find(parent, l), b = find(parent, q);
          if (a != b) parent[size_t(std::max(a, b))] = std::min(a, b), l = std::min(a, b);
        }
      }
      if (l < 0) {
        l = int(parent.size());
        parent.push_back(l);
      }
      label[i] = l;
    }
  std::vector<int> root(parent.size()), index(parent.size(), -1);
  std::vector<Spot> spots;
  for (size_t l = 0; l < parent.size(); ++l) {
    root[l] = find(parent, int(l));
    if (root[l] == int(l)) index[l] = int(spots.size()), spots.emplace_back();
  }
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      int& l = label[size_t(y) * W + x];
      if (l < 0) continue;
      l = index[size_t(root[size_t(l)])];
      Spot& s = spots[size_t(l)];
      ++s.area, s.sx += x, s.sy += y;
      s.x0 = std::min(s.x0, x), s.y0 = std::min(s.y0, y), s.x1 = std::max(s.x1, x), s.y1 = std::max(s.y1, y);
    }
  std::vector<int> scored;
  for (size_t s = 0; s < spots.size(); ++s)
    if (spots[s].area >= kMinArea) scored.push_back(int(s));
  result.spots = int(spots.size());
  result.scored = int(scored.size());
  // Crops: source, cleaned, previous, next (3 channels each, code >> 2) and the spot's mask.
  std::vector<uint8_t> crops(scored.size() * kChannels * kCrop * kCrop, 0);
  parallel_rows(int(scored.size()), [&](int a, int b) {
    for (int k = a; k < b; ++k) {
      const Spot& s = spots[size_t(scored[size_t(k)])];
      const double cx = s.sx / s.area, cy = s.sy / s.area;
      const int x0 = int(std::nearbyint(cx - kCrop / 2.0)), y0 = int(std::nearbyint(cy - kCrop / 2.0));
      uint8_t* out = crops.data() + size_t(k) * kChannels * kCrop * kCrop;
      const std::vector<uint16_t>* images[4] = {&base, &dry, &before, &after};
      for (int yy = 0; yy < kCrop; ++yy) {
        const int y = y0 + yy;
        if (y < 0 || y >= H) continue;
        for (int xx = 0; xx < kCrop; ++xx) {
          const int x = x0 + xx;
          if (x < 0 || x >= W) continue;
          const size_t i = size_t(y) * W + x;
          for (int im = 0; im < 4; ++im)
            for (int c = 0; c < 3; ++c)
              out[(size_t(im * 3 + c) * kCrop + yy) * kCrop + xx] = uint8_t((*images[im])[i * 3 + c] >> 2);
          if (label[i] == scored[size_t(k)]) out[(size_t(12) * kCrop + yy) * kCrop + xx] = 255;
        }
      }
    }
  });
  const std::vector<float> probs = recovery_forward(device, model, crops.data(), int(scored.size()));
  std::vector<float> by_spot(spots.size(), -1.f);
  for (size_t k = 0; k < scored.size(); ++k) by_spot[size_t(scored[k])] = probs[k];
  result.spot_probability.resize(scored.size());
  for (size_t k = 0; k < scored.size(); ++k) {
    const Spot& s = spots[size_t(scored[k])];
    result.spot_probability[k] = {float(s.sx / s.area / scale), float(s.sy / s.area / scale), s.area, probs[k]};
  }
  // Native probability map: the largest probability of any scored spot under the pixel's footprint.
  parallel_rows(h, [&](int a, int b) {
    for (int y = a; y < b; ++y)
      for (int x = 0; x < w; ++x) {
        float best = -1.f;
        for (int dy = 0; dy < scale; ++dy)
          for (int dx = 0; dx < scale; ++dx) {
            const int l = label[size_t(y * scale + dy) * W + x * scale + dx];
            if (l >= 0) best = std::max(best, by_spot[size_t(l)]);
          }
        probability[size_t(y) * w + x] = best < 0 ? 0 : uint8_t(1 + std::nearbyint(std::min(1.f, best) * 254.f));
      }
  });
  return result;
}

}  // namespace adr
