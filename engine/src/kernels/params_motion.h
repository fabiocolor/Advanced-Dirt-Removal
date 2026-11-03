/* Parameter structs for the "motion" stage group (int/float members only). */
#ifndef ADR_PARAMS_MOTION_H
#define ADR_PARAMS_MOTION_H

/* Packed (H,W,4) 8x8 block search and byte-plane block searches. */
typedef struct {
  int h, w;          /* image geometry */
  int gh, gw;        /* block grid */
  int ph, pw;        /* penalty window */
  int block;         /* block size (8 for packed) */
  int scale;         /* vector scale (packed); 1 for byte searches */
  int blend_amount;
  int gated;         /* packed: 1 gate plane, 2 block list; byte: 1 gate plane */
  int threshold;     /* byte: index into the threshold buffer, or -1 for no gate */
  int tpb;           /* threads per block: candidate rows x strips per row */
  int bpg;           /* blocks per work-group (tpb * bpg == group size) */
  int nsx;           /* candidate strips per candidate row */
} MotionSearchParams;

/* Candidate upsampling (packed 8x8 or byte 8/4 blocks). */
typedef struct {
  int h, w, gh, gw, ch, cw;
  int block, channels, units, agreement, early;
  int tpb, bpg;
} MotionUpsampleParams;

typedef struct {
  int h, w, gh, gw;
  int scale, error_threshold, texture_threshold, separation, penalty_span;
  float repair_amount;
  int tpb, bpg;      /* threads per block (divides 64) and blocks per work-group */
} MotionRegularizeParams;

typedef struct {
  int n, quantum_x, quantum_y;
  int gh, gw; /* > 0: also write row candidates of the (gh,gw) field */
} MotionGlobalParams;

typedef struct {
  int gh, gw;
} MotionGridParams;

typedef struct {
  int n;
  int has_noise; /* 1: use the supplied noise scalar instead of the error mean */
} MotionNoiseParams;

typedef struct {
  int h, w, oh, ow;
} MotionReduceParams;

#endif
