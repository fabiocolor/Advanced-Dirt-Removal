// Host-requested reduced-resolution derivatives. Never mutate the native
// sequence, paint geometry, queued calculation or on-disk recovery mattes.
std::vector<float> previewPixels(Image& image){
 std::vector<float> rgb(size_t(image.width())*image.height()*3);
 for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)for(int c=0;c<3;++c){
  float v=image.read(x,y,c);require(finiteBits(v),"Nonfinite preview input");rgb[(size_t(y)*image.width()+x)*3+c]=v;
 }
 return rgb;
}
OfxStatus renderPreview(OfxImageEffectHandle e,OfxPropertySetHandle in,Instance&i,double sx,double sy){
#if ADR_HOST_METAL_TRANSPORT
 HostMetalScope transport(in);
#elif ADR_HOST_GPU_TRANSPORT
 HostGpuScope transport(in);
#endif
 const int time=frameTime(in);auto range=sourceRange(i);require(time>=range[0]&&time<=range[1],"Preview time outside source");
 Image source(i.source,time,e,"preview source",true),output(i.output,time,e,"preview output",true);
 const int w=source.width(),h=source.height();
 require(source.b==output.b&&source.channels==output.channels,"Preview output geometry/components differ");
 const auto window=renderWindow(e,in,source.b);
 OfxRectD rod;check(fx->clipGetRegionOfDefinition(i.source,time,&rod),"Preview canonical source RoD");OfxPropertySetHandle clipProps=nullptr;check(fx->clipGetPropertySet(i.source,&clipProps),"Preview source properties");
 double par=1.;if(prop->propGetDouble(clipProps,kOfxImagePropPixelAspectRatio,0,&par)!=kOfxStatOK)par=1.;
 require(std::isfinite(par)&&par>0,"Invalid preview pixel aspect");
 const double canonicalW=(rod.x2-rod.x1)/par,canonicalH=rod.y2-rod.y1;
 require(std::isfinite(canonicalW)&&std::isfinite(canonicalH)&&canonicalW>=256&&canonicalW<=16384&&canonicalH>=256&&canonicalH<=16384,"Invalid preview source RoD");
 const int fullW=int(std::llround(canonicalW)),fullH=int(std::llround(canonicalH));
 require(std::abs(w-fullW*sx)<=2&&std::abs(h-fullH*sy)<=2,"Preview grid differs from its canonical source");
 require(fullW>=256&&fullW<=16384&&fullH>=256&&fullH<=16384,"Invalid preview canonical dimensions");
 auto working=previewPixels(source),original=working,clean=working;
 std::string identity;auto controls=readControls(i,time,identity);const auto settings=readSettings(i,time);
 const int kind=paint_ui::getInt(e,"paint_source_kind");
 auto strokes=frameStrokes(i,time);const bool painted=paint::anyOn(strokes,time);
 int paintOn=0;params->paramGetValueAtTime(i.parameters.at("paint"),time,&paintOn);
 const bool builtInBrush=paintHost()&&(paintOn||!strokes.strokes.empty());
 const bool skipCleaning=kind==0&&settings.maskMode==2&&builtInBrush&&!painted;
 auto read=[&](int t){Image im(i.source,t,e,"preview temporal source",true);require(im.b==source.b,"Preview source geometry changed");return previewPixels(im);};
 if(kind==1){
  require(settings.encoding==0&&settings.mode==0,"Additional source: use Video encoding and Auto recovery Off");
  if(originalConnected(i)){
   Image im(additionalClip(i),time,e,"preview additional source",true);
   require(im.b==source.b,"Preview additional source geometry differs");original=previewPixels(im);
  }else if(!paint_ui::getText(e,"paint_source_folder").empty()){
   const auto folder=paint_ui::getText(e,"paint_source_folder");const auto info=paint_source::sourceInfo(folder);
   require(info.width==fullW&&info.height==fullH,"Preview original canonical dimensions differ");
   require(std::abs(info.fps-number(properties(e),kOfxImageEffectPropFrameRate))<1e-8,"Preview original frame rate differs");
   const long long frame=static_cast<long long>(time)+paint_ui::getInt(e,"paint_source_offset");
   require(std::abs(double(frame))<=1e9,"Preview original offset outside supported range");paint_source::requireSourceFrame(folder,frame);
   auto pair=i.originalSource.getPreview(folder,int(frame),fullW,fullH);
   original=paint_source::preview(*pair.original,fullW,fullH,w,h);
  }else if(persistentMessages)persistentMessages->setPersistentMessage(e,kOfxMessageWarning,"adr","%s","Connect the aligned original to the effect's second input (Additional source). Showing the processed picture.");
 }else if(kind==2){
  require(paint_ui::preparationAvailable()&&paintHost(),"Experimental prepared shot is disabled");
  const auto folder=paint_ui::getText(e,"prepare_folder");const auto info=paint_source::sourceInfo(folder);
  require(info.width==fullW&&info.height==fullH&&std::abs(info.fps-number(properties(e),kOfxImageEffectPropFrameRate))<1e-8,"Preview prepared geometry/rate mismatch");
  std::ifstream manifest(paint_source::fs::u8path(folder)/"shot.txt");int encoding=-1;manifest>>encoding;
  require(bool(manifest)&&encoding==settings.encoding,"Prepared shot input encoding changed; prepare again");
  paint_source::requireSourceFrame(folder,time);auto pair=i.preparedFrames.get(folder,time,fullW,fullH);
  original=paint_source::preview(*pair.original,fullW,fullH,w,h);clean=paint_source::preview(*pair.cleaned,fullW,fullH,w,h);
 }else if(!skipCleaning){
  // Independent preview engine: reduced frames cannot enter the native cache.
  // Edge padding meets the engine's minimum geometry without scaling a second
  // time. Crop it away before recovery and display; master pixels are untouched.
  const int pw=std::max(w,256),ph=std::max(h,256);
  auto pad=[&](const std::vector<float>&rgb){std::vector<float> v(size_t(pw)*ph*3);for(int y=0;y<ph;++y)for(int x=0;x<pw;++x)for(int c=0;c<3;++c)v[(size_t(y)*pw+x)*3+c]=rgb[(size_t(std::min(y,h-1))*w+std::min(x,w-1))*3+c];return v;};
  if(settings.encoding)controls.extended_range=1;
  if(controls.limit_spot_size)controls.largest_spot=std::clamp(int(std::lround(controls.largest_spot*std::sqrt(sx*sy))),1,256);
  if(spanishWindow(e,time)){range[0]=std::max(range[0],time-kSpanishBack);range[1]=std::min(range[1],time+kSpanishAhead);}
  char error[2048]={};std::unique_ptr<adr_sequence,decltype(&adr_sequence_destroy)> sequence(adr_sequence_create(context(),&controls,pw,ph,range[1]-range[0]+1,error,sizeof error),adr_sequence_destroy);
  require(bool(sequence),std::string("Preview sequence: ")+error,kOfxStatFailed);
  std::vector<float> repaired(size_t(pw)*ph*3),linear;
  for(;;){
   require(!fx->abort(e),"Preview render aborted",kOfxStatFailed);
   const int f=adr_sequence_pull(sequence.get(),repaired.data(),size_t(pw)*12,error,sizeof error);
   require(f>=-1,std::string("Preview repair: ")+error,kOfxStatFailed);
   if(f==time-range[0])break;
   if(f>=0)continue;
   const int next=adr_sequence_next_source(sequence.get());require(next>=0,"Preview sequence stalled",kOfxStatFailed);
   auto raw=pad(range[0]+next==time?working:read(range[0]+next));const float* pixels=raw.data();
   if(settings.encoding){toLinear(raw,linear);pixels=linear.data();}
   require(adr_sequence_push(sequence.get(),pixels,size_t(pw)*12,error,sizeof error)==0,std::string("Preview upload: ")+error,kOfxStatFailed);
  }
  for(int y=0;y<h;++y)std::copy_n(repaired.data()+size_t(y)*pw*3,size_t(w)*3,clean.data()+size_t(y)*w*3);
  if(settings.encoding){std::vector<float> encoded;cineonDelta(working,clean,encoded);clean=std::move(encoded);}
 }
 std::vector<uint8_t> matte;
 require(!(settings.mode&&settings.maskMode==2),spanishRecoveryError,kOfxStatFailed);
 if(settings.mode){
  // A preview may score its derivative but must never publish a reduced matte
  // as completed native analysis or consume a queued whole-shot calculation.
  auto neighbours=recoveryNeighbours(time,range[0],range[1]);auto prev=read(neighbours[0]),next=read(neighbours[1]);
  std::vector<uint8_t> probability(size_t(w)*h);char error[2048]={};
  require(adr_recovery_frame(recoveryModel(),prev.data(),original.data(),next.data(),clean.data(),w,h,size_t(w)*12,ADR_RECOVERY_INPUT_VIDEO,probability.data(),error,sizeof error)>=0,std::string("Preview recovery: ")+error,kOfxStatFailed);
  const uint8_t cut=uint8_t(1+std::nearbyint(settings.confidence*254));matte.resize(probability.size());for(size_t k=0;k<matte.size();++k)matte[k]=probability[k]>=cut;
 }
 std::vector<float> mm,ms,finished;
 const bool haveMask=kind!=1&&!builtInBrush&&readMask(i,time,w,h,source.b,mm,ms);
 auto layers=painted?paint::layers(strokes,time,w,h,fullW,fullH):paint::Layers{};
 finish(settings,time,w,h,original,clean.data(),matte,haveMask,mm,ms,painted?&layers:nullptr,finished);
 for(float v:finished){require(finiteBits(v),"Nonfinite preview output");if(output.depth==PixelDepth::Half)(void)encodeHalf(v);}
 require(!fx->abort(e),"Preview render aborted",kOfxStatFailed);
 for(int y=window[1];y<window[3];++y)for(int x=window[0];x<window[2];++x){const int row=source.b[3]-1-y,col=x-source.b[0];
  for(int c=0;c<3;++c)output.write(col,row,c,finished[(size_t(row)*w+col)*3+c]);
  if(output.channels==4){if(output.depth==source.depth)std::memcpy(output.sample(col,row,3),source.sample(col,row,3),size_t(source.sampleBytes));else output.write(col,row,3,source.read(col,row,3));}
 }
 output.publish(window);
 if(persistentMessages)persistentMessages->clearPersistentMessage(e);
 adrlog::info(i.label()+" preview frame "+std::to_string(time)+" complete "+std::to_string(w)+"x"+std::to_string(h)+" (native history and recovery files unchanged)");
 return kOfxStatOK;
}
