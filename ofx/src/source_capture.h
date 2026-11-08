// Resolve's source-track preparer uses this pass-through filter in a temporary
// timeline. It writes original pixels, independently of every cleaning engine.
#include "ofxImageEffect.h"
#include "ofxProperty.h"
#include "ofxParam.h"
#include "ofxMessage.h"
#include "paint_source.h"
#include "diagnostic_log.h"
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace adr_source_capture {
OfxHost* host=nullptr;OfxImageEffectSuiteV1* fx=nullptr;OfxPropertySuiteV1* prop=nullptr;OfxParameterSuiteV1* param=nullptr;OfxMessageSuiteV2* messages=nullptr;
void checked(OfxStatus s,const char*where){if(s!=kOfxStatOK)throw std::runtime_error(std::string("Source capture ")+where+": OFX status "+std::to_string(s));}
#define check(call) checked((call),#call)
void require(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int integer(OfxPropertySetHandle p,const char*k,int index=0){int v;check(prop->propGetInt(p,k,index,&v));return v;}
double number(OfxPropertySetHandle p,const char*k,int index=0){double v;auto status=prop->propGetDouble(p,k,index,&v);if(status!=kOfxStatOK)throw std::runtime_error(std::string("Capture property ")+k+": OFX status "+std::to_string(status));require(std::isfinite(v),"Nonfinite capture metadata");return v;}
std::string text(OfxPropertySetHandle p,const char*k){char*v=nullptr;check(prop->propGetString(p,k,0,&v));return v?v:"";}
OfxPropertySetHandle props(OfxImageEffectHandle e){OfxPropertySetHandle p;check(fx->getPropertySet(e,&p));return p;}
struct State{OfxImageClipHandle source=nullptr,output=nullptr;};
State& state(OfxImageEffectHandle e){void*p=nullptr;check(prop->propGetPointer(props(e),kOfxPropInstanceData,0,&p));require(p,"Source capture instance missing");return *static_cast<State*>(p);}
struct Image{
 OfxPropertySetHandle p=nullptr;char*data=nullptr;std::array<int,4>b{};int stride=0,channels=0;
 Image(OfxImageClipHandle clip,double time){check(fx->clipGetImage(clip,time,nullptr,&p));try{require(text(p,kOfxImageEffectPropPixelDepth)==kOfxBitDepthFloat,"Source preparation requires float RGB/RGBA");auto c=text(p,kOfxImageEffectPropComponents);channels=c==kOfxImageComponentRGBA?4:c==kOfxImageComponentRGB?3:0;require(channels!=0,"Source preparation requires RGB/RGBA");check(prop->propGetIntN(p,kOfxImagePropBounds,4,b.data()));int rod[4];check(prop->propGetIntN(p,kOfxImagePropRegionOfDefinition,4,rod));require(std::equal(b.begin(),b.end(),rod),"Source preparation requires full-frame images");require(b[2]>b[0]&&b[3]>b[1]&&b[2]-b[0]<=16384&&b[3]-b[1]<=16384,"Invalid capture geometry");stride=integer(p,kOfxImagePropRowBytes);require(std::abs(int64_t(stride))>=int64_t(b[2]-b[0])*channels*4,"Invalid capture row stride");void*v=nullptr;check(prop->propGetPointer(p,kOfxImagePropData,0,&v));data=static_cast<char*>(v);require(data,"Capture image pixels missing");}catch(...){fx->clipReleaseImage(p);p=nullptr;throw;}}
 ~Image(){if(p)fx->clipReleaseImage(p);}Image(const Image&)=delete;
 float read(int x,int y,int c)const{float v;std::memcpy(&v,data+int64_t(b[3]-b[1]-1-y)*stride+(int64_t(x)*channels+c)*4,4);return v;}
 void write(int x,int y,int c,float v)const{std::memcpy(data+int64_t(b[3]-b[1]-1-y)*stride+(int64_t(x)*channels+c)*4,&v,4);}
};
void setHost(OfxHost*h){host=h;}
OfxStatus entry(const char*action,const void*handle,OfxPropertySetHandle in,OfxPropertySetHandle out){auto e=(OfxImageEffectHandle)handle;try{
 if(!std::strcmp(action,kOfxActionLoad)){
  fx=(OfxImageEffectSuiteV1*)host->fetchSuite(host->host,kOfxImageEffectSuite,1);prop=(OfxPropertySuiteV1*)host->fetchSuite(host->host,kOfxPropertySuite,1);param=(OfxParameterSuiteV1*)host->fetchSuite(host->host,kOfxParameterSuite,1);messages=(OfxMessageSuiteV2*)host->fetchSuite(host->host,kOfxMessageSuite,2);require(fx&&prop&&param,"Source preparation needs Resolve OFX suites");
  auto name=text(host->host,kOfxPropName),exe=adrlog::executablePath();std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});std::transform(exe.begin(),exe.end(),exe.begin(),[](unsigned char c){return char(std::tolower(c));});require(name.find("resolve")!=std::string::npos||exe.find("resolve")!=std::string::npos,"Recovery Source Capture is supported inside DaVinci Resolve only");return kOfxStatOK;
 }
 if(!std::strcmp(action,kOfxActionDescribe)){auto p=props(e);check(prop->propSetString(p,kOfxPropLabel,0,"ADR Recovery Source Capture"));check(prop->propSetString(p,kOfxImageEffectPluginPropGrouping,0,"FabioColor / Setup"));for(int j=0;j<2;++j)check(prop->propSetString(p,kOfxImageEffectPropSupportedContexts,j,j?kOfxImageEffectContextGeneral:kOfxImageEffectContextFilter));check(prop->propSetString(p,kOfxImageEffectPropSupportedPixelDepths,0,kOfxBitDepthFloat));check(prop->propSetInt(p,kOfxImageEffectPropSupportsTiles,0,0));check(prop->propSetInt(p,kOfxImageEffectPropSupportsMultiResolution,0,0));check(prop->propSetInt(p,kOfxImageEffectPluginPropHostFrameThreading,0,0));check(prop->propSetString(p,kOfxImageEffectPluginRenderThreadSafety,0,kOfxImageEffectRenderInstanceSafe));return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionDescribeInContext)){
  for(auto name:{"Source","Output"}){OfxPropertySetHandle p;check(fx->clipDefine(e,name,&p));check(prop->propSetString(p,kOfxImageEffectPropSupportedComponents,0,kOfxImageComponentRGBA));check(prop->propSetString(p,kOfxImageEffectPropSupportedComponents,1,kOfxImageComponentRGB));check(prop->propSetInt(p,kOfxImageEffectPropSupportsTiles,0,0));}
  OfxParamSetHandle ps;check(fx->getParamSet(e,&ps));OfxPropertySetHandle p;check(param->paramDefine(ps,kOfxParamTypeString,"folder",&p));check(prop->propSetString(p,kOfxPropLabel,0,"Additional source folder"));check(prop->propSetString(p,kOfxParamPropDefault,0,""));check(prop->propSetString(p,kOfxParamPropStringMode,0,kOfxParamStringIsDirectoryPath));check(prop->propSetInt(p,kOfxParamPropAnimates,0,0));
  check(param->paramDefine(ps,kOfxParamTypeInteger,"frame_shift",&p));check(prop->propSetString(p,kOfxPropLabel,0,"Source frame shift"));check(prop->propSetInt(p,kOfxParamPropDefault,0,0));check(prop->propSetInt(p,kOfxParamPropMin,0,-1000000000));check(prop->propSetInt(p,kOfxParamPropMax,0,1000000000));check(prop->propSetInt(p,kOfxParamPropAnimates,0,0));check(param->paramDefine(ps,kOfxParamTypeDouble,"source_fps",&p));check(prop->propSetDouble(p,kOfxParamPropDefault,0,24));check(prop->propSetDouble(p,kOfxParamPropMin,0,.001));check(prop->propSetDouble(p,kOfxParamPropMax,0,1000));check(prop->propSetInt(p,kOfxParamPropAnimates,0,0));return kOfxStatOK;
 }
 if(!std::strcmp(action,kOfxActionCreateInstance)){auto s=std::make_unique<State>();check(fx->clipGetHandle(e,"Source",&s->source,nullptr));check(fx->clipGetHandle(e,"Output",&s->output,nullptr));check(prop->propSetPointer(props(e),kOfxPropInstanceData,0,s.release()));return kOfxStatOK;}
 if(!std::strcmp(action,kOfxActionDestroyInstance)){delete &state(e);return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionGetRegionOfDefinition)){OfxRectD r;check(fx->clipGetRegionOfDefinition(state(e).source,number(in,kOfxPropTime),&r));double v[]{r.x1,r.y1,r.x2,r.y2};check(prop->propSetDoubleN(out,kOfxImageEffectPropRegionOfDefinition,4,v));return kOfxStatOK;}
 if(!std::strcmp(action,kOfxImageEffectActionRender)){
  if(messages)messages->clearPersistentMessage(e);auto&s=state(e);double time=number(in,kOfxPropTime);require(time==std::round(time)&&std::abs(time)<=1e9,"Fractional capture frame is unsupported");Image source(s.source,time),dest(s.output,time);require(source.b==dest.b,"Capture output differs from source geometry");char*field=nullptr;if(prop->propGetString(in,kOfxImageEffectPropFieldToRender,0,&field)==kOfxStatOK&&field)require(std::string(field)==kOfxImageFieldNone,"Prepare progressive full frames before recovery");int window[4];check(prop->propGetIntN(in,kOfxImageEffectPropRenderWindow,4,window));require(window[0]>=dest.b[0]&&window[1]>=dest.b[1]&&window[2]<=dest.b[2]&&window[3]<=dest.b[3]&&window[2]>=window[0]&&window[3]>=window[1],"Invalid capture render window");int w=source.b[2]-source.b[0],h=source.b[3]-source.b[1];double sx=number(in,kOfxImageEffectPropRenderScale,0),sy=number(in,kOfxImageEffectPropRenderScale,1);require(sx>0&&sy>0,"Invalid capture scale");
  std::vector<float> pixels(size_t(w)*h*3);for(int y=0;y<h;++y){if(fx->abort(e))return kOfxStatFailed;for(int x=0;x<w;++x){for(int c=0;c<3;++c){float v=source.read(x,y,c);require(std::isfinite(v),"Nonfinite capture input");pixels[(size_t(y)*w+x)*3+c]=v;int hostX=x+dest.b[0],hostY=dest.b[3]-1-y;if(hostX>=window[0]&&hostX<window[2]&&hostY>=window[1]&&hostY<window[3])dest.write(x,y,c,v);}if(dest.channels==4&&x+dest.b[0]>=window[0]&&x+dest.b[0]<window[2]&&dest.b[3]-1-y>=window[1]&&dest.b[3]-1-y<window[3])dest.write(x,y,3,source.channels==4?source.read(x,y,3):1.f);}}
  if(sx==1&&sy==1){OfxParamSetHandle ps;check(fx->getParamSet(e,&ps));OfxParamHandle p;char*folder=nullptr;int shift=0;check(param->paramGetHandle(ps,"folder",&p,nullptr));check(param->paramGetValue(p,&folder));check(param->paramGetHandle(ps,"frame_shift",&p,nullptr));check(param->paramGetValue(p,&shift));require(folder&&*folder,"Choose an additional-source folder before preparation");long long frame=static_cast<long long>(time)+shift;require(std::abs(double(frame))<=1e9,"Capture frame shift exceeds supported range");OfxPropertySetHandle cp;check(fx->clipGetPropertySet(s.source,&cp));double fps=0;check(param->paramGetHandle(ps,"source_fps",&p,nullptr));check(param->paramGetValue(p,&fps));double hostFps=0;if(prop->propGetDouble(cp,kOfxImageEffectPropFrameRate,0,&hostFps)==kOfxStatOK&&hostFps>0)require(std::abs(hostFps-fps)<1e-8,"Configured capture frame rate differs from host source");paint_source::writeSourceInfo(folder,w,h,fps);bool written=paint_source::write(folder,int(frame),w,h,pixels,pixels);adrlog::info("Recovery Source Capture frame "+std::to_string(frame)+(written?" written":" reused"));}
  return kOfxStatOK;
 }
 return kOfxStatReplyDefault;
 }catch(const std::exception&x){adrlog::error(std::string("Recovery Source Capture ")+action+": "+x.what());if(messages&&e&&!std::strcmp(action,kOfxImageEffectActionRender))messages->setPersistentMessage(e,kOfxMessageError,"recovery-source","%s",x.what());return kOfxStatFailed;}}
#ifndef ADR_CAPTURE_ID
#define ADR_CAPTURE_ID "com.fabiocolor.adr.sourcecapture"
#endif
OfxPlugin plugin{kOfxImageEffectPluginApi,1,ADR_CAPTURE_ID,1,0,setHost,entry};
}
#undef check
OfxPlugin* adrCapturePlugin(){return &adr_source_capture::plugin;}
