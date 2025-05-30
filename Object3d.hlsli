// 頂点シェーダーの出力
struct VertexShaderOutput
{ // (クリッピング空間での座標)
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
};