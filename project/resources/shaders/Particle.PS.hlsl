#include "Particle.hlsli"


ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float32_t4> gTextrue : register(t0);
SamplerState gSampler : register(s0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTextrue.Sample(gSampler, transformedUV.xy);
    output.color = gMaterial.color * textureColor * input.color;
    
    // textrueのa値が0の時にPixelを棄却
    if (textureColor.a == 0.0)
    {
        discard;
    }
  
    return output;
}