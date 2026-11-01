// Advanced Dirt Removal: OFX image-effect adapter for the linked engine
// (Metal, CUDA, OpenCL or CPU). Fetches temporal source frames from the host,
// streams them through the engine and writes the repaired frame.
#include "ofxImageEffect.h"
#include "ofxProperty.h"
#include "ofxTimeLine.h"
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
#include "../../engine/tools/sha256.h"
#include "diagnostic_log.h"
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#ifndef ADR_PLUGIN_VERSION
#define ADR_PLUGIN_VERSION "dev"
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
namespace {
OfxHost* host=nullptr;
OfxImageEffectSuiteV1* fx=nullptr;
OfxPropertySuiteV1* prop=nullptr;
OfxParameterSuiteV1* params=nullptr;
OfxMessageSuiteV1* messages=nullptr;
OfxMessageSuiteV2* persistentMessages=nullptr;
std::mutex mutex;
std::mutex liveInstancesMutex;
std::set<OfxImageEffectHandle> liveInstances;
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
std::string str(OfxPropertySetHandle p,const char*k){char*v=nullptr;check(prop->propGetString(p,k,0,&v),k);return v?v:"";}
int integer(OfxPropertySetHandle p,const char*k,int i=0){int v=0;check(prop->propGetInt(p,k,i,&v),k);return v;}
double number(OfxPropertySetHandle p,const char*k,int i=0){double v=0;check(prop->propGetDouble(p,k,i,&v),k);require(std::isfinite(v),std::string("nonfinite property ")+k);return v;}
void si(OfxPropertySetHandle p,const char*k,int v){check(prop->propSetInt(p,k,0,v),k);}
void ss(OfxPropertySetHandle p,const char*k,const char*v,int i=0){check(prop->propSetString(p,k,i,v),k);}
OfxPropertySetHandle properties(OfxImageEffectHandle e){OfxPropertySetHandle p;check(fx->getPropertySet(e,&p),"effect properties");return p;}
int frameTime(OfxPropertySetHandle p){double t=number(p,kOfxPropTime);require(t==std::round(t)&&std::abs(t)<=1e9,"fractional frame times are unsupported");return int(t);}
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
 Image(OfxImageClipHandle clip,int t,OfxImageEffectHandle=nullptr,const char*role="probe"){
  if(auto status=fx->clipGetImage(clip,t,nullptr,&handle);status!=kOfxStatOK)throw Error(status,std::string("host failed to provide required in-range image: ")+role+" frame "+std::to_string(t)+" (OFX status="+std::to_string(status)+")");
  try {
   require(handle,"null host image");
   auto token=str(handle,kOfxImageEffectPropPixelDepth);require(token==kOfxBitDepthFloat||token==kOfxBitDepthHalf||token==kOfxBitDepthShort,"Float, Half or unsigned Short RGB/RGBA required");depth=token==kOfxBitDepthFloat?PixelDepth::Float:token==kOfxBitDepthHalf?PixelDepth::Half:PixelDepth::Short;sampleBytes=depth==PixelDepth::Float?4:2;
   auto c=str(handle,kOfxImageEffectPropComponents);channels=c==kOfxImageComponentRGB?3:c==kOfxImageComponentRGBA?4:0;require(channels,"RGB/RGBA required");
   premult=channels==4&&str(handle,kOfxImageEffectPropPreMultiplication)==kOfxImagePreMultiplied;
   try{scaleOne(handle);}catch(const std::exception&ex){throw std::runtime_error(std::string(role)+" image: "+ex.what());}auto field=str(handle,kOfxImagePropField);
   // Some hosts label progressive full frames "Both"; accept that only when the clip itself is progressive.
   if(field!=kOfxImageFieldNone){OfxPropertySetHandle cp=nullptr;check(fx->clipGetPropertySet(clip,&cp),"image clip fields");auto order=str(cp,kOfxImageClipPropFieldOrder);require((field.empty()||field==kOfxImageFieldBoth)&&order==kOfxImageFieldNone,"Fielded image unsupported: image="+field+", clip="+order);}

   // Any pixel aspect: the effect works on the stored pixel grid (PAL/NTSC SD, anamorphic scans).
   check(prop->propGetIntN(handle,kOfxImagePropBounds,4,b.data()),"image bounds");stride=integer(handle,kOfxImagePropRowBytes);
   void*p=nullptr;check(prop->propGetPointer(handle,kOfxImagePropData,0,&p),"image pixels");data=static_cast<char*>(p);
   require(data&&b[2]>b[0]&&b[3]>b[1]&&int64_t(b[2])-b[0]<=16384&&int64_t(b[3])-b[1]<=16384,"invalid image bounds/data");
   require(std::abs(int64_t(stride))>=int64_t(width())*channels*sampleBytes,"insufficient rowBytes");
   std::array<int,4>rod{};check(prop->propGetIntN(handle,kOfxImagePropRegionOfDefinition,4,rod.data()),"image RoD");require(rod==b,"full-frame host images required");
  }catch(...){if(handle)fx->clipReleaseImage(handle);handle=nullptr;throw;}
 }
 ~Image(){if(handle)fx->clipReleaseImage(handle);} Image(const Image&)=delete;
 int width()const{return b[2]-b[0];}int height()const{return b[3]-b[1];}
 char*row(int topdown)const{return data+int64_t(height()-1-topdown)*stride;}
 char*sample(int x,int y,int c)const{return row(y)+(int64_t(x)*channels+c)*sampleBytes;}
 float read(int x,int y,int c)const{if(sampleBytes==4){float v;std::memcpy(&v,sample(x,y,c),4);return v;}uint16_t v;std::memcpy(&v,sample(x,y,c),2);return depth==PixelDepth::Half?decodeHalf(v):decodeShort(v);}
 bool opaque(int x,int y)const{if(sampleBytes==4)return read(x,y,3)==1.f;uint16_t v;std::memcpy(&v,sample(x,y,3),2);return v==(depth==PixelDepth::Half?0x3c00u:65535u);}
 void write(int x,int y,int c,float v)const{if(sampleBytes==4)std::memcpy(sample(x,y,c),&v,4);else{auto q=depth==PixelDepth::Half?encodeHalf(v):encodeShort(v);std::memcpy(sample(x,y,c),&q,2);}}
};
struct Control {const char*name;const char*label;bool boolean;double initial,maximum;int minimum=0;const char*const* choices=nullptr;bool integral=false;const char*hint=nullptr;};
const char*const analyseChoices[]={"Luma","RGB"};
const char*const shotChoices[]={"Off","Guard","Skip"};
// UI order. Names are stable identifiers saved in host projects; labels may change.
const Control controls[]={
 {"enabled","Enabled",true,1,1,0,nullptr,false,"Off passes the source through unchanged."},
 {"analyse","Analyse",false,0,1,0,analyseChoices,true,"Luma finds spots in brightness; RGB also finds spots that differ only in colour."},
 {"remove_dark","Remove dark spots",true,1,1,0,nullptr,false,"Repair spots darker than their surroundings (dust on a print, emulsion specks)."},
 {"remove_light","Remove light spots",true,1,1,0,nullptr,false,"Repair spots brighter than their surroundings (dirt on a negative, scratches through to the base)."},
 {"sensitivity","Sensitivity",false,.75,1,0,nullptr,false,"How readily a temporary change is treated as dirt. Higher finds fainter spots and risks repairing real detail."},
 {"extra_sensitivity","Extra sensitivity",true,0,1,0,nullptr,false,"For heavily damaged material: drop detail preservation wherever enough neighbouring frames agree. Can remove real detail."},
 {"repair_amount","Repair amount",false,1,2,0,nullptr,false,"How strongly detected spots are replaced from neighbouring frames."},
 {"analysis_detail","Analysis detail",false,.5,1,0,nullptr,false,"Balance of fine versus coarse analysis when separating dirt from grain and noise."},
 {"limit_spot_size","Limit spot size",true,0,1,0,nullptr,false,"Ignore spots larger than Largest spot."},
 {"largest_spot","Largest spot (px)",false,25,256,1,nullptr,true,"Largest spot diameter repaired when Limit spot size is on."},
 {"blend_repairs","Blend repairs",true,1,1,0,nullptr,false,"Soften the edges of repairs so they blend with grain."},
 {"blend_amount","Blend amount",false,.5,1,0,nullptr,false,"How much repair edges are softened."},
 {"motion_guard","Motion guard",false,.25,1,0,nullptr,false,"Protect moving detail from being mistaken for dirt. Higher protects more and cleans less in motion."},
 {"shot_changes","Shot changes",false,1,2,0,shotChoices,true,"Frames from another shot are never used for repair. Off: no extra care. Guard: repair frames next to a shot change more cautiously. Skip: leave frames next to a shot change unrepaired."},
 {"shot_guard","Shot-change guard",false,.75,1,0,nullptr,false,"How cautious Guard is next to a shot change."},
 {"flash_guard","Flash guard",true,0,1,0,nullptr,false,"Leave frames detected as single-frame flashes unrepaired."},
 {"extended_range","Extended-range input",true,0,1,0,nullptr,false,"Analyse values below 0 or above 1 (scene-referred or log-to-linear material) instead of the normal display range."}};
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
struct Instance {
 OfxImageClipHandle source=nullptr,output=nullptr;
 std::map<std::string,OfxParamHandle> parameters;
 adr_sequence* sequence=nullptr;
 adr_history* history=nullptr; // motion-history checkpoints for key
 std::string key;            // controls, range and geometry of the live sequence
 int first=0;
 uint64_t generation=0;
 int cachedFrame=std::numeric_limits<int>::min();
 std::vector<float> cached;  // last delivered output, top-down packed RGB
 std::vector<float> scratch;
 std::vector<float> prefetch;  // next source frame, fetched while the GPU renders
 int prefetchTime=std::numeric_limits<int>::min();
 std::string prefetchKey;
 std::mutex lock;            // serializes this instance's actions
 // Diagnostic log: instance number and the live sequence's statistics.
 int id=0;
 std::string label()const{return "instance #"+std::to_string(id);}
 int rendered=0,servedCached=0;double renderMs=0;float sourceMin=0,sourceMax=0;bool sourceSeen=false,flatWarned=false,rangeWarned=false;
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
 void close(){summary("closed");prefetchTime=std::numeric_limits<int>::min();if(sequence)adr_sequence_destroy(sequence);sequence=nullptr;if(history)adr_history_destroy(history);history=nullptr;key.clear();cachedFrame=std::numeric_limits<int>::min();cached.clear();}
 // Host memory purges drop frames and GPU buffers but keep the few-KB
 // motion-history checkpoints, which stay valid for the same key.
 void purge(){summary("host purged caches");prefetchTime=std::numeric_limits<int>::min();prefetch.clear();prefetch.shrink_to_fit();if(sequence)adr_sequence_destroy(sequence);sequence=nullptr;if(engineContext)adr_context_trim(engineContext);cachedFrame=std::numeric_limits<int>::min();cached.clear();cached.shrink_to_fit();scratch.clear();scratch.shrink_to_fit();}
 ~Instance(){close();}
};
Instance& instance(OfxImageEffectHandle e){void*p=nullptr;check(prop->propGetPointer(properties(e),kOfxPropInstanceData,0,&p),"instance data");require(p,"missing instance",kOfxStatErrBadHandle);return *static_cast<Instance*>(p);}
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
// Startup summary for the diagnostic log: plugin build, host, machine and overrides.
std::string hostText(const char*k){char*v=nullptr;return host&&host->host&&prop->propGetString(host->host,k,0,&v)==kOfxStatOK&&v?v:"";}
void logStartup(){
 std::array<int,3> version{};if(host&&host->host)prop->propGetIntN(host->host,kOfxPropVersion,3,version.data());
 adrlog::info(std::string("Advanced Dirt Removal ")+ADR_PLUGIN_VERSION+" (source "+ADR_SOURCE_REVISION+", built "+__DATE__+" "+__TIME__+"), engine "+adr_version()+", kernels "+std::string(adr_kernel_source_sha256()).substr(0,12));
 adrlog::info("plugin binary: "+adrlog::pluginPath());
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
OfxStatus render(OfxImageEffectHandle e,OfxPropertySetHandle in){
 auto&i=instance(e);
 try{
  if(persistentMessages)persistentMessages->clearPersistentMessage(e);
  scaleOne(in);int time=frameTime(in);auto range=sourceRange(i);require(time>=range[0]&&time<=range[1],"time outside source");
  Image source(i.source,time,e,"current"),output(i.output,time,e,"output");int w=source.width(),h=source.height();
  require(w>=256&&w<=16384&&h>=256&&h<=16384,"Advanced Dirt Removal needs full frames of 256 to 16384 pixels per side");require(source.b==output.b&&source.channels==output.channels,"output geometry/components differ");
  auto window=renderWindow(e,in,source.b);
  std::string identity;adr_controls c=readControls(i,time,identity);
  std::ostringstream key;key<<identity<<'|'<<range[0]<<'|'<<range[1]<<'|'<<w<<'x'<<h<<'|'<<i.generation;
  if(key.str()!=i.key){if(std::getenv("ADR_DEBUG")&&!i.key.empty())std::fprintf(stderr,"adr key change instance=%p\n",(void*)&i);i.close();}
  const int index=time-range[0],count=range[1]-range[0]+1;
  const auto traceStart=std::chrono::steady_clock::now();double tFetch=0,tEngine=0;
  const int traceLive=i.sequence?adr_sequence_next_output(i.sequence):-1;
  const char* traceWhat=i.cachedFrame==time?"cached":"render";
  if(i.cachedFrame!=time){
   char error[2048]={};
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
    require(adr_sequence_push(i.sequence,pixels,size_t(w)*12,error,sizeof(error))==0,std::string("Advanced Dirt Removal upload: ")+error,kOfxStatFailed);
   }
   i.cached.swap(i.scratch);i.cachedFrame=time;
  }
  trace(&i,time-range[0],traceLive,traceWhat,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-traceStart).count(),tFetch,tEngine);
  {const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-traceStart).count();
   if(traceWhat[0]=='c')++i.servedCached;else{++i.rendered;i.renderMs+=ms;}
   char line[200];std::snprintf(line,sizeof line," frame %d (%d/%d) %s in %.1f ms (host fetch %.1f, GPU %.1f)",time,index+1,count,traceWhat[0]=='c'?"from cache":"rendered",ms,tFetch,tEngine);
   adrlog::info(i.label()+line);}
  const float* pixels=i.cached.data();
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
  trace(&i,time-range[0],-1,"write",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tw).count());
  return kOfxStatOK;
 }catch(...){i.close();throw;}
}
OfxStatus entry(const char*action,const void*handle,OfxPropertySetHandle in,OfxPropertySetHandle out){
 // There is no private persistent state to serialize. Resolve can call this
 // from its UI thread while a render waits for that thread in clipGetImage.
 // Taking the render lock here creates a host/plugin lock inversion.
 if(!std::strcmp(action,kOfxActionSyncPrivateData))return kOfxStatReplyDefault;
 // Some hosts bracket every frame. These callbacks do not own or change the
 // bounded temporal history; PurgeCaches, edits and destruction release it.
 // Hosts may issue them while a render waits for host image acquisition.
 if(!std::strcmp(action,kOfxImageEffectActionBeginSequenceRender))return kOfxStatReplyDefault;
 if(!std::strcmp(action,kOfxImageEffectActionEndSequenceRender))return kOfxStatOK;
 auto e=(OfxImageEffectHandle)handle;
 // Metadata reads only immutable clip handles and host properties. Resolve
 // can request it on another thread while clipGetImage waits for that thread;
 // serializing these queries with the render creates a lock inversion.
 const bool metadata=!std::strcmp(action,kOfxImageEffectActionGetFramesNeeded)||!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)||!std::strcmp(action,kOfxImageEffectActionGetRegionsOfInterest)||!std::strcmp(action,kOfxImageEffectActionGetClipPreferences);
 // Per-instance actions take only that instance's lock, so a downstream
 // instance waiting in clipGetImage never blocks an upstream instance's
 // render (chained nodes). Lifecycle and descriptor actions take the global lock.
 const bool perInstance=!std::strcmp(action,kOfxImageEffectActionRender)||!std::strcmp(action,kOfxActionInstanceChanged)||!std::strcmp(action,kOfxActionPurgeCaches);
 std::unique_lock<std::mutex> lock;
 try{
 // Time/control notifications already preserve history: every render keys
 // controls by their actual values. They must not block a host/UI callback.
 if(!std::strcmp(action,kOfxActionInstanceChanged)&&!invalidatesHistory(in))return kOfxStatReplyDefault;
 if(perInstance)lock=std::unique_lock<std::mutex>(instance(e).lock);else if(!metadata)lock=std::unique_lock<std::mutex>(mutex);
 if(!std::strcmp(action,kOfxActionLoad)){require(host,"missing host");fx=(OfxImageEffectSuiteV1*)host->fetchSuite(host->host,kOfxImageEffectSuite,1);prop=(OfxPropertySuiteV1*)host->fetchSuite(host->host,kOfxPropertySuite,1);params=(OfxParameterSuiteV1*)host->fetchSuite(host->host,kOfxParameterSuite,1);messages=(OfxMessageSuiteV1*)host->fetchSuite(host->host,kOfxMessageSuite,1);persistentMessages=(OfxMessageSuiteV2*)host->fetchSuite(host->host,kOfxMessageSuite,2);require(fx&&prop&&params,"required OFX suites unavailable",kOfxStatErrMissingHostFeature);logStartup();return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionUnload)){if(engineContext){uint64_t a=0,p=0;adr_context_memory(engineContext,&a,&p);adrlog::info("unload: GPU memory peak "+std::to_string(p>>20)+" MiB");adr_context_destroy(engineContext);}else adrlog::info("unload");engineContext=nullptr;adrlog::close();return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionDescribe)){auto p=properties(e);ss(p,kOfxPropLabel,"Advanced Dirt Removal");ss(p,kOfxImageEffectPluginPropGrouping,"FabioColor");{auto status=prop->propSetString(p,kOfxPropPluginDescription,0,("Open-source temporal dirt and dust removal for film scans (Metal, CUDA, OpenCL or CPU). Works on the RGB values as delivered; review moving shots before delivery."+(adrlog::path().empty()?std::string():" Diagnostic log: "+adrlog::path())).c_str());if(status!=kOfxStatErrUnknown)check(status,"optional plugin description");}ss(p,kOfxImageEffectPropSupportedContexts,kOfxImageEffectContextFilter);ss(p,kOfxImageEffectPropSupportedPixelDepths,kOfxBitDepthFloat,0);ss(p,kOfxImageEffectPropSupportedPixelDepths,kOfxBitDepthHalf,1);ss(p,kOfxImageEffectPropSupportedPixelDepths,kOfxBitDepthShort,2);ss(p,kOfxImageEffectPluginRenderThreadSafety,kOfxImageEffectRenderInstanceSafe);si(p,kOfxImageEffectPluginPropHostFrameThreading,0);si(p,kOfxImageEffectPropSupportsTiles,0);si(p,kOfxImageEffectPropSupportsMultiResolution,0);si(p,kOfxImageEffectPropTemporalClipAccess,1);si(p,kOfxImageEffectPropSupportsMultipleClipPARs,0);si(p,kOfxImageEffectPropSupportsMultipleClipDepths,0);return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionDescribeInContext)){require(str(in,kOfxImageEffectPropContext)==kOfxImageEffectContextFilter,"Filter context required");for(auto name:{"Source","Output"}){OfxPropertySetHandle p;check(fx->clipDefine(e,name,&p),"define clip");ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGB,0);ss(p,kOfxImageEffectPropSupportedComponents,kOfxImageComponentRGBA,1);si(p,kOfxImageEffectPropSupportsTiles,0);if(!std::strcmp(name,"Source"))si(p,kOfxImageEffectPropTemporalClipAccess,1);}OfxParamSetHandle ps;check(fx->getParamSet(e,&ps),"parameters");for(auto&c:controls){OfxPropertySetHandle p;check(params->paramDefine(ps,c.boolean?kOfxParamTypeBoolean:c.choices?kOfxParamTypeChoice:c.integral?kOfxParamTypeInteger:kOfxParamTypeDouble,c.name,&p),"define control");ss(p,kOfxPropLabel,c.label);if(c.hint)ss(p,kOfxParamPropHint,c.hint);si(p,kOfxParamPropAnimates,0);if(c.boolean||c.integral){si(p,kOfxParamPropDefault,int(c.initial));if(c.choices){for(int index=0;index<=int(c.maximum);++index)ss(p,kOfxParamPropChoiceOption,c.choices[index],index);}else if(c.integral){for(auto key:{kOfxParamPropMin,kOfxParamPropDisplayMin})si(p,key,c.minimum);for(auto key:{kOfxParamPropMax,kOfxParamPropDisplayMax})si(p,key,int(c.maximum));}}else{for(auto v:{std::make_pair(kOfxParamPropDefault,c.initial),{kOfxParamPropMin,0.},{kOfxParamPropMax,c.maximum},{kOfxParamPropDisplayMin,0.},{kOfxParamPropDisplayMax,c.maximum}})check(prop->propSetDouble(p,v.first,0,v.second),"control range");}}
 return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionCreateInstance)){auto i=std::make_unique<Instance>();check(fx->clipGetHandle(e,"Source",&i->source,nullptr),"source clip");check(fx->clipGetHandle(e,"Output",&i->output,nullptr),"output clip");OfxParamSetHandle ps;check(fx->getParamSet(e,&ps),"parameters");for(auto&c:controls)check(params->paramGetHandle(ps,c.name,&i->parameters[c.name],nullptr),"control handle");check(prop->propSetPointer(properties(e),kOfxPropInstanceData,0,i.get()),"instance data");static int created=0;i->id=++created;trackInstance(e);adrlog::info(i->label()+" created");i.release();return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionDestroyInstance)){auto&i=instance(e);i.close();adrlog::info(i.label()+" destroyed");untrackInstance(e);delete &i;check(prop->propSetPointer(properties(e),kOfxPropInstanceData,0,nullptr),"clear instance");return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionInstanceChanged)){auto&i=instance(e);if(invalidatesHistory(in)){++i.generation;if(i.sequence){char*type=nullptr;char*name=nullptr;char*why=nullptr;if(in){prop->propGetString(in,kOfxPropType,0,&type);prop->propGetString(in,kOfxPropName,0,&name);prop->propGetString(in,kOfxPropChangeReason,0,&why);}adrlog::info(i.label()+" history reset: host reported a change ("+(type?type:"?")+" "+(name?name:"?")+", "+(why?why:"?")+")");}i.close();}return kOfxStatReplyDefault;}
 if(!std::strcmp(action,kOfxActionPurgeCaches)){auto&i=instance(e);adrlog::info(i.label()+" host asked to purge caches");i.purge();return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionGetFramesNeeded)){int t=frameTime(in);auto r=sourceRange(instance(e));require(t>=r[0]&&t<=r[1],"time outside source");double v[2]={double(r[0]),double(std::min(r[1],t+4))};/* t+4 is prefetched while the GPU renders t */return prop->propSetDoubleN(out,"OfxImageClipPropFrameRange_Source",2,v);}
 if(!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)||!std::strcmp(action,kOfxImageEffectActionGetRegionsOfInterest)){scaleOne(in);OfxRectD rod;check(fx->clipGetRegionOfDefinition(instance(e).source,number(in,kOfxPropTime),&rod),"source RoD");double v[]={rod.x1,rod.y1,rod.x2,rod.y2};return prop->propSetDoubleN(out,!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)?kOfxImageEffectPropRegionOfDefinition:"OfxImageClipPropRoI_Source",4,v);}
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
OfxPlugin plugin={kOfxImageEffectPluginApi,1,"com.fabiocolor.advanceddirtremoval",2,0,setHost,entry};
}
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
extern "C" EXPORT int OfxGetNumberOfPlugins(){return 1;}
extern "C" EXPORT OfxPlugin* OfxGetPlugin(int index){return index==0?&plugin:nullptr;}
