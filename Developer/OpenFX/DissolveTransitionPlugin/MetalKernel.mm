#import <Metal/Metal.h>

#include <unordered_map>
#include <mutex>

const char* kernelSource =
"#include <metal_stdlib>                                                                                                \n" \
"using namespace metal;                                                                                                 \n" \
"kernel void DissolveTransitionKernel(constant int& p_Width, constant int& p_Height, constant float& p_Transition,      \n" \
"                                     const device float* p_SrcFromInput, device float* p_SrcToInput,                   \n" \
"                                     device float* p_Output, uint2 id [[ thread_position_in_grid ]])                   \n" \
"{                                                                                                                      \n" \
"   if ((id.x < p_Width) && (id.y < p_Height))                                                                          \n" \
"   {                                                                                                                   \n" \
"       const int index = ((id.y * p_Width) + id.x) * 4;                                                                \n" \
"       const float alphaWeightFrom = p_SrcFromInput[index + 3] * (1.0f - p_Transition);                                \n" \
"       const float alphaWeightTo   = p_SrcToInput[index + 3] * p_Transition;                                           \n" \
"                                                                                                                       \n" \
"       // Dissolve RGB channels                                                                                        \n" \
"       p_Output[index + 0] = alphaWeightFrom * p_SrcFromInput[index + 0] + alphaWeightTo * p_SrcToInput[index + 0];    \n" \
"       p_Output[index + 1] = alphaWeightFrom * p_SrcFromInput[index + 1] + alphaWeightTo * p_SrcToInput[index + 1];    \n" \
"       p_Output[index + 2] = alphaWeightFrom * p_SrcFromInput[index + 2] + alphaWeightTo * p_SrcToInput[index + 2];    \n" \
"                                                                                                                       \n" \
"       // Dissolve alpha channel                                                                                       \n" \
"       p_Output[index + 3] = alphaWeightFrom + alphaWeightTo;                                                          \n" \
"   }                                                                                                                   \n" \
"}                                                                                                                      \n";

std::mutex s_PipelineQueueMutex;
typedef std::unordered_map<id<MTLCommandQueue>, id<MTLComputePipelineState>> PipelineQueueMap;
PipelineQueueMap s_PipelineQueueMap;

void RunMetalKernel(void* p_CmdQ, int p_Width, int p_Height, float p_Transition, const float* p_SrcFromInput, const float* p_SrcToInput, float* p_Output)
{
    const char* kernelName = "DissolveTransitionKernel";

    id<MTLCommandQueue>            queue = static_cast<id<MTLCommandQueue> >(p_CmdQ);
    id<MTLDevice>                  device = queue.device;
    id<MTLLibrary>                 metalLibrary;     // Metal library
    id<MTLFunction>                kernelFunction;   // Compute kernel
    id<MTLComputePipelineState>    pipelineState;    // Metal pipeline
    NSError* err;

    std::unique_lock<std::mutex> lock(s_PipelineQueueMutex);

    const auto it = s_PipelineQueueMap.find(queue);
    if (it == s_PipelineQueueMap.end())
    {
        id<MTLLibrary>                 metalLibrary;     // Metal library
        id<MTLFunction>                kernelFunction;   // Compute kernel
        NSError* err;

        MTLCompileOptions* options = [MTLCompileOptions new];
        options.fastMathEnabled = YES;
        if (!(metalLibrary    = [device newLibraryWithSource:@(kernelSource) options:options error:&err]))
        {
            fprintf(stderr, "Failed to load metal library, %s\n", err.localizedDescription.UTF8String);
            return;
        }
        [options release];
        if (!(kernelFunction  = [metalLibrary newFunctionWithName:[NSString stringWithUTF8String:kernelName]/* constantValues : constantValues */]))
        {
            fprintf(stderr, "Failed to retrieve kernel\n");
            [metalLibrary release];
            return;
        }
        if (!(pipelineState   = [device newComputePipelineStateWithFunction:kernelFunction error:&err]))
        {
            fprintf(stderr, "Unable to compile, %s\n", err.localizedDescription.UTF8String);
            [metalLibrary release];
            [kernelFunction release];
            return;
        }

        s_PipelineQueueMap[queue] = pipelineState;

        //Release resources
        [metalLibrary release];
        [kernelFunction release];
    }
    else
    {
        pipelineState = it->second;
    }

    id<MTLBuffer> srcFromDeviceBuf = reinterpret_cast<id<MTLBuffer> >(const_cast<float*>(p_SrcFromInput));
    id<MTLBuffer> srcToDeviceBuf   = reinterpret_cast<id<MTLBuffer> >(const_cast<float*>(p_SrcToInput));
    id<MTLBuffer> dstDeviceBuf     = reinterpret_cast<id<MTLBuffer> >(p_Output);

    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    commandBuffer.label = [NSString stringWithFormat:@"DissolveTransitionKernel"];

    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:pipelineState];

    int exeWidth = [pipelineState threadExecutionWidth];
    MTLSize threadGroupCount = MTLSizeMake(exeWidth, 1, 1);
    MTLSize threadGroups     = MTLSizeMake((p_Width + exeWidth - 1)/exeWidth, p_Height, 1);

    [computeEncoder setBytes:&p_Width          length: sizeof(int)   atIndex: 0];
    [computeEncoder setBytes:&p_Height         length: sizeof(int)   atIndex: 1];
    [computeEncoder setBytes:&p_Transition     length: sizeof(float) atIndex: 2];
    [computeEncoder setBuffer:srcFromDeviceBuf offset: 0             atIndex: 3];
    [computeEncoder setBuffer:srcToDeviceBuf   offset: 0             atIndex: 4];
    [computeEncoder setBuffer:dstDeviceBuf     offset: 0             atIndex: 5];

    [computeEncoder dispatchThreadgroups:threadGroups threadsPerThreadgroup: threadGroupCount];

    [computeEncoder endEncoding];
    [commandBuffer commit];
}
