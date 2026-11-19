#include "../src/host_metal.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
 @autoreleasepool {
  try {
   id<MTLDevice> device=MTLCreateSystemDefaultDevice();
   require(device!=nil,"Metal device unavailable");
   id<MTLCommandQueue> queue=[device newCommandQueue];
   for(size_t stride:{size_t(40),size_t(37)}) {
    const size_t size=stride*5;
    id<MTLBuffer> src=[device newBufferWithLength:size options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst=[device newBufferWithLength:size options:MTLResourceStorageModeShared];
    std::vector<unsigned char> expected(size,0xa5);
    auto* input=static_cast<unsigned char*>(src.contents);
    for(size_t k=0;k<size;++k)input[k]=static_cast<unsigned char>(k*73+19);
    std::memset(dst.contents,0xa5,size);
    auto read=adr_host::stageMetalImage((__bridge void*)queue,(__bridge void*)src,size,true);
    require(std::memcmp(read->data(),src.contents,size)==0,"download changes bytes");
    auto write=adr_host::stageMetalImage((__bridge void*)queue,(__bridge void*)dst,size,false);
    std::memcpy(write->data(),read->data(),size);
    // Odd offsets/row lengths cover byte and half formats, clipped masks and
    // partial windows. Every byte outside the requested rows must survive.
    const size_t start=7,rowBytes=21,rows=4;
    for(size_t y=0;y<rows;++y)std::copy(input+start+y*stride,input+start+y*stride+rowBytes,expected.begin()+start+y*stride);
    write->uploadRows(start,stride,rowBytes,rows);
    require(std::memcmp(dst.contents,expected.data(),size)==0,"partial upload changes padding/outside pixels");
    bool rejected=false;
    try{write->uploadRows(size-2,stride,21,2);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"invalid window accepted");
    write->uploadRows(size,0,0,0);
    require(std::memcmp(dst.contents,expected.data(),size)==0,"empty upload changes output");
    rejected=false;
    try{adr_host::stageMetalImage((__bridge void*)queue,(__bridge void*)src,size+1,true);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"undersized host buffer accepted");
    std::printf("Metal byte transport exact: stride=%zu, partial/empty/error paths\n",stride);
   }
   bool rejected=false;
   try{adr_host::stageMetalImage(nullptr,nullptr,1,true);}catch(const std::runtime_error&){rejected=true;}
   require(rejected,"null transport accepted");
   return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
 }
}
