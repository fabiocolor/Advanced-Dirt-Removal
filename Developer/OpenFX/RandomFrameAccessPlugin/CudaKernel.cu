__global__ void TemporalBlurKernel(int p_Width, int p_Height, float p_Blend,
                                   const float* p_PrevInput, const float* p_CurrInput, const float* p_NextInput, float* p_Output)
{
   const int x = blockIdx.x * blockDim.x + threadIdx.x;
   const int y = blockIdx.y * blockDim.y + threadIdx.y;

   if ((x < p_Width) && (y < p_Height))
   {
      const int index = ((y * p_Width) + x) * 4;

      // Blend the inputs layer by layer in the order Black Screen, p_PrevInput, p_CurrInput and p_NextInput.
      p_Output[index + 0] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 0] + (1 - p_Blend) * p_CurrInput[index + 0]) + (1 - p_Blend) * p_NextInput[index + 0];
      p_Output[index + 1] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 1] + (1 - p_Blend) * p_CurrInput[index + 1]) + (1 - p_Blend) * p_NextInput[index + 1];
      p_Output[index + 2] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 2] + (1 - p_Blend) * p_CurrInput[index + 2]) + (1 - p_Blend) * p_NextInput[index + 2];
      p_Output[index + 3] = p_Blend * (p_Blend * (1.0f - p_Blend) * p_PrevInput[index + 3] + (1 - p_Blend) * p_CurrInput[index + 3]) + (1 - p_Blend) * p_NextInput[index + 3];
   }
}

void RunCudaKernel(void* p_Stream, int p_Width, int p_Height, float p_Blend, const float* p_PrevInput, const float* p_CurrInput, const float* p_NextInput, float* p_Output)
{
    dim3 threads(128, 1, 1);
    dim3 blocks(((p_Width + threads.x - 1) / threads.x), p_Height, 1);
    cudaStream_t stream = static_cast<cudaStream_t>(p_Stream);

    TemporalBlurKernel<<<blocks, threads, 0, stream>>>(p_Width, p_Height, p_Blend, p_PrevInput, p_CurrInput, p_NextInput, p_Output);
}
