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
    float4 transformedUV = mul(float32_t4(input.texcoord, 1.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTextrue.Sample(gSampler, transformedUV.xy);
    //float32_t4 textrueColor = gTextrue.Sample(gSampler, input.texcoord);
    if (gMaterial.enableLighting != 0) // Lightingする場合
    {
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
        
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        output.color.a = gMaterial.color.a * textureColor.a;
        
        // textrueのa値が0.5以下の時にPixelを棄却
        if (textureColor.a <= 0.5)
        {
            discard;
        }
        // textrueのa値が0の時にPixelを棄却
        if (textureColor.a == 0.0)
        {
            discard;
        }  
    }
    else
    {
        // Lightingしない場合。前回までと同じ演算
        output.color = gMaterial.color * textureColor;
        // output.colorのa値が0の時にPixelを棄却
        if (output.color.a == 0.0)
        {
            discard;
        }
    }
   
    
    return output;
    
}