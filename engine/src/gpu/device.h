// Minimal device abstraction shared by the Metal, CUDA, OpenCL and CPU backends.
//
// All work is issued to one in-order queue per device. Kernel arguments are a
// list of buffers followed by one plain-old-data parameter struct, matching the
// dialect.h convention. Buffers are recycled through a size-keyed pool so that
// steady-state frame processing performs no driver allocations.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace adr {

struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct BufferHandle {
  void* native = nullptr;  // id<MTLBuffer>, CUdeviceptr, cl_mem or host memory
  size_t bytes = 0;
};

struct Dim3 {
  int x = 1, y = 1, z = 1;
};

class Device;

// Reference-counted pooled device buffer.
class Buffer {
 public:
  Buffer() = default;
  Buffer(Device* device, BufferHandle handle);
  void* native() const { return state_ ? state_->handle.native : nullptr; }
  size_t bytes() const { return state_ ? state_->handle.bytes : 0; }
  explicit operator bool() const { return static_cast<bool>(state_); }

 private:
  struct State {
    Device* device = nullptr;
    BufferHandle handle;
    ~State();
  };
  std::shared_ptr<State> state_;
};

struct KernelHandle {
  void* native = nullptr;
  std::string name;
};

class Device {
 public:
  virtual ~Device() = default;
  virtual std::string name() const = 0;
  virtual std::string backend() const = 0;

  Buffer alloc(size_t bytes);  // on allocation failure, trims the pool and retries once
  void trim();  // releases pooled buffers
  // Device memory held by this engine (live plus pooled buffers) and its peak.
  size_t allocated_bytes() const { return native_bytes_; }
  size_t peak_bytes() const { return peak_bytes_; }

  // Per-device cache for constant tables (weights, LUTs). Entries are created
  // once and destroyed with the device; never keep device data in statics.
  template <class T, class Make>
  std::shared_ptr<T> cached(const std::string& key, Make make) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) return std::static_pointer_cast<T>(it->second);
    std::shared_ptr<T> value = std::make_shared<T>(make());
    cache_[key] = value;
    return value;
  }

  virtual void upload(const Buffer& dst, const void* src, size_t bytes, size_t offset = 0) = 0;
  // Waits for queued work before reading.
  virtual void download(void* dst, const Buffer& src, size_t bytes, size_t offset = 0) = 0;
  virtual void fill(const Buffer& dst, uint8_t value, size_t bytes, size_t offset = 0) = 0;
  virtual void copy(const Buffer& dst, const Buffer& src, size_t bytes, size_t dst_offset = 0,
                    size_t src_offset = 0) = 0;

  KernelHandle* kernel(const std::string& name);
  // threads: total work items (rounded up to whole groups); group: work-group size.
  virtual void launch(KernelHandle* kernel, Dim3 threads, Dim3 group,
                      std::initializer_list<const Buffer*> buffers, const void* params,
                      size_t params_bytes) = 0;
  template <class P>
  void launch(const std::string& name, Dim3 threads, Dim3 group,
              std::initializer_list<const Buffer*> buffers, const P& params) {
    launch(kernel(name), threads, group, buffers, &params, sizeof(P));
  }
  // Submits queued work without waiting.
  virtual void flush() = 0;
  virtual void synchronize() = 0;

  // Native queue/stream handles for host interoperability (may be null).
  virtual void* native_queue() const { return nullptr; }

 protected:
  virtual BufferHandle allocate_native(size_t bytes) = 0;
  virtual void release_native(BufferHandle handle) = 0;
  virtual void* load_kernel(const std::string& name) = 0;
  void release_to_pool(BufferHandle handle);
  // Backends call this first in their destructor: drops cached tables and
  // releases every pooled buffer while the native device is still alive.
  void release_resources();
  friend class Buffer;

 private:
  std::mutex pool_mutex_;
  std::multimap<size_t, BufferHandle> pool_;
  std::atomic<size_t> native_bytes_{0}, peak_bytes_{0};
  std::map<std::string, std::unique_ptr<KernelHandle>> kernels_;
  std::mutex cache_mutex_;
  std::map<std::string, std::shared_ptr<void>> cache_;
};

// Backend factory. name: "metal", "cuda", "opencl", "cpu" or "auto".
// Selection of a specific adapter: "opencl:1", "cuda:0".
std::unique_ptr<Device> create_device(const std::string& name);
std::vector<std::string> available_backends();

}  // namespace adr
