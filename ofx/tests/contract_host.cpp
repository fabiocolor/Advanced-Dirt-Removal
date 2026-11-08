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
struct Clip {Properties props;std::map<int,std::unique_ptr<TestImage>> images;bool output=false;};
struct Effect {Properties props;std::map<std::string,Parameter> parameters;Clip source,output;};
Effect& E(OfxImageEffectHandle e){return *reinterpret_cast<Effect*>(e);}
bool enforceFrameRange=false;double declaredFrameRange[2]{};int undeclaredFetches=0;
std::vector<int> fetched;int acquired=0,released=0,abortCount=0;bool abortEnabled=false,slowImageOnce=false;int missing=-999;
OfxStatus effectProps(OfxImageEffectHandle e,OfxPropertySetHandle*p){*p=H(E(e).props);return 0;}
OfxStatus paramSet(OfxImageEffectHandle e,OfxParamSetHandle*p){*p=reinterpret_cast<OfxParamSetHandle>(&E(e).parameters);return 0;}
OfxStatus defineClip(OfxImageEffectHandle e,const char*n,OfxPropertySetHandle*p){*p=H(std::string(n)=="Source"?E(e).source.props:E(e).output.props);return 0;}
OfxStatus clipHandle(OfxImageEffectHandle e,const char*n,OfxImageClipHandle*c,OfxPropertySetHandle*p){auto&clip=std::string(n)=="Source"?E(e).source:E(e).output;*c=reinterpret_cast<OfxImageClipHandle>(&clip);if(p)*p=H(clip.props);return 0;}
OfxStatus clipProps(OfxImageClipHandle c,OfxPropertySetHandle*p){*p=H(reinterpret_cast<Clip*>(c)->props);return 0;}
OfxStatus getImage(OfxImageClipHandle c,double t,const OfxRectD*,OfxPropertySetHandle*p){auto&clip=*reinterpret_cast<Clip*>(c);if(!clip.output&&enforceFrameRange&&(t<declaredFrameRange[0]||t>declaredFrameRange[1])){++undeclaredFetches;std::cerr<<"FAIL: source fetch "<<t<<" outside declared range "<<declaredFrameRange[0]<<".."<<declaredFrameRange[1]<<"\n";return kOfxStatFailed;}if(duringImage){auto callback=std::move(duringImage);duringImage=nullptr;callback();}if(slowImageOnce){slowImageOnce=false;std::this_thread::sleep_for(std::chrono::milliseconds(5300));}if(!clip.output)fetched.push_back(int(t));if(!clip.output&&int(t)==missing)return kOfxStatFailed;auto it=clip.images.find(int(t));if(it==clip.images.end())return kOfxStatFailed;*p=H(it->second->props);++acquired;return 0;}
OfxStatus releaseImage(OfxPropertySetHandle){++released;return 0;}
OfxStatus rod(OfxImageClipHandle,double,OfxRectD*r){*r={-7,11,-3,14};return 0;}
int aborted(OfxImageEffectHandle){return abortEnabled&&++abortCount>12;}
OfxStatus defineParameter(OfxParamSetHandle s,const char*t,const char*n,OfxPropertySetHandle*p){auto&items=*reinterpret_cast<std::map<std::string,Parameter>*>(s);auto inserted=items.emplace(n,Parameter{});auto&v=inserted.first->second;if(inserted.second)v.ordinal=items.size()-1;v.type=t;v.choice=std::string(t)==kOfxParamTypeChoice||std::string(t)==kOfxParamTypeBoolean||std::string(t)==kOfxParamTypeInteger;v.text=std::string(t)==kOfxParamTypeString;*p=H(v.props);return 0;}
OfxStatus parameterHandle(OfxParamSetHandle s,const char*n,OfxParamHandle*h,OfxPropertySetHandle*p){auto&v=(*reinterpret_cast<std::map<std::string,Parameter>*>(s))[n];*h=reinterpret_cast<OfxParamHandle>(&v);if(p)*p=H(v.props);return 0;}
OfxStatus value(OfxParamHandle h,OfxTime t,...){auto&p=*reinterpret_cast<Parameter*>(h);va_list a;va_start(a,t);if(p.text){auto&s=p.props.strings[kOfxParamPropDefault];if(s.empty())s.push_back("");*va_arg(a,char**)=s[0].data();}else if(p.choice)*va_arg(a,int*)=p.props.ints[kOfxParamPropDefault][0];else *va_arg(a,double*)=p.props.doubles[kOfxParamPropDefault][0];va_end(a);return 0;}
OfxStatus currentValue(OfxParamHandle h,...){auto&p=*reinterpret_cast<Parameter*>(h);va_list a;va_start(a,h);if(p.text){auto&v=p.props.strings[kOfxParamPropDefault];if(v.empty())v.push_back("");*va_arg(a,char**)=v[0].data();}else if(p.choice)*va_arg(a,int*)=p.props.ints[kOfxParamPropDefault][0];else *va_arg(a,double*)=p.props.doubles[kOfxParamPropDefault][0];va_end(a);return kOfxStatOK;}
// UI writes must occur in host callbacks, even when a native key arrived first.
bool inPaintCallback=false;int paintUndoDepth=0,paintUndoBegins=0,paintWrongContext=0,paintRedraws=0;
OfxStatus paintSetValue(OfxParamHandle h,...){auto&p=*reinterpret_cast<Parameter*>(h);va_list a;va_start(a,h);if(p.text)p.props.strings[kOfxParamPropDefault]={va_arg(a,const char*)};else if(p.choice)p.props.ints[kOfxParamPropDefault]={va_arg(a,int)};else p.props.doubles[kOfxParamPropDefault]={va_arg(a,double)};va_end(a);return kOfxStatOK;}
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
 verify(paint_ui::key('+')==paint_ui::Action::SoftUp&&paint_ui::key('=')==paint_ui::Action::SoftUp&&paint_ui::key(0xffff)==paint_ui::Action::Delete&&paint_ui::key(0xff08)==paint_ui::Action::Delete,"brush key aliases and both delete symbols");
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
 verify(plugin->mainEntry(kOfxImageEffectActionDescribeInContext,handle,ip,nullptr)==0,"context");verify(e.parameters.size()==67,"all controls, groups and pages exposed");verify(e.parameters.count("page_paint")==0&&e.parameters["paint"].props.ints[kOfxParamPropSecret]==std::vector<int>{1}&&e.parameters["paint_tool"].props.ints[kOfxParamPropSecret]==std::vector<int>{1},"viewer painting hidden outside Resolve");verify(e.props.pointers.count(kOfxImageEffectPluginPropOverlayInteractV1)==0,"no viewer overlay outside Resolve");for(auto pg:{"page_cleaning","page_recovery","page_view"})verify(e.parameters.count(pg)==1,"inspector page defined");for(auto g:{"detection","processing","safeguards"})verify(e.parameters.count(g)==1,"control group defined");verify(e.parameters["sensitivity"].props.strings[kOfxParamPropParent]==std::vector<std::string>{"detection"},"controls are grouped");verify(plugin->mainEntry(kOfxActionCreateInstance,handle,nullptr,nullptr)==0,"create");
 // Compact Resolve presentation preserves every existing value/type/range/choice.
 // The normal-host descriptor is an independent ordering of the same controls.
 {
  const auto savedHost=hostProperties;auto*savedInteracts=interacts;OfxInteractSuiteV1 suite{};interacts=&suite;setString(H(hostProperties),kOfxPropName,0,"DaVinciResolve");
  Effect compact;auto compactHandle=reinterpret_cast<OfxImageEffectHandle>(&compact);
  verify(plugin->mainEntry(kOfxImageEffectActionDescribeInContext,compactHandle,ip,nullptr)==kOfxStatOK,"compact Resolve descriptor");
  for(const auto&entry:e.parameters){const auto&before=entry.second;if(before.type==kOfxParamTypeGroup||before.type==kOfxParamTypePage)continue;
   auto found=compact.parameters.find(entry.first);verify(found!=compact.parameters.end(),"compact layout retains every parameter identity");const auto&after=found->second;
   verify(before.type==after.type,"compact layout retains parameter types");
   auto a=before.props,b=after.props;
   for(auto key:{kOfxPropLabel,kOfxParamPropHint,kOfxParamPropParent}){a.strings.erase(key);b.strings.erase(key);}
   a.ints.erase(kOfxParamPropSecret);b.ints.erase(kOfxParamPropSecret);
   verify(a.strings==b.strings&&a.ints==b.ints&&a.doubles==b.doubles,"compact layout retains defaults, ranges, choices and evaluation behavior");
  }
  verify(compact.parameters.at("view").ordinal<compact.parameters.at("mask_mode").ordinal&&compact.parameters.at("sensitivity").ordinal<compact.parameters.at("paint").ordinal,"checking and cleaning controls precede paint");
  for(auto name:{"auto_recovery_group","processing","stroke_details_group","paint_options_group","paint_source_group","view_group"})verify(compact.parameters.at(name).props.ints.at(kOfxParamPropGroupOpen)==std::vector<int>{0},"secondary groups start collapsed");
  verify(compact.parameters.at("paint_hold").props.strings.at(kOfxParamPropParent)==std::vector<std::string>{"stroke_details_group"},"duration is a stroke detail");
  const auto&children=compact.parameters.at("page_paint").props.strings.at(kOfxParamPropPageChild);for(auto name:{"paint_undo","paint_redo"})verify(std::find(children.begin(),children.end(),name)!=children.end(),"page-respecting hosts retain paint history buttons");
  hostProperties=savedHost;interacts=savedInteracts;std::cout<<"compact UI parameter contract and group defaults: passed\n";
 }
 // Native keys request redraw without editing OFX parameters outside a
 // callback. A host-delayed mouse-up must preserve the queued command order.
 {
  const auto savedParameters=e.parameters;
  pas.paramSetValue=paintSetValue;pas.paramEditBegin=paintEditBegin;pas.paramEditEnd=paintEditEnd;pas.paramGetPropertySet=paintParameterProperties;
  OfxInteractSuiteV1 testInteract{};testInteract.interactGetPropertySet=paintInteractProperties;testInteract.interactRedraw=paintRedraw;
  auto* savedInteract=interacts;interacts=&testInteract;
  paint_ui::Overlay o;o.effect=handle;o.hasCursor=true;Properties overlayProperties;o.handle=reinterpret_cast<OfxInteractHandle>(&overlayProperties);setPointer(H(overlayProperties),kOfxPropInstanceData,0,&o);
  setInt(H(e.parameters.at("paint").props),kOfxParamPropDefault,0,1);
  setDouble(H(e.parameters.at("paint_softness").props),kOfxParamPropDefault,0,.5);
  verify(paint_ui::nativeKey(&o,'=',false),"native softness key accepted");
  verify(paint_ui::getDouble(handle,"paint_softness")==.5&&paintUndoBegins==0&&paintRedraws==1,"native key only schedules a callback");
  inPaintCallback=true;verify(paint_ui::overlay(kOfxInteractActionGainFocus,o.handle,nullptr,nullptr)==kOfxStatOK,"host focus callback drains queued key");inPaintCallback=false;
  verify(paint_ui::getDouble(handle,"paint_softness")==.6&&paintUndoBegins==1&&paintUndoDepth==0,"key edit is balanced inside host callback");
  o.drawing=true;
  verify(paint_ui::nativeKey(&o,'v',false)&&paint_ui::nativeKey(&o,'-',false),"keys accepted before delayed mouse-up");
  inPaintCallback=true;verify(paint_ui::overlay(kOfxInteractActionGainFocus,o.handle,nullptr,nullptr)==kOfxStatOK,"host redraw during open gesture");inPaintCallback=false;
  verify(paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolPaint&&paint_ui::getDouble(handle,"paint_softness")==.6,"open gesture keeps queued keys pending");
  inPaintCallback=true;verify(paint_ui::overlay(kOfxInteractActionLoseFocus,o.handle,nullptr,nullptr)==kOfxStatOK,"host completes delayed gesture");inPaintCallback=false;
  verify(paint_ui::getInt(handle,"paint_tool")==paint_ui::ToolSelect&&paint_ui::getDouble(handle,"paint_softness")==.5&&o.pendingKeys.empty(),"queued tool and softness changes survive gesture completion");
  verify(paintUndoDepth==0&&paintWrongContext==0,"all paint undo groups stay inside host callbacks");
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
  verify(paint_ui::nativeKey(&o,paint_input::UndoKey,false),"viewer undo is accepted");
  verify(paint_ui::overlay(kOfxInteractActionGainFocus,o.handle,nullptr,nullptr)==kOfxStatOK,"undo is applied in host callback");
  verify(paint_ui::getText(handle,"paint_data")==edited&&paint_ui::getInt(handle,"paint_stroke")==1,"undo delete restores exact properties and selection");
  verify(paint_ui::nativeKey(&o,paint_input::RedoKey,false),"viewer redo is accepted");
  verify(paint_ui::overlay(kOfxInteractActionGainFocus,o.handle,nullptr,nullptr)==kOfxStatOK,"redo is applied in host callback");
  verify(paint_ui::getText(handle,"paint_data")==deleted,"redo delete restores exact document");
  paint_ui::restorePaint(handle,false);paint_ui::command(handle,paint_ui::Action::SoftDown);
  verify(!paint_ui::canPaintUndo(handle,true),"new paint edit clears the redo branch");
  for(int n=0;n<80;++n)paint_ui::command(handle,n%2?paint_ui::Action::SizeDown:paint_ui::Action::SizeUp);
  verify(paint_ui::histories.at(handle).undo.size()<=64,"paint history count is bounded");
  inPaintCallback=false;paint_ui::histories.erase(handle);
  std::cout<<"viewer paint undo/redo, property restoration and bounds: passed\n";

  interacts=savedInteract;pas.paramSetValue=nullptr;pas.paramEditBegin=nullptr;pas.paramEditEnd=nullptr;pas.paramGetPropertySet=nullptr;
  for(auto&saved:savedParameters)e.parameters.at(saved.first)=saved.second;
  std::cout<<"native key scheduling and delayed mouse-up: passed\n";
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
  Properties changed,ignored;
  setString(H(changed),kOfxPropType,0,kOfxTypeClip);setString(H(changed),kOfxPropName,0,"Source");setString(H(changed),kOfxPropChangeReason,0,kOfxChangeTime);
  callback(kOfxActionInstanceChanged,H(changed),ignored,kOfxStatReplyDefault);
  setString(H(changed),kOfxPropType,0,kOfxTypeParameter);setString(H(changed),kOfxPropName,0,"enabled");setString(H(changed),kOfxPropChangeReason,0,kOfxChangeUserEdited);
  callback(kOfxActionInstanceChanged,H(changed),ignored,kOfxStatReplyDefault);
  verify(instance(handle).sequence==sequenceBefore&&instance(handle).generation==generationBefore,"metadata and ignored callbacks preserve engine state");
  std::cout<<"concurrent host metadata/save/bracket/ignored-change callbacks passed\n";
 };
 verify(e.props.strings[kOfxImageEffectPluginRenderThreadSafety][0]==kOfxImageEffectRenderInstanceSafe,"instances are protected by the plugin lock");
 // A genuinely cold late-frame request must replay exactly and stay within its declaration.
 renderFrame(count-1);
 for(int f=0;f<count;++f){renderFrame(f);auto before=instance(handle).sequence;verify(plugin->mainEntry(kOfxImageEffectActionEndSequenceRender,handle,ip,nullptr)==0,"end bracket");verify(instance(handle).sequence==before,"end bracket preserves bounded history");}
 verify(messageCalls==0,"normal rendering does not call host logging or UI callbacks");
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
  data={""};mode={0};renderFrame(f);
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
  for(int spanish:{0,1})for(int painted:{0,1}){
   data={painted?paint::encode(doc):""};setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,spanish);
   Properties needed;verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(needed))==0,"prepared dependencies");
   verify(needed.doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>{100,100},"prepared input needs only the current frame");declaredFrameRange[0]=declaredFrameRange[1]=100;fetched.clear();
   {auto status=plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);if(status)std::cerr<<lastHostMessage<<"\n";verify(status==0,"prepared render");}
   verify(!instance(handle).sequence&&!instance(handle).history,"prepared painting never creates the cleaning engine sequence");
   verify(fetched==std::vector<int>{100,100},"prepared painting fetches only the processed frame");
   Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),100);const bool useOriginal=spanish?!painted:painted;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c)verify(result.read(x,y,c)==(useOriginal?.375f:sourcePixels[(size_t(y)*w+x)*3+c]),"prepared original/processed endpoints exact");
  }
  data={""};setInt(H(e.parameters["mask_mode"].props),kOfxParamPropDefault,0,0);
  interacts=nullptr;verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0,"prepared source rejected outside Resolve");
  setInt(H(e.parameters["paint_source_kind"].props),kOfxParamPropDefault,0,0);renderFrame(0);
  std::cout<<"Prepared original: exact Recovery/Spanish endpoints, current frame only, no engine sequence, host gating passed\n";
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
