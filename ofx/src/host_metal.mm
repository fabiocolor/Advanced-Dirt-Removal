#include "host_metal.h"
#import <Metal/Metal.h>
#include <stdexcept>
#include <limits>

namespace adr_host {
namespace {
void completed(id<MTLCommandBuffer> command) {
 if(!command)throw std::runtime_error("Metal host transport: cannot create command buffer");
 [command commit];[command waitUntilCompleted];
 if(command.status!=MTLCommandBufferStatusCompleted)
  throw std::runtime_error("Metal host transport: transfer failed");
}
class StagedImage final:public MetalImage {
 id<MTLCommandQueue> queue_;
 id<MTLBuffer> image_,stage_;
public:
 StagedImage(void* queue,void* image,size_t requiredBytes,bool download) {
  @autoreleasepool {
   queue_=(__bridge id<MTLCommandQueue>)queue;
   image_=(__bridge id<MTLBuffer>)image;
   if(!queue_||!image_||!requiredBytes||image_.length<requiredBytes)
    throw std::runtime_error("Metal host transport: invalid image/queue/size");
   if(image_.device!=queue_.device)
    throw std::runtime_error("Metal host transport: image and queue use different devices");
   stage_=[queue_.device newBufferWithLength:requiredBytes options:MTLResourceStorageModeShared];
   if(!stage_||!stage_.contents)throw std::runtime_error("Metal host transport: cannot allocate staging buffer");
   if(download) {
    id<MTLCommandBuffer> command=[queue_ commandBuffer];
    id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
    if(!blit)throw std::runtime_error("Metal host transport: cannot create download encoder");
    [blit copyFromBuffer:image_ sourceOffset:0 toBuffer:stage_ destinationOffset:0 size:requiredBytes];
    [blit endEncoding];completed(command);
   }
  }
 }
 char* data() const noexcept override {return static_cast<char*>(stage_.contents);}
 void uploadRows(size_t start,size_t stride,size_t rowBytes,size_t rows) override {
  if(!rows||!rowBytes)return;
  // Check without unsigned overflow, before any command can publish output.
  if(start>stage_.length||rowBytes>stage_.length-start||
     (rows>1&&stride>(stage_.length-start-rowBytes)/(rows-1)))
   throw std::runtime_error("Metal host transport: output window exceeds staging buffer");
  @autoreleasepool {
   id<MTLCommandBuffer> command=[queue_ commandBuffer];
   id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
   if(!blit)throw std::runtime_error("Metal host transport: cannot create upload encoder");
   for(size_t y=0;y<rows;++y) {
    const size_t offset=start+y*stride;
    [blit copyFromBuffer:stage_ sourceOffset:offset toBuffer:image_ destinationOffset:offset size:rowBytes];
   }
   [blit endEncoding];
   // The CPU engine uses its own queue. Completing these bounded transfers
   // establishes ordering and keeps staging memory alive through the copy.
   completed(command);
  }
 }
};
}
std::unique_ptr<MetalImage> stageMetalImage(void* queue,void* image,size_t bytes,bool download) {
 return std::make_unique<StagedImage>(queue,image,bytes,download);
}
}
