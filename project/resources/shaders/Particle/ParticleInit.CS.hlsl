// パーティクルと FreeList を初期化するコンピュートシェーダー（起動時に1回だけ実行）
//   ・全パーティクルを死亡状態にする
//   ・FreeList に全インデックス（0 〜 maxParticles-1）を積む
//   ・スタックトップ gFreeListIndex[0] を maxParticles-1 にする（＝全部空き）
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

// u0: パーティクルバッファ
RWStructuredBuffer<GPUParticleData> gParticles : register(u0);

// u1: FreeList本体
RWStructuredBuffer<uint> gFreeList : register(u1);

// u2: FreeListのスタックトップ
RWStructuredBuffer<int> gFreeListIndex : register(u2);

// b0: 更新用定数バッファ（maxParticles を使う）
cbuffer UpdateCB : register(b0)
{
    float deltaTime;
    float gravityY;
    uint maxParticles;
    float pad;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= maxParticles)
        return;

    // 全パーティクルを死亡状態に
    gParticles[i].alive = 0;
    gParticles[i].scale = 0.0f;

    // FreeList に自分のインデックスを登録（最初は全スロットが空き）
    gFreeList[i] = i;

    // スタックトップは先頭スレッドが1回だけ書く
    if (i == 0)
    {
        gFreeListIndex[0] = (int) maxParticles - 1;
    }
}
