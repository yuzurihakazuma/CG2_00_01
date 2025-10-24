#include "Particle.hlsli"

struct ParticleForGPU
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4 color;
};

// VS用：インスタンシング行列（t0）
StructuredBuffer<ParticleForGPU> gParticle : register(t0);

// 頂点シェーダー入力
struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal   : NORMAL0;
};

// 頂点シェーダー本体
VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;

    ParticleForGPU tm = gParticle[instanceId];

    // 座標変換
    output.position = mul(input.position, tm.WVP);

    // そのまま渡す
    output.texcoord = input.texcoord;

    // 法線はWorldの3x3で変換（非一様スケールなら本来は逆転置）
    float32_t3x3 world3x3 = (float32_t3x3)tm.World;
    output.normal = normalize(mul(input.normal, world3x3));
    
    output.color = gParticle[instanceId].color;
    

    return output;
}
