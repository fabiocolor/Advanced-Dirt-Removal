// Advanced Dirt Removal engine benchmark and determinism check.
//
//   adr_bench --input film.u16 --width 1920 --height 1080 --frames 88
//                  [--backend auto] [--repeat 3] [--expect <sha256>] [--frame-hashes out.txt]
//                  [--control name=value ...] [--output out.f32]
//   adr_bench --synthetic SEED --width W --height H --frames N [--write-input clip.u16|clip.f32] [...]
//
// Input: raw little-endian top-down RGB, uint16 (converted exactly as
// float32(code)/float32(65535)) or float32 when the file name ends in .f32.
// --synthetic generates a deterministic film-like uint16 clip instead (moving
// textured content, grain, transient dark/bright specks and a hard cut halfway)
// with integer arithmetic only, so every platform gets identical input bytes;
// it lets remote GPUs be checked against a reference hash without transferring media.
// The SHA-256 covers every output float32 RGB byte in frame order.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "adr/adr.h"
#include "sha256.h"

namespace {

using adr_tools::Sha256;

bool set_control(adr_controls& c, const std::string& assignment) {
  auto eq = assignment.find('=');
  if (eq == std::string::npos) return false;
  std::string k = assignment.substr(0, eq);
  double v = std::atof(assignment.c_str() + eq + 1);
  if (k == "enabled") c.enabled = int(v);
  else if (k == "analyse") c.mode = int(v);
  else if (k == "remove_dark") c.remove_dark = int(v);
  else if (k == "remove_light") c.remove_light = int(v);
  else if (k == "repair_amount") c.repair_amount = float(v);
  else if (k == "analysis_detail") c.analysis_detail = float(v);
  else if (k == "sensitivity") c.sensitivity = float(v);
  else if (k == "extra_sensitivity") c.extra_sensitivity = int(v);
  else if (k == "motion_guard") c.motion_guard = float(v);
  else if (k == "shot_changes") c.shot_changes = int(v);
  else if (k == "shot_guard") c.shot_guard = float(v);
  else if (k == "blend_repairs") c.blend_repairs = int(v);
  else if (k == "blend_amount") c.blend_amount = float(v);
  else if (k == "extended_range") c.extended_range = int(v);
  else if (k == "flash_guard") c.flash_guard = int(v);
  else if (k == "limit_spot_size") c.limit_spot_size = int(v);
  else if (k == "largest_spot") c.largest_spot = int(v);
  else return false;
  return true;
}

double now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

namespace {
uint64_t splitmix(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
uint32_t lattice(uint64_t seed, int64_t gx, int64_t gy) {  // value noise lattice, 0..65535
  uint64_t s = seed ^ (uint64_t(gx) * 0x632BE59BD9B4E019ull) ^ (uint64_t(gy) * 0x8CB92BA72F3D8DD7ull);
  return uint32_t(splitmix(s) >> 48);
}
// Codes in [4096, 61439]: bilinear value noise on a 64-px lattice plus a 16-px octave.
void synthetic_frame(uint64_t seed, int f, int n, int w, int h, std::vector<uint16_t>& codes) {
  const uint64_t shot = seed + (f >= n / 2 ? 0x51ED270Bull : 0);
  const int64_t ox = 3 * int64_t(f), oy = int64_t(f);  // camera pan, px per frame
  auto octave = [&](int64_t x, int64_t y, int cell, uint64_t salt) -> int64_t {
    const int64_t gx = x >= 0 ? x / cell : -((-x + cell - 1) / cell), gy = y >= 0 ? y / cell : -((-y + cell - 1) / cell);
    const int64_t fx = x - gx * cell, fy = y - gy * cell;
    const int64_t a = lattice(shot ^ salt, gx, gy), b = lattice(shot ^ salt, gx + 1, gy);
    const int64_t c = lattice(shot ^ salt, gx, gy + 1), d = lattice(shot ^ salt, gx + 1, gy + 1);
    const int64_t top = a * (cell - fx) + b * fx, bottom = c * (cell - fx) + d * fx;
    return (top * (cell - fy) + bottom * fy) / (int64_t(cell) * cell);
  };
  uint64_t grain = seed * 31 + uint64_t(f) * 0x2545F4914F6CDD1Dull;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int64_t v = (3 * octave(x + ox, y + oy, 64, 1) + octave(x + ox, y + oy, 16, 2)) / 4;  // 0..65535
      for (int c = 0; c < 3; ++c) {
        const int64_t tint = (v * (12 + c)) / 14;
        const int64_t g = int64_t(splitmix(grain) % 1201) - 600;
        codes[(size_t(y) * w + x) * 3 + c] = uint16_t(std::clamp<int64_t>(4096 + tint * 57343 / 65535 + g, 0, 65535));
      }
    }
  uint64_t dirt = seed ^ (uint64_t(f) << 32) ^ 0xD1B54A32D192ED03ull;
  const int specks = std::max(4, int(int64_t(w) * h / 40000));
  for (int k = 0; k < specks; ++k) {
    const int sx = 1 + int(splitmix(dirt) % 9), sy = 1 + int(splitmix(dirt) % 9);
    const int x0 = int(splitmix(dirt) % uint64_t(std::max(1, w - sx))), y0 = int(splitmix(dirt) % uint64_t(std::max(1, h - sy)));
    const uint16_t value = (splitmix(dirt) & 1) ? 1200 : 64000;
    for (int y = y0; y < y0 + sy; ++y)
      for (int x = x0; x < x0 + sx; ++x)
        for (int c = 0; c < 3; ++c) codes[(size_t(y) * w + x) * 3 + c] = value;
  }
}
}  // namespace

int main(int argc, char** argv) {
  std::string input, backend = "auto", expect, frame_hashes, output_path, write_input;
  int w = 0, h = 0, n = 0, repeat = 1;
  bool synthetic = false;
  uint64_t seed = 0;
  adr_controls controls;
  adr_controls_default(&controls);
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit(2); }
      return argv[++i];
    };
    if (a == "--input") input = next();
    else if (a == "--synthetic") { synthetic = true; seed = std::strtoull(next().c_str(), nullptr, 10); }
    else if (a == "--width") w = std::atoi(next().c_str());
    else if (a == "--height") h = std::atoi(next().c_str());
    else if (a == "--frames") n = std::atoi(next().c_str());
    else if (a == "--backend") backend = next();
    else if (a == "--repeat") repeat = std::atoi(next().c_str());
    else if (a == "--expect") expect = next();
    else if (a == "--frame-hashes") frame_hashes = next();
    else if (a == "--output") output_path = next();
    else if (a == "--write-input") write_input = next();
    else if (a == "--control") {
      if (!set_control(controls, next())) { std::cerr << "bad control\n"; return 2; }
    } else { std::cerr << "unknown argument " << a << "\n"; return 2; }
  }
  if ((input.empty() && !synthetic) || w <= 0 || h <= 0 || n <= 0) {
    std::cerr << "usage: adr_bench --input film.u16|film.f32 | --synthetic SEED  --width W --height H --frames N\n";
    return 2;
  }
  const size_t samples = size_t(w) * h * 3;
  const bool is_float = input.size() > 4 && input.substr(input.size() - 4) == ".f32";
  std::vector<std::vector<float>> frames(n, std::vector<float>(samples));
  if (synthetic) {
    std::vector<uint16_t> codes(samples);
    std::ofstream dump;
    if (!write_input.empty()) dump.open(write_input, std::ios::binary);
    const bool dump_float = write_input.size() > 4 && write_input.substr(write_input.size() - 4) == ".f32";
    for (int f = 0; f < n; ++f) {
      synthetic_frame(seed, f, n, w, h, codes);
      for (size_t k = 0; k < samples; ++k) frames[f][k] = float(codes[k]) / 65535.f;
      if (dump && dump_float) dump.write(reinterpret_cast<const char*>(frames[f].data()), std::streamsize(samples * 4));
      else if (dump) dump.write(reinterpret_cast<const char*>(codes.data()), std::streamsize(samples * 2));
    }
  } else {
    std::ifstream in(input, std::ios::binary);
    if (!in) { std::cerr << "cannot open input\n"; return 1; }
    std::vector<uint16_t> codes(samples);
    for (int f = 0; f < n; ++f) {
      if (is_float) {
        in.read(reinterpret_cast<char*>(frames[f].data()), std::streamsize(samples * 4));
      } else {
        in.read(reinterpret_cast<char*>(codes.data()), std::streamsize(samples * 2));
        for (size_t k = 0; k < samples; ++k) frames[f][k] = float(codes[k]) / 65535.f;
      }
      if (!in) { std::cerr << "input too short\n"; return 1; }
    }
  }
  char error[4096] = {};
  double t0 = now();
  adr_context* context = adr_context_create(backend.c_str(), error, sizeof(error));
  if (!context) { std::cerr << "context: " << error << "\n"; return 1; }
  const double init_seconds = now() - t0;
  std::vector<float> out(samples);
  std::ostringstream json;
  json << "{\n \"backend\": \"" << adr_context_backend(context) << "\",\n \"device\": \"" << adr_context_device_name(context)
       << "\",\n \"engine\": \"" << adr_version() << "\",\n \"kernel_source_sha256\": \"" << adr_kernel_source_sha256()
       << "\",\n \"rcpss\": \"" << adr_reciprocal_table() << "\",\n \"geometry\": [" << n << ", " << h << ", " << w << ", 3],\n \"context_init_seconds\": " << init_seconds
       << ",\n \"runs\": [";
  int status = 0;
  for (int r = 0; r < repeat; ++r) {
    Sha256 digest;
    std::vector<std::string> hashes;
    std::vector<std::vector<float>> results;  // hashed after timing
    results.reserve(n);
    std::vector<double> frame_seconds;
    std::ofstream raw;
    if (!output_path.empty() && r == 0) raw.open(output_path, std::ios::binary);
    double start = now();
    adr_sequence* seq = adr_sequence_create(context, &controls, w, h, n, error, sizeof(error));
    if (!seq) { std::cerr << "sequence: " << error << "\n"; return 1; }
    double previous = now();
    while (adr_sequence_next_output(seq) >= 0) {
      int frame = adr_sequence_pull(seq, out.data(), size_t(w) * 12, error, sizeof(error));
      if (frame == -2) { std::cerr << "render: " << error << "\n"; return 1; }
      if (frame == -1) {
        int next = adr_sequence_next_source(seq);
        if (adr_sequence_push(seq, frames[next].data(), size_t(w) * 12, error, sizeof(error))) {
          std::cerr << "push: " << error << "\n";
          return 1;
        }
        continue;
      }
      results.push_back(out);
      double t = now();
      frame_seconds.push_back(t - previous);
      previous = t;
    }
    double seconds = now() - start;
    adr_sequence_destroy(seq);
    for (auto& result : results) {
      digest.update(result.data(), samples * 4);
      if (!frame_hashes.empty()) {
        Sha256 one;
        one.update(result.data(), samples * 4);
        hashes.push_back(one.hex());
      }
      if (raw) raw.write(reinterpret_cast<const char*>(result.data()), std::streamsize(samples * 4));
    }
    std::string hex = digest.hex();
    std::vector<double> sorted = frame_seconds;
    std::sort(sorted.begin(), sorted.end());
    bool match = expect.empty() || hex == expect;
    if (!match) status = 3;
    json << (r ? "," : "") << "\n  {\"repeat\": " << r << ", \"seconds\": " << seconds << ", \"ms_per_frame\": "
         << seconds * 1000 / n << ", \"median_frame_ms\": " << sorted[sorted.size() / 2] * 1000
         << ", \"output_sha256\": \"" << hex << "\", \"matches_expected\": " << (expect.empty() ? "null" : match ? "true" : "false")
         << "}";
    if (!frame_hashes.empty() && r == 0) {
      std::ofstream fh(frame_hashes);
      for (auto& x : hashes) fh << x << "\n";
    }
  }
  uint64_t allocated = 0, peak = 0;
  adr_context_memory(context, &allocated, &peak);
  json << "\n ],\n \"device_peak_mib\": " << peak / 1048576.0 << "\n}\n";
  std::cout << json.str();
  adr_context_destroy(context);
  return status;
}
