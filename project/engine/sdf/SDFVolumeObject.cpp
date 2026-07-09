#include "SDFVolumeObject.h"

#include <fstream>
#include <vector>
#include <cstring>

#include "engine/base/DirectXCommon.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/RootSignatureBuilder.h"
#include "engine/graphics/GraphicsPipelineBuilder.h"
#include "engine/3d/obj/Obj3dCommon.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"

Microsoft::WRL::ComPtr<ID3D12RootSignature> SDFVolumeObject::sRootSignature_;
Microsoft::WRL::ComPtr<ID3D12PipelineState> SDFVolumeObject::sPipeline_;

// 共有パイプライン：シーンMRT（色＋マスク、深度あり）へレイマーチング結果を描く
bool SDFVolumeObject::BuildPipeline() {
    if ( sPipeline_ ) return true;

    auto dxCommon = DirectXCommon::GetInstance();
    auto device = dxCommon->GetDevice();

    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCBV(0, D3D12_SHADER_VISIBILITY_ALL);                  // [0] b0（VS/PS共用）
    rsBuilder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [1] t0（距離ボリューム）
    rsBuilder.AddDescriptorTableSRV(1, D3D12_SHADER_VISIBILITY_PIXEL); // [2] t1（カラーボリューム）
    rsBuilder.AddDefaultSampler(0);                                    //     s0
    rsBuilder.Build(device, sRootSignature_);
    if ( !sRootSignature_ ) return false;

    static D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto& compiler = dxCommon->GetShaderCompiler();
    auto vsBlob = compiler.CompileShader(L"resources/shaders/SDF/SDFVolume.VS.hlsl", L"vs_6_0");
    auto psBlob = compiler.CompileShader(L"resources/shaders/SDF/SDFVolume.PS.hlsl", L"ps_6_0");
    if ( !vsBlob || !psBlob ) return false;

    // シーンMRTパス（Object3D等と同じ2枚のRT＋深度）に合わせる。
    // カリング無し＝カメラがボックス内に入っても破綻しない（レイ開始を t=0 にクランプ済み）
    GraphicsPipelineBuilder builder;
    builder
        .SetRootSignature(sRootSignature_.Get())
        .SetShaders(vsBlob.Get(), psBlob.Get())
        .SetInputLayout(inputLayout, _countof(inputLayout))
        .SetBlendMode(BlendMode::kNormal)
        .SetCullMode(D3D12_CULL_MODE_NONE)
        .SetDepthStencil(true, true)
        .SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB });
    builder.Build(device, sPipeline_);
    return sPipeline_ != nullptr;
}

void SDFVolumeObject::FinalizeShared() {
    sPipeline_.Reset();
    sRootSignature_.Reset();
}

bool SDFVolumeObject::Load(const std::string& sdf3dPath, ID3D12GraphicsCommandList* commandList) {
    if ( !BuildPipeline() ) return false;

    // --- 1. ファイル読み込み（ヘッダ36バイト + float距離配列）---
    std::ifstream file(sdf3dPath, std::ios::binary);
    if ( !file ) return false;
    file.read(reinterpret_cast<char*>( &header_ ), sizeof(Header));
    const size_t count = static_cast<size_t>( header_.width ) * header_.height * header_.depth;
    if ( count == 0 || count > 512ull * 512ull * 512ull ) return false; // 壊れたヘッダを弾く
    std::vector<float> data(count);
    file.read(reinterpret_cast<char*>( data.data() ), count * sizeof(float));
    if ( !file ) return false;

    auto dx = DirectXCommon::GetInstance();
    auto device = dx->GetDevice();

    // --- 2. Texture3D（R32_FLOAT）を作成 ---
    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = header_.width;
    desc.Height = header_.height;
    desc.DepthOrArraySize = static_cast<UINT16>( header_.depth );
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture_));
    if ( FAILED(hr) ) return false;

    // --- 3. アップロード（フットプリントの RowPitch に合わせて行単位で詰める）---
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT numRows = 0;
    UINT64 rowSize = 0, total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSize, &total);

    uploadBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(total);
    uint8_t* mapped = nullptr;
    uploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &mapped ));
    const uint32_t w = header_.width, h = header_.height, d = header_.depth;
    const size_t srcRowBytes = static_cast<size_t>( w ) * sizeof(float);
    for ( uint32_t z = 0; z < d; ++z ) {
        for ( uint32_t y = 0; y < h; ++y ) {
            std::memcpy(
                mapped + footprint.Offset + ( static_cast<size_t>( z ) * numRows + y ) * footprint.Footprint.RowPitch,
                reinterpret_cast<const uint8_t*>( data.data() ) + ( static_cast<size_t>( z ) * h + y ) * srcRowBytes,
                srcRowBytes);
        }
    }
    uploadBuffer_->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst {};
    dst.pResource = texture_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src {};
    src.pResource = uploadBuffer_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // --- 4. SRV（Texture3D）---
    srvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforTexture3D(srvIndex_, texture_.Get(), DXGI_FORMAT_R32_FLOAT, 1);

    // --- 4.5. カラーボリューム（同名の .sdfcol があれば読む。無ければ baseColor の単色）---
    //   SDF3DPrintf.py がテクスチャ付きモデルから「最寄り表面の色」を焼いた RGBA8 ボリューム。
    //   ヒット点でこれをサンプルすると、元テクスチャと同じ模様の色が付く。
    {
        std::string colPath = sdf3dPath;
        size_t dot = colPath.find_last_of('.');
        if ( dot != std::string::npos ) { colPath = colPath.substr(0, dot); }
        colPath += ".sdfcol";

        std::ifstream colFile(colPath, std::ios::binary);
        if ( colFile ) {
            Header ch {};
            colFile.read(reinterpret_cast<char*>( &ch ), sizeof(Header));
            const size_t colCount = static_cast<size_t>( ch.width ) * ch.height * ch.depth;
            if ( ch.width == header_.width && ch.height == header_.height &&
                 ch.depth == header_.depth && colCount > 0 ) {
                std::vector<uint8_t> colData(colCount * 4);
                colFile.read(reinterpret_cast<char*>( colData.data() ), colData.size());
                if ( colFile ) {
                    D3D12_RESOURCE_DESC cdesc = desc;
                    cdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // PNG由来のsRGB色
                    if ( SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &cdesc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&colorTexture_))) ) {

                        D3D12_PLACED_SUBRESOURCE_FOOTPRINT cfp {};
                        UINT cRows = 0;
                        UINT64 cRowSize = 0, cTotal = 0;
                        device->GetCopyableFootprints(&cdesc, 0, 1, 0, &cfp, &cRows, &cRowSize, &cTotal);

                        colorUploadBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(cTotal);
                        uint8_t* cm = nullptr;
                        colorUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &cm ));
                        const size_t cSrcRow = static_cast<size_t>( w ) * 4;
                        for ( uint32_t z = 0; z < d; ++z ) {
                            for ( uint32_t y = 0; y < h; ++y ) {
                                std::memcpy(
                                    cm + cfp.Offset + ( static_cast<size_t>( z ) * cRows + y ) * cfp.Footprint.RowPitch,
                                    colData.data() + ( static_cast<size_t>( z ) * h + y ) * cSrcRow,
                                    cSrcRow);
                            }
                        }
                        colorUploadBuffer_->Unmap(0, nullptr);

                        D3D12_TEXTURE_COPY_LOCATION cdst {};
                        cdst.pResource = colorTexture_.Get();
                        cdst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        cdst.SubresourceIndex = 0;
                        D3D12_TEXTURE_COPY_LOCATION csrc {};
                        csrc.pResource = colorUploadBuffer_.Get();
                        csrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                        csrc.PlacedFootprint = cfp;
                        commandList->CopyTextureRegion(&cdst, 0, 0, 0, &csrc, nullptr);

                        D3D12_RESOURCE_BARRIER cbarrier {};
                        cbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        cbarrier.Transition.pResource = colorTexture_.Get();
                        cbarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                        cbarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                        cbarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        commandList->ResourceBarrier(1, &cbarrier);

                        colorSrvIndex_ = SrvManager::GetInstance()->Allocate();
                        SrvManager::GetInstance()->CreateSRVforTexture3D(
                            colorSrvIndex_, colorTexture_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1);
                        hasColorVolume_ = true;
                    }
                }
            }
        }
    }

    // --- 5. プロキシ箱（単位キューブ 0〜1。VSで boxMin〜boxMax へ展開する）---
    const float cubeVerts[8][3] = {
        { 0,0,0 }, { 1,0,0 }, { 0,1,0 }, { 1,1,0 },
        { 0,0,1 }, { 1,0,1 }, { 0,1,1 }, { 1,1,1 },
    };
    const uint32_t cubeIndices[36] = {
        0,2,1, 1,2,3,   // -Z面
        4,5,6, 5,7,6,   // +Z面
        0,4,2, 2,4,6,   // -X面
        1,3,5, 3,7,5,   // +X面
        0,1,4, 1,5,4,   // -Y面
        2,6,3, 3,6,7,   // +Y面
    };

    auto factory = ResourceFactory::GetInstance();
    vertexBuffer_ = factory->CreateBufferResource(sizeof(cubeVerts));
    void* vbMapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &vbMapped);
    std::memcpy(vbMapped, cubeVerts, sizeof(cubeVerts));
    vertexBuffer_->Unmap(0, nullptr);
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = sizeof(cubeVerts);
    vbv_.StrideInBytes = sizeof(float) * 3;

    indexBuffer_ = factory->CreateBufferResource(sizeof(cubeIndices));
    void* ibMapped = nullptr;
    indexBuffer_->Map(0, nullptr, &ibMapped);
    std::memcpy(ibMapped, cubeIndices, sizeof(cubeIndices));
    indexBuffer_->Unmap(0, nullptr);
    ibv_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibv_.SizeInBytes = sizeof(cubeIndices);
    ibv_.Format = DXGI_FORMAT_R32_UINT;

    // --- 6. 定数バッファ ---
    constantBuffer_ = factory->CreateBufferResource(sizeof(VolumeCB));
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &cbData_ ));

    loaded_ = true;
    return true;
}

// 毎フレーム：カメラと配置から CB を作り直す
void SDFVolumeObject::Update() {
    if ( !loaded_ || !cbData_ ) return;
    const Camera* camera = Obj3dCommon::GetInstance()->GetDefaultCamera();
    if ( !camera ) return;

    Matrix4x4 vp = camera->GetViewProjectionMatrix();
    std::memcpy(cbData_->viewProj, &vp, sizeof(float) * 16);

    // ワールドAABB = ローカルbox × 一様スケール + 平行移動
    cbData_->boxMin[0] = header_.boxMin[0] * scale_ + translation_.x;
    cbData_->boxMin[1] = header_.boxMin[1] * scale_ + translation_.y;
    cbData_->boxMin[2] = header_.boxMin[2] * scale_ + translation_.z;
    cbData_->boxMax[0] = header_.boxMax[0] * scale_ + translation_.x;
    cbData_->boxMax[1] = header_.boxMax[1] * scale_ + translation_.y;
    cbData_->boxMax[2] = header_.boxMax[2] * scale_ + translation_.z;

    Vector3 camPos = camera->GetWorldPosition();
    cbData_->cameraPos[0] = camPos.x;
    cbData_->cameraPos[1] = camPos.y;
    cbData_->cameraPos[2] = camPos.z;
    cbData_->distScale = scale_; // 距離値もスケールに合わせてワールド単位へ

    cbData_->baseColor[0] = color_.x;
    cbData_->baseColor[1] = color_.y;
    cbData_->baseColor[2] = color_.z;
    cbData_->baseColor[3] = color_.w;
    cbData_->lightDir[0] = lightDir_.x;
    cbData_->lightDir[1] = lightDir_.y;
    cbData_->lightDir[2] = lightDir_.z;
    cbData_->erode = erode_;
    cbData_->useColorTex = hasColorVolume_ ? 1.0f : 0.0f;
}

// シーンMRTパスの最後で呼ぶ（専用のPSO/ルートシグネチャを自分でセットする）
void SDFVolumeObject::Draw(ID3D12GraphicsCommandList* commandList) {
    if ( !loaded_ || !sPipeline_ ) return;

    commandList->SetGraphicsRootSignature(sRootSignature_.Get());
    commandList->SetPipelineState(sPipeline_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetIndexBuffer(&ibv_);
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress()); // b0
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(1, srvIndex_);                    // t0
    // t1: カラーボリューム（無い時は距離ボリュームをダミーで挿す。useColorTex=0 なので読まれない）
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(2, hasColorVolume_ ? colorSrvIndex_ : srvIndex_);
    commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}
