/* Kernel parameter structs shared by host C++ and every kernel dialect.
 * Members are 32-bit int/float only so layouts agree across compilers. */
#ifndef ADR_PARAMS_H
#define ADR_PARAMS_H

typedef struct {
  int h, w;        /* analysis (output) geometry */
  int flip;        /* 1: input rows are top-down and are read bottom-up */
  int in_stride;   /* input row stride in floats */
  int rgb_mode;    /* also write quantized RGB analysis planes */
  int hdr;         /* extended-range analysis curve */
} PrepareParams;

typedef struct {
  int h, w;        /* source geometry */
  int oh, ow;      /* output geometry */
  int mode;        /* reduction kernel 0, 1 or 2 */
} ReduceParams;

typedef struct {
  int h, w;        /* source geometry */
  int side, center;
} DoubleParams;

typedef struct {
  int n;
} SizeParams;

typedef struct {
  int n, offset;
  unsigned int value;
} FillParams;

#endif
