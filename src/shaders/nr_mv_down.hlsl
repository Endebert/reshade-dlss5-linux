// Motion vectors down to the model's resolution. They are in UV units, so only the grid
// changes; the nearest vector is taken rather than blending across an object's edge.
#include "nr_scale.hlsli"

Texture2D<float2> Src : register(t0);
Texture2D<float2> Unused : register(t1);
RWTexture2D<float2> Dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_width || id.y >= g_height)
        return;
    uint w, h;
    Src.GetDimensions(w, h);
    const uint2 s = min(uint2((float2(id.xy) + 0.5) * float2(w, h) / float2(g_width, g_height)),
                        uint2(w - 1, h - 1));
    Dst[id.xy] = Src.Load(int3(s, 0));
}
