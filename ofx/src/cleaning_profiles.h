// Resolve's two cleaning setups share the published inspector controls. Only
// UI callbacks swap their values; rendering still reads those original params.
// The hidden document retains the inactive setup in projects and host history.
#pragma once
namespace cleaning_ui {
constexpr size_t count=sizeof(controls)/sizeof(controls[0])-1; // Enabled is global.
using Values=std::array<double,count>;
struct Profiles {int active=0;std::array<Values,2> values{};};
thread_local bool updating=false;
struct Updating {bool before=updating;Updating(){updating=true;}~Updating(){updating=before;}};

void validate(const Values&values){
 for(size_t n=0;n<count;++n){const auto&c=controls[n+1];const double v=values[n];
  require(std::isfinite(v)&&v>=c.minimum&&v<=c.maximum,"Saved cleaning configuration has an invalid value");
  if(c.boolean||c.integral)require(v==std::round(v),"Saved cleaning configuration has a fractional integer");
 }
}
Profiles defaults(){
 Profiles p;
 for(size_t n=0;n<count;++n)p.values[0][n]=p.values[1][n]=controls[n+1].initial;
 for(size_t n=0;n<count;++n){const std::string name=controls[n+1].name;
  if(name=="sensitivity"||name=="extra_sensitivity"||name=="analysis_detail")p.values[1][n]=1;
  else if(name=="repair_amount")p.values[1][n]=1.5;
  else if(name=="motion_guard")p.values[1][n]=0;
 }
 return p;
}
std::string encode(const Profiles&p){
 require(p.active==0||p.active==1,"Invalid cleaning configuration mode");
 std::ostringstream out;out.imbue(std::locale::classic());out<<"ADRCP1 "<<p.active<<' '<<count<<std::setprecision(17);
 for(const auto&v:p.values){validate(v);for(double x:v)out<<' '<<x;}
 return out.str();
}
Profiles decode(const std::string&text){
 Profiles p;std::istringstream in(text);in.imbue(std::locale::classic());std::string version,extra;size_t fields=0;
 require(bool(in>>version>>p.active>>fields)&&version=="ADRCP1"&&fields==count&&(p.active==0||p.active==1),"Cannot read saved cleaning configurations");
 for(auto&v:p.values){for(auto&x:v)require(bool(in>>x),"Incomplete saved cleaning configuration");validate(v);}
 require(!(in>>extra),"Unexpected saved cleaning configuration data");return p;
}
Values current(OfxImageEffectHandle e){
 Values values;
 for(size_t n=0;n<count;++n){const auto&c=controls[n+1];auto h=instance(e).parameters.at(c.name);
  if(c.boolean||c.integral){int v=0;check(params->paramGetValue(h,&v),c.name);values[n]=v;}
  else check(params->paramGetValue(h,&values[n]),c.name);
 }
 validate(values);return values;
}
void initialize(OfxImageEffectHandle e){
 instance(e).cleaningProfile={};instance(e).cleaningProfile.previousMode=paint_ui::getInt(e,"mask_mode");
}
void install(OfxImageEffectHandle e,const Profiles&p){
 const auto saved=encode(p);Updating guard;
 for(size_t n=0;n<count;++n){const auto&c=controls[n+1];const double value=p.values[p.active][n];
  if(c.boolean||c.integral)paint_ui::setInt(e,c.name,int(value));else paint_ui::setDouble(e,c.name,value);
 }
 paint_ui::setText(e,"cleaning_profiles",saved);instance(e).cleaningProfile.previousMode=p.active;
}
void applyCurrent(OfxImageEffectHandle e,const Values&values){
 validate(values);const int mode=paint_ui::getInt(e,"mask_mode");
 const auto text=paint_ui::getText(e,"cleaning_profiles");Profiles p=text.empty()?defaults():decode(text);
 if(text.empty())p.active=instance(e).cleaningProfile.previousMode;
 require((mode==0||mode==1)&&(p.active==0||p.active==1),"Invalid cleaning configuration mode");
 if(p.active!=mode)p.values[p.active]=current(e);
 p.active=mode;p.values[mode]=values;install(e,p);
}
void activate(OfxImageEffectHandle e){
 if(!paintHost()||updating)return;
 auto&state=instance(e).cleaningProfile;const int mode=paint_ui::getInt(e,"mask_mode");
 const auto text=paint_ui::getText(e,"cleaning_profiles");
 Profiles p=text.empty()?defaults():decode(text);
 const int previous=text.empty()?state.previousMode:p.active;
 require((mode==0||mode==1)&&(previous==0||previous==1),"Invalid cleaning configuration mode");
 if(mode==previous){state.previousMode=mode;return;}
 // Capture the actual visible values, including edits made since the last
 // switch. Old projects start here without any change to their loaded look.
 p.values[previous]=current(e);p.active=mode;install(e,p);
 adrlog::info(instance(e).label()+" restored "+(mode?"Spanish":"Automatic/Recovery")+" cleaning configuration");
}
bool control(const char*name){for(size_t n=1;n<=count;++n)if(!std::strcmp(name,controls[n].name))return true;return false;}
void changed(OfxImageEffectHandle e,OfxPropertySetHandle in){
 if(!paintHost()||updating)return;
 char*name=nullptr;char*type=nullptr;char*reason=nullptr;
 if(prop->propGetString(in,kOfxPropType,0,&type)!=kOfxStatOK||!type||std::strcmp(type,kOfxTypeParameter)||prop->propGetString(in,kOfxPropName,0,&name)!=kOfxStatOK||!name)return;
 prop->propGetString(in,kOfxPropChangeReason,0,&reason);
 auto&state=instance(e).cleaningProfile;
 const bool mode=!std::strcmp(name,"mask_mode"),profiles=!std::strcmp(name,"cleaning_profiles");
 if(state.depth){
  state.modeChanged|=mode;state.userMode|=mode&&reason&&!std::strcmp(reason,kOfxChangeUserEdited);
  state.profilesRestored|=profiles;state.controlsRestored|=control(name);return;
 }
 if(mode&&reason&&!std::strcmp(reason,kOfxChangeUserEdited))activate(e);
 else if(mode||profiles)state.previousMode=paint_ui::getInt(e,"mask_mode");
}
void begin(OfxImageEffectHandle e){
 if(!paintHost()||updating)return;auto&state=instance(e).cleaningProfile;
 if(state.depth++==0)state.modeChanged=state.userMode=state.controlsRestored=state.profilesRestored=false;
}
void end(OfxImageEffectHandle e){
 if(!paintHost()||updating)return;auto&state=instance(e).cleaningProfile;
 if(!state.depth||--state.depth)return;
 const bool switchMode=state.modeChanged&&state.userMode&&!state.controlsRestored&&!state.profilesRestored;
 state.modeChanged=state.userMode=state.controlsRestored=state.profilesRestored=false;
 // A restored bank or explicit control values make a load/Undo/Redo batch
 // authoritative. Never replace them with a first-use preset.
 if(switchMode)activate(e);else state.previousMode=paint_ui::getInt(e,"mask_mode");
}
}
