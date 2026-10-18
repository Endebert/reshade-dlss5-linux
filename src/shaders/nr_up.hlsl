// The full-resolution frame plus the model's change, scaled up: the UI and anything else drawn
// at full resolution keeps its sharpness, and the model's work lands where it belongs.
#include "nr_scale.hlsli"

Texture2D<float4> Frame : register(t0);
Texture2D<float4> Delta : register(t1);
RWTexture2D<float4> Dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_width || id.y >= g_height)
        return;
    const float4 o = Frame.Load(int3(id.xy, 0));
    const float2 uv = (float2(id.xy) + 0.5) / float2(g_width, g_height);
    Dst[id.xy] = float4(o.rgb + Bilinear(Delta, uv).rgb, o.a);
}
