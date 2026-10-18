// Vulkan transport: ReShade's depth (any depth format, sampled through its depth aspect) to
// R32_FLOAT, which can be copied into the texture shared with the D3D12 side. Compiled to SPIR-V,
// once as is and once with FLIP (RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN). The target is normally the
// source's size; a differently sized one is resampled.
[[vk::binding(0, 0)]] Texture2D<float> Src;
[[vk::binding(0, 1)]] [[vk::image_format("r32f")]] RWTexture2D<float> Dst;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h, sw, sh;
    Dst.GetDimensions(w, h);
    Src.GetDimensions(sw, sh);
    if (id.x >= w || id.y >= h)
        return;
    uint2 s = uint2(id.x * sw / w, id.y * sh / h);
#if FLIP
    s.y = sh - 1 - s.y;
#endif
    Dst[id.xy] = Src.Load(int3(s, 0));
}
