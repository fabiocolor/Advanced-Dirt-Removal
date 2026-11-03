// Group "detect": byte-domain analysis, detection, repair masks and the
// post-repair contrast mask.
#pragma once

#include "controls.h"
#include "stages/stages.h"

namespace adr {

// ---- Primitive byte filters (also used by the repair group). ----
// compensate_analysis on one channel of an (H,W,C) uint8 image -> (H,W) uint8.
// vectors: int16 per 2x2 or 4x4 block (block size inferred from the array shape).
Tensor compensate_analysis(Device& device, const Tensor& image, int channel, const Tensor& vectors);
// gaussian_analysis_bytes(source, window)
Tensor gaussian_bytes(Device& device, const Tensor& source, int window);
// Weighted residual core with device int32 scalars divisor/rounding (shape {}).
Tensor weighted_residual(Device& device, const Tensor& source, const Tensor& lut, int window, const Tensor& divisor,
                         const Tensor& rounding);
// remap_analysis_bytes with host endpoints.
Tensor remap_bytes(Device& device, const Tensor& source, int input_low, int input_high, int output_low,
                   int output_high);

struct TemporalCombine {
  Tensor base, centered_residual, temporal_spread;  // (H,W) uint8, or (3,H,W) in RGB mode
};
struct NoiseAnalysis {
  Tensor trimmed_mean;  // float32 {} (RGB mode: {3})
  Tensor noise;         // float32 {} (RGB mode: {3})
  Tensor lut;           // uint8 {256} (RGB mode: {3,256})
  Tensor divisor;       // int32 {} (RGB mode: {3})
  Tensor rounding;      // int32 {} (RGB mode: {3})
};
struct DetectionRefinement {
  Tensor residual;    // (H,W) uint8, RGB mode (3,H,W)
  Tensor confidence;  // (H,W) uint8 (RGB mode: channel minimum)
  Tensor mask;        // (H,W) uint8 (RGB mode: channel maximum)
};

// combine_analysis_bytes(current, references) for N in 0..4 stacked planes.
TemporalCombine combine_bytes(Device& device, const Tensor& current, const std::vector<Tensor>& references,
                              float third_weight, float fourth_weight);

// Detection for one frame. offsets are the valid temporal offsets in the
// order -1, 1, -2, 2; references/vectors follow
// that order. In RGB mode the per-channel rgb_planes are used.
struct FrameDetection {
  std::vector<int> offsets;
  TemporalCombine combined;
  NoiseAnalysis noise;
  DetectionRefinement detection;
};
FrameDetection detect_frame(Device& device, const Prepared& current, const std::vector<const Prepared*>& references,
                            const std::vector<Tensor>& vectors, const std::vector<int>& offsets,
                            const Controls& controls);

// ---- Repair masks. ----
// condition_repair_spread; noise_mean is a float32 {} device scalar or null.
Tensor condition_repair_spread(Device& device, const Tensor& spread, const Tensor* noise_mean, int valid_references);
// build_repair_mask -> repair mask (H,W) uint8; noise is the detection noise {}.
Tensor build_repair_mask(Device& device, const Tensor& centered_residual, const Tensor& noise, int valid_references,
                         const Controls& controls);
// excluded_regions(mask, maximum_area): 255 for
// 8-connected components outside [1, maximum_area].
Tensor excluded_regions(Device& device, const Tensor& mask, int maximum_area);
// preservation_mask
Tensor preservation_mask(Device& device, const Tensor& conditioned_spread, int valid_references,
                         const Controls& controls);

// ---- Post-repair contrast (float engine). ----
// quantized codes are int32 (H,W) planes (RGB10 or HDR codes); returns the
// contrast_blend_mask(introduced_contrast(...)) uint8 blend plane.
Tensor contrast_blend(Device& device, const Tensor& repaired_codes, const Tensor& original_codes,
                      const Controls& controls);

}  // namespace adr
