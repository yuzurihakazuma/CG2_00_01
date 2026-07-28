// GPU側でパーティクルを発生させるコンピュートシェーダー。
// FreeList から空きスロット番号を取り出して、そこへ発生リクエストを書き込む。
// （CPU側で空きスロットを推測して上書きする方式だと、生きている粒を潰す事故が起きる）
struct GPUParticleData
{
    float3 position;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
    float scale;
    uint alive; // 1=生存, 0=死亡
    float2 pad;
};

// u0: パーティクルバッファ（空きスロットへ書き込む）
RWStructuredBuffer<GPUParticleData> gParticles : register(u0);

// u1: FreeList（空きスロット番号のスタック）
RWStructuredBuffer<uint> gFreeList : register(u1);

// u2: FreeListの空き数カウンタ（[0]のみ使用）
RWStructuredBuffer<int> gFreeListIndex : register(u2);

// t0: このフレームの発生リクエスト（CPUが詰めた新規パーティクルの中身）
StructuredBuffer<GPUParticleData> gEmitRequests : register(t0);

// b0: 発生用定数バッファ
cbuffer EmitCB : register(b0)
{
    uint emitCount; // このフレームの発生数
    float3 pad2;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= emitCount)
        return;

    // FreeList から空きスロットを1つ取り出す（スタックのpop。加算前の値が返る）
    int freeListIndex;
    InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
    if (freeListIndex > 0)
    {
        uint particleIndex = gFreeList[freeListIndex - 1];
        gParticles[particleIndex] = gEmitRequests[i];
    }
    else
    {
        // 空きが無い：減らしたカウンタを戻して、このリクエストは破棄する
        InterlockedAdd(gFreeListIndex[0], 1);
    }
}
