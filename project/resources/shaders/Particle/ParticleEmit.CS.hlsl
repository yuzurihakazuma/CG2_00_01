// パーティクルを発生させるコンピュートシェーダー
//   CPUが用意した発生リクエスト（位置・速度・寿命など）を受け取り、
//   FreeList から空きインデックスを取り出して（pop）そのスロットに書き込む。
//   空きが無ければ発生させない（生きているパーティクルを上書きしない）。
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

// t0: このフレームの発生リクエスト（CPUが書き込んだUploadバッファ）
StructuredBuffer<GPUParticleData> gEmitRequests : register(t0);

// u0: パーティクルバッファ
RWStructuredBuffer<GPUParticleData> gParticles : register(u0);

// u1: FreeList本体
RWStructuredBuffer<uint> gFreeList : register(u1);

// u2: FreeListのスタックトップ
RWStructuredBuffer<int> gFreeListIndex : register(u2);

// b0: 更新用定数バッファ（maxParticles を上限チェックに使う）
cbuffer UpdateCB : register(b0)
{
    float deltaTime;
    float gravityY;
    uint maxParticles;
    float pad;
};

// b1: 発生用定数バッファ
cbuffer EmitCB : register(b1)
{
    uint emitCount; // このフレームに発生させる数
    float3 emitPad;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= emitCount)
        return;

    // FreeList から空きインデックスを1つ取り出す（pop）。
    // InterlockedAdd は「減算前の値」を返すので、それがそのまま取り出し位置になる。
    int freeListIndex;
    InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);

    if (0 <= freeListIndex && freeListIndex < (int) maxParticles)
    {
        // 空きスロットを取得してパーティクルを書き込む
        uint particleIndex = gFreeList[freeListIndex];
        gParticles[particleIndex] = gEmitRequests[i];
    }
    else
    {
        // 空きが無かった（使い切っていた）ので、カウンターを戻して何もしない。
        // ※これにより上限到達時も FreeList が壊れず、
        //   誰かが死んで返却されればまた発生できるようになる。
        InterlockedAdd(gFreeListIndex[0], 1);
    }
}
