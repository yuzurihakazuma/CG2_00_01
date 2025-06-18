#include "object3d.hlsli"
struct TransformationMatirx
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};
ConstantBuffer<TransformationMatirx> gTransformationMatrix : register(b0);


// 頂点シェーダーの入力
struct VertexShaderInput
{
    // 変換済みの座標(スクリーン空間)
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    
};
// 頂点シェーダー本体
VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) gTransformationMatrix.World));
   
    
    
    return output;
}

