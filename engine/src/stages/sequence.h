// Group "sequence": temporal scheduling, cut/flash decisions and the bounded
// streaming renderer.
#pragma once

#include <map>
#include <memory>

#include "controls.h"
#include "stages/detect.h"
#include "stages/motion.h"
#include "stages/stages.h"

namespace adr {

struct SequenceStatistics {
  int source_frames_read = 0;
  int output_frames = 0;
  int maximum_buffered_source_frames = 0;
  int maximum_buffered_motion_frames = 0;
};

// Motion-history checkpoints. The complete motion-history state before motion
// frame k is the four retained history fields plus the cut and flash
// decisions near k. Fields are immutable device tensors, so a checkpoint
// shares their buffers. A Sequence started from checkpoint k reproduces every
// output >= k exactly, after replaying only source frames k-2..k+3.
struct HistoryCheckpoint {
  std::map<int, Tensor> history;
  std::map<int, int> shots;
  std::map<int, float> flash_scores;
};
struct HistoryStore {
  std::map<int, HistoryCheckpoint> checkpoints;  // by motion frame
};

class Sequence {
 public:
  Sequence(Device& device, const Controls& controls, int width, int height, int count,
           std::shared_ptr<HistoryStore> store = nullptr, int start = 0);

  // Index of the next source frame to push, or -1 once all are loaded.
  int next_source() const { return loaded_ < count_ ? loaded_ : -1; }
  // Index of the next output frame, or -1 when the sequence is complete.
  int next_output() const { return output_ < count_ ? output_ : -1; }
  // Latest checkpointed frame <= frame in store (0 when none).
  static int resume_frame(const HistoryStore& store, int frame);
  // True when next_output() can be produced with the frames pushed so far.
  bool ready() const;

  // Pushes source frame next_source(): top-down float32 RGB rows.
  void push(const float* rgb, size_t row_bytes);
  // Device-resident variant; the buffer is copied.
  void push_device(const Buffer& rgb);
  // Produces next_output() into host rows; returns its index.
  int pull(float* rgb, size_t row_bytes);
  // Produces next_output() and leaves it on the device (top-down RGB).
  int pull_device(Tensor& rgb);
  // Advances past next_output() without rendering it: motion history and cut
  // state advance exactly as for a rendered frame. Returns the skipped index.
  int skip();

  const SequenceStatistics& statistics() const { return stats_; }
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  struct Frame {
    Tensor original;  // top-down (H,W,3) float32
    Prepared prepared;
    Tensor features;  // bottom-up (3,H,W) float32
  };
  struct Motion {
    std::map<int, Tensor> vectors;
    std::map<int, std::vector<Tensor>> levels;
  };

  void load(const Buffer& rgb);
  void advance();
  Tensor render(int frame);
  void finish(int frame);

  Device& device_;
  Controls controls_;
  int w_, h_, count_, levels_;
  int loaded_ = 0, output_ = 0, next_motion_ = 0;
  std::map<int, Frame> frames_;
  std::map<int, Motion> motions_;
  std::map<int, Tensor> history_;  // per offset
  std::map<int, int> shots_;
  std::map<int, float> flash_scores_;
  std::shared_ptr<HistoryStore> store_;
  SequenceStatistics stats_;
};

}  // namespace adr
