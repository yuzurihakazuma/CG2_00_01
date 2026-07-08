// デバッグ線描画用 ピクセルシェーダー
// シーンのMRT（color + mask の2枚）に描くため2出力。マスクは常に0（効果対象外）。
struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
    float4 mask : SV_TARGET1;
};

PSOutput main(PSInput input)
{
    PSOutput output;
    output.color = input.color;
    output.mask = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return output;
}
