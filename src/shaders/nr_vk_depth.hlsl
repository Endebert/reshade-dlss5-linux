// Vulkan transport: ReShade's depth (any depth format, sampled through its depth aspect) to
// R32_FLOAT, which can be copied into the texture shared with the D3D12 side. Compiled to SPIR-V.
[[vk::binding(0, 0)]] Texture2D<float> Src;
[[vk::binding(0, 1)]] [[vk::image_format("r32f")]] RWTexture2D<float> Dst;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    Dst.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;
    Dst[id.xy] = Src.Load(int3(id.xy, 0));
}
