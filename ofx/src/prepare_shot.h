// Included inside the adapter namespace after the colour and source helpers.
// Explicit immutable snapshot: never a speculative temporal cache hit. A ready
// marker is published only after every original/cleaned pair is written.
void prepareShot(OfxImageEffectHandle e,Instance&i){
 struct ResetRequest {std::atomic<bool>& flag;~ResetRequest(){flag=false;}} reset{i.prepareRequested};
 auto feedback=std::atomic_load(&i.prepareFeedback);
 require(bool(feedback),"Missing preparation request");
 const auto start=std::chrono::steady_clock::now();
 adr_sequence* seq=nullptr;
 auto update=[&](int done){
  feedback->done=done;feedback->seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  require(!feedback->cancel&&!fx->abort(e),"Shot preparation cancelled",kOfxStatFailed);
 };
 try{
  const auto range=sourceRange(i);const int count=range[1]-range[0]+1;
  feedback->total=count;feedback->phase=recovery_feedback::State::Running;update(0);
  const auto folder=paint_ui::getText(e,"prepare_folder");
  require(!folder.empty()&&!paint_source::fs::exists(paint_source::fs::u8path(folder)),"Preparation needs a new folder");
  Image first(i.source,range[0],e,"preparation source");const int w=first.width(),h=first.height();
  std::string identity;auto c=readControls(i,range[0],identity);const auto settings=readSettings(i,range[0]);if(settings.encoding)c.extended_range=1;
  paint_source::writeSourceInfo(folder,w,h,number(properties(e),kOfxImageEffectPropFrameRate));
  char error[2048]={};seq=adr_sequence_create(context(),&c,w,h,count,error,sizeof error);require(seq,std::string("Preparation: ")+error);
  std::map<int,std::vector<float>> sources;
  auto get=[&](int t)->const std::vector<float>&{auto&v=sources[t];if(v.empty())readSource(i,e,t,w,h,first.b,v);return v;};
  std::vector<float> clean(size_t(w)*h*3),linear,encoded;
  int done=0;
  while(adr_sequence_next_output(seq)>=0){
   update(done);const int f=adr_sequence_pull(seq,clean.data(),size_t(w)*12,error,sizeof error);
   require(f>=-1,std::string("Preparation: ")+error);
   if(f<0){const int next=adr_sequence_next_source(seq);require(next>=0&&next<count,"Preparation source outside shot");
    const auto&raw=get(range[0]+next);const float*input=raw.data();if(settings.encoding){toLinear(raw,linear);input=linear.data();}
    require(adr_sequence_push(seq,input,size_t(w)*12,error,sizeof error)==0,std::string("Preparation: ")+error);continue;}
   const int time=range[0]+f;std::string checkIdentity;readControls(i,time,checkIdentity);
   require(checkIdentity==identity&&readSettings(i,time).encoding==settings.encoding,"Cleaning settings changed during preparation; prepare again");
   const auto&raw=get(time);
   if(settings.encoding)cineonDelta(raw,clean,encoded);
   paint_source::write(folder,time,w,h,raw,settings.encoding?encoded:clean);
   for(auto it=sources.begin();it!=sources.end();)if(it->first<=time)it=sources.erase(it);else ++it;
   update(++done);
  }
  adr_sequence_destroy(seq);seq=nullptr;adr_context_trim(context());
  auto root=paint_source::fs::u8path(folder);
  {std::ofstream file(root/"shot.txt");file<<settings.encoding<<'\n'<<identity<<'\n'<<"engine "<<ADR_PLUGIN_VERSION<<' '<<ADR_SOURCE_REVISION<<'\n';file.close();require(bool(file),"Cannot save prepared-shot manifest");}
  auto temporary=root/"source-ready.tmp";
  {std::ofstream file(temporary);file<<"ADRREADY1\n"<<range[0]<<' '<<count<<'\n';file.close();require(bool(file),"Cannot finish preparation");}
  update(done);auto ec=paint_source::publish(temporary,root/"source-ready.txt");require(!ec,"Cannot publish prepared shot");paint_source::fs::remove(temporary);
  feedback->phase=recovery_feedback::State::Complete;
  adrlog::info(i.label()+" prepared shot: "+std::to_string(count)+" exact original/cleaned pairs in "+folder);
 }catch(...){if(seq)adr_sequence_destroy(seq);if(engineContext)adr_context_trim(engineContext);
  feedback->phase=feedback->cancel||fx->abort(e)?recovery_feedback::State::Cancelled:recovery_feedback::State::Failed;throw;}
}
