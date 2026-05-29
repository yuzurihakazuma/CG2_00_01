// FreeListからスロットを取得してパーティクルを発生させるコンピュートシェーダー
struct GPUParticleData
{
    float3 position;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
    float scale;
    uint alive;
    float2 pad;
};

// u0: パーティクルバッファ
RWStructuredBuffer<GPUParticleData> gParticles      : register(u0);
// u1: FreeListのスタックトップ
RWStructuredBuffer<uint>            gFreeListIndex  : register(u1);
// u2: FreeListの本体
RWStructuredBuffer<uint>            gFreeList       : register(u2);

// t0: 今フレームのEmitリクエスト (CPUがUploadバッファに書き込んだもの)
StructuredBuffer<GPUParticleData>   gEmitRequests   : register(t0);

// b0: Emit数
cbuffer EmitCB : register(b0)
{
    uint emitCount;
    float3 cbPad;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= emitCount)
        return;

    // FreeListからスロットを1つ取得 (スタックのpop)
    uint prev;
    InterlockedAdd(gFreeListIndex[0], -1, prev);

    // 空きがなければ何もしない (アンダーフロー防止)
    if (prev == 0)
    {
        // 取り過ぎたので戻す
        InterlockedAdd(gFreeListIndex[0], 1, prev);
        return;
    }

    uint slot = gFreeList[prev - 1];
    gParticles[slot] = gEmitRequests[i];
}
