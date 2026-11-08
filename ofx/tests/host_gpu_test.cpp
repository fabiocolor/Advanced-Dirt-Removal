// Driver fault tests use CPU-backed fake handles; never submit invalid GPU work.
#include "../src/host_gpu.cpp"
#include <algorithm>
#include <future>
#include <cstdio>
#include <functional>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void* const hostContext=reinterpret_cast<void*>(uintptr_t(0x11));
void* const engineContext=reinterpret_cast<void*>(uintptr_t(0x22));
thread_local void* selectedContext=nullptr; // Each test explicitly selects its simulated context.
thread_local bool initialized=true;
int waits=0,uploads=0,failUpload=0;
int ADR_GPU_CALL getContext(void** p){if(!initialized)return 3;*p=selectedContext;return 0;}
int ADR_GPU_CALL setContext(void* p){selectedContext=p;return 0;}
int ADR_GPU_CALL waitStream(void*){++waits;return selectedContext==hostContext?0:201;}
int ADR_GPU_CALL download(void* dst,uint64_t src,size_t n){if(selectedContext!=hostContext)return 201;std::memcpy(dst,reinterpret_cast<void*>(uintptr_t(src)),n);return 0;}
int ADR_GPU_CALL upload(uint64_t dst,const void* src,size_t n){if(selectedContext!=hostContext)return 201;if(++uploads==failUpload)return 999;std::memcpy(reinterpret_cast<void*>(uintptr_t(dst)),src,n);return 0;}
struct Memory {std::vector<unsigned char> bytes;void* context=hostContext;unsigned type=0x10f0;};
struct Queue {void* context=hostContext;std::vector<std::function<void()>> pending;int writes=0,failAt=0;};
int ADR_GPU_CALL memoryInfo(void* p,unsigned key,size_t,void* dst,size_t*){auto& m=*static_cast<Memory*>(p);if(key==0x1100)*static_cast<unsigned*>(dst)=m.type;else if(key==0x1102)*static_cast<size_t*>(dst)=m.bytes.size();else if(key==0x1106)*static_cast<void**>(dst)=m.context;else return -30;return 0;}
int ADR_GPU_CALL queueInfo(void* p,unsigned key,size_t,void* dst,size_t*){if(key!=0x1090)return -30;*static_cast<void**>(dst)=static_cast<Queue*>(p)->context;return 0;}
int ADR_GPU_CALL finish(void* p){auto& q=*static_cast<Queue*>(p);for(auto& op:q.pending)op();q.pending.clear();return 0;}
int ADR_GPU_CALL read(void* q,void* p,unsigned blocking,size_t offset,size_t n,void* dst,unsigned,const void*,void*){require(blocking==1,"download is blocking");auto& m=*static_cast<Memory*>(p);if(offset>m.bytes.size()||n>m.bytes.size()-offset)return -30;finish(q);std::memcpy(dst,m.bytes.data()+offset,n);return 0;}
int ADR_GPU_CALL write(void* p,void* memory,unsigned blocking,size_t offset,size_t n,const void* src,unsigned,const void*,void*){auto& q=*static_cast<Queue*>(p);auto& m=*static_cast<Memory*>(memory);if(++q.writes==q.failAt)return -5;if(offset>m.bytes.size()||n>m.bytes.size()-offset)return -30;q.pending.push_back([&,offset,n,src]{std::memcpy(m.bytes.data()+offset,src,n);});if(blocking)finish(p);return 0;}
template<class F>void rejects(F fn,const char* message){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}require(failed,message);}
}
int main(){try{
 auto& cu=adr_host::cuda();cu.valid=true;cu.getContext=getContext;cu.setContext=setContext;cu.waitStream=waitStream;cu.download=download;cu.upload=upload;
 auto& cl=adr_host::opencl();cl.valid=true;cl.memoryInfo=memoryInfo;cl.queueInfo=queueInfo;cl.read=read;cl.write=write;cl.finish=finish;
 for(auto backend:{adr_host::GpuBackend::Cuda,adr_host::GpuBackend::OpenCL})for(size_t stride:{size_t(40),size_t(37),size_t(21)}){
  selectedContext=hostContext;Memory src,dst;Queue queue;
  const size_t size=stride*5;src.bytes.resize(size);dst.bytes.assign(size,0xa5);
  for(size_t n=0;n<size;++n)src.bytes[n]=static_cast<unsigned char>(n*73+19);
  std::vector<unsigned char> expected=dst.bytes;
  {
   adr_host::GpuScope scope(backend,&queue);
   auto source=adr_host::stageGpuImage(backend==adr_host::GpuBackend::Cuda?static_cast<void*>(src.bytes.data()):static_cast<void*>(&src),size,true);
   require(std::memcmp(source->data(),src.bytes.data(),size)==0,"download exact bytes");
   selectedContext=engineContext; // Models engine calls selecting their own primary context.
   {adr_host::HostContextScope callback;require(selectedContext==hostContext,"host callback uses original context");}
   require(selectedContext==engineContext,"callback restores engine context");
   auto target=adr_host::stageGpuImage(backend==adr_host::GpuBackend::Cuda?static_cast<void*>(dst.bytes.data()):static_cast<void*>(&dst),size,false);
   require(selectedContext==engineContext,"image acquisition restores engine context");
   std::memcpy(target->data(),source->data(),size);
   const size_t start=stride==21?0:7,rowBytes=21,rows=4;
   for(size_t y=0;y<rows;++y)std::copy(src.bytes.begin()+start+y*stride,src.bytes.begin()+start+y*stride+rowBytes,expected.begin()+start+y*stride);
   target->uploadRows(start,stride,rowBytes,rows);
   require(dst.bytes==expected,"partial/full upload preserves padding and outside pixels");
   require(selectedContext==engineContext,"upload restores engine context");
   rejects([&]{target->uploadRows(size-2,stride,21,2);},"reject out-of-range write");
   rejects([&]{target->uploadRows(0,1,21,2);},"reject overlapping row geometry");
   target->uploadRows(size,0,0,0);require(dst.bytes==expected,"empty write preserves output");
   rejects([&]{adr_host::stageGpuImage(nullptr,size,true);},"reject null handle");
   if(backend==adr_host::GpuBackend::OpenCL){
    rejects([&]{adr_host::stageGpuImage(&src,size+1,true);},"reject undersized OpenCL buffer");
    src.context=engineContext;rejects([&]{adr_host::stageGpuImage(&src,size,true);},"reject mismatched OpenCL contexts");src.context=hostContext;
    src.type=0x10f1;rejects([&]{adr_host::stageGpuImage(&src,size,true);},"reject OpenCL images when buffers were requested");src.type=0x10f0;
    queue.writes=0;queue.failAt=2;
    if(stride!=21){rejects([&]{target->uploadRows(start,stride,rowBytes,rows);},"OpenCL dispatch failure propagates");require(queue.pending.empty(),"queued staging reads finish before exception");}
   }else{
    uploads=0;failUpload=2;const int before=waits;
    if(stride!=21){rejects([&]{target->uploadRows(start,stride,rowBytes,rows);},"CUDA dispatch failure propagates");require(waits>=before+2,"CUDA failure completes previous copies");}
    failUpload=0;
   }
  }
  require(selectedContext==hostContext,"render restores host context");require(adr_host::gpuStagingMemory().live==0,"staging released on normal/error paths");
  std::printf("GPU byte transport exact: %s stride=%zu; partial/padding/error/context paths\n",backend==adr_host::GpuBackend::Cuda?"CUDA":"OpenCL",stride);
 }
 selectedContext=nullptr;initialized=false;
 {adr_host::GpuScope scope(adr_host::GpuBackend::Cpu,nullptr);initialized=true;selectedContext=engineContext;{adr_host::HostContextScope callback;require(selectedContext==nullptr,"new engine context is hidden from CPU host callback");}require(selectedContext==engineContext,"new engine context restored");}
 require(selectedContext==nullptr,"original uninitialized caller context restored");
 selectedContext=hostContext;
 {adr_host::GpuScope scope(adr_host::GpuBackend::Cuda,nullptr);auto child=std::async(std::launch::async,[]{selectedContext=engineContext;adr_host::GpuScope other(adr_host::GpuBackend::Cpu,nullptr);require(!adr_host::gpuTransportActive(),"thread-local CPU scope");});child.get();require(adr_host::gpuTransportActive()&&selectedContext==hostContext,"other thread cannot overwrite host scope");}
 rejects([]{adr_host::GpuScope scope(adr_host::GpuBackend::OpenCL,nullptr);},"missing OpenCL queue rejected");
 rejects([]{adr_host::GpuScope scope(adr_host::GpuBackend::Cpu,nullptr);adr_host::stageGpuImage(reinterpret_cast<void*>(1),1,true);},"CPU scope cannot stage GPU handles");
 rejects([]{adr_host::GpuScope scope(adr_host::GpuBackend::Cuda,nullptr);adr_host::gpuHostReleaseStatus(-7);},"failed host release cannot report successful render");
 require(!adr_host::gpuTransportActive()&&adr_host::gpuStagingMemory().live==0,"all scopes and staging released");
 std::puts("GPU transport driver-fault and scope checks passed (simulated drivers; no invalid GPU work)");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
