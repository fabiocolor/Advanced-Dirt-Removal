// Painting in the host viewer: the Paint controls and the viewer overlay.
// Included by adr_ofx.cpp inside its anonymous namespace (uses its OFX suites
// and helpers). Strokes are stored in the hidden paint_data parameter, so they
// are saved with the project. Viewer Undo/Redo uses a bounded paint history.
//
// Never take the instance render lock here: hosts call these on their UI
// thread, which a render may be waiting for inside clipGetImage.
namespace paint_ui {
// Kept opt-in until native temporal OFX chains reproduce preparation exactly.
bool preparationAvailable(){const char*v=std::getenv("ADR_EXPERIMENTAL_PREPARE_SHOT");return v&&std::strcmp(v,"1")==0;}
void gestureTrace(const char* message){if(std::getenv("ADR_PAINT_INPUT_TRACE"))std::fprintf(stderr,"ADR paint gesture: %s\n",message);}
enum class Action {None,Paint,Erase,Select,Round,Square,Rectangle,SizeDown,SizeUp,SoftDown,SoftUp,OpacityDown,OpacityUp,Previous,Next,Delete,Toggle,Outlines,Mode,UndoPaint,RedoPaint,View1,View2,View3,View4};
enum Tool {ToolPaint=0,ToolErase=1,ToolSelect=2};
inline bool isParam(const char*name){return name&&!std::strncmp(name,"paint",5);}

OfxParamSetHandle paramSet(OfxImageEffectHandle e){OfxParamSetHandle ps;check(fx->getParamSet(e,&ps),"parameters");return ps;}
OfxParamHandle parameter(OfxImageEffectHandle e,const char*n){OfxParamHandle h;check(params->paramGetHandle(paramSet(e),n,&h,nullptr),n);return h;}
int getInt(OfxImageEffectHandle e,const char*n){int v=0;check(params->paramGetValue(parameter(e,n),&v),n);return v;}
double getDouble(OfxImageEffectHandle e,const char*n){double v=0;check(params->paramGetValue(parameter(e,n),&v),n);require(std::isfinite(v),n);return v;}
std::string getText(OfxImageEffectHandle e,const char*n){char*v=nullptr;check(params->paramGetValue(parameter(e,n),&v),n);return v?v:"";}
paint::Document document(OfxImageEffectHandle e){return paint::decode(getText(e,"paint_data"));}
thread_local bool updating=false;
struct Updating{bool before;Updating():before(updating){updating=true;}~Updating(){updating=before;}};
// Resolve can record even a redundant programmatic write as an edit: write only real changes.
void setInt(OfxImageEffectHandle e,const char*n,int v){if(getInt(e,n)!=v)check(params->paramSetValue(parameter(e,n),v),n);}
void setDouble(OfxImageEffectHandle e,const char*n,double v){if(getDouble(e,n)!=v)check(params->paramSetValue(parameter(e,n),v),n);}
void setText(OfxImageEffectHandle e,const char*n,const std::string&v){if(getText(e,n)!=v)check(params->paramSetValue(parameter(e,n),v.c_str()),n);}
void enable(OfxImageEffectHandle e,const char*n,bool on){if(!params->paramGetPropertySet)return;OfxPropertySetHandle p=nullptr;if(params->paramGetPropertySet(parameter(e,n),&p)==kOfxStatOK&&p)prop->propSetInt(p,kOfxParamPropEnabled,0,on?1:0);}
void store(OfxImageEffectHandle e,const paint::Document&d){setText(e,"paint_data",paint::encode(d));}
// Resolve Edit coalesces keyboard-only OFX edits into a host entry. Keep a
// bounded paint history so the explicit paint buttons undo
// exactly one stroke/property edit without touching a timeline operation.
struct PaintState {std::string data;int selected=0;};
struct PaintHistory {std::vector<PaintState> undo,redo;int depth=0;bool restoring=false;PaintState before;};
std::map<OfxImageEffectHandle,PaintHistory> histories;
PaintState paintState(OfxImageEffectHandle e){return {getText(e,"paint_data"),getInt(e,"paint_stroke")};}
void boundHistory(PaintHistory&h){
 size_t bytes=0;for(auto*list:{&h.undo,&h.redo})for(auto&v:*list)bytes+=v.data.size();
 while(h.undo.size()+h.redo.size()>64||bytes>(size_t(64)<<20)){
  auto&list=h.undo.empty()?h.redo:h.undo;bytes-=list.front().data.size();list.erase(list.begin());
 }
}
void beginPaintEdit(OfxImageEffectHandle e){auto&h=histories[e];if(h.restoring)return;if(h.depth++==0)h.before=paintState(e);}
void endPaintEdit(OfxImageEffectHandle e){
 auto&h=histories[e];if(h.restoring||h.depth==0)return;if(--h.depth)return;
 auto now=paintState(e);if(h.before.data!=now.data){h.undo.push_back(std::move(h.before));h.redo.clear();boundHistory(h);}h.before={};
}
bool canPaintUndo(OfxImageEffectHandle e,bool redo){auto it=histories.find(e);return it!=histories.end()&&!(redo?it->second.redo:it->second.undo).empty();}
thread_local bool hostChangeGroup=false;
struct Undo{
 OfxImageEffectHandle effect;OfxParamSetHandle ps;bool active=false,recording=false;
 Undo(OfxImageEffectHandle e,const char*name):effect(e),ps(paramSet(e)){
  if(!hostChangeGroup){check(params->paramEditBegin(ps,name),"begin undo");active=true;}
  try{beginPaintEdit(e);recording=true;}catch(...){if(active)params->paramEditEnd(ps);throw;}
 }
 ~Undo(){try{end();}catch(const std::exception&ex){adrlog::error(std::string("paint undo: ")+ex.what());}}
 void end(){if(recording){endPaintEdit(effect);recording=false;enable(effect,"paint_undo",canPaintUndo(effect,false));enable(effect,"paint_redo",canPaintUndo(effect,true));}if(active){check(params->paramEditEnd(ps),"end undo");active=false;}}
};
double rectWidth(const paint::Stroke&s,const paint::Document&d){return std::abs(s.points[1].x-s.points[0].x)*d.width;}
double rectHeight(const paint::Stroke&s,const paint::Document&d){return std::abs(s.points[1].y-s.points[0].y)*d.height;}
// Scales a stroke to a new size; a rectangle keeps its centre and proportions.
void resize(paint::Stroke&s,const paint::Document&d,double size,double previous){
 if(s.shape==paint::Shape::Rectangle){const double k=size/std::max(previous,1e-9);paint::rectangleSize(s,d.width,d.height,std::clamp(rectWidth(s,d)*k,1.,32768.),std::clamp(rectHeight(s,d)*k,1.,32768.));}
 s.diameter=size;
}
// Brings the label and enabled states up to date. loadBrush: show the selected stroke's values in the controls.
void refresh(OfxImageEffectHandle e,bool loadBrush=false){
 if(!params->paramGetValue||!params->paramSetValue)return;  // minimal test hosts
 Updating guard;
 // Older projects could save Select with its outlines hidden.
 if(!getInt(e,"paint_outlines")&&getInt(e,"paint_tool")==ToolSelect){
  setInt(e,"paint_tool",ToolPaint);setInt(e,"paint_stroke",0);
 }
 auto d=document(e);int selected=getInt(e,"paint_stroke");
 if(selected<0||selected>int(d.strokes.size())){selected=0;setInt(e,"paint_stroke",0);}
 const bool spanish=getInt(e,"mask_mode")==1;std::string info;
 if(selected){
  auto&s=d.strokes[selected-1];
  info="Stroke "+std::to_string(selected)+" of "+std::to_string(d.strokes.size())+": "+(s.erase?"erase":spanish?"repair":"recover")+", "+(s.shape==paint::Shape::Rectangle?"rectangle":s.shape==paint::Shape::Square?"square":"round")
   +", frame"+(s.first==s.last?" "+std::to_string(s.first):"s "+std::to_string(s.first)+"-"+std::to_string(s.last))+(s.enabled?"":" (off)");
  if(loadBrush){
   setInt(e,"paint_shape",int(s.shape));setDouble(e,"paint_size",s.diameter);setDouble(e,"paint_softness",s.softness);setDouble(e,"paint_opacity",s.opacity);
   setInt(e,"paint_hold",std::clamp(s.last-s.first+1,1,100000));setInt(e,"paint_stroke_on",s.enabled);
   if(s.shape==paint::Shape::Rectangle){setDouble(e,"paint_rect_width",std::clamp(rectWidth(s,d),1.,32768.));setDouble(e,"paint_rect_height",std::clamp(rectHeight(s,d),1.,32768.));}
  }
 }else info=d.strokes.empty()?"No strokes yet":std::string("None (")+std::to_string(d.strokes.size())+(d.strokes.size()==1?" stroke)":" strokes)");
 setText(e,"paint_stroke_info",info);
 const bool rect=selected&&d.strokes[selected-1].shape==paint::Shape::Rectangle;
 enable(e,"paint_undo",canPaintUndo(e,false));enable(e,"paint_redo",canPaintUndo(e,true));
 for(auto n:{"paint_delete","paint_stroke_on"})enable(e,n,selected!=0);
 for(auto n:{"paint_rect_width","paint_rect_height"})enable(e,n,rect);
 for(auto n:{"paint_previous","paint_next"})enable(e,n,!d.strokes.empty());
}
bool restorePaint(OfxImageEffectHandle e,bool redo){
 auto&h=histories[e];auto&from=redo?h.redo:h.undo;if(from.empty())return false;
 PaintState target=from.back(),current=paintState(e);
 // Validate before changing either the document or history.
 paint::decode(target.data);
 h.restoring=true;
 try{
  {Undo undo(e,redo?"Redo paint":"Undo paint");Updating guard;setText(e,"paint_data",target.data);setInt(e,"paint_stroke",target.selected);refresh(e,true);}
  from.pop_back();(redo?h.undo:h.redo).push_back(std::move(current));boundHistory(h);h.restoring=false;refresh(e);return true;
 }catch(...){h.restoring=false;throw;}
}
// New Resolve effects are ready to paint. A connected legacy mask with no
// built-in strokes starts on its original route until Recovery is chosen.
void initializePainting(OfxImageEffectHandle e){
 if(!paintHost()||!params->paramSetValue||!instance(e).mask||!document(e).strokes.empty())return;
 // Resolve can expose the connected original through Mask. An explicit
 // source route owns that input; it must not disable saved viewer painting.
 if(getInt(e,"paint_source_kind")!=0)return;
 OfxPropertySetHandle p=nullptr;int connected=0;
 if(fx->clipGetPropertySet(instance(e).mask,&p)==kOfxStatOK&&prop->propGetInt(p,kOfxImageClipPropConnected,0,&connected)==kOfxStatOK&&connected){Updating guard;setInt(e,"paint",0);}
}
void readyToPaint(OfxImageEffectHandle e){
 Updating guard;
 setInt(e,"paint",1);setInt(e,"paint_tool",ToolPaint);setInt(e,"paint_outlines",0);setInt(e,"paint_stroke",0);
}
void setStrokeReview(OfxImageEffectHandle e,bool show){
 if(!show){readyToPaint(e);return;}
 Updating guard;
 setInt(e,"paint",1);setInt(e,"paint_outlines",1);setInt(e,"paint_tool",ToolSelect);
}
std::map<OfxImageEffectHandle,double> cursorTimes;
void observeTime(OfxImageEffectHandle e,OfxPropertySetHandle in){double t=0;if(in&&prop->propGetDouble(in,kOfxPropTime,0,&t)==kOfxStatOK&&std::isfinite(t))cursorTimes[e]=t;}
bool playhead(OfxImageEffectHandle e,double&t){
 if(timeline&&timeline->getTime&&timeline->getTime(e,&t)==kOfxStatOK&&std::isfinite(t))return true;
 auto found=cursorTimes.find(e);if(found==cursorTimes.end())return false;t=found->second;return true;
}
using SeekFallback=bool(*)(void*,double,double,int,int);
SeekFallback fallbackSeek=paint_navigation::request;
// Previous/Next stroke: strokes in frame order. With nothing selected, start
// from the playhead. Move to the stroke and keep the active pointer tool.
void step(OfxImageEffectHandle e,const paint::Document&d,int selected,bool next){
 const int count=int(d.strokes.size());if(!count)return;
 std::vector<int> order(static_cast<size_t>(count));for(int k=0;k<count;++k)order[size_t(k)]=k;
 std::stable_sort(order.begin(),order.end(),[&](int a,int b){return d.strokes[size_t(a)].first<d.strokes[size_t(b)].first;});
 int at=-1;for(int k=0;k<count;++k)if(order[size_t(k)]==selected-1)at=k;
 double now=0;const bool known=playhead(e,now);
 if(at<0){
  if(next){at=0;if(known)for(int k=0;k<count;++k)if(d.strokes[size_t(order[size_t(k)])].last>=now){at=k;break;}}
  else{at=count-1;if(known)for(int k=count-1;k>=0;--k)if(d.strokes[size_t(order[size_t(k)])].first<=now){at=k;break;}}
 }else at=(at+(next?1:count-1))%count;
 const int chosen=order[size_t(at)];
 {Updating guard;setInt(e,"paint_stroke",chosen+1);}
 const auto&s=d.strokes[size_t(chosen)];
 {
  const auto status=timeline&&timeline->gotoTime?timeline->gotoTime(e,double(s.first)):kOfxStatErrUnsupported;
  if(status!=kOfxStatOK){
   bool queued=false;if(known&&paintHost()&&fallbackSeek){const auto range=sourceRange(instance(e));queued=fallbackSeek(e,now,s.first,range[0],range[1]);}
   if(!queued)adrlog::info("paint: host could not move to stroke frame "+std::to_string(s.first)+" (OFX status "+std::to_string(status)+")");
  }
 }
}
// Buttons and Inspector edits share the same paint history.
void command(OfxImageEffectHandle e,Action action){
 using A=Action;
 auto d=document(e);int selected=getInt(e,"paint_stroke");const bool has=selected>0&&selected<=int(d.strokes.size());const int count=int(d.strokes.size());
 switch(action){
  case A::UndoPaint:case A::RedoPaint:restorePaint(e,action==A::RedoPaint);return;
  case A::Paint:readyToPaint(e);refresh(e);return;
  case A::Select:setStrokeReview(e,true);refresh(e);return;
  case A::Erase:{Updating guard;setInt(e,"paint_tool",ToolErase);setInt(e,"paint_stroke",0);refresh(e);return;}
  case A::Previous:case A::Next:if(!count)return;step(e,d,has?selected:0,action==A::Next);refresh(e,true);return;
  case A::Outlines:setStrokeReview(e,!getInt(e,"paint_outlines"));refresh(e);return;
  case A::View1:case A::View2:case A::View3:case A::View4:{const int v=int(action)-int(A::View1);if(getInt(e,"view")==v)return;Undo undo(e,"View");Updating guard;setInt(e,"view",v);return;}
  case A::Mode:{Undo undo(e,"Recovery mode");{Updating guard;setInt(e,"mask_mode",!getInt(e,"mask_mode"));readyToPaint(e);}refresh(e);return;}
  case A::Delete:case A::Toggle:if(!has)return;break;
  default:break;
 }
 Undo undo(e,"Paint");Updating guard;
 auto value=[&](const char*n,double v){const double previous=getDouble(e,n);setDouble(e,n,v);if(!has)return;auto&s=d.strokes[selected-1];
  if(!std::strcmp(n,"paint_size"))resize(s,d,v,previous);else if(!std::strcmp(n,"paint_softness"))s.softness=v;else if(!std::strcmp(n,"paint_opacity"))s.opacity=v;};
 switch(action){
  case A::Round:case A::Square:case A::Rectangle:{auto shape=action==A::Round?paint::Shape::Round:action==A::Square?paint::Shape::Square:paint::Shape::Rectangle;setInt(e,"paint_shape",int(shape));if(has)paint::changeShape(d.strokes[selected-1],shape,d.width,d.height);break;}
  case A::SizeDown:value("paint_size",std::max(1.,getDouble(e,"paint_size")*.8));break;
  case A::SizeUp:value("paint_size",std::min(2048.,getDouble(e,"paint_size")*1.25));break;
  case A::SoftDown:value("paint_softness",std::max(0.,getDouble(e,"paint_softness")-.1));break;
  case A::SoftUp:value("paint_softness",std::min(1.,getDouble(e,"paint_softness")+.1));break;
  case A::OpacityDown:value("paint_opacity",std::max(0.,getDouble(e,"paint_opacity")-.1));break;
  case A::OpacityUp:value("paint_opacity",std::min(1.,getDouble(e,"paint_opacity")+.1));break;
  case A::Delete:d.strokes.erase(d.strokes.begin()+selected-1);setInt(e,"paint_stroke",0);break;
  case A::Toggle:d.strokes[selected-1].enabled=!d.strokes[selected-1].enabled;break;
  default:break;
 }
 store(e,d);refresh(e,true);
}
// Hosts can restore a whole parameter set in any order during Undo/Redo.
// Defer dependent brush controls until the set is complete; paint_data is
// authoritative whenever the host restores it in the same change group.
struct ChangeBatch {int depth=0;std::vector<std::pair<std::string,std::string>> names;};
std::map<OfxImageEffectHandle,ChangeBatch> changeBatches;
void applyChanged(OfxImageEffectHandle e,const std::string&name,const char*reason){
 if(updating)return;
 const bool userEdited=reason&&!std::strcmp(reason,kOfxChangeUserEdited);
 if(name=="paint_data"||name=="paint_stroke"){if(name=="paint_data")histories.erase(e);refresh(e,true);return;}
 if(name=="mask_mode"){if(userEdited)readyToPaint(e);refresh(e);return;}
 if(name=="paint_source_kind"){updateRecoveryEnabled(instance(e));refreshRecoveryUI(e);return;}
 if(name=="paint_tool"){
  const int tool=getInt(e,"paint_tool");
  if(tool==ToolSelect&&userEdited)setStrokeReview(e,true);
  else if(tool!=ToolSelect&&getInt(e,"paint_stroke")){Updating guard;setInt(e,"paint_stroke",0);}
  refresh(e);return;
 }
 if(!userEdited)return;
 if(name=="paint_outlines"){setStrokeReview(e,getInt(e,"paint_outlines")!=0);refresh(e);return;}
 if(name=="paint_resume"){readyToPaint(e);refresh(e);return;}
 if(name=="paint_previous"){command(e,Action::Previous);return;}
 if(name=="paint_next"){command(e,Action::Next);return;}
 if(name=="paint_delete"){command(e,Action::Delete);return;}
 if(name=="paint_undo"||name=="paint_redo"){command(e,name=="paint_undo"?Action::UndoPaint:Action::RedoPaint);return;}
 const auto encoded=getText(e,"paint_data");auto d=paint::decode(encoded);const int selected=getInt(e,"paint_stroke");
 if(selected<1||selected>int(d.strokes.size()))return;  // the controls set the next stroke
 auto&s=d.strokes[selected-1];
 if(name=="paint_size")resize(s,d,getDouble(e,"paint_size"),s.diameter);
 else if(name=="paint_softness")s.softness=getDouble(e,"paint_softness");
 else if(name=="paint_opacity")s.opacity=getDouble(e,"paint_opacity");
 else if(name=="paint_shape")paint::changeShape(s,paint::Shape(getInt(e,"paint_shape")),d.width,d.height);
 else if(name=="paint_rect_width"||name=="paint_rect_height"){if(s.shape!=paint::Shape::Rectangle)return;paint::rectangleSize(s,d.width,d.height,getDouble(e,"paint_rect_width"),getDouble(e,"paint_rect_height"));}
 else if(name=="paint_hold")s.last=s.first+getInt(e,"paint_hold")-1;
 else if(name=="paint_stroke_on")s.enabled=getInt(e,"paint_stroke_on")!=0;
 else return;
 // Undo, redo and project loading also arrive as user edits. A control that
 // already matches the stored stroke must not open a new edit (it would clear Redo).
 if(paint::encode(d)==encoded)return;
 Undo undo(e,"Edit stroke");store(e,d);refresh(e,true);
}
void beginChanges(OfxImageEffectHandle e){++changeBatches[e].depth;gestureTrace("begin parameter changes");}
void endChanges(OfxImageEffectHandle e){
 auto found=changeBatches.find(e);if(found==changeBatches.end())return;
 if(--found->second.depth>0)return;
 auto names=std::move(found->second.names);changeBatches.erase(found);
 if(names.empty())return;
 struct HostGroup {bool previous=hostChangeGroup;HostGroup(){hostChangeGroup=true;}~HostGroup(){hostChangeGroup=previous;}} group;
 const bool documentRestored=std::any_of(names.begin(),names.end(),[](const auto&n){return n.first=="paint_data";});
 gestureTrace(documentRestored?"restore complete stroke document":"apply grouped brush controls");
 if(documentRestored){histories.erase(e);refresh(e,true);return;}
 for(const auto&name:names)applyChanged(e,name.first,name.second.c_str());
}
// InstanceChanged for paint parameters (and Mode, which changes the wording).
void changed(OfxImageEffectHandle e,OfxPropertySetHandle in){
 if(updating)return;if(!cursorTimes.count(e))observeTime(e,in);char*name=nullptr;check(prop->propGetString(in,kOfxPropName,0,&name),"changed parameter");
 char*reason=nullptr;prop->propGetString(in,kOfxPropChangeReason,0,&reason);
 auto found=changeBatches.find(e);
 if(found!=changeBatches.end()){found->second.names.emplace_back(name?name:"",reason?reason:"");return;}
 applyChanged(e,name?name:"",reason);
}
// A predeclared manual section lets its Method control precede the brush controls.
void define(OfxParamSetHandle ps,bool visible,bool groupDefined=false){
 const char* detailsParent=groupDefined?"paint_group":nullptr;
 auto make=[&](const char*type,const char*name,const char*label,const char*parent,const char*hint,bool evaluate=false){
  OfxPropertySetHandle p;check(params->paramDefine(ps,type,name,&p),name);ss(p,kOfxPropLabel,label);if(parent)ss(p,kOfxParamPropParent,parent);if(hint)ss(p,kOfxParamPropHint,hint);
  if(std::strcmp(type,kOfxParamTypeGroup)&&std::strcmp(type,kOfxParamTypePushButton)){si(p,kOfxParamPropAnimates,0);si(p,kOfxParamPropEvaluateOnChange,evaluate);si(p,kOfxParamPropCanUndo,evaluate);}
  if(!visible)si(p,kOfxParamPropSecret,1);return p;};
 auto choice=[&](const char*name,const char*label,const char*parent,const char*hint,std::initializer_list<const char*> options){auto p=make(kOfxParamTypeChoice,name,label,parent,hint);int k=0;for(auto o:options)ss(p,kOfxParamPropChoiceOption,o,k++);si(p,kOfxParamPropDefault,0);};
 auto real=[&](const char*name,const char*label,const char*parent,const char*hint,double initial,double lo,double hi,double displayHi){auto p=make(kOfxParamTypeDouble,name,label,parent,hint);for(auto v:{std::make_pair(kOfxParamPropDefault,initial),{kOfxParamPropMin,lo},{kOfxParamPropMax,hi},{kOfxParamPropDisplayMin,lo},{kOfxParamPropDisplayMax,displayHi}})check(prop->propSetDouble(p,v.first,0,v.second),name);};
 auto boolean=[&](const char*name,const char*label,const char*parent,const char*hint,int initial,bool evaluate=false){auto p=make(kOfxParamTypeBoolean,name,label,parent,hint,evaluate);si(p,kOfxParamPropDefault,initial);};
 auto label=[&](const char*name,const char*text,const char*parent,const char*initial){auto p=make(kOfxParamTypeString,name,text,parent,nullptr);ss(p,kOfxParamPropStringMode,kOfxParamStringIsLabel);ss(p,kOfxParamPropDefault,initial);si(p,kOfxParamPropPersistant,0);};
 if(!groupDefined){auto p=make(kOfxParamTypeGroup,"paint_group","Paint",nullptr,nullptr);si(p,kOfxParamPropGroupOpen,1);}
 boolean("paint","Paint in viewer","paint_group","Paint directly in this effect's viewer (turn on Open FX Overlay). Recovery gives original detail back; Spanish dirt removal repairs only where painted. No separate Mask Paint is needed. While editing is enabled or strokes exist, the built-in brush replaces the legacy external mask.",visible?1:0,true);
 boolean("paint_outlines","Show strokes","paint_group","Show the strokes on the current frame and activate Select. Turn off to hide the outlines and return to Paint.",0);
 choice("paint_tool","Tool","paint_group","Paint, Erase (take paint away) or Select (click a stroke to select, move or resize it).",{"Paint","Erase","Select"});
 choice("paint_shape","Brush","paint_group","Round or square brush, or a rectangle dragged from corner to corner.",{"Round","Square","Rectangle"});
 real("paint_size","Size","paint_group","Brush diameter in picture pixels. Shift-drag in the viewer.",80,1,2048,400);
 real("paint_softness","Softness","paint_group","0 is a hard edge. Option/Alt-drag in the viewer.",.5,0,1,1);
 make(kOfxParamTypePushButton,"paint_undo","Undo paint","paint_group","Undo one paint stroke or property edit. History is kept for this editing session.");
 make(kOfxParamTypePushButton,"paint_redo","Redo paint","paint_group","Redo a paint edit.");
 {auto p=make(kOfxParamTypeGroup,"stroke_details_group","Strokes",detailsParent,nullptr);si(p,kOfxParamPropGroupOpen,0);}
 label("paint_stroke_info","Selected","stroke_details_group","No strokes yet");
 make(kOfxParamTypePushButton,"paint_previous","Previous stroke","stroke_details_group","Select the previous stroke in frame order and go to its frame.");
 make(kOfxParamTypePushButton,"paint_next","Next stroke","stroke_details_group","Select the next stroke in frame order and go to its frame.");
 make(kOfxParamTypePushButton,"paint_delete","Delete stroke","stroke_details_group","Delete the selected stroke. Undo brings it back.");
 make(kOfxParamTypePushButton,"paint_resume","Resume painting","stroke_details_group","Hide stroke outlines, clear the selection and enable Paint.");
 {auto p=make(kOfxParamTypeInteger,"paint_hold","Hold (frames)","stroke_details_group","How many frames a new stroke lasts, from the frame it is painted on. 1 paints only the current frame.");si(p,kOfxParamPropDefault,1);si(p,kOfxParamPropMin,1);si(p,kOfxParamPropMax,100000);si(p,kOfxParamPropDisplayMin,1);si(p,kOfxParamPropDisplayMax,250);}
 boolean("paint_stroke_on","Stroke on","stroke_details_group","Turn the selected stroke off to compare without it.",1);
 real("paint_rect_width","Rectangle width","stroke_details_group","Width of the selected rectangle in picture pixels.",80,1,32768,2048);
 real("paint_rect_height","Rectangle height","stroke_details_group","Height of the selected rectangle in picture pixels.",80,1,32768,2048);
 {auto p=make(kOfxParamTypeGroup,"paint_options_group","Brush options",detailsParent,nullptr);si(p,kOfxParamPropGroupOpen,0);}
 real("paint_opacity","Opacity","paint_options_group","How strongly the stroke applies.",1,0,1,1);
 real("paint_smoothing","Smoothing","paint_options_group","Smooths the pen path while you paint.",.2,0,.95,.95);
 boolean("paint_pressure","Tablet pressure","paint_options_group","Pen pressure changes the brush size. Leave off for a mouse.",0);
 label("paint_keys","Gestures","paint_options_group","Shift-drag: size; Option/Alt-drag: softness. Keyboard commands stay with Resolve.");
 {auto p=make(kOfxParamTypeGroup,"paint_source_group","Source setup",detailsParent,nullptr);si(p,kOfxParamPropGroupOpen,0);}
 {auto p=make(kOfxParamTypeChoice,"paint_source_kind","Paint source","paint_source_group","Clean this clip: repair this input. Additional source: paint a processed picture back to the aligned original connected to the second input.",true);ss(p,kOfxParamPropChoiceOption,"Clean this clip",0);ss(p,kOfxParamPropChoiceOption,"Additional source",1);if(preparationAvailable())ss(p,kOfxParamPropChoiceOption,"Prepared shot (experimental)",2);si(p,kOfxParamPropDefault,0);}
 {auto p=make(kOfxParamTypeString,"paint_source_folder","Original folder","paint_source_group","Run Prepare ADR Recovery Source with the processed export on V2 and the aligned original on V1. Keep the verified source folder with the project.",true);ss(p,kOfxParamPropStringMode,kOfxParamStringIsDirectoryPath);ss(p,kOfxParamPropDefault,"");si(p,kOfxParamPropSecret,1);}
 {auto p=make(kOfxParamTypeInteger,"paint_source_offset","Original frame offset","paint_source_group","Prepared frame = working frame + offset. Leave at 0 for the prepared V2/V1 workflow.",true);si(p,kOfxParamPropDefault,0);si(p,kOfxParamPropMin,-1000000000);si(p,kOfxParamPropMax,1000000000);si(p,kOfxParamPropSecret,1);}
 {auto p=make(kOfxParamTypePushButton,"prepare_shot","Prepare shot for painting","paint_source_group","Save a new exact original/cleaned snapshot of this shot for manual painting. Cleaning is calculated once; later frames need only the current input. Auto recovery is not included. Cancel preserves earlier preparations.");if(!preparationAvailable())si(p,kOfxParamPropSecret,1);}
 {auto p=make(kOfxParamTypeString,"prepare_folder","Prepared shot folder","paint_source_group","Keep this folder with the project. Prepared shot freezes the cleaning settings and temporal source at preparation time. Prepare again after upstream changes.",true);ss(p,kOfxParamPropStringMode,kOfxParamStringIsDirectoryPath);ss(p,kOfxParamPropDefault,"");if(!preparationAvailable())si(p,kOfxParamPropSecret,1);}
 {auto p=make(kOfxParamTypeInteger,"prepare_request","Preparation revision","paint_source_group",nullptr,true);si(p,kOfxParamPropDefault,0);si(p,kOfxParamPropSecret,1);}
 {auto p=make(kOfxParamTypeInteger,"paint_stroke","Selected stroke","paint_group",nullptr);si(p,kOfxParamPropDefault,0);si(p,kOfxParamPropMin,0);si(p,kOfxParamPropMax,4096);si(p,kOfxParamPropSecret,1);}
 {auto p=make(kOfxParamTypeString,"paint_data","Strokes","paint_group",nullptr,true);ss(p,kOfxParamPropDefault,"");ss(p,kOfxParamPropStringMode,kOfxParamStringIsMultiLine);si(p,kOfxParamPropSecret,1);si(p,kOfxParamPropPersistant,1);}
}

// ---- Viewer overlay ----------------------------------------------------------
struct Overlay {
 OfxImageEffectHandle effect=nullptr;OfxInteractHandle handle=nullptr;
 OfxRectD rod{};double par=1;int frame=0,interactionFrame=0;
 paint::Point cursor{},down{};bool hasCursor=false,drawing=false,moving=false,adjusting=false;
 std::array<double,2> screen{},screenDown{},pixelScale{1,1};
 int adjustment=0,corner=-1;double startSize=80,startSoftness=.5;
 unsigned modifiers=0;
 paint::Document work;size_t index=0;std::unique_ptr<Undo> undo;
};
#if defined(ADR_PAINT_TEST_HOOKS)
std::set<Overlay*> testOverlays;
#endif
void geometry(Overlay&o,double time){
 check(fx->clipGetRegionOfDefinition(instance(o.effect).source,time,&o.rod),"paint source region");
 OfxPropertySetHandle cp;check(fx->clipGetPropertySet(instance(o.effect).source,&cp),"paint source properties");
 o.par=1;prop->propGetDouble(cp,kOfxImagePropPixelAspectRatio,0,&o.par);
 require(std::isfinite(o.par)&&o.par>0&&o.rod.x2>o.rod.x1&&o.rod.y2>o.rod.y1,"invalid painting geometry");
}
void readScale(Overlay&o,OfxPropertySetHandle in){for(int j=0;j<2;++j){double scale=1;if(prop->propGetDouble(in,kOfxInteractPropPixelScale,j,&scale)==kOfxStatOK&&std::isfinite(scale)&&scale>0)o.pixelScale[j]=scale;}}
void coordinates(Overlay&o,OfxPropertySetHandle in){
 o.frame=frameTime(in);observeTime(o.effect,in);geometry(o,o.frame);
 const double x=number(in,kOfxInteractPropPenPosition,0),y=number(in,kOfxInteractPropPenPosition,1);double pressure=1;
 if(getInt(o.effect,"paint_pressure"))prop->propGetDouble(in,kOfxInteractPropPenPressure,0,&pressure);if(!std::isfinite(pressure))pressure=1;
 o.cursor={(x-o.rod.x1)/(o.rod.x2-o.rod.x1),(y-o.rod.y1)/(o.rod.y2-o.rod.y1),std::clamp(pressure,0.,1.)};o.hasCursor=true;
 // Resolve 21's PenViewportPosition is not in screen pixels on macOS. Screen
 // distance comes from canonical position / pixel scale, which also keeps
 // handles and drag sensitivity independent of the viewer zoom.
 readScale(o,in);for(int j=0;j<2;++j)o.screen[j]=(j?y:x)/o.pixelScale[j];
}
int pictureWidth(const Overlay&o){return int(std::llround((o.rod.x2-o.rod.x1)/o.par));}
int pictureHeight(const Overlay&o){return int(std::llround(o.rod.y2-o.rod.y1));}
void finishGesture(Overlay&o){
 if(!o.drawing&&!o.moving&&!o.adjusting&&!o.undo)return;
 gestureTrace("finish");o.drawing=o.moving=o.adjusting=false;o.corner=-1;o.adjustment=0;
 if(o.undo){o.undo->end();o.undo.reset();}refresh(o.effect);
}
unsigned (*modifierQuery)()=paint_input::nativeModifiers;
// macOS and Windows: the current keyboard state, sampled only during a pointer
// event the host delivered (a modifier release swallowed by a host command must
// not leave a stale key down). Elsewhere: the host's key events.
unsigned pointerModifiers(const Overlay&o){
#if defined(__APPLE__)||defined(_WIN32)
 (void)o;return modifierQuery();
#else
 unsigned r=modifierQuery();
 if(o.modifiers&3u)r|=paint_input::Shift;
 const unsigned alt=(1u<<(0xffe9-0xffe1))|(1u<<(0xffea-0xffe1));
 if(o.modifiers&alt)r|=paint_input::Alt;
 if(o.modifiers&~(3u|alt))r|=paint_input::HostCommand;
 return r;
#endif
}
// Horizontal drag: right is bigger/softer. Size doubles every 120 screen pixels.
void adjustBrush(Overlay&o){
 auto e=o.effect;const double dx=o.screen[0]-o.screenDown[0];
 const bool size=o.adjustment==1;const char*n=size?"paint_size":"paint_softness";
 const double v=size?std::clamp(o.startSize*std::exp2(dx/120.),1.,2048.):std::clamp(o.startSoftness+dx/240.,0.,1.);
 if(getDouble(e,n)==v)return;
 if(!o.undo)o.undo=std::make_unique<Undo>(e,size?"Brush size":"Brush softness");
 Updating guard;setDouble(e,n,v);auto d=o.work;const int selected=getInt(e,"paint_stroke");
 if(selected>0&&selected<=int(d.strokes.size())){auto&s=d.strokes[selected-1];if(size)resize(s,d,v,o.work.strokes[selected-1].diameter);else s.softness=v;store(e,d);}
 refresh(e,true);
}
std::array<paint::Point,4> corners(const paint::Stroke&s){const double l=std::min(s.points[0].x,s.points[1].x),r=std::max(s.points[0].x,s.points[1].x),b=std::min(s.points[0].y,s.points[1].y),t=std::max(s.points[0].y,s.points[1].y);return {{{l,b,1},{r,b,1},{r,t,1},{l,t,1}}};}
int cornerAt(const Overlay&o,const paint::Stroke&s){auto cs=corners(s);for(int j=0;j<4;++j)if(std::abs((cs[j].x-o.cursor.x)*(o.rod.x2-o.rod.x1)/o.pixelScale[0])<=8&&std::abs((cs[j].y-o.cursor.y)*(o.rod.y2-o.rod.y1)/o.pixelScale[1])<=8)return j;return -1;}
double X(const Overlay&o,double x){return o.rod.x1+x*(o.rod.x2-o.rod.x1);}
double Y(const Overlay&o,double y){return o.rod.y1+y*(o.rod.y2-o.rod.y1);}
void circle(const Overlay&o,const paint::Point&p,double radius){paint_gl::loop();for(int j=0;j<64;++j){const double a=j*6.283185307179586/64;paint_gl::vertex(X(o,p.x)+std::cos(a)*radius*o.par,Y(o,p.y)+std::sin(a)*radius);}paint_gl::end();}
void box(const Overlay&o,const paint::Point&a,const paint::Point&b){paint_gl::loop();paint_gl::vertex(X(o,a.x),Y(o,a.y));paint_gl::vertex(X(o,b.x),Y(o,a.y));paint_gl::vertex(X(o,b.x),Y(o,b.y));paint_gl::vertex(X(o,a.x),Y(o,b.y));paint_gl::end();}
void brushOutline(const Overlay&o,const paint::Point&p,double diameter,paint::Shape shape){
 const double r=diameter*.5;
 if(shape==paint::Shape::Square)box(o,{p.x-r*o.par/(o.rod.x2-o.rod.x1),p.y-r/(o.rod.y2-o.rod.y1),1},{p.x+r*o.par/(o.rod.x2-o.rod.x1),p.y+r/(o.rod.y2-o.rod.y1),1});else circle(o,p,r);
}
void draw(Overlay&o,OfxPropertySetHandle in){
 auto e=o.effect;observeTime(e,in);const double time=number(in,kOfxPropTime);geometry(o,time);readScale(o,in);
 // Read every host value before touching GL state.
 const bool outlines=getInt(e,"paint_outlines")!=0;
 const auto d=outlines?document(e):paint::Document{};
 const int selected=outlines?getInt(e,"paint_stroke"):0,tool=getInt(e,"paint_tool");
 const double diameter=getDouble(e,"paint_size"),softness=getDouble(e,"paint_softness");const auto shape=paint::Shape(getInt(e,"paint_shape"));
 if(!paint_gl::ready())return;
 paint_gl::Scope gl;
 for(size_t j=0;j<d.strokes.size();++j){
  const auto&s=d.strokes[j];if(time<s.first||time>s.last)continue;
  const bool isSelected=int(j+1)==selected;
  if(isSelected)paint_gl::color(1,.85f,0,1);else if(!s.enabled)paint_gl::color(.5f,.5f,.5f,.5f);else if(s.erase)paint_gl::color(1,.35f,.25f,.7f);else paint_gl::color(.1f,1,.7f,.7f);
  if(s.shape==paint::Shape::Rectangle){
   box(o,s.points[0],s.points[1]);
   if(isSelected&&tool==ToolSelect){const double rx=4*o.pixelScale[0]/(o.rod.x2-o.rod.x1),ry=4*o.pixelScale[1]/(o.rod.y2-o.rod.y1);for(auto q:corners(s))box(o,{q.x-rx,q.y-ry,1},{q.x+rx,q.y+ry,1});}
   continue;
  }
  paint_gl::strip();for(auto&q:s.points)paint_gl::vertex(X(o,q.x),Y(o,q.y));paint_gl::end();
  brushOutline(o,s.points.front(),s.diameter*s.points.front().pressure,s.shape);
  if(s.points.size()>1)brushOutline(o,s.points.back(),s.diameter*s.points.back().pressure,s.shape);
 }
 if(!o.hasCursor||(tool==ToolSelect&&!o.adjusting))return;
 const auto at=o.adjusting?o.down:o.cursor;
 if(shape==paint::Shape::Rectangle&&!o.adjusting){
  const double rx=7*o.pixelScale[0]/(o.rod.x2-o.rod.x1),ry=7*o.pixelScale[1]/(o.rod.y2-o.rod.y1);
  paint_gl::color(1,1,1,1);paint_gl::lines();paint_gl::vertex(X(o,at.x-rx),Y(o,at.y));paint_gl::vertex(X(o,at.x+rx),Y(o,at.y));paint_gl::vertex(X(o,at.x),Y(o,at.y-ry));paint_gl::vertex(X(o,at.x),Y(o,at.y+ry));paint_gl::end();
  return;
 }
 const auto cursorShape=shape==paint::Shape::Rectangle?paint::Shape::Round:shape;
 paint_gl::color(0,0,0,1);paint_gl::lineWidth(3);brushOutline(o,at,diameter,cursorShape);
 paint_gl::color(1,1,1,1);paint_gl::lineWidth(1);brushOutline(o,at,diameter,cursorShape);
 paint_gl::color(1,1,1,.5f);brushOutline(o,at,diameter*(1-softness),cursorShape);
}
OfxStatus penDown(Overlay&o,OfxInteractHandle h,OfxPropertySetHandle in){
 gestureTrace("down");
 auto e=o.effect;if(o.drawing||o.moving||o.adjusting)finishGesture(o);
 coordinates(o,in);if(o.cursor.x<0||o.cursor.x>1||o.cursor.y<0||o.cursor.y>1)return kOfxStatReplyDefault;
 const auto modifiers=pointerModifiers(o);if(modifiers&paint_input::HostCommand)return kOfxStatReplyDefault;  // host pan/zoom chords
 o.work=document(e);o.down=o.cursor;o.interactionFrame=o.frame;const int tool=getInt(e,"paint_tool");
 o.adjustment=modifiers&paint_input::Alt?2:modifiers&paint_input::Shift?1:0;
 if(o.adjustment){o.adjusting=true;o.screenDown=o.screen;o.startSize=getDouble(e,"paint_size");o.startSoftness=getDouble(e,"paint_softness");interacts->interactRedraw(h);return kOfxStatOK;}
 const int w=pictureWidth(o),hh=pictureHeight(o);
 if(tool==ToolSelect){
  int found=-1;o.corner=-1;const int selected=getInt(e,"paint_stroke");
  if(selected>0&&selected<=int(o.work.strokes.size())){const auto&s=o.work.strokes[selected-1];if(s.shape==paint::Shape::Rectangle&&o.frame>=s.first&&o.frame<=s.last){o.corner=cornerAt(o,s);if(o.corner>=0)found=selected-1;}}
  // Topmost stroke under the pen on this frame; switched-off and transparent strokes stay selectable.
  for(int j=int(o.work.strokes.size())-1;found<0&&j>=0;--j){auto s=o.work.strokes[j];s.opacity=1;if(o.frame>=s.first&&o.frame<=s.last&&paint::coverage(s,o.cursor.x*w,o.cursor.y*hh,w,hh)>0)found=j;}
  {Updating guard;setInt(e,"paint_stroke",found+1);}refresh(e,true);
  if(found<0){interacts->interactRedraw(h);return kOfxStatOK;}
  o.index=size_t(found);o.moving=true;
 }else{
  require(o.work.strokes.size()<4096,"Paint stroke limit reached (4096)");
  if(o.work.strokes.empty()){o.work.width=w;o.work.height=hh;}
  require(o.work.width==w&&o.work.height==hh,"The picture size changed since these strokes were painted");
  paint::Stroke s;s.id=o.work.nextId++;s.first=o.frame;s.last=o.frame+getInt(e,"paint_hold")-1;s.erase=tool==ToolErase;s.shape=paint::Shape(getInt(e,"paint_shape"));
  s.diameter=getDouble(e,"paint_size");s.softness=getDouble(e,"paint_softness");s.opacity=getDouble(e,"paint_opacity");
  s.points.push_back(o.cursor);if(s.shape==paint::Shape::Rectangle)s.points.push_back(o.cursor);
  o.work.strokes.push_back(std::move(s));o.index=o.work.strokes.size()-1;o.drawing=true;
  {Updating guard;setInt(e,"paint_stroke",0);}
 }
 o.undo=std::make_unique<Undo>(e,tool==ToolSelect?"Move stroke":"Paint stroke");
 // A new rectangle is stored once it has a size (on motion).
 if(o.drawing&&o.work.strokes[o.index].shape!=paint::Shape::Rectangle)store(e,o.work);
 interacts->interactRedraw(h);return kOfxStatOK;
}
OfxStatus penMove(Overlay&o,OfxInteractHandle h,OfxPropertySetHandle in,bool up){
 if(up)gestureTrace("up");
 auto e=o.effect;coordinates(o,in);
 if((o.drawing||o.moving||o.adjusting)&&o.frame!=o.interactionFrame){finishGesture(o);return kOfxStatOK;}
 if(o.adjusting)adjustBrush(o);
 else if(o.drawing&&o.work.strokes[o.index].shape==paint::Shape::Rectangle){
  auto&s=o.work.strokes[o.index];s.points[1]=o.cursor;
  const bool empty=std::abs(s.points[1].x-s.points[0].x)*o.work.width<1||std::abs(s.points[1].y-s.points[0].y)*o.work.height<1;
  if(!empty)store(e,o.work);
  else if(up){o.work.strokes.erase(o.work.strokes.begin()+o.index);store(e,o.work);}  // a click makes no rectangle
 }else if(o.drawing){
  auto&ps=o.work.strokes[o.index].points;auto q=o.cursor;
  if(!ps.empty()&&!up){const double smooth=getDouble(e,"paint_smoothing");q.x=ps.back().x*smooth+q.x*(1-smooth);q.y=ps.back().y*smooth+q.y*(1-smooth);}
  if(ps.empty()||std::hypot((q.x-ps.back().x)*o.work.width,(q.y-ps.back().y)*o.work.height)>.25||std::abs(q.pressure-ps.back().pressure)>1e-4){ps.push_back(q);store(e,o.work);}
 }else if(o.moving){
  auto d=o.work;auto&s=d.strokes[o.index];
  if(o.corner>=0){auto opposite=corners(s)[(o.corner+2)%4],edge=o.cursor;
   if(std::abs(edge.x-opposite.x)*d.width<1)edge.x=opposite.x+(edge.x<opposite.x?-1.:1.)/d.width;
   if(std::abs(edge.y-opposite.y)*d.height<1)edge.y=opposite.y+(edge.y<opposite.y?-1.:1.)/d.height;
   s.points={opposite,edge};}
  else{const double dx=o.cursor.x-o.down.x,dy=o.cursor.y-o.down.y;for(auto&q:s.points){q.x+=dx;q.y+=dy;}}
  store(e,d);refresh(e,true);
 }
 if(up)finishGesture(o);
 interacts->interactRedraw(h);return kOfxStatOK;
}
OfxStatus overlay(const char*action,const void*handle,OfxPropertySetHandle in,OfxPropertySetHandle){
 auto h=(OfxInteractHandle)handle;Overlay*active=nullptr;
 try{
  if(!std::strcmp(action,kOfxActionDescribe))return kOfxStatOK;
  OfxPropertySetHandle ip=nullptr;check(interacts->interactGetPropertySet(h,&ip),"overlay properties");
  if(!std::strcmp(action,kOfxActionCreateInstance)){auto o=std::make_unique<Overlay>();void*e=nullptr;check(prop->propGetPointer(ip,kOfxPropEffectInstance,0,&e),"overlay effect");o->effect=(OfxImageEffectHandle)e;o->handle=h;check(prop->propSetPointer(ip,kOfxPropInstanceData,0,o.get()),"overlay instance");
#if defined(ADR_PAINT_TEST_HOOKS)
   testOverlays.insert(o.get());
#endif
   o.release();return kOfxStatOK;}
  void*p=nullptr;check(prop->propGetPointer(ip,kOfxPropInstanceData,0,&p),"overlay state");require(p,"missing overlay state");auto&o=*static_cast<Overlay*>(p);active=&o;auto e=o.effect;
  if(!std::strcmp(action,kOfxActionDestroyInstance)){
   // Resolve can destroy the effect before its overlay: no parameter or undo
   // calls here. Every stroke was already stored while painting.
#if defined(ADR_PAINT_TEST_HOOKS)
   testOverlays.erase(&o);
#endif
   if(o.undo)o.undo->active=false;delete &o;return kOfxStatOK;
  }
  refreshRecoveryUI(e);
  if(!std::strcmp(action,kOfxInteractActionGainFocus)){o.modifiers=0;return kOfxStatOK;}
  if(!std::strcmp(action,kOfxInteractActionLoseFocus)){o.modifiers=0;finishGesture(o);o.hasCursor=false;return kOfxStatOK;}
  if(!getInt(e,"paint")){if(o.drawing||o.moving||o.adjusting)finishGesture(o);return kOfxStatReplyDefault;}
  if(!std::strcmp(action,kOfxInteractActionDraw)){draw(o,in);return kOfxStatOK;}
  const bool down=!std::strcmp(action,kOfxInteractActionKeyDown),repeat=!std::strcmp(action,kOfxInteractActionKeyRepeat),up=!std::strcmp(action,kOfxInteractActionKeyUp);
  if(down||repeat||up){
   int k=0;prop->propGetInt(in,"kOfxPropKeySym",0,&k);
   if(k>=0xffe1&&k<=0xffee&&k!=0xffe5&&k!=0xffe6){const unsigned bit=1u<<(k-0xffe1);if(up)o.modifiers&=~bit;else o.modifiers|=bit;return kOfxStatReplyDefault;}
   // All keyboard commands belong to Resolve. Modifiers are observed only
   // for pointer gestures and are passed through too.
   return kOfxStatReplyDefault;
  }
  if(!std::strcmp(action,kOfxInteractActionPenDown))return penDown(o,h,in);
  if(!std::strcmp(action,kOfxInteractActionPenMotion)){
   if(!o.drawing&&!o.moving&&!o.adjusting){coordinates(o,in);interacts->interactRedraw(h);return kOfxStatReplyDefault;}
   return penMove(o,h,in,false);
  }
  if(!std::strcmp(action,kOfxInteractActionPenUp)){if(!o.drawing&&!o.moving&&!o.adjusting)return kOfxStatReplyDefault;return penMove(o,h,in,true);}
  return kOfxStatReplyDefault;
 }catch(const std::exception&ex){
  // A failed gesture must not leave a host undo group open. Strokes already stored stay.
  adrlog::error(std::string("paint overlay ")+action+": "+ex.what());
  if(active){active->drawing=active->moving=active->adjusting=false;
   if(active->undo){try{active->undo->end();}catch(...){active->undo->active=false;}active->undo.reset();}
   if(persistentMessages)persistentMessages->setPersistentMessage(active->effect,kOfxMessageError,"adr-paint","%s",ex.what());}
  return kOfxStatFailed;
 }
}
}
