/* Advanced Dirt Removal engine: C API.
 *
 * Input/output frames are top-down, tightly packed float32 RGB, declared
 * full-range display-referred Rec.709 in [0,1]. No colour transform, scaling,
 * retiming or clipping is applied to delivered pixels.
 */
#ifndef ADR_ENGINE_H
#define ADR_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(ADR_BUILDING_SHARED)
#define ADR_API __declspec(dllexport)
#else
#define ADR_API
#endif
#else
#define ADR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct adr_context adr_context;

/* backend: "auto", "metal", "cuda", "opencl" or "cpu", optionally ":index".
 * "auto" tries the GPU backends first and falls back to the CPU. */
ADR_API adr_context* adr_context_create(const char* backend, char* error, int error_bytes);
ADR_API void adr_context_destroy(adr_context* context);
ADR_API const char* adr_context_device_name(const adr_context* context);
ADR_API const char* adr_context_backend(const adr_context* context);
ADR_API const char* adr_version(void);
/* Releases GPU buffers held in the reuse pool (live sequences are unaffected). */
ADR_API void adr_context_trim(adr_context* context);
/* Device memory held by the context (live plus pooled buffers) and its peak, in bytes. */
ADR_API void adr_context_memory(const adr_context* context, uint64_t* allocated, uint64_t* peak);
ADR_API const char* adr_kernel_source_sha256(void);
/* Reciprocal-approximation table in use (ADR_RECIPROCAL_TABLE: host, amd or
 * intel), e.g. "host (amd)"; NULL when ADR_RECIPROCAL_TABLE is invalid. Results
 * follow the x86 RCPSS table of the chosen CPU family; pin amd or intel so
 * machines with different CPUs render identical frames. */
ADR_API const char* adr_reciprocal_table(void);

typedef struct adr_controls {
  int32_t enabled;            /* default 1 */
  int32_t mode;               /* 0 Luma, 1 RGB */
  int32_t remove_dark;        /* 1 */
  int32_t remove_light;       /* 1 */
  float repair_amount;        /* 0..2, 1 */
  float analysis_detail;      /* 0..1, 0.5 */
  float sensitivity;          /* 0..1, 0.75 */
  int32_t extra_sensitivity;  /* 0 */
  float motion_guard;         /* 0..1, 0.25 */
  int32_t shot_changes;       /* 0 Off, 1 Guard, 2 Skip; 1 */
  float shot_guard;           /* 0..1, 0.75 */
  int32_t blend_repairs;      /* 1 */
  float blend_amount;         /* 0..1, 0.5 */
  int32_t extended_range;     /* 0 */
  int32_t flash_guard;        /* 0 */
  int32_t limit_spot_size;    /* 0 */
  int32_t largest_spot;       /* 1..256, 25 */
} adr_controls;

ADR_API void adr_controls_default(adr_controls* controls);

/* Streaming sequence renderer. Source frames must be supplied in order from
 * frame zero; output for frame n becomes available once frames up to n+3 (or
 * the clip end) have been pushed. */
typedef struct adr_sequence adr_sequence;
ADR_API adr_sequence* adr_sequence_create(adr_context* context, const adr_controls* controls, int width,
                                       int height, int frame_count, char* error, int error_bytes);
ADR_API void adr_sequence_destroy(adr_sequence* sequence);
/* Motion-history checkpoint store shared by sequences over the same clip,
 * controls and geometry. A sequence created with a store records a checkpoint
 * per motion frame (a few KB of GPU memory each); a later sequence can resume
 * at any checkpoint and reproduce every following output exactly. */
typedef struct adr_history adr_history;
ADR_API adr_history* adr_history_create(void);
ADR_API void adr_history_destroy(adr_history* history);
/* Latest checkpointed frame <= frame (0 when none). */
ADR_API int adr_history_resume_frame(const adr_history* history, int frame);
/* Like adr_sequence_create, recording into history. Starts at the latest
 * checkpoint <= first_output (0 when none); adr_sequence_next_output reports it. */
ADR_API adr_sequence* adr_sequence_create_resumable(adr_context* context, const adr_controls* controls, int width,
                                                 int height, int frame_count, adr_history* history,
                                                 int first_output, char* error, int error_bytes);
/* Index of the next source frame to push, or -1 when all are loaded. */
ADR_API int adr_sequence_next_source(const adr_sequence* sequence);
ADR_API int adr_sequence_next_output(const adr_sequence* sequence);
ADR_API int adr_sequence_push(adr_sequence* sequence, const float* rgb, size_t row_bytes, char* error,
                            int error_bytes);
/* Advances past the next output frame without rendering it (forward seeks);
 * returns its index or -1 when more source frames are needed. */
ADR_API int adr_sequence_skip(adr_sequence* sequence, char* error, int error_bytes);
/* Writes the next output frame; returns its index or -1 when unavailable. */
ADR_API int adr_sequence_pull(adr_sequence* sequence, float* rgb, size_t row_bytes, char* error,
                            int error_bytes);

/* ---- Auto recovery ---- */
/* A small classifier that estimates, for every spot the cleaning changed,
 * whether it was picture detail to give back.
 * model_path: the recovery model file (weights under WEIGHTS_LICENSE). */
typedef struct adr_recovery adr_recovery;
ADR_API adr_recovery* adr_recovery_create(adr_context* context, const char* model_path, char* error, int error_bytes);
ADR_API void adr_recovery_destroy(adr_recovery* recovery);
/* Input encoding of the RGB values. */
enum { ADR_RECOVERY_INPUT_VIDEO = 0, ADR_RECOVERY_INPUT_CINEON_LINEAR = 1 };
/* prev/current/next: source frames; cleaned: the engine's output for current. Top-down float32 RGB rows,
 * row_bytes apart. probability: width*height bytes, 0 where nothing was repaired, otherwise
 * 1 + round(p * 254) for the recovery probability p of the spot covering the pixel.
 * Returns the number of spots scored, or -1 on error. */
ADR_API int adr_recovery_frame(adr_recovery* recovery, const float* prev, const float* current, const float* next,
                               const float* cleaned, int width, int height, size_t row_bytes, int input_encoding,
                               unsigned char* probability, char* error, int error_bytes);

/* Development: classify count prepared crops (count x 13 x 160 x 160 bytes) into probabilities. */
ADR_API int adr_recovery_classify(adr_recovery* recovery, const unsigned char* crops, int count, float* probabilities,
                                  char* error, int error_bytes);

/* ---- Development API: run one stage on host arrays. ---- */
enum { ADR_U8 = 0, ADR_I8 = 1, ADR_I16 = 2, ADR_U16 = 3, ADR_I32 = 4, ADR_F32 = 5 };
typedef struct adr_array {
  void* data;
  int32_t dtype;
  int32_t ndim;
  int64_t shape[6];
} adr_array;

/* Outputs are allocated by the library; release each with adr_array_free. */
ADR_API int adr_debug_stage(adr_context* context, const char* stage, const adr_array* inputs, int input_count,
                          const double* scalars, int scalar_count, adr_array* outputs, int output_capacity,
                          int* output_count, char* error, int error_bytes);
ADR_API void adr_array_free(adr_array* array);
ADR_API int adr_debug_stage_names(char* buffer, int buffer_bytes);

#ifdef __cplusplus
}
#endif
#endif
