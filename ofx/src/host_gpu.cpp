#include "host_gpu.h"
#include <atomic>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define ADR_GPU_CALL __stdcall
#else
#include <dlfcn.h>
#define ADR_GPU_CALL
#endif

namespace adr_host {
namespace {
void checked(int status,const char* operation) {
 if(status)throw std::runtime_error(std::string("GPU host transport: ")+operation+" (status "+std::to_string(status)+")");
}
struct Library {
#ifdef _WIN32
 HMODULE handle;
 Library(const char* windowsName,const char*):handle(LoadLibraryExA(windowsName,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)){}
 void* symbol(const char* name){return handle?reinterpret_cast<void*>(GetProcAddress(handle,name)):nullptr;}
 ~Library(){if(handle)FreeLibrary(handle);}
#else
 void* handle;
 Library(const char*,const char* unixName):handle(dlopen(unixName,RTLD_NOW|RTLD_LOCAL)){}
 void* symbol(const char* name){return handle?dlsym(handle,name):nullptr;}
 ~Library(){if(handle)dlclose(handle);}
#endif
 template<class F>bool bind(F& fn,const char* name){auto address=symbol(name);static_assert(sizeof(fn)==sizeof(address),"driver function ABI");std::memcpy(&fn,&address,sizeof(address));return address!=nullptr;}
 Library(const Library&)=delete;
 Library& operator=(const Library&)=delete;
};
struct CudaApi {
 Library library{"nvcuda.dll","libcuda.so.1"};
 int(ADR_GPU_CALL* getContext)(void**)=nullptr;
 int(ADR_GPU_CALL* setContext)(void*)=nullptr;
 int(ADR_GPU_CALL* waitStream)(void*)=nullptr;
 int(ADR_GPU_CALL* download)(void*,uint64_t,size_t)=nullptr;
 int(ADR_GPU_CALL* upload)(uint64_t,const void*,size_t)=nullptr;
 bool valid;
 CudaApi(){valid=library.bind(getContext,"cuCtxGetCurrent");valid=library.bind(setContext,"cuCtxSetCurrent")&&valid;valid=library.bind(waitStream,"cuStreamSynchronize")&&valid;valid=library.bind(download,"cuMemcpyDtoH_v2")&&valid;valid=library.bind(upload,"cuMemcpyHtoD_v2")&&valid;}
};
CudaApi& cuda(){static CudaApi api;return api;}
struct OpenCLApi {
 Library library{"OpenCL.dll","libOpenCL.so.1"};
 int(ADR_GPU_CALL* memoryInfo)(void*,unsigned,size_t,void*,size_t*)=nullptr;
 int(ADR_GPU_CALL* queueInfo)(void*,unsigned,size_t,void*,size_t*)=nullptr;
 int(ADR_GPU_CALL* read)(void*,void*,unsigned,size_t,size_t,void*,unsigned,const void*,void*)=nullptr;
 int(ADR_GPU_CALL* write)(void*,void*,unsigned,size_t,size_t,const void*,unsigned,const void*,void*)=nullptr;
 int(ADR_GPU_CALL* finish)(void*)=nullptr;
 bool valid;
 OpenCLApi(){valid=library.bind(memoryInfo,"clGetMemObjectInfo");valid=library.bind(queueInfo,"clGetCommandQueueInfo")&&valid;valid=library.bind(read,"clEnqueueReadBuffer")&&valid;valid=library.bind(write,"clEnqueueWriteBuffer")&&valid;valid=library.bind(finish,"clFinish")&&valid;}
};
OpenCLApi& opencl(){static OpenCLApi api;return api;}
std::atomic<uint64_t> liveBytes{0},peakBytes{0};
struct TrackBytes {
 uint64_t bytes=0;
 void assign(size_t n){bytes=n;const auto live=liveBytes.fetch_add(n)+n;auto peak=peakBytes.load();while(live>peak&&!peakBytes.compare_exchange_weak(peak,live)){} }
 ~TrackBytes(){liveBytes.fetch_sub(bytes);}
 TrackBytes()=default;TrackBytes(const TrackBytes&)=delete;
};
}
struct GpuState {
 GpuBackend backend;
 void* queue;
 GpuState* previous=nullptr;
 void* hostContext=nullptr;
 bool restoreContext=false;
 int contextError=0;
 int releaseError=0;
 bool releasingContext=false;
};
namespace {thread_local GpuState* current=nullptr;}
GpuScope::GpuScope(GpuBackend backend,void* queue,bool active,bool releasingContext) {
 if(!active)return;
 auto state=std::make_unique<GpuState>();state->backend=backend;state->queue=queue;state->previous=current;
 state->releasingContext=releasingContext;
 if(backend==GpuBackend::OpenCL&&!queue)throw std::runtime_error("OpenCL host transport: missing command queue");
 auto& api=cuda();
 if(api.valid){const int status=api.getContext(&state->hostContext);state->restoreContext=status==0||status==3; // NOT_INITIALIZED means no caller context.
  if(backend==GpuBackend::Cuda){checked(status,"host CUDA context");if(!state->hostContext)throw std::runtime_error("CUDA host transport: missing caller context");}}
 else if(backend==GpuBackend::Cuda)throw std::runtime_error("CUDA host transport: driver entry points unavailable");
 state_=std::move(state);current=state_.get();
}
GpuScope::~GpuScope() noexcept(false) {
 if(!state_)return;
 current=state_->previous;
 auto& api=cuda();
 if(state_->restoreContext&&api.valid){void* present=nullptr;const int status=api.getContext(&present);
  if(status!=3&&(status||present!=state_->hostContext)){const int restored=api.setContext(state_->hostContext);
   // Unload may have destroyed the engine-owned primary context that was
   // current on entry. It cannot be restored; detach it instead. A live
   // external host context must still be restored normally.
   if(state_->releasingContext&&(restored==201||restored==709)){const int detached=api.setContext(nullptr);if(detached)state_->contextError=detached;}
   else if(restored)state_->contextError=restored;}}
 if(state_->contextError&&std::uncaught_exceptions()==0)checked(state_->contextError,"restore caller CUDA context");
 if(state_->releaseError&&std::uncaught_exceptions()==0)checked(state_->releaseError,"host image release");
}
HostContextScope::HostContextScope():owner_(current) {
 if(!owner_||!owner_->restoreContext)return;
 auto& api=cuda();const int status=api.getContext(&previous_);
 if(status==3)return;
 if(status){owner_->contextError=status;checked(status,"current CUDA context");}
 if(previous_!=owner_->hostContext){const int selected=api.setContext(owner_->hostContext);if(selected)owner_->contextError=selected;checked(selected,"select host CUDA context");changed_=true;}
}
HostContextScope::~HostContextScope() noexcept {
 if(changed_){const int status=cuda().setContext(previous_);if(status)owner_->contextError=status;}
}
bool gpuTransportActive() noexcept{return current&&current->backend!=GpuBackend::Cpu;}
const char* gpuTransportName() noexcept{return !current||current->backend==GpuBackend::Cpu?"CPU":current->backend==GpuBackend::Cuda?"CUDA":"OpenCL";}
void gpuHostReleaseStatus(int status) noexcept{if(current&&status)current->releaseError=status;}
GpuStagingMemory gpuStagingMemory() noexcept{return {liveBytes.load(),peakBytes.load()};}
namespace {
class StagedImage final:public GpuImage {
 GpuBackend backend_;
 void* queue_;
 void* image_;
 size_t bytes_;
 std::vector<char> pixels_;
 TrackBytes tracked_;
public:
 StagedImage(void* image,size_t bytes,bool download):backend_(current->backend),queue_(current->queue),image_(image),bytes_(bytes) {
  HostContextScope context;
  if(backend_==GpuBackend::OpenCL){auto& api=opencl();if(!api.valid)throw std::runtime_error("OpenCL host transport: driver entry points unavailable");
   unsigned type=0;size_t size=0;void* memoryContext=nullptr;void* queueContext=nullptr;
   checked(api.memoryInfo(image_,0x1100,sizeof(type),&type,nullptr),"OpenCL memory type"); // CL_MEM_TYPE
   checked(api.memoryInfo(image_,0x1102,sizeof(size),&size,nullptr),"OpenCL memory size");
   checked(api.memoryInfo(image_,0x1106,sizeof(memoryContext),&memoryContext,nullptr),"OpenCL memory context");
   checked(api.queueInfo(queue_,0x1090,sizeof(queueContext),&queueContext,nullptr),"OpenCL queue context");
   if(type!=0x10f0||bytes>size||memoryContext!=queueContext)throw std::runtime_error("OpenCL host transport: invalid buffer size/type/context");
  }
  pixels_.resize(bytes);tracked_.assign(bytes);
  if(download){
   if(backend_==GpuBackend::Cuda){checked(cuda().waitStream(queue_),"wait host CUDA producer");checked(cuda().download(pixels_.data(),uint64_t(reinterpret_cast<uintptr_t>(image_)),bytes_),"CUDA image download");}
   else {auto& api=opencl();checked(api.finish(queue_),"wait host OpenCL producer");checked(api.read(queue_,image_,1,0,bytes_,pixels_.data(),0,nullptr,nullptr),"OpenCL image download");}
  }
 }
 char* data() noexcept override{return pixels_.data();}
 void uploadRows(size_t start,size_t stride,size_t rowBytes,size_t rows) override {
  if(!rows||!rowBytes)return;
  if(start>bytes_||rowBytes>bytes_-start||(rows>1&&(stride<rowBytes||stride>(bytes_-start-rowBytes)/(rows-1))))throw std::runtime_error("GPU host transport: output window exceeds staging buffer");
  HostContextScope context;
  if(backend_==GpuBackend::Cuda){auto& api=cuda();checked(api.waitStream(queue_),"wait host CUDA output");
   const auto base=uint64_t(reinterpret_cast<uintptr_t>(image_));
   // Full, unpadded frames use one copy. Partial windows never overwrite the
   // surrounding host pixels or padding. Synchronous pageable-host copies
   // use the default stream; complete it before returning to the host stream.
   try{
    if(stride==rowBytes)checked(api.upload(base+start,pixels_.data()+start,rowBytes*rows),"CUDA image upload");
    else for(size_t y=0;y<rows;++y){const size_t offset=start+y*stride;checked(api.upload(base+offset,pixels_.data()+offset,rowBytes),"CUDA image row upload");}
    checked(api.waitStream(nullptr),"complete CUDA image upload");
   }catch(...){api.waitStream(nullptr);throw;}
  }else{auto& api=opencl();checked(api.finish(queue_),"wait host OpenCL output");
   try{
    if(stride==rowBytes)checked(api.write(queue_,image_,0,start,rowBytes*rows,pixels_.data()+start,0,nullptr,nullptr),"OpenCL image upload");
    else for(size_t y=0;y<rows;++y){const size_t offset=start+y*stride;checked(api.write(queue_,image_,0,offset,rowBytes,pixels_.data()+offset,0,nullptr,nullptr),"OpenCL image row upload");}
    checked(api.finish(queue_),"complete OpenCL image upload");
   }catch(...){api.finish(queue_);throw;} // Queued reads of pixels_ must finish before unwinding.
  }
 }
};
}
std::unique_ptr<GpuImage> stageGpuImage(void* image,size_t bytes,bool download) {
 if(!gpuTransportActive()||!image||!bytes)throw std::runtime_error("GPU host transport: invalid scope/image/size");
 return std::make_unique<StagedImage>(image,bytes,download);
}
}
