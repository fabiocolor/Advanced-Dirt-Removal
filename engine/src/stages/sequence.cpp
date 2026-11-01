// Streaming sequence renderer: six source frames
// and two motion frames are buffered; output n is produced once source frames
// through n+3 are loaded. The host reads exactly one small device result per
// frame (four scene-error means) to take the cut decision.
#include "stages/sequence.h"

#include <algorithm>
#include <cstring>

#include "stages/refine.h"
#include "stages/repair.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace adr {

namespace {
// ADR_PROFILE=1: synchronise after each phase and print cumulative times.
struct Profile {
  bool on = std::getenv("ADR_PROFILE") != nullptr;
  std::map<std::string, double> total;
  std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
  void mark(Device& d, const char* name) {
    if (!on) return;
    d.synchronize();
    auto now = std::chrono::steady_clock::now();
    total[name] += std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
  }
  ~Profile() {
    if (!on) return;
    for (auto& e : total) std::fprintf(stderr, "profile %-10s %10.1f ms\n", e.first.c_str(), e.second);
  }
};
Profile& profile() {
  static Profile p;
  return p;
}
constexpr int kOffsets[4] = {-1, 1, -2, 2};

int motion_block(int width) { return width < 800 ? 16 : width < 1350 ? 32 : width < 3000 ? 64 : 128; }

float next_shot_score(const std::map<int, float>& errors) {
  if (!errors.count(-1) || !errors.count(1)) return 0.f;
  const float minus2 = errors.count(-2) ? errors.at(-2) : 0.f;
  float past = std::max(errors.at(-1), minus2);
  past = std::max(past, 1.f);
  const float plus2 = errors.count(2) ? errors.at(2) : 255.f;
  const float future = std::min(errors.at(1), plus2);
  return future / past - 1.f;
}
}  // namespace

int Sequence::resume_frame(const HistoryStore& store, int frame) {
  auto it = store.checkpoints.upper_bound(frame);
  if (it == store.checkpoints.begin()) return 0;
  return std::prev(it)->first;
}

Sequence::Sequence(Device& device, const Controls& controls, int width, int height, int count,
                   std::shared_ptr<HistoryStore> store, int start)
    : device_(device), controls_(controls), w_(width), h_(height), count_(count), levels_(pyramid_levels(width)),
      store_(std::move(store)) {
  controls_.validate();
  if (count < 1) throw Error("Expected a positive frame count");
  // The motion size class is chosen from the width with no class above 3000
  // pixels, so wider frames use the 4K parameters. Odd sizes are supported;
  // compensation then uses the general stencil (repair.kern).
  if (width < 256 || height < 256 || width > 16384 || height > 16384)
    throw Error("Expected 256 to 16384 pixels in each dimension");
  const int block = motion_block(width);
  for (int offset : kOffsets)
    history_[offset] = zeros(device_, DType::i16, {(height + block - 1) / block, (width + block - 1) / block, 2});
  shots_[0] = 0;
  if (start > 0 && controls_.enabled) {
    if (!store_ || !store_->checkpoints.count(start)) throw Error("No motion-history checkpoint for the start frame");
    const HistoryCheckpoint& c = store_->checkpoints.at(start);
    history_ = c.history;
    shots_ = c.shots;
    flash_scores_ = c.flash_scores;
    next_motion_ = start;
    output_ = start;
    loaded_ = std::max(0, start - 2);
  } else if (start > 0) {
    output_ = loaded_ = start;  // bypass needs no history
  }
}

bool Sequence::ready() const {
  if (output_ >= count_) return false;
  const int needed = controls_.enabled ? std::min(count_, output_ + 4) : output_ + 1;
  return loaded_ >= needed;
}

void Sequence::push(const float* rgb, size_t row_bytes) {
  if (loaded_ >= count_) throw Error("All source frames are already loaded");
  profile().mark(device_, "other");
  const size_t row = size_t(w_) * 3 * sizeof(float);
  Tensor staging = empty(device_, DType::f32, {h_, w_, 3});
  if (row_bytes == row) {
    device_.upload(staging.buf, rgb, row * h_);
  } else {
    std::vector<float> packed(size_t(w_) * 3 * h_);
    for (int y = 0; y < h_; ++y)
      std::memcpy(packed.data() + size_t(y) * w_ * 3, reinterpret_cast<const char*>(rgb) + row_bytes * y, row);
    device_.upload(staging.buf, packed.data(), row * h_);
  }
  profile().mark(device_, "upload");
  load(staging.buf);
}

void Sequence::push_device(const Buffer& rgb) {
  if (loaded_ >= count_) throw Error("All source frames are already loaded");
  Tensor copy = empty(device_, DType::f32, {h_, w_, 3});
  device_.copy(copy.buf, rgb, copy.bytes());
  load(copy.buf);
}

void Sequence::load(const Buffer& rgb) {
  Frame frame;
  frame.original.buf = rgb;
  frame.original.dtype = DType::f32;
  frame.original.shape = {h_, w_, 3};
  if (controls_.enabled) {
    frame.prepared = prepare_float(device_, rgb, h_, w_, w_ * 3, true, levels_, controls_.mode == 1,
                                   controls_.extended_range, &frame.features);
  }
  profile().mark(device_, "prepare");
  frames_[loaded_] = std::move(frame);
  ++loaded_;
  ++stats_.source_frames_read;
  stats_.maximum_buffered_source_frames = std::max(stats_.maximum_buffered_source_frames, int(frames_.size()));
  device_.flush();
}

void Sequence::advance() {
  const int frame = next_motion_;
  if (store_ && !store_->checkpoints.count(frame)) {
    HistoryCheckpoint c;
    c.history = history_;
    for (auto& e : shots_)
      if (e.first >= frame - 2) c.shots.insert(e);
    for (auto& e : flash_scores_)
      if (e.first >= frame - 1) c.flash_scores.insert(e);
    store_->checkpoints.emplace(frame, std::move(c));
  }
  Motion motion;
  std::map<int, Tensor> means, errors;
  for (int offset : kOffsets) {
    const int reference = frame + offset;
    const bool valid = reference >= 0 && reference < count_ && (offset > 0 || shots_.at(reference) == shots_.at(frame));
    if (!valid) {
      history_[offset] = zeros(device_, DType::i16, history_[offset].shape);
      continue;
    }
    Tensor seed = reduce_motion_vectors(device_, history_[offset]);
    const Tensor* noise = nullptr;
    if (offset == -2 || offset == 2) {
      auto it = means.find(offset == -2 ? -1 : 1);
      if (it != means.end()) noise = &it->second;
    }
    profile().mark(device_, "other");
    PairMotion pair = estimate_pair(device_, frames_.at(frame).prepared, frames_.at(reference).prepared, seed, noise);
    profile().mark(device_, "motion");
    means[offset] = pair.motion_noise;
    history_[offset] = pair.history;
    errors[offset] = error_mean(device_, pair.scene_error);
    motion.vectors[offset] = pair.vectors;
    motion.levels[offset] = pair.levels;
  }
  if (frame + 1 < count_) {
    // The single host synchronisation per frame: scene-error means.
    std::map<int, float> host;
    for (auto& entry : errors) {
      float value = 0;
      device_.download(&value, entry.second.buf, sizeof(float));
      host[entry.first] = value;
    }
    shots_[frame + 1] = shots_.at(frame) + (next_shot_score(host) > .5f ? 1 : 0);
    const bool same_future = shots_[frame + 1] == shots_.at(frame);
    int valid_bits = 0;
    for (int k = 0; k < 4; ++k)
      if (host.count(kOffsets[k]) && (kOffsets[k] < 0 || same_future)) valid_bits |= 1 << k;
    auto ratio = [&](int numerator, std::initializer_list<int> denominators) {
      auto it = denominators.begin();
      float denominator = host.at(*it);
      for (++it; it != denominators.end(); ++it) denominator = std::max(denominator, host.at(*it));
      return host.at(numerator) / std::max(denominator, 1.f);
    };
    if (valid_bits == 11) flash_scores_[frame - 1] = ratio(-1, {1, 2});
    if (valid_bits == 11 || valid_bits == 15)
      flash_scores_[frame + 1] = ratio(1, {-1, 2});
    else if (valid_bits == 7)
      flash_scores_[frame + 1] = ratio(1, {-1, -2});
  }
  motions_[frame] = std::move(motion);
  stats_.maximum_buffered_motion_frames = std::max(stats_.maximum_buffered_motion_frames, int(motions_.size()));
  ++next_motion_;
}

Tensor Sequence::render(int frame) {
  Frame& current = frames_.at(frame);
  Tensor output = empty(device_, DType::f32, {h_, w_, 3});
  if (!controls_.enabled) {
    device_.copy(output.buf, current.original.buf, output.bytes());
    return output;
  }
  while (next_motion_ < std::min(count_, frame + 2)) advance();
  // Detection: references in offset order -1, 1, -2, 2 within the shot.
  std::vector<int> offsets;
  std::vector<const Prepared*> references;
  std::vector<Tensor> vectors;
  Motion& motion = motions_.at(frame);
  for (int k : kOffsets) {
    const int r = frame + k;
    if (r >= 0 && r < count_ && shots_.at(r) == shots_.at(frame)) {
      offsets.push_back(k);
      references.push_back(&frames_.at(r).prepared);
      vectors.push_back(motion.vectors.at(k));
    }
  }
  profile().mark(device_, "other");
  FrameDetection detection = detect_frame(device_, current.prepared, references, vectors, offsets, controls_);
  profile().mark(device_, "detect");
  auto flash = flash_scores_.find(frame);
  const float flash_score = flash == flash_scores_.end() ? -1.f : flash->second;
  if (controls_.flash_guard && flash_score > 1.5f) {
    device_.copy(output.buf, current.original.buf, output.bytes());
  } else {
    MotionMasks masks = prepare_motion_masks(device_, detection.detection.mask, detection.detection.confidence);
    std::map<int, Tensor> refined = refine_frame_motion(
        device_, masks, motion.levels, offsets, [&](int k) -> const Prepared& { return frames_.at(frame + k).prepared; },
        w_);
    profile().mark(device_, "refine");
    std::vector<Tensor> reference_features, refined_vectors;
    for (int k : offsets) {
      reference_features.push_back(frames_.at(frame + k).features);
      refined_vectors.push_back(refined.at(k));
    }
    FloatRepair repair = repair_float_frame(device_, current.features, reference_features, refined_vectors, offsets,
                                            detection, current.prepared, references, controls_);
    finish_rgb(device_, repair.output, current.original.buf, h_, w_, controls_, output.buf);
    profile().mark(device_, "repair");
  }
  return output;
}

void Sequence::finish(int frame) {
  if (controls_.enabled) {
    motions_.erase(frame);
    const int keep = std::max(0, frame - 1);
    for (auto it = shots_.begin(); it != shots_.end();) it = it->first < keep ? shots_.erase(it) : std::next(it);
    for (auto it = flash_scores_.begin(); it != flash_scores_.end();)
      it = it->first < keep ? flash_scores_.erase(it) : std::next(it);
  }
  const int keep = controls_.enabled ? std::max(0, frame - 1) : frame + 1;
  for (auto it = frames_.begin(); it != frames_.end();) it = it->first < keep ? frames_.erase(it) : std::next(it);
  // Later frames use only its prepared images and features; free the float source now.
  if (auto it = frames_.find(frame); it != frames_.end()) it->second.original = Tensor();
  ++output_;
  device_.flush();
}

int Sequence::pull_device(Tensor& rgb) {
  if (!ready()) return -1;
  const int frame = output_;
  rgb = render(frame);
  finish(frame);
  ++stats_.output_frames;
  return frame;
}

int Sequence::skip() {
  if (!ready()) return -1;
  const int frame = output_;
  if (controls_.enabled)
    while (next_motion_ < std::min(count_, frame + 2)) advance();
  finish(frame);
  return frame;
}

int Sequence::pull(float* rgb, size_t row_bytes) {
  Tensor out;
  const int frame = pull_device(out);
  if (frame < 0) return -1;
  profile().mark(device_, "other");
  const size_t row = size_t(w_) * 3 * sizeof(float);
  if (row_bytes == row) {
    device_.download(rgb, out.buf, row * h_);
  } else {
    std::vector<float> packed(size_t(w_) * 3 * h_);
    device_.download(packed.data(), out.buf, row * h_);
    for (int y = 0; y < h_; ++y)
      std::memcpy(reinterpret_cast<char*>(rgb) + row_bytes * y, packed.data() + size_t(y) * w_ * 3, row);
  }
  profile().mark(device_, "download");
  return frame;
}

}  // namespace adr
