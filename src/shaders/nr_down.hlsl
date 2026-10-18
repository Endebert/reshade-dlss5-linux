// The full frame down to the model's resolution: four bilinear taps per pixel, a box filter
// for the 1.5-2x reductions this is used for.
#include "nr_scale.hlsli"

Texture2D<float4> Src : register(t0);
Texture2D<float4> Unused : register(t1);
RWTexture2D<float4> Dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_width || id.y >= g_height)
        return;
    const float2 size = float2(g_width, g_height);
    const float2 p = float2(id.xy);
    float4 sum = Bilinear(Src, (p + float2(0.25, 0.25)) / size);
    sum += Bilinear(Src, (p + float2(0.75, 0.25)) / size);
    sum += Bilinear(Src, (p + float2(0.25, 0.75)) / size);
    sum += Bilinear(Src, (p + float2(0.75, 0.75)) / size);
    Dst[id.xy] = sum * 0.25;
}
