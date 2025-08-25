__DEVICE__ inline float ConvertRGBToLum(float3 p_Val)
{
    return (0.299f * p_Val.x + 0.587f * p_Val.y + 0.114f * p_Val.z);
}
