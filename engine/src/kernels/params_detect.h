/* Parameter structs for the "detect" stage group (int/float members only). */
#ifndef ADR_PARAMS_DETECT_H
#define ADR_PARAMS_DETECT_H

/* compensate: one channel of an interleaved (H,W,cstride) byte image; z = channel. */
typedef struct {
  int h, w, block;
  int cstride, c0;  /* source channel stride and first channel */
  int nchan;
} DetectCompensateParams;

/* combine: current interleaved (cstride, c0), references planar (C,n); z = channel. */
typedef struct {
  int n, count, nchan;
  int cstride, c0;
  float third, fourth;
} DetectCombineParams;

/* 8x8 difference blocks and histogram; z = channel. */
typedef struct {
  int h, w, rows, cols;
  int cstride, c0, nchan;
} DetectBlocksParams;

/* noise estimate / repair LUT scalars: one thread per channel. */
typedef struct {
  int nchan, mode1, remove_dark, remove_light;
  float multiplier, scale;
  int blend, pad0;   /* repair: blend towards the raw noise (repair_weight > .5) */
  float value, pad1; /* repair: repair_weight */
} DetectScalarParams;

/* weighted residual: mode 0 plain, 1 clamp + absolute output, 2 clamp + gate. */
typedef struct {
  int h, w, window, center, nchan;
  int lo, hi, mode;
} DetectWeightedParams;

/* separable Gaussian passes: mode 0 plain, 1 amplify/average (horizontal),
 * 2 energy/confidence/mask (horizontal), 3 mask refinement (horizontal). */
typedef struct {
  int h, w, window, nchan, mode;
  int in_low, span, out_low, coarse, fractional;
  float scale, rcp8192;
} DetectGaussParams;

typedef struct {
  int n;
  int in_low, span, out_low, coarse, fractional;
} DetectRemapParams;

typedef struct {
  int n;
  float divisor;
} DetectConditionParams;

typedef struct {
  int h, w;
} DetectPlaneParams;

typedef struct {
  int h, w, maximum_area, mode; /* mode 0: 255 for excluded, 1: zero excluded target pixels */
} DetectComponentParams;

typedef struct {
  int h, w;
  float denominator, factor;
  int base_low, base_high;
} DetectContrastParams;

#endif
