// Reduced-resolution standalone: helpers shared by the down, delta, motion and up passes.
#include "nr_common.hlsli"

// Bilinear filtering by hand (the compose root signature has no sampler).
float4 Bilinear(Texture2D<float4> t, float2 uv)
{
    uint w, h;
    t.GetDimensions(w, h);
    const float2 p = uv * float2(w, h) - 0.5;
    const int2 i = int2(floor(p));
    const float2 f = p - float2(i);
    const int2 hi = int2(w, h) - 1;
    const int2 a = clamp(i, 0, hi);
    const int2 b = clamp(i + 1, 0, hi);
    const float4 c00 = t.Load(int3(a.x, a.y, 0));
    const float4 c10 = t.Load(int3(b.x, a.y, 0));
    const float4 c01 = t.Load(int3(a.x, b.y, 0));
    const float4 c11 = t.Load(int3(b.x, b.y, 0));
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}
