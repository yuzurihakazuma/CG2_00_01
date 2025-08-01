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

    // UV変換（アフィン行列使用）
    float4 transformedUV = mul(float4(input.texcoord, 1.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTextrue.Sample(gSampler, transformedUV.xy);

    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(-gDirectionalLight.direction);

    float3 baseColor = (gMaterial.color.rgb * textureColor.rgb);
    float lightingFactor = 1.0f;

    if (gMaterial.enableLighting == 1)
    {
        // Lambert
        lightingFactor = max(dot(normal, lightDir), 0.0f);
    }
    else if (gMaterial.enableLighting == 2)
    {
        // Half-Lambert
        float NdotL = dot(normal, lightDir);
        lightingFactor = pow(NdotL * 0.5f + 0.5f, 2.0f);
    }

    if (gMaterial.enableLighting != 0)
    {
        baseColor *= gDirectionalLight.color.rgb * gDirectionalLight.intensity * lightingFactor;
    }

    output.color = float4(baseColor, gMaterial.color.a * textureColor.a);
    return output;
}