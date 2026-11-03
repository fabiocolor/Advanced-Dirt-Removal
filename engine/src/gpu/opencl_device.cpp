// OpenCL 1.2 backend for AMD, Intel and other non-CUDA GPUs on Windows/Linux.
// The ICD loader is opened at runtime, so no OpenCL SDK is needed to build and
// the plugin still loads where no OpenCL runtime is installed.
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "gpu/device.h"
#include "kernels/kernel_source.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define ADR_CL_API __stdcall
#else
#include <dlfcn.h>
#define ADR_CL_API
#endif

namespace adr {
namespace {

struct FillWords {
  int n, offset;
  uint32_t value;
};

// Minimal OpenCL 1.2 declarations (Khronos ABI).
typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef uint32_t cl_bool;
typedef uint64_t cl_bitfield;
typedef cl_bitfield cl_device_type;
typedef cl_bitfield cl_mem_flags;
typedef cl_bitfield cl_device_fp_config;
typedef cl_bitfield cl_command_queue_properties;
typedef struct _cl_platform_id* cl_platform_id;
typedef struct _cl_device_id* cl_device_id;
typedef struct _cl_context* cl_context;
typedef struct _cl_command_queue* cl_command_queue;
typedef struct _cl_mem* cl_mem;
typedef struct _cl_program* cl_program;
typedef struct _cl_kernel* cl_kernel;
typedef struct _cl_event* cl_event;
typedef intptr_t cl_context_properties;

constexpr cl_int CL_SUCCESS = 0;
constexpr cl_device_type CL_DEVICE_TYPE_GPU = 1 << 2;
constexpr cl_uint CL_DEVICE_NAME = 0x102B;
constexpr cl_uint CL_DEVICE_VENDOR = 0x102C;
constexpr cl_uint CL_DEVICE_SINGLE_FP_CONFIG = 0x101B;
constexpr cl_uint CL_DEVICE_GLOBAL_MEM_SIZE = 0x101F;
constexpr cl_uint CL_DEVICE_HOST_UNIFIED_MEMORY = 0x1035;
constexpr cl_device_fp_config CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT = 1 << 7;
constexpr cl_uint CL_PROGRAM_BUILD_LOG = 0x1183;
constexpr cl_mem_flags CL_MEM_READ_WRITE = 1 << 0;
constexpr cl_uint CL_TRUE = 1;

struct Api {
  cl_int(ADR_CL_API* GetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
  cl_int(ADR_CL_API* GetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
  cl_int(ADR_CL_API* GetDeviceInfo)(cl_device_id, cl_uint, size_t, void*, size_t*);
  cl_context(ADR_CL_API* CreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*, void*, void*,
                                       cl_int*);
  cl_command_queue(ADR_CL_API* CreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int*);
  cl_program(ADR_CL_API* CreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
  cl_int(ADR_CL_API* BuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*);
  cl_int(ADR_CL_API* GetProgramBuildInfo)(cl_program, cl_device_id, cl_uint, size_t, void*, size_t*);
  cl_kernel(ADR_CL_API* CreateKernel)(cl_program, const char*, cl_int*);
  cl_int(ADR_CL_API* SetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
  cl_int(ADR_CL_API* EnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*,
                                          const size_t*, cl_uint, const cl_event*, cl_event*);
  cl_mem(ADR_CL_API* CreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*);
  cl_int(ADR_CL_API* ReleaseMemObject)(cl_mem);
  cl_int(ADR_CL_API* EnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, const void*, cl_uint,
                                        const cl_event*, cl_event*);
  cl_int(ADR_CL_API* EnqueueReadBuffer)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, void*, cl_uint,
                                       const cl_event*, cl_event*);
  cl_int(ADR_CL_API* EnqueueCopyBuffer)(cl_command_queue, cl_mem, cl_mem, size_t, size_t, size_t, cl_uint,
                                       const cl_event*, cl_event*);
  cl_int(ADR_CL_API* EnqueueFillBuffer)(cl_command_queue, cl_mem, const void*, size_t, size_t, size_t, cl_uint,
                                       const cl_event*, cl_event*);
  cl_int(ADR_CL_API* Flush)(cl_command_queue);
  cl_int(ADR_CL_API* Finish)(cl_command_queue);
  cl_int(ADR_CL_API* ReleaseKernel)(cl_kernel);
  cl_int(ADR_CL_API* ReleaseProgram)(cl_program);
  cl_int(ADR_CL_API* ReleaseCommandQueue)(cl_command_queue);
  cl_int(ADR_CL_API* ReleaseContext)(cl_context);
};

const Api& api() {
  static Api table;
  static bool loaded = false;
  static std::string failure;
  if (loaded) return table;
  if (!failure.empty()) throw Error(failure);
#if defined(_WIN32)
  HMODULE library = LoadLibraryA("OpenCL.dll");
  auto symbol = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(library, name)); };
#elif defined(__APPLE__)
  void* library = dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
  auto symbol = [&](const char* name) { return dlsym(library, name); };
#else
  void* library = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!library) library = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
  auto symbol = [&](const char* name) { return dlsym(library, name); };
#endif
  if (!library) {
    failure = "OpenCL runtime not found";
    throw Error(failure);
  }
  bool ok = true;
  auto bind = [&](auto& fn, const char* name) {
    void* address = symbol(name);
    if (!address) ok = false;
    std::memcpy(&fn, &address, sizeof(void*));
  };
  bind(table.GetPlatformIDs, "clGetPlatformIDs");
  bind(table.GetDeviceIDs, "clGetDeviceIDs");
  bind(table.GetDeviceInfo, "clGetDeviceInfo");
  bind(table.CreateContext, "clCreateContext");
  bind(table.CreateCommandQueue, "clCreateCommandQueue");
  bind(table.CreateProgramWithSource, "clCreateProgramWithSource");
  bind(table.BuildProgram, "clBuildProgram");
  bind(table.GetProgramBuildInfo, "clGetProgramBuildInfo");
  bind(table.CreateKernel, "clCreateKernel");
  bind(table.SetKernelArg, "clSetKernelArg");
  bind(table.EnqueueNDRangeKernel, "clEnqueueNDRangeKernel");
  bind(table.CreateBuffer, "clCreateBuffer");
  bind(table.ReleaseMemObject, "clReleaseMemObject");
  bind(table.EnqueueWriteBuffer, "clEnqueueWriteBuffer");
  bind(table.EnqueueReadBuffer, "clEnqueueReadBuffer");
  bind(table.EnqueueCopyBuffer, "clEnqueueCopyBuffer");
  bind(table.EnqueueFillBuffer, "clEnqueueFillBuffer");
  bind(table.Flush, "clFlush");
  bind(table.Finish, "clFinish");
  bind(table.ReleaseKernel, "clReleaseKernel");
  bind(table.ReleaseProgram, "clReleaseProgram");
  bind(table.ReleaseCommandQueue, "clReleaseCommandQueue");
  bind(table.ReleaseContext, "clReleaseContext");
  if (!ok) {
    failure = "OpenCL 1.2 entry points missing";
    throw Error(failure);
  }
  loaded = true;
  return table;
}

void check(cl_int status, const char* what) {
  if (status != CL_SUCCESS) throw Error(std::string("OpenCL ") + what + " failed: " + std::to_string(status));
}

std::string device_string(cl_device_id device, cl_uint what) {
  size_t size = 0;
  api().GetDeviceInfo(device, what, 0, nullptr, &size);
  std::string text(size, '\0');
  api().GetDeviceInfo(device, what, size, text.data(), nullptr);
  while (!text.empty() && text.back() == '\0') text.pop_back();
  return text;
}

class OpenCLDevice final : public Device {
 public:
  explicit OpenCLDevice(cl_device_id device) {
    const Api& cl = api();
    device_ = device;
    name_ = device_string(device_, CL_DEVICE_NAME) + " (" + device_string(device_, CL_DEVICE_VENDOR) + ")";
    try {
      build();
    } catch (...) {
      if (program_) cl.ReleaseProgram(program_);
      if (queue_) cl.ReleaseCommandQueue(queue_);
      if (context_) cl.ReleaseContext(context_);
      throw;
    }
  }

 private:
  void build() {
    const Api& cl = api();
    cl_device_fp_config fp = 0;
    cl.GetDeviceInfo(device_, CL_DEVICE_SINGLE_FP_CONFIG, sizeof(fp), &fp, nullptr);
    cl_int status = 0;
    context_ = cl.CreateContext(nullptr, 1, &device_, nullptr, nullptr, &status);
    check(status, "context creation");
    queue_ = cl.CreateCommandQueue(context_, device_, 0, &status);
    check(status, "queue creation");
    std::string source = kernel_source();
    const char* text = source.c_str();
    size_t length = source.size();
    program_ = cl.CreateProgramWithSource(context_, 1, &text, &length, &status);
    check(status, "program creation");
    // Exact IEEE division/sqrt keep backends identical; no relaxed-math flags.
    std::string options = "-cl-std=CL1.2 -DADR_OPENCL=1";
    if (fp & CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT) options += " -cl-fp32-correctly-rounded-divide-sqrt";
    else throw Error("OpenCL device lacks correctly rounded float32 divide/sqrt: " + name_);
    status = cl.BuildProgram(program_, 1, &device_, options.c_str(), nullptr, nullptr);
    if (status != CL_SUCCESS) {
      size_t size = 0;
      cl.GetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size);
      std::string log(size, '\0');
      cl.GetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, size, log.data(), nullptr);
      throw Error("OpenCL kernel compilation failed: " + log.substr(0, 4000));
    }
  }

 public:

  ~OpenCLDevice() override {
    const Api& cl = api();
    try {
      synchronize();
    } catch (...) {
    }
    release_resources();
    for (cl_kernel k : kernels_) cl.ReleaseKernel(k);
    if (program_) cl.ReleaseProgram(program_);
    if (queue_) cl.ReleaseCommandQueue(queue_);
    if (context_) cl.ReleaseContext(context_);
  }

  std::string name() const override { return name_; }
  std::string backend() const override { return "opencl"; }
  void* native_queue() const override { return queue_; }

  void upload(const Buffer& dst, const void* src, size_t bytes, size_t offset) override {
    if (!bytes) return;
    // Blocking write: the host buffer may be released after return, and the
    // in-order queue keeps earlier readers of a recycled buffer ahead of it.
    check(api().EnqueueWriteBuffer(queue_, mem(dst), CL_TRUE, offset, bytes, src, 0, nullptr, nullptr), "write");
  }

  void download(void* dst, const Buffer& src, size_t bytes, size_t offset) override {
    if (!bytes) return;
    check(api().EnqueueReadBuffer(queue_, mem(src), CL_TRUE, offset, bytes, dst, 0, nullptr, nullptr), "read");
  }

  void fill(const Buffer& dst, uint8_t value, size_t bytes, size_t offset) override {
    if (!bytes) return;
    // Apple's OpenCL ignores clEnqueueFillBuffer; fill aligned words with a
    // kernel and the rare unaligned remainder with a blocking write.
    size_t head = std::min(bytes, (4 - offset % 4) % 4), words = (bytes - head) / 4, tail = bytes - head - words * 4;
    if (head) write_bytes(dst, offset, head, value);
    if (words) {
      FillWords p{int(words), int((offset + head) / 4), 0x01010101u * value};
      launch(kernel("adr_fill_words"), Dim3{int(words), 1, 1}, Dim3{256, 1, 1}, {&dst}, &p, sizeof(p));
    }
    if (tail) write_bytes(dst, offset + head + words * 4, tail, value);
  }

  void write_bytes(const Buffer& dst, size_t offset, size_t bytes, uint8_t value) {
    std::vector<uint8_t> host(bytes, value);
    check(api().EnqueueWriteBuffer(queue_, mem(dst), CL_TRUE, offset, bytes, host.data(), 0, nullptr, nullptr), "fill");
  }

  void copy(const Buffer& dst, const Buffer& src, size_t bytes, size_t dst_offset, size_t src_offset) override {
    if (!bytes) return;
    check(api().EnqueueCopyBuffer(queue_, mem(src), mem(dst), src_offset, dst_offset, bytes, 0, nullptr, nullptr),
          "copy");
  }

  void launch(KernelHandle* kernel, Dim3 threads, Dim3 group, std::initializer_list<const Buffer*> buffers,
              const void* params, size_t params_bytes) override {
    if (threads.x <= 0 || threads.y <= 0 || threads.z <= 0) return;
    const Api& cl = api();
    cl_kernel k = static_cast<cl_kernel>(kernel->native);
    cl_uint index = 0;
    for (const Buffer* b : buffers) {
      cl_mem m = (b && *b) ? mem(*b) : nullptr;
      check(cl.SetKernelArg(k, index++, sizeof(cl_mem), &m), "argument");
    }
    if (params_bytes) check(cl.SetKernelArg(k, index, params_bytes, params), "parameters");
    const size_t local[3] = {size_t(group.x), size_t(group.y), size_t(group.z)};
    const size_t global[3] = {size_t((threads.x + group.x - 1) / group.x) * local[0],
                              size_t((threads.y + group.y - 1) / group.y) * local[1],
                              size_t((threads.z + group.z - 1) / group.z) * local[2]};
    cl_int status = cl.EnqueueNDRangeKernel(queue_, k, 3, nullptr, global, local, 0, nullptr, nullptr);
    if (status != CL_SUCCESS) throw Error("OpenCL launch of " + kernel->name + " failed: " + std::to_string(status));
  }

  void flush() override { check(api().Flush(queue_), "flush"); }
  void synchronize() override { check(api().Finish(queue_), "finish"); }

 protected:
  BufferHandle allocate_native(size_t bytes) override {
    cl_int status = 0;
    cl_mem m = api().CreateBuffer(context_, CL_MEM_READ_WRITE, bytes, nullptr, &status);
    check(status, "allocation");
    BufferHandle handle;
    handle.native = m;
    handle.bytes = bytes;
    return handle;
  }

  void release_native(BufferHandle handle) override { api().ReleaseMemObject(static_cast<cl_mem>(handle.native)); }

  void* load_kernel(const std::string& name) override {
    cl_int status = 0;
    cl_kernel k = api().CreateKernel(program_, name.c_str(), &status);
    if (status != CL_SUCCESS) return nullptr;
    kernels_.push_back(k);
    return k;
  }

 private:
  static cl_mem mem(const Buffer& b) { return static_cast<cl_mem>(b.native()); }

  cl_device_id device_ = nullptr;
  cl_context context_ = nullptr;
  cl_command_queue queue_ = nullptr;
  cl_program program_ = nullptr;
  std::vector<cl_kernel> kernels_;
  std::string name_;
};

}  // namespace

namespace {

std::vector<cl_device_id> opencl_gpus() {
  const Api& cl = api();
  cl_uint platform_count = 0;
  check(cl.GetPlatformIDs(0, nullptr, &platform_count), "platform query");
  std::vector<cl_platform_id> platforms(platform_count);
  check(cl.GetPlatformIDs(platform_count, platforms.data(), nullptr), "platform query");
  std::vector<cl_device_id> gpus;
  for (cl_platform_id platform : platforms) {
    cl_uint count = 0;
    if (cl.GetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &count) != CL_SUCCESS || !count) continue;
    std::vector<cl_device_id> devices(count);
    cl.GetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, count, devices.data(), nullptr);
    gpus.insert(gpus.end(), devices.begin(), devices.end());
  }
  if (gpus.empty()) throw Error("No OpenCL GPU device");
  return gpus;
}

template <class T>
T device_value(cl_device_id device, cl_uint what) {
  T value{};
  api().GetDeviceInfo(device, what, sizeof(value), &value, nullptr);
  return value;
}

}  // namespace

// index >= 0: that device in platform order. index < 0 (default): discrete GPUs
// before integrated ones, then by memory, skipping devices that cannot run the
// kernels, so hybrid laptops use their dedicated GPU.
std::unique_ptr<Device> create_opencl_device(int index) {
  std::vector<cl_device_id> gpus = opencl_gpus();
  if (index >= 0) {
    if (index >= int(gpus.size())) throw Error("OpenCL device index out of range");
    return std::make_unique<OpenCLDevice>(gpus[index]);
  }
  auto rank = [](cl_device_id d) {
    return std::make_pair(device_value<cl_bool>(d, CL_DEVICE_HOST_UNIFIED_MEMORY) ? 0 : 1,
                          device_value<cl_ulong>(d, CL_DEVICE_GLOBAL_MEM_SIZE));
  };
  std::stable_sort(gpus.begin(), gpus.end(), [&](cl_device_id a, cl_device_id b) { return rank(a) > rank(b); });
  std::string errors;
  for (cl_device_id d : gpus) {
    try {
      return std::make_unique<OpenCLDevice>(d);
    } catch (const std::exception& error) {
      errors += std::string(errors.empty() ? "" : "; ") + error.what();
    }
  }
  throw Error(errors);
}

}  // namespace adr
