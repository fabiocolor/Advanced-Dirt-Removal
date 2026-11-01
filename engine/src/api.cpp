// C API: contexts, development stage execution and the streaming renderer.
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "adr/adr.h"
#include "kernels/kernel_source.h"
#include "rcpss.h"
#include "tensor.h"
#if ADR_HAVE_SEQUENCE
#include "stages/sequence.h"
#endif

namespace adr {

static std::map<std::string, StageFn>& registry() {
  static std::map<std::string, StageFn> stages;
  return stages;
}
void register_stage(const std::string& name, StageFn fn) { registry()[name] = std::move(fn); }
const StageFn* find_stage(const std::string& name) {
  auto it = registry().find(name);
  return it == registry().end() ? nullptr : &it->second;
}
std::vector<std::string> stage_names() {
  std::vector<std::string> names;
  for (auto& entry : registry()) names.push_back(entry.first);
  return names;
}

}  // namespace adr

struct adr_context {
  std::unique_ptr<adr::Device> device;
  std::string name, backend;
  std::mutex mutex;
};

static void set_error(char* error, int bytes, const char* message) {
  if (!error || bytes <= 0) return;
  std::strncpy(error, message, size_t(bytes) - 1);
  error[bytes - 1] = 0;
}

extern "C" {

ADR_API const char* adr_version(void) { return "2.0.0"; }
ADR_API const char* adr_kernel_source_sha256(void) { return adr::kernel_source_sha256(); }
ADR_API const char* adr_reciprocal_table(void) {
  try {
    return adr::rcpss_table().source.c_str();
  } catch (const std::exception&) {
    return nullptr;
  }
}

ADR_API adr_context* adr_context_create(const char* backend, char* error, int error_bytes) {
  try {
    adr::rcpss_table();  // reject an invalid ADR_RECIPROCAL_TABLE before any render
    auto context = std::make_unique<adr_context>();
    context->device = adr::create_device(backend ? backend : "auto");
    context->name = context->device->name();
    context->backend = context->device->backend();
    return context.release();
  } catch (const std::exception& e) {
    set_error(error, error_bytes, e.what());
    return nullptr;
  }
}

ADR_API void adr_context_destroy(adr_context* context) { delete context; }
ADR_API void adr_context_trim(adr_context* context) {
  if (!context) return;
  std::lock_guard<std::mutex> lock(context->mutex);
  try {
    context->device->trim();
  } catch (...) {
  }
}
ADR_API void adr_context_memory(const adr_context* context, uint64_t* allocated, uint64_t* peak) {
  if (allocated) *allocated = context->device->allocated_bytes();
  if (peak) *peak = context->device->peak_bytes();
}
ADR_API const char* adr_context_device_name(const adr_context* context) { return context->name.c_str(); }
ADR_API const char* adr_context_backend(const adr_context* context) { return context->backend.c_str(); }

ADR_API int adr_debug_stage(adr_context* context, const char* stage, const adr_array* inputs, int input_count,
                          const double* scalars, int scalar_count, adr_array* outputs, int output_capacity,
                          int* output_count, char* error, int error_bytes) {
  try {
    std::lock_guard<std::mutex> lock(context->mutex);
    const adr::StageFn* fn = adr::find_stage(stage);
    if (!fn) throw adr::Error(std::string("Unknown stage: ") + stage);
    adr::Device& device = *context->device;
    std::vector<adr::Tensor> in;
    for (int i = 0; i < input_count; ++i) {
      std::vector<int64_t> shape(inputs[i].shape, inputs[i].shape + inputs[i].ndim);
      in.push_back(adr::upload(device, adr::DType(inputs[i].dtype), shape, inputs[i].data));
    }
    std::vector<double> s(scalars, scalars + scalar_count);
    std::vector<adr::Tensor> out = (*fn)(device, in, s);
    if (int(out.size()) > output_capacity) throw adr::Error("Too many stage outputs");
    device.synchronize();
    for (size_t i = 0; i < out.size(); ++i) {
      adr_array& a = outputs[i];
      a.dtype = int32_t(out[i].dtype);
      a.ndim = int32_t(out[i].shape.size());
      for (size_t d = 0; d < out[i].shape.size(); ++d) a.shape[d] = out[i].shape[d];
      a.data = std::malloc(out[i].bytes() ? out[i].bytes() : 1);
      if (out[i].bytes()) device.download(a.data, out[i].buf, out[i].bytes());
    }
    *output_count = int(out.size());
    return 0;
  } catch (const std::exception& e) {
    set_error(error, error_bytes, e.what());
    return -1;
  }
}

ADR_API void adr_array_free(adr_array* array) {
  if (array && array->data) std::free(array->data);
  if (array) array->data = nullptr;
}

ADR_API int adr_debug_stage_names(char* buffer, int buffer_bytes) {
  std::string all;
  for (auto& n : adr::stage_names()) all += n + "\n";
  if (buffer && buffer_bytes > 0) set_error(buffer, buffer_bytes, all.c_str());
  return int(all.size());
}

#if ADR_HAVE_SEQUENCE
}  // extern "C"

struct adr_sequence {
  adr_context* context;
  std::unique_ptr<adr::Sequence> sequence;
};

extern "C" {

ADR_API adr_sequence* adr_sequence_create(adr_context* context, const adr_controls* controls, int width, int height,
                                       int frame_count, char* error, int error_bytes) {
  try {
    std::lock_guard<std::mutex> lock(context->mutex);
    adr_controls defaults;
    adr_controls_default(&defaults);
    auto seq = std::make_unique<adr_sequence>();
    seq->context = context;
    seq->sequence = std::make_unique<adr::Sequence>(*context->device, adr::Controls::from_c(controls ? *controls : defaults),
                                                   width, height, frame_count);
    return seq.release();
  } catch (const std::exception& e) {
    set_error(error, error_bytes, e.what());
    return nullptr;
  }
}

}  // extern "C"
struct adr_history {
  std::shared_ptr<adr::HistoryStore> store = std::make_shared<adr::HistoryStore>();
};
extern "C" {

ADR_API adr_history* adr_history_create(void) { return new adr_history(); }
ADR_API void adr_history_destroy(adr_history* history) { delete history; }
ADR_API int adr_history_resume_frame(const adr_history* history, int frame) {
  return history ? adr::Sequence::resume_frame(*history->store, frame) : 0;
}

ADR_API adr_sequence* adr_sequence_create_resumable(adr_context* context, const adr_controls* controls, int width,
                                                 int height, int frame_count, adr_history* history,
                                                 int first_output, char* error, int error_bytes) {
  try {
    std::lock_guard<std::mutex> lock(context->mutex);
    adr_controls defaults;
    adr_controls_default(&defaults);
    const adr::Controls k = adr::Controls::from_c(controls ? *controls : defaults);
    int start = 0;
    if (history && first_output > 0) {
      start = k.enabled ? adr::Sequence::resume_frame(*history->store, first_output) : first_output;
      start = std::min(start, frame_count - 1);
    }
    auto seq = std::make_unique<adr_sequence>();
    seq->context = context;
    seq->sequence = std::make_unique<adr::Sequence>(*context->device, k, width, height, frame_count,
                                                   history ? history->store : nullptr, std::max(0, start));
    return seq.release();
  } catch (const std::exception& e) {
    set_error(error, error_bytes, e.what());
    return nullptr;
  }
}

ADR_API void adr_sequence_destroy(adr_sequence* sequence) {
  if (!sequence) return;
  std::lock_guard<std::mutex> lock(sequence->context->mutex);
  delete sequence;
}

ADR_API int adr_sequence_next_source(const adr_sequence* s) { return s->sequence->next_source(); }
ADR_API int adr_sequence_next_output(const adr_sequence* s) { return s->sequence->next_output(); }

ADR_API int adr_sequence_push(adr_sequence* s, const float* rgb, size_t row_bytes, char* error, int error_bytes) {
  try {
    std::lock_guard<std::mutex> lock(s->context->mutex);
    s->sequence->push(rgb, row_bytes);
    return 0;
  } catch (const std::exception& e) {
    set_error(error, error_bytes, e.what());
    return -1;
  }
}

ADR_API int adr_sequence_skip(adr_sequence* s, char* error, int error_bytes) {
  try {
    std::lock_guard<std::mutex> lock(s->context->mutex);
    return s->sequence->skip();
  } catch (const std::exception& e) {
    set_error(error, error_bytes, e.what());
    return -2;
  }
}

ADR_API int adr_sequence_pull(adr_sequence* s, float* rgb, size_t row_bytes, char* error, int error_bytes) {
  try {
    std::lock_guard<std::mutex> lock(s->context->mutex);
    return s->sequence->pull(rgb, row_bytes);
  } catch (const std::exception& e) {
    set_error(error, error_bytes, e.what());
    return -2;
  }
}
#endif

ADR_API void adr_controls_default(adr_controls* c) {
  *c = adr_controls{1, 0, 1, 1, 1.f, .5f, .75f, 0, .25f, 1, .75f, 1, .5f, 0, 0, 0, 25};
}

}  // extern "C"
