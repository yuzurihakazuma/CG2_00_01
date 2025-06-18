#include "object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
};
ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float32_t4> gTextrue : register(t0);
SamplerState gSampler : register(s0);


struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t4 textrueColor = gTextrue.Sample(gSampler, input.texcoord);
    output.color = gMaterial.color * textrueColor;
    return output;
    
}