// Explicit, per-user cleaning presets. This header is included inside the
// adapter namespace after cleaning_profiles.h. No render/lifecycle action
// reads or applies these files; existing project values always win on load.
#pragma once
namespace user_defaults {
using Path=std::filesystem::path;
const char*modeName(int mode){require(mode==0||mode==1,"Invalid defaults mode");return mode?"Spanish":"Automatic/Recovery";}
Path folder(){
#ifdef _WIN32
 if(const auto*p=_wgetenv(L"ADR_USER_DEFAULTS_DIR"))if(*p)return Path(p);
 if(const auto*p=_wgetenv(L"LOCALAPPDATA"))if(*p)return Path(p)/L"FabioColor"/L"AdvancedDirtRemoval"/L"Defaults";
#else
 if(const auto*p=std::getenv("ADR_USER_DEFAULTS_DIR"))if(*p)return std::filesystem::u8path(p);
#ifdef __APPLE__
 if(const auto*p=std::getenv("HOME"))if(*p)return std::filesystem::u8path(p)/"Library"/"Application Support"/"FabioColor"/"AdvancedDirtRemoval"/"Defaults";
#else
 if(const auto*p=std::getenv("XDG_CONFIG_HOME"))if(*p)return std::filesystem::u8path(p)/"fabiocolor"/"advanced-dirt-removal"/"defaults";
 if(const auto*p=std::getenv("HOME"))if(*p)return std::filesystem::u8path(p)/".config"/"fabiocolor"/"advanced-dirt-removal"/"defaults";
#endif
#endif
 throw Error(kOfxStatFailed,"Cannot locate this user's cleaning defaults folder");
}
Path file(const Path&root,int mode){modeName(mode);return root/(mode?"spanish.adrcfg":"automatic.adrcfg");}
void save(const Path&root,int mode,const cleaning_ui::Values&values){
 auto p=cleaning_ui::defaults();p.active=mode;modeName(mode);p.values[mode]=values;
 const auto text=cleaning_ui::encode(p); // validate before creating/changing files
 std::error_code ec;std::filesystem::create_directories(root,ec);
 require(!ec,"Cannot create the cleaning defaults folder",kOfxStatFailed);
 const auto target=file(root,mode);static std::atomic<uint64_t> serial{0};
 Path temp=target;temp+=".tmp-"+std::to_string(adrlog::pid())+"-"+std::to_string(++serial);
 try{
  {std::ofstream out(temp,std::ios::binary);out<<text<<'\n';out.flush();require(bool(out),"Cannot write cleaning defaults",kOfxStatFailed);out.close();require(!out.fail(),"Cannot close cleaning defaults",kOfxStatFailed);}
#ifdef _WIN32
  require(MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot replace saved cleaning defaults",kOfxStatFailed);
#else
  std::filesystem::rename(temp,target,ec);require(!ec,"Cannot replace saved cleaning defaults",kOfxStatFailed);
#endif
 }catch(...){std::filesystem::remove(temp,ec);throw;}
}
bool load(const Path&root,int mode,cleaning_ui::Values&values){
 const auto path=file(root,mode);std::error_code ec;const bool exists=std::filesystem::exists(path,ec);
 require(!ec,"Cannot inspect saved cleaning defaults",kOfxStatFailed);if(!exists)return false;
 const auto bytes=std::filesystem::file_size(path,ec);require(!ec&&bytes<=4096,"Saved cleaning defaults are unreadable or too large",kOfxStatFailed);
 std::ifstream in(path,std::ios::binary);require(bool(in),"Cannot read saved cleaning defaults",kOfxStatFailed);
 std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
 require(!in.bad(),"Cannot finish reading saved cleaning defaults",kOfxStatFailed);
 const auto p=cleaning_ui::decode(text);require(p.active==mode,"Saved cleaning defaults belong to a different method",kOfxStatFailed);
 values=p.values[mode];return true;
}
void status(OfxImageEffectHandle e,const std::string&text){cleaning_ui::Updating guard;paint_ui::setText(e,"cleaning_default_status",text);}
struct Edit {
 OfxParamSetHandle ps=nullptr;bool active=false;
 explicit Edit(OfxImageEffectHandle e){
  if(params->paramEditBegin&&params->paramEditEnd){check(fx->getParamSet(e,&ps),"defaults parameter set");check(params->paramEditBegin(ps,"Load cleaning settings"),"begin defaults edit");active=true;}
 }
 ~Edit(){if(active)params->paramEditEnd(ps);}
};
bool changed(OfxImageEffectHandle e,OfxPropertySetHandle in){
 if(!paintHost()||cleaning_ui::updating)return false;
 char*type=nullptr;char*name=nullptr;char*reason=nullptr;
 if(prop->propGetString(in,kOfxPropType,0,&type)!=kOfxStatOK||!type||std::strcmp(type,kOfxTypeParameter)||prop->propGetString(in,kOfxPropName,0,&name)!=kOfxStatOK||!name)return false;
 const int mode=paint_ui::getInt(e,"mask_mode");
 if(!std::strcmp(name,"mask_mode")){status(e,std::string(modeName(mode))+": defaults load explicitly");return false;}
 const bool saving=!std::strcmp(name,"cleaning_default_save"),loading=!std::strcmp(name,"cleaning_default_load"),factory=!std::strcmp(name,"cleaning_default_factory");
 if(!saving&&!loading&&!factory)return false;
 prop->propGetString(in,kOfxPropChangeReason,0,&reason);if(!reason||std::strcmp(reason,kOfxChangeUserEdited))return true;
 try{
  if(saving){save(folder(),mode,cleaning_ui::current(e));status(e,std::string("Saved ")+modeName(mode)+" user default");}
  else{
   cleaning_ui::Values values{};
   if(factory)values=cleaning_ui::defaults().values[mode];
   else if(!load(folder(),mode,values)){status(e,std::string("No saved user default for ")+modeName(mode));return true;}
   {Edit edit(e);cleaning_ui::applyCurrent(e,values);}
   status(e,std::string(factory?"Factory settings loaded for ":"User default loaded for ")+modeName(mode));
  }
  adrlog::info(instance(e).label()+" "+(saving?"saved":factory?"restored factory":"loaded user")+" defaults for "+modeName(mode));
 }catch(const std::exception&ex){
  adrlog::error(instance(e).label()+" cleaning defaults: "+ex.what());
  try{status(e,"Defaults action failed; see the error and diagnostic log");}catch(...){}
  if(messages)messages->message(e,kOfxMessageError,"adr-defaults","%s",ex.what());
 }
 return true;
}
}
