/* Parameter structs for the "repair" group kernels. */
#ifndef ADR_PARAMS_REPAIR_H
#define ADR_PARAMS_REPAIR_H

typedef struct {
  int h, w;
  int block;     /* 2 or 4: vector block size */
  int channel0;  /* first source channel of the (C,H,W) feature tensor */
  int channels;  /* channels to compensate; output (channels,H,W) */
} RepairCompensateParams;

typedef struct {
  int pixels;
  int count;  /* 0..4 reference planes */
  int force;
} RepairReconstructParams;

typedef struct {
  int h, w;
  int in_place;  /* 1: true division by 255; 0: aligned reciprocal groups */
} RepairBlendParams;

/* Fused compensation, reconstruction and blend over the three feature planes.
 * Reference buffers are bound in repair order (sorted by temporal offset). */
typedef struct {
  int h, w;
  int count;
  int block0, block1, block2, block3;
} RepairFrameParams;

typedef struct {
  int h, w;
  int hdr;        /* 1: HDR analysis codes through the 32768-entry table */
  int smoothing;  /* 0: write the final top-down RGB directly */
  int c0, c1;     /* smoothing: code/blur channels [c0, c1) */
  float g0, g1, g2, g3, g4;  /* five-tap float Gaussian */
} RepairFinishParams;

typedef struct {
  int h, w;
  float g0, g1, g2, g3, g4;
} RepairGaussianParams;

typedef struct {
  int n;
  int channel;
  int channels;
} RepairChannelParams;

#endif
