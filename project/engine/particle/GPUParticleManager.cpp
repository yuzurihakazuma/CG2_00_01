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
    // 二重初期化を防ぐ（Framework と GamePlayScene の両方から呼ばれても安全）
    if (isInitialized_) { return; }

    assert(dxCommon);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateParticleBuffer();
    CreateConstantBuffers();
    CreateVertexBuffer();

    // テクスチャ読み込み
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    TextureData texData = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
        textureFilePath, cmd);
    textureSrvIndex_ = texData.srvIndex;

    isInitialized_ = true;
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

    // ⑤ Emit用のUploadバッファを永続確保
    const size_t emitSize = sizeof(GPUParticleData) * kMaxEmitPerFrame;
    emitUploadBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(emitSize);
    emitUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&emitUploadData_));
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

    uint32_t slot = nextFreeSlot_;
    nextFreeSlot_ = (nextFreeSlot_ + 1) % kMaxParticles; // 循環

    emitQueue_.push_back({ slot, p });
}

// -------------------------------------------------------
//  Emitキューをアップロード
// -------------------------------------------------------
void GPUParticleManager::UploadEmitQueue(ID3D12GraphicsCommandList* commandList){
   
    lastFrameEmitCount_ = static_cast< uint32_t >( emitQueue_.size() ); 
    totalEmitted_ += lastFrameEmitCount_;
    
    if (emitQueue_.empty()) return;

    // UAV → COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    commandList->ResourceBarrier(1, &barrier);

    // UploadバッファにデータをコピーしてからGPUバッファへ転送
    for (uint32_t i = 0; i < static_cast<uint32_t>(emitQueue_.size()); i++)
    {
        const auto& entry = emitQueue_[i];
        emitUploadData_[i] = entry.data; // Uploadバッファに書き込む

        // 対象スロットに1粒だけコピー
        commandList->CopyBufferRegion(
            particleBuffer_.Get(),
            entry.slot * sizeof(GPUParticleData), // コピー先オフセット
            emitUploadBuffer_.Get(),
            i * sizeof(GPUParticleData),           // コピー元オフセット
            sizeof(GPUParticleData)
        );
    }

    emitQueue_.clear();

    // COPY_DEST → UNORDERED_ACCESS に戻す
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &barrier);
}

// -------------------------------------------------------
//  Dispatch (Computeシェーダー実行)
// -------------------------------------------------------
void GPUParticleManager::Dispatch(ID3D12GraphicsCommandList* commandList){
    // 新しいパーティクルをGPUに転送
    UploadEmitQueue(commandList);

    // UAVバリア (前フレームのCompute書き込みが完了するまで待つ)
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = particleBuffer_.Get();
    commandList->ResourceBarrier(1, &uavBarrier);

    // DescriptorHeapをセット
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Computeパイプラインをセット
    PipelineManager::GetInstance()->SetGPUParticleComputePipeline(commandList);

    // [0]: UAVバッファ
    srvManager_->SetComputeRootDescriptorTable(0, uavIndex_);
    // [1]: 更新用CB
    commandList->SetComputeRootConstantBufferView(
        1, updateCBResource_->GetGPUVirtualAddress());

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

    initBuffer_.Reset();
    particleBuffer_.Reset();
    emitUploadBuffer_.Reset();
    updateCBResource_.Reset();
    cameraCBResource_.Reset();
    materialCBResource_.Reset();
    vertexResource_.Reset();

    // フラグをリセットして次回 Initialize() が正しく再実行されるようにする
    isInitialized_ = false;
}
