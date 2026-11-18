// OFX contract test: drives the plugin through a minimal in-process host.
#include "../src/adr_ofx.cpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <functional>
#include <thread>
#include <chrono>
#include <map>
#include <cstdarg>
#include <iostream>
#include <cassert>
#include <future>
#include <atomic>
#ifndef _WIN32
#include <dlfcn.h>
#endif
struct Properties {std::map<std::string,std::vector<int>> ints;std::map<std::string,std::vector<double>> doubles;std::map<std::string,std::vector<std::string>> strings;std::map<std::string,std::vector<void*>> pointers;};
Properties& P(OfxPropertySetHandle p){return *reinterpret_cast<Properties*>(p);}OfxPropertySetHandle H(Properties&p){return reinterpret_cast<OfxPropertySetHandle>(&p);}
#define SCALAR(NAME,TYPE,FIELD) \
OfxStatus set##NAME(OfxPropertySetHandle p,const char*k,int i,TYPE v){auto&a=P(p).FIELD[k];a.resize(std::max(a.size(),size_t(i+1)));a[i]=v;return 0;} \
OfxStatus get##NAME(OfxPropertySetHandle p,const char*k,int i,TYPE*v){auto it=P(p).FIELD.find(k);if(it==P(p).FIELD.end()||i<0||i>=int(it->second.size()))return kOfxStatErrUnknown;*v=it->second[i];return 0;}
SCALAR(Int,int,ints) SCALAR(Double,double,doubles) SCALAR(Pointer,void*,pointers)
std::function<void()> duringImage;
bool rejectLabel=false;int descriptorMessages=0,messageCalls=0;
bool rejectMetalSupport=true;int rejectedMetalSupport=0;

OfxPropertySetHandle strictPreferences=nullptr;int forbiddenPreferenceWrites=0;
OfxStatus setString(OfxPropertySetHandle p,const char*k,int i,const char*v){if(rejectMetalSupport&&(!std::strcmp(k,"OfxImageEffectPropMetalRenderSupported")||!std::strcmp(k,"OfxImageEffectPropCudaRenderSupported")||!std::strcmp(k,"OfxImageEffectPropCudaStreamSupported")||!std::strcmp(k,"OfxImageEffectPropOpenCLRenderSupported"))){++rejectedMetalSupport;return kOfxStatErrUnknown;}if(std::strcmp(k,kOfxPropPluginDescription)==0||(rejectLabel&&std::strcmp(k,kOfxPropLabel)==0))return kOfxStatErrUnknown;if(p==strictPreferences&&(std::string(k).find("OfxImageClipPropDepth_")==0||std::string(k).find("OfxImageClipPropComponents_")==0)){++forbiddenPreferenceWrites;return kOfxStatErrValue;}auto&a=P(p).strings[k];a.resize(std::max(a.size(),size_t(i+1)));a[i]=v;return 0;}
OfxStatus getString(OfxPropertySetHandle p,const char*k,int i,char**v){auto it=P(p).strings.find(k);if(it==P(p).strings.end()||i<0||i>=int(it->second.size()))return kOfxStatErrUnknown;*v=it->second[i].data();return 0;}
#define ARRAY(NAME,TYPE) \
OfxStatus set##NAME##N(OfxPropertySetHandle p,const char*k,int n,const TYPE*v){for(int i=0;i<n;++i)set##NAME(p,k,i,v[i]);return 0;} \
OfxStatus get##NAME##N(OfxPropertySetHandle p,const char*k,int n,TYPE*v){for(int i=0;i<n;++i){auto s=get##NAME(p,k,i,&v[i]);if(s)return s;}return 0;}
ARRAY(Int,int) ARRAY(Double,double)
struct Parameter {Properties props;bool choice=false;bool text=false;std::string type;size_t ordinal=0;};
struct TestImage {Properties props;std::vector<float> backing;};
struct Clip {Properties props;std::map<int,std::unique_ptr<TestImage>> images;bool output=false,mask=false;};
struct Effect {Properties props;std::map<std::string,Parameter> parameters;Clip source,output,mask,original;};
Effect& E(OfxImageEffectHandle e){return *reinterpret_cast<Effect*>(e);}
bool enforceFrameRange=false;double declaredFrameRange[2]{};int undeclaredFetches=0;
std::vector<int> fetched;int maskFetches=0;int acquired=0,released=0,abortCount=0;bool abortEnabled=false,slowImageOnce=false;int missing=-999;
OfxStatus effectProps(OfxImageEffectHandle e,OfxPropertySetHandle*p){*p=H(E(e).props);return 0;}
OfxStatus paramSet(OfxImageEffectHandle e,OfxParamSetHandle*p){*p=reinterpret_cast<OfxParamSetHandle>(&E(e).parameters);return 0;}
OfxStatus defineClip(OfxImageEffectHandle e,const char*n,OfxPropertySetHandle*p){*p=H(std::string(n)=="Source"?E(e).source.props:std::string(n)=="Mask"?E(e).mask.props:std::string(n)=="Original"?E(e).original.props:E(e).output.props);return 0;}
OfxStatus clipHandle(OfxImageEffectHandle e,const char*n,OfxImageClipHandle*c,OfxPropertySetHandle*p){auto&clip=std::string(n)=="Source"?E(e).source:std::string(n)=="Mask"?E(e).mask:std::string(n)=="Original"?E(e).original:E(e).output;*c=reinterpret_cast<OfxImageClipHandle>(&clip);if(p)*p=H(clip.props);return 0;}
OfxStatus clipProps(OfxImageClipHandle c,OfxPropertySetHandle*p){*p=H(reinterpret_cast<Clip*>(c)->props);return 0;}
OfxStatus getImage(OfxImageClipHandle c,double t,const OfxRectD*,OfxPropertySetHandle*p){auto&clip=*reinterpret_cast<Clip*>(c);if(clip.mask)++maskFetches;if(!clip.output&&enforceFrameRange&&(t<declaredFrameRange[0]||t>declaredFrameRange[1])){++undeclaredFetches;std::cerr<<"FAIL: source fetch "<<t<<" outside declared range "<<declaredFrameRange[0]<<".."<<declaredFrameRange[1]<<"\n";return kOfxStatFailed;}if(duringImage){auto callback=std::move(duringImage);duringImage=nullptr;callback();}if(slowImageOnce){slowImageOnce=false;std::this_thread::sleep_for(std::chrono::milliseconds(5300));}if(!clip.output)fetched.push_back(int(t));if(!clip.output&&int(t)==missing)return kOfxStatFailed;auto it=clip.images.find(int(t));if(it==clip.images.end())return kOfxStatFailed;*p=H(it->second->props);++acquired;return 0;}
OfxStatus releaseImage(OfxPropertySetHandle){++released;return 0;}
OfxRectD testRod{-7,11,-3,14};
OfxStatus rod(OfxImageClipHandle,double,OfxRectD*r){*r=testRod;return 0;}
int aborted(OfxImageEffectHandle){return abortEnabled&&++abortCount>12;}
OfxStatus defineParameter(OfxParamSetHandle s,const char*t,const char*n,OfxPropertySetHandle*p){auto&items=*reinterpret_cast<std::map<std::string,Parameter>*>(s);auto inserted=items.emplace(n,Parameter{});auto&v=inserted.first->second;if(inserted.second)v.ordinal=items.size()-1;v.type=t;v.choice=std::string(t)==kOfxParamTypeChoice||std::string(t)==kOfxParamTypeBoolean||std::string(t)==kOfxParamTypeInteger;v.text=std::string(t)==kOfxParamTypeString;*p=H(v.props);return 0;}
OfxStatus parameterHandle(OfxParamSetHandle s,const char*n,OfxParamHandle*h,OfxPropertySetHandle*p){auto&v=(*reinterpret_cast<std::map<std::string,Parameter>*>(s))[n];*h=reinterpret_cast<OfxParamHandle>(&v);if(p)*p=H(v.props);return 0;}
OfxStatus value(OfxParamHandle h,OfxTime t,...){auto&p=*reinterpret_cast<Parameter*>(h);va_list a;va_start(a,t);if(p.text){auto&s=p.props.strings[kOfxParamPropDefault];if(s.empty())s.push_back("");*va_arg(a,char**)=s[0].data();}else if(p.choice)*va_arg(a,int*)=p.props.ints[kOfxParamPropDefault][0];else *va_arg(a,double*)=p.props.doubles[kOfxParamPropDefault][0];va_end(a);return 0;}
OfxStatus currentValue(OfxParamHandle h,...){auto&p=*reinterpret_cast<Parameter*>(h);va_list a;va_start(a,h);if(p.text){auto&v=p.props.strings[kOfxParamPropDefault];if(v.empty())v.push_back("");*va_arg(a,char**)=v[0].data();}else if(p.choice)*va_arg(a,int*)=p.props.ints[kOfxParamPropDefault][0];else *va_arg(a,double*)=p.props.doubles[kOfxParamPropDefault][0];va_end(a);return kOfxStatOK;}
// UI writes must occur in host callbacks, even when a native key arrived first.
bool inPaintCallback=false;int paintUndoDepth=0,paintUndoBegins=0,paintWrongContext=0,paintRedraws=0;
std::function<void(OfxParamHandle)> duringParameterSet;
OfxStatus paintSetValue(OfxParamHandle h,...){auto&p=*reinterpret_cast<Parameter*>(h);va_list a;va_start(a,h);if(p.text)p.props.strings[kOfxParamPropDefault]={va_arg(a,const char*)};else if(p.choice)p.props.ints[kOfxParamPropDefault]={va_arg(a,int)};else p.props.doubles[kOfxParamPropDefault]={va_arg(a,double)};va_end(a);if(duringParameterSet)duringParameterSet(h);return kOfxStatOK;}
OfxStatus paintEditBegin(OfxParamSetHandle,const char*){if(!inPaintCallback){++paintWrongContext;return kOfxStatErrBadHandle;}++paintUndoDepth;++paintUndoBegins;return kOfxStatOK;}
OfxStatus paintEditEnd(OfxParamSetHandle){if(!inPaintCallback||paintUndoDepth<=0){++paintWrongContext;return kOfxStatErrBadHandle;}--paintUndoDepth;return kOfxStatOK;}
OfxStatus paintParameterProperties(OfxParamHandle h,OfxPropertySetHandle*p){*p=H(reinterpret_cast<Parameter*>(h)->props);return kOfxStatOK;}
OfxStatus paintInteractProperties(OfxInteractHandle h,OfxPropertySetHandle*p){*p=reinterpret_cast<OfxPropertySetHandle>(h);return kOfxStatOK;}
OfxStatus paintRedraw(OfxInteractHandle){++paintRedraws;return kOfxStatOK;}
std::string lastHostMessage,lastCadenceMessage,lastRenderActionMessage;
std::string progressiveBothMessage;
OfxStatus message(void*handle,const char*,const char*,const char*format,...){++messageCalls;if(!isLiveInstance(reinterpret_cast<OfxImageEffectHandle>(handle)))++descriptorMessages;char buffer[4096];va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);lastHostMessage=buffer;if(lastHostMessage.find("Fabiocolor cadence diagnostic:")!=std::string::npos)lastCadenceMessage=buffer;if(lastHostMessage.find("Fabiocolor render action diagnostic:")!=std::string::npos)lastRenderActionMessage=buffer;if(lastHostMessage.find("Fabiocolor progressive action quirk admitted:")!=std::string::npos)progressiveBothMessage=buffer;return kOfxStatOK;}
OfxStatus timeBounds(void*,double*a,double*b){*a=0;*b=24;return kOfxStatOK;}
OfxStatus timeCursor(void*,double*t){*t=12;return kOfxStatOK;}
OfxTimeLineSuiteV1 ts{};OfxMessageSuiteV1 ms{};
int progressStarts=0,progressEnds=0;bool progressCancel=false;
std::vector<double> progressValues;
OfxStatus progressStart(void*,const char*){++progressStarts;return kOfxStatOK;}
OfxStatus progressStartV2(void*,const char*,const char*){++progressStarts;return kOfxStatOK;}
OfxStatus progressUpdate(void*,double value){progressValues.push_back(value);return progressCancel?kOfxStatReplyNo:kOfxStatOK;}
OfxStatus progressEnd(void*){++progressEnds;return kOfxStatOK;}
OfxProgressSuiteV1 progressSuite{progressStart,progressUpdate,progressEnd};
OfxProgressSuiteV2 progressSuiteV2{progressStartV2,progressUpdate,progressEnd};
OfxPropertySuiteV1 ps{};OfxParameterSuiteV1 pas{};OfxImageEffectSuiteV1 es{};
const void* fetch(OfxPropertySetHandle,const char*s,int v){if(v!=1)return nullptr;if(std::strcmp(s,kOfxPropertySuite)==0)return &ps;if(std::strcmp(s,kOfxParameterSuite)==0)return &pas;if(std::strcmp(s,kOfxImageEffectSuite)==0)return &es;if(std::strcmp(s,kOfxTimeLineSuite)==0)return &ts;if(std::strcmp(s,kOfxMessageSuite)==0)return &ms;return nullptr;}
void environment(const char*k,const std::string&v){
#ifdef _WIN32
 _putenv_s(k,v.c_str());
#else
 setenv(k,v.c_str(),1);
#endif
}
void verify(bool okay,const char*m){if(!okay){std::cerr<<"FAIL: "<<m<<"\n";std::exit(1);}}
void configureSuites(){ts.getTime=timeCursor;ts.getTimeBounds=timeBounds;ms.message=message;ps.propSetInt=setInt;ps.propGetInt=getInt;ps.propSetDouble=setDouble;ps.propGetDouble=getDouble;ps.propSetString=setString;ps.propGetString=getString;ps.propSetPointer=setPointer;ps.propGetPointer=getPointer;ps.propSetIntN=setIntN;ps.propGetIntN=getIntN;ps.propSetDoubleN=setDoubleN;ps.propGetDoubleN=getDoubleN;pas.paramDefine=defineParameter;pas.paramGetHandle=parameterHandle;pas.paramGetValueAtTime=value;pas.paramGetValue=currentValue;es.getPropertySet=effectProps;es.getParamSet=paramSet;es.clipDefine=defineClip;es.clipGetHandle=clipHandle;es.clipGetPropertySet=clipProps;es.clipGetImage=getImage;es.clipReleaseImage=releaseImage;es.clipGetRegionOfDefinition=rod;es.abort=aborted;}
std::unique_ptr<TestImage> frameImage(const std::vector<float>&rgb,int frame,int w,int h,bool negative,bool output){
 auto im=std::make_unique<TestImage>();int stride=w*4+5;im->backing.resize(size_t(stride)*h,-1234);auto*data=im->backing.data()+(negative?stride*(h-1):0);auto p=H(im->props);
 setString(p,kOfxImageEffectPropPixelDepth,0,kOfxBitDepthFloat);setString(p,kOfxImageEffectPropComponents,0,kOfxImageComponentRGBA);setString(p,kOfxImageEffectPropPreMultiplication,0,kOfxImageUnPreMultiplied);setDouble(p,kOfxImagePropPixelAspectRatio,0,1.);setDouble(p,kOfxImageEffectPropRenderScale,0,1.);setDouble(p,kOfxImageEffectPropRenderScale,1,1.);setString(p,kOfxImagePropField,0,kOfxImageFieldNone);
 int b[]={-7,11,w-7,h+11};setIntN(p,kOfxImagePropBounds,4,b);setIntN(p,kOfxImagePropRegionOfDefinition,4,b);setInt(p,kOfxImagePropRowBytes,0,stride*4*(negative?-1:1));setPointer(p,kOfxImagePropData,0,data);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<4;++c)data[(negative?-1:1)*y*stride+x*4+c]=output?-999.f:c==3?float(x)/w:rgb[((size_t(frame)*h+(h-1-y))*w+x)*3+c];return im;
}
std::vector<float> readPixels(const char*path,size_t count){std::ifstream in(path,std::ios::binary);std::vector<float> out(count);in.read(reinterpret_cast<char*>(out.data()),std::streamsize(count*4));verify(bool(in),"source fixture read");return out;}
int main(int argc,char**argv){
 environment("ADR_EXPERIMENTAL_PREPARE_SHOT","1");
 // Unbuffered output and a terminate handler, so a crash still shows how far the test got and why.
 std::setvbuf(stdout,nullptr,_IONBF,0);
 std::set_terminate([]{try{if(auto x=std::current_exception())std::rethrow_exception(x);std::cerr<<"FAIL: terminate without exception\n";}catch(const std::exception&x){std::cerr<<"FAIL: uncaught exception: "<<x.what()<<"\n";}catch(...){std::cerr<<"FAIL: uncaught non-standard exception\n";}std::_Exit(3);});
 verify(argc==7,"usage: contract_host backend source.rgb32 target.rgb32 width height count");
 // Error reporting can inspect one instance while the host creates/destroys
 // another. Exercise the shared membership registry without host callbacks or
 // fictitious object lifetimes; these opaque dummy handles are never dereferenced.
 {
  std::array<int,65> tokens{};
  auto stable=reinterpret_cast<OfxImageEffectHandle>(&tokens.back());
  trackInstance(stable);
  std::atomic<bool> start{false},finished{false};std::atomic<unsigned> lookups{0};
  auto reader=std::async(std::launch::async,[&]{
   while(!start.load())std::this_thread::yield();
   unsigned n=0;
   do{
    verify(isLiveInstance(stable),"concurrent membership changes preserve other instance");
    (void)isLiveInstance(reinterpret_cast<OfxImageEffectHandle>(&tokens[n++%64]));
    ++lookups;
   }while(!finished.load());
  });
  auto writer=std::async(std::launch::async,[&]{
   start.store(true);while(!lookups.load())std::this_thread::yield();
   for(unsigned n=0;n<10000;++n){auto h=reinterpret_cast<OfxImageEffectHandle>(&tokens[n%64]);trackInstance(h);verify(isLiveInstance(h),"tracked instance is visible");untrackInstance(h);}
   finished.store(true);
  });
  if(writer.wait_for(std::chrono::seconds(2))!=std::future_status::ready||reader.wait_for(std::chrono::seconds(2))!=std::future_status::ready){std::cerr<<"FAIL: concurrent instance membership timed out\n";std::_Exit(1);}
  writer.get();reader.get();
  for(unsigned n=0;n<64;++n)verify(!isLiveInstance(reinterpret_cast<OfxImageEffectHandle>(&tokens[n])),"removed dummy instance absent");
  untrackInstance(stable);verify(!isLiveInstance(stable),"stable dummy instance removed");
  std::cout<<"concurrent instance membership: 10000 mutations, "<<lookups.load()<<" lookup passes\n";
 }

 // Parameter-write brackets can reenter while the lifecycle lock is held.
 // A deadline makes this a deterministic failure instead of a hung test.
 {
  std::lock_guard<std::mutex> lifecycle(mutex);
  auto job=std::async(std::launch::async,[]{
   return entry(kOfxActionBeginInstanceChanged,nullptr,nullptr,nullptr)==kOfxStatReplyDefault
       &&entry(kOfxActionEndInstanceChanged,nullptr,nullptr,nullptr)==kOfxStatReplyDefault;
  });
  if(job.wait_for(std::chrono::seconds(2))!=std::future_status::ready){std::cerr<<"FAIL: parameter-write bracket deadlocks during initialization\n";std::_Exit(1);}
  verify(job.get(),"reentrant parameter-write brackets");
 }
 environment("ADR_BACKEND",argv[1]);
 environment("ADR_RECOVERY_MODEL",(std::filesystem::path(__FILE__).parent_path().parent_path()/"resources"/"recovery-model.bin").u8string());
 // The diagnostic log is on by default; keep the test's log in a private folder and check it at the end.
 const auto logFolder=std::filesystem::temp_directory_path()/("adr-contract-log-"+std::to_string(adrlog::pid()));std::filesystem::remove_all(logFolder);environment("ADR_LOG",logFolder.u8string());int w=std::stoi(argv[4]),h=std::stoi(argv[5]),count=std::stoi(argv[6]);
 auto sourcePixels=readPixels(argv[2],size_t(count)*h*w*3),targetPixels=readPixels(argv[3],size_t(count)*h*w*3);
 configureSuites();Properties hostProperties;OfxHost provider{H(hostProperties),fetch};auto*plugin=OfxGetPlugin(0);plugin->setHost(&provider);verify(plugin->mainEntry(kOfxActionLoad,nullptr,nullptr,nullptr)==0,"load");
 Effect e;e.output.output=true;auto handle=reinterpret_cast<OfxImageEffectHandle>(&e);Properties in,out;auto ip=H(in);setString(ip,kOfxImageEffectPropContext,0,kOfxImageEffectContextFilter);
 rejectLabel=true;verify(plugin->mainEntry(kOfxActionDescribe,handle,nullptr,nullptr)==kOfxStatErrUnknown,"descriptor rejection returns status without instance message");verify(descriptorMessages==0,"no descriptor messages");rejectLabel=false;
 verify(plugin->mainEntry(kOfxActionDescribe,handle,nullptr,nullptr)==0,"describe with absent optional description");
#if ADR_HOST_METAL_TRANSPORT || ADR_HOST_GPU_TRANSPORT
 verify(rejectedMetalSupport>=2,"CPU-only host rejects optional GPU extensions without preventing discovery");
#endif
 rejectMetalSupport=false;verify(plugin->mainEntry(kOfxActionDescribe,handle,nullptr,nullptr)==kOfxStatOK,"GPU-capable descriptor also accepted");
 verify(plugin->mainEntry(kOfxImageEffectActionDescribeInContext,handle,ip,nullptr)==0,"context");verify(e.parameters.size()==78,"all controls, groups and pages exposed");verify(e.parameters.count("page_paint")==0&&e.parameters["paint"].props.ints[kOfxParamPropSecret]==std::vector<int>{1}&&e.parameters["paint_tool"].props.ints[kOfxParamPropSecret]==std::vector<int>{1},"viewer painting hidden outside Resolve");verify(e.props.pointers.count(kOfxImageEffectPluginPropOverlayInteractV1)==0,"no viewer overlay outside Resolve");for(auto pg:{"page_cleaning","page_recovery","page_view"})verify(e.parameters.count(pg)==1,"inspector page defined");for(auto g:{"detection","processing","safeguards"})verify(e.parameters.count(g)==1,"control group defined");verify(e.parameters["sensitivity"].props.strings[kOfxParamPropParent]==std::vector<std::string>{"detection"},"controls are grouped");verify(plugin->mainEntry(kOfxActionCreateInstance,handle,nullptr,nullptr)==0,"create");
 // Compact Resolve presentation preserves every existing value/type/range/choice.
 // The normal-host descriptor is an independent ordering of the same controls.
 {
  const auto savedHost=hostProperties;auto*savedInteracts=interacts;OfxInteractSuiteV1 suite{};interacts=&suite;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
  environment("ADR_EXPERIMENTAL_PREPARE_SHOT","0");
  Effect hidden;auto hiddenHandle=reinterpret_cast<OfxImageEffectHandle>(&hidden);
  verify(plugin->mainEntry(kOfxImageEffectActionDescribeInContext,hiddenHandle,ip,nullptr)==kOfxStatOK,"default preparation descriptor");
  verify(hidden.parameters["paint_source_kind"].props.strings[kOfxParamPropChoiceOption].size()==2&&hidden.parameters["prepare_shot"].props.ints[kOfxParamPropSecret]==std::vector<int>{1}&&hidden.parameters["prepare_folder"].props.ints[kOfxParamPropSecret]==std::vector<int>{1},"unqualified preparation is unavailable in the normal UI");
  environment("ADR_EXPERIMENTAL_PREPARE_SHOT","1");
  Effect compact;auto compactHandle=reinterpret_cast<OfxImageEffectHandle>(&compact);
  verify(plugin->mainEntry(kOfxImageEffectActionDescribeInContext,compactHandle,ip,nullptr)==kOfxStatOK,"compact Resolve descriptor");
  for(const auto&entry:e.parameters){const auto&before=entry.second;if(before.type==kOfxParamTypeGroup||before.type==kOfxParamTypePage)continue;
   auto found=compact.parameters.find(entry.first);verify(found!=compact.parameters.end(),"compact layout retains every parameter identity");const auto&after=found->second;
   verify(before.type==after.type,"compact layout retains parameter types");
   auto a=before.props,b=after.props;
   for(auto key:{kOfxPropLabel,kOfxParamPropHint,kOfxParamPropParent}){a.strings.erase(key);b.strings.erase(key);}
   a.ints.erase(kOfxParamPropSecret);b.ints.erase(kOfxParamPropSecret);
   if(entry.first=="paint"){verify(a.ints.at(kOfxParamPropDefault)==std::vector<int>{0}&&b.ints.at(kOfxParamPropDefault)==std::vector<int>{1},"painting defaults on in Resolve only");a.ints.erase(kOfxParamPropDefault);b.ints.erase(kOfxParamPropDefault);}
   verify(a.strings==b.strings&&a.ints==b.ints&&a.doubles==b.doubles,"compact layout retains defaults, ranges, choices and evaluation behavior");
  }
  verify(compact.parameters.at("paint").props.ints.at(kOfxParamPropEvaluateOnChange)==std::vector<int>{1},"enabling the built-in brush invalidates legacy-mask output immediately");
  const auto method=compact.parameters.at("mask_mode").ordinal;
  for(const auto&c:controls)verify(compact.parameters.at(c.name).ordinal<method,"every automatic cleaning option precedes manual method");
  for(auto name:{"encoding","auto_recovery","recovery_confidence","recovery_calculate","recovery_request","recovery_folder"})verify(compact.parameters.at(name).ordinal<method,"automatic setup and recovery options precede manual method");
  verify(method<compact.parameters.at("paint").ordinal,"manual method precedes brush controls");
  verify(compact.parameters.at("mask_mode").props.strings.at(kOfxParamPropParent)==std::vector<std::string>{"paint_group"},"method belongs to manual recovery section");
  for(auto name:{"stroke_details_group","paint_options_group","paint_source_group"})verify(compact.parameters.at(name).props.strings.at(kOfxParamPropParent)==std::vector<std::string>{"paint_group"},"manual options stay in manual recovery section");
  verify(!compact.parameters.at("encoding").props.strings.count(kOfxParamPropParent),"published input encoding remains top level");
  for(auto&c:controls)if(c.group)verify(compact.parameters.at(c.name).props.strings.at(kOfxParamPropParent)==std::vector<std::string>{c.group},"published automatic control grouping retained");
  for(auto&g:groups)verify(compact.parameters.at(g.name).props.strings.at(kOfxPropLabel)==std::vector<std::string>{g.label},"published automatic group labels retained");
  auto inGroup=[&](const char*name,const char*parent){return compact.parameters.at(name).props.strings.at(kOfxParamPropParent)==std::vector<std::string>{parent};};
  for(auto name:{"detection","processing","safeguards","auto_recovery_group","cleaning_defaults_group"})verify(inGroup(name,"automatic_cleaning_group"),"automatic settings share one section");
  for(auto name:{"automatic_cleaning_group","auto_recovery_group","processing","stroke_details_group","paint_options_group","paint_source_group","view_group","cleaning_defaults_group"})verify(compact.parameters.at(name).props.ints.at(kOfxParamPropGroupOpen)==std::vector<int>{0},"secondary groups start collapsed");
  verify(compact.parameters.at("paint_group").props.ints.at(kOfxParamPropGroupOpen)==std::vector<int>{1},"Paint opens by default");
  for(auto name:{"paint_tool","paint_shape","paint_size","paint_softness","paint_undo","paint_redo"})verify(inGroup(name,"paint_group"),"everyday paint controls are directly accessible");
  for(auto name:{"paint_stroke_info","paint_resume","paint_hold"})verify(inGroup(name,"stroke_details_group"),"stroke management stays together");
  verify(inGroup("paint_opacity","paint_options_group"),"opacity is available in Brush options");
  const auto&outlines=compact.parameters.at("paint_outlines").props;
  verify(outlines.ints.at(kOfxParamPropDefault)==std::vector<int>{0}&&outlines.strings.at(kOfxParamPropParent)==std::vector<std::string>{"paint_group"},"stroke review is off by default and available beside the tool");
  const auto&children=compact.parameters.at("page_paint").props.strings.at(kOfxParamPropPageChild);for(auto name:{"paint_undo","paint_redo"})verify(std::find(children.begin(),children.end(),name)!=children.end(),"page-respecting hosts retain paint history buttons");
  hostProperties=savedHost;interacts=savedInteracts;std::cout<<"compact UI parameter contract and group defaults: passed\n";
 }
 // Every keyboard event belongs to Resolve, even during a paint gesture.
 {
  const auto savedParameters=e.parameters;
  pas.paramSetValue=paintSetValue;pas.paramEditBegin=paintEditBegin;pas.paramEditEnd=paintEditEnd;pas.paramGetPropertySet=paintParameterProperties;
  OfxInteractSuiteV1 testInteract{};testInteract.interactGetPropertySet=paintInteractProperties;testInteract.interactRedraw=paintRedraw;
  auto* savedInteract=interacts;interacts=&testInteract;
  paint_ui::Overlay o;o.effect=handle;o.hasCursor=true;Properties overlayProperties;o.handle=reinterpret_cast<OfxInteractHandle>(&overlayProperties);setPointer(H(overlayProperties),kOfxPropInstanceData,0,&o);
  setInt(H(e.parameters.at("paint").props),kOfxParamPropDefault,0,1);
  setDouble(H(e.parameters.at("paint_softness").props),kOfxParamPropDefault,0,.5);
  const auto beforeKeys=e.parameters;const int undoBeforeKeys=paintUndoBegins;
  Properties keyProperties;
  for(int phase=0;phase<4;++phase){
   o.drawing=phase==1;o.moving=phase==2;o.adjusting=phase==3;
   for(int k:std::initializer_list<int>{'b','e','v','c','q','r','i','h','x','1','2','3','4','[',']','-','=','+',',','.','z',0xffff,0xff08,0xff09,0xff51,0xff53,0xffe1,0xffe3,0xffe7,0xffe9}){
    setInt(H(keyProperties),"kOfxPropKeySym",0,k);
    for(auto event:{kOfxInteractActionKeyDown,kOfxInteractActionKeyRepeat,kOfxInteractActionKeyUp}){
     inPaintCallback=true;auto status=paint_ui::overlay(event,o.handle,H(keyProperties),nullptr);inPaintCallback=false;
     verify(status==kOfxStatReplyDefault,"paint overlay leaves every keyboard command with Resolve");
    }
   }
   verify(o.drawing==(phase==1)&&o.moving==(phase==2)&&o.adjusting==(phase==3),"keyboard events do not finish or alter pointer gestures");
  }
  for(const auto&entry:beforeKeys){const auto&now=e.parameters.at(entry.first).props;const auto&before=entry.second.props;verify(now.ints==before.ints&&now.doubles==before.doubles&&now.strings==before.strings,"keyboard events leave all paint parameters and stroke data unchanged");}
  verify(paintUndoBegins==undoBeforeKeys&&paintUndoDepth==0&&paintRedraws==0,"keyboard events create no paint history or redraw");
  o.drawing=o.moving=o.adjusting=false;o.modifiers=0;
  std::cout<<"Resolve keyboard pass-through during idle and all pointer gestures: passed\n";
  // Resolve Edit restores brush controls and the hidden document together.
  // A stale control must never rewrite that restored document or clear Redo.
  paint::Document restored;restored.width=1920;restored.height=1080;restored.nextId=2;
  paint::Stroke stroke;stroke.id=1;stroke.diameter=40;stroke.points.push_back({.5,.5,1});restored.strokes.push_back(stroke);
  const auto encoded=paint::encode(restored);
  Properties notification;setString(H(notification),kOfxPropChangeReason,0,kOfxChangeUserEdited);
  for(bool documentFirst:{false,true}){
   const int undoBefore=paintUndoBegins;
   paint_ui::setInt(handle,"paint_stroke",1);paint_ui::store(handle,restored);
   paint_ui::beginChanges(handle);
   auto notify=[&](const char*name){setString(H(notification),kOfxPropName,0,name);paint_ui::changed(handle,H(notification));};
   if(documentFirst)notify("paint_data");
   paint_ui::setDouble(handle,"paint_size",80);notify("paint_size");
   if(!documentFirst)notify("paint_data");
   verify(paint_ui::getText(handle,"paint_data")==encoded,"grouped restore never rewrites strokes between notifications");
   paint_ui::endChanges(handle);
   verify(paint_ui::getText(handle,"paint_data")==encoded&&paint_ui::getDouble(handle,"paint_size")==40,"stored document wins in either host restoration order");
   verify(paintUndoBegins==undoBefore,"restoring a host undo group creates no new undo entry");
  }
  paint_ui::beginChanges(handle);paint_ui::setDouble(handle,"paint_size",90);setString(H(notification),kOfxPropName,0,"paint_size");paint_ui::changed(handle,H(notification));paint_ui::endChanges(handle);
  verify(paint_ui::document(handle).strokes[0].diameter==90,"a grouped user control edit still updates the selected stroke");
  verify(paintWrongContext==0&&paintUndoDepth==0,"grouped controls use the host's existing undo transaction");
  std::cout<<"grouped parameter restoration and brush edits: passed\n";
  paint_ui::histories.erase(handle);o.hasCursor=true;inPaintCallback=true;
  paint_ui::command(handle,paint_ui::Action::SizeUp);const auto edited=paint_ui::getText(handle,"paint_data");
  paint_ui::command(handle,paint_ui::Action::Delete);const auto deleted=paint_ui::getText(handle,"paint_data");
  verify(edited!=deleted&&paint_ui::canPaintUndo(handle,false),"paint edits have their own undo history");
  setString(H(notification),kOfxPropName,0,"paint_undo");paint_ui::changed(handle,H(notification));
  verify(paint_ui::getText(handle,"paint_data")==edited&&paint_ui::getInt(handle,"paint_stroke")==1,"undo delete restores exact properties and selection");
  setString(H(notification),kOfxPropName,0,"paint_redo");paint_ui::changed(handle,H(notification));
  verify(paint_ui::getText(handle,"paint_data")==deleted,"redo delete restores exact document");
  paint_ui::restorePaint(handle,false);paint_ui::command(handle,paint_ui::Action::SoftDown);
  verify(!paint_ui::canPaintUndo(handle,true),"new paint edit clears the redo branch");
  for(int n=0;n<80;++n)paint_ui::command(handle,n%2?paint_ui::Action::SizeDown:paint_ui::Action::SizeUp);
  verify(paint_ui::histories.at(handle).undo.size()<=64,"paint history count is bounded");
  paint_ui::histories.erase(handle);
  const auto beforeStroke=paint_ui::getText(handle,"paint_data");
  auto painted=paint_ui::document(handle);
  auto newStroke=painted.strokes.back();newStroke.id=painted.nextId++;
  painted.strokes.push_back(newStroke);
  auto notifyStrokeWrite=[&]{
   Properties change;setString(H(change),kOfxPropName,0,"paint_data");setString(H(change),kOfxPropChangeReason,0,kOfxChangeUserEdited);
   paint_ui::beginChanges(handle);paint_ui::changed(handle,H(change));paint_ui::endChanges(handle);
  };
  const auto documentHandle=paint_ui::parameter(handle,"paint_data");
  duringParameterSet=[&](OfxParamHandle changed){if(changed==documentHandle)notifyStrokeWrite();};
  const auto preview=paint::encode(painted);
  {
   paint_ui::Undo undo(handle,"Paint stroke");
   paint_ui::store(handle,painted);
   painted.strokes.back().points.push_back({.55,.5,1});
   paint_ui::store(handle,painted,true);
  }
  duringParameterSet=nullptr;
  paint_ui::setText(handle,"paint_data",preview);
  notifyStrokeWrite();
  verify(paint_ui::restorePaint(handle,false)&&paint_ui::getText(handle,"paint_data")==beforeStroke,"host replay of the last preview preserves the stroke undo entry");
  notifyStrokeWrite();
  verify(paint_ui::restorePaint(handle,true)&&paint_ui::getText(handle,"paint_data")==preview,"redo restores the exact stroke accepted by the host");
  paint_ui::setText(handle,"paint_data",beforeStroke);notifyStrokeWrite();
  verify(!paint_ui::canPaintUndo(handle,false)&&!paint_ui::canPaintUndo(handle,true),"a different host-restored stroke document invalidates local history");
  inPaintCallback=false;paint_ui::histories.erase(handle);
  std::cout<<"viewer paint undo/redo, property restoration and bounds: passed\n";
  // Previous/Next stroke: frame order, starting from the playhead (12 in this
  // host), and the playhead moves to a stroke that is not on the current frame.
  {
   static std::vector<double> moves;moves.clear();ts.gotoTime=[](void*,double t)->OfxStatus{moves.push_back(t);return kOfxStatOK;};
   paint::Document strokes;strokes.width=1920;strokes.height=1080;strokes.nextId=4;
   for(int f:{30,10,20}){paint::Stroke s;s.id=int(strokes.strokes.size())+1;s.first=s.last=f;s.points.push_back({.5,.5,1});strokes.strokes.push_back(s);}
   paint_ui::store(handle,strokes);paint_ui::setInt(handle,"paint_stroke",0);paint_ui::setInt(handle,"paint_tool",paint_ui::ToolPaint);inPaintCallback=true;
   auto picked=[&]{return paint_ui::document(handle).strokes.at(size_t(paint_ui::getInt(handle,"paint_stroke")-1)).first;};
   paint_ui::command(handle,paint_ui::Action::Next);verify(picked()==20&&moves==std::vector<double>{20},"Next from the playhead selects the next stroke in frame order and goes to it");
   paint_ui::command(handle,paint_ui::Action::Next);verify(picked()==30&&moves.back()==30,"Next follows frame order, not paint order");
   paint_ui::command(handle,paint_ui::Action::Next);verify(picked()==10&&moves.back()==10,"Next wraps to the first stroke");
   paint_ui::command(handle,paint_ui::Action::Previous);verify(picked()==30&&moves.back()==30,"Previous wraps to the last stroke");
   verify(paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolPaint,"stroke browsing keeps Paint active");
   const auto reviewed=paint_ui::getText(handle,"paint_data");
   auto notifyPaint=[&](const char*name){setString(H(notification),kOfxPropName,0,name);paint_ui::changed(handle,H(notification));};
   paint_ui::setInt(handle,"paint_outlines",1);notifyPaint("paint_outlines");
   verify(paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolSelect,"Show strokes activates Select");
   paint_ui::command(handle,paint_ui::Action::Previous);
   verify(picked()==20&&paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolSelect,"stroke browsing keeps explicit Select active");
   paint_ui::setDouble(handle,"difference_threshold",7.);
   paint_ui::setInt(handle,"paint_outlines",0);notifyPaint("paint_outlines");
   verify(paint_ui::getInt(handle,"paint")==1&&paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolPaint&&paint_ui::getInt(handle,"paint_stroke")==0,"hiding strokes immediately restores Paint and clears selection");
   verify(paint_ui::getText(handle,"paint_data")==reviewed&&paint_ui::getDouble(handle,"difference_threshold")==7.,"Show strokes preserves the stroke document and viewer threshold");
   paint_ui::setInt(handle,"paint_tool",paint_ui::ToolSelect);notifyPaint("paint_tool");
   verify(paint_ui::getInt(handle,"paint_outlines")==1,"choosing Select also shows stroke outlines");
   paint_ui::setInt(handle,"paint",0);notifyPaint("paint_resume");
   verify(paint_ui::getInt(handle,"paint")==1&&paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolPaint&&paint_ui::getInt(handle,"paint_outlines")==0,"Resume painting re-enables the brush and hides outlines");
   verify(paint_ui::getText(handle,"paint_data")==reviewed&&paint_ui::getDouble(handle,"difference_threshold")==7.,"resuming preserves the stroke document and viewer threshold");
   paint_ui::setInt(handle,"paint_tool",paint_ui::ToolSelect);paint_ui::setInt(handle,"paint_stroke",2);
   paint_ui::refresh(handle,true);
   verify(paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolPaint&&paint_ui::getInt(handle,"paint_stroke")==0&&paint_ui::getText(handle,"paint_data")==reviewed,"saved Select with hidden outlines opens in Paint and preserves the strokes");
   paint_ui::setInt(handle,"paint_stroke",0);paint_ui::command(handle,paint_ui::Action::Previous);verify(picked()==10&&moves.back()==10,"Previous from the playhead selects the stroke before it");
   paint::Document here=strokes;here.strokes[0].first=0;here.strokes[0].last=40;paint_ui::store(handle,here);paint_ui::setInt(handle,"paint_stroke",0);const size_t before=moves.size();
   paint_ui::command(handle,paint_ui::Action::Next);verify(moves.size()==before+1&&moves.back()==0,"held stroke navigation goes to its first frame even when visible now");
   auto savedFallback=paint_ui::fallbackSeek;auto savedHost=hostProperties;auto*savedInteracts=interacts;OfxInteractSuiteV1 interactive{};interacts=&interactive;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
   ts.getTime=[](void*,double*)->OfxStatus{return kOfxStatErrUnsupported;};ts.gotoTime=[](void*,double)->OfxStatus{return kOfxStatErrUnsupported;};
   static std::vector<double> fallback;fallback.clear();paint_ui::fallbackSeek=[](void*,double now,double target,int,int)->bool{fallback={now,target};return true;};
   const double fallbackRange[]={0,40};setDoubleN(H(e.source.props),kOfxImageEffectPropFrameRange,2,fallbackRange);
   paint_ui::cursorTimes[handle]=12;paint_ui::store(handle,strokes);paint_ui::setInt(handle,"paint_stroke",0);
   paint_ui::command(handle,paint_ui::Action::Next);verify(fallback==std::vector<double>({12,20}),"unsupported OFX timeline uses observed viewer time and the Resolve navigation fallback");
   paint_ui::setInt(handle,"paint",0);paint_ui::command(handle,paint_ui::Action::Mode);verify(paint_ui::getInt(handle,"paint")==1,"manual mode command enables painting");
   paint_ui::setInt(handle,"paint",0);paint_ui::applyChanged(handle,"mask_mode",kOfxChangeUserEdited);verify(paint_ui::getInt(handle,"paint")==1&&paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolPaint&&paint_ui::getInt(handle,"paint_stroke")==0,"choosing Recovery resumes painting instead of stroke review");
   paint_ui::setInt(handle,"paint",0);paint_ui::applyChanged(handle,"mask_mode",kOfxChangePluginEdited);verify(paint_ui::getInt(handle,"paint")==0,"loading settings preserves an explicitly disabled brush");
   paint_ui::setText(handle,"paint_data","");paint_ui::setInt(handle,"paint",1);setInt(H(e.mask.props),kOfxImageClipPropConnected,0,1);paint_ui::initializePainting(handle);
   verify(paint_ui::getInt(handle,"paint")==0,"old connected-mask project keeps its compatibility route on initialization");
   for(int sourceKind:{1,2})for(int enabled:{0,1}){
    paint_ui::setInt(handle,"paint_source_kind",sourceKind);paint_ui::setInt(handle,"paint",enabled);paint_ui::initializePainting(handle);
    verify(paint_ui::getInt(handle,"paint")==enabled,"explicit original/prepared source preserves saved paint on/off with a connected Mask input");
   }
   paint_ui::setInt(handle,"paint_source_kind",0);setInt(H(e.mask.props),kOfxImageClipPropConnected,0,0);
   paint_ui::fallbackSeek=savedFallback;hostProperties=savedHost;interacts=savedInteracts;ts.getTime=timeCursor;ts.gotoTime=nullptr;inPaintCallback=false;paint_ui::histories.erase(handle);
   std::cout<<"stroke navigation in frame order moves the playhead: passed\n";
  }

  interacts=savedInteract;pas.paramSetValue=nullptr;pas.paramEditBegin=nullptr;pas.paramEditEnd=nullptr;pas.paramGetPropertySet=nullptr;
  for(auto&saved:savedParameters)e.parameters.at(saved.first)=saved.second;
  std::cout<<"native key scheduling and delayed mouse-up: passed\n";
 }
 // Separate cleaning configurations: real callback reentry, host change
 // batches, saved-project recreation and Undo/Redo ordering are all exercised.
 {
  const auto savedParameters=e.parameters;const auto savedHost=hostProperties;const auto savedState=instance(handle).cleaningProfile;
  auto*savedInteracts=interacts;OfxInteractSuiteV1 suite{};interacts=&suite;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
  pas.paramSetValue=paintSetValue;pas.paramGetPropertySet=paintParameterProperties;
  Effect*editing=&e;
  auto notify=[&](const std::string&name){Properties change;setString(H(change),kOfxPropType,0,kOfxTypeParameter);setString(H(change),kOfxPropName,0,name.c_str());setString(H(change),kOfxPropChangeReason,0,kOfxChangeUserEdited);setDouble(H(change),kOfxPropTime,0,0);const auto result=plugin->mainEntry(kOfxActionInstanceChanged,reinterpret_cast<OfxImageEffectHandle>(editing),H(change),nullptr);verify(result==kOfxStatOK||result==kOfxStatReplyDefault,"configuration parameter callback");};
  duringParameterSet=[&](OfxParamHandle h){for(auto&item:editing->parameters)if(reinterpret_cast<OfxParamHandle>(&item.second)==h){notify(item.first);return;}};
  cleaning_ui::initialize(handle);
  auto defaults=cleaning_ui::defaults();
  auto index=[&](const char*name){for(size_t n=0;n<cleaning_ui::count;++n)if(!std::strcmp(controls[n+1].name,name))return n;verify(false,"known profile control");return size_t(0);};
  verify(defaults.values[0][index("sensitivity")]==.75&&defaults.values[1][index("sensitivity")]==1&&defaults.values[1][index("extra_sensitivity")]==1&&defaults.values[1][index("analysis_detail")]==1&&defaults.values[1][index("repair_amount")]==1.5&&defaults.values[1][index("motion_guard")]==0,"operator-selected stronger Spanish defaults");
  verify(cleaning_ui::decode(cleaning_ui::encode(defaults)).values==defaults.values,"mode document roundtrip");
  for(const auto&text:std::vector<std::string>{"ADRCP2 0 16","ADRCP1 0 16","ADRCP1 2 16",cleaning_ui::encode(defaults)+" trailing"}){bool rejected=false;try{cleaning_ui::decode(text);}catch(const Error&){rejected=true;}verify(rejected,"invalid saved configurations rejected");}
  {auto bad=defaults;bad.values[1][index("analyse")]=.5;bool rejected=false;try{cleaning_ui::encode(bad);}catch(const Error&){rejected=true;}verify(rejected,"fractional choice rejected before any control write");}
  verify(e.parameters.at("cleaning_profiles").props.ints.at(kOfxParamPropSecret)==std::vector<int>{1}&&e.parameters.at("cleaning_profiles").props.ints.at(kOfxParamPropEvaluateOnChange)==std::vector<int>{0},"profile storage adds no visible controls or render invalidation");
  paint_ui::setInt(handle,"enabled",0);paint_ui::setInt(handle,"encoding",1);paint_ui::setInt(handle,"view",2);paint_ui::setInt(handle,"auto_recovery",1);paint_ui::setDouble(handle,"paint_size",137);
  paint_ui::setDouble(handle,"sensitivity",.62);paint_ui::setDouble(handle,"repair_amount",.9);paint_ui::setDouble(handle,"motion_guard",.41);
  const auto automatic=cleaning_ui::current(handle);
  verify(plugin->mainEntry(kOfxActionBeginInstanceChanged,handle,nullptr,nullptr)==kOfxStatReplyDefault,"begin genuine mode switch");
  paint_ui::setInt(handle,"mask_mode",1);
  verify(cleaning_ui::current(handle)==automatic,"mode callback waits for the complete host batch");
  verify(plugin->mainEntry(kOfxActionEndInstanceChanged,handle,nullptr,nullptr)==kOfxStatReplyDefault,"finish genuine mode switch");
  verify(cleaning_ui::current(handle)==defaults.values[1],"first Spanish switch restores stronger settings");
  verify(paint_ui::getInt(handle,"enabled")==0&&paint_ui::getInt(handle,"encoding")==1&&paint_ui::getInt(handle,"view")==2&&paint_ui::getInt(handle,"auto_recovery")==1&&paint_ui::getDouble(handle,"paint_size")==137,"encoding, bypass, view, recovery preference and brush stay shared");
  paint_ui::setDouble(handle,"sensitivity",.93);paint_ui::setDouble(handle,"analysis_detail",.81);paint_ui::setDouble(handle,"motion_guard",.05);
  const auto spanish=cleaning_ui::current(handle);const auto spanishSnapshot=e.parameters;
  paint_ui::setInt(handle,"mask_mode",0);verify(cleaning_ui::current(handle)==automatic,"automatic custom settings restored exactly");const auto automaticSnapshot=e.parameters;
  paint_ui::setInt(handle,"mask_mode",1);verify(cleaning_ui::current(handle)==spanish,"Spanish custom settings restored exactly");
  auto restore=[&](const auto&snapshot,bool bankFirst){
   plugin->mainEntry(kOfxActionBeginInstanceChanged,handle,nullptr,nullptr);
   auto one=[&](const char*name){e.parameters.at(name).props=snapshot.at(name).props;notify(name);};
   if(bankFirst)one("cleaning_profiles");one("mask_mode");for(const auto&c:controls)one(c.name);if(!bankFirst)one("cleaning_profiles");
   plugin->mainEntry(kOfxActionEndInstanceChanged,handle,nullptr,nullptr);
  };
  restore(automaticSnapshot,false);verify(cleaning_ui::current(handle)==automatic&&paint_ui::getText(handle,"cleaning_profiles")==automaticSnapshot.at("cleaning_profiles").props.strings.at(kOfxParamPropDefault)[0],"Undo restores controls and bank with method first");
  restore(spanishSnapshot,true);verify(cleaning_ui::current(handle)==spanish&&paint_ui::getText(handle,"cleaning_profiles")==spanishSnapshot.at("cleaning_profiles").props.strings.at(kOfxParamPropDefault)[0],"Redo restores controls and bank with bank first");
  Effect reopened;reopened.parameters=e.parameters;editing=&reopened;auto rh=reinterpret_cast<OfxImageEffectHandle>(&reopened);
  verify(plugin->mainEntry(kOfxActionCreateInstance,rh,nullptr,nullptr)==kOfxStatOK,"reopen profile instance");
  verify(cleaning_ui::current(rh)==spanish,"reopen preserves the active edited Spanish values");
  paint_ui::setInt(rh,"mask_mode",0);verify(cleaning_ui::current(rh)==automatic,"reopen retains inactive automatic settings");
  paint_ui::setInt(rh,"mask_mode",1);verify(cleaning_ui::current(rh)==spanish,"reopen retains edited Spanish settings");
  verify(plugin->mainEntry(kOfxActionDestroyInstance,rh,nullptr,nullptr)==kOfxStatOK,"destroy reopened profile instance");
  Effect legacy;legacy.parameters=spanishSnapshot;legacy.parameters.at("cleaning_profiles").props.strings[kOfxParamPropDefault]={""};editing=&legacy;auto lh=reinterpret_cast<OfxImageEffectHandle>(&legacy);
  verify(plugin->mainEntry(kOfxActionCreateInstance,lh,nullptr,nullptr)==kOfxStatOK,"load pre-profile Spanish project");
  verify(cleaning_ui::current(lh)==spanish,"old Spanish project is not reset on load");
  paint_ui::setInt(lh,"mask_mode",0);verify(cleaning_ui::current(lh)==defaults.values[0],"old Spanish project gets standard inactive automatic settings");
  paint_ui::setInt(lh,"mask_mode",1);verify(cleaning_ui::current(lh)==spanish,"old Spanish custom values survive first switch");
  verify(plugin->mainEntry(kOfxActionDestroyInstance,lh,nullptr,nullptr)==kOfxStatOK,"destroy legacy profile instance");
  // User defaults are explicit, separate per mode and local to the user.
  // All file operations use this owned fixture directory, never AppData/HOME.
  const auto previousDefaults=std::getenv("ADR_USER_DEFAULTS_DIR");const std::string savedDefaults=previousDefaults?previousDefaults:"";
  const auto storage=logFolder/"defaults-storage"/std::filesystem::u8path(u8"pr\u00e9sets-\u7528\u6237");
  auto bytes=[](const auto&path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());};
  user_defaults::save(storage,0,automatic);user_defaults::save(storage,1,spanish);
  cleaning_ui::Values loaded{};verify(user_defaults::load(storage,0,loaded)&&loaded==automatic,"automatic user default roundtrip");
  verify(user_defaults::load(storage,1,loaded)&&loaded==spanish,"Spanish user default roundtrip");
  const auto spanishBytes=bytes(user_defaults::file(storage,1));auto automatic2=automatic;automatic2[index("sensitivity")]=.64;
  user_defaults::save(storage,0,automatic2);verify(user_defaults::load(storage,0,loaded)&&loaded==automatic2&&bytes(user_defaults::file(storage,1))==spanishBytes,"replacement is complete and leaves other mode untouched");
  const auto original=bytes(user_defaults::file(storage,0));auto invalid=automatic2;invalid[index("sensitivity")]=2;
  bool rejected=false;try{user_defaults::save(storage,0,invalid);}catch(const Error&){rejected=true;}verify(rejected&&bytes(user_defaults::file(storage,0))==original,"invalid defaults cannot replace a valid file");
  const auto blocked=storage.parent_path()/"blocked-root";{std::ofstream out(blocked);out<<"owned blocker";}
  rejected=false;try{user_defaults::save(blocked,0,automatic);}catch(const Error&){rejected=true;}verify(rejected&&bytes(user_defaults::file(storage,0))==original,"write failure keeps saved defaults");
  const auto rejectedRoot=storage/"reject-replacement";std::filesystem::create_directories(user_defaults::file(rejectedRoot,0));
  rejected=false;try{user_defaults::save(rejectedRoot,0,automatic);}catch(const Error&){rejected=true;}verify(rejected&&std::filesystem::is_directory(user_defaults::file(rejectedRoot,0)),"replacement failure keeps its target and cleans the temporary file");
  for(const auto&entry:std::filesystem::recursive_directory_iterator(storage))verify(entry.path().filename().u8string().find(".tmp-")==std::string::npos,"successful/failed saves leave no temporary files");
  const auto uiRoot=logFolder/"defaults-ui";environment("ADR_USER_DEFAULTS_DIR",uiRoot.u8string());editing=&e;
  pas.paramEditBegin=paintEditBegin;pas.paramEditEnd=paintEditEnd;inPaintCallback=true;
  auto action=[&](const char*name){notify(name);};
  const auto beforeMissing=cleaning_ui::current(handle);action("cleaning_default_load");
  verify(cleaning_ui::current(handle)==beforeMissing&&!std::filesystem::exists(uiRoot),"loading absent defaults changes no settings and creates no folder");
  action("cleaning_default_save");verify(user_defaults::load(uiRoot,1,loaded)&&loaded==spanish,"Save button stores selected Spanish mode");
  const auto savedSpanish=bytes(user_defaults::file(uiRoot,1));action("cleaning_default_factory");
  verify(cleaning_ui::current(handle)==defaults.values[1]&&bytes(user_defaults::file(uiRoot,1))==savedSpanish,"factory button resets current mode without overwriting user defaults");
  action("cleaning_default_load");verify(cleaning_ui::current(handle)==spanish,"Load button restores saved Spanish default");
  paint_ui::setInt(handle,"mask_mode",0);verify(cleaning_ui::current(handle)==automatic,"loading Spanish does not replace inactive automatic edits");
  paint_ui::setDouble(handle,"sensitivity",.64);action("cleaning_default_save");
  verify(user_defaults::load(uiRoot,0,loaded)&&loaded==automatic2&&bytes(user_defaults::file(uiRoot,1))==savedSpanish,"Save button keeps separate automatic and Spanish files");
  action("cleaning_default_factory");verify(cleaning_ui::current(handle)==defaults.values[0],"factory button selects automatic factory values");
  action("cleaning_default_load");verify(cleaning_ui::current(handle)==automatic2,"automatic user default loads explicitly");
  verify(paint_ui::getInt(handle,"encoding")==1&&paint_ui::getInt(handle,"enabled")==0&&paint_ui::getInt(handle,"view")==2&&paint_ui::getDouble(handle,"paint_size")==137&&paint_ui::getInt(handle,"auto_recovery")==1,"user defaults exclude shared/source/brush controls");
  Effect fresh;fresh.parameters=savedParameters;editing=&fresh;const auto fh=reinterpret_cast<OfxImageEffectHandle>(&fresh);
  verify(plugin->mainEntry(kOfxActionCreateInstance,fh,nullptr,nullptr)==kOfxStatOK&&cleaning_ui::current(fh)==defaults.values[0],"new node does not automatically apply user defaults");
  action("cleaning_default_load");verify(cleaning_ui::current(fh)==automatic2,"new node can explicitly load automatic default");
  paint_ui::setInt(fh,"mask_mode",1);verify(cleaning_ui::current(fh)==defaults.values[1],"first Spanish switch retains factory preset until explicit load");
  action("cleaning_default_load");verify(cleaning_ui::current(fh)==spanish,"new node explicitly loads Spanish user default");
  const auto portableValues=fresh.parameters;verify(plugin->mainEntry(kOfxActionDestroyInstance,fh,nullptr,nullptr)==kOfxStatOK,"destroy defaults target");
  environment("ADR_USER_DEFAULTS_DIR",(logFolder/"unavailable-defaults").u8string());
  Effect portable;portable.parameters=portableValues;editing=&portable;const auto ph=reinterpret_cast<OfxImageEffectHandle>(&portable);
  verify(plugin->mainEntry(kOfxActionCreateInstance,ph,nullptr,nullptr)==kOfxStatOK&&cleaning_ui::current(ph)==spanish,"saved project retains values without the user-default files");
  paint_ui::setInt(ph,"mask_mode",0);verify(cleaning_ui::current(ph)==automatic2&&!std::filesystem::exists(logFolder/"unavailable-defaults"),"project retains inactive settings without reading/creating user defaults");
  verify(plugin->mainEntry(kOfxActionDestroyInstance,ph,nullptr,nullptr)==kOfxStatOK,"destroy portable project instance");
  environment("ADR_USER_DEFAULTS_DIR",uiRoot.u8string());editing=&e;const auto beforeError=cleaning_ui::current(handle);
  {std::ofstream out(user_defaults::file(uiRoot,0));auto wrong=defaults;wrong.active=1;out<<cleaning_ui::encode(wrong);}
  const int errorsBefore=messageCalls;action("cleaning_default_load");
  verify(messageCalls==errorsBefore+1&&cleaning_ui::current(handle)==beforeError&&paint_ui::getText(handle,"cleaning_default_status").find("failed")!=std::string::npos,"wrong-mode file reports an error without replacing current settings");
  user_defaults::save(uiRoot,0,automatic2);const auto beforeWriteError=bytes(user_defaults::file(uiRoot,0));environment("ADR_USER_DEFAULTS_DIR",blocked.u8string());action("cleaning_default_save");
  verify(cleaning_ui::current(handle)==beforeError&&bytes(user_defaults::file(uiRoot,0))==beforeWriteError,"UI save failure preserves current and previous saved settings");
  verify(paintUndoDepth==0&&paintWrongContext==0,"defaults loads bracket host Undo in the UI callback");
  inPaintCallback=false;pas.paramEditBegin=nullptr;pas.paramEditEnd=nullptr;environment("ADR_USER_DEFAULTS_DIR",savedDefaults);
  std::cout<<"user defaults: explicit per-mode save/load/factory, replacement, failure isolation, Unicode path and portable projects passed\n";
  duringParameterSet=nullptr;pas.paramSetValue=nullptr;pas.paramGetPropertySet=nullptr;interacts=savedInteracts;hostProperties=savedHost;
  for(const auto&saved:savedParameters)e.parameters.at(saved.first)=saved.second;instance(handle).cleaningProfile=savedState;
  std::cout<<"cleaning configurations: stronger defaults, independent edits, reentry, batches, Undo/Redo, reopen and legacy load passed\n";
 }
 // ADR_TEST_CONTROLS="name=value;...": render with these control values (by OFX parameter name) instead of the defaults.
 if(const char*overrides=std::getenv("ADR_TEST_CONTROLS")){std::string list=overrides;size_t start=0;while(start<list.size()){size_t end=list.find(';',start);if(end==std::string::npos)end=list.size();std::string item=list.substr(start,end-start);start=end+1;if(item.empty())continue;auto eq=item.find('=');verify(eq!=std::string::npos,"ADR_TEST_CONTROLS item needs name=value");auto it=e.parameters.find(item.substr(0,eq));verify(it!=e.parameters.end(),"ADR_TEST_CONTROLS names an existing control");const double v=std::stod(item.substr(eq+1));if(it->second.choice)setInt(H(it->second.props),kOfxParamPropDefault,0,int(v));else setDouble(H(it->second.props),kOfxParamPropDefault,0,v);std::cout<<"control "<<item<<"\n";}}
 double range[]={100,100.+count-1};setDoubleN(H(e.source.props),kOfxImageEffectPropFrameRange,2,range);setDouble(H(e.source.props),kOfxImageEffectPropFrameRate,0,24);setDouble(H(e.props),kOfxImageEffectPropFrameRate,0,24);
 for(int f=0;f<count;++f){e.source.images[f+100]=frameImage(sourcePixels,f,w,h,f%2,false);e.output.images[f+100]=frameImage(sourcePixels,f,w,h,f%2==0,true);}
 double scale[]={1,1};setDoubleN(ip,kOfxImageEffectPropRenderScale,2,scale);int bounds[]={-7,11,w-7,h+11};setIntN(ip,kOfxImageEffectPropRenderWindow,4,bounds);setString(ip,kOfxImageEffectPropFieldToRender,0,kOfxImageFieldNone);
 auto renderFrame=[&](int f){setDouble(ip,kOfxPropTime,0,f+100);Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==kOfxStatOK,"query actual render dependencies");auto range=needed.doubles["OfxImageClipPropFrameRange_Source"];verify(range.size()==2,"one declared source interval");declaredFrameRange[0]=range[0];declaredFrameRange[1]=range[1];enforceFrameRange=true;auto status=plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);if(status)std::cerr<<lastHostMessage<<'\n';verify(status==0,"engine render");Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),f+100);Image original(reinterpret_cast<OfxImageClipHandle>(&e.source),f+100);for(int y=0;y<h;++y)for(int x=0;x<w;++x){for(int c=0;c<3;++c)verify(result.read(x,y,c)==targetPixels[((size_t(f)*h+y)*w+x)*3+c],"engine RGB equality");verify(result.read(x,y,3)==original.read(x,y,3),"alpha preservation");}for(int y=0;y<h;++y)for(int pad=0;pad<5;++pad)verify(e.output.images[f+100]->backing[size_t(y)*(w*4+5)+w*4+pad]==-1234,"padding preservation");std::cout<<"frame "<<f<<" exact\n";};
 // Reproduce Resolve's render/host lock inversion: clipGetImage cannot return
 // until another host thread finishes metadata, save or cursor callbacks on
 // this same instance. Each callback has its own two-second deadline, so the
 // regression fails instead of hanging if one accidentally takes render.lock.
 duringImage=[&]{
  const auto sequenceBefore=instance(handle).sequence;
  const auto generationBefore=instance(handle).generation;
  Properties query;setDouble(H(query),kOfxPropTime,0,100);setDoubleN(H(query),kOfxImageEffectPropRenderScale,2,scale);
  auto callback=[&](const char*action,OfxPropertySetHandle args,Properties&reply,OfxStatus expected){
   auto job=std::async(std::launch::async,[&]{return plugin->mainEntry(action,handle,args,H(reply));});
   if(job.wait_for(std::chrono::seconds(2))!=std::future_status::ready){std::cerr<<"FAIL: callback deadlocks during image fetch: "<<action<<'\n';std::_Exit(1);}
   verify(job.get()==expected,"concurrent callback status");
  };
  Properties saved,begun,ended;
  callback(kOfxActionSyncPrivateData,nullptr,saved,kOfxStatReplyDefault);
  callback(kOfxImageEffectActionBeginSequenceRender,H(query),begun,kOfxStatReplyDefault);
  callback(kOfxImageEffectActionEndSequenceRender,H(query),ended,kOfxStatOK);
  Properties frames,region,interest,preferences;
  callback(kOfxImageEffectActionGetFramesNeeded,H(query),frames,kOfxStatOK);
  verify(frames.doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>({100.,double(std::min(100+count-1,104))}),"concurrent frames query preserves temporal range");
  callback(kOfxImageEffectActionGetRegionOfDefinition,H(query),region,kOfxStatOK);
  verify(region.doubles[kOfxImageEffectPropRegionOfDefinition]==std::vector<double>({-7,11,-3,14}),"concurrent source RoD query");
  callback(kOfxImageEffectActionGetRegionsOfInterest,H(query),interest,kOfxStatOK);
  verify(interest.doubles["OfxImageClipPropRoI_Source"]==std::vector<double>({-7,11,-3,14}),"concurrent source RoI query");
  callback(kOfxImageEffectActionGetClipPreferences,H(query),preferences,kOfxStatOK);
  verify(preferences.ints[kOfxImageEffectFrameVarying]==std::vector<int>({1}),"concurrent clip preferences query");
  Properties identity;callback(kOfxImageEffectActionIsIdentity,H(query),identity,kOfxStatReplyDefault);
  Properties changed,ignored;
  setString(H(changed),kOfxPropType,0,kOfxTypeClip);setString(H(changed),kOfxPropName,0,"Source");setString(H(changed),kOfxPropChangeReason,0,kOfxChangeTime);
  callback(kOfxActionInstanceChanged,H(changed),ignored,kOfxStatReplyDefault);
  setString(H(changed),kOfxPropType,0,kOfxTypeParameter);setString(H(changed),kOfxPropName,0,"enabled");setString(H(changed),kOfxPropChangeReason,0,kOfxChangeUserEdited);
  callback(kOfxActionInstanceChanged,H(changed),ignored,kOfxStatReplyDefault);
  verify(instance(handle).sequence==sequenceBefore&&instance(handle).generation==generationBefore,"metadata and ignored callbacks preserve engine state");
  std::cout<<"concurrent host metadata/save/bracket/ignored-change callbacks passed\n";
 };
 verify(e.props.strings[kOfxImageEffectPluginRenderThreadSafety][0]==kOfxImageEffectRenderInstanceSafe,"instances are protected by the plugin lock");
 const int messagesBeforeRendering=messageCalls;
 // A genuinely cold late-frame request must replay exactly and stay within its declaration.
 renderFrame(count-1);
 for(int f=0;f<count;++f){renderFrame(f);auto before=instance(handle).sequence;verify(plugin->mainEntry(kOfxImageEffectActionEndSequenceRender,handle,ip,nullptr)==0,"end bracket");verify(instance(handle).sequence==before,"end bracket preserves bounded history");}
 verify(messageCalls==messagesBeforeRendering,"normal rendering does not call host logging or UI callbacks");
 verify(plugin->mainEntry(kOfxActionPurgeCaches,handle,nullptr,nullptr)==kOfxStatOK,"purge caches");verify(!instance(handle).sequence,"purge releases live sequence");renderFrame(count/2);
 // Paint: a hard rectangle on one frame. Recovery: the source inside, the exact repair outside.
 // Spanish dirt removal: the repair inside only; with nothing painted, the source everywhere.
 {
  const int f=3;paint::Document d;d.width=w;d.height=h;d.nextId=2;paint::Stroke s;s.id=1;s.first=s.last=100+f;s.shape=paint::Shape::Rectangle;s.softness=0;s.points={{.25,.25,1},{.5,.75,1}};d.strokes.push_back(s);
  auto&data=e.parameters["paint_data"].props.strings[kOfxParamPropDefault];auto&mode=e.parameters["mask_mode"].props.ints[kOfxParamPropDefault];
  auto inside=[&](int x,int y){const double px=x+.5,py=h-1-y+.5;return px>.25*w&&px<.5*w&&py>.25*h&&py<.75*h;};
  auto paintRender=[&](int frame,int spanish,const char*name){
   mode={spanish};setDouble(ip,kOfxPropTime,0,frame+100);Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==kOfxStatOK,"paint frames needed");
   declaredFrameRange[0]=needed.doubles["OfxImageClipPropFrameRange_Source"][0];declaredFrameRange[1]=needed.doubles["OfxImageClipPropFrameRange_Source"][1];
   verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==0,name);
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),frame+100);size_t in=0,repaired=0;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){const bool painted=frame==f&&inside(x,y);const bool repair=spanish?painted:!painted;if(painted)++in;
    for(int c=0;c<3;++c){const size_t k=((size_t(frame)*h+y)*w+x)*3+c;const float want=repair?targetPixels[k]:sourcePixels[k];verify(result.read(x,y,c)==want,name);}
    if(repair&&targetPixels[((size_t(frame)*h+y)*w+x)*3]!=sourcePixels[((size_t(frame)*h+y)*w+x)*3])++repaired;}
   std::cout<<name<<": "<<in<<" painted pixels, "<<repaired<<" repaired pixels exact\n";
  };
  data={paint::encode(d)};
  paintRender(f,0,"paint recovery rectangle");paintRender(f-1,0,"paint on another frame changes nothing");
  paintRender(f,1,"paint Spanish dirt removal rectangle");paintRender(f+1,1,"Spanish dirt removal with nothing painted");
  {// Shared cleaned-frame cache: a stroke edit (here on another frame) in a fresh
   // sequence reuses the exact cleaned frame; a changed source frame never does.
   auto&inst=instance(handle);
   paint::Document d2=d;paint::Stroke s2=s;s2.id=2;s2.first=s2.last=100+f+6;d2.nextId=3;d2.strokes.push_back(s2);data={paint::encode(d2)};
   inst.close();
   paintRender(f,1,"Spanish stroke edit reuses the shared cleaned frame exactly");
   verify(inst.rendered==0&&inst.servedCached==1&&!inst.sequence,"stroke edit serves the cleaned frame without restarting the engine");
   auto changed=sourcePixels;changed[size_t(f)*w*h*3]+=.25f;e.source.images[f+100]=frameImage(changed,f,w,h,f%2,false);
   inst.close();setDouble(ip,kOfxPropTime,0,f+100);verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==0,"render after an upstream change");
   verify(inst.rendered==1&&inst.servedCached==0,"a changed source frame is never served from the shared cache");
   e.source.images[f+100]=frameImage(sourcePixels,f,w,h,f%2,false);inst.close();
   paintRender(f,1,"restored source renders exactly again");verify(inst.rendered==1,"restored source is recomputed, not served stale");
   const auto beforeGeneration=inst.generation;
   e.source.images[f+100]=frameImage(changed,f,w,h,f%2,false);
   verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==0,"local cache checks source without a host notification");
   verify(inst.generation==beforeGeneration+1&&inst.rendered==1&&inst.servedCached==0,"source mutation discards local cleaning and motion history");
   e.source.images[f+100]=frameImage(sourcePixels,f,w,h,f%2,false);
   paintRender(f,1,"restored source invalidates local cache and remains exact");
   verify(inst.generation==beforeGeneration+2,"restoring upstream source also invalidates cached cleaning");
   data={paint::encode(d)};
   std::cout<<"shared cleaned-frame cache: exact stroke-edit hit, upstream change refused\n";
  }
  // A connected legacy mask must not also affect built-in painting. This is
  // deliberately a full-white mask: any accidental fetch changes every pixel.
  const auto savedHost=hostProperties;auto*savedInteracts=interacts;OfxInteractSuiteV1 suite{};interacts=&suite;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
  e.mask.mask=true;e.mask.output=true;setInt(H(e.mask.props),kOfxImageClipPropConnected,0,1);
  for(int frame:{f,f+1}){auto image=frameImage(sourcePixels,frame,w,h,false,false);std::fill(image->backing.begin(),image->backing.end(),1.f);e.mask.images[frame+100]=std::move(image);}
  auto&paintEnabled=e.parameters["paint"].props.ints[kOfxParamPropDefault];
  auto&automatic=e.parameters["auto_recovery"].props.ints[kOfxParamPropDefault];
  maskFetches=0;paintEnabled={1};
  paintRender(f,0,"built-in recovery ignores connected legacy mask");
  paintRender(f,1,"built-in Spanish ignores connected legacy mask");
  automatic={1};paintRender(f,1,"Spanish temporarily bypasses automatic recovery");
  verify(automatic==std::vector<int>{1},"Spanish preserves saved automatic-recovery preference");automatic={0};
  paintEnabled={0};paintRender(f,0,"saved strokes still own recovery with editing off");
  paintRender(f+1,0,"saved strokes own frames without a stroke");
  const int renderedBefore=instance(handle).rendered,cachedBefore=instance(handle).cachedFrame;
  const int nextBefore=adr_sequence_next_output(instance(handle).sequence);fetched.clear();
  paintRender(f+1,1,"Spanish untouched frames ignore legacy mask");
  verify(std::all_of(fetched.begin(),fetched.end(),[&](int t){return t==f+101;}),"untouched Spanish fetches only current source");
  verify(instance(handle).rendered==renderedBefore&&instance(handle).cachedFrame==cachedBefore&&adr_sequence_next_output(instance(handle).sequence)==nextBefore,"untouched Spanish never advances the engine or poisons cleaned cache");
  Properties identity;verify(plugin->mainEntry(kOfxImageEffectActionIsIdentity,handle,ip,H(identity))==kOfxStatOK&&identity.strings[kOfxPropName]==std::vector<std::string>{"Source"},"untouched Spanish restored view is host identity");
  // Even with no engine/cache to fall back on, an untouched Spanish frame
  // stays exact. Red difference still needs its grey diagnostic background.
  instance(handle).close();paintRender(f+1,1,"cold untouched Spanish is exact");
  verify(!instance(handle).sequence&&!instance(handle).history,"cold untouched Spanish never creates an engine sequence");
  auto&encoding=e.parameters["encoding"].props.ints[kOfxParamPropDefault];encoding={1};
  paintRender(f+1,1,"untouched Spanish Cineon is exact without engine");encoding={0};
  auto&view=e.parameters["view"].props.ints[kOfxParamPropDefault];view={1};
  verify(plugin->mainEntry(kOfxImageEffectActionIsIdentity,handle,ip,H(identity))==kOfxStatReplyDefault,"diagnostic views are not bypassed as identity");
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK,"untouched Spanish red difference renders without engine");
  {Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),f+101);for(int y=0;y<h;++y)for(int x=0;x<w;++x){const size_t k=((size_t(f+1)*h+y)*w+x)*3;const float grey=.2126f*sourcePixels[k]+.7152f*sourcePixels[k+1]+.0722f*sourcePixels[k+2];for(int c=0;c<3;++c)verify(result.read(x,y,c)==grey,"untouched Spanish preserves red difference background");}}
  verify(!instance(handle).sequence,"Spanish diagnostic view does not start cleaning");view={0};
  // A later new stroke must now obtain real repaired pixels, rather than a
  // source-only frame accidentally stored in the cleaned-picture cache.
  paintRender(f,1,"stroke after untouched bypass uses genuine repair");
  const int editsBefore=instance(handle).rendered;paintRender(f,1,"same-frame Spanish reuses repaired picture");
  verify(instance(handle).rendered==editsBefore,"same-frame Spanish brush render does not rerun cleaning");
  d.strokes[0].opacity=.5;data={paint::encode(d)};
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK&&instance(handle).rendered==editsBefore,"editing stroke opacity reuses the cleaned frame");
  {Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),f+100);for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c){const size_t k=((size_t(f)*h+y)*w+x)*3+c;const float want=inside(x,y)?sourcePixels[k]+(targetPixels[k]-sourcePixels[k])*.5f:sourcePixels[k];verify(result.read(x,y,c)==want,"Spanish opacity edit blends only brush pixels, exactly");}}
  d.strokes[0].opacity=1;data={paint::encode(d)};
  data={""};paintEnabled={1};paintRender(f+1,0,"empty built-in brush ignores legacy mask while editing");
  verify(maskFetches==0,"built-in recovery never requests an external mask image");
  paintEnabled={0};mode={0};setDouble(ip,kOfxPropTime,0,f+100);
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK,"old mask-only project still renders");
  verify(maskFetches==1,"legacy project fetches its original mask");
  {Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),f+100);for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==sourcePixels[((size_t(f)*h+y)*w+x)*3+c],"legacy full-white recovery mask retains original output");}
  {// Spanish cleans a painted frame from its own window (8 back, 3 ahead), never from the clip start.
   const int g=15;paint::Document late=d;late.strokes[0].first=late.strokes[0].last=100+g;data={paint::encode(late)};mode={1};paintEnabled={1};
   verify(plugin->mainEntry(kOfxActionPurgeCaches,handle,nullptr,nullptr)==kOfxStatOK,"purge before windowed Spanish");instance(handle).close();
   setDouble(ip,kOfxPropTime,0,g+100);Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==kOfxStatOK,"windowed Spanish frames needed");
   const auto range=needed.doubles["OfxImageClipPropFrameRange_Source"];verify(range==std::vector<double>{100.+g-8,100.+g+3},"Spanish declares only its window");
   declaredFrameRange[0]=range[0];declaredFrameRange[1]=range[1];const bool savedEnforce=enforceFrameRange;enforceFrameRange=true;fetched.clear();
   verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK,"windowed Spanish renders");enforceFrameRange=savedEnforce;
   verify(!fetched.empty()&&std::all_of(fetched.begin(),fetched.end(),[&](int t){return t>=100+g-8&&t<=100+g+3;}),"Spanish fetches only its window");
   verify(!instance(handle).sequence&&!instance(handle).history,"windowed Spanish keeps no motion history");
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),g+100);
   for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c){const size_t k=((size_t(g)*h+y)*w+x)*3+c;verify(result.read(x,y,c)==(inside(x,y)?targetPixels[k]:sourcePixels[k]),"windowed Spanish equals full-shot cleaning");}
   std::cout<<"Spanish window: frames "<<g-8<<".."<<g+3<<" only, exact against full-shot cleaning\n";
   // Once that frame is cleaned, a stroke edit or playback pass needs only the frame itself.
   Properties again;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(again))==kOfxStatOK&&again.doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>{100.+g,100.+g},"cached Spanish clean declares only its own frame");
   const int cleanedBefore=instance(handle).rendered;declaredFrameRange[0]=declaredFrameRange[1]=100+g;enforceFrameRange=true;fetched.clear();
   late.strokes[0].opacity=.5;data={paint::encode(late)};
   verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK,"cached Spanish stroke edit renders from its own frame");enforceFrameRange=savedEnforce;
   verify(std::all_of(fetched.begin(),fetched.end(),[&](int t){return t==100+g;})&&instance(handle).rendered==cleanedBefore,"cached Spanish stroke edit fetches no neighbours and does not clean again");
   {Image edited(reinterpret_cast<OfxImageClipHandle>(&e.output),g+100);for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c){const size_t k=((size_t(g)*h+y)*w+x)*3+c;verify(edited.read(x,y,c)==(inside(x,y)?sourcePixels[k]+(targetPixels[k]-sourcePixels[k])*.5f:sourcePixels[k]),"cached Spanish stroke edit stays exact");}}
   std::cout<<"Spanish cached frame: stroke edits need only frame "<<g<<"\n";
   data={paint::encode(d)};paintEnabled={0};mode={0};
  }
  setInt(H(e.mask.props),kOfxImageClipPropConnected,0,0);e.mask.images.clear();hostProperties=savedHost;interacts=savedInteracts;
  std::cout<<"integrated recovery: exclusive built-in brush, Spanish auto bypass and legacy mask compatibility passed\n";
  data={""};mode={0};renderFrame(f);
 }

 // A real first-use Spanish profile must reach the cleaner, and its
 // stronger result must still be confined exactly to the painted rectangle.
 {
  const auto savedHost=hostProperties;auto*savedInteracts=interacts;OfxInteractSuiteV1 suite{};interacts=&suite;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
  pas.paramSetValue=paintSetValue;pas.paramGetPropertySet=paintParameterProperties;
  const char*priorDefaults=std::getenv("ADR_USER_DEFAULTS_DIR");const std::string savedDefaults=priorDefaults?priorDefaults:"";
  const auto blockedDefaults=logFolder/"render-defaults"/"blocked";std::filesystem::create_directories(blockedDefaults.parent_path());{std::ofstream out(blockedDefaults);out<<"owned render blocker";}
  environment("ADR_USER_DEFAULTS_DIR",blockedDefaults.u8string());
  Effect stronger;stronger.parameters=e.parameters;stronger.source.props=e.source.props;stronger.output.props=e.output.props;stronger.output.output=true;
  stronger.parameters.at("mask_mode").props.ints[kOfxParamPropDefault]={0};stronger.parameters.at("auto_recovery").props.ints[kOfxParamPropDefault]={0};
  stronger.parameters.at("cleaning_profiles").props.strings[kOfxParamPropDefault]={""};
  for(const auto&c:controls)if(c.boolean||c.integral)stronger.parameters.at(c.name).props.ints[kOfxParamPropDefault]={int(c.initial)};else stronger.parameters.at(c.name).props.doubles[kOfxParamPropDefault]={c.initial};
  const int frame=3;
  for(int f=0;f<=frame+4;++f)stronger.source.images[f+100]=frameImage(sourcePixels,f,w,h,f%2,false);
  stronger.output.images[frame+100]=frameImage(sourcePixels,frame,w,h,false,true);
  const auto sh=reinterpret_cast<OfxImageEffectHandle>(&stronger);
  verify(plugin->mainEntry(kOfxActionCreateInstance,sh,nullptr,nullptr)==kOfxStatOK,"create stronger Spanish render instance");
  paint_ui::setInt(sh,"mask_mode",1);Properties change;setString(H(change),kOfxPropType,0,kOfxTypeParameter);setString(H(change),kOfxPropName,0,"mask_mode");setString(H(change),kOfxPropChangeReason,0,kOfxChangeUserEdited);setDouble(H(change),kOfxPropTime,0,frame+100);
  verify(plugin->mainEntry(kOfxActionInstanceChanged,sh,H(change),nullptr)==kOfxStatOK,"choose stronger Spanish configuration");
  paint::Document doc;doc.width=w;doc.height=h;doc.nextId=2;paint::Stroke stroke;stroke.id=1;stroke.first=stroke.last=frame+100;stroke.shape=paint::Shape::Rectangle;stroke.softness=0;stroke.points={{.25,.25,1},{.5,.75,1}};doc.strokes.push_back(stroke);paint_ui::store(sh,doc);
  adr_controls c;adr_controls_default(&c);c.sensitivity=1;c.extra_sensitivity=1;c.analysis_detail=1;c.repair_amount=1.5;c.motion_guard=0;
  char error[2048]={};auto*reference=adr_sequence_create(context(),&c,w,h,count,error,sizeof error);verify(reference,"create independent stronger-cleaning reference");std::vector<float> expected(size_t(w)*h*3);
  for(;;){const int f=adr_sequence_pull(reference,expected.data(),size_t(w)*12,error,sizeof error);if(f==frame)break;if(f>=0)continue;verify(f==-1,"stronger reference pending input");const int next=adr_sequence_next_source(reference);verify(next>=0&&next<count,"stronger reference source");verify(adr_sequence_push(reference,sourcePixels.data()+size_t(next)*w*h*3,size_t(w)*12,error,sizeof error)==0,"stronger reference upload");}
  adr_sequence_destroy(reference);
  setDouble(ip,kOfxPropTime,0,frame+100);Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,sh,ip,H(needed))==kOfxStatOK,"stronger Spanish dependencies");
  const auto dependency=needed.doubles["OfxImageClipPropFrameRange_Source"];declaredFrameRange[0]=dependency[0];declaredFrameRange[1]=dependency[1];
  verify(plugin->mainEntry(kOfxImageEffectActionRender,sh,ip,nullptr)==kOfxStatOK,"stronger Spanish frame render");
  size_t different=0;
  {Image result(reinterpret_cast<OfxImageClipHandle>(&stronger.output),frame+100);for(int y=0;y<h;++y)for(int x=0;x<w;++x){const bool inside=x+.5>.25*w&&x+.5<.5*w&&h-1-y+.5>.25*h&&h-1-y+.5<.75*h;for(int channel=0;channel<3;++channel){const size_t k=(size_t(y)*w+x)*3+channel;if(inside&&expected[k]!=targetPixels[size_t(frame)*w*h*3+k])++different;verify(result.read(x,y,channel)==(inside?expected[k]:sourcePixels[size_t(frame)*w*h*3+k]),"stronger defaults use exact repair inside brush and original outside");}verify(result.read(x,y,3)==float(x)/w,"stronger profile preserves alpha");}}
  verify(different>0,"stronger defaults genuinely differ from automatic cleaning on the fixture");
  verify(plugin->mainEntry(kOfxActionDestroyInstance,sh,nullptr,nullptr)==kOfxStatOK,"destroy stronger Spanish render instance");
  environment("ADR_USER_DEFAULTS_DIR",savedDefaults);
  pas.paramSetValue=nullptr;pas.paramGetPropertySet=nullptr;interacts=savedInteracts;hostProperties=savedHost;
  std::cout<<"stronger Spanish defaults: direct-engine repair inside brush, exact original outside, alpha preserved\n";
 }

 // Built-in paint/erase deliberately override the automatic-recovery matte;
 // untouched pixels continue to follow it, including partially erased edges.
 {
  Settings settings;settings.maskMode=1;std::vector<float> original(12,.25f),repair(12,.75f),out;
  std::vector<uint8_t> matte(4,1);paint::Layers brush;brush.value={1,0,0,0};brush.touched={1,1,.5f,0};
  finish(settings,0,4,1,original,repair.data(),matte,false,{}, {},&brush,out);
  for(int x=0;x<4;++x)for(int c=0;c<3;++c)verify(out[x*3+c]==(x==1?.75f:x==2?.5f:.25f),"manual paint/erase and automatic matte compose once");
 }

 // A finished processed input recovers a separately prepared original. No temporal
 // fetch or cleaning sequence is allowed on this route, including after brush edits.
 {
  const auto folder=logFolder/"prepared-original";std::vector<float> original(size_t(w)*h*3,.375f);
  paint_source::writeSourceInfo(folder.string(),w,h,24);paint_source::write(folder.string(),5,w,h,original,original);
  {std::ofstream marker(folder/"source-ready.txt");marker<<"ADRREADY1 5 1\n";}
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,1);
  setInt(H(e.parameters["paint_source_offset"].props),kOfxParamPropDefault,0,-95);
  setString(H(e.parameters["paint_source_folder"].props),kOfxParamPropDefault,0,folder.string().c_str());
  setDouble(ip,kOfxPropTime,0,100);OfxInteractSuiteV1 overlaySuite{};interacts=&overlaySuite;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
  auto&data=e.parameters["paint_data"].props.strings[kOfxParamPropDefault];
  paint::Document doc;doc.width=w;doc.height=h;doc.nextId=2;paint::Stroke stroke;stroke.id=1;stroke.first=stroke.last=100;stroke.shape=paint::Shape::Rectangle;stroke.softness=0;stroke.points={{-1,-1,1},{2,2,1}};doc.strokes.push_back(stroke);
  for(int automatic:{0,1})for(int spanish:{0,1})for(int painted:{0,1}){
   setInt(H(e.parameters["auto_recovery"].props),kOfxParamPropDefault,0,automatic);
   data={painted?paint::encode(doc):""};setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,spanish);
   Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==0,"prepared dependencies");
   verify(needed.doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>{100,100},"prepared input needs only the current frame");declaredFrameRange[0]=declaredFrameRange[1]=100;fetched.clear();
   {auto status=plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);if(status)std::cerr<<lastHostMessage<<"\n";verify(status==0,"prepared render");}
   verify(!instance(handle).sequence&&!instance(handle).history,"prepared painting never creates the cleaning engine sequence");
   verify(fetched==std::vector<int>{100,100},"prepared painting fetches only the processed frame");
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);const bool useOriginal=spanish?!painted:painted;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==(useOriginal?.375f:sourcePixels[(size_t(y)*w+x)*3+c]),"prepared original/processed endpoints exact");
  }
  // The original connected to the Additional source input replaces the prepared folder: no path, no preparation.
  setString(H(e.parameters["paint_source_folder"].props),kOfxParamPropDefault,0,"");setInt(H(e.parameters["paint_source_offset"].props),kOfxParamPropDefault,0,0);
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,1);
  setInt(H(e.original.props),kOfxImageClipPropConnected,0,0);data={paint::encode(doc)};
  {verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==0,"unconnected additional source still renders");
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==sourcePixels[(size_t(y)*w+x)*3+c],"unconnected additional source shows the processed picture");}
  {auto image=frameImage(sourcePixels,0,w,h,false,false);std::fill(image->backing.begin(),image->backing.end(),.625f);e.original.images[100]=std::move(image);}
  setInt(H(e.original.props),kOfxImageClipPropConnected,0,1);
  for(int spanish:{0,1})for(int painted:{0,1}){
   data={painted?paint::encode(doc):""};setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,spanish);
   declaredFrameRange[0]=declaredFrameRange[1]=100;const bool savedEnforce=enforceFrameRange;enforceFrameRange=true;fetched.clear();
   {auto status=plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);if(status)std::cerr<<lastHostMessage<<"\n";verify(status==0,"additional source render");}enforceFrameRange=savedEnforce;
   verify(!instance(handle).sequence&&!instance(handle).history,"additional source painting never creates the cleaning engine sequence");
   verify(std::all_of(fetched.begin(),fetched.end(),[](int t){return t==100;}),"additional source painting fetches only the current frame");
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);const bool useOriginal=spanish?!painted:painted;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==(useOriginal?.625f:sourcePixels[(size_t(y)*w+x)*3+c]),"additional source/processed endpoints exact");
  }
  setInt(H(e.original.props),kOfxImageClipPropConnected,0,0);e.original.images.clear();
  // A host that connects the original to the second (mask) input instead.
  {auto image=frameImage(sourcePixels,0,w,h,false,false);std::fill(image->backing.begin(),image->backing.end(),.625f);e.mask.images[100]=std::move(image);}
  setInt(H(e.mask.props),kOfxImageClipPropConnected,0,1);
  for(int spanish:{0,1})for(int painted:{0,1}){
   data={painted?paint::encode(doc):""};setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,spanish);
   {auto status=plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);if(status)std::cerr<<lastHostMessage<<"\n";verify(status==0,"additional source on the mask input renders");}
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);const bool useOriginal=spanish?!painted:painted;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==(useOriginal?.625f:sourcePixels[(size_t(y)*w+x)*3+c]),"additional source on the mask input is exact, never used as a mask");
  }
  setInt(H(e.mask.props),kOfxImageClipPropConnected,0,0);e.mask.images.clear();
  std::cout<<"Additional source: exact Recovery/Spanish endpoints from the connected original, current frame only, no folder; unconnected shows the processed picture\n";
  data={""};setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,0);
  verify(e.parameters["auto_recovery"].props.ints[kOfxParamPropDefault]==std::vector<int>{1},"prepared route preserves automatic-recovery preference");
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,0);
  verify(readSettings(instance(handle),100).mode==1,"ordinary recovery resumes saved automatic-recovery preference");
  setInt(H(e.parameters["auto_recovery"].props),kOfxParamPropDefault,0,0);
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,1);
  interacts=nullptr;verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0,"prepared source rejected outside Resolve");
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,0);renderFrame(0);
  std::cout<<"Prepared original: exact Recovery/Spanish endpoints, current frame only, no engine sequence, host gating passed\n";
 }

 // Explicit preparation freezes exact native-encoding pairs. Rendering from
 // the snapshot must never request temporal inputs, even after a host purge.
 {
  auto savedHost=hostProperties;auto* savedInteracts=interacts;OfxInteractSuiteV1 suite{};interacts=&suite;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
  auto& inst=instance(handle);const auto folder=logFolder/"prepared-shot";
  setString(H(e.parameters["prepare_folder"].props),kOfxParamPropDefault,0,folder.string().c_str());
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,2);
  setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,0);
  setInt(H(e.parameters["paint"].props),kOfxParamPropDefault,0,1);
  e.parameters["paint_data"].props.strings[kOfxParamPropDefault]={""};
  auto feedback=std::make_shared<recovery_feedback::State>();std::atomic_store(&inst.prepareFeedback,feedback);inst.prepareRequested=true;
  bool checkedRunningRange=false;std::function<void()> inspectPreparation;
  inspectPreparation=[&]{if(feedback->phase!=recovery_feedback::State::Running){duringImage=inspectPreparation;return;}
   Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==0,"running preparation dependencies");
   verify(needed.doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>{100.,double(99+count)},"preparation keeps the whole shot declared until finished");checkedRunningRange=true;
  };duringImage=inspectPreparation;
  renderFrame(3);verify(feedback->phase==recovery_feedback::State::Complete&&feedback->done==count,"preparation completes every source frame");
  verify(checkedRunningRange&&!inst.prepareRequested,"running declaration checked and request cleared after preparation");
  for(int f:{count-1,0,3}){
   inst.purge();fetched.clear();renderFrame(f);
   verify(!inst.sequence&&!inst.history,"prepared render never runs the cleaning engine");
   for(int got:fetched)verify(got==100+f,"prepared render requests current frame only");
   auto pair=paint_source::readPair(folder.string(),100+f,w,h);
   verify(std::memcmp(pair.original.data(),sourcePixels.data()+size_t(f)*w*h*3,pair.original.size()*4)==0,"prepared source exact on disk");
  }
  auto held=inst.preparedFrames.get(folder.string(),103,w,h);auto reads=inst.preparedFrames.reads;
  verify(inst.preparedFrames.get(folder.string(),103,w,h).cleaned==held.cleaned&&inst.preparedFrames.reads==reads,"prepared frame retains immutable pair without rereading");
  inst.preparedFrames.get(folder.string(),100,w,h);
  verify(inst.preparedFrames.get(folder.string(),103,w,h).cleaned==held.cleaned,"prepared stepping keeps a bounded RAM window");
  // Spanish and Recovery must use the same exact pair with opposite gates.
  paint::Document d;d.width=w;d.height=h;d.nextId=2;paint::Stroke stroke;stroke.id=1;stroke.first=stroke.last=103;stroke.shape=paint::Shape::Rectangle;stroke.softness=0;stroke.points={{-1,-1,1},{2,2,1}};d.strokes={stroke};
  for(int spanish:{0,1})for(int painted:{0,1}){
   setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,spanish);e.parameters["paint_data"].props.strings[kOfxParamPropDefault]={painted?paint::encode(d):""};
   setDouble(ip,kOfxPropTime,0,103);declaredFrameRange[0]=declaredFrameRange[1]=103;
   verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==0,"prepared brush render");
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),103);const auto& want=(spanish==painted)?targetPixels:sourcePixels;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==want[((size_t(3)*h+y)*w+x)*3+c],"prepared brush endpoints exact");
  }
  auto changed=sourcePixels;changed[size_t(3)*w*h*3]+=.25f;e.source.images[103]=frameImage(changed,3,w,h,true,false);
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0,"prepared shot rejects changed current source without fetching undeclared frames");e.source.images[103]=frameImage(sourcePixels,3,w,h,true,false);
  setString(H(e.parameters["prepare_folder"].props),kOfxParamPropDefault,0,(folder.string()+"-cancel").c_str());
  feedback=std::make_shared<recovery_feedback::State>();feedback->cancel=true;std::atomic_store(&inst.prepareFeedback,feedback);inst.prepareRequested=true;declaredFrameRange[0]=100;declaredFrameRange[1]=99+count;
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0&&feedback->phase==recovery_feedback::State::Cancelled,"preparation cancellation is observed");
  verify(!std::filesystem::exists(folder.string()+"-cancel/source-ready.txt")&&std::filesystem::exists(folder/"source-ready.txt"),"cancel never publishes an incomplete shot or removes prior preparation");
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,0);setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,0);e.parameters["paint_data"].props.strings[kOfxParamPropDefault]={""};hostProperties=savedHost;interacts=savedInteracts;renderFrame(0);
  std::cout<<"Prepared shot: native pairs, random seeks after purge, exact brush gates, current source check and cancellation passed\n";
 }

 // Whole-shot analysis: the UI never takes the render lock, the declared
 // dependency covers the shot, progress cancels cleanly, and saved mattes
 // avoid rerunning either cleaning or classification.
 {
  pas.paramSetValue=paintSetValue;pas.paramGetPropertySet=paintParameterProperties;
  Properties changed;setString(H(changed),kOfxPropType,0,kOfxTypeParameter);setString(H(changed),kOfxPropName,0,"recovery_calculate");
  setString(H(changed),kOfxPropChangeReason,0,kOfxChangeUserEdited);
  auto calculate=[&]{return plugin->mainEntry(kOfxActionInstanceChanged,handle,H(changed),nullptr);};
  auto&i=instance(handle);
  {
   std::lock_guard<std::mutex> renderLock(i.lock);
   auto job=std::async(std::launch::async,calculate);
   if(job.wait_for(std::chrono::seconds(2))!=std::future_status::ready){std::cerr<<"FAIL: calculation button waits for render lock\n";std::_Exit(1);}
   verify(job.get()==kOfxStatOK,"calculation queues without render lock");
  }
  verify(i.calculateRequested&&i.recoveryState==Instance::Queued,"queued recovery state");
  verify(e.parameters.at("recovery_status").props.strings.at(kOfxParamPropDefault)[0].find("Queued")!=std::string::npos,"queued status is visible immediately");
  setDouble(ip,kOfxPropTime,0,100);Properties needed;
  verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==kOfxStatOK&&needed.doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>({100.,double(99+count)}),"whole-shot analysis declares every needed source frame");
  declaredFrameRange[0]=100;declaredFrameRange[1]=99+count;
  auto renderAnalysis=[&]{
   Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==kOfxStatOK,"analysis dependencies");
   const auto range=needed.doubles["OfxImageClipPropFrameRange_Source"];declaredFrameRange[0]=range[0];declaredFrameRange[1]=range[1];
   return plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);
  };
  verify(calculate()==kOfxStatOK&&!i.calculateRequested&&i.recoveryState==Instance::Cancelled,"second click cancels queued work");
  verify(e.parameters.at("auto_recovery").props.ints.at(kOfxParamPropDefault)==std::vector<int>{0},"cancelling restores Auto recovery instead of starting a per-frame classifier");
  auto before=e.output.images[100]->backing;
  progressV1=&progressSuite;progressCancel=true;
  verify(calculate()==kOfxStatOK,"queue cancellation test");
  verify(renderAnalysis()!=kOfxStatOK,"host progress cancellation stops analysis");
  verify(i.recoveryState==Instance::Cancelled&&progressStarts==1&&progressEnds==1,"cancelled progress closes and retains its status");
  verify(e.output.images[100]->backing==before,"cancelled analysis publishes no output pixels");
  progressCancel=false;progressV1=nullptr;
  verify(calculate()==kOfxStatOK,"queue native presentation cancellation test");
  auto feedback=std::atomic_load(&i.recoveryFeedback);verify(bool(feedback),"each calculation owns a presentation snapshot");feedback->cancel=true;
  verify(renderAnalysis()!=kOfxStatOK&&i.recoveryState==Instance::Cancelled,"presentation cancels work without host progress support");
  verify(feedback->phase==recovery_feedback::State::Cancelled&&e.output.images[100]->backing==before,"presentation receives terminal cancellation; no partial output published");
  const auto folder=logFolder/"analysis-mattes";
  paint_ui::setText(handle,"recovery_folder",folder.u8string());
  std::string controlIdentity;readControls(i,100,controlIdentity);auto settings=readSettings(i,100);
  const auto dir=recoveryShotFolder(i,handle,settings,controlIdentity,{100,99+count},w,h,{bounds[0],bounds[1],bounds[2],bounds[3]});
  std::vector<uint8_t> zero(size_t(w)*h,0);
  for(int f=100;f<100+count;++f)saveProbability(dir,f,w,h,zero);
  const auto modelPath=recoveryModelPath();const auto modelHash=recoveryModelIdentity();
  const auto changedModel=logFolder/"model-fixtures"/"different-model.bin";std::filesystem::create_directories(changedModel.parent_path());
  {std::ofstream weights(changedModel,std::ios::binary);weights<<"different checkpoint identity";}
  environment("ADR_RECOVERY_MODEL",changedModel.u8string());
  verify(recoveryModelIdentity()!=modelHash,"different weight contents have a different identity");
  const auto changedDir=recoveryShotFolder(i,handle,settings,controlIdentity,{100,99+count},w,h,{bounds[0],bounds[1],bounds[2],bounds[3]});
  std::vector<uint8_t> cacheProbe;
  verify(changedDir!=dir&&!loadProbability(changedDir,100,w,h,cacheProbe),"new checkpoint cannot reuse old saved probability maps");
  environment("ADR_RECOVERY_MODEL",modelPath);
  verify(recoveryShotFolder(i,handle,settings,controlIdentity,{100,99+count},w,h,{bounds[0],bounds[1],bounds[2],bounds[3]})==dir&&loadProbability(dir,100,w,h,cacheProbe)&&cacheProbe==zero,"previous checkpoint maps remain intact and addressable");
  std::cout<<"model identity: new checkpoints isolate saved mattes; previous maps preserved\n";

  setInt(H(e.parameters["auto_recovery"].props),kOfxParamPropDefault,0,0);renderFrame(0);
  const int rendered=i.rendered;auto*sequence=i.sequence;
  progressV1=nullptr;progressV2=&progressSuiteV2;progressValues.clear();
  verify(calculate()==kOfxStatOK,"queue cached analysis");
  feedback=std::atomic_load(&i.recoveryFeedback);verify(feedback&&!feedback->cancel,"new calculation does not inherit a previous Cancel request");
  verify(renderAnalysis()==kOfxStatOK,"valid whole-shot maps are reused");
  verify(i.recoveryState==Instance::Complete&&i.recoveryDone==count&&i.sequence==sequence&&i.rendered==rendered,"cached analysis creates no cleaning or classification pass");
  verify(feedback->phase==recovery_feedback::State::Complete&&feedback->done==count&&feedback->total==count,"presentation receives completed frame counts without a UI refresh callback");
  verify(progressStarts==2&&progressEnds==2&&!progressValues.empty()&&progressValues.front()==0&&progressValues.back()==1&&std::is_sorted(progressValues.begin(),progressValues.end()),"V2 progress is monotonic and closes on completion");
  auto&status=e.parameters.at("recovery_status").props.strings.at(kOfxParamPropDefault)[0];
  const auto statusHandle=paint_ui::parameter(handle,"recovery_status");
  int statusWrites=0;
  Properties statusChanged;
  setString(H(statusChanged),kOfxPropType,0,kOfxTypeParameter);
  setString(H(statusChanged),kOfxPropName,0,"recovery_status");
  setString(H(statusChanged),kOfxPropChangeReason,0,kOfxChangePluginEdited);
  duringParameterSet=[&](OfxParamHandle written){
   if(written!=statusHandle)return;
   verify(++statusWrites==1,"status notification does not recursively write");
   auto published=status;
   status="Queued";
   plugin->mainEntry(kOfxActionInstanceChanged,handle,H(statusChanged),nullptr);
   status=std::move(published);
  };
  refreshRecoveryUI(handle);
  duringParameterSet=nullptr;
  verify(statusWrites==1,"completion status was written once");
  verify(e.parameters.at("recovery_status").props.strings.at(kOfxParamPropDefault)[0].find("Complete:")!=std::string::npos,"completion and timing refresh in UI callback");
  // A partially cached shot must skip existing mattes, then classify the
  // missing frame from exactly the same repaired pixels as the full engine.
  const auto partialFolder=logFolder/"analysis-partial";paint_ui::setText(handle,"recovery_folder",partialFolder.u8string());
  settings=readSettings(i,100);const auto partialDir=recoveryShotFolder(i,handle,settings,controlIdentity,{100,99+count},w,h,{bounds[0],bounds[1],bounds[2],bounds[3]});
  const int absent=3;for(int f=0;f<count;++f)if(f!=absent)saveProbability(partialDir,100+f,w,h,zero);
  environment("ADR_RECOVERY_MODEL",(std::filesystem::path(__FILE__).parent_path().parent_path()/"resources"/"recovery-model.bin").u8string());
  std::vector<uint8_t> expected(size_t(w)*h),actual;char analysisError[2048]={};const auto stride=size_t(w)*h*3;
  verify(adr_recovery_frame(recoveryModel(),sourcePixels.data()+(absent-1)*stride,sourcePixels.data()+absent*stride,sourcePixels.data()+(absent+1)*stride,targetPixels.data()+absent*stride,w,h,size_t(w)*12,ADR_RECOVERY_INPUT_VIDEO,expected.data(),analysisError,sizeof analysisError)>=0,"independent classifier reference");
  verify(calculate()==kOfxStatOK&&renderAnalysis()==kOfxStatOK,"partial-cache whole-shot pass completes");
  verify(i.recoveryState==Instance::Complete&&loadProbability(partialDir,100+absent,w,h,actual)&&actual==expected,"missing matte matches direct-engine classifier bytes");
  verify(progressStarts==3&&progressEnds==3,"partial-cache calculation closes progress");
  paint_ui::setText(handle,"recovery_folder",(logFolder/"analysis-missing-source").u8string());
  verify(calculate()==kOfxStatOK,"queue missing-source analysis");missing=101;
  before=e.output.images[100]->backing;
  verify(renderAnalysis()!=kOfxStatOK&&i.recoveryState==Instance::Failed,"analysis failure has an explicit failed status");
  verify(progressStarts==4&&progressEnds==4&&e.output.images[100]->backing==before,"failed analysis closes progress without publishing pixels");missing=-999;
  progressV1=nullptr;progressV2=nullptr;
  i.recoveryState=Instance::Idle;i.cancelCalculation=false;i.calculateRequested=false;
  paint_ui::setText(handle,"recovery_folder","");setInt(H(e.parameters["auto_recovery"].props),kOfxParamPropDefault,0,0);refreshRecoveryUI(handle);
  pas.paramSetValue=nullptr;pas.paramGetPropertySet=nullptr;renderFrame(0);
  std::cout<<"analysis UI: lock-free queue/cancel, full dependencies, V1/V2 progress, matte reuse, failure and timing passed\n";
 }

 // Progressive "Both" renders: some hosts report an inclusive full-frame window.
 setString(H(e.source.props),kOfxImageClipPropFieldOrder,0,kOfxImageFieldNone);setString(H(e.output.props),kOfxImageClipPropFieldOrder,0,kOfxImageFieldNone);setString(ip,kOfxImageEffectPropFieldToRender,0,kOfxImageFieldBoth);
 int inclusive[]={bounds[0],bounds[1],bounds[2]-1,bounds[3]-1};setIntN(ip,kOfxImageEffectPropRenderWindow,4,inclusive);
 auto full=std::array<int,4>{bounds[0],bounds[1],bounds[2],bounds[3]};
 verify(renderWindow(handle,ip,full)==full,"inclusive full-frame window widened for progressive Both");
 e.output.images[100]=frameImage(sourcePixels,0,w,h,true,true);renderFrame(0);
 auto rejected=[&](){try{renderWindow(handle,ip,full);return false;}catch(const Error&){return true;}};
 int partial[]={bounds[0]+3,bounds[1]+2,bounds[2]-5,bounds[3]-4};setIntN(ip,kOfxImageEffectPropRenderWindow,4,partial);
 verify(renderWindow(handle,ip,full)==std::array<int,4>{partial[0],partial[1],partial[2],partial[3]},"Both keeps partial windows unchanged");
 setIntN(ip,kOfxImageEffectPropRenderWindow,4,bounds);e.output.images[101]=frameImage(sourcePixels,1,w,h,false,true);renderFrame(1);
 for(auto order:{kOfxImageFieldLower,kOfxImageFieldUpper}){
  setString(H(e.source.props),kOfxImageClipPropFieldOrder,0,order);verify(rejected(),"Both rejects fielded source");setString(H(e.source.props),kOfxImageClipPropFieldOrder,0,kOfxImageFieldNone);
  setString(H(e.output.props),kOfxImageClipPropFieldOrder,0,order);verify(rejected(),"Both rejects fielded output");setString(H(e.output.props),kOfxImageClipPropFieldOrder,0,kOfxImageFieldNone);
  setString(ip,kOfxImageEffectPropFieldToRender,0,order);verify(rejected(),"single-field render rejected");setString(ip,kOfxImageEffectPropFieldToRender,0,kOfxImageFieldBoth);
 }
 setIntN(ip,kOfxImageEffectPropRenderWindow,4,inclusive);
 setString(ip,kOfxImageEffectPropFieldToRender,0,kOfxImageFieldNone);verify(renderWindow(handle,ip,{bounds[0],bounds[1],bounds[2],bounds[3]})==std::array<int,4>{inclusive[0],inclusive[1],inclusive[2],inclusive[3]},"ordinary partial window is not enlarged");
 setIntN(ip,kOfxImageEffectPropRenderWindow,4,bounds);renderFrame(count-1);
 auto pid=instance(handle).sequence;
 setString(ip,kOfxPropType,0,kOfxTypeParameter);setString(ip,kOfxPropName,0,"enabled");setString(ip,kOfxPropChangeReason,0,kOfxChangeUserEdited);
 setInt(H(e.parameters["enabled"].props),kOfxParamPropDefault,0,0);verify(plugin->mainEntry(kOfxActionInstanceChanged,handle,ip,nullptr)==kOfxStatReplyDefault,"parameter edit notification");verify(instance(handle).sequence==pid,"parameter notification retains the sequence for keyed replay");
 setDouble(ip,kOfxPropTime,0,100);verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==0,"disabled parameter edit render");{Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==sourcePixels[(size_t(y)*w+x)*3+c],"changed controls replay without stale output");}
 setInt(H(e.parameters["enabled"].props),kOfxParamPropDefault,0,1);plugin->mainEntry(kOfxActionInstanceChanged,handle,ip,nullptr);renderFrame(0);pid=instance(handle).sequence;setString(ip,kOfxPropType,0,kOfxTypeClip);setString(ip,kOfxPropName,0,"Source");
 setString(ip,kOfxPropChangeReason,0,kOfxChangeTime);verify(plugin->mainEntry(kOfxActionInstanceChanged,handle,ip,nullptr)==kOfxStatReplyDefault,"time notification");verify(instance(handle).sequence==pid,"time notification preserves history");setString(ip,kOfxPropChangeReason,0,kOfxChangeUserEdited);renderFrame(count/2);renderFrame(1);renderFrame(count/2+1);
 setDouble(ip,kOfxPropTime,0,100+count/2);verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(out))==0,"frames needed");verify(P(H(out)).doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>({100.,double(std::min(100+count-1,104+count/2))}),"declared complete motion history");setDouble(ip,kOfxPropTime,0,99);verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(out))==0&&P(H(out)).doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>({99.,99.}),"time outside the source needs only itself");
 verify(plugin->mainEntry(kOfxActionInstanceChanged,handle,ip,nullptr)==kOfxStatReplyDefault,"invalidate");verify(!instance(handle).sequence,"edited source releases the sequence");
 auto saved=e.output.images[100]->backing;missing=101;setDouble(ip,kOfxPropTime,0,100);verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0,"missing temporal frame fails");verify(e.output.images[100]->backing==saved,"failure publishes no pixels");verify(!instance(handle).sequence,"failed render releases the sequence");missing=-999;
 // Missing-image failure left no sequence: cancellation now exercises cold replay.
 auto beforeAbort=e.output.images[count-1+100]->backing;
 setDouble(ip,kOfxPropTime,0,count-1+100);Properties abortNeeded;
 verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(abortNeeded))==kOfxStatOK,"abort dependencies");
 auto abortRange=abortNeeded.doubles["OfxImageClipPropFrameRange_Source"];verify(abortRange.size()==2,"abort source range");
 declaredFrameRange[0]=abortRange[0];declaredFrameRange[1]=abortRange[1];
 abortEnabled=true;abortCount=0;
 verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=kOfxStatOK&&abortCount>12,"cold replay cancellation fails explicitly");
 abortEnabled=false;
 verify(e.output.images[count-1+100]->backing==beforeAbort,"cancellation publishes no pixels");verify(!instance(handle).sequence,"cancellation releases sequence");
 renderFrame(count-1);renderFrame(0);
 // Change the actual delivered image dimensions on this same instance. A
 // host resize node can keep its canvas unchanged, so do not rely on that
 // alone to exercise geometry invalidation. Compare the smaller render to
 // a separate direct-engine sequence, then restore the existing fixture.
 {
  const int sw=256,sh=256;
  std::vector<float> small(size_t(count)*sw*sh*3),expected(size_t(sw)*sh*3);
  for(int f=0;f<count;++f)for(int y=0;y<sh;++y)for(int x=0;x<sw;++x)for(int c=0;c<3;++c)
   small[((size_t(f)*sh+y)*sw+x)*3+c]=sourcePixels[((size_t(f)*h+y*2)*w+x*2)*3+c];
  adr_controls controls;adr_controls_default(&controls);char error[2048]={};
  auto* reference=adr_sequence_create(context(),&controls,sw,sh,count,error,sizeof(error));
  verify(reference,"geometry direct-engine sequence");
  for(;;){int output=adr_sequence_pull(reference,expected.data(),size_t(sw)*12,error,sizeof(error));
   if(output==0)break;
   verify(output==-1,"geometry reference pending input");
   int next=adr_sequence_next_source(reference);verify(next>=0&&next<count,"geometry reference source");
   verify(adr_sequence_push(reference,small.data()+size_t(next)*sw*sh*3,size_t(sw)*12,error,sizeof(error))==0,"geometry reference push");
  }
  adr_sequence_destroy(reference);
  auto sourceImages=std::move(e.source.images),outputImages=std::move(e.output.images);
  for(int f=0;f<count;++f){e.source.images[f+100]=frameImage(small,f,sw,sh,false,false);e.output.images[f+100]=frameImage(small,f,sw,sh,false,true);}
  int smallBounds[]={-7,11,sw-7,sh+11};setIntN(ip,kOfxImageEffectPropRenderWindow,4,smallBounds);setDouble(ip,kOfxPropTime,0,100);
  declaredFrameRange[0]=100;declaredFrameRange[1]=std::min(104,100+count-1);
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==0,"changed geometry renders");
  {Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);for(int y=0;y<sh;++y)for(int x=0;x<sw;++x)for(int c=0;c<3;++c)
   verify(result.read(x,y,c)==expected[(size_t(y)*sw+x)*3+c],"changed geometry exact direct-engine pixels");}
  verify(instance(handle).key.find("256x256")!=std::string::npos,"sequence uses changed dimensions");
  e.source.images=std::move(sourceImages);e.output.images=std::move(outputImages);setIntN(ip,kOfxImageEffectPropRenderWindow,4,bounds);renderFrame(0);
  std::cout<<"same-instance 512 -> 256 -> 512 geometry exact\n";
 }
 {
  const auto*sequence=instance(handle).sequence;const auto*history=instance(handle).history;
  const int cached=instance(handle).cachedFrame;const auto pixels=instance(handle).cached;
  const auto output=e.output.images[100]->backing;const double proxy[]={0,.5};setDoubleN(ip,kOfxImageEffectPropRenderScale,2,proxy);
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=kOfxStatOK,"invalid preview scale rejected");
  verify(instance(handle).sequence==sequence&&instance(handle).history==history&&instance(handle).cachedFrame==cached&&instance(handle).cached==pixels,"rejected preview never clears full-resolution history or cleaned cache");
  verify(e.output.images[100]->backing==output,"rejected preview publishes no pixels");
  setDoubleN(ip,kOfxImageEffectPropRenderScale,2,scale);renderFrame(0);
 }

 {
  const auto nativeRod=testRod;testRod={-7,11,double(w-7),double(h+11)};
  const auto nativeParameters=e.parameters;auto nativeSource=std::move(e.source.images),nativeOutput=std::move(e.output.images),nativeOriginal=std::move(e.original.images);
  const auto originalProperties=e.original.props;
  const auto*sequence=instance(handle).sequence;const auto*history=instance(handle).history;const auto nativeCache=instance(handle).cached;
  const int nativeW=instance(handle).frameW,nativeH=instance(handle).frameH;
  setInt(H(e.original.props),kOfxImageClipPropConnected,0,1);
  setInt(H(e.parameters.at("paint_source_kind").props),kOfxParamPropDefault,0,1);
  setInt(H(e.parameters.at("auto_recovery").props),kOfxParamPropDefault,0,0);
  setInt(H(e.parameters.at("paint").props),kOfxParamPropDefault,0,1);
  paint::Document document;document.width=w;document.height=h;
  paint::Stroke stroke;stroke.id=1;stroke.first=stroke.last=100;stroke.shape=paint::Shape::Rectangle;stroke.points={{.25,.25,1},{.75,.75,1}};stroke.opacity=1;stroke.softness=0;document.strokes.push_back(stroke);document.nextId=2;
  setString(H(e.parameters.at("paint_data").props),kOfxParamPropDefault,0,paint::encode(document).c_str());
  for(auto dimensions:{std::make_pair(92,46),std::make_pair(256,128),std::make_pair(171,117)}){
   const int pw=dimensions.first,ph=dimensions.second;double proxy[]={pw==171?.333:double(pw)/w,pw==171?.229:double(ph)/h};setDoubleN(ip,kOfxImageEffectPropRenderScale,2,proxy);
   std::vector<float> native(sourcePixels.begin(),sourcePixels.begin()+size_t(w)*h*3);auto clean=paint_source::preview(native,w,h,pw,ph),original=clean;for(auto&v:original)v+=.125f;
   e.source.images[100]=frameImage(clean,0,pw,ph,true,false);e.output.images[100]=frameImage(clean,0,pw,ph,false,true);e.original.images[100]=frameImage(original,0,pw,ph,false,false);
   // Resolve labels images scale 1 even though the action requests a proxy.
   int proxyBounds[]={-7,11,pw-7,ph+11};setIntN(ip,kOfxImageEffectPropRenderWindow,4,proxyBounds);
   for(int method:{0,1}){
    setInt(H(e.parameters.at("mask_mode").props),kOfxParamPropDefault,0,method);setInt(H(e.parameters.at("view").props),kOfxParamPropDefault,0,0);
    if(method==1){Properties query,needed;setDouble(H(query),kOfxPropTime,0,100);verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,H(query),H(needed))==kOfxStatOK,"frames-needed without optional render scale");}
    verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK,"anisotropic thumbnail and viewer proxy render");
    Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100,nullptr,"preview result",true);Image input(reinterpret_cast<OfxImageClipHandle>(&e.source),100,nullptr,"preview input",true);
    for(int y=0;y<ph;++y)for(int x=0;x<pw;++x){bool inside=(x+.5)/pw>.25&&(x+.5)/pw<.75&&(y+.5)/ph>.25&&(y+.5)/ph<.75;
     for(int c=0;c<3;++c){size_t k=(size_t(y)*pw+x)*3+c;const float expected=(method==0?inside:!inside)?original[k]:clean[k];verify(result.read(x,y,c)==expected,"Recovery and Spanish proxy restore correct original/processed endpoint");}
     verify(result.read(x,y,3)==input.read(x,y,3),"proxy alpha preserved");
    }
   }
   verify(instance(handle).sequence==sequence&&instance(handle).history==history&&instance(handle).cached==nativeCache&&instance(handle).frameW==nativeW&&instance(handle).frameH==nativeH,"proxy preserves native history, pixels and paint dimensions");
  }
  // Live cleaning on a tiny derivative uses an isolated padded engine, not a
  // passthrough. Compare against a separate direct-engine reference.
  const int pw=92,ph=46;double proxy[]={double(pw)/w,double(ph)/h};setDoubleN(ip,kOfxImageEffectPropRenderScale,2,proxy);
  setInt(H(e.parameters.at("paint_source_kind").props),kOfxParamPropDefault,0,0);setInt(H(e.parameters.at("mask_mode").props),kOfxParamPropDefault,0,0);setString(H(e.parameters.at("paint_data").props),kOfxParamPropDefault,0,"");
  e.source.images.clear();for(int f=0;f<count;++f){std::vector<float> native(sourcePixels.begin()+size_t(f)*w*h*3,sourcePixels.begin()+size_t(f+1)*w*h*3);auto rgb=paint_source::preview(native,w,h,pw,ph);e.source.images[f+100]=frameImage(rgb,0,pw,ph,f%2,false);}
  std::vector<float> blank(size_t(pw)*ph*3);e.output.images[100]=frameImage(blank,0,pw,ph,false,true);int liveBounds[]={-7,11,pw-7,ph+11};setIntN(ip,kOfxImageEffectPropRenderWindow,4,liveBounds);
  setDouble(ip,kOfxPropTime,0,100);enforceFrameRange=false;auto previewStatus=plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);if(previewStatus)std::cerr<<lastHostMessage<<'\n';verify(previewStatus==kOfxStatOK,"tiny live-cleaning preview renders");
  {std::string identity;auto controls=readControls(instance(handle),100,identity);if(readSettings(instance(handle),100).encoding)controls.extended_range=1;
   char error[2048]={};std::unique_ptr<adr_sequence,decltype(&adr_sequence_destroy)> reference(adr_sequence_create(context(),&controls,256,256,count,error,sizeof error),adr_sequence_destroy);verify(bool(reference),"preview reference sequence");std::vector<float> expected(size_t(256)*256*3),linear;
   for(;;){int f=adr_sequence_pull(reference.get(),expected.data(),size_t(256)*12,error,sizeof error);verify(f>=-1,"preview reference pull");if(f==0)break;int next=adr_sequence_next_source(reference.get());verify(next>=0,"preview reference source index");Image input(reinterpret_cast<OfxImageClipHandle>(&e.source),100+next);std::vector<float> padded(size_t(256)*256*3);for(int y=0;y<256;++y)for(int x=0;x<256;++x)for(int c=0;c<3;++c)padded[(size_t(y)*256+x)*3+c]=input.read(std::min(x,pw-1),std::min(y,ph-1),c);const float* pixels=padded.data();if(readSettings(instance(handle),100).encoding){toLinear(padded,linear);pixels=linear.data();}verify(adr_sequence_push(reference.get(),pixels,size_t(256)*12,error,sizeof error)==0,"preview reference upload");}
   std::vector<float> cropped(size_t(pw)*ph*3);for(int y=0;y<ph;++y)std::copy_n(expected.data()+size_t(y)*256*3,size_t(pw)*3,cropped.data()+size_t(y)*pw*3);
   Image input(reinterpret_cast<OfxImageClipHandle>(&e.source),100),result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);if(readSettings(instance(handle),100).encoding){std::vector<float> encoded;cineonDelta(previewPixels(input),cropped,encoded);cropped=std::move(encoded);}
   for(int y=0;y<ph;++y)for(int x=0;x<pw;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==cropped[(size_t(y)*pw+x)*3+c],"live tiny preview equals independent direct-engine crop");
  }
  verify(instance(handle).sequence==sequence&&instance(handle).history==history&&instance(handle).cached==nativeCache,"live preview also preserves native cache");
  for(int outside:{99,100+count}){
   e.output.images[outside]=frameImage(blank,0,pw,ph,false,true);setDouble(ip,kOfxPropTime,0,outside);const auto fetches=fetched.size();
   verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK,"outside-shot preview completes without source fetches or persistent error");
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),outside);
   for(int y=0;y<ph;++y)for(int x=0;x<pw;++x)for(int c=0;c<4;++c)verify(result.read(x,y,c)==0,"outside-shot preview is transparent black");
   verify(fetched.size()==fetches&&instance(handle).sequence==sequence&&instance(handle).history==history&&instance(handle).cached==nativeCache,"outside-shot preview preserves source dependencies and native history");
  }
  setDouble(ip,kOfxPropTime,0,100);
  const auto previewFolder=logFolder/"preview-must-not-save-mattes";setString(H(e.parameters.at("recovery_folder").props),kOfxParamPropDefault,0,previewFolder.u8string().c_str());setInt(H(e.parameters.at("auto_recovery").props),kOfxParamPropDefault,0,1);
  const bool queued=instance(handle).calculateRequested;const auto state=instance(handle).recoveryState.load();instance(handle).calculateRequested=true;instance(handle).recoveryState=Instance::Queued;
  verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)==kOfxStatOK,"tiny auto-recovery preview renders");
  verify(instance(handle).calculateRequested&&instance(handle).recoveryState==Instance::Queued&&!std::filesystem::exists(previewFolder),"preview does not consume queued native analysis or save reduced mattes");
  instance(handle).calculateRequested=queued;instance(handle).recoveryState=state;
  e.source.images=std::move(nativeSource);e.output.images=std::move(nativeOutput);e.original.images=std::move(nativeOriginal);e.original.props=originalProperties;testRod=nativeRod;
  // The mock host hands out pointers to its parameter objects as OFX handles.
  // Restore their values in place so the active instance keeps valid handles.
  for(const auto&[name,parameter]:nativeParameters)e.parameters.at(name)=parameter;
  setDoubleN(ip,kOfxImageEffectPropRenderScale,2,scale);setIntN(ip,kOfxImageEffectPropRenderWindow,4,bounds);renderFrame(0);
  std::cout<<"proxy: anisotropic tiny/viewer Recovery and Spanish endpoints, alpha, native history isolation, live cleaning and native return passed\n";
 }
 setDouble(ip,kOfxPropTime,0,100.5);verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0,"fractional time rejected");
 verify(plugin->mainEntry(kOfxActionDestroyInstance,handle,nullptr,nullptr)==0,"destroy");verify(acquired==released,"all image handles released");verify(undeclaredFetches==0,"all source fetches inside actual render declaration");verify(plugin->mainEntry(kOfxActionUnload,nullptr,nullptr,nullptr)==0,"unload");
 {std::vector<std::filesystem::path> logs;for(auto&f:std::filesystem::directory_iterator(logFolder))if(f.is_regular_file())logs.push_back(f.path());verify(logs.size()==1,"one diagnostic log per process");
  std::string text;{std::ifstream in(logs[0]);text.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());}
  for(auto want:{"Advanced Dirt Removal ","plugin binary: ","host: ","system: ","environment overrides: ","GPU: ","instance #1 created","sequence start: 512x512","frame 100 (1/21) rendered","history reset","ERROR","host failed to provide required in-range image","fractional frame times","sequence ended","instance #1 destroyed","unload"})
   if(text.find(want)==std::string::npos){std::cerr<<"FAIL: diagnostic log lacks \""<<want<<"\"\n"<<text;std::exit(1);}
  verify(lastHostMessage.find("Details in the Advanced Dirt Removal log: ")!=std::string::npos,"host error message names the log");
  environment("ADR_LOG","off");verify(plugin->mainEntry(kOfxActionLoad,nullptr,nullptr,nullptr)==0,"reload");verify(plugin->mainEntry(kOfxActionUnload,nullptr,nullptr,nullptr)==0,"unload again");
  size_t after=0;for(auto&f:std::filesystem::directory_iterator(logFolder)){if(f.is_regular_file())++after;}verify(after==1,"ADR_LOG=off writes nothing");
  if(std::getenv("ADR_SHOW_LOG"))std::cout<<text;
  std::cout<<"diagnostic log: "<<text.size()<<" bytes, "<<std::count(text.begin(),text.end(),'\n')<<" lines\n";std::filesystem::remove_all(logFolder);}
 std::cout<<"contract passed\n";
}
