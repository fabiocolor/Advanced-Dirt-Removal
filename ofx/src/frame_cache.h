#pragma once
// Process-wide cache of the cleaning engine's exact output frames.
//
// The engine's cleaned picture depends only on the cleaning controls, the
// shot (range, size, encoding) and the source pixels. It never depends on
// brush strokes, which are composited afterwards. When a stroke changes a
// parameter, Resolve invalidates its own node cache and asks for every frame
// again; without this cache each painted frame restarts the temporal engine
// (55-100 ms at 2K). With it, a re-render is a copy plus the composite.
//
// Entries are bit-exact engine outputs. A hit additionally requires the
// fingerprint of the current source frame to match the one recorded when the
// frame was cleaned, so an upstream grade change on that frame is never served
// stale. Memory is bounded (least recently used entries are evicted).
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef __APPLE__
#include <sys/sysctl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace frame_cache {

inline uint64_t physicalMemory(){
#ifdef __APPLE__
 uint64_t bytes=0;size_t size=sizeof bytes;if(sysctlbyname("hw.memsize",&bytes,&size,nullptr,0)==0)return bytes;return 0;
#elif defined(_WIN32)
 MEMORYSTATUSEX s{};s.dwLength=sizeof s;return GlobalMemoryStatusEx(&s)?uint64_t(s.ullTotalPhys):0;
#else
 const long pages=sysconf(_SC_PHYS_PAGES),page=sysconf(_SC_PAGE_SIZE);return pages>0&&page>0?uint64_t(pages)*uint64_t(page):0;
#endif
}

// ADR_FRAME_CACHE_MB overrides (0 disables). Default: 20% of physical memory,
// at most 8 GiB, at least 512 MiB.
inline size_t defaultBudget(){
 if(const char*v=std::getenv("ADR_FRAME_CACHE_MB")){char*end=nullptr;const unsigned long long mb=std::strtoull(v,&end,10);if(end!=v)return size_t(mb)<<20;}
 const uint64_t ram=physicalMemory();
 uint64_t b=ram?ram/5:(uint64_t(2)<<30);
 b=b>(uint64_t(8)<<30)?(uint64_t(8)<<30):b;b=b<(uint64_t(512)<<20)?(uint64_t(512)<<20):b;
 return size_t(b);
}

// 64-bit fingerprint of float samples, independent of thread count.
template<class Parallel> uint64_t fingerprint(const std::vector<float>&v,int rows,Parallel parallel){
 if(v.empty()||rows<=0)return 0;
 const size_t perRow=v.size()/size_t(rows);
 std::vector<uint64_t> part(static_cast<size_t>(rows),0);
 parallel(rows,[&](int a,int b){
  for(int r=a;r<b;++r){
   const unsigned char*p=reinterpret_cast<const unsigned char*>(v.data()+size_t(r)*perRow);const size_t n=perRow*sizeof(float);
   uint64_t h=0x9e3779b97f4a7c15ull^uint64_t(r);size_t k=0;
   for(;k+8<=n;k+=8){uint64_t x;std::memcpy(&x,p+k,8);h^=x;h*=0xff51afd7ed558ccdull;h^=h>>29;}
   for(;k<n;++k){h^=p[k];h*=0x100000001b3ull;}
   part[size_t(r)]=h;
  }
 });
 uint64_t h=0xcbf29ce484222325ull^uint64_t(v.size());
 for(uint64_t x:part){h^=x+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);}
 return h;
}

class Cache {
public:
 using Frame=std::shared_ptr<const std::vector<float>>;
 explicit Cache(size_t budgetBytes):budget(budgetBytes){}
 size_t capacity()const{return budget;}
 // Returns the frame only when the stored source fingerprint matches.
 Frame find(const std::string&key,uint64_t sourcePrint){
  std::lock_guard<std::mutex> g(m);
  auto it=index.find(key);if(it==index.end()){++misses;return nullptr;}
  if(it->second->print!=sourcePrint){used-=bytes(*it->second);order.erase(it->second);index.erase(it);++stale;return nullptr;}
  order.splice(order.begin(),order,it->second);++hits;return it->second->frame;
 }
 bool contains(const std::string&key){std::lock_guard<std::mutex> g(m);return index.count(key)!=0;}
 void put(const std::string&key,uint64_t sourcePrint,Frame frame){
  const size_t need=frame->size()*sizeof(float);if(!budget||need>budget)return;
  std::lock_guard<std::mutex> g(m);
  auto it=index.find(key);if(it!=index.end()){used-=bytes(*it->second);order.erase(it->second);index.erase(it);}
  while(used+need>budget&&!order.empty()){used-=bytes(order.back());index.erase(order.back().key);order.pop_back();++evicted;}
  order.push_front(Entry{key,sourcePrint,std::move(frame)});index[key]=order.begin();used+=need;
 }
 void clear(){std::lock_guard<std::mutex> g(m);order.clear();index.clear();used=0;}
 struct Stats{size_t frames,bytes,budget;uint64_t hits,misses,stale,evicted;};
 Stats stats(){std::lock_guard<std::mutex> g(m);return {order.size(),used,budget,hits,misses,stale,evicted};}
private:
 struct Entry{std::string key;uint64_t print;Frame frame;};
 static size_t bytes(const Entry&e){return e.frame->size()*sizeof(float);}
 std::mutex m;size_t budget,used=0;uint64_t hits=0,misses=0,stale=0,evicted=0;
 std::list<Entry> order;std::unordered_map<std::string,std::list<Entry>::iterator> index;
};

inline Cache& shared(){static Cache c(defaultBudget());return c;}

}  // namespace frame_cache
