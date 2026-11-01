/* Parameter structs of the "nn" group: the auto-recovery classifier (int/float members only). */
#ifndef ADR_PARAMS_NN_H
#define ADR_PARAMS_NN_H

typedef struct {
  int batch, ci, hi, wi;   /* input (B,Ci,Hi,Wi) */
  int co, ho, wo;          /* output (B,Co,Ho,Wo) */
  int k, stride, pad;      /* square kernel */
  int relu, residual;      /* epilogue: add residual (B,Co,Ho,Wo), then ReLU */
} NnConvParams;

typedef struct {
  int batch, c, hi, wi, ho, wo;
} NnPoolParams;

typedef struct {
  int batch, c, hw;        /* global average over hw positions, then one dot product */
  float bias;
} NnHeadParams;

#endif
