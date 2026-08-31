#include "GPUParticleManager.h"

#include <cassert>
#include <cstring>

#include "engine/base/DirectXCommon.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/graphics/TextureManager.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"

using namespace MatrixMath;

// -------------------------------------------------------
//  初期化
// -------------------------------------------------------
void GPUParticleManager::Initialize(
    DirectXCommon* dxCommon, SrvManager* srvManager,
    const std::string& textureFilePath){
    assert(dxCommon);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateParticleBuffer();
    CreateFreeListBuffers();
    CreateConstantBuffers();
    CreateVertexBuffer();

    // テクスチャ読み込み
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    TextureData texData = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
        textureFilePath, cmd);
    textureSrvIndex_ = texData.srvIndex;
}

// -------------------------------------------------------
//  パーティクルバッファ (UAV + SRV)
// -------------------------------------------------------
void GPUParticleManager::CreateParticleBuffer(){
    const size_t bufferSize = sizeof(GPUParticleData) * kMaxParticles;

    // ① Default Heap に UAVバッファを作成
    particleBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(bufferSize);

    // ② UAVとして登録 (Computeシェーダーで書き込む用)
    uavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(
        uavIndex_, particleBuffer_.Get(),
        kMaxParticles, sizeof(GPUParticleData));

    // ③ SRVとして登録 (描画時にVSで読む用)
    srvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        srvIndex_, particleBuffer_.Get(),
        kMaxParticles, sizeof(GPUParticleData));

    // ④ 初期化: 全パーティクルを alive=0 にする
    //    Upload Heap の一時バッファを使ってコピー
    initBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(bufferSize); GPUParticleData* initData = nullptr;
    initBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&initData));
    ZeroMemory(initData, bufferSize); // alive=0 で初期化
    initBuffer_->Unmap(0, nullptr);

    auto* cmd = dxCommon_->GetCommandList();

    // COMMON → COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd->ResourceBarrier(1, &barrier);

    cmd->CopyResource(particleBuffer_.Get(), initBuffer_.Get());

    // COPY_DEST → UNORDERED_ACCESS
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cmd->ResourceBarrier(1, &barrier);

    // ⑤ Emit用のUploadバッファを永続確保（EmitCS が t0 の発生リクエストとして読む）
    const size_t emitSize = sizeof(GPUParticleData) * kMaxEmitPerFrame;
    emitUploadBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(emitSize);
    emitUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&emitUploadData_));
    emitSrvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        emitSrvIndex_, emitUploadBuffer_.Get(),
        kMaxEmitPerFrame, sizeof(GPUParticleData));
}

// -------------------------------------------------------
//  FreeList（空きスロット管理）バッファ (UAV)
//   ・freeListBuffer_      : 空きスロット番号のスタック（uint × kMaxParticles）
//   ・freeListIndexBuffer_ : 空き数カウンタ（int × 1）
//   死亡時に UpdateCS が番号を返却し、発生時に EmitCS が取り出して再利用する。
//   これが無いとCPU側の循環インデックスが生存中の粒を上書きしてしまう
// -------------------------------------------------------
void GPUParticleManager::CreateFreeListBuffers(){
    auto* cmd = dxCommon_->GetCommandList();

    // 遷移バリアの共通形
    auto transition = [&](ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = res;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        cmd->ResourceBarrier(1, &barrier);
    };

    // --- FreeList本体：初期値は「全スロットが空き」= freeList[i] = i ---
    const size_t freeListSize = sizeof(uint32_t) * kMaxParticles;
    freeListBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(freeListSize);
    freeListUavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(
        freeListUavIndex_, freeListBuffer_.Get(),
        kMaxParticles, sizeof(uint32_t));

    freeListInitBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(freeListSize);
    uint32_t* freeListInit = nullptr;
    freeListInitBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&freeListInit));
    for ( uint32_t i = 0; i < kMaxParticles; ++i ) { freeListInit[i] = i; }
    freeListInitBuffer_->Unmap(0, nullptr);

    transition(freeListBuffer_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(freeListBuffer_.Get(), freeListInitBuffer_.Get());
    transition(freeListBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // --- 空き数カウンタ：初期値は kMaxParticles（全部空き） ---
    freeListIndexBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(sizeof(int32_t));
    freeListIndexUavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(
        freeListIndexUavIndex_, freeListIndexBuffer_.Get(),
        1, sizeof(int32_t));

    freeListIndexInitBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(sizeof(int32_t));
    int32_t* counterInit = nullptr;
    freeListIndexInitBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&counterInit));
    *counterInit = static_cast<int32_t>( kMaxParticles );
    freeListIndexInitBuffer_->Unmap(0, nullptr);

    transition(freeListIndexBuffer_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(freeListIndexBuffer_.Get(), freeListIndexInitBuffer_.Get());
    transition(freeListIndexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

// -------------------------------------------------------
//  定数バッファ
// -------------------------------------------------------
void GPUParticleManager::CreateConstantBuffers(){
    // Compute用
    updateCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleUpdateCB) + 0xFF) & ~0xFF);
    updateCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&updateCBData_));

    // Camera用
    cameraCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleCameraCB) + 0xFF) & ~0xFF);
    cameraCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraCBData_));

    // Material用 (白・ライトなし)
    materialCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleMaterial) + 0xFF) & ~0xFF);
    GPUParticleMaterial* mat = nullptr;
    materialCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&mat));
    mat->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    mat->enableLighting = 0;
    mat->uvTransform = MakeIdentity4x4();
    materialCBResource_->Unmap(0, nullptr);

    // Emit用 (b0: このフレームの発生数)
    emitCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(0x100);
    emitCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitCBData_));
    *emitCBData_ = 0;
}

// -------------------------------------------------------
//  板ポリ頂点バッファ (ParticleManagerと同じ形式)
// -------------------------------------------------------
void GPUParticleManager::CreateVertexBuffer(){
    struct VertexData { Vector4 position; Vector2 texcoord; Vector3 normal; };

    VertexData vertices[] = {
        {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };

    vertexResource_ = ResourceFactory::GetInstance()->CreateBufferResource(sizeof(vertices));

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    void* data = nullptr;
    vertexResource_->Map(0, nullptr, &data);
    std::memcpy(data, vertices, sizeof(vertices));
    vertexResource_->Unmap(0, nullptr);
}

// -------------------------------------------------------
//  毎フレーム更新
// -------------------------------------------------------
void GPUParticleManager::Update(float deltaTime, Camera* camera){
    updateCBData_->deltaTime = deltaTime;
    updateCBData_->gravityY = gravityY_;
    updateCBData_->maxParticles = kMaxParticles;

    cameraCBData_->view = camera->GetViewMatrix();
    cameraCBData_->projection = camera->GetProjectionMatrix();
}

// -------------------------------------------------------
//  Emit (発生キューに追加)
// -------------------------------------------------------
void GPUParticleManager::Emit(const Vector3& position, const Vector3& velocity,
    float lifeTime, float scale, const Vector4& color){
   
    
    
    if (emitQueue_.size() >= kMaxEmitPerFrame) return; // 上限チェック

    GPUParticleData p = {};
    p.position = position;
    p.velocity = velocity;
    p.lifeTime = lifeTime;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = scale;
    p.alive = 1;

    // 書き込み先スロットはGPU側の EmitCS が FreeList から取り出して決める。
    //   （以前のCPU循環インデックス方式は、生存中のパーティクルを上書きする事故があった）
    emitQueue_.push_back(p);
}

// -------------------------------------------------------
//  Emitキューをアップロード
//   Uploadバッファ（EmitCS の t0）へ発生リクエストを書き写すだけ。
//   スロット決定と particleBuffer_ への書き込みは EmitCS が FreeList を使って行う
// -------------------------------------------------------
void GPUParticleManager::UploadEmitQueue(ID3D12GraphicsCommandList* commandList){
    (void)commandList;
    lastFrameEmitCount_ = static_cast< uint32_t >( emitQueue_.size() );
    totalEmitted_ += lastFrameEmitCount_;

    if (emitQueue_.empty()) { *emitCBData_ = 0; return; }

    std::memcpy(emitUploadData_, emitQueue_.data(),
                sizeof(GPUParticleData) * emitQueue_.size());
    *emitCBData_ = lastFrameEmitCount_;
    emitQueue_.clear();
}

// -------------------------------------------------------
//  Dispatch (Computeシェーダー実行)
// -------------------------------------------------------
void GPUParticleManager::Dispatch(ID3D12GraphicsCommandList* commandList){
    // 新しいパーティクル（発生リクエスト）をUploadバッファへ書き写す
    UploadEmitQueue(commandList);

    // UAVバリア (前フレームのCompute書き込みが完了するまで待つ)
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = particleBuffer_.Get();
    commandList->ResourceBarrier(1, &uavBarrier);

    // DescriptorHeapをセット
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // --- Emit: FreeListから空きスロットを取り出して新しい粒を書き込む ---
    if ( lastFrameEmitCount_ > 0 ) {
        PipelineManager::GetInstance()->SetGPUParticleEmitPipeline(commandList);
        srvManager_->SetComputeRootDescriptorTable(0, uavIndex_);              // [0]: u0 パーティクル
        srvManager_->SetComputeRootDescriptorTable(1, freeListUavIndex_);      // [1]: u1 FreeList
        srvManager_->SetComputeRootDescriptorTable(2, freeListIndexUavIndex_); // [2]: u2 空き数カウンタ
        srvManager_->SetComputeRootDescriptorTable(3, emitSrvIndex_);          // [3]: t0 発生リクエスト
        commandList->SetComputeRootConstantBufferView(
            4, emitCBResource_->GetGPUVirtualAddress());                       // [4]: b0 emitCount
        UINT emitGroups = ( lastFrameEmitCount_ + 255 ) / 256;
        commandList->Dispatch(emitGroups, 1, 1);

        // Emitの書き込みが終わってから Update を走らせる（同フレーム内の順序保証）
        D3D12_RESOURCE_BARRIER emitBarriers[3] = {};
        emitBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        emitBarriers[0].UAV.pResource = particleBuffer_.Get();
        emitBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        emitBarriers[1].UAV.pResource = freeListBuffer_.Get();
        emitBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        emitBarriers[2].UAV.pResource = freeListIndexBuffer_.Get();
        commandList->ResourceBarrier(3, emitBarriers);
    }

    // --- Update: 物理更新＋寿命判定。死亡した粒の番号は FreeList へ返却する ---
    PipelineManager::GetInstance()->SetGPUParticleComputePipeline(commandList);

    srvManager_->SetComputeRootDescriptorTable(0, uavIndex_);              // [0]: u0 パーティクル
    srvManager_->SetComputeRootDescriptorTable(1, freeListUavIndex_);      // [1]: u1 FreeList
    srvManager_->SetComputeRootDescriptorTable(2, freeListIndexUavIndex_); // [2]: u2 空き数カウンタ
    commandList->SetComputeRootConstantBufferView(
        3, updateCBResource_->GetGPUVirtualAddress());                     // [3]: b0 更新用CB

    // Dispatch (256スレッドのグループを必要数起動)
    UINT groupCount = (kMaxParticles + 255) / 256;
    commandList->Dispatch(groupCount, 1, 1);

    // UAV → SRV (描画パスで読むため状態遷移)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

// -------------------------------------------------------
//  Draw (描画)
// -------------------------------------------------------
void GPUParticleManager::Draw(ID3D12GraphicsCommandList* commandList){
    // DescriptorHeapをセット
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // 描画用パイプラインをセット
    PipelineManager::GetInstance()->SetGPUParticleDrawPipeline(commandList);

    // [0]: マテリアル (b0 PIXEL)
    commandList->SetGraphicsRootConstantBufferView(
        0, materialCBResource_->GetGPUVirtualAddress());
    // [1]: パーティクルデータSRV (t1 VERTEX)
    srvManager_->SetGraphicsRootDescriptorTable(1, srvIndex_);
    // [2]: テクスチャ (t0 PIXEL)
    srvManager_->SetGraphicsRootDescriptorTable(2, textureSrvIndex_);
    // [3]: ライトCB (b1 PIXEL) ← 今回はダミーでmaterialと同じアドレスを指す
    commandList->SetGraphicsRootConstantBufferView(
        3, materialCBResource_->GetGPUVirtualAddress());
    // [4]: カメラ行列 (b1 VERTEX)
    commandList->SetGraphicsRootConstantBufferView(
        4, cameraCBResource_->GetGPUVirtualAddress());

    // 板ポリ6頂点 × kMaxParticles インスタンス
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->DrawInstanced(6, kMaxParticles, 0, 0);

    // SRV → UAV に戻す (次フレームのComputeのため)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &barrier);
}

// -------------------------------------------------------
//  終了処理
// -------------------------------------------------------
void GPUParticleManager::Finalize(){
    if (emitUploadData_) {
        emitUploadBuffer_->Unmap(0, nullptr);
        emitUploadData_ = nullptr;
    }
    if (updateCBData_) {
        updateCBResource_->Unmap(0, nullptr);
        updateCBData_ = nullptr;
    }
    if (cameraCBData_) {
        cameraCBResource_->Unmap(0, nullptr);
        cameraCBData_ = nullptr;
    }
    if (emitCBData_) {
        emitCBResource_->Unmap(0, nullptr);
        emitCBData_ = nullptr;
    }

    initBuffer_.Reset();
    freeListInitBuffer_.Reset();
    freeListIndexInitBuffer_.Reset();
    particleBuffer_.Reset();
    freeListBuffer_.Reset();
    freeListIndexBuffer_.Reset();
    emitUploadBuffer_.Reset();
    updateCBResource_.Reset();
    cameraCBResource_.Reset();
    materialCBResource_.Reset();
    emitCBResource_.Reset();
    vertexResource_.Reset();
}
