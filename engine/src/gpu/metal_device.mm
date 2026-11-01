// Metal backend. Kernels are compiled at runtime from the embedded dialect
// source with fast math disabled; no Xcode toolchain is needed at build time.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "gpu/device.h"
#include "kernels/kernel_source.h"

namespace adr {
namespace {

class MetalDevice final : public Device {
 public:
  explicit MetalDevice(int index) {
    @autoreleasepool {
      NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
      if (devices.count == 0) throw Error("No Metal device");
      if (index < 0 || index >= (int)devices.count) throw Error("Metal device index out of range");
      device_ = devices[index];
      queue_ = [device_ newCommandQueue];
      NSError* error = nil;
      MTLCompileOptions* options = [MTLCompileOptions new];
      if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
        options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
      } else {
        options.fastMathEnabled = NO;
      }
      options.preprocessorMacros = @{@"ADR_METAL" : @1};
      std::string source = kernel_source();
      library_ = [device_ newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()]
                                       options:options
                                         error:&error];
      if (!library_) {
        throw Error(std::string("Metal kernel compilation failed: ") +
                    (error ? error.localizedDescription.UTF8String : "unknown"));
      }
    }
  }

  ~MetalDevice() override {
    try {
      synchronize();
    } catch (...) {
    }
    release_resources();
    for (auto* kernel : pipelines_) (void)(__bridge_transfer id<MTLComputePipelineState>)kernel;
  }

  std::string name() const override { return device_.name.UTF8String; }
  std::string backend() const override { return "metal"; }
  void* native_queue() const override { return (__bridge void*)queue_; }

  void upload(const Buffer& dst, const void* src, size_t bytes, size_t offset) override {
    if (!bytes) return;
    // Stage through a fresh buffer so pooled destinations still read by queued
    // work are never overwritten early. The command buffer retains the stage.
    id<MTLBuffer> stage = [device_ newBufferWithBytes:src
                                               length:bytes
                                              options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = blit_encoder();
    [blit copyFromBuffer:stage sourceOffset:0 toBuffer:buffer(dst) destinationOffset:offset size:bytes];
  }

  void download(void* dst, const Buffer& src, size_t bytes, size_t offset) override {
    synchronize();
    std::memcpy(dst, static_cast<const char*>(buffer(src).contents) + offset, bytes);
  }

  void fill(const Buffer& dst, uint8_t value, size_t bytes, size_t offset) override {
    if (!bytes) return;
    [blit_encoder() fillBuffer:buffer(dst) range:NSMakeRange(offset, bytes) value:value];
  }

  void copy(const Buffer& dst, const Buffer& src, size_t bytes, size_t dst_offset,
            size_t src_offset) override {
    if (!bytes) return;
    [blit_encoder() copyFromBuffer:buffer(src)
                      sourceOffset:src_offset
                          toBuffer:buffer(dst)
                 destinationOffset:dst_offset
                              size:bytes];
  }

  void launch(KernelHandle* kernel, Dim3 threads, Dim3 group,
              std::initializer_list<const Buffer*> buffers, const void* params,
              size_t params_bytes) override {
    if (threads.x <= 0 || threads.y <= 0 || threads.z <= 0) return;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)kernel->native;
    id<MTLComputeCommandEncoder> encoder = compute_encoder();
    [encoder setComputePipelineState:pipeline];
    NSUInteger index = 0;
    for (const Buffer* b : buffers) {
      [encoder setBuffer:(b && *b) ? buffer(*b) : nil offset:0 atIndex:index++];
    }
    if (params_bytes) [encoder setBytes:params length:params_bytes atIndex:index];
    MTLSize groups = MTLSizeMake((threads.x + group.x - 1) / group.x, (threads.y + group.y - 1) / group.y,
                                 (threads.z + group.z - 1) / group.z);
    [encoder dispatchThreadgroups:groups threadsPerThreadgroup:MTLSizeMake(group.x, group.y, group.z)];
  }

  void flush() override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    end_encoder();
    if (command_) {
      [command_ commit];
      last_ = command_;
      command_ = nil;
    }
  }

  void synchronize() override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    flush();
    if (last_) {
      [last_ waitUntilCompleted];
      if (last_.status == MTLCommandBufferStatusError) {
        NSError* error = last_.error;
        last_ = nil;
        throw Error(std::string("Metal command buffer failed: ") +
                    (error ? error.localizedDescription.UTF8String : "unknown"));
      }
      last_ = nil;
    }
  }

 protected:
  BufferHandle allocate_native(size_t bytes) override {
    id<MTLBuffer> b = [device_ newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (!b) throw Error("Metal allocation failed");
    BufferHandle handle;
    handle.native = (__bridge_retained void*)b;
    handle.bytes = bytes;
    return handle;
  }

  void release_native(BufferHandle handle) override {
    id<MTLBuffer> b = (__bridge_transfer id<MTLBuffer>)handle.native;
    (void)b;
  }

  void* load_kernel(const std::string& name) override {
    @autoreleasepool {
      id<MTLFunction> function = [library_ newFunctionWithName:[NSString stringWithUTF8String:name.c_str()]];
      if (!function) return nullptr;
      NSError* error = nil;
      id<MTLComputePipelineState> pipeline = [device_ newComputePipelineStateWithFunction:function error:&error];
      if (!pipeline) {
        throw Error("Metal pipeline creation failed for " + name + ": " +
                    (error ? error.localizedDescription.UTF8String : "unknown"));
      }
      void* retained = (__bridge_retained void*)pipeline;
      pipelines_.push_back(retained);
      return retained;
    }
  }

 private:
  static id<MTLBuffer> buffer(const Buffer& b) { return (__bridge id<MTLBuffer>)b.native(); }

  id<MTLCommandBuffer> command() {
    if (!command_) command_ = [queue_ commandBuffer];
    return command_;
  }

  void end_encoder() {
    if (compute_) {
      [compute_ endEncoding];
      compute_ = nil;
    }
    if (blit_) {
      [blit_ endEncoding];
      blit_ = nil;
    }
  }

  id<MTLComputeCommandEncoder> compute_encoder() {
    if (!compute_) {
      end_encoder();
      compute_ = [command() computeCommandEncoderWithDispatchType:MTLDispatchTypeSerial];
    }
    return compute_;
  }

  id<MTLBlitCommandEncoder> blit_encoder() {
    if (!blit_) {
      end_encoder();
      blit_ = [command() blitCommandEncoder];
    }
    return blit_;
  }

  std::recursive_mutex mutex_;
  std::vector<void*> pipelines_;
  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLLibrary> library_ = nil;
  id<MTLCommandBuffer> command_ = nil;
  id<MTLCommandBuffer> last_ = nil;
  id<MTLComputeCommandEncoder> compute_ = nil;
  id<MTLBlitCommandEncoder> blit_ = nil;
};

}  // namespace

std::unique_ptr<Device> create_metal_device(int index) { return std::make_unique<MetalDevice>(index); }

}  // namespace adr
