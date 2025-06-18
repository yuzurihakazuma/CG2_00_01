#include "object3d.hlsli"


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
    float32_t4 textrueColor = gTextrue.Sample(gSampler, input.texcoord);
    if (gMaterial.enableLighting != 0) // Lightingする場合
    {
        float cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
        output.color = gMaterial.color * textrueColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
        
    }
    else
    {
        // Lightingしない場合。前回までと同じ演算
        output.color = gMaterial.color * textrueColor;
        
    }
    
    
    
    
    return output;
    
}