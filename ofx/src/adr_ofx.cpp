// Advanced Dirt Removal: OFX image-effect adapter for the linked engine
// (Metal, CUDA, OpenCL or CPU). Fetches temporal source frames from the host,
// streams them through the engine and writes the repaired frame.
#include <cctype>
#include "ofxImageEffect.h"
#include "ofxProperty.h"
#include "ofxTimeLine.h"
#include "ofxProgress.h"
#include "adr/adr.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sys/stat.h>
#include "../../engine/tools/sha256.h"
#include "diagnostic_log.h"
#include "ofxInteract.h"
#include "paint.h"
#include "paint_source.h"
#include "paint_input.h"
#include "paint_gl.h"
#include "paint_navigation.h"
#include "recovery_feedback.h"
#include "frame_cache.h"
#include "paint_cache.h"
#ifdef _WIN32
#include <process.h>
#include <direct.h>
#else
#include <unistd.h>
#endif
#ifndef ADR_PLUGIN_VERSION
#define ADR_PLUGIN_VERSION "dev"
#endif
// Side-by-side test builds can use another identity (-DADR_PLUGIN_ID=... -DADR_PLUGIN_LABEL=...).
#ifndef ADR_PLUGIN_ID
#define ADR_PLUGIN_ID "com.fabiocolor.advanceddirtremoval"
#endif
#ifndef ADR_PLUGIN_LABEL
#define ADR_PLUGIN_LABEL "Advanced Dirt Removal"
#endif
#ifndef ADR_SOURCE_REVISION
#define ADR_SOURCE_REVISION "unknown"
#endif
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if ADR_HOST_METAL_TRANSPORT
#include "host_metal.h"
#elif ADR_HOST_GPU_TRANSPORT
#include "host_gpu.h"
#endif
namespace {
OfxHost* host=nullptr;
OfxImageEffectSuiteV1* fx=nullptr;
OfxPropertySuiteV1* prop=nullptr;
OfxParameterSuiteV1* params=nullptr;
OfxMessageSuiteV1* messages=nullptr;
OfxMessageSuiteV2* persistentMessages=nullptr;
OfxInteractSuiteV1* interacts=nullptr;
OfxTimeLineSuiteV1* timeline=nullptr;  // stroke navigation moves the playhead (optional)  // viewer painting (Resolve only)
OfxProgressSuiteV1* progressV1=nullptr;
OfxProgressSuiteV2* progressV2=nullptr;
std::mutex mutex;
std::mutex liveInstancesMutex;
std::set<OfxImageEffectHandle> liveInstances;
#if ADR_HOST_METAL_TRANSPORT
// Image fetches (including recovery and prefetch) execute on the host render
// thread. Nested upstream renders restore its previous transport context.
thread_local void* hostMetalQueue=nullptr;
struct HostMetalScope {
 void* previous=hostMetalQueue;
 explicit HostMetalScope(OfxPropertySetHandle in) {
  int enabled=0;prop->propGetInt(in,"OfxImageEffectPropMetalEnabled",0,&enabled);
  hostMetalQueue=nullptr;
  if(enabled) {
   void* queue=nullptr;
   if(prop->propGetPointer(in,"OfxImageEffectPropMetalCommandQueue",0,&queue)!=kOfxStatOK||!queue) {
    hostMetalQueue=previous;
    throw std::runtime_error("Metal host transport: missing render command queue");
   }
   hostMetalQueue=queue;
  }
 }
 ~HostMetalScope(){hostMetalQueue=previous;}
};
#endif
#if ADR_HOST_GPU_TRANSPORT
struct HostGpuScope:adr_host::GpuScope {
 struct Request {adr_host::GpuBackend backend;void* queue;};
 static Request request(OfxPropertySetHandle in){
  int cuda=0,cl=0,metal=0;
  prop->propGetInt(in,"OfxImageEffectPropCudaEnabled",0,&cuda);
  prop->propGetInt(in,"OfxImageEffectPropOpenCLEnabled",0,&cl);
  prop->propGetInt(in,"OfxImageEffectPropMetalEnabled",0,&metal);
  if(metal||(cuda&&cl))throw std::runtime_error("Unsupported or conflicting host GPU transport request");
  void* queue=nullptr;
  if(cuda||cl){const char* key=cuda?"OfxImageEffectPropCudaStream":"OfxImageEffectPropOpenCLCommandQueue";
   if(prop->propGetPointer(in,key,0,&queue)!=kOfxStatOK||(cl&&!queue))throw std::runtime_error("GPU host transport: missing host stream/queue");}
  return {cuda?adr_host::GpuBackend::Cuda:cl?adr_host::GpuBackend::OpenCL:adr_host::GpuBackend::Cpu,queue};
 }
 explicit HostGpuScope(Request r):adr_host::GpuScope(r.backend,r.queue){}
 explicit HostGpuScope(OfxPropertySetHandle in):HostGpuScope(request(in)){}
};
#endif
// Instance actions and other instances' lifecycle actions can overlap. Protect
// only set membership here; never hold this lock while calling the host/log.
bool isLiveInstance(OfxImageEffectHandle e){std::lock_guard<std::mutex> guard(liveInstancesMutex);return liveInstances.count(e)!=0;}
void trackInstance(OfxImageEffectHandle e){std::lock_guard<std::mutex> guard(liveInstancesMutex);liveInstances.insert(e);}
void untrackInstance(OfxImageEffectHandle e){std::lock_guard<std::mutex> guard(liveInstancesMutex);liveInstances.erase(e);}
struct Error:std::runtime_error { OfxStatus status; Error(OfxStatus s,const std::string&m):std::runtime_error(m),status(s){} };
void require(bool ok,const std::string&m,OfxStatus s=kOfxStatErrFormat){if(!ok)throw Error(s,std::string(m)+" (OFX status="+std::to_string(s)+")");}
// Literal guard messages must not construct a temporary std::string per pixel.
// Build the identical diagnostic only on failure; dynamic strings retain their overload.
void require(bool ok,const char*m,OfxStatus s=kOfxStatErrFormat){if(!ok)throw Error(s,std::string(m)+" (OFX status="+std::to_string(s)+")");}
void check(OfxStatus s,const char*m){if(s!=kOfxStatOK)throw Error(s,std::string(m)+" (OFX status="+std::to_string(s)+")");}
void releaseHostImage(OfxPropertySetHandle image){
 const auto status=fx->clipReleaseImage(image);
#if ADR_HOST_GPU_TRANSPORT
 adr_host::gpuHostReleaseStatus(status);
#else
 (void)status;
#endif
}
std::string str(OfxPropertySetHandle p,const char*k){char*v=nullptr;check(prop->propGetString(p,k,0,&v),k);return v?v:"";}
int integer(OfxPropertySetHandle p,const char*k,int i=0){int v=0;check(prop->propGetInt(p,k,i,&v),k);return v;}
double number(OfxPropertySetHandle p,const char*k,int i=0){double v=0;check(prop->propGetDouble(p,k,i,&v),k);require(std::isfinite(v),std::string("nonfinite property ")+k);return v;}
void si(OfxPropertySetHandle p,const char*k,int v){check(prop->propSetInt(p,k,0,v),k);}
void ss(OfxPropertySetHandle p,const char*k,const char*v,int i=0){check(prop->propSetString(p,k,i,v),k);}
OfxPropertySetHandle properties(OfxImageEffectHandle e){OfxPropertySetHandle p;check(fx->getPropertySet(e,&p),"effect properties");return p;}
int frameTime(OfxPropertySetHandle p){double t=number(p,kOfxPropTime);require(t==std::round(t)&&std::abs(t)<=1e9,"fractional frame times are unsupported");return int(t);}
// GetFramesNeeded only guarantees time. Without an optional scale, declare
// conservative temporal dependencies so later proxy renders may fetch them.
bool nativeScale(OfxPropertySetHandle p){double x=0,y=0;return prop->propGetDouble(p,kOfxImageEffectPropRenderScale,0,&x)==kOfxStatOK&&prop->propGetDouble(p,kOfxImageEffectPropRenderScale,1,&y)==kOfxStatOK&&x==1.&&y==1.;}
void scaleOne(OfxPropertySetHandle p){double x=number(p,kOfxImageEffectPropRenderScale,0),y=number(p,kOfxImageEffectPropRenderScale,1);require(x==1.&&y==1.,"proxy/renderScale other than 1 is unsupported ("+std::to_string(x)+","+std::to_string(y)+")");}
float decodeShort(uint16_t value){return float(value)/65535.f;}
uint16_t encodeShort(float value){
 require(std::isfinite(value),"nonfinite Short output");
 double scaled=std::clamp(double(value),0.,1.)*65535.;
 double lower=std::floor(scaled),fraction=scaled-lower;
 uint32_t rounded=uint32_t(lower);
 if(fraction>.5||(fraction==.5&&(rounded&1)))++rounded;
 return uint16_t(rounded);
}
// IEEE 754 binary16 transport; bit operations make rounding independent of
// the host's floating-point rounding mode. No [0,1] clamp is applied to HALF.
enum class PixelDepth {Float, Half, Short};
float decodeHalf(uint16_t value){
 uint32_t sign=uint32_t(value&0x8000u)<<16,exponent=(value>>10)&31u,mantissa=value&1023u,bits=sign;
 require(exponent!=31u,"nonfinite Half input");
 if(exponent){bits|=(exponent+112u)<<23;bits|=mantissa<<13;}
 else if(mantissa){int power=-14;while(!(mantissa&1024u)){mantissa<<=1;--power;}bits|=uint32_t(power+127)<<23;bits|=(mantissa&1023u)<<13;}
 float result;std::memcpy(&result,&bits,4);return result;
}
uint16_t encodeHalf(float value){
 require(std::isfinite(value),"nonfinite Half output");
 uint32_t bits;std::memcpy(&bits,&value,4);uint16_t sign=uint16_t((bits>>16)&0x8000u);
 uint32_t magnitude=bits&0x7fffffffu,mantissa=magnitude&0x7fffffu;
 if(magnitude==0)return sign;
 int power=int(magnitude>>23)-127;
 if(power < -25)return sign;
 uint32_t rounded=0;
 if(power < -14){
  unsigned shift=unsigned(-power-1);uint32_t significand=0x800000u|mantissa;
  rounded=significand>>shift;uint32_t remainder=significand&((1u<<shift)-1u),tie=1u<<(shift-1u);
  if(remainder>tie||(remainder==tie&&(rounded&1u)))++rounded;
  return uint16_t(sign|rounded); // A rounded subnormal may become 0x0400.
 }
 require(power<=15,"Half output exceeds finite binary16 range");
 uint32_t exponent=uint32_t(power+15);rounded=mantissa>>13;uint32_t remainder=mantissa&8191u;
 if(remainder>4096u||(remainder==4096u&&(rounded&1u)))++rounded;
 if(rounded==1024u){rounded=0;++exponent;}
 require(exponent<31u,"Half output rounds to infinity");
 return uint16_t(sign|(exponent<<10)|rounded);
}
struct Image {
 OfxPropertySetHandle handle=nullptr; char*data=nullptr; std::array<int,4>b{};int stride=0,channels=0,sampleBytes=0;PixelDepth depth=PixelDepth::Float;bool premult=false;
#if ADR_HOST_METAL_TRANSPORT
 std::unique_ptr<adr_host::MetalImage> metalImage;
#elif ADR_HOST_GPU_TRANSPORT
 std::unique_ptr<adr_host::GpuImage> gpuImage;
#endif
 Image(OfxImageClipHandle clip,int t,OfxImageEffectHandle=nullptr,const char*role="probe",bool preview=false){
#if ADR_HOST_GPU_TRANSPORT
  adr_host::HostContextScope hostContext;
#endif
  if(auto status=fx->clipGetImage(clip,t,nullptr,&handle);status!=kOfxStatOK)throw Error(status,std::string("host failed to provide required in-range image: ")+role+" frame "+std::to_string(t)+" (OFX status="+std::to_string(status)+")");
  try {
   require(handle,"null host image");
   auto token=str(handle,kOfxImageEffectPropPixelDepth);require(token==kOfxBitDepthFloat||token==kOfxBitDepthHalf||token==kOfxBitDepthShort,"Float, Half or unsigned Short RGB/RGBA required");depth=token==kOfxBitDepthFloat?PixelDepth::Float:token==kOfxBitDepthHalf?PixelDepth::Half:PixelDepth::Short;sampleBytes=depth==PixelDepth::Float?4:2;
   auto c=str(handle,kOfxImageEffectPropComponents);channels=c==kOfxImageComponentRGB?3:c==kOfxImageComponentRGBA?4:0;require(channels,"RGB/RGBA required");
   premult=channels==4&&str(handle,kOfxImageEffectPropPreMultiplication)==kOfxImagePreMultiplied;
   try{if(!preview)scaleOne(handle);else{double sx=number(handle,kOfxImageEffectPropRenderScale,0),sy=number(handle,kOfxImageEffectPropRenderScale,1);require(sx>0&&sx<=1&&sy>0&&sy<=1,"Invalid preview image scale");}}catch(const std::exception&ex){throw std::runtime_error(std::string(role)+" image: "+ex.what());}auto field=str(handle,kOfxImagePropField);
   // Some hosts label progressive full frames "Both"; accept that only when the clip itself is progressive.
   if(field!=kOfxImageFieldNone){OfxPropertySetHandle cp=nullptr;check(fx->clipGetPropertySet(clip,&cp),"image clip fields");auto order=str(cp,kOfxImageClipPropFieldOrder);require((field.empty()||field==kOfxImageFieldBoth)&&order==kOfxImageFieldNone,"Fielded image unsupported: image="+field+", clip="+order);}

   // Any pixel aspect: the effect works on the stored pixel grid (PAL/NTSC SD, anamorphic scans).
   check(prop->propGetIntN(handle,kOfxImagePropBounds,4,b.data()),"image bounds");stride=integer(handle,kOfxImagePropRowBytes);
   void*p=nullptr;check(prop->propGetPointer(handle,kOfxImagePropData,0,&p),"image pixels");data=static_cast<char*>(p);
   require(data&&b[2]>b[0]&&b[3]>b[1]&&int64_t(b[2])-b[0]<=16384&&int64_t(b[3])-b[1]<=16384,"invalid image bounds/data");
   require(std::abs(int64_t(stride))>=int64_t(width())*channels*sampleBytes,"insufficient rowBytes");
   std::array<int,4>rod{};check(prop->propGetIntN(handle,kOfxImagePropRegionOfDefinition,4,rod.data()),"image RoD");require(rod==b,"full-frame host images required");
#if ADR_HOST_METAL_TRANSPORT
   if(hostMetalQueue) {
    require(stride>0,"Metal host transport requires positive rowBytes");
    const size_t bytes=size_t(height()-1)*size_t(stride)+size_t(width())*channels*sampleBytes;
    metalImage=adr_host::stageMetalImage(hostMetalQueue,data,bytes,std::strcmp(role,"output")!=0);
    data=metalImage->data();
   }
#elif ADR_HOST_GPU_TRANSPORT
   if(adr_host::gpuTransportActive()){
    require(stride>0,"GPU host transport requires positive rowBytes");
    const size_t bytes=size_t(height()-1)*size_t(stride)+size_t(width())*channels*sampleBytes;
    gpuImage=adr_host::stageGpuImage(data,bytes,std::strcmp(role,"output")!=0);data=gpuImage->data();
   }
#endif
  }catch(...){if(handle)releaseHostImage(handle);handle=nullptr;throw;}
 }
 ~Image(){
#if ADR_HOST_GPU_TRANSPORT
  // Destruction cannot throw while unwinding an acquisition/render error.
  try{adr_host::HostContextScope hostContext;if(handle)releaseHostImage(handle);}catch(...){if(handle)releaseHostImage(handle);}
#else
  if(handle)releaseHostImage(handle);
#endif
 } Image(const Image&)=delete;
 int width()const{return b[2]-b[0];}int height()const{return b[3]-b[1];}
 char*row(int topdown)const{return data+int64_t(height()-1-topdown)*stride;}
 char*sample(int x,int y,int c)const{return row(y)+(int64_t(x)*channels+c)*sampleBytes;}
 float read(int x,int y,int c)const{if(sampleBytes==4){float v;std::memcpy(&v,sample(x,y,c),4);return v;}uint16_t v;std::memcpy(&v,sample(x,y,c),2);return depth==PixelDepth::Half?decodeHalf(v):decodeShort(v);}
 bool opaque(int x,int y)const{if(sampleBytes==4)return read(x,y,3)==1.f;uint16_t v;std::memcpy(&v,sample(x,y,3),2);return v==(depth==PixelDepth::Half?0x3c00u:65535u);}
 void write(int x,int y,int c,float v)const{if(sampleBytes==4)std::memcpy(sample(x,y,c),&v,4);else{auto q=depth==PixelDepth::Half?encodeHalf(v):encodeShort(v);std::memcpy(sample(x,y,c),&q,2);}}
 void publish(const std::array<int,4>& window) {
#if ADR_HOST_METAL_TRANSPORT
  if(metalImage) {
   const size_t start=size_t(window[1]-b[1])*size_t(stride)+size_t(window[0]-b[0])*channels*sampleBytes;
   metalImage->uploadRows(start,size_t(stride),size_t(window[2]-window[0])*channels*sampleBytes,size_t(window[3]-window[1]));
  }
#elif ADR_HOST_GPU_TRANSPORT
  if(gpuImage){
   const size_t start=size_t(window[1]-b[1])*size_t(stride)+size_t(window[0]-b[0])*channels*sampleBytes;
   gpuImage->uploadRows(start,size_t(stride),size_t(window[2]-window[0])*channels*sampleBytes,size_t(window[3]-window[1]));
  }
#else
  (void)window;
#endif
 }
};
struct Control {const char*name;const char*label;bool boolean;double initial,maximum;int minimum=0;const char*const* choices=nullptr;bool integral=false;const char*hint=nullptr;const char*group=nullptr;};
const char*const analyseChoices[]={"Luma","RGB"};
const char*const shotChoices[]={"Off","Guard","Skip"};
// UI order and groups. Names are stable identifiers saved in host projects; labels may change.
struct Group {const char*name;const char*label;};
const Group groups[]={{"detection","Detection"},{"processing","Processing"},{"safeguards","Safeguards"}};
const Control controls[]={
 {"enabled","Enabled",true,1,1,0,nullptr,false,"Off passes the source through unchanged.",nullptr},
 {"analyse","Analyse",false,0,1,0,analyseChoices,true,"Luma finds spots in brightness; RGB also finds spots that differ only in colour.","detection"},
 {"remove_dark","Remove dark spots",true,1,1,0,nullptr,false,"Repair spots darker than their surroundings (dust on a print, emulsion specks).","detection"},
 {"remove_light","Remove light spots",true,1,1,0,nullptr,false,"Repair spots brighter than their surroundings (dirt on a negative, scratches through to the base).","detection"},
 {"sensitivity","Sensitivity",false,.75,1,0,nullptr,false,"How readily a temporary change is treated as dirt. Higher finds fainter spots and risks repairing real detail.","detection"},
 {"extra_sensitivity","Extra sensitivity",true,0,1,0,nullptr,false,"For heavily damaged material: drop detail preservation wherever enough neighbouring frames agree. Can remove real detail.","detection"},
 {"analysis_detail","Analysis detail",false,.5,1,0,nullptr,false,"Balance of fine versus coarse analysis when separating dirt from grain and noise.","detection"},
 {"limit_spot_size","Limit spot size",true,0,1,0,nullptr,false,"Ignore spots larger than Largest spot.","detection"},
 {"largest_spot","Largest spot (px)",false,25,256,1,nullptr,true,"Largest spot diameter repaired when Limit spot size is on.","detection"},
 {"extended_range","Extended-range input",true,0,1,0,nullptr,false,"Analyse values below 0 or above 1 (scene-referred or log-to-linear material) instead of the normal display range.","detection"},
 {"repair_amount","Repair amount",false,1,2,0,nullptr,false,"How strongly detected spots are replaced from neighbouring frames.","processing"},
 {"blend_repairs","Blend repairs",true,1,1,0,nullptr,false,"Soften the edges of repairs so they blend with grain.","processing"},
 {"blend_amount","Blend amount",false,.5,1,0,nullptr,false,"How much repair edges are softened.","processing"},
 {"motion_guard","Motion guard",false,.25,1,0,nullptr,false,"Protect moving detail from being mistaken for dirt. Higher protects more and cleans less in motion.","safeguards"},
 {"shot_changes","Shot changes",false,1,2,0,shotChoices,true,"Frames from another shot are never used for repair. Off: no extra care. Guard: repair frames next to a shot change more cautiously. Skip: leave frames next to a shot change unrepaired.","safeguards"},
 {"shot_guard","Shot-change guard",false,.75,1,0,nullptr,false,"How cautious Guard is next to a shot change.","safeguards"},
 {"flash_guard","Flash guard",true,0,1,0,nullptr,false,"Leave frames detected as single-frame flashes unrepaired.","safeguards"}};
bool isExtraParam(const char*name);
// ADR_HASH_LOG=<folder>: append the SHA-256 of every float32 RGB frame handed to and returned
// by the engine (top-down, interleaved), so a render in any host, including GUI-only ones, can be
// checked against adr_bench afterwards. Off unless the variable is set.
const char* hashLogDir(){static const char* d=std::getenv("ADR_HASH_LOG");return d&&*d?d:nullptr;}
void hashLog(const std::string& line){
 static std::mutex m;std::lock_guard<std::mutex> l(m);
#ifdef _WIN32
 static const std::string path=std::string(hashLogDir())+"/adr-hashes-"+std::to_string(_getpid())+".log";
#else
 static const std::string path=std::string(hashLogDir())+"/adr-hashes-"+std::to_string(getpid())+".log";
#endif
 if(FILE*f=std::fopen(path.c_str(),"a")){std::fputs((line+"\n").c_str(),f);std::fclose(f);}
}
// ADR_DUMP_DIR=<folder> (diagnostic, with the hash log): also write each logged frame as raw
// float32 RGB, <what>-<frame>.f32, so a host's delivered input can be replayed through the engine.
const char* dumpDir(){static const char* d=std::getenv("ADR_DUMP_DIR");return d&&*d?d:nullptr;}
void hashFrame(const char* what,int frame,const float* rgb,size_t samples){
 if(!hashLogDir())return;
 adr_tools::Sha256 s;s.update(rgb,samples*sizeof(float));hashLog(std::string(what)+" "+std::to_string(frame)+" "+s.hex());
 if(dumpDir()){
  char name[32];std::snprintf(name,sizeof name,"/%s-%05d.f32",what,frame);
  if(FILE*f=std::fopen((std::string(dumpDir())+name).c_str(),"wb")){std::fwrite(rgb,sizeof(float),samples,f);std::fclose(f);}
 }
}
// One process-wide GPU context, shared by every instance. Renders are already
// serialized by the plugin lock; the context also locks internally.
adr_context* engineContext=nullptr;
std::string engineBackend;
// Row-parallel loop for host image conversion (CPU-bound copies).
template<class F> void parallelRows(int h,F f){
 const int n=std::max(1,std::min<int>(16,int(std::thread::hardware_concurrency())));
 if(n==1||h<64){f(0,h);return;}
 std::vector<std::thread> pool;const int step=(h+n-1)/n;
 for(int k=0;k<n;++k){int a=k*step,b=std::min(h,a+step);if(a<b)pool.emplace_back([=,&f]{f(a,b);});}
 for(auto&t:pool)t.join();
}
inline bool finiteBits(float v){uint32_t b;std::memcpy(&b,&v,4);return (b&0x7f800000u)!=0x7f800000u;}
// ADR_TRACE=<file>: append one line per render request
// (instance, time, live sequence position, action, milliseconds).
void trace(const void* instance,int time,int live,const char* what,double ms,double fetch=0,double engine=0){
 static const char* path=std::getenv("ADR_TRACE");if(!path||!*path)return;
 static std::mutex m;std::lock_guard<std::mutex> g(m);
 if(FILE* f=std::fopen(path,"a")){std::fprintf(f,"%p %d %d %s %.2f fetch=%.2f engine=%.2f\n",instance,time,live,what,ms,fetch,engine);std::fclose(f);}
}
std::mutex contextMutex;
adr_context* context(){
 std::lock_guard<std::mutex> guard(contextMutex);
 if(engineContext)return engineContext;
 const char*choice=std::getenv("ADR_BACKEND");
 char error[1024]={};
 const auto t0=std::chrono::steady_clock::now();
 if(choice&&*choice)engineContext=adr_context_create(choice,error,sizeof(error));
 else{
  // "auto", one backend at a time in the engine's own order, so the log records why each one was skipped.
  for(const char*backend:{"metal","cuda","opencl","cpu"}){
   error[0]=0;engineContext=adr_context_create(backend,error,sizeof(error));
   if(engineContext)break;
   if(std::strstr(error,"not built"))continue;
   adrlog::info(std::string("GPU backend ")+backend+" unavailable: "+error);
  }
 }
 if(!engineContext){adrlog::error(std::string("no usable GPU or CPU backend: ")+error);require(false,std::string("Advanced Dirt Removal could not start a GPU or CPU engine: ")+error,kOfxStatFailed);}
 engineBackend=std::string(adr_context_backend(engineContext))+" / "+adr_context_device_name(engineContext);
 if(!std::strcmp(adr_context_backend(engineContext),"cpu"))adrlog::warn("no usable GPU: running on the CPU, which is much slower (set ADR_BACKEND to choose a backend)");
 adrlog::info("GPU: "+engineBackend+" (requested "+(choice&&*choice?choice:"auto")+", ready in "+std::to_string(int(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()))+" ms); CPU table "+(adr_reciprocal_table()?adr_reciprocal_table():"?"));
 return engineContext;
}
struct CleaningProfileUI {
 int previousMode=0,depth=0;
 bool modeChanged=false,userMode=false,controlsRestored=false,profilesRestored=false;
};
struct Instance {
 OfxImageClipHandle source=nullptr,output=nullptr,mask=nullptr,original=nullptr;
 std::map<std::string,OfxParamHandle> parameters;
 adr_sequence* sequence=nullptr;
 adr_history* history=nullptr; // motion-history checkpoints for key
 std::string key;            // controls, range and geometry of the live sequence
 int first=0;
 uint64_t generation=0;
 std::map<int,uint64_t> sourcePrints; // validate local hits too, including after an upstream edit
 int cachedFrame=std::numeric_limits<int>::min();
 int frameW=0,frameH=0; // last rendered size, so frames-needed can find a cached Spanish clean
 frame_cache::Cache::Frame cached;  // immutable shared output, top-down packed RGB
 // While Paint in viewer is on: the last few cleaned frames, so stepping back
 // and forth to paint does not clean them again. Same key as the sequence.
 std::map<int,frame_cache::Cache::Frame> recent;
 paint::LayerCache paintLayers;
 paint_source::FrameCache originalSource;
 paint_source::PairCache preparedFrames;
 std::vector<float> scratch;
 std::vector<float> prefetch;  // next source frame, fetched while the GPU renders
 int prefetchTime=std::numeric_limits<int>::min();
 std::string prefetchKey;
 std::mutex lock;            // serializes this instance's actions
 std::string recoveryId,recoveryIdFor; // auto-recovery shot folder and what it was computed for
 // UI callbacks must never wait for the render lock: fetching an image can
 // itself need the UI thread. Analysis requests/status cross that boundary
 // through atomics; host parameter writes stay in UI/interact callbacks.
 enum RecoveryState {Idle,Queued,Running,Complete,Cancelled,Failed};
 std::atomic<bool> calculateRequested{false},cancelCalculation{false};
 std::atomic<int> recoveryState{Idle},recoveryDone{0},recoveryTotal{0};
 std::atomic<double> recoverySeconds{0};
 std::atomic<int> recoveryPreviousAuto{0};
 std::atomic<bool> restoreAutoOnCancel{false};
 std::shared_ptr<recovery_feedback::State> recoveryFeedback;
 std::shared_ptr<recovery_feedback::State> prepareFeedback;
 std::atomic<bool> prepareRequested{false};
 CleaningProfileUI cleaningProfile;  // UI callbacks only; saved values live in host parameters
 // Diagnostic log: instance number and the live sequence's statistics.
 int id=0;
 std::string label()const{return "instance #"+std::to_string(id);}
 int rendered=0,servedCached=0;double renderMs=0;float sourceMin=0,sourceMax=0;bool sourceSeen=false,flatWarned=false,rangeWarned=false,originalLogged=false,originalWarned=false;int comparedFrame=std::numeric_limits<int>::min(),compareState=-1;
 void summary(const char*why){
  if(sequence&&(rendered||servedCached)){
   std::ostringstream s;s.precision(4);
   s<<label()<<" sequence ended ("<<why<<"): "<<rendered<<" frames rendered, "<<servedCached<<" served from cache, "<<(rendered?renderMs/rendered:0.)<<" ms/frame";
   if(sourceSeen)s<<", source values "<<sourceMin<<".."<<sourceMax;
   if(engineContext){uint64_t a=0,p=0;adr_context_memory(engineContext,&a,&p);s<<", GPU memory peak "<<(p>>20)<<" MiB";}
   adrlog::info(s.str());
  }
  rendered=servedCached=0;renderMs=0;sourceSeen=flatWarned=rangeWarned=false;
 }
 void close(){sourcePrints.clear();originalSource.clear();preparedFrames.clear();summary("closed");prefetchTime=std::numeric_limits<int>::min();const bool live=sequence!=nullptr;if(sequence)adr_sequence_destroy(sequence);sequence=nullptr;if(history)adr_history_destroy(history);history=nullptr;key.clear();cachedFrame=std::numeric_limits<int>::min();cached.reset();recent.clear();
  // A closed sequence (new settings or clip, error, instance gone) leaves its
  // buffers in the engine's reuse pool. Release them: the next sequence may use
  // other sizes, and on unified-memory Macs pooled buffers are system RAM.
  if(live&&engineContext)adr_context_trim(engineContext);}
 // Host memory purges drop frames and GPU buffers but keep the few-KB
 // motion-history checkpoints, which stay valid for the same key.
 void purge(){originalSource.clear();preparedFrames.clear();summary("host purged caches");prefetchTime=std::numeric_limits<int>::min();prefetch.clear();prefetch.shrink_to_fit();if(sequence)adr_sequence_destroy(sequence);sequence=nullptr;if(engineContext)adr_context_trim(engineContext);cachedFrame=std::numeric_limits<int>::min();cached.reset();paintLayers.clear();recent.clear();scratch.clear();scratch.shrink_to_fit();}
 ~Instance(){auto preparation=std::atomic_load(&prepareFeedback);if(preparation){preparation->cancel=true;if(preparation->phase<recovery_feedback::State::Complete)preparation->phase=recovery_feedback::State::Cancelled;}auto feedback=std::atomic_load(&recoveryFeedback);if(feedback){int busy=recovery_feedback::State::Queued;if(!feedback->phase.compare_exchange_strong(busy,recovery_feedback::State::Cancelled)){busy=recovery_feedback::State::Running;feedback->phase.compare_exchange_strong(busy,recovery_feedback::State::Cancelled);}feedback->cancel=true;}close();}
};
Instance& instance(OfxImageEffectHandle e){void*p=nullptr;check(prop->propGetPointer(properties(e),kOfxPropInstanceData,0,&p),"instance data");require(p,"missing instance",kOfxStatErrBadHandle);return *static_cast<Instance*>(p);}
void updateRecoveryEnabled(Instance&);
void refreshRecoveryUI(OfxImageEffectHandle);
bool paintHost();
std::array<int,2> sourceRange(Instance&);
#include "paint_ui.h"
#include "cleaning_profiles.h"
#include "user_defaults.h"
std::array<int,2> sourceRange(Instance&i){
 OfxPropertySetHandle p;check(fx->clipGetPropertySet(i.source,&p),"source properties");
 double first=std::ceil(number(p,kOfxImageEffectPropFrameRange,0)),last=std::floor(number(p,kOfxImageEffectPropFrameRange,1));
 require(first>=-1e9&&last<=1e9&&first<=last&&last-first<1000000,"invalid source frame range");return {int(first),int(last)};
}
adr_controls readControls(Instance&i,double time,std::string&identity){
 adr_controls c;adr_controls_default(&c);std::ostringstream out;out.precision(17);
 for(auto&k:controls){
  if(k.boolean||k.integral){int v=0;check(params->paramGetValueAtTime(i.parameters.at(k.name),time,&v),"integer control");require(v>=k.minimum&&v<=k.maximum,"invalid integer control");out<<k.name<<'='<<v<<';';
   std::string n=k.name;
   if(n=="enabled")c.enabled=v;else if(n=="analyse")c.mode=v;else if(n=="largest_spot")c.largest_spot=v;else if(n=="shot_changes")c.shot_changes=v;
   else if(n=="extended_range")c.extended_range=v;else if(n=="flash_guard")c.flash_guard=v;else if(n=="limit_spot_size")c.limit_spot_size=v;
   else if(n=="extra_sensitivity")c.extra_sensitivity=v;else if(n=="remove_dark")c.remove_dark=v;else if(n=="remove_light")c.remove_light=v;else if(n=="blend_repairs")c.blend_repairs=v;
  }else{double v=0;check(params->paramGetValueAtTime(i.parameters.at(k.name),time,&v),"numeric control");require(std::isfinite(v)&&v>=0&&v<=k.maximum,"invalid numeric control");
   // The engine evaluates controls in float32.
   float f=float(v);out<<k.name<<'='<<f<<';';std::string n=k.name;
   if(n=="repair_amount")c.repair_amount=f;else if(n=="analysis_detail")c.analysis_detail=f;else if(n=="sensitivity")c.sensitivity=f;
   else if(n=="motion_guard")c.motion_guard=f;else if(n=="shot_guard")c.shot_guard=f;else if(n=="blend_amount")c.blend_amount=f;}
 }
 identity=out.str();return c;
}
// Reads one temporal source frame as packed top-down float RGB.
void readSource(Instance&i,OfxImageEffectHandle e,int time,int w,int h,const std::array<int,4>&bounds,std::vector<float>&rgb){
 auto th=std::chrono::steady_clock::now();
 Image image(i.source,time,e,"temporal source");require(image.b==bounds&&image.width()==w&&image.height()==h,"source geometry changed");
 const double hostMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-th).count();
 auto tc=std::chrono::steady_clock::now();
 rgb.resize(size_t(w)*h*3);
 require(!fx->abort(e),"render aborted",kOfxStatFailed);
 // RGB is processed exactly as delivered and alpha passes through. Hosts
 // label alpha inconsistently (Nuke: alpha 0 marked opaque, then
 // premultiplied downstream), so premultiplication is not enforced here;
 // unpremultiply genuinely semi-transparent content upstream. Finite
 // excursions (superwhites, negatives) are processed as delivered.
 std::atomic<bool> finite{true};
 std::mutex rangeLock;float lo=std::numeric_limits<float>::infinity(),hi=-lo;
 parallelRows(h,[&](int y0,int y1){
  bool ok=true;float mn=std::numeric_limits<float>::infinity(),mx=-mn;
  for(int y=y0;y<y1;++y){
   float*row=rgb.data()+size_t(y)*w*3;
   if(image.depth==PixelDepth::Float){
    const float*src=reinterpret_cast<const float*>(image.row(y));const int ch=image.channels;
    for(int x=0;x<w;++x){const float*p=src+size_t(x)*ch;float*q=row+size_t(x)*3;q[0]=p[0];q[1]=p[1];q[2]=p[2];ok&=finiteBits(p[0])&finiteBits(p[1])&finiteBits(p[2]);}
   }else{
    for(int x=0;x<w;++x)for(int c=0;c<3;++c){float v=image.read(x,y,c);row[size_t(x)*3+c]=v;ok&=finiteBits(v);}
   }
   if(ok)for(size_t k=0;k<size_t(w)*3;++k){mn=std::min(mn,row[k]);mx=std::max(mx,row[k]);}
  }
  if(!ok)finite=false;
  std::lock_guard<std::mutex> g(rangeLock);lo=std::min(lo,mn);hi=std::max(hi,mx);
 });
 if(!finite.load())adrlog::error(i.label()+" source frame "+std::to_string(time)+" contains NaN or infinity");
 require(finite.load(),"Advanced Dirt Removal requires finite RGB input (NaN or infinity found)");
 // Diagnostics: a flat frame usually means the host delivered no picture (offline media, failed decode).
 if(!i.sourceSeen){
  i.sourceMin=lo;i.sourceMax=hi;i.sourceSeen=true;
  adrlog::info(i.label()+" source frames are "+(image.depth==PixelDepth::Float?"float":image.depth==PixelDepth::Half?"half":"16-bit")+(image.channels==4?" RGBA":" RGB")+(image.premult?" premultiplied":"")+", first fetch "+std::to_string(int(hostMs))+" ms from the host");
 }else{i.sourceMin=std::min(i.sourceMin,lo);i.sourceMax=std::max(i.sourceMax,hi);}
 if(lo==hi&&!i.flatWarned){i.flatWarned=true;adrlog::warn(i.label()+" source frame "+std::to_string(time)+" is flat (every value "+std::to_string(lo)+"): the host may have delivered no picture");}
 if((lo<0.f||hi>1.f)&&!i.rangeWarned){i.rangeWarned=true;adrlog::info(i.label()+" source frame "+std::to_string(time)+" has values outside 0..1 ("+std::to_string(lo)+".."+std::to_string(hi)+"); processed as delivered");}
 trace(&i,time,-1,image.depth==PixelDepth::Float?(image.channels==4?"fetch-float-rgba":"fetch-float-rgb"):"fetch-other",hostMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tc).count(),0);
}
// The Additional source input (the aligned original of a processed picture), packed RGB like readSource.
bool clipConnected(OfxImageClipHandle clip){
 if(!clip)return false;OfxPropertySetHandle cp=nullptr;if(fx->clipGetPropertySet(clip,&cp)!=kOfxStatOK)return false;
 int connected=0;prop->propGetInt(cp,kOfxImageClipPropConnected,0,&connected);return connected!=0;
}
// The additional source arrives on the Additional source input; when a host
// connects the picture to the second (mask) input instead, that is used.
OfxImageClipHandle additionalClip(Instance&i){return clipConnected(i.original)?i.original:clipConnected(i.mask)?i.mask:nullptr;}
bool originalConnected(Instance&i){return additionalClip(i)!=nullptr;}
std::shared_ptr<const std::vector<float>> readOriginal(Instance&i,OfxImageEffectHandle e,int time,int w,int h){
 Image image(additionalClip(i),time,e,"additional source");
 require(image.width()==w&&image.height()==h,"The additional source is "+std::to_string(image.width())+"x"+std::to_string(image.height())+"; it must match the processed picture ("+std::to_string(w)+"x"+std::to_string(h)+")");
 auto rgb=std::make_shared<std::vector<float>>(size_t(w)*h*3);std::atomic<bool> finite{true};
 parallelRows(h,[&](int y0,int y1){bool ok=true;for(int y=y0;y<y1;++y){float*row=rgb->data()+size_t(y)*w*3;
  if(image.depth==PixelDepth::Float){const float*src=reinterpret_cast<const float*>(image.row(y));const int ch=image.channels;for(int x=0;x<w;++x){const float*p=src+size_t(x)*ch;float*q=row+size_t(x)*3;q[0]=p[0];q[1]=p[1];q[2]=p[2];ok&=finiteBits(p[0])&finiteBits(p[1])&finiteBits(p[2]);}}
  else for(int x=0;x<w;++x)for(int c=0;c<3;++c){float v=image.read(x,y,c);row[size_t(x)*3+c]=v;ok&=finiteBits(v);}}
  if(!ok)finite=false;});
 require(finite.load(),"The additional source contains NaN or infinity");
 return rgb;
}
// Startup summary for the diagnostic log: plugin build, host, machine and overrides.
std::string hostText(const char*k){char*v=nullptr;return host&&host->host&&prop->propGetString(host->host,k,0,&v)==kOfxStatOK&&v?v:"";}
// Resolve and Fusion are resolution-independent: a mask made at another resolution covers the same picture.
// Other hosts place every image on one pixel canvas, and may hand the mask over cropped to its data window.
bool positionMappedMasks(){std::string n=hostText(kOfxPropName);for(char&c:n)c=char(std::tolower((unsigned char)c));return n.find("resolve")!=std::string::npos||n.find("fusion")!=std::string::npos||n.find("blackmagic")!=std::string::npos;}
// Viewer painting needs the host's overlay suite; it is offered in DaVinci Resolve,
// where the Color and Edit pages have no mask paint tool of their own.
bool paintHost(){if(!interacts)return false;std::string n=hostText(kOfxPropName);for(char&c:n)c=char(std::tolower((unsigned char)c));return n.find("resolve")!=std::string::npos;}
void logStartup(){
 std::array<int,3> version{};if(host&&host->host)prop->propGetIntN(host->host,kOfxPropVersion,3,version.data());
 adrlog::info(std::string("Advanced Dirt Removal ")+ADR_PLUGIN_VERSION+" (source "+ADR_SOURCE_REVISION+", built "+__DATE__+" "+__TIME__+"), engine "+adr_version()+", kernels "+std::string(adr_kernel_source_sha256()).substr(0,12));
 adrlog::info("plugin binary: "+adrlog::pluginPath());
 adrlog::info("shared cleaned-frame cache budget: "+std::to_string(frame_cache::shared().capacity()>>20)+" MiB (ADR_FRAME_CACHE_MB overrides; 0 disables)");
 adrlog::info("host: "+hostText(kOfxPropName)+" / "+hostText(kOfxPropLabel)+" "+hostText(kOfxPropVersionLabel)+" ("+std::to_string(version[0])+"."+std::to_string(version[1])+"."+std::to_string(version[2])+"), process "+adrlog::executablePath());
 char memory[32];std::snprintf(memory,sizeof memory,"%.1f GB RAM",adrlog::memoryGiB());
 adrlog::info("system: "+adrlog::osName()+"; "+adrlog::cpuName()+", "+std::to_string(std::thread::hardware_concurrency())+" threads, "+memory);
 adrlog::info("environment overrides: "+adrlog::overrides());
 if(hashLogDir())adrlog::info(std::string("frame hash log on: ")+hashLogDir()+(dumpDir()?std::string(", raw frames to ")+dumpDir():std::string()));
}
std::string who(OfxImageEffectHandle e){if(!e||!isLiveInstance(e))return "plugin";try{return instance(e).label();}catch(...){return "plugin";}}
void reportError(OfxImageEffectHandle e,const char*text){
 // Descriptor and Load actions do not have instance handles. Some hosts
 // dereference an instance in their message suite even for a descriptor.
 if(!e||!isLiveInstance(e))return;
 const std::string log=adrlog::path();const std::string full=log.empty()?std::string(text):std::string(text)+"\nDetails in the Advanced Dirt Removal log: "+log;
 if(persistentMessages&&e)persistentMessages->setPersistentMessage(e,kOfxMessageError,"adr","%s",full.c_str());
 else if(messages)messages->message(e,kOfxMessageLog,"adr","%s",full.c_str());
}
bool invalidatesHistory(OfxPropertySetHandle in){
 if(!in)return true;
 char*type=nullptr;char*name=nullptr;
 if(prop->propGetString(in,kOfxPropType,0,&type)==kOfxStatOK&&type&&std::strcmp(type,kOfxTypeParameter)==0&&prop->propGetString(in,kOfxPropName,0,&name)==kOfxStatOK&&name){
  // Every render sends every control value. The adapter keys its sequence by
  // those exact values, so even genuine parameter edits safely replay.
  // Some hosts resend every value per frame; notifications alone are not edits.
  for(auto&control:controls)if(std::strcmp(name,control.name)==0)return false;
  if(isExtraParam(name))return false;
 }
 char*reason=nullptr;auto status=prop->propGetString(in,kOfxPropChangeReason,0,&reason);
 // Controls are non-animated and the stream already binds frame indices.
 // Timeline cursor notifications do not alter source content or settings.
 return status!=kOfxStatOK||!reason||std::strcmp(reason,kOfxChangeTime)!=0;
}
// Progressive renders may arrive labelled "Both" fields; accepted only when both
// clips are progressive. Some hosts then report an inclusive render window
// (one pixel short on the right and top) while image bounds stay exclusive;
// that exact full-frame case is widened to the bounds.
std::array<int,4> renderWindow(OfxImageEffectHandle e,OfxPropertySetHandle in,const std::array<int,4>&bounds){
 std::array<int,4> window{};check(prop->propGetIntN(in,kOfxImageEffectPropRenderWindow,4,window.data()),"render window");
 auto field=str(in,kOfxImageEffectPropFieldToRender);
 if(field!=kOfxImageFieldNone){
  require(field==kOfxImageFieldBoth,"field renders unsupported");
  for(auto role:{"Source","Output"}){OfxImageClipHandle clip=nullptr;OfxPropertySetHandle cp=nullptr;
   check(fx->clipGetHandle(e,role,&clip,&cp),"progressive clip");
   if(!cp)check(fx->clipGetPropertySet(clip,&cp),"progressive clip properties");
   require(cp&&str(cp,kOfxImageClipPropFieldOrder)==kOfxImageFieldNone,"field renders unsupported: progressive clips required");}
  if(window==std::array<int,4>{bounds[0],bounds[1],bounds[2]-1,bounds[3]-1})window=bounds;
 }
 require(window[0]>=bounds[0]&&window[1]>=bounds[1]&&window[2]<=bounds[2]&&window[3]<=bounds[3]&&window[0]<=window[2]&&window[1]<=window[3],"invalid render window");
 return window;
}
// ---- Encoding, auto recovery, recovery mask, limits and views ------------------
// Everything after the cleaning engine: none of these controls changes the cleaning
// itself (except Input encoding, which is part of the sequence key).
//
// Auto recovery: a classifier trained on restorers' decisions marks the repaired spots
// that were real picture detail. "Calculate" stores a per-frame probability map for the
// shot (keyed by the shot and its cleaning controls); renders turn it into a matte at
// the chosen confidence. The Mask input edits it: unpainted areas follow the matte,
// white strokes give the original back, black strokes keep the repair.
const char*const spanishRecoveryError="Auto recovery cannot be used with Spanish dirt removal. Set Auto recovery to Off, or turn Spanish dirt removal off.";
const char*const encodingChoices[]={"Video","Cineon log"};
const char*const recoveryModeChoices[]={"Off","On"};
const char*const maskModeChoices[]={"Recovery","Spanish dirt removal"};
const char*const viewChoices[]={"Restored","Red difference","True difference","Auto recovery matte"};
// View choice -> internal view (0 restored, 1 original, 2 wipe, 3 true difference, 4 red, 5 eligibility, 6 changed, 7 matte).
const int viewOfChoice[]={0,4,3,7};
struct Extra{const char*name;const char*label;const char*hint;const char*group;int kind;double initial,minimum,maximum;const char*const*choices=nullptr;int count=0;};
enum{ExtraChoice,ExtraBool,ExtraDouble,ExtraInt,ExtraString,ExtraButton};
const Extra extras[]={
 {"mask_mode","Mode","Recovery keeps original detail inside the mask. Spanish dirt removal repairs only inside the mask. In Resolve, each method remembers its own cleaning settings.","auto_recovery_group",ExtraChoice,0,0,1,maskModeChoices,2},
 {"recovery_calculate","Calculate recovery","Calculate the auto-recovery matte for the whole shot. Progress is shown during calculation; click again to cancel. Valid saved mattes are reused.","auto_recovery_group",ExtraButton,0,0,0},
 {"recovery_status","Calculation","Status and timing of this instance's whole-shot recovery calculation.","auto_recovery_group",ExtraString,0,0,0},
 {"recovery_request","Recovery request","Internal: changed by Calculate recovery so the host renders again.","auto_recovery_group",ExtraInt,0,0,1000000},
 {"auto_recovery","Auto recovery","On: give back repaired spots the model judges to be real picture detail. The matte is calculated once per shot and cleaning settings, and recalculated automatically when Input encoding or any cleaning control changes (Calculate recovery does the whole shot at once).","auto_recovery_group",ExtraChoice,0,0,1,recoveryModeChoices,2},
 {"recovery_confidence","Confidence","Give back only spots the model is at least this sure were real picture detail. Higher gives back less.","auto_recovery_group",ExtraDouble,.8,.5,.99},
 {"recovery_folder","Matte folder","Where calculated mattes are stored. Empty: the user's application data folder.","auto_recovery_group",ExtraString,0,0,0},
 {"cleaning_profiles","Saved cleaning configurations","Internal: independent Automatic/Recovery and Spanish cleaning settings.","auto_recovery_group",ExtraString,0,0,0},
 {"cleaning_default_save","Save as my default","Save the selected method's cleaning settings for this user, for explicit reuse across nodes and projects.","cleaning_defaults_group",ExtraButton,0,0,0},
 {"cleaning_default_load","Load my default","Load this user's saved cleaning settings for the selected method into this node. Other mode settings and shared controls are kept.","cleaning_defaults_group",ExtraButton,0,0,0},
 {"cleaning_default_factory","Use factory settings","Restore the selected method's factory cleaning settings in this node. Saved user defaults are kept; use Save as my default to replace them.","cleaning_defaults_group",ExtraButton,0,0,0},
 {"cleaning_default_status","Defaults","Feedback for the selected method's user defaults.","cleaning_defaults_group",ExtraString,0,0,0},
 {"view","View","Restored: the result. Red difference: grey picture with every repair in red. True difference: the change, amplified. Auto recovery matte: where auto recovery gives back the original.",nullptr,ExtraChoice,0,0,3,viewChoices,4},
 {"difference_threshold","Difference threshold (%)","Red difference: changes smaller than this are not shown in red. The engine moves unrepaired pixels by less than one 10-bit code, so 0.1% shows only real repairs.","view_group",ExtraDouble,.1,0,100},
 {"red_intensity","Red difference intensity","Brightness of the red highlights.","view_group",ExtraDouble,8,0,100},
 {"red_opacity","Red highlight opacity","Opacity of the red highlights.","view_group",ExtraDouble,.85,0,1},
 {"gray_background","Grayscale background","Brightness of the grey picture behind the red highlights (0 = black).","view_group",ExtraDouble,1,0,1},
 {"difference_gain","Difference gain","Gain of the True difference view.","view_group",ExtraDouble,4,0,100},
};
bool isExtraParam(const char*name){if(!std::strcmp(name,"encoding")||paint_ui::isParam(name))return true;for(auto&x:extras)if(!std::strcmp(x.name,name))return true;return false;}
void defineEncodingParam(OfxParamSetHandle ps,const char*parent=nullptr){
 OfxPropertySetHandle p;check(params->paramDefine(ps,kOfxParamTypeChoice,"encoding",&p),"define encoding");
 ss(p,kOfxPropLabel,"Input encoding");if(parent)ss(p,kOfxParamPropParent,parent);ss(p,kOfxParamPropHint,"Video / already converted: values as delivered. Cineon log: Cineon-encoded film scans; cleaning works on linear values and the repair is put back in log, so untouched pixels keep their exact values.");
 si(p,kOfxParamPropAnimates,0);for(int k=0;k<2;++k)ss(p,kOfxParamPropChoiceOption,encodingChoices[k],k);si(p,kOfxParamPropDefault,0);
}
// Defines one group of extras (nullptr: the top-level ones) in display order.
void defineExtraParam(OfxParamSetHandle ps,const Extra&x,const char*parent,const char*label=nullptr){
 OfxPropertySetHandle p;
 const char*type=x.kind==ExtraChoice?kOfxParamTypeChoice:x.kind==ExtraBool?kOfxParamTypeBoolean:x.kind==ExtraDouble?kOfxParamTypeDouble:x.kind==ExtraInt?kOfxParamTypeInteger:x.kind==ExtraString?kOfxParamTypeString:kOfxParamTypePushButton;
  check(params->paramDefine(ps,type,x.name,&p),"define extra control");ss(p,kOfxPropLabel,label?label:x.label);ss(p,kOfxParamPropHint,x.hint);if(parent)ss(p,kOfxParamPropParent,parent);
  if(label&&!std::strcmp(x.name,"mask_mode"))ss(p,kOfxParamPropHint,"Recovery: paint original detail back over automatic cleanup. Spanish dirt removal: apply cleanup only where painted. Each method remembers its Detection, Processing and Safeguards settings.");
  if(x.kind==ExtraButton)return;
  si(p,kOfxParamPropAnimates,0);
  if(x.kind==ExtraChoice){for(int k=0;k<x.count;++k)ss(p,kOfxParamPropChoiceOption,x.choices[k],k);si(p,kOfxParamPropDefault,int(x.initial));}
  else if(x.kind==ExtraBool)si(p,kOfxParamPropDefault,int(x.initial));
  else if(x.kind==ExtraInt){if(!std::strcmp(x.name,"recovery_request")){si(p,kOfxParamPropSecret,1);si(p,kOfxParamPropEvaluateOnChange,1);}si(p,kOfxParamPropDefault,int(x.initial));for(auto k:{kOfxParamPropMin,kOfxParamPropDisplayMin})si(p,k,int(x.minimum));for(auto k:{kOfxParamPropMax,kOfxParamPropDisplayMax})si(p,k,int(x.maximum));}
  else if(x.kind==ExtraDouble){for(auto v:{std::make_pair(kOfxParamPropDefault,x.initial),{kOfxParamPropMin,x.minimum},{kOfxParamPropMax,x.maximum},{kOfxParamPropDisplayMin,x.minimum},{kOfxParamPropDisplayMax,x.maximum}})check(prop->propSetDouble(p,v.first,0,v.second),"extra range");}
  else if(x.kind==ExtraString){
   const bool defaultStatus=!std::strcmp(x.name,"cleaning_default_status");
   const bool status=!std::strcmp(x.name,"recovery_status")||defaultStatus;
   const bool profiles=!std::strcmp(x.name,"cleaning_profiles");
   ss(p,kOfxParamPropStringMode,status?kOfxParamStringIsLabel:profiles?kOfxParamStringIsMultiLine:kOfxParamStringIsDirectoryPath);
   ss(p,kOfxParamPropDefault,defaultStatus?"Save or load defaults for the selected method":status?"Not calculated in this session":"");
   if(status){si(p,kOfxParamPropEvaluateOnChange,0);si(p,kOfxParamPropCanUndo,0);si(p,kOfxParamPropPersistant,0);}
   if(profiles){si(p,kOfxParamPropSecret,1);si(p,kOfxParamPropEvaluateOnChange,0);si(p,kOfxParamPropCanUndo,1);si(p,kOfxParamPropPersistant,1);}
  }
}
void defineExtraParams(OfxParamSetHandle ps,const char*group){
 if(group){OfxPropertySetHandle p;check(params->paramDefine(ps,kOfxParamTypeGroup,group,&p),"define group");ss(p,kOfxPropLabel,!std::strcmp(group,"auto_recovery_group")?"Recovery":"View options");si(p,kOfxParamPropGroupOpen,!std::strcmp(group,"auto_recovery_group"));}
 for(auto&x:extras){const bool top=!x.group;if(group?(top||std::strcmp(x.group,group)):!top)continue;defineExtraParam(ps,x,x.group);}
}
void defineUserDefaults(OfxParamSetHandle ps,const char*parent=nullptr){
 OfxPropertySetHandle p;check(params->paramDefine(ps,kOfxParamTypeGroup,"cleaning_defaults_group",&p),"define cleaning defaults");
 ss(p,kOfxPropLabel,"Cleaning defaults");si(p,kOfxParamPropGroupOpen,0);
 if(parent)ss(p,kOfxParamPropParent,parent);else si(p,kOfxParamPropSecret,1);
 for(const auto&x:extras)if(x.group&&!std::strcmp(x.group,"cleaning_defaults_group")){
  defineExtraParam(ps,x,x.group);
  if(!parent){OfxParamHandle h=nullptr;check(params->paramGetHandle(ps,x.name,&h,&p),"default control");si(p,kOfxParamPropSecret,1);}
 }
}
// Inspector tabs (OFX pages). Children are listed in display order.
void definePages(OfxParamSetHandle ps,bool paintHost){
 std::vector<std::string> cleaning={"encoding","view"};
 for(auto&c:controls)if(std::strcmp(c.name,"enabled"))cleaning.push_back(c.name);
 if(paintHost)for(auto&x:extras)if(x.group&&!std::strcmp(x.group,"cleaning_defaults_group"))cleaning.push_back(x.name);
 std::vector<std::string> recovery={"recovery_calculate","recovery_status","auto_recovery","recovery_confidence","recovery_folder"};
 if(!paintHost)recovery.insert(recovery.begin(),"mask_mode");
 const std::vector<std::string> view={"difference_threshold","red_intensity","red_opacity","gray_background","difference_gain"};
 struct Page{const char*name;const char*label;const std::vector<std::string>*kids;};
 const std::vector<std::string> paintPage={
  "mask_mode","paint","paint_outlines","paint_tool","paint_shape","paint_size","paint_softness","paint_undo","paint_redo",
  "paint_stroke_info","paint_previous","paint_next","paint_delete","paint_resume","paint_hold","paint_stroke_on","paint_rect_width","paint_rect_height",
  "paint_opacity","paint_smoothing","paint_pressure","paint_keys",
  "paint_source_kind","paint_source_folder","paint_source_offset","prepare_shot","prepare_folder"
 };
 const Page pages[]={{"page_cleaning","Cleaning",&cleaning},{"page_recovery","Recovery",&recovery},{"page_paint","Paint",&paintPage},{"page_view","View",&view}};
 for(auto&pg:pages){
  if(pg.kids==&paintPage&&!paintHost)continue;
  OfxPropertySetHandle p;check(params->paramDefine(ps,kOfxParamTypePage,pg.name,&p),"define page");ss(p,kOfxPropLabel,pg.label);
  for(size_t k=0;k<pg.kids->size();++k)ss(p,kOfxParamPropPageChild,(*pg.kids)[k].c_str(),int(k));
 }
}
// Display order is independent of controls[]: that array also keys recovery caches.
void defineCleaningControl(OfxParamSetHandle ps,const Control&c,const char*parent){
 OfxPropertySetHandle p;check(params->paramDefine(ps,c.boolean?kOfxParamTypeBoolean:c.choices?kOfxParamTypeChoice:c.integral?kOfxParamTypeInteger:kOfxParamTypeDouble,c.name,&p),"define control");ss(p,kOfxPropLabel,c.label);if(parent)ss(p,kOfxParamPropParent,parent);if(!std::strcmp(c.name,"enabled"))si(p,kOfxParamPropSecret,1);if(c.hint)ss(p,kOfxParamPropHint,c.hint);si(p,kOfxParamPropAnimates,0);if(c.boolean||c.integral){si(p,kOfxParamPropDefault,int(c.initial));if(c.choices){for(int index=0;index<=int(c.maximum);++index)ss(p,kOfxParamPropChoiceOption,c.choices[index],index);}else if(c.integral){for(auto key:{kOfxParamPropMin,kOfxParamPropDisplayMin})si(p,key,c.minimum);for(auto key:{kOfxParamPropMax,kOfxParamPropDisplayMax})si(p,key,int(c.maximum));}}else{for(auto v:{std::make_pair(kOfxParamPropDefault,c.initial),{kOfxParamPropMin,0.},{kOfxParamPropMax,c.maximum},{kOfxParamPropDisplayMin,0.},{kOfxParamPropDisplayMax,c.maximum}})check(prop->propSetDouble(p,v.first,0,v.second),"control range");}
}
void defineControlPresentation(OfxParamSetHandle ps){
 if(!paintHost()){
  defineEncodingParam(ps);std::set<std::string> definedGroups;
  for(auto&c:controls){
   if(c.group&&!definedGroups.count("top")){definedGroups.insert("top");defineExtraParams(ps,nullptr);defineExtraParams(ps,"auto_recovery_group");paint_ui::define(ps,false);}
   if(c.group&&definedGroups.insert(c.group).second)for(auto&g:groups)if(!std::strcmp(g.name,c.group)){OfxPropertySetHandle p;check(params->paramDefine(ps,kOfxParamTypeGroup,g.name,&p),"define group");ss(p,kOfxPropLabel,g.label);si(p,kOfxParamPropGroupOpen,!std::strcmp(g.name,"detection"));}
   defineCleaningControl(ps,c,c.group);
  }
  defineUserDefaults(ps);defineExtraParams(ps,"view_group");definePages(ps,false);return;
 }
 auto group=[&](const char*name,const char*label,bool open,const char*parent=nullptr){
  OfxPropertySetHandle p;check(params->paramDefine(ps,kOfxParamTypeGroup,name,&p),"define group");
  ss(p,kOfxPropLabel,label);si(p,kOfxParamPropGroupOpen,open);if(parent)ss(p,kOfxParamPropParent,parent);
 };
 auto extra=[&](const char*name,const char*parent){for(auto&x:extras)if(!std::strcmp(name,x.name)){defineExtraParam(ps,x,parent,!std::strcmp(name,"mask_mode")?"Method":nullptr);return;}require(false,"missing display extra");};
 defineEncodingParam(ps);extra("view",nullptr);defineExtraParams(ps,"view_group");
 const char*cleaningGroup="automatic_cleaning_group";
 group(cleaningGroup,"Automatic cleaning",false);
 // Preserve the original order inside the automatic cleaning section.
 std::set<std::string> definedGroups;
 for(auto&c:controls){
  if(c.group&&definedGroups.insert(c.group).second)for(auto&g:groups)if(!std::strcmp(g.name,c.group))group(g.name,g.label,!std::strcmp(g.name,"detection"),cleaningGroup);
  defineCleaningControl(ps,c,c.group);
 }
 group("auto_recovery_group","Auto recovery",false,cleaningGroup);
 for(auto name:{"auto_recovery","recovery_confidence","recovery_calculate","recovery_status","recovery_request","recovery_folder","cleaning_profiles"})extra(name,"auto_recovery_group");
 defineUserDefaults(ps,cleaningGroup);
 group("paint_group","Paint",true);
 extra("mask_mode","paint_group");
 paint_ui::define(ps,true,true);
 definePages(ps,true);
}
// Spanish dirt removal makes auto recovery irrelevant: grey its controls out while it is on.
void updateRecoveryEnabled(Instance&i){
 int spanish=0,prepared=0;if(params->paramGetValue(i.parameters.at("mask_mode"),&spanish)!=kOfxStatOK)return;params->paramGetValue(i.parameters.at("paint_source_kind"),&prepared);
 if((spanish||prepared)&&(i.recoveryState==Instance::Queued||i.recoveryState==Instance::Running)){
  i.cancelCalculation=true;i.calculateRequested=false;if(i.recoveryState==Instance::Queued)i.recoveryState=Instance::Cancelled;
 }
 if(!params->paramGetPropertySet)return;
 for(auto&c:controls){OfxPropertySetHandle ph=nullptr;if(params->paramGetPropertySet(i.parameters.at(c.name),&ph)==kOfxStatOK&&ph)prop->propSetInt(ph,kOfxParamPropEnabled,0,prepared?0:1);}
 for(auto n:{"auto_recovery","recovery_calculate","recovery_confidence","recovery_folder"}){if(!std::strcmp(n,"auto_recovery")&&!paintHost())continue;OfxPropertySetHandle ph=nullptr;
  if(params->paramGetPropertySet(i.parameters.at(n),&ph)==kOfxStatOK&&ph)prop->propSetInt(ph,kOfxParamPropEnabled,0,(spanish||prepared)?0:1);}
}
struct Settings{
 int encoding=0,mode=0,maskMode=1,view=0,first=0,last=0;bool rect=false,frames=false;
 double confidence=.8,amount=1,ax=.5,ay=.5,aw=.25,ah=.25,feather=.01,threshold=.1,redIntensity=8,redOpacity=.85,gray=1,gain=4,wipe=.5;std::string folder;
};
Settings readSettings(Instance&i,double time){
 Settings s;auto I=[&](const char*n,int&v){int x;if(params->paramGetValueAtTime(i.parameters.at(n),time,&x)==kOfxStatOK)v=x;};
 auto D=[&](const char*n,double&v){double x;if(params->paramGetValueAtTime(i.parameters.at(n),time,&x)==kOfxStatOK&&std::isfinite(x))v=x;};
 I("encoding",s.encoding);I("auto_recovery",s.mode);int choice=0;I("view",choice);
 int maskChoice=0;I("mask_mode",maskChoice);const bool spanish=maskChoice==1;
 D("recovery_confidence",s.confidence);
 D("difference_threshold",s.threshold);D("red_intensity",s.redIntensity);D("red_opacity",s.redOpacity);D("gray_background",s.gray);D("difference_gain",s.gain);
 s.maskMode=spanish?2:1;s.view=viewOfChoice[std::max(0,std::min(3,choice))];
 // Spanish painting and prepared exports own their finishing route. Keep the
 // saved automatic-recovery setting for when ordinary recovery is resumed.
 int prepared=0;I("paint_source_kind",prepared);
 if(paintHost()&&(spanish||prepared))s.mode=0;
 char*str=nullptr;if(params->paramGetValueAtTime(i.parameters.at("recovery_folder"),time,&str)==kOfxStatOK&&str)s.folder=str;
 s.encoding=s.encoding==1;s.mode=s.mode>0?1:0;s.view=std::max(0,std::min(7,s.view));s.confidence=std::max(.5,std::min(.99,s.confidence));
 return s;
}
void syncRecoveryFeedback(Instance&i){
 auto feedback=std::atomic_load(&i.recoveryFeedback);if(!feedback)return;
 if(feedback->cancel)i.cancelCalculation=true;
 feedback->done=i.recoveryDone.load();feedback->total=i.recoveryTotal.load();feedback->seconds=i.recoverySeconds.load();
 const int state=i.recoveryState.load();
 feedback->phase=state==Instance::Queued?recovery_feedback::State::Queued:state==Instance::Running?recovery_feedback::State::Running:state==Instance::Complete?recovery_feedback::State::Complete:state==Instance::Cancelled?recovery_feedback::State::Cancelled:recovery_feedback::State::Failed;
}
// Called only from InstanceChanged/interact actions, where OFX permits writes.
// A render only updates atomics and the optional host progress suite.
void refreshRecoveryUI(OfxImageEffectHandle e){
 // Some hosts immediately re-enter InstanceChanged before publishing the new value.
 static thread_local bool refreshing=false;
 if(refreshing||!params->paramSetValue||!isLiveInstance(e))return;
 struct RefreshScope {
  bool& active;
  explicit RefreshScope(bool& flag):active(flag){active=true;}
  ~RefreshScope(){active=false;}
 } scope(refreshing);
 auto&i=instance(e);syncRecoveryFeedback(i);const int state=i.recoveryState.load();
 if((state==Instance::Cancelled||state==Instance::Failed)&&i.restoreAutoOnCancel.exchange(false)){
  int current=0;auto h=i.parameters.at("auto_recovery");params->paramGetValue(h,&current);
  if(current!=i.recoveryPreviousAuto)params->paramSetValue(h,i.recoveryPreviousAuto.load());
 }
 const int done=i.recoveryDone.load(),total=i.recoveryTotal.load();const double secs=i.recoverySeconds.load();
 char text[256];
 switch(state){
  case Instance::Queued:std::snprintf(text,sizeof text,"Queued: waiting for Resolve to render");break;
  case Instance::Running:std::snprintf(text,sizeof text,"%s %d/%d frames (%.0f%%), %.1f s",i.cancelCalculation?"Cancelling":"Calculating",done,total,100.*done/std::max(1,total),secs);break;
  case Instance::Complete:std::snprintf(text,sizeof text,"Complete: %d frames in %.1f s (%.2f s/frame)",done,secs,secs/std::max(1,done));break;
  case Instance::Cancelled:std::snprintf(text,sizeof text,"Cancelled: %d/%d frames saved",done,total);break;
  case Instance::Failed:std::snprintf(text,sizeof text,"Calculation failed: see Resolve's error and the diagnostic log");break;
  default:std::snprintf(text,sizeof text,"Not calculated in this session");break;
 }
 char*previous=nullptr;auto h=i.parameters.at("recovery_status");
 if(params->paramGetValue(h,&previous)==kOfxStatOK&&(!previous||std::strcmp(previous,text)))params->paramSetValue(h,text);
 if(params->paramGetPropertySet){OfxPropertySetHandle p=nullptr;
  if(params->paramGetPropertySet(i.parameters.at("recovery_calculate"),&p)==kOfxStatOK&&p){
   const char*label=state==Instance::Queued||state==Instance::Running?"Cancel calculation":"Calculate recovery";char*previousLabel=nullptr;
   if(prop->propGetString(p,kOfxPropLabel,0,&previousLabel)!=kOfxStatOK||!previousLabel||std::strcmp(previousLabel,label))prop->propSetString(p,kOfxPropLabel,0,label);
  }
 }
}
paint::Document frameStrokes(Instance&i,int time){
 char*text=nullptr;
 if(params->paramGetValueAtTime(i.parameters.at("paint_data"),time,&text)==kOfxStatOK&&text&&*text)return paint::decode(text);
 return {};
}
// Spanish dirt removal cleans the finished picture as its own pass: each
// painted frame is cleaned from its own window of source frames, never from
// the clip start, so Resolve renders the nodes before it only for those
// frames. The engine needs 3 frames ahead; 8 back matched the full-shot
// output exactly, including fast pans (Temporal Restoration Lab,
// benchmarks/runs/adr-spanish-window-20261115).
constexpr int kSpanishBack=8,kSpanishAhead=3;
bool spanishWindow(OfxImageEffectHandle e,int time){
 auto&i=instance(e);
 return paintHost()&&paint_ui::getInt(e,"paint_source_kind")==0&&!i.prepareRequested&&readSettings(i,time).maskMode==2;
}
// Key of a cleaned frame in the shared cache (the engine picture, without strokes).
std::string cleanKeyFor(Instance&i,const std::string&identity,const std::array<int,2>&range,int w,int h,int encoding,bool windowed,int time){
 std::ostringstream ck;ck<<i.id<<'|'<<i.generation<<'|'<<identity<<'|'<<range[0]<<'|'<<range[1]<<'|'<<w<<'x'<<h<<"|enc"<<encoding<<(windowed?"|spanish-window":"")<<"|frame"<<time;return ck.str();
}
// A painted Spanish frame whose clean is already cached needs only its own
// frame from the host. Without this Resolve re-renders the 12 frames before
// it (all nodes upstream) for every stroke edit and on every playback pass.
bool spanishCleanCached(OfxImageEffectHandle e,int time){
 auto&i=instance(e);if(!i.frameW||frame_cache::shared().capacity()==0)return false;
 std::string identity;readControls(i,time,identity);
 return frame_cache::shared().contains(cleanKeyFor(i,identity,sourceRange(i),i.frameW,i.frameH,readSettings(i,time).encoding,true,time));
}
bool untouchedSpanish(OfxImageEffectHandle e,int time){
 auto&i=instance(e);
 if(!paintHost()||(paint_ui::getInt(e,"paint_source_kind")!=0||i.prepareRequested)||readSettings(i,time).maskMode!=2)return false;
 int on=0;params->paramGetValueAtTime(i.parameters.at("paint"),time,&on);
 const auto doc=frameStrokes(i,time);
 return (on||!doc.strokes.empty())&&!paint::anyOn(doc,time);
}
// Cineon log (black 95, white 685, film gamma 0.6) <-> linear; values as code / 1023.
const double cineonOffset=std::pow(10.0,(95.0-685.0)*0.002/0.6),cineonGain=1.0/(1.0-cineonOffset);
inline float cineonToLinear(float v){return float(cineonGain*std::pow(10.0,(double(v)*1023.0-685.0)*0.002/0.6)-(cineonGain-1.0));}
inline float linearToCineon(float v){return float((685.0+std::log10(std::max((double(v)+cineonGain-1.0)/cineonGain,1e-10))*0.6/0.002)/1023.0);}
void toLinear(const std::vector<float>&in,std::vector<float>&out){out.resize(in.size());parallelRows(int(in.size()/3/1024)+1,[&](int a,int b){for(size_t k=size_t(a)*3072;k<std::min(in.size(),size_t(b)*3072);++k)out[k]=cineonToLinear(in[k]);});}
// Cineon: the engine's change, converted back to log, added to the original log values; exactly the source where nothing changed.
void cineonDelta(const std::vector<float>&src,const std::vector<float>&cleanedLinear,std::vector<float>&out){
 out.resize(src.size());
 parallelRows(int(src.size()/3/1024)+1,[&](int a,int b){for(size_t k=size_t(a)*3072;k<std::min(src.size(),size_t(b)*3072);++k){const float ref=cineonToLinear(src[k]);const float c=cleanedLinear[k];out[k]=c==ref?src[k]:src[k]+(linearToCineon(c)-linearToCineon(ref));}});
}
std::string defaultRecoveryFolder(){
#ifdef _WIN32
 const char*b=std::getenv("LOCALAPPDATA");return b?std::string(b)+"\\FabioColor\\AdvancedDirtRemoval\\Recovery":"";
#elif defined(__APPLE__)
 const char*h=std::getenv("HOME");return h?std::string(h)+"/Library/Application Support/FabioColor/AdvancedDirtRemoval/Recovery":"";
#else
 const char*x=std::getenv("XDG_DATA_HOME");const char*h=std::getenv("HOME");return x&&*x?std::string(x)+"/fabiocolor/advanced-dirt-removal/recovery":h?std::string(h)+"/.local/share/fabiocolor/advanced-dirt-removal/recovery":"";
#endif
}
std::string recoveryModelPath(){
 if(const char*m=std::getenv("ADR_RECOVERY_MODEL"))return m;
 std::string bin=adrlog::pluginPath();  // <bundle>/Contents/<arch>/AdvancedDirtRemoval.ofx
 for(int up=0;up<2;++up){auto k=bin.find_last_of("/\\");if(k==std::string::npos)return "";bin.resize(k);}
 return bin+"/Resources/recovery-model.bin";
}
// Weight files are immutable during a host session; install updates with the
// host closed. Hash once per path, before looking up any saved probability map.
std::string recoveryModelIdentity(){
 static std::mutex lock;static std::map<std::string,std::string> identities;
 const auto path=recoveryModelPath();std::lock_guard<std::mutex> guard(lock);
 auto known=identities.find(path);if(known!=identities.end())return known->second;
 std::ifstream file(path,std::ios::binary);require(bool(file),"auto recovery model unavailable: "+path,kOfxStatFailed);
 adr_tools::Sha256 hash;std::array<char,65536> buffer{};
 while(file){file.read(buffer.data(),buffer.size());hash.update(buffer.data(),size_t(file.gcount()));}
 require(file.eof(),"auto recovery model read failed: "+path,kOfxStatFailed);
 const auto digest=hash.hex();identities.emplace(path,digest);return digest;
}
std::mutex recoveryMutex;adr_recovery* recoveryEngine=nullptr;
adr_recovery* recoveryModel(){
 std::lock_guard<std::mutex> g(recoveryMutex);
 if(!recoveryEngine){char error[1024]={};const auto path=recoveryModelPath();recoveryEngine=adr_recovery_create(context(),path.c_str(),error,sizeof error);
  require(recoveryEngine,std::string("auto recovery model unavailable (")+path+"): "+error,kOfxStatFailed);adrlog::info("auto recovery model loaded: "+path+", sha256 "+recoveryModelIdentity());}
 return recoveryEngine;
}
bool makeDirs(const std::string&path){
 for(size_t k=1;k<=path.size();++k)if(k==path.size()||path[k]=='/'||path[k]=='\\'){const std::string cur=path.substr(0,k);
#ifdef _WIN32
  _mkdir(cur.c_str());
#else
  mkdir(cur.c_str(),0755);
#endif
 }
 struct stat st{};return stat(path.c_str(),&st)==0;
}
// One folder per model and shot: weight hash, cleaning controls, encoding,
// frame range, geometry and first source frame pixels. Old folders stay intact.
std::string recoveryShotFolder(Instance&i,OfxImageEffectHandle e,const Settings&s,const std::string&identity,const std::array<int,2>&range,int w,int h,const std::array<int,4>&bounds){
 std::ostringstream id;id<<identity<<'|'<<range[0]<<'|'<<range[1]<<'|'<<w<<'x'<<h<<"|enc"<<s.encoding<<"|model-sha256:"<<recoveryModelIdentity();
 if(i.recoveryIdFor!=id.str()){
  std::vector<float> first;readSource(i,e,range[0],w,h,bounds,first);
  adr_tools::Sha256 sh;sh.update(id.str().data(),id.str().size());sh.update(first.data(),first.size()*sizeof(float));
  i.recoveryId=sh.hex().substr(0,20);i.recoveryIdFor=id.str();
 }
 const std::string base=s.folder.empty()?defaultRecoveryFolder():s.folder;
 require(!base.empty(),"auto recovery: no folder for the matte",kOfxStatFailed);
 return base+"/shot-"+i.recoveryId;
}
std::string recoveryFile(const std::string&dir,int time){return dir+"/frame-"+std::to_string(time)+".adrp";}
// Probability map file: "ADRP", width, height, then (run length uint32, value uint8) pairs.
void saveProbability(const std::string&dir,int time,int w,int h,const std::vector<uint8_t>&p){
 require(makeDirs(dir),"auto recovery: cannot create "+dir,kOfxStatFailed);
 const std::string tmp=recoveryFile(dir,time)+".tmp";
 {std::ofstream f(tmp,std::ios::binary);f.write("ADRP",4);int32_t g[2]={w,h};f.write(reinterpret_cast<const char*>(g),8);
  for(size_t k=0;k<p.size();){size_t j=k;while(j<p.size()&&p[j]==p[k]&&j-k<0xffffffffu)++j;uint32_t n=uint32_t(j-k);f.write(reinterpret_cast<const char*>(&n),4);f.write(reinterpret_cast<const char*>(&p[k]),1);k=j;}
  require(bool(f),"auto recovery: cannot write "+tmp,kOfxStatFailed);}
 std::remove(recoveryFile(dir,time).c_str());require(std::rename(tmp.c_str(),recoveryFile(dir,time).c_str())==0,"auto recovery: cannot store the matte",kOfxStatFailed);
}
bool loadProbability(const std::string&dir,int time,int w,int h,std::vector<uint8_t>&p){
 std::ifstream f(recoveryFile(dir,time),std::ios::binary);if(!f)return false;
 char magic[4];int32_t g[2];f.read(magic,4);f.read(reinterpret_cast<char*>(g),8);if(!f||std::memcmp(magic,"ADRP",4)||g[0]!=w||g[1]!=h)return false;
 p.assign(size_t(w)*h,0);size_t k=0;uint32_t n;uint8_t v;
 while(k<p.size()&&f.read(reinterpret_cast<char*>(&n),4)&&f.read(reinterpret_cast<char*>(&v),1)){if(n>p.size()-k)return false;std::fill(p.begin()+k,p.begin()+k+n,v);k+=n;}
 return k==p.size();
}
// The previous and next source frames, mirrored inside the shot at its ends (as in training).
std::array<int,2> recoveryNeighbours(int f,int first,int last){
 const int p=f-1>=first?f-1:f+2<=last?f+2:f+1<=last?f+1:f;
 const int n=f+1<=last?f+1:f-2>=first?f-2:f-1>=first?f-1:f;
 return {p,n};
}
// Scores one frame. cleaned is in the input encoding (after the Cineon delta), like the sources.
void computeProbability(Instance&i,OfxImageEffectHandle e,int time,const std::array<int,2>&range,int w,int h,const std::array<int,4>&bounds,const std::vector<float>&cur,const float*cleaned,std::vector<uint8_t>&p){
 const auto nb=recoveryNeighbours(time,range[0],range[1]);
 std::vector<float> prev,next;readSource(i,e,nb[0],w,h,bounds,prev);readSource(i,e,nb[1],w,h,bounds,next);
 p.assign(size_t(w)*h,0);char error[2048]={};const auto t0=std::chrono::steady_clock::now();
 const int spots=adr_recovery_frame(recoveryModel(),prev.data(),cur.data(),next.data(),cleaned,w,h,size_t(w)*12,ADR_RECOVERY_INPUT_VIDEO,p.data(),error,sizeof error);
 require(spots>=0,std::string("auto recovery: ")+error,kOfxStatFailed);
 char line[160];std::snprintf(line,sizeof line," auto recovery frame %d: %d spots scored in %.0f ms",time,spots,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());adrlog::info(i.label()+line);
}
struct RecoveryProgress {
 OfxImageEffectHandle effect;Instance&item;bool active=false,v2=false;
 std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
 RecoveryProgress(OfxImageEffectHandle e,Instance&i,int total):effect(e),item(i){
  item.recoveryDone=0;item.recoveryTotal=total;item.recoverySeconds=0;item.recoveryState=Instance::Running;syncRecoveryFeedback(item);
  if(progressV2&&progressV2->progressStart&&progressV2->progressUpdate&&progressV2->progressEnd){v2=true;active=progressV2->progressStart(e,"Calculating recovery","adr-recovery")==kOfxStatOK;}
  if(!active&&progressV1&&progressV1->progressStart&&progressV1->progressUpdate&&progressV1->progressEnd){v2=false;active=progressV1->progressStart(e,"Calculating recovery")==kOfxStatOK;}
  adrlog::info(i.label()+(active?" auto recovery: host progress started":" auto recovery: host progress unavailable; status refreshes in the inspector"));
 }
 void update(int done){
  item.recoveryDone=done;item.recoverySeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  syncRecoveryFeedback(item);
  if(active){const double value=double(done)/std::max(1,item.recoveryTotal.load());
   const auto result=v2?progressV2->progressUpdate(effect,value):progressV1->progressUpdate(effect,value);
   if(result==kOfxStatReplyNo)item.cancelCalculation=true;
  }
  if(item.cancelCalculation||fx->abort(effect)){item.recoveryState=Instance::Cancelled;throw Error(kOfxStatFailed,"render aborted: recovery calculation cancelled");}
 }
 ~RecoveryProgress(){syncRecoveryFeedback(item);if(active){if(v2)progressV2->progressEnd(effect);else progressV1->progressEnd(effect);}}
};
// Whole-shot analysis is driven from a render, never from the UI's image-fetch
// callback. Existing valid mattes avoid both classification and repair work.
void calculateShot(OfxImageEffectHandle e,Instance&i){
 const auto t0=std::chrono::steady_clock::now();
 auto range=sourceRange(i);const int count=range[1]-range[0]+1;
 std::array<int,4> bounds;int w,h;{Image first(i.source,range[0],e,"auto recovery");bounds=first.b;w=first.width();h=first.height();}
 require(w>=256&&w<=16384&&h>=256&&h<=16384,"Advanced Dirt Removal needs full frames of 256 to 16384 pixels per side");
 std::string identity;adr_controls c=readControls(i,range[0],identity);const Settings s=readSettings(i,range[0]);if(s.encoding)c.extended_range=1;
 const std::string dir=recoveryShotFolder(i,e,s,identity,range,w,h,bounds);
 adrlog::info(i.label()+" auto recovery: calculating frames "+std::to_string(range[0])+".."+std::to_string(range[1])+" into "+dir);
 RecoveryProgress progress(e,i,count);progress.update(0);
 std::vector<bool> cached(size_t(count),false);std::vector<uint8_t> p;int done=0;
 for(int k=0;k<count;++k){cached[k]=loadProbability(dir,range[0]+k,w,h,p);if(cached[k])++done;progress.update(done);}
 if(done==count){
  i.restoreAutoOnCancel=false;i.recoveryState=Instance::Complete;
  adrlog::info(i.label()+" auto recovery: reused all "+std::to_string(count)+" saved mattes; no cleaning or classification");return;
 }
 char error[2048]={};adr_sequence* seq=adr_sequence_create(context(),&c,w,h,count,error,sizeof error);require(seq,std::string("auto recovery: ")+error,kOfxStatFailed);
 std::map<int,std::vector<float>> src;auto get=[&](int t)->const std::vector<float>&{auto&v=src[t];if(v.empty())readSource(i,e,t,w,h,bounds,v);return v;};
 std::vector<float> out(size_t(w)*h*3),lin,enc;p.resize(size_t(w)*h);
 try{
  while(adr_sequence_next_output(seq)>=0){
   progress.update(done);
   const bool reuse=cached[size_t(adr_sequence_next_output(seq))];
   const int f=reuse?adr_sequence_skip(seq,error,sizeof error):adr_sequence_pull(seq,out.data(),size_t(w)*12,error,sizeof error);require(f>=-1,std::string("auto recovery: ")+error,kOfxStatFailed);
   if(f==-1){const int sidx=adr_sequence_next_source(seq);require(sidx>=0,"auto recovery: sequence stalled",kOfxStatFailed);
    const auto&raw=get(range[0]+sidx);const float*push=raw.data();if(s.encoding){toLinear(raw,lin);push=lin.data();}
    require(adr_sequence_push(seq,push,size_t(w)*12,error,sizeof error)==0,std::string("auto recovery: ")+error,kOfxStatFailed);continue;}
   const int time=range[0]+f;const auto nb=recoveryNeighbours(time,range[0],range[1]);
   if(!reuse){
    const float*cleaned=out.data();if(s.encoding){cineonDelta(get(time),out,enc);cleaned=enc.data();}
    const int spots=adr_recovery_frame(recoveryModel(),get(nb[0]).data(),get(time).data(),get(nb[1]).data(),cleaned,w,h,size_t(w)*12,ADR_RECOVERY_INPUT_VIDEO,p.data(),error,sizeof error);
    require(spots>=0,std::string("auto recovery: ")+error,kOfxStatFailed);saveProbability(dir,time,w,h,p);++done;
   }
   for(auto it=src.begin();it!=src.end();)if(it->first<time-2)it=src.erase(it);else ++it;
   progress.update(done);
   if(!reuse&&done%10==0)adrlog::info(i.label()+" auto recovery: "+std::to_string(done)+"/"+std::to_string(count)+" frames");
  }
 }catch(...){adr_sequence_destroy(seq);adr_context_trim(context());throw;}
 adr_sequence_destroy(seq);adr_context_trim(context());  // the whole-shot pass's buffers
 const double secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
 i.recoverySeconds=secs;i.restoreAutoOnCancel=false;i.recoveryState=Instance::Complete;
 char line[200];std::snprintf(line,sizeof line," auto recovery: %d frames calculated in %.0f s (%.2f s/frame)",done,secs,secs/std::max(1,done));adrlog::info(i.label()+line);
}
// The optional Mask input as coverage m and stroke colour s (white 1, black 0) per source pixel, top-down.
bool readMask(Instance&i,int time,int w,int h,const std::array<int,4>&bounds,std::vector<float>&m,std::vector<float>&s){
#if ADR_HOST_GPU_TRANSPORT
 adr_host::HostContextScope hostContext;
#endif
 if(!i.mask)return false;
 OfxPropertySetHandle cp=nullptr;if(fx->clipGetPropertySet(i.mask,&cp)!=kOfxStatOK)return false;int connected=0;prop->propGetInt(cp,kOfxImageClipPropConnected,0,&connected);if(!connected)return false;
 OfxPropertySetHandle img=nullptr;if(fx->clipGetImage(i.mask,time,nullptr,&img)!=kOfxStatOK||!img)return false;
 struct Release{OfxPropertySetHandle h;~Release(){releaseHostImage(h);}}release{img};
 const std::string depth=str(img,kOfxImageEffectPropPixelDepth),comp=str(img,kOfxImageEffectPropComponents);
 const int ch=comp==kOfxImageComponentRGBA?4:comp==kOfxImageComponentRGB?3:comp==kOfxImageComponentAlpha?1:0;
 const int bytes=depth==kOfxBitDepthFloat?4:depth==kOfxBitDepthHalf||depth==kOfxBitDepthShort?2:depth==kOfxBitDepthByte?1:0;
 require(ch&&bytes,"Mask input: unsupported image format",kOfxStatFailed);
 std::array<int,4> mb{};check(prop->propGetIntN(img,kOfxImagePropBounds,4,mb.data()),"mask bounds");const int stride=integer(img,kOfxImagePropRowBytes);
 void*data=nullptr;check(prop->propGetPointer(img,kOfxImagePropData,0,&data),"mask pixels");
#if ADR_HOST_METAL_TRANSPORT
 std::unique_ptr<adr_host::MetalImage> metalMask;
 if(hostMetalQueue) {
  require(mb[2]>mb[0]&&mb[3]>mb[1]&&int64_t(mb[2])-mb[0]<=16384&&int64_t(mb[3])-mb[1]<=16384&&stride>0&&int64_t(stride)>=int64_t(mb[2]-mb[0])*ch*bytes,"Metal mask: invalid bounds/rowBytes");
  const size_t length=size_t(mb[3]-mb[1]-1)*size_t(stride)+size_t(mb[2]-mb[0])*ch*bytes;
  metalMask=adr_host::stageMetalImage(hostMetalQueue,data,length,true);data=metalMask->data();
 }
#elif ADR_HOST_GPU_TRANSPORT
 std::unique_ptr<adr_host::GpuImage> gpuMask;
 if(adr_host::gpuTransportActive()){
  require(mb[2]>mb[0]&&mb[3]>mb[1]&&int64_t(mb[2])-mb[0]<=16384&&int64_t(mb[3])-mb[1]<=16384&&stride>0&&int64_t(stride)>=int64_t(mb[2]-mb[0])*ch*bytes,"GPU mask: invalid bounds/rowBytes");
  const size_t length=size_t(mb[3]-mb[1]-1)*size_t(stride)+size_t(mb[2]-mb[0])*ch*bytes;
  gpuMask=adr_host::stageGpuImage(data,length,true);data=gpuMask->data();
 }
#endif
 auto val=[&](const char*px,int c)->float{const char*q=px+c*bytes;if(bytes==4){float v;std::memcpy(&v,q,4);return v;}if(bytes==1)return uint8_t(*q)/255.f;uint16_t v;std::memcpy(&v,q,2);return depth==kOfxBitDepthHalf?decodeHalf(v):v/65535.f;};
 m.assign(size_t(w)*h,0.f);s.assign(size_t(w)*h,1.f);
 // On canvas hosts the mask is read at the same pixel position (outside its window is 0). On
 // resolution-independent hosts a mask at another resolution is mapped by position (normalised coordinates).
 static const bool byPosition=positionMappedMasks();const bool pixelMapped=!byPosition||(mb[2]-mb[0]==w&&mb[3]-mb[1]==h);const int mw=mb[2]-mb[0],mh=mb[3]-mb[1];
 parallelRows(h,[&](int y0,int y1){for(int y=y0;y<y1;++y){const int Y=pixelMapped?bounds[3]-1-y:mb[1]+int((h-1-y+.5)*mh/h);if(Y<mb[1]||Y>=mb[3])continue;const char*row=static_cast<const char*>(data)+int64_t(Y-mb[1])*stride;
  for(int x=0;x<w;++x){const int X=pixelMapped?bounds[0]+x:mb[0]+int((x+.5)*mw/w);if(X<mb[0]||X>=mb[2])continue;const char*px=row+int64_t(X-mb[0])*ch*bytes;
   float a,r;if(ch==1){a=val(px,0);r=a;}else if(ch==3){a=val(px,0);r=a;}else{a=val(px,3);r=val(px,0);}
   a=std::min(1.f,std::max(0.f,a));const size_t k=size_t(y)*w+x;m[k]=a;s[k]=a>1e-6f?std::min(1.f,std::max(0.f,r/a)):1.f;}}});
 return true;
}
// Final picture: recovery gate (auto matte, mask, limits), blend, then the chosen view. Exactly the
// repair wherever the gate is 1 and the applied amount is 1, exactly the source where it is 0.
// Paint (strokes painted in the viewer) sits over the Mask input: paint value P
// and coverage T (paint::Layers); the mask shows through (1 - T).
void finish(const Settings&s,int time,int w,int h,const std::vector<float>&src,const float*rep,const std::vector<uint8_t>&matte,bool haveMask,
            const std::vector<float>&mm,const std::vector<float>&ms,const paint::Layers*painted,std::vector<float>&out){
 out.resize(size_t(w)*h*3);
 const bool autoOn=!matte.empty();const bool frameOk=!s.frames||(time>=s.first&&time<=s.last);
 const float thr=float(s.threshold/100.0);
 parallelRows(h,[&](int y0,int y1){for(int y=y0;y<y1;++y)for(int x=0;x<w;++x){
  const size_t k=size_t(y)*w+x;const float A=autoOn?float(matte[k]):0.f;const float m=haveMask?mm[k]:0.f,st=haveMask?ms[k]:1.f;
  const float P=painted?painted->value[k]:0.f,T=painted?painted->touched[k]:0.f;
  float gate;
  if(s.maskMode==0)gate=1.f-A;
  else if(s.maskMode==1)gate=1.f-(autoOn?A*(1.f-m)*(1.f-T)+st*m*(1.f-T)+P:m*(1.f-T)+P);
  else gate=m*(1.f-T)+P;
  if(s.rect){const double nx=(x+.5)/w,ny=1.0-(y+.5)/h,f=std::max(s.feather,1e-6);
   gate*=float(std::min(std::min(std::max((s.aw/2-std::fabs(nx-s.ax))/f,0.0),1.0),std::min(std::max((s.ah/2-std::fabs(ny-s.ay))/f,0.0),1.0)));}
  if(!frameOk)gate=0;
  const float g=std::max(0.f,std::min(1.f,gate*float(s.amount)));
  float res[3],sv[3];
  for(int c=0;c<3;++c){sv[c]=src[k*3+c];const float rv=rep[k*3+c];res[c]=g<=0?sv[c]:g>=1?rv:sv[c]+(rv-sv[c])*g;}
  float*o=&out[k*3];
  const float d=std::max(std::fabs(res[0]-sv[0]),std::max(std::fabs(res[1]-sv[1]),std::fabs(res[2]-sv[2])));
  switch(s.view){
   case 0:o[0]=res[0];o[1]=res[1];o[2]=res[2];break;
   case 1:o[0]=sv[0];o[1]=sv[1];o[2]=sv[2];break;
   case 2:{const bool left=(x+.5)/w<s.wipe;for(int c=0;c<3;++c)o[c]=left?sv[c]:res[c];break;}
   case 3:for(int c=0;c<3;++c)o[c]=std::fabs(res[c]-sv[c])*float(s.gain);break;
   case 4:{const float changed=std::min(d*float(s.redIntensity),1.f);const float red=d>thr?changed:0.f;const float op=red*float(s.redOpacity);
    const float lum=(0.2126f*sv[0]+0.7152f*sv[1]+0.0722f*sv[2])*float(s.gray);o[0]=lum*(1-op)+op;o[1]=lum*(1-op);o[2]=lum*(1-op);break;}
   case 5:o[0]=o[1]=o[2]=g;break;
   case 6:o[0]=o[1]=o[2]=std::min(d*float(s.redIntensity),1.f);break;
   default:o[0]=o[1]=o[2]=A;break;
  }
 }});
}
#include "prepare_shot.h"
#include "preview_render.h"
OfxStatus render(OfxImageEffectHandle e,OfxPropertySetHandle in){
 auto&i=instance(e);
 struct CompleteTiming {
  Instance& i;int frame;bool success=false;
  std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
  ~CompleteTiming(){if(success){char line[180];std::snprintf(line,sizeof line," frame %d complete render %.3f ms (includes fetch, masks, blend, validation and output)",frame,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());adrlog::info(i.label()+line);}}
 } completeTiming{i,frameTime(in)};
 const double sx=number(in,kOfxImageEffectPropRenderScale,0),sy=number(in,kOfxImageEffectPropRenderScale,1);
 require(sx>0&&sx<=1&&sy>0&&sy<=1,"Invalid render scale");
 if(sx!=1.||sy!=1.)return renderPreview(e,in,i,sx,sy);
 try{
#if ADR_HOST_METAL_TRANSPORT
  HostMetalScope transport(in);
  if(hostMetalQueue&&i.rendered==0)adrlog::info(i.label()+" using Metal host image transport (engine unchanged)");
#elif ADR_HOST_GPU_TRANSPORT
  HostGpuScope transport(in);
  if(adr_host::gpuTransportActive()&&i.rendered==0)adrlog::info(i.label()+" using "+adr_host::gpuTransportName()+" host image transport (engine unchanged)");
#endif
  if(persistentMessages)persistentMessages->clearPersistentMessage(e);
  int time=frameTime(in);auto range=sourceRange(i);require(time>=range[0]&&time<=range[1],"time outside source");
  Image source(i.source,time,e,"current"),output(i.output,time,e,"output");int w=source.width(),h=source.height();
  require(w>=256&&w<=16384&&h>=256&&h<=16384,"Advanced Dirt Removal needs full frames of 256 to 16384 pixels per side");require(source.b==output.b&&source.channels==output.channels,"output geometry/components differ");
  auto window=renderWindow(e,in,source.b);
  std::string identity;adr_controls c=readControls(i,time,identity);const Settings st=readSettings(i,time);if(st.encoding)c.extended_range=1;
  if(i.prepareRequested)prepareShot(e,i);
  // Windowed Spanish output is kept apart from full-shot output in the shared cache.
  const bool windowed=spanishWindow(e,time);i.frameW=w;i.frameH=h;
  std::ostringstream key;key<<identity<<'|'<<range[0]<<'|'<<range[1]<<'|'<<w<<'x'<<h<<'|'<<i.generation<<"|enc"<<st.encoding;
  const int sourceKind=paint_ui::getInt(e,"paint_source_kind");
  const bool prepared=sourceKind!=0;
  if(!prepared&&st.mode&&st.maskMode!=2&&i.calculateRequested.exchange(false)){
   try{calculateShot(e,i);}catch(const Error&ex){i.recoveryState=std::strstr(ex.what(),"render aborted")?Instance::Cancelled:Instance::Failed;syncRecoveryFeedback(i);throw;}catch(...){i.recoveryState=Instance::Failed;syncRecoveryFeedback(i);throw;}
  }
  if(!prepared&&key.str()!=i.key){if(std::getenv("ADR_DEBUG")&&!i.key.empty())std::fprintf(stderr,"adr key change instance=%p\n",(void*)&i);i.close();}
  const int index=time-range[0],count=range[1]-range[0]+1;
  const auto traceStart=std::chrono::steady_clock::now();double tFetch=0,tEngine=0;
  const int traceLive=i.sequence?adr_sequence_next_output(i.sequence):-1;
  const char* traceWhat=i.cachedFrame==time?"cached":"render";
  // Paint: strokes on this frame, and the recent-frames cache while painting.
  int paintOn=0;params->paramGetValueAtTime(i.parameters.at("paint"),time,&paintOn);
  const auto strokes=frameStrokes(i,time);
  const bool painted=paint::anyOn(strokes,time);
  const bool builtInBrush=paintHost()&&(paintOn||!strokes.strokes.empty());
  const bool skipCleaning=!prepared&&st.maskMode==2&&builtInBrush&&!painted;
  if(painted)require(strokes.width==w&&strokes.height==h,"The picture size changed since these strokes were painted ("+std::to_string(strokes.width)+"x"+std::to_string(strokes.height)+", now "+std::to_string(w)+"x"+std::to_string(h)+")");
  if(!paintOn)i.recent.clear();
  // Shared cleaned-frame cache. The engine's picture never depends on strokes,
  // so a stroke edit (which makes Resolve re-render every frame)
  // or a backward seek reuses the exact cleaned frame instead of
  // restarting the temporal engine. A hit requires this frame's source to be
  // bit-identical to the one that was cleaned.
  std::vector<float> currentSource;bool haveCurrentSource=false;uint64_t currentPrint=0;double tShared=0;bool sharedHit=false;
  auto sourcePrint=[&]{
   if(!haveCurrentSource){auto t0=std::chrono::steady_clock::now();readSource(i,e,time,w,h,source.b,currentSource);currentPrint=frame_cache::fingerprint(currentSource,h,[](int n,auto f){parallelRows(n,f);});haveCurrentSource=true;tShared+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();}
   return currentPrint;};
  if(!prepared&&!skipCleaning){
   const auto print=sourcePrint();auto prior=i.sourcePrints.find(time);
   if(prior!=i.sourcePrints.end()&&prior->second!=print){
    // Hosts may omit a Source-change notification. A changed current source
    // invalidates motion history and every local/shared frame for this instance.
    i.close();++i.generation;key.str("");key.clear();
    key<<identity<<'|'<<range[0]<<'|'<<range[1]<<'|'<<w<<'x'<<h<<'|'<<i.generation<<"|enc"<<st.encoding;
    traceWhat="render";adrlog::info(i.label()+" source changed; discarded cached cleaning and motion history");
   }
   i.sourcePrints[time]=print;
   if(i.cachedFrame!=time&&paintOn&&i.key==key.str()){auto it=i.recent.find(time);if(it!=i.recent.end()){i.cached=it->second;i.cachedFrame=time;traceWhat="cached";}}
  }
  const std::string cleanKey=cleanKeyFor(i,identity,range,w,h,st.encoding,windowed,time);
  const bool useShared=!prepared&&!skipCleaning&&frame_cache::shared().capacity()>0;
  if(useShared&&i.cachedFrame!=time&&frame_cache::shared().contains(cleanKey)){
   const uint64_t print=sourcePrint();auto t0=std::chrono::steady_clock::now();
   if(auto hit=frame_cache::shared().find(cleanKey,print)){i.cached=hit;i.cachedFrame=time;if(i.key.empty())i.key=key.str();traceWhat="cached";sharedHit=true;}
   tShared+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  }
  std::vector<float> working;std::shared_ptr<const std::vector<float>> original,preparedClean;
  if(sourceKind==2){
   require(paint_ui::preparationAvailable(),"Experimental prepared shot is disabled; select Clean this clip");
   require(paintHost(),"Prepared shot is supported inside DaVinci Resolve only");
   const auto folder=paint_ui::getText(e,"prepare_folder");
   const auto info=paint_source::sourceInfo(folder);
   require(info.width==w&&info.height==h&&std::abs(info.fps-number(properties(e),kOfxImageEffectPropFrameRate))<1e-8,"Prepared shot geometry/rate mismatch");
   paint_source::requireSourceFrame(folder,time);
   std::ifstream manifest(paint_source::fs::u8path(folder)/"shot.txt");int encoding=-1;manifest>>encoding;
   require(bool(manifest)&&encoding==st.encoding,"Prepared shot input encoding changed; prepare again");
   if(i.sequence||i.history||!i.key.empty())i.close();
   auto pair=i.preparedFrames.get(folder,time,w,h);original=pair.original;
   readSource(i,e,time,w,h,source.b,currentSource);haveCurrentSource=true;
   require(currentSource.size()==original->size()&&std::memcmp(currentSource.data(),original->data(),currentSource.size()*sizeof(float))==0,"Prepared shot source changed; switch to Clean this clip and prepare again");
   preparedClean=pair.cleaned;
  }else if(prepared){
   require(paintHost(),"Additional source painting is supported inside DaVinci Resolve only");
   require(st.encoding==0,"Additional source: use Video input encoding with matching original/processed values");
   require(st.mode==0,"Additional source: set Auto recovery to Off");
   if(originalConnected(i)){
    original=readOriginal(i,e,time,w,h);
    if(!i.originalLogged){i.originalLogged=true;adrlog::info(i.label()+" additional source from the "+(clipConnected(i.original)?"Additional source":"mask")+" input: painting blends the processed picture and the additional source, cleaning engine bypassed");}
   }else if(paint_ui::getText(e,"paint_source_folder").empty()){
    // Nothing connected yet: keep the viewer usable with the processed picture.
    readSource(i,e,time,w,h,source.b,currentSource);haveCurrentSource=true;original=std::make_shared<const std::vector<float>>(currentSource);
    if(persistentMessages)persistentMessages->setPersistentMessage(e,kOfxMessageWarning,"adr","%s","Connect the aligned original to the effect's second input (Additional source). Showing the processed picture.");
    if(!i.originalWarned){i.originalWarned=true;adrlog::warn(i.label()+" additional source not connected (Additional source input "+(i.original?"present":"missing")+", mask input "+(i.mask?"present":"missing")+"; neither reported connected): showing the processed picture");}
   }else{
   const auto folder=paint_ui::getText(e,"paint_source_folder");
    const auto info=paint_source::sourceInfo(folder);
    require(info.width==w&&info.height==h,"Prepared original dimensions differ from the working image");
    require(std::abs(info.fps-number(properties(e),kOfxImageEffectPropFrameRate))<1e-8,"Prepared original frame rate differs from the working image");
    const long long frame=static_cast<long long>(time)+paint_ui::getInt(e,"paint_source_offset");
    require(frame>=-1000000000&&frame<=1000000000,"Prepared original frame offset is outside the supported range");
    paint_source::requireSourceFrame(folder,frame);original=i.originalSource.getPreview(folder,int(frame),w,h).original;
   }
   if(i.sequence||i.history||!i.key.empty())i.close();
   readSource(i,e,time,w,h,source.b,working);
   if(originalConnected(i)&&time!=i.comparedFrame&&original->size()==working.size()){
    // Logged when it changes: whether the additional source really carries another picture.
    i.comparedFrame=time;size_t differ=0;double largest=0;for(size_t k=0;k<working.size();++k){const double d=std::fabs(double((*original)[k])-working[k]);if(d>0){++differ;largest=std::max(largest,d);}}
    char line[220];std::snprintf(line,sizeof line," additional source (%s input) frame %d: %zu of %zu samples differ from the processed picture (largest %.4f)%s",clipConnected(i.original)?"Additional source":"mask",time,differ,working.size(),largest,differ?"":"; the additional source carries the same picture");
    const int state=(differ?1:0)+(clipConnected(i.original)?2:0);
    if(state!=i.compareState){i.compareState=state;if(differ)adrlog::info(i.label()+line);else adrlog::warn(i.label()+line);}
   }
   if(!originalConnected(i))adrlog::info(i.label()+" prepared original frame "+std::to_string(time+paint_ui::getInt(e,"paint_source_offset"))+": input is the processed image, cleaning engine bypassed");
  }else if(skipCleaning){
   // Empty Spanish frames have no repair contribution. Do not start, advance,
   // replace or pollute the cleaned-frame cache; a later stroke still needs
   // the genuine cleaned picture. Diagnostic views use this same original.
   readSource(i,e,time,w,h,source.b,working);traceWhat="untouched";
   adrlog::info(i.label()+" Spanish frame "+std::to_string(time)+": no active strokes; current source only, cleaning engine bypassed");
  }else if(i.cachedFrame!=time){
   char error[2048]={};
   if(windowed){
    // A fresh sequence over this frame's window only; no motion history.
    if(i.sequence)adr_sequence_destroy(i.sequence);i.sequence=nullptr;
    if(i.history)adr_history_destroy(i.history);i.history=nullptr;
    const int first=std::max(range[0],time-kSpanishBack),last=std::min(range[1],time+kSpanishAhead),target=time-first;
    std::unique_ptr<adr_sequence,void(*)(adr_sequence*)> seq(adr_sequence_create(context(),&c,w,h,last-first+1,error,sizeof(error)),adr_sequence_destroy);
    require(seq!=nullptr,std::string("Advanced Dirt Removal sequence: ")+error,kOfxStatFailed);
    i.key=key.str();i.first=range[0];
    if(hashLogDir())hashLog("window "+std::to_string(w)+"x"+std::to_string(h)+" first="+std::to_string(first)+" last="+std::to_string(last)+" target="+std::to_string(time)+" key="+i.key);
    i.scratch.resize(size_t(w)*h*3);std::vector<float> rgb,raw,lin;
    for(int next=adr_sequence_next_output(seq.get());next>=0&&next<=target;next=adr_sequence_next_output(seq.get())){
     require(!fx->abort(e),"render aborted",kOfxStatFailed);
     auto te=std::chrono::steady_clock::now();
     const int produced=next==target?adr_sequence_pull(seq.get(),i.scratch.data(),size_t(w)*12,error,sizeof(error)):adr_sequence_skip(seq.get(),error,sizeof(error));
     tEngine+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-te).count();
     require(produced>=-1,std::string("Advanced Dirt Removal render: ")+error,kOfxStatFailed);
     if(produced>=0){if(produced==target)hashFrame("out",time,i.scratch.data(),i.scratch.size());continue;}
     const int pending=adr_sequence_next_source(seq.get());require(pending>=0,"Advanced Dirt Removal sequence stalled",kOfxStatFailed);
     auto tf=std::chrono::steady_clock::now();
     // The current frame is the one fingerprinted above, so cached results
     // always describe the pixels that were cleaned.
     const float* pixels=nullptr;
     if(first+pending==time){sourcePrint();pixels=currentSource.data();}
     else{readSource(i,e,first+pending,w,h,source.b,rgb);pixels=rgb.data();}
     tFetch+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tf).count();
     hashFrame("in",first+pending,pixels,size_t(w)*h*3);
     if(st.encoding){raw.assign(pixels,pixels+size_t(w)*h*3);toLinear(raw,lin);pixels=lin.data();}
     require(adr_sequence_push(seq.get(),pixels,size_t(w)*12,error,sizeof(error))==0,std::string("Advanced Dirt Removal upload: ")+error,kOfxStatFailed);
    }
   }else{
   const int live=i.sequence?adr_sequence_next_output(i.sequence):-1;
   // Recreate on backward seeks, and on forward jumps when a checkpoint is
   // closer to the target than the live sequence position.
   const bool jump=live>=0&&live<index&&i.history&&adr_history_resume_frame(i.history,index)>live+1;
   if(!i.sequence||live<0||live>index||jump){
    // Seeks resume from the nearest motion-history checkpoint <= index
    // (exact), instead of replaying from the clip start.
    if(i.key!=key.str())i.close();
    if(i.sequence)adr_sequence_destroy(i.sequence);
    i.sequence=nullptr;
    if(!i.history)i.history=adr_history_create();
    i.sequence=adr_sequence_create_resumable(context(),&c,w,h,count,i.history,index,error,sizeof(error));
    require(i.sequence,std::string("Advanced Dirt Removal sequence: ")+error,kOfxStatFailed);i.key=key.str();i.first=range[0];
    {double par=1;prop->propGetDouble(source.handle,kOfxImagePropPixelAspectRatio,0,&par);
     auto depth=[](const Image&m){return std::string(m.depth==PixelDepth::Float?"float":m.depth==PixelDepth::Half?"half":"16-bit")+(m.channels==4?" RGBA":" RGB");};
     std::ostringstream s;s<<i.label()<<(live<0?" sequence start: ":" sequence restart (seek from index "+std::to_string(live)+"): ")<<w<<"x"<<h<<" pixel aspect "<<par
      <<", frames "<<range[0]<<".."<<range[1]<<" ("<<count<<"), requested frame "<<time<<", resuming at index "<<adr_sequence_next_output(i.sequence)
      <<", source "<<depth(source)<<", output "<<depth(output)<<", window "<<window[0]<<","<<window[1]<<","<<window[2]<<","<<window[3]<<", controls "<<identity;
     adrlog::info(s.str());}
    if(hashLogDir())hashLog("seq "+std::to_string(w)+"x"+std::to_string(h)+" first="+std::to_string(range[0])+" count="+std::to_string(count)+" resume="+std::to_string(index)+" engine="+engineBackend+" rcpss="+(adr_reciprocal_table()?adr_reciprocal_table():"?")+" key="+i.key);
    if(std::getenv("ADR_DEBUG"))std::fprintf(stderr,"adr seek instance=%p time=%d resume=%d\n",(void*)&i,time,adr_sequence_next_output(i.sequence));
   }
   std::vector<float> rgb;
   for(int next=adr_sequence_next_output(i.sequence);next>=0&&next<=index;next=adr_sequence_next_output(i.sequence)){
    require(!fx->abort(e),"render aborted",kOfxStatFailed);
    const bool wanted=next==index;
    int produced=-1;
    auto te=std::chrono::steady_clock::now();
    if(wanted){
     i.scratch.resize(size_t(w)*h*3);
     // Render on a worker thread while this (host) thread fetches the source
     // frame the next request will need: host decode overlaps GPU work.
     const int ahead=adr_sequence_next_source(i.sequence);
     auto job=std::async(std::launch::async,[&]{return adr_sequence_pull(i.sequence,i.scratch.data(),size_t(w)*12,error,sizeof(error));});
     if(ahead>=0&&ahead<=index+4&&i.prefetchTime!=range[0]+ahead){
      try{readSource(i,e,range[0]+ahead,w,h,source.b,i.prefetch);i.prefetchTime=range[0]+ahead;i.prefetchKey=i.key;}
      catch(...){job.wait();throw;}
     }
     produced=job.get();
     if(produced>=0)hashFrame("out",range[0]+produced,i.scratch.data(),i.scratch.size());
    }
    else produced=adr_sequence_skip(i.sequence,error,sizeof(error));
    require(produced>=-1,std::string("Advanced Dirt Removal render: ")+error,kOfxStatFailed);
    tEngine+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-te).count();
    if(produced>=0)continue;
    const int pending=adr_sequence_next_source(i.sequence);require(pending>=0,"Advanced Dirt Removal sequence stalled",kOfxStatFailed);
    auto tf=std::chrono::steady_clock::now();
    const float* pixels=nullptr;
    if(i.prefetchTime==range[0]+pending&&i.prefetchKey==i.key){pixels=i.prefetch.data();i.prefetchTime=std::numeric_limits<int>::min();}
    else{readSource(i,e,range[0]+pending,w,h,source.b,rgb);pixels=rgb.data();}
    tFetch+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tf).count();
    hashFrame("in",range[0]+pending,pixels,size_t(w)*h*3);
    std::vector<float> raw,lin;if(st.encoding){raw.assign(pixels,pixels+size_t(w)*h*3);toLinear(raw,lin);pixels=lin.data();}
    require(adr_sequence_push(i.sequence,pixels,size_t(w)*12,error,sizeof(error))==0,std::string("Advanced Dirt Removal upload: ")+error,kOfxStatFailed);
   }
   }
   i.cached=std::make_shared<const std::vector<float>>(std::move(i.scratch));i.cachedFrame=time;i.sourcePrints[time]=sourcePrint();
   if(useShared){const uint64_t print=sourcePrint();auto t0=std::chrono::steady_clock::now();frame_cache::shared().put(cleanKey,print,i.cached);tShared+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();}
   if(paintOn){
    // Keep nearby frames within 512 MiB; very large frames use only cached above.
    const size_t bytes=i.cached->size()*sizeof(float),keep=paint::cacheFrames(bytes);
    if(keep)i.recent[time]=i.cached;
    while(i.recent.size()>keep){auto farthest=i.recent.begin();for(auto it=i.recent.begin();it!=i.recent.end();++it)if(std::abs(it->first-time)>std::abs(farthest->first-time))farthest=it;i.recent.erase(farthest);}
   }
  }
  trace(&i,time-range[0],traceLive,traceWhat,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-traceStart).count(),tFetch,tEngine);
  {const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-traceStart).count();
   if(traceWhat[0]=='c')++i.servedCached;else if(!skipCleaning){++i.rendered;i.renderMs+=ms;}
   char line[240];std::snprintf(line,sizeof line," frame %d (%d/%d) %s in %.1f ms (host fetch %.1f, GPU %.1f, shared cache %.1f)",time,index+1,count,prepared?"prepared (no cleaning)":skipCleaning?"unchanged (no cleaning)":sharedHit?"from shared cache":traceWhat[0]=='c'?"from cache":"rendered",ms,tFetch,tEngine,tShared);
   adrlog::info(i.label()+line);}
  const float* pixels=preparedClean?preparedClean->data():prepared||skipCleaning?working.data():i.cached->data();
  // Finishing: Cineon delta, auto recovery, the Mask input, limits and views. Skipped (output = the
  // engine's picture) when none of them is active.
  std::vector<float> finished;
  {std::vector<float> mm,ms;
   // Existing projects retain the optional external-mask route. Once the
   // built-in brush is used, its document owns manual recovery on every frame,
   // including frames without a stroke and while editing is switched off.
   const bool haveMask=sourceKind!=1&&!builtInBrush&&readMask(i,time,w,h,source.b,mm,ms);  // with Additional source the mask input may carry the original
   // Spanish dirt removal with nothing painted leaves the picture unrepaired.
   const bool active=prepared||st.encoding||st.mode||haveMask||painted||st.maskMode==2||st.rect||st.frames||st.amount<1||st.view!=0;
   if(active){
    std::vector<float> fetchedSource;const std::vector<float>* sourcePixels=prepared?original.get():skipCleaning?&working:haveCurrentSource?&currentSource:nullptr;
    if(!sourcePixels){readSource(i,e,time,w,h,source.b,fetchedSource);sourcePixels=&fetchedSource;}const auto&src=*sourcePixels;
    std::vector<float> enc;const float*rep=pixels;if(st.encoding&&!skipCleaning&&!prepared){cineonDelta(src,*i.cached,enc);rep=enc.data();}
    std::vector<uint8_t> matte;
    if(st.mode&&st.maskMode==2)throw Error(kOfxStatFailed,spanishRecoveryError);  // shown by the host as a render error
    if(st.mode&&st.maskMode!=2){  // Spanish dirt removal repairs only inside the mask: nothing to give back
     const std::string dir=recoveryShotFolder(i,e,st,identity,range,w,h,source.b);
     std::vector<uint8_t> prob;bool have=loadProbability(dir,time,w,h,prob);
     if(!have){computeProbability(i,e,time,range,w,h,source.b,src,rep,prob);saveProbability(dir,time,w,h,prob);have=true;}
     if(have){const uint8_t cut=uint8_t(1+std::nearbyint(st.confidence*254));matte.resize(prob.size());for(size_t k=0;k<prob.size();++k)matte[k]=prob[k]>=cut;}
    }
    auto layers=painted?i.paintLayers.get(strokes,time,w,h):nullptr;
    finish(st,time,w,h,src,rep,matte,haveMask,mm,ms,layers.get(),finished);pixels=finished.data();
   }}
  // Validate every sample before publishing any output pixel.
  std::atomic<bool> valid{true};
  parallelRows(h,[&](int y0,int y1){bool ok=true;for(size_t k=size_t(y0)*w*3;k<size_t(y1)*w*3;++k){ok&=finiteBits(pixels[k]);if(output.depth==PixelDepth::Half&&ok)try{(void)encodeHalf(pixels[k]);}catch(...){ok=false;}}if(!ok)valid=false;});
  require(valid.load(),"nonfinite or unrepresentable repaired output");
  require(!fx->abort(e),"render aborted",kOfxStatFailed);
  const auto tw=std::chrono::steady_clock::now();
  const bool fastFloat=output.depth==PixelDepth::Float&&source.depth==PixelDepth::Float;
  parallelRows(window[3]-window[1],[&](int r0,int r1){
   for(int y=window[1]+r0;y<window[1]+r1;++y){
    const int row=source.b[3]-1-y;
    if(fastFloat){
     float*out=reinterpret_cast<float*>(output.row(row));const float*src=reinterpret_cast<const float*>(source.row(row));const int ch=output.channels;
     for(int x=window[0];x<window[2];++x){const int col=x-source.b[0];const float*p=&pixels[(size_t(row)*w+col)*3];float*q=out+size_t(col)*ch;q[0]=p[0];q[1]=p[1];q[2]=p[2];if(ch==4)q[3]=src[size_t(col)*4+3];}
    }else{
     for(int x=window[0];x<window[2];++x){int col=x-source.b[0];for(int ch=0;ch<3;++ch)output.write(col,row,ch,pixels[(size_t(row)*w+col)*3+ch]);if(output.channels==4){if(output.depth==source.depth)std::memcpy(output.sample(col,row,3),source.sample(col,row,3),size_t(source.sampleBytes));else output.write(col,row,3,source.read(col,row,3));}}
    }
   }
  });
  output.publish(window);
  completeTiming.success=true;
  trace(&i,time-range[0],-1,"write",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tw).count());
  return kOfxStatOK;
 }catch(...){i.close();throw;}
}
OfxStatus entry(const char*action,const void*handle,OfxPropertySetHandle in,OfxPropertySetHandle out){
 // There is no private persistent state to serialize. Resolve can call this
 // from its UI thread while a render waits for that thread in clipGetImage.
 // Taking the render lock here creates a host/plugin lock inversion.
 if(!std::strcmp(action,kOfxActionSyncPrivateData))return kOfxStatReplyDefault;
 // Hosts may bracket a programmatic parameter write with these notifications,
 // including while CreateInstance is refreshing the paint controls. Track
 // only UI changes here; never reacquire the lifecycle or render locks.
 if(!std::strcmp(action,kOfxActionBeginInstanceChanged)||!std::strcmp(action,kOfxActionEndInstanceChanged)){
  auto effect=(OfxImageEffectHandle)handle;
  try{if(isLiveInstance(effect)){if(!std::strcmp(action,kOfxActionBeginInstanceChanged)){cleaning_ui::begin(effect);paint_ui::beginChanges(effect);}else{cleaning_ui::end(effect);paint_ui::endChanges(effect);}}}
  catch(const std::exception&ex){paint_ui::changeBatches.erase(effect);adrlog::error(std::string("paint changes: ")+ex.what());return kOfxStatFailed;}
  return kOfxStatReplyDefault;
 }
 // Some hosts bracket every frame. These callbacks do not own or change the
 // bounded temporal history; PurgeCaches, edits and destruction release it.
 // Hosts may issue them while a render waits for host image acquisition.
 if(!std::strcmp(action,kOfxImageEffectActionBeginSequenceRender))return kOfxStatReplyDefault;
 if(!std::strcmp(action,kOfxImageEffectActionEndSequenceRender))return kOfxStatOK;
 auto e=(OfxImageEffectHandle)handle;
 // Metadata reads only immutable clip handles and host properties. Resolve
 // can request it on another thread while clipGetImage waits for that thread;
 // serializing these queries with the render creates a lock inversion.
 const bool metadata=!std::strcmp(action,kOfxImageEffectActionIsIdentity)||!std::strcmp(action,kOfxImageEffectActionGetFramesNeeded)||!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)||!std::strcmp(action,kOfxImageEffectActionGetRegionsOfInterest)||!std::strcmp(action,kOfxImageEffectActionGetClipPreferences);
 // Per-instance actions take only that instance's lock, so a downstream
 // instance waiting in clipGetImage never blocks an upstream instance's
 // render (chained nodes). Lifecycle and descriptor actions take the global lock.
 const bool perInstance=!std::strcmp(action,kOfxImageEffectActionRender)||!std::strcmp(action,kOfxActionInstanceChanged)||!std::strcmp(action,kOfxActionPurgeCaches);
 std::unique_lock<std::mutex> lock;
 try{
 // Time/control notifications already preserve history: every render keys
 // controls by their actual values. They must not block a host/UI callback.
 // A render error stays on the node until it is cleared, and some hosts (Nuke) will not render again while it is shown.
 // Clear it on any change and whenever the host re-validates the node; the next render reports it again if it remains.
 if((!std::strcmp(action,kOfxActionInstanceChanged)||!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)||!std::strcmp(action,kOfxImageEffectActionGetFramesNeeded))&&persistentMessages&&isLiveInstance(e))persistentMessages->clearPersistentMessage(e);
 if(!std::strcmp(action,kOfxActionInstanceChanged)&&in){char*type=nullptr;char*name=nullptr;
  if(isLiveInstance(e))refreshRecoveryUI(e);
  if(user_defaults::changed(e,in))return kOfxStatOK;
  if(prop->propGetString(in,kOfxPropName,0,&name)==kOfxStatOK&&name&&!std::strcmp(name,"auto_recovery")){
   auto&i=instance(e);int on=0;params->paramGetValue(i.parameters.at("auto_recovery"),&on);
   if(!on&&(i.recoveryState==Instance::Queued||i.recoveryState==Instance::Running)){
    i.restoreAutoOnCancel=false;i.cancelCalculation=true;i.calculateRequested=false;if(i.recoveryState==Instance::Queued)i.recoveryState=Instance::Cancelled;refreshRecoveryUI(e);
   }
  }
  if(prop->propGetString(in,kOfxPropName,0,&name)==kOfxStatOK&&name&&!std::strcmp(name,"mask_mode"))updateRecoveryEnabled(instance(e));
  cleaning_ui::changed(e,in);
  if(name&&!std::strcmp(name,"prepare_shot")){
   require(paint_ui::preparationAvailable(),"Shot preparation is experimental and disabled in this build configuration");
   auto&i=instance(e);auto old=std::atomic_load(&i.prepareFeedback);
   if(old&&old->phase<recovery_feedback::State::Complete){old->cancel=true;return kOfxStatOK;}
   require(paintHost(),"Prepare shot is supported in DaVinci Resolve only");
   require(paint_ui::getInt(e,"paint_source_kind")!=1,"Additional source paints an already processed picture; select Clean this clip before preparing");
   require(paint_ui::getInt(e,"mask_mode")==1||paint_ui::getInt(e,"auto_recovery")==0,"Turn Auto recovery off before preparing a shot for manual painting");
   const auto folder=defaultRecoveryFolder()+"/painting/shot-"+std::to_string(adrlog::pid())+"-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
   auto feedback=std::make_shared<recovery_feedback::State>();feedback->title="Advanced Dirt Removal — Prepare shot";std::atomic_store(&i.prepareFeedback,feedback);
   i.prepareRequested=true;
   paint_ui::setText(e,"prepare_folder",folder);
   paint_ui::setInt(e,"paint_source_kind",2);
   paint_ui::setInt(e,"prepare_request",(paint_ui::getInt(e,"prepare_request")+1)%1000000);
   recovery_feedback::show(feedback);updateRecoveryEnabled(i);return kOfxStatOK;
  }
  // Paint controls: UI thread work only, never the render lock.
  if(prop->propGetString(in,kOfxPropType,0,&type)==kOfxStatOK&&type&&!std::strcmp(type,kOfxTypeParameter)&&prop->propGetString(in,kOfxPropName,0,&name)==kOfxStatOK&&name&&paint_ui::isParam(name)){paint_ui::changed(e,in);return kOfxStatOK;}
  if(prop->propGetString(in,kOfxPropType,0,&type)==kOfxStatOK&&type&&!std::strcmp(type,kOfxTypeParameter)&&prop->propGetString(in,kOfxPropName,0,&name)==kOfxStatOK&&name&&!std::strcmp(name,"mask_mode")){paint_ui::changed(e,in);updateRecoveryEnabled(instance(e));refreshRecoveryUI(e);return kOfxStatOK;}
  if(prop->propGetString(in,kOfxPropType,0,&type)==kOfxStatOK&&type&&!std::strcmp(type,kOfxTypeParameter)&&prop->propGetString(in,kOfxPropName,0,&name)==kOfxStatOK&&name&&!std::strcmp(name,"recovery_calculate")){
   // Never fetch frames here: in some hosts this runs on the UI thread, which the image fetch waits for.
   auto&i=instance(e);
   {int spanish=0;if(params->paramGetValue(i.parameters.at("mask_mode"),&spanish)==kOfxStatOK&&spanish==1){if(messages)messages->message(e,kOfxMessageError,"adr","%s",spanishRecoveryError);return kOfxStatOK;}}
   if(i.recoveryState==Instance::Queued||i.recoveryState==Instance::Running){
    i.cancelCalculation=true;i.calculateRequested=false;
    if(i.recoveryState==Instance::Queued)i.recoveryState=Instance::Cancelled;
    refreshRecoveryUI(e);return kOfxStatOK;
   }
   int previousAuto=0;params->paramGetValue(i.parameters.at("auto_recovery"),&previousAuto);i.recoveryPreviousAuto=previousAuto;i.restoreAutoOnCancel=true;
   i.cancelCalculation=false;i.recoveryDone=0;i.recoveryTotal=0;i.recoveryState=Instance::Queued;i.calculateRequested=true;
   auto feedback=std::make_shared<recovery_feedback::State>();std::atomic_store(&i.recoveryFeedback,feedback);
   if(paintHost()){
    recovery_feedback::show(feedback);
#ifdef _WIN32
    // The Windows panel opens on its own message loop.
    adrlog::info(i.label()+" auto recovery: requested native progress panel");
#else
    adrlog::info(i.label()+(feedback->presented?" auto recovery: native progress panel shown":" auto recovery: native progress panel unavailable in this callback"));
#endif
   }
   refreshRecoveryUI(e);
   int n=0;params->paramGetValue(i.parameters.at("recovery_request"),&n);
   if(params->paramSetValue){params->paramSetValue(i.parameters.at("auto_recovery"),1);params->paramSetValue(i.parameters.at("recovery_request"),(n+1)%1000000);}
   adrlog::info(i.label()+" auto recovery: whole-shot calculation requested; it runs with the next render");
   return kOfxStatOK;}}
 if(!std::strcmp(action,kOfxActionInstanceChanged)&&!invalidatesHistory(in))return kOfxStatReplyDefault;
 if(perInstance)lock=std::unique_lock<std::mutex>(instance(e).lock);else if(!metadata)lock=std::unique_lock<std::mutex>(mutex);
#if ADR_HOST_GPU_TRANSPORT
 // Purge/unload can free CUDA engine resources too. Preserve the caller's
 // context without adding driver operations to reentrant metadata actions.
 adr_host::GpuScope lifecycleTransport(adr_host::GpuBackend::Cpu,nullptr,!std::strcmp(action,kOfxActionUnload)||!std::strcmp(action,kOfxActionPurgeCaches),!std::strcmp(action,kOfxActionUnload));
#endif
 if(!std::strcmp(action,kOfxActionLoad)){require(host,"missing host");fx=(OfxImageEffectSuiteV1*)host->fetchSuite(host->host,kOfxImageEffectSuite,1);prop=(OfxPropertySuiteV1*)host->fetchSuite(host->host,kOfxPropertySuite,1);params=(OfxParameterSuiteV1*)host->fetchSuite(host->host,kOfxParameterSuite,1);messages=(OfxMessageSuiteV1*)host->fetchSuite(host->host,kOfxMessageSuite,1);persistentMessages=(OfxMessageSuiteV2*)host->fetchSuite(host->host,kOfxMessageSuite,2);interacts=(OfxInteractSuiteV1*)host->fetchSuite(host->host,kOfxInteractSuite,1);timeline=(OfxTimeLineSuiteV1*)host->fetchSuite(host->host,kOfxTimeLineSuite,1);progressV2=(OfxProgressSuiteV2*)host->fetchSuite(host->host,kOfxProgressSuite,2);progressV1=progressV2?nullptr:(OfxProgressSuiteV1*)host->fetchSuite(host->host,kOfxProgressSuite,1);require(fx&&prop&&params,"required OFX suites unavailable",kOfxStatErrMissingHostFeature);logStartup();return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionUnload)){paint_navigation::shutdown();{auto c=frame_cache::shared().stats();adrlog::info("shared cleaned-frame cache: "+std::to_string(c.hits)+" hits, "+std::to_string(c.misses)+" misses, "+std::to_string(c.stale)+" stale (source changed), "+std::to_string(c.evicted)+" evicted");}frame_cache::shared().clear();recovery_feedback::shutdown();if(recoveryEngine){adr_recovery_destroy(recoveryEngine);recoveryEngine=nullptr;}if(engineContext){uint64_t a=0,p=0;adr_context_memory(engineContext,&a,&p);adrlog::info("unload: GPU memory peak "+std::to_string(p>>20)+" MiB");adr_context_destroy(engineContext);}else adrlog::info("unload");engineContext=nullptr;adrlog::close();return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionDescribe)){auto p=properties(e);
#if ADR_HOST_METAL_TRANSPORT
 // GPU extensions are optional. CPU-only hosts may reject this property;
 // they must still be able to discover and render the existing CPU path.
 {auto s=prop->propSetString(p,"OfxImageEffectPropMetalRenderSupported",0,"true");
  if(s!=kOfxStatErrUnknown&&s!=kOfxStatErrUnsupported)check(s,"optional Metal host transport");}
#elif ADR_HOST_GPU_TRANSPORT
 for(auto key:{"OfxImageEffectPropCudaRenderSupported","OfxImageEffectPropCudaStreamSupported","OfxImageEffectPropOpenCLRenderSupported"}){
  const auto status=prop->propSetString(p,key,0,"true");if(status!=kOfxStatErrUnknown&&status!=kOfxStatErrUnsupported)check(status,"optional GPU host transport");
 }
#endif
if(paintHost())check(prop->propSetPointer(p,kOfxImageEffectPluginPropOverlayInteractV1,0,(void*)paint_ui::overlay),"paint overlay");
ss(p,kOfxPropLabel,ADR_PLUGIN_LABEL);ss(p,kOfxImageEffectPluginPropGrouping,"FabioColor");{auto status=prop->propSetString(p,kOfxPropPluginDescription,0,("Open-source temporal dirt and dust removal for film scans (Metal, CUDA, OpenCL or CPU). Works on the RGB values as delivered; review moving shots before delivery."+(adrlog::path().empty()?std::string():" Diagnostic log: "+adrlog::path())).c_str());if(status!=kOfxStatErrUnknown)check(status,"optional plugin description");}ss(p,kOfxImageEffectPropSupportedContexts,kOfxImageEffectContextFilter);ss(p,kOfxImageEffectPropSupportedPixelDepths,kOfxBitDepthFloat,0);ss(p,kOfxImageEffectPropSupportedPixelDepths,kOfxBitDepthHalf,1);ss(p,kOfxImageEffectPropSupportedPixelDepths,kOfxBitDepthShort,2);ss(p,kOfxImageEffectPluginRenderThreadSafety,kOfxImageEffectRenderInstanceSafe);si(p,kOfxImageEffectPluginPropHostFrameThreading,0);si(p,kOfxImageEffectPropSupportsTiles,0);si(p,kOfxImageEffectPropSupportsMultiResolution,0);si(p,kOfxImageEffectPropTemporalClipAccess,1);si(p,kOfxImageEffectPropSupportsMultipleClipPARs,0);si(p,kOfxImageEffectPropSupportsMultipleClipDepths,0);return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionDescribeInContext)){require(str(in,kOfxImageEffectPropContext)==kOfxImageEffectContextFilter,"Filter context required");for(auto name:{"Source","Output"}){OfxPropertySetHandle p;check(fx->clipDefine(e,name,&p),"define clip");ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGB,0);ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGBA,1);si(p,kOfxImageEffectPropSupportsTiles,0);if(!std::strcmp(name,"Source"))si(p,kOfxImageEffectPropTemporalClipAccess,1);}{OfxPropertySetHandle p;check(fx->clipDefine(e,"Mask",&p),"define mask clip");ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentAlpha,0);ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGBA,1);ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGB,2);si(p,kOfxImageClipPropOptional,1);si(p,kOfxImageClipPropIsMask,1);si(p,kOfxImageEffectPropSupportsTiles,0);}{OfxPropertySetHandle p;check(fx->clipDefine(e,"Original",&p),"define additional source clip");/* identifier kept: Resolve keeps connections by clip name */ss(p,kOfxPropLabel,"Additional source");ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGB,0);ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGBA,1);si(p,kOfxImageClipPropOptional,1);si(p,kOfxImageEffectPropSupportsTiles,0);}OfxParamSetHandle ps;check(fx->getParamSet(e,&ps),"parameters");defineControlPresentation(ps);
 return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionCreateInstance)){auto i=std::make_unique<Instance>();check(fx->clipGetHandle(e,"Source",&i->source,nullptr),"source clip");check(fx->clipGetHandle(e,"Output",&i->output,nullptr),"output clip");OfxParamSetHandle ps;check(fx->getParamSet(e,&ps),"parameters");for(auto&c:controls)check(params->paramGetHandle(ps,c.name,&i->parameters[c.name],nullptr),"control handle");check(params->paramGetHandle(ps,"encoding",&i->parameters["encoding"],nullptr),"encoding handle");for(auto&x:extras)check(params->paramGetHandle(ps,x.name,&i->parameters[x.name],nullptr),"extra handle");for(auto n:{"paint","paint_data","paint_source_kind","paint_source_folder","paint_source_offset","prepare_folder","prepare_request"})check(params->paramGetHandle(ps,n,&i->parameters[n],nullptr),"paint handle");if(fx->clipGetHandle(e,"Mask",&i->mask,nullptr)!=kOfxStatOK)i->mask=nullptr;if(fx->clipGetHandle(e,"Original",&i->original,nullptr)!=kOfxStatOK)i->original=nullptr;check(prop->propSetPointer(properties(e),kOfxPropInstanceData,0,i.get()),"instance data");static int created=0;i->id=++created;trackInstance(e);adrlog::info(i->label()+" created");i.release();lock.unlock();/* Host parameter writes can synchronously reenter the adapter. */try{cleaning_ui::initialize(e);}catch(...){}try{updateRecoveryEnabled(instance(e));}catch(...){}try{paint_ui::initializePainting(e);paint_ui::refresh(e);}catch(...){}return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionDestroyInstance)){auto&i=instance(e);i.close();adrlog::info(i.label()+" destroyed");untrackInstance(e);paint_ui::changeBatches.erase(e);paint_ui::histories.erase(e);paint_ui::cursorTimes.erase(e);delete &i;check(prop->propSetPointer(properties(e),kOfxPropInstanceData,0,nullptr),"clear instance");return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionInstanceChanged)){auto&i=instance(e);if(invalidatesHistory(in)){++i.generation;frame_cache::shared().erasePrefix(std::to_string(i.id)+'|');  // this effect's cleaned frames only; other nodes are unaffected
 if(i.sequence){char*type=nullptr;char*name=nullptr;char*why=nullptr;if(in){prop->propGetString(in,kOfxPropType,0,&type);prop->propGetString(in,kOfxPropName,0,&name);prop->propGetString(in,kOfxPropChangeReason,0,&why);}adrlog::info(i.label()+" history reset: host reported a change ("+(type?type:"?")+" "+(name?name:"?")+", "+(why?why:"?")+")");}i.close();}return kOfxStatReplyDefault;}
 if(!std::strcmp(action,kOfxActionPurgeCaches)){auto&i=instance(e);adrlog::info(i.label()+" host asked to purge caches");i.purge();frame_cache::shared().clear();return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionIsIdentity)){
  const int t=frameTime(in);const auto range=sourceRange(instance(e));
  if(t>=range[0]&&t<=range[1]&&untouchedSpanish(e,t)&&readSettings(instance(e),t).view==0){
   ss(out,kOfxPropName,"Source");check(prop->propSetDouble(out,kOfxPropTime,0,t),"identity time");return kOfxStatOK;
  }
  return kOfxStatReplyDefault;
 }
 if(!std::strcmp(action,kOfxImageEffectActionGetFramesNeeded)){
  const int t=frameTime(in);auto&i=instance(e);const auto r=sourceRange(i);
  double v[2]={double(t),double(t)};
  if(t>=r[0]&&t<=r[1]&&(i.prepareRequested||(paint_ui::getInt(e,"paint_source_kind")==0&&!untouchedSpanish(e,t)))){
   if(spanishWindow(e,t)){if(!nativeScale(in)||!spanishCleanCached(e,t)){v[0]=std::max(r[0],t-kSpanishBack);v[1]=std::min(r[1],t+kSpanishAhead);}}
   else{v[0]=r[0];v[1]=i.prepareRequested||i.calculateRequested||i.recoveryState==Instance::Queued||i.recoveryState==Instance::Running?r[1]:std::min(r[1],t+4);}
  }
  {const double now[2]={double(t),double(t)};prop->propSetDoubleN(out,"OfxImageClipPropFrameRange_Original",2,now);}
  return prop->propSetDoubleN(out,"OfxImageClipPropFrameRange_Source",2,v);
 }
 if(!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)||!std::strcmp(action,kOfxImageEffectActionGetRegionsOfInterest)){require(number(in,kOfxImageEffectPropRenderScale,0)>0&&number(in,kOfxImageEffectPropRenderScale,1)>0,"Invalid metadata render scale");OfxRectD rod;check(fx->clipGetRegionOfDefinition(instance(e).source,number(in,kOfxPropTime),&rod),"source RoD");double v[]={rod.x1,rod.y1,rod.x2,rod.y2};return prop->propSetDoubleN(out,!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)?kOfxImageEffectPropRegionOfDefinition:"OfxImageClipPropRoI_Source",4,v);}
 if(!std::strcmp(action,kOfxImageEffectActionGetClipPreferences)){si(out,kOfxImageEffectFrameVarying,1);
  // Alpha passes through unchanged, so the output keeps the source's
  // premultiplication state (an opaque source stays opaque downstream).
  OfxPropertySetHandle sp=nullptr;char*premult=nullptr;
  if(fx->clipGetPropertySet(instance(e).source,&sp)==kOfxStatOK&&prop->propGetString(sp,kOfxImageEffectPropPreMultiplication,0,&premult)==kOfxStatOK&&premult)
   prop->propSetString(out,kOfxImageEffectPropPreMultiplication,0,premult);
  return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionRender))return render(e,in);
 return kOfxStatReplyDefault;
 }catch(const Error&ex){
  if(std::strstr(ex.what(),"render aborted"))adrlog::info(who(e)+" "+action+": cancelled by the host");else adrlog::error(who(e)+" "+action+": "+ex.what());
  reportError(e,ex.what());return ex.status;}
 catch(const std::bad_alloc&){adrlog::error(who(e)+" "+action+": out of memory (host)");return kOfxStatErrMemory;}
 catch(const std::exception&ex){adrlog::error(who(e)+" "+action+": "+ex.what());reportError(e,ex.what());return kOfxStatFailed;}
 catch(...){adrlog::error(who(e)+" "+action+": unknown failure");return kOfxStatFailed;}}
void setHost(OfxHost*h){host=h;}
OfxPlugin plugin={kOfxImageEffectPluginApi,1,ADR_PLUGIN_ID,2,0,setHost,entry};
}
#include "source_capture.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
extern "C" EXPORT int OfxGetNumberOfPlugins(){return 2;}
extern "C" EXPORT OfxPlugin* OfxGetPlugin(int index){return index==0?&plugin:index==1?adrCapturePlugin():nullptr;}
#if defined(ADR_PAINT_TEST_HOOKS)
// Read-only, opt-in native-host evidence. Absent from production builds. The
// test driver invokes this on the host UI thread with a new output directory.
extern "C" EXPORT int ADRPaintTestSnapshot(const char* folder){
 try{
  require(folder&&*folder,"missing test output folder");
  std::vector<OfxImageEffectHandle> effects;
  {std::lock_guard<std::mutex> guard(liveInstancesMutex);effects.assign(liveInstances.begin(),liveInstances.end());}
  const auto root=std::filesystem::u8path(folder);std::filesystem::create_directories(root);
  for(auto effect:effects){
   auto& item=instance(effect);auto file=root/("instance-"+std::to_string(item.id)+".txt");require(!std::filesystem::exists(file),"test snapshot already exists");
   std::ofstream out(file);require(bool(out),"cannot write test snapshot");
   for(auto name:{"paint","paint_tool","paint_shape","paint_stroke","paint_hold","mask_mode","view","paint_source_kind","paint_source_offset","paint_outlines"})out<<name<<'='<<paint_ui::getInt(effect,name)<<'\n';
   out<<std::setprecision(17);for(auto name:{"paint_size","paint_softness","paint_opacity"})out<<name<<'='<<paint_ui::getDouble(effect,name)<<'\n';
   if(paint_ui::cursorTimes.count(effect))out<<"viewer_time="<<paint_ui::cursorTimes.at(effect)<<'\n';
   {double hostTime=0;const auto status=timeline&&timeline->getTime?timeline->getTime(effect,&hostTime):kOfxStatErrUnsupported;out<<"host_time_status="<<status<<'\n'<<"host_time="<<hostTime<<'\n';}
   int overlayIndex=0;for(auto*overlay:paint_ui::testOverlays)if(overlay->effect==effect){
    const auto prefix="overlay"+std::to_string(overlayIndex++)+"_";
    out<<prefix<<"drawing="<<overlay->drawing<<'\n'<<prefix<<"moving="<<overlay->moving<<'\n'<<prefix<<"adjusting="<<overlay->adjusting<<'\n'<<prefix<<"cursor="<<overlay->hasCursor<<'\n'<<prefix<<"index="<<overlay->index<<'\n'<<prefix<<"frame="<<overlay->frame<<'\n'<<prefix<<"x="<<overlay->cursor.x<<'\n'<<prefix<<"y="<<overlay->cursor.y<<'\n'<<prefix<<"down_x="<<overlay->down.x<<'\n'<<prefix<<"down_y="<<overlay->down.y<<'\n';
   }
   out<<"paint_data:\n"<<paint_ui::getText(effect,"paint_data");require(bool(out),"test snapshot write failed");
  }
  return 0;
 }catch(...){return 1;}
}
#endif
