// 頂点シェーダーの出力
struct VertexShaderOutput{
    // (クリッピング空間での座標)
    float32_t4 position : SV_POSITION;
    
};
// 頂点シェーダーの入力
struct VertexShaderInput
{
    // 変換済みの座標(スクリーン空間)
    float32_t4 position : POSITIONT;
    
};
// 頂点シェーダー本体
VertexShaderOutput main(VertexShaderInput input){
    VertexShaderOutput output;
    output.position = input.position;
    
    return output;
}

