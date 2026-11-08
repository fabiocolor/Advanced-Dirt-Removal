#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace adr_host {
enum class GpuBackend { Cpu, Cuda, OpenCL };
struct GpuState;
// Render/lifecycle scope only: metadata callbacks must not initialize drivers.
class GpuScope {
 std::unique_ptr<GpuState> state_;
public:
 GpuScope(GpuBackend backend,void* queue,bool active=true,bool releasingContext=false);
 ~GpuScope() noexcept(false);
 GpuScope(const GpuScope&)=delete;
 GpuScope& operator=(const GpuScope&)=delete;
};
// Engine CUDA calls can select their own primary context. Host image callbacks
// and transfers must use the context the host supplied, then restore the engine.
class HostContextScope {
 GpuState* owner_=nullptr;
 void* previous_=nullptr;
 bool changed_=false;
public:
 HostContextScope();
 ~HostContextScope() noexcept;
 HostContextScope(const HostContextScope&)=delete;
 HostContextScope& operator=(const HostContextScope&)=delete;
};
bool gpuTransportActive() noexcept;
const char* gpuTransportName() noexcept;
void gpuHostReleaseStatus(int status) noexcept;
class GpuImage {
public:
 virtual ~GpuImage()=default;
 virtual char* data() noexcept=0;
 virtual void uploadRows(size_t start,size_t stride,size_t rowBytes,size_t rows)=0;
};
std::unique_ptr<GpuImage> stageGpuImage(void* image,size_t bytes,bool download);
struct GpuStagingMemory {uint64_t live,peak;};
GpuStagingMemory gpuStagingMemory() noexcept;
}
