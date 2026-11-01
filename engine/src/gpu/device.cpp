#include "gpu/device.h"

#include <algorithm>
#include <cstdlib>

namespace adr {

Buffer::Buffer(Device* device, BufferHandle handle) : state_(std::make_shared<State>()) {
  state_->device = device;
  state_->handle = handle;
}

Buffer::State::~State() {
  if (device && handle.native) device->release_to_pool(handle);
}

Buffer Device::alloc(size_t bytes) {
  if (bytes == 0) bytes = 4;
  // Round to 256 bytes so near sizes share pool entries and every backend
  // gets aligned allocations.
  bytes = (bytes + 255) & ~size_t(255);
  BufferHandle handle;
  {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto it = pool_.find(bytes);
    if (it != pool_.end()) {
      handle = it->second;
      pool_.erase(it);
    }
  }
  if (!handle.native) {
    try {
      handle = allocate_native(bytes);
    } catch (const Error&) {
      trim();  // pooled buffers of other sizes may be what fills a small GPU
      handle = allocate_native(bytes);
    }
    const size_t now = native_bytes_ += handle.bytes;
    size_t peak = peak_bytes_.load();
    while (now > peak && !peak_bytes_.compare_exchange_weak(peak, now)) {
    }
  }
  return Buffer(this, handle);
}

void Device::release_to_pool(BufferHandle handle) {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  pool_.emplace(handle.bytes, handle);
}

void Device::trim() {
  std::multimap<size_t, BufferHandle> pool;
  {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    pool.swap(pool_);
  }
  synchronize();
  for (auto& entry : pool) {
    native_bytes_ -= entry.second.bytes;
    release_native(entry.second);
  }
}

void Device::release_resources() {
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
  }
  trim();
}

KernelHandle* Device::kernel(const std::string& name) {
  auto it = kernels_.find(name);
  if (it != kernels_.end()) return it->second.get();
  auto handle = std::make_unique<KernelHandle>();
  handle->name = name;
  handle->native = load_kernel(name);
  if (!handle->native) throw Error("GPU kernel not found: " + name);
  auto* raw = handle.get();
  kernels_.emplace(name, std::move(handle));
  return raw;
}

#if ADR_HAVE_METAL
std::unique_ptr<Device> create_metal_device(int index);
#endif
#if ADR_HAVE_CUDA
std::unique_ptr<Device> create_cuda_device(int index);
#endif
#if ADR_HAVE_OPENCL
std::unique_ptr<Device> create_opencl_device(int index);
#endif
#if ADR_HAVE_CPU
std::unique_ptr<Device> create_cpu_device(int index);
#endif

std::vector<std::string> available_backends() {
  std::vector<std::string> names;
#if ADR_HAVE_METAL
  names.push_back("metal");
#endif
#if ADR_HAVE_CUDA
  names.push_back("cuda");
#endif
#if ADR_HAVE_OPENCL
  names.push_back("opencl");
#endif
#if ADR_HAVE_CPU
  names.push_back("cpu");  // last: "auto" prefers any GPU
#endif
  return names;
}

std::unique_ptr<Device> create_device(const std::string& spec) {
  std::string name = spec;
  int index = -1;  // default adapter: backend's choice (OpenCL ranks devices)
  auto colon = spec.find(':');
  if (colon != std::string::npos) {
    name = spec.substr(0, colon);
    index = std::atoi(spec.c_str() + colon + 1);
  }
  if (name == "auto" || name.empty()) {
    std::string last;
    for (const auto& candidate : available_backends()) {
      try {
        return create_device(index < 0 ? candidate : candidate + ":" + std::to_string(index));
      } catch (const std::exception& error) {
        last = error.what();
      }
    }
    throw Error("No usable backend" + (last.empty() ? std::string() : ": " + last));
  }
#if ADR_HAVE_METAL
  if (name == "metal") return create_metal_device(std::max(index, 0));
#endif
#if ADR_HAVE_CUDA
  if (name == "cuda") return create_cuda_device(std::max(index, 0));
#endif
#if ADR_HAVE_OPENCL
  if (name == "opencl") return create_opencl_device(index);
#endif
#if ADR_HAVE_CPU
  if (name == "cpu") return create_cpu_device(index);
#endif
  throw Error("Backend not built: " + name);
}

}  // namespace adr
