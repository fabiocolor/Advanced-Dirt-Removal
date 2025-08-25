#import <Metal/Metal.h>

#include <unordered_map>
#include <mutex>

// Blend the inputs layer by layer in the order Black Screen, p_PrevInput, p_CurrInput and p_NextInput.
const char* kernelSource =
"#include <metal_stdlib>                                                                                                                                                         \n" \
"using namespace metal;                                                                                                                                                          \n" \
"kernel void TemporalBlurKernel(constant int& p_Width, constant int& p_Height, constant float& p_Blend,                                                                          \n" \
"                               const device float* p_PrevInput, device float* p_CurrInput, device float* p_NextInput,                                                           \n" \
"                               device float* p_Output, uint2 id [[ thread_position_in_grid ]])                                                                                  \n" \
"{                                                                                                                                                                               \n" \
"   if ((id.x < p_Width) && (id.y < p_Height))                                                                                                                                   \n" \
"   {                                                                                                                                                                            \n" \
"       const int index = ((id.y * p_Width) + id.x) * 4;                                                                                                                         \n" \
"       p_Output[index + 0] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 0] + (1 - p_Blend) * p_CurrInput[index + 0]) + (1 - p_Blend) * p_NextInput[index + 0]; \n" \
"       p_Output[index + 1] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 1] + (1 - p_Blend) * p_CurrInput[index + 1]) + (1 - p_Blend) * p_NextInput[index + 1]; \n" \
"       p_Output[index + 2] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 2] + (1 - p_Blend) * p_CurrInput[index + 2]) + (1 - p_Blend) * p_NextInput[index + 2]; \n" \
"       p_Output[index + 3] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 3] + (1 - p_Blend) * p_CurrInput[index + 3]) + (1 - p_Blend) * p_NextInput[index + 3]; \n" \
"   }                                                                                                                                 \n" \
"}                                                                                                                                    \n";

std::mutex s_PipelineQueueMutex;
typedef std::unordered_map<id<MTLCommandQueue>, id<MTLComputePipelineState>> PipelineQueueMap;
PipelineQueueMap s_PipelineQueueMap;

void RunMetalKernel(void* p_CmdQ, int p_Width, int p_Height, float p_Blend, const float* p_PrevInput, const float* p_CurrInput, const float* p_NextInput, float* p_Output)
{
    const char* kernelName = "TemporalBlurKernel";

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

    id<MTLBuffer> prevSrcDeviceBuf = reinterpret_cast<id<MTLBuffer> >(const_cast<float *>(p_PrevInput));
    id<MTLBuffer> currSrcDeviceBuf = reinterpret_cast<id<MTLBuffer> >(const_cast<float *>(p_CurrInput));
    id<MTLBuffer> nextSrcDeviceBuf = reinterpret_cast<id<MTLBuffer> >(const_cast<float *>(p_NextInput));
    id<MTLBuffer> dstDeviceBuf = reinterpret_cast<id<MTLBuffer> >(p_Output);

    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    commandBuffer.label = [NSString stringWithFormat:@"TemporalBlurKernel"];

    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:pipelineState];

    int exeWidth = [pipelineState threadExecutionWidth];
    MTLSize threadGroupCount = MTLSizeMake(exeWidth, 1, 1);
    MTLSize threadGroups     = MTLSizeMake((p_Width + exeWidth - 1)/exeWidth, p_Height, 1);

    [computeEncoder setBytes:&p_Width          length:sizeof(int)   atIndex:0];
    [computeEncoder setBytes:&p_Height         length:sizeof(int)   atIndex:1];
    [computeEncoder setBytes:&p_Blend          length:sizeof(float) atIndex:2];
    [computeEncoder setBuffer:prevSrcDeviceBuf offset: 0 atIndex: 3];
    [computeEncoder setBuffer:currSrcDeviceBuf offset: 0 atIndex: 4];
    [computeEncoder setBuffer:nextSrcDeviceBuf offset: 0 atIndex: 5];
    [computeEncoder setBuffer:dstDeviceBuf     offset: 0 atIndex: 6];

    [computeEncoder dispatchThreadgroups:threadGroups threadsPerThreadgroup: threadGroupCount];

    [computeEncoder endEncoding];
    [commandBuffer commit];
}
