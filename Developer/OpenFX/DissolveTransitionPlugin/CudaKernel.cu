__global__ void DissolveTransitionKernel(int p_Width, int p_Height, float p_Transition,
                                         const float* p_SrcFromInput, const float* p_SrcToInput, float* p_Output)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if ((x < p_Width) && (y < p_Height))
    {
        const int index = ((y * p_Width) + x) * 4;

        const float alphaWeightFrom = p_SrcFromInput[index + 3] * (1.0f - p_Transition);
        const float alphaWeightTo   = p_SrcToInput[index + 3] * p_Transition;

        // Dissolve RGB channels
        p_Output[index + 0] = alphaWeightFrom * p_SrcFromInput[index + 0] + alphaWeightTo * p_SrcToInput[index + 0];
        p_Output[index + 1] = alphaWeightFrom * p_SrcFromInput[index + 1] + alphaWeightTo * p_SrcToInput[index + 1];
        p_Output[index + 2] = alphaWeightFrom * p_SrcFromInput[index + 2] + alphaWeightTo * p_SrcToInput[index + 2];

        // Dissolve alpha channel
        p_Output[index + 3] = alphaWeightFrom + alphaWeightTo;
    }
}

void RunCudaKernel(void* p_Stream, int p_Width, int p_Height, float p_Transition, const float* p_SrcFromInput, const float* p_SrcToInput, float* p_Output)
{
    dim3 threads(128, 1, 1);
    dim3 blocks(((p_Width + threads.x - 1) / threads.x), p_Height, 1);
    cudaStream_t stream = static_cast<cudaStream_t>(p_Stream);

    DissolveTransitionKernel<<<blocks, threads, 0, stream>>>(p_Width, p_Height, p_Transition, p_SrcFromInput, p_SrcToInput, p_Output);
}
