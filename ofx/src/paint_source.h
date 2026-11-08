// Lossless RGB32 recovery pairs. The original is stored as an exact XOR delta
// from the cleaned image, with zero runs and varints. Frame files are immutable.
#pragma once
#include "../../engine/tools/sha256.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cmath>
#include <atomic>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <cerrno>
#ifdef _WIN32
#include <process.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <stdio.h>
#endif
#endif
namespace paint_source {
namespace fs=std::filesystem;
// Publish a completed file without replacing an existing bake. Native exclusive
// rename also works on removable volumes that cannot create hard links.
inline std::error_code publish(const fs::path&temp,const fs::path&file){
#ifdef __APPLE__
 if(renamex_np(temp.c_str(),file.c_str(),RENAME_EXCL)==0)return {};
 return std::error_code(errno,std::generic_category());
#elif defined(_WIN32)
 if(MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_WRITE_THROUGH))return {};
 return std::error_code(GetLastError(),std::system_category());
#else
 std::error_code ec;fs::create_hard_link(temp,file,ec);return ec;
#endif
}
inline void require(bool b,const char*s){if(!b)throw std::runtime_error(s);}
inline std::string hash(const std::vector<float>&v){adr_tools::Sha256 h;h.update(v.data(),v.size()*sizeof(float));return h.hex();}
inline fs::path path(const std::string&folder,int frame){return fs::u8path(folder)/("frame-"+std::to_string(frame)+".adrs");}
struct SourceInfo {int width=0,height=0;double fps=0;};
inline SourceInfo sourceInfo(const std::string&folder){std::ifstream f(fs::u8path(folder)/"source-info.txt");std::string tag;SourceInfo info;f>>tag>>info.width>>info.height>>info.fps;require(bool(f)&&tag=="ADRSOURCE1"&&info.width>0&&info.width<=16384&&info.height>0&&info.height<=16384&&std::isfinite(info.fps)&&info.fps>0,"Invalid additional-source metadata; prepare the source track first");return info;}
inline void requireSourceFrame(const std::string&folder,long long frame){std::ifstream f(fs::u8path(folder)/"source-ready.txt");std::string tag;long long first=0,count=0;f>>tag>>first>>count;require(bool(f)&&tag=="ADRREADY1"&&count>0&&count<=2000000000&&first>=-1000000000&&first<=1000000000,"Additional source is incomplete; finish Prepare ADR Recovery Source first");require(frame>=first&&frame-first<count,"Requested frame is outside the prepared additional source; check alignment and frame offset");}
inline void writeSourceInfo(const std::string&folder,int w,int h,double fps){
 require(w>0&&h>0&&w<=16384&&h<=16384&&std::isfinite(fps)&&fps>0,"Invalid additional-source geometry/rate");fs::create_directories(fs::u8path(folder));auto file=fs::u8path(folder)/"source-info.txt";
 auto matches=[&]{auto info=sourceInfo(folder);require(info.width==w&&info.height==h&&std::abs(info.fps-fps)<1e-8,"Additional-source folder already has different geometry/rate; choose a new folder");};
 if(fs::exists(file)){matches();return;}
 static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);if(fs::exists(file)){matches();return;}
 auto temp=file;temp+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 try{std::ofstream f(temp);f<<"ADRSOURCE1\n"<<w<<' '<<h<<' '<<std::setprecision(17)<<fps<<'\n';f.close();require(bool(f),"Cannot write additional-source metadata");auto ec=publish(temp,file);fs::remove(temp);if(ec){require(fs::exists(file),"Cannot publish additional-source metadata");matches();}}catch(...){std::error_code ec;fs::remove(temp,ec);throw;}
}
struct Pair {std::vector<float> cleaned,original;};
inline void varint(std::vector<uint8_t>&out,uint32_t n){while(n>=128){out.push_back(uint8_t(n)|128);n>>=7;}out.push_back(uint8_t(n));}
inline uint32_t unvarint(const std::vector<uint8_t>&in,size_t&pos){uint32_t n=0;for(unsigned shift=0;shift<=28;shift+=7){require(pos<in.size(),"Truncated recovery delta");uint8_t v=in[pos++];require(shift<28||v<16,"Invalid recovery delta varint");n|=uint32_t(v&127)<<shift;if(!(v&128))return n;}throw std::runtime_error("Invalid recovery delta varint");}
inline uint32_t bits(float v){uint32_t n;std::memcpy(&n,&v,4);return n;}
inline std::vector<uint8_t> packOriginal(const std::vector<float>&original,const std::vector<float>&cleaned){
 require(original.size()==cleaned.size(),"Recovery delta geometry mismatch");std::vector<uint8_t> out;
 for(size_t j=0;j<original.size();){uint32_t x=bits(original[j])^bits(cleaned[j]);if(x){varint(out,x);++j;}else{size_t first=j;while(j<original.size()&&bits(original[j])==bits(cleaned[j]))++j;varint(out,0);varint(out,uint32_t(j-first));}}return out;
}
inline std::vector<float> unpackOriginal(const std::vector<uint8_t>&packed,const std::vector<float>&cleaned){
 std::vector<float> original=cleaned;size_t pos=0,j=0;
 while(j<original.size()){uint32_t x=unvarint(packed,pos);if(!x){uint32_t run=unvarint(packed,pos);require(run>0&&run<=original.size()-j,"Invalid recovery delta run");j+=run;}else{uint32_t value=bits(cleaned[j])^x;std::memcpy(&original[j++],&value,4);}}
 require(pos==packed.size(),"Trailing recovery delta data");return original;
}
inline Pair readPair(const std::string&folder,int frame,int w,int h,const std::vector<float>*expected=nullptr){
 std::ifstream f(path(folder,frame),std::ios::binary);require(bool(f),"Recovery bake frame missing. Bake this frame in Advanced Dirt Removal first.");
 std::string magic,sourceHash,cleanHash;int ww=0,hh=0,tt=0;f>>std::setw(8)>>magic>>ww>>hh>>tt>>std::setw(65)>>sourceHash>>std::setw(65)>>cleanHash;require(bool(f)&&(magic=="ADRS1"||magic=="ADRS2")&&ww==w&&hh==h&&tt==frame,"Recovery bake geometry/frame mismatch");require(f.get()=='\n',"Invalid recovery bake header");
 require(w>0&&h>0&&w<=16384&&h<=16384&&sourceHash.size()==64&&cleanHash.size()==64,"Invalid recovery bake header");
 if(expected)require(expected->size()==size_t(w)*h*3&&sourceHash==hash(*expected),"Recovery bake source mismatch: use the original clip at the same colour/processing stage");
 size_t bytes=0;if(magic=="ADRS2"){f>>bytes;require(bool(f)&&f.get()=='\n'&&bytes<=size_t(w)*h*3*5+10,"Invalid recovery delta size");}
 Pair pair;pair.cleaned.resize(size_t(w)*h*3);f.read(reinterpret_cast<char*>(pair.cleaned.data()),std::streamsize(pair.cleaned.size()*4));require(bool(f)&&hash(pair.cleaned)==cleanHash,"Recovery bake is incomplete or corrupt");
 if(magic=="ADRS2"){std::vector<uint8_t> packed(bytes);f.read(reinterpret_cast<char*>(packed.data()),std::streamsize(bytes));require(bool(f),"Truncated recovery source delta");pair.original=unpackOriginal(packed,pair.cleaned);require(hash(pair.original)==sourceHash,"Recovery source delta is corrupt");}
 require(f.peek()==EOF,"Trailing recovery bake data");return pair;
}
inline std::vector<float> read(const std::string&folder,int frame,int w,int h,const std::vector<float>&original){return readPair(folder,frame,w,h,&original).cleaned;}
// Viewer previews are explicitly resampled derivatives. They cannot establish
// a full-source pixel hash; native renders always use read()/get() below.
inline std::vector<float> preview(const std::vector<float>&native,int fullW,int fullH,int w,int h){
 require(fullW>0&&fullH>0&&w>0&&h>0&&native.size()==size_t(fullW)*fullH*3,"Invalid recovery preview geometry");std::vector<float> out(size_t(w)*h*3);
 for(int y=0;y<h;++y){double py=std::clamp((y+.5)*fullH/h-.5,0.,double(fullH-1));int y0=int(py),y1=std::min(y0+1,fullH-1);float fy=float(py-y0);
  for(int x=0;x<w;++x){double px=std::clamp((x+.5)*fullW/w-.5,0.,double(fullW-1));int x0=int(px),x1=std::min(x0+1,fullW-1);float fx=float(px-x0);
   for(int c=0;c<3;++c){float a=native[(size_t(y0)*fullW+x0)*3+c],b=native[(size_t(y0)*fullW+x1)*3+c],d=native[(size_t(y1)*fullW+x0)*3+c],e=native[(size_t(y1)*fullW+x1)*3+c];float upper=a+(b-a)*fx,lower=d+(e-d)*fx;out[(size_t(y)*w+x)*3+c]=upper+(lower-upper)*fy;}
  }
 }return out;
}
inline bool write(const std::string&folder,int frame,int w,int h,const std::vector<float>&original,const std::vector<float>&cleaned){
 static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);
 require(w>0&&h>0&&w<=16384&&h<=16384&&original.size()==size_t(w)*h*3&&cleaned.size()==original.size(),"Invalid recovery bake geometry");
 fs::create_directories(fs::u8path(folder));auto file=path(folder,frame);
 if(fs::exists(file)){require(hash(read(folder,frame,w,h,original))==hash(cleaned),"Recovery bake already contains different cleaning settings; choose a new empty folder");return false;}
#ifdef _WIN32
 const auto pid=_getpid();
#else
 const auto pid=getpid();
#endif
 static std::atomic<unsigned long long> counter{0};auto temp=file;temp+=".tmp-"+std::to_string(pid)+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++counter);
 try{
  std::ofstream f(temp,std::ios::binary|std::ios::trunc);require(bool(f),"Cannot write recovery bake folder");auto packed=packOriginal(original,cleaned);f<<"ADRS2 "<<w<<' '<<h<<' '<<frame<<'\n'<<hash(original)<<'\n'<<hash(cleaned)<<'\n'<<packed.size()<<'\n';f.write(reinterpret_cast<const char*>(cleaned.data()),std::streamsize(cleaned.size()*4));f.write(reinterpret_cast<const char*>(packed.data()),std::streamsize(packed.size()));f.close();require(bool(f),"Recovery bake write failed (disk full?)");
  // Atomic publish without replacing a pre-existing frame, even in another process.
  std::error_code ec=publish(temp,file);fs::remove(temp);
  if(ec){require(fs::exists(file),"Cannot atomically publish recovery bake on this filesystem");require(hash(read(folder,frame,w,h,original))==hash(cleaned),"Another process published a different recovery bake frame");return false;}
 }catch(...){std::error_code ec;fs::remove(temp,ec);throw;}
 return true;
}
// Only one native frame is retained. A brush edit reuses its immutable cleaned
// pixels; source changes or external file edits force fresh validation.
class FrameCache {
 std::mutex mutex_;
 std::string key_,sourceHash_;
 fs::file_time_type stamp_{};
 std::shared_ptr<const std::vector<float>> pixels_,original_;
public:
 size_t reads=0,hits=0;
 void clear(){std::lock_guard<std::mutex> guard(mutex_);key_.clear();sourceHash_.clear();pixels_.reset();original_.reset();}
 std::shared_ptr<const std::vector<float>> get(const std::string&folder,int frame,int w,int h,const std::vector<float>&original){
  auto file=path(folder,frame);auto sourceHash=hash(original);std::error_code ec;auto stamp=fs::last_write_time(file,ec);require(!ec,"Recovery bake frame missing. Bake this frame in Advanced Dirt Removal first.");
  std::string key=file.u8string()+"|"+std::to_string(w)+"x"+std::to_string(h);
  std::lock_guard<std::mutex> guard(mutex_);
  if(pixels_&&key==key_&&sourceHash==sourceHash_&&stamp==stamp_){++hits;return pixels_;}
  auto data=std::make_shared<const std::vector<float>>(read(folder,frame,w,h,original));
  ++reads;key_=std::move(key);sourceHash_=std::move(sourceHash);stamp_=stamp;pixels_=std::move(data);original_.reset();return pixels_;
 }
 struct PreviewPair {std::shared_ptr<const std::vector<float>> cleaned,original;};
 PreviewPair getPreview(const std::string&folder,int frame,int w,int h){
  auto file=path(folder,frame);std::error_code ec;auto stamp=fs::last_write_time(file,ec);require(!ec,"Recovery bake frame missing. Bake this frame in Advanced Dirt Removal first.");std::string key=file.u8string()+"|"+std::to_string(w)+"x"+std::to_string(h);
  std::lock_guard<std::mutex> guard(mutex_);if(pixels_&&original_&&key==key_&&stamp==stamp_){++hits;return {pixels_,original_};}
  auto pair=readPair(folder,frame,w,h);require(!pair.original.empty(),"This legacy test bake has no paired source for scaled previews; create a new bake");++reads;key_=std::move(key);sourceHash_="preview";stamp_=stamp;const bool same=pair.cleaned.size()==pair.original.size()&&std::memcmp(pair.cleaned.data(),pair.original.data(),pair.cleaned.size()*sizeof(float))==0;pixels_=std::make_shared<const std::vector<float>>(std::move(pair.cleaned));original_=same?pixels_:std::make_shared<const std::vector<float>>(std::move(pair.original));return {pixels_,original_};
 }

};
}
