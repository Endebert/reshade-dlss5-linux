// What the model changed, at its own resolution: answer minus the frame it was given.
#include "nr_scale.hlsli"

Texture2D<float4> Answer : register(t0);
Texture2D<float4> Given : register(t1);
RWTexture2D<float4> Dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_width || id.y >= g_height)
        return;
    Dst[id.xy] = Answer.Load(int3(id.xy, 0)) - Given.Load(int3(id.xy, 0));
}
