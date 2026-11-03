/* Parameter structs of the "refine" group (motion masks and refinement). */
#ifndef ADR_PARAMS_REFINE_H
#define ADR_PARAMS_REFINE_H

typedef struct {
  int h, w;       /* source plane geometry */
  int oh, ow;     /* reduced geometry */
  int threshold;  /* byte-narrowed binary threshold */
} RefineReduceParams;

typedef struct {
  int sh, sw;       /* source plane geometry */
  int gh, gw;       /* block grid */
  int block;        /* 2, 4 or 8 */
  int fh, fw;       /* finer class grid (gate) */
  int has_finer;
} RefineClassifyParams;

typedef struct {
  int ph, pw;       /* confidence plane geometry */
  int gh, gw;       /* block grid */
  int block;
  int qh, qw;       /* coarser (parent) class grid */
  int has_parent;
} RefineFinalizeParams;

typedef struct {
  int n;            /* blocks in the grid */
} RefineCompactParams;

typedef struct {
  int h, w;         /* packed image geometry */
  int gh, gw;       /* block grid */
  int ch, cw;       /* coarse field geometry */
  int block, units, level, threshold, mode;
  int has_coarse, valid0, valid1;
  int groups;       /* launched work-groups (grid-stride over active blocks) */
} RefineParams;

#endif
