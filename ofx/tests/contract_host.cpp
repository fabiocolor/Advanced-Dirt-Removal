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
OfxPropertySetHandle strictPreferences=nullptr;int forbiddenPreferenceWrites=0;
OfxStatus setString(OfxPropertySetHandle p,const char*k,int i,const char*v){if(std::strcmp(k,kOfxPropPluginDescription)==0||(rejectLabel&&std::strcmp(k,kOfxPropLabel)==0))return kOfxStatErrUnknown;if(p==strictPreferences&&(std::string(k).find("OfxImageClipPropDepth_")==0||std::string(k).find("OfxImageClipPropComponents_")==0)){++forbiddenPreferenceWrites;return kOfxStatErrValue;}auto&a=P(p).strings[k];a.resize(std::max(a.size(),size_t(i+1)));a[i]=v;return 0;}
OfxStatus getString(OfxPropertySetHandle p,const char*k,int i,char**v){auto it=P(p).strings.find(k);if(it==P(p).strings.end()||i<0||i>=int(it->second.size()))return kOfxStatErrUnknown;*v=it->second[i].data();return 0;}
#define ARRAY(NAME,TYPE) \
OfxStatus set##NAME##N(OfxPropertySetHandle p,const char*k,int n,const TYPE*v){for(int i=0;i<n;++i)set##NAME(p,k,i,v[i]);return 0;} \
OfxStatus get##NAME##N(OfxPropertySetHandle p,const char*k,int n,TYPE*v){for(int i=0;i<n;++i){auto s=get##NAME(p,k,i,&v[i]);if(s)return s;}return 0;}
ARRAY(Int,int) ARRAY(Double,double)
struct Parameter {Properties props;bool choice=false;bool text=false;};
struct TestImage {Properties props;std::vector<float> backing;};
struct Clip {Properties props;std::map<int,std::unique_ptr<TestImage>> images;bool output=false;};
struct Effect {Properties props;std::map<std::string,Parameter> parameters;Clip source,output;};
Effect& E(OfxImageEffectHandle e){return *reinterpret_cast<Effect*>(e);}
std::vector<int> fetched;int acquired=0,released=0,abortCount=0;bool abortEnabled=false,slowImageOnce=false;int missing=-999;
OfxStatus effectProps(OfxImageEffectHandle e,OfxPropertySetHandle*p){*p=H(E(e).props);return 0;}
OfxStatus paramSet(OfxImageEffectHandle e,OfxParamSetHandle*p){*p=reinterpret_cast<OfxParamSetHandle>(&E(e).parameters);return 0;}
OfxStatus defineClip(OfxImageEffectHandle e,const char*n,OfxPropertySetHandle*p){*p=H(std::string(n)=="Source"?E(e).source.props:E(e).output.props);return 0;}
OfxStatus clipHandle(OfxImageEffectHandle e,const char*n,OfxImageClipHandle*c,OfxPropertySetHandle*p){auto&clip=std::string(n)=="Source"?E(e).source:E(e).output;*c=reinterpret_cast<OfxImageClipHandle>(&clip);if(p)*p=H(clip.props);return 0;}
OfxStatus clipProps(OfxImageClipHandle c,OfxPropertySetHandle*p){*p=H(reinterpret_cast<Clip*>(c)->props);return 0;}
OfxStatus getImage(OfxImageClipHandle c,double t,const OfxRectD*,OfxPropertySetHandle*p){auto&clip=*reinterpret_cast<Clip*>(c);if(duringImage){auto callback=std::move(duringImage);duringImage=nullptr;callback();}if(slowImageOnce){slowImageOnce=false;std::this_thread::sleep_for(std::chrono::milliseconds(5300));}if(!clip.output)fetched.push_back(int(t));if(!clip.output&&int(t)==missing)return kOfxStatFailed;auto it=clip.images.find(int(t));if(it==clip.images.end())return kOfxStatFailed;*p=H(it->second->props);++acquired;return 0;}
OfxStatus releaseImage(OfxPropertySetHandle){++released;return 0;}
OfxStatus rod(OfxImageClipHandle,double,OfxRectD*r){*r={-7,11,-3,14};return 0;}
int aborted(OfxImageEffectHandle){return abortEnabled&&++abortCount>12;}
OfxStatus defineParameter(OfxParamSetHandle s,const char*t,const char*n,OfxPropertySetHandle*p){auto&v=(*reinterpret_cast<std::map<std::string,Parameter>*>(s))[n];v.choice=std::string(t)==kOfxParamTypeChoice||std::string(t)==kOfxParamTypeBoolean||std::string(t)==kOfxParamTypeInteger;v.text=std::string(t)==kOfxParamTypeString;*p=H(v.props);return 0;}
OfxStatus parameterHandle(OfxParamSetHandle s,const char*n,OfxParamHandle*h,OfxPropertySetHandle*p){auto&v=(*reinterpret_cast<std::map<std::string,Parameter>*>(s))[n];*h=reinterpret_cast<OfxParamHandle>(&v);if(p)*p=H(v.props);return 0;}
OfxStatus value(OfxParamHandle h,OfxTime t,...){auto&p=*reinterpret_cast<Parameter*>(h);va_list a;va_start(a,t);if(p.text){auto&s=p.props.strings[kOfxParamPropDefault];if(s.empty())s.push_back("");*va_arg(a,char**)=s[0].data();}else if(p.choice)*va_arg(a,int*)=p.props.ints[kOfxParamPropDefault][0];else *va_arg(a,double*)=p.props.doubles[kOfxParamPropDefault][0];va_end(a);return 0;}
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
void configureSuites(){ts.getTime=timeCursor;ts.getTimeBounds=timeBounds;ms.message=message;ps.propSetInt=setInt;ps.propGetInt=getInt;ps.propSetDouble=setDouble;ps.propGetDouble=getDouble;ps.propSetString=setString;ps.propGetString=getString;ps.propSetPointer=setPointer;ps.propGetPointer=getPointer;ps.propSetIntN=setIntN;ps.propGetIntN=getIntN;ps.propSetDoubleN=setDoubleN;ps.propGetDoubleN=getDoubleN;pas.paramDefine=defineParameter;pas.paramGetHandle=parameterHandle;pas.paramGetValueAtTime=value;es.getPropertySet=effectProps;es.getParamSet=paramSet;es.clipDefine=defineClip;es.clipGetHandle=clipHandle;es.clipGetPropertySet=clipProps;es.clipGetImage=getImage;es.clipReleaseImage=releaseImage;es.clipGetRegionOfDefinition=rod;es.abort=aborted;}
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
 environment("ADR_BACKEND",argv[1]);
 // The diagnostic log is on by default; keep the test's log in a private folder and check it at the end.
 const auto logFolder=std::filesystem::temp_directory_path()/("adr-contract-log-"+std::to_string(adrlog::pid()));std::filesystem::remove_all(logFolder);environment("ADR_LOG",logFolder.u8string());int w=std::stoi(argv[4]),h=std::stoi(argv[5]),count=std::stoi(argv[6]);
 auto sourcePixels=readPixels(argv[2],size_t(count)*h*w*3),targetPixels=readPixels(argv[3],size_t(count)*h*w*3);
 configureSuites();Properties hostProperties;OfxHost provider{H(hostProperties),fetch};auto*plugin=OfxGetPlugin(0);plugin->setHost(&provider);verify(plugin->mainEntry(kOfxActionLoad,nullptr,nullptr,nullptr)==0,"load");
 Effect e;e.output.output=true;auto handle=reinterpret_cast<OfxImageEffectHandle>(&e);Properties in,out;auto ip=H(in);setString(ip,kOfxImageEffectPropContext,0,kOfxImageEffectContextFilter);
 rejectLabel=true;verify(plugin->mainEntry(kOfxActionDescribe,handle,nullptr,nullptr)==kOfxStatErrUnknown,"descriptor rejection returns status without instance message");verify(descriptorMessages==0,"no descriptor messages");rejectLabel=false;
 verify(plugin->mainEntry(kOfxActionDescribe,handle,nullptr,nullptr)==0,"describe with absent optional description");verify(plugin->mainEntry(kOfxImageEffectActionDescribeInContext,handle,ip,nullptr)==0,"context");verify(e.parameters.size()==39,"all controls, groups and pages exposed");for(auto pg:{"page_cleaning","page_recovery","page_view"})verify(e.parameters.count(pg)==1,"inspector page defined");for(auto g:{"detection","processing","safeguards"})verify(e.parameters.count(g)==1,"control group defined");verify(e.parameters["sensitivity"].props.strings[kOfxParamPropParent]==std::vector<std::string>{"detection"},"controls are grouped");verify(plugin->mainEntry(kOfxActionCreateInstance,handle,nullptr,nullptr)==0,"create");
 // ADR_TEST_CONTROLS="name=value;...": render with these control values (by OFX parameter name) instead of the defaults.
 if(const char*overrides=std::getenv("ADR_TEST_CONTROLS")){std::string list=overrides;size_t start=0;while(start<list.size()){size_t end=list.find(';',start);if(end==std::string::npos)end=list.size();std::string item=list.substr(start,end-start);start=end+1;if(item.empty())continue;auto eq=item.find('=');verify(eq!=std::string::npos,"ADR_TEST_CONTROLS item needs name=value");auto it=e.parameters.find(item.substr(0,eq));verify(it!=e.parameters.end(),"ADR_TEST_CONTROLS names an existing control");const double v=std::stod(item.substr(eq+1));if(it->second.choice)setInt(H(it->second.props),kOfxParamPropDefault,0,int(v));else setDouble(H(it->second.props),kOfxParamPropDefault,0,v);std::cout<<"control "<<item<<"\n";}}
 double range[]={100,100.+count-1};setDoubleN(H(e.source.props),kOfxImageEffectPropFrameRange,2,range);setDouble(H(e.source.props),kOfxImageEffectPropFrameRate,0,24);
 for(int f=0;f<count;++f){e.source.images[f+100]=frameImage(sourcePixels,f,w,h,f%2,false);e.output.images[f+100]=frameImage(sourcePixels,f,w,h,f%2==0,true);}
 double scale[]={1,1};setDoubleN(ip,kOfxImageEffectPropRenderScale,2,scale);int bounds[]={-7,11,w-7,h+11};setIntN(ip,kOfxImageEffectPropRenderWindow,4,bounds);setString(ip,kOfxImageEffectPropFieldToRender,0,kOfxImageFieldNone);
 auto renderFrame=[&](int f){setDouble(ip,kOfxPropTime,0,f+100);auto status=plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr);if(status)std::cerr<<lastHostMessage<<'\n';verify(status==0,"engine render");Image result(reinterpret_cast<OfxImageClipHandle>(&e.output),f+100);Image original(reinterpret_cast<OfxImageClipHandle>(&e.source),f+100);for(int y=0;y<h;++y)for(int x=0;x<w;++x){for(int c=0;c<3;++c)verify(result.read(x,y,c)==targetPixels[((size_t(f)*h+y)*w+x)*3+c],"engine RGB equality");verify(result.read(x,y,3)==original.read(x,y,3),"alpha preservation");}for(int y=0;y<h;++y)for(int pad=0;pad<5;++pad)verify(e.output.images[f+100]->backing[size_t(y)*(w*4+5)+w*4+pad]==-1234,"padding preservation");std::cout<<"frame "<<f<<" exact\n";};
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
 for(int f=0;f<count;++f){renderFrame(f);auto before=instance(handle).sequence;verify(plugin->mainEntry(kOfxImageEffectActionEndSequenceRender,handle,ip,nullptr)==0,"end bracket");verify(instance(handle).sequence==before,"end bracket preserves bounded history");}
 verify(messageCalls==0,"normal rendering does not call host logging or UI callbacks");
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
 setDouble(ip,kOfxPropTime,0,100+count/2);verify(plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded,handle,ip,H(out))==0,"frames needed");verify(P(H(out)).doubles["OfxImageClipPropFrameRange_Source"]==std::vector<double>({100.,double(std::min(100+count-1,104+count/2))}),"declared complete motion history");
 verify(plugin->mainEntry(kOfxActionInstanceChanged,handle,ip,nullptr)==kOfxStatReplyDefault,"invalidate");verify(!instance(handle).sequence,"edited source releases the sequence");
 auto saved=e.output.images[100]->backing;missing=101;setDouble(ip,kOfxPropTime,0,100);verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0,"missing temporal frame fails");verify(e.output.images[100]->backing==saved,"failure publishes no pixels");verify(!instance(handle).sequence,"failed render releases the sequence");missing=-999;
 setDouble(ip,kOfxPropTime,0,100.5);verify(plugin->mainEntry(kOfxImageEffectActionRender,handle,ip,nullptr)!=0,"fractional time rejected");
 verify(plugin->mainEntry(kOfxActionDestroyInstance,handle,nullptr,nullptr)==0,"destroy");verify(acquired==released,"all image handles released");verify(plugin->mainEntry(kOfxActionUnload,nullptr,nullptr,nullptr)==0,"unload");
 {std::vector<std::filesystem::path> logs;for(auto&f:std::filesystem::directory_iterator(logFolder))logs.push_back(f.path());verify(logs.size()==1,"one diagnostic log per process");
  std::string text;{std::ifstream in(logs[0]);text.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());}
  for(auto want:{"Advanced Dirt Removal ","plugin binary: ","host: ","system: ","environment overrides: ","GPU: ","instance #1 created","sequence start: 512x512","frame 100 (1/21) rendered","history reset","ERROR","host failed to provide required in-range image","fractional frame times","sequence ended","instance #1 destroyed","unload"})
   if(text.find(want)==std::string::npos){std::cerr<<"FAIL: diagnostic log lacks \""<<want<<"\"\n"<<text;std::exit(1);}
  verify(lastHostMessage.find("Details in the Advanced Dirt Removal log: ")!=std::string::npos,"host error message names the log");
  environment("ADR_LOG","off");verify(plugin->mainEntry(kOfxActionLoad,nullptr,nullptr,nullptr)==0,"reload");verify(plugin->mainEntry(kOfxActionUnload,nullptr,nullptr,nullptr)==0,"unload again");
  size_t after=0;for(auto&f:std::filesystem::directory_iterator(logFolder)){(void)f;++after;}verify(after==1,"ADR_LOG=off writes nothing");
  if(std::getenv("ADR_SHOW_LOG"))std::cout<<text;
  std::cout<<"diagnostic log: "<<text.size()<<" bytes, "<<std::count(text.begin(),text.end(),'\n')<<" lines\n";std::filesystem::remove_all(logFolder);}
 std::cout<<"contract passed\n";
}
