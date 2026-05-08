#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0); // マテリアルの定数バッファ
Texture2D<float4> gTexture : register(t0); // テクスチャ
SamplerState gSampler : register(s0); // サンプラー
ConstantBuffer<DirectionalLightData> gDirectionalLightData : register(b1);
ConstantBuffer<Camera> gCamera : register(b2); // カメラの定数バッファ
ConstantBuffer<PointLight> gPointLight : register(b3); // 点光源の定数バッファ
ConstantBuffer<SpotLight> gSpotLight : register(b4); // スポットライトの定数バッファ

Texture2D<float4> gNoiseTexture : register(t1); // ノイズテクスチャ
ConstantBuffer<DissolveData> gDissolve : register(b5); // ディゾルブ用の定数バッファ

TextureCube<float32_t4> gEnvironmentTexture : register(t3); // 環境マップテクスチャ


struct PixelShaderOutput
{
    float4 color : SV_TARGET0; // 0枚目のキャンバス（色）
    float4 mask : SV_TARGET1; // 1枚目のキャンバス（エフェクトをかけるかどうかのフラグ！）
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    if (gDissolve.threshold > 0.0f)
    {
        // 1. ノイズテクスチャからノイズの暗さをサンプリングする
        float noiseValue = gNoiseTexture.Sample(gSampler, transformedUV.xy).r;
        
        // 2. ノイズの暗さ が「進行度(threshold)」より小さかったら消滅させる！
        if (noiseValue <= gDissolve.threshold)
        {
            discard;
        }
    }
    float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
    
    // --- 光の計算結果を入れる変数 ---
    float3 diffuseDirectional = float3(0, 0, 0);
    float3 specularDirectional = float3(0, 0, 0);
    float3 diffusePoint = float3(0, 0, 0);
    float3 specularPoint = float3(0, 0, 0);
    float3 diffuseSpot = float3(0, 0, 0); 
    float3 specularSpot = float3(0, 0, 0);
    
    
    if (gMaterial.enableLighting != 0)
    {
        for (int i = 0; i < gDirectionalLightData.activeCount; ++i)
        {
            float NdotL_Dir = dot(normalize(input.normal), -gDirectionalLightData.lights[i].direction);
            float cos_Dir = pow(NdotL_Dir * 0.5f + 0.5f, 2.0f);
            
            // += を使って光を足し合わせる
            diffuseDirectional += gMaterial.color.rgb * textureColor.rgb * gDirectionalLightData.lights[i].color.rgb * cos_Dir * gDirectionalLightData.lights[i].intensity;
            
            float3 halfVector_Dir = normalize(-gDirectionalLightData.lights[i].direction + toEye);
            float NDotH_Dir = dot(normalize(input.normal), halfVector_Dir);
            float specularPow_Dir = pow(saturate(NDotH_Dir), gMaterial.shininess);
            
            // += を使ってハイライトを足し合わせる
            specularDirectional += gDirectionalLightData.lights[i].color.rgb * gDirectionalLightData.lights[i].intensity * specularPow_Dir * float3(1.0f, 1.0f, 1.0f);
        }
        //  点光源 (Point Light) の計算
        float3 pointLightDirection = normalize(input.worldPosition - gPointLight.position);
        float NdotL_Point = dot(normalize(input.normal), -pointLightDirection);
        float cos_Point = pow(NdotL_Point * 0.5f + 0.5f, 2.0f);
        
        // 光源から現在のピクセルまでの距離を測る
        float distance = length(gPointLight.position - input.worldPosition);
        // 距離が radius に近づくほど 0 になるような係数(factor)を作る
        float factor = pow(saturate(-distance / gPointLight.radius + 1.0f), gPointLight.decay);
        
        
        diffusePoint = gMaterial.color.rgb * textureColor.rgb * gPointLight.color.rgb * cos_Point * gPointLight.intensity * factor;
        
        
        float3 halfVector_Point = normalize(-pointLightDirection + toEye);
        float NDotH_Point = dot(normalize(input.normal), halfVector_Point);
        float specularPow_Point = pow(saturate(NDotH_Point), gMaterial.shininess);
        // 距離が radius に近づくほど 0 になるような係数(factor)を作る
        specularPoint = gPointLight.color.rgb * gPointLight.intensity * specularPow_Point * float3(1.0f, 1.0f, 1.0f) * factor;
        
        float3 spotLightDirectionOnSurface = normalize(input.worldPosition - gSpotLight.position);
        float cosAngle = dot(spotLightDirectionOnSurface, gSpotLight.direction);
        
        // フォールオフ(角度による減衰)の計算
        float falloffFactor = saturate((cosAngle - gSpotLight.cosAngle) / (gSpotLight.cosFalloffStart - gSpotLight.cosAngle));
        
        // 距離による減衰の計算
        float spotDistance = length(gSpotLight.position - input.worldPosition);
        float attenuationFactor = pow(saturate(-spotDistance / gSpotLight.distance + 1.0f), gSpotLight.decay);
        
        // 光の当たり方（Diffuse / Specular）の計算
        float NdotL_Spot = dot(normalize(input.normal), -spotLightDirectionOnSurface);
        float cos_Spot = pow(NdotL_Spot * 0.5f + 0.5f, 2.0f);
        
        diffuseSpot = gMaterial.color.rgb * textureColor.rgb * gSpotLight.color.rgb * cos_Spot * gSpotLight.intensity * attenuationFactor * falloffFactor;
        
        float3 halfVector_Spot = normalize(-spotLightDirectionOnSurface + toEye);
        float NDotH_Spot = dot(normalize(input.normal), halfVector_Spot);
        float specularPow_Spot = pow(saturate(NDotH_Spot), gMaterial.shininess);
        
        specularSpot = gSpotLight.color.rgb * gSpotLight.intensity * specularPow_Spot * float3(1.0f, 1.0f, 1.0f) * attenuationFactor * falloffFactor;
        
        
        float3 ambient = gMaterial.color.rgb * textureColor.rgb * 0.15f; // 0.15は環境光の強さ。お好みで調整してください
        output.color.rgb = ambient + diffuseDirectional + specularDirectional + diffusePoint + specularPoint + diffuseSpot + specularSpot;
        output.color.a = gMaterial.color.a * textureColor.a;
        
        // 環境マップによる反射の計算
        float3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
        
        float3 reflecttedVector = reflect(cameraToPosition, normalize(input.normal));
        
        float4 environmentColor = gEnvironmentTexture.Sample(gSampler, reflecttedVector);
        
        output.color.rgb += environmentColor.rgb * gMaterial.environmentCoefficient; // 環境マップの影響を加える。0.5はお好みで調整してください
        
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }
    
    if (gMaterial.emissive > 1.0f)
    {
        // output.color.rgb（通常の色）に、emissive（光る強さ）を掛け算して書き込む！
        // これにより、emissiveが 5.0 なら、マスク画像の明るさも 5.0 になる
        output.mask = float4(output.color.rgb * gMaterial.emissive, 1.0f);
    }
    else
    {
        output.mask = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    return output;
}