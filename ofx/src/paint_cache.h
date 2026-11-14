#pragma once
#include "paint.h"
#include <list>
#include <memory>
#include <string>

namespace paint {
// Render-thread owned. Key only the active strokes, retaining their order and
// all properties: editing another frame must not discard this frame's mask.
// Exact strings avoid hash collisions; the byte budget includes both layers.
class LayerCache {
 struct Entry {std::string key;std::shared_ptr<const Layers> layers;size_t bytes;};
 std::list<Entry> entries;
 size_t used=0,budget;
public:
 size_t hits=0,misses=0;
 explicit LayerCache(size_t bytes=128u<<20):budget(bytes){}
 void clear(){entries.clear();used=0;}
 std::shared_ptr<const Layers> get(const Document&doc,int frame,int w,int h){
  Document active;active.width=doc.width;active.height=doc.height;
  for(auto s:doc.strokes)if(s.enabled&&s.opacity>0&&frame>=s.first&&frame<=s.last){
   s.id=active.nextId++;s.first=s.last=0;active.strokes.push_back(std::move(s));
  }
  const std::string key=std::to_string(w)+"x"+std::to_string(h)+"|"+encode(active);
  for(auto it=entries.begin();it!=entries.end();++it)if(it->key==key){
   ++hits;auto result=it->layers;entries.splice(entries.begin(),entries,it);return result;
  }
  ++misses;auto result=std::make_shared<const Layers>(layers(doc,frame,w,h));
  const size_t bytes=(result->value.size()+result->touched.size())*sizeof(float)+key.size();
  if(bytes<=budget){while(used+bytes>budget&&!entries.empty()){used-=entries.back().bytes;entries.pop_back();}
   entries.push_front({key,result,bytes});used+=bytes;}
  return result;
 }
};
}
