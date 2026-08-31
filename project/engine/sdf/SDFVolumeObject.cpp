#include "SDFVolumeObject.h"

#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>

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
std::vector<SDFVolumeObject*> SDFVolumeObject::sInstances_;

SDFVolumeObject::~SDFVolumeObject() {
    auto it = std::find(sInstances_.begin(), sInstances_.end(), this);
    if ( it != sInstances_.end() ) { sInstances_.erase(it); }
}

// 共有パイプライン：シーンMRT（色＋マスク、深度あり）へレイマーチング結果を描く
bool SDFVolumeObject::BuildPipeline() {
    if ( sPipeline_ ) return true;

    auto dxCommon = DirectXCommon::GetInstance();
    auto device = dxCommon->GetDevice();

    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCBV(0, D3D12_SHADER_VISIBILITY_ALL);                  // [0] b0（VS/PS共用）
    rsBuilder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [1] t0（距離A）
    rsBuilder.AddDescriptorTableSRV(1, D3D12_SHADER_VISIBILITY_PIXEL); // [2] t1（カラーA）
    rsBuilder.AddDescriptorTableSRV(2, D3D12_SHADER_VISIBILITY_PIXEL); // [3] t2（距離B=モーフ先）
    rsBuilder.AddDescriptorTableSRV(3, D3D12_SHADER_VISIBILITY_PIXEL); // [4] t3（カラーB）
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

// .sdf3d（＋あれば同名の .sdfcol）を1組読み込み、Texture3D にして out へ詰める
bool SDFVolumeObject::LoadVolumeSet(const std::string& sdf3dPath,
                                    ID3D12GraphicsCommandList* commandList, VolumeSet& out) {
    // --- 1. 距離ボリューム（ヘッダ36バイト + float配列）---
    std::ifstream file(sdf3dPath, std::ios::binary);
    if ( !file ) return false;
    file.read(reinterpret_cast<char*>( &out.header ), sizeof(Header));
    const size_t count = static_cast<size_t>( out.header.width ) * out.header.height * out.header.depth;
    if ( count == 0 || count > 512ull * 512ull * 512ull ) return false; // 壊れたヘッダを弾く
    std::vector<float> data(count);
    file.read(reinterpret_cast<char*>( data.data() ), count * sizeof(float));
    if ( !file ) return false;

    auto dx = DirectXCommon::GetInstance();
    auto device = dx->GetDevice();
    auto factory = ResourceFactory::GetInstance();

    const uint32_t w = out.header.width, h = out.header.height, d = out.header.depth;

    // Texture3D を1枚作ってアップロードする共通手順
    auto createAndUpload = [&](DXGI_FORMAT format, const uint8_t* src, size_t srcPixelBytes,
                               Microsoft::WRL::ComPtr<ID3D12Resource>& tex,
                               Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuf) -> bool {
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.Width = w;
        desc.Height = h;
        desc.DepthOrArraySize = static_cast<UINT16>( d );
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES heap {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if ( FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))) ) {
            return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {};
        UINT numRows = 0;
        UINT64 rowSize = 0, total = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &numRows, &rowSize, &total);

        uploadBuf = factory->CreateBufferResource(total);
        uint8_t* mapped = nullptr;
        uploadBuf->Map(0, nullptr, reinterpret_cast<void**>( &mapped ));
        const size_t srcRowBytes = static_cast<size_t>( w ) * srcPixelBytes;
        for ( uint32_t z = 0; z < d; ++z ) {
            for ( uint32_t y = 0; y < h; ++y ) {
                std::memcpy(
                    mapped + fp.Offset + ( static_cast<size_t>( z ) * numRows + y ) * fp.Footprint.RowPitch,
                    src + ( static_cast<size_t>( z ) * h + y ) * srcRowBytes,
                    srcRowBytes);
            }
        }
        uploadBuf->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dst {};
        dst.pResource = tex.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION srcLoc {};
        srcLoc.pResource = uploadBuf.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp;
        commandList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = tex.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
        return true;
    };

    // 距離（R32_FLOAT）
    if ( !createAndUpload(DXGI_FORMAT_R32_FLOAT,
        reinterpret_cast<const uint8_t*>( data.data() ), sizeof(float), out.tex, out.upload) ) {
        return false;
    }
    // リロード時は同じSRVスロットへ上書き（参照側のインデックスを無効化しない）
    if ( !out.srvAllocated ) {
        out.srv = SrvManager::GetInstance()->Allocate();
        out.srvAllocated = true;
    }
    SrvManager::GetInstance()->CreateSRVforTexture3D(out.srv, out.tex.Get(), DXGI_FORMAT_R32_FLOAT, 1);

    // --- 2. カラーボリューム（同名 .sdfcol。無ければ単色フォールバック）---
    const std::string colPath = ColPathOf(sdf3dPath);

    out.hasColor = false; // リロードで .sdfcol が消えた/壊れた場合は単色に戻す
    std::ifstream colFile(colPath, std::ios::binary);
    if ( colFile ) {
        Header ch {};
        colFile.read(reinterpret_cast<char*>( &ch ), sizeof(Header));
        if ( ch.width == out.header.width && ch.height == out.header.height && ch.depth == out.header.depth ) {
            std::vector<uint8_t> colData(count * 4);
            colFile.read(reinterpret_cast<char*>( colData.data() ), colData.size());
            if ( colFile && createAndUpload(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                colData.data(), 4, out.colTex, out.colUpload) ) {
                if ( !out.colSrvAllocated ) {
                    out.colSrv = SrvManager::GetInstance()->Allocate();
                    out.colSrvAllocated = true;
                }
                SrvManager::GetInstance()->CreateSRVforTexture3D(
                    out.colSrv, out.colTex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1);
                out.hasColor = true;
            }
        }
    }

    // ホットリロード用にロード元とタイムスタンプを記録
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        out.path = sdf3dPath;
        out.distTime = fs::last_write_time(sdf3dPath, ec);
        out.colTime = {};
        if ( fs::exists(colPath, ec) ) { out.colTime = fs::last_write_time(colPath, ec); }
    }

    out.loaded = true;
    return true;
}

std::string SDFVolumeObject::ColPathOf(const std::string& sdf3dPath) {
    std::string colPath = sdf3dPath;
    size_t dot = colPath.find_last_of('.');
    if ( dot != std::string::npos ) { colPath = colPath.substr(0, dot); }
    return colPath + ".sdfcol";
}

// 1組ぶんの更新チェック：.sdf3d / .sdfcol の last_write_time がロード時と違えば true
bool SDFVolumeObject::IsSetModified(const VolumeSet& v) {
    if ( !v.loaded || v.path.empty() ) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    auto distTime = fs::last_write_time(v.path, ec);
    if ( ec ) return false; // ファイルが消えている間は何もしない（生成途中の可能性）
    if ( distTime != v.distTime ) return true;
    const std::string colPath = ColPathOf(v.path);
    if ( fs::exists(colPath, ec) ) {
        auto colTime = fs::last_write_time(colPath, ec);
        if ( !ec && colTime != v.colTime ) return true; // .sdfcol の後付け追加も拾う
    }
    return false;
}

bool SDFVolumeObject::IsFileModified() const {
    return IsSetModified(volA_) || IsSetModified(volB_);
}

// 更新されたボリュームだけ読み直す。テクスチャは作り直すが SRV スロットは維持されるので、
// 参照側（ゲームコード/エディタ配置）は何もしなくてよい
bool SDFVolumeObject::Reload(ID3D12GraphicsCommandList* commandList) {
    bool reloaded = false;
    if ( IsSetModified(volA_) && LoadVolumeSet(volA_.path, commandList, volA_) ) { reloaded = true; }
    if ( IsSetModified(volB_) && LoadVolumeSet(volB_.path, commandList, volB_) ) { reloaded = true; }
    return reloaded;
}

void SDFVolumeObject::ClearMorphTarget() {
    volB_ = VolumeSet {};
}

bool SDFVolumeObject::Load(const std::string& sdf3dPath, ID3D12GraphicsCommandList* commandList) {
    if ( !BuildPipeline() ) return false;
    if ( !LoadVolumeSet(sdf3dPath, commandList, volA_) ) return false;

    // ホットリロードの巡回対象として登録（SDFManager がファイル更新を検知して Reload を呼ぶ）
    if ( std::find(sInstances_.begin(), sInstances_.end(), this) == sInstances_.end() ) {
        sInstances_.push_back(this);
    }

    // --- プロキシ箱（単位キューブ 0〜1。VSで rayBox へ展開する）---
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

    constantBuffer_ = factory->CreateBufferResource(sizeof(VolumeCB));
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &cbData_ ));

    return true;
}

// モーフ先（形B）の読み込み。SetMorphT(0〜1) で A→B へ連続変形できるようになる
bool SDFVolumeObject::LoadMorphTarget(const std::string& sdf3dPath, ID3D12GraphicsCommandList* commandList) {
    if ( !volA_.loaded ) return false;
    return LoadVolumeSet(sdf3dPath, commandList, volB_);
}

// 毎フレーム：カメラと配置から CB を作り直す
void SDFVolumeObject::Update() {
    if ( !volA_.loaded || !cbData_ ) return;
    const Camera* camera = Obj3dCommon::GetInstance()->GetDefaultCamera();
    if ( !camera ) return;

    Matrix4x4 vp = camera->GetViewProjectionMatrix();
    std::memcpy(cbData_->viewProj, &vp, sizeof(float) * 16);

    // ワールドAABB = ローカルbox × 一様スケール + 平行移動（A/Bそれぞれ）
    const float tr[3] = { translation_.x, translation_.y, translation_.z };
    for ( int i = 0; i < 3; ++i ) {
        float aMin = volA_.header.boxMin[i] * scale_ + tr[i];
        float aMax = volA_.header.boxMax[i] * scale_ + tr[i];
        cbData_->boxMinA[i] = aMin;
        cbData_->boxMaxA[i] = aMax;

        float bMin = aMin, bMax = aMax;
        if ( volB_.loaded ) {
            bMin = volB_.header.boxMin[i] * scale_ + tr[i];
            bMax = volB_.header.boxMax[i] * scale_ + tr[i];
        }
        cbData_->boxMinB[i] = bMin;
        cbData_->boxMaxB[i] = bMax;

        // レイ/プロキシ箱は A∪B（モーフ中どちらの形も収まる）
        cbData_->rayBoxMin[i] = ( std::min )( aMin, bMin );
        cbData_->rayBoxMax[i] = ( std::max )( aMax, bMax );
    }

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

    cbData_->useColorTexA = volA_.hasColor ? 1.0f : 0.0f;
    cbData_->useColorTexB = volB_.hasColor ? 1.0f : 0.0f;
    cbData_->useMorph = volB_.loaded ? 1.0f : 0.0f;
    cbData_->morphT = std::clamp(morphT_, 0.0f, 1.0f);

    cbData_->meltStyle     = meltStyle_;
    cbData_->meltNoiseFreq = meltNoiseFreq_;
    cbData_->meltBand      = meltBand_;
    cbData_->meltPad       = 0.0f;
}

// シーンMRTパスの最後で呼ぶ（専用のPSO/ルートシグネチャを自分でセットする）
void SDFVolumeObject::Draw(ID3D12GraphicsCommandList* commandList) {
    if ( !volA_.loaded || !sPipeline_ ) return;

    auto srv = SrvManager::GetInstance();
    commandList->SetGraphicsRootSignature(sRootSignature_.Get());
    commandList->SetPipelineState(sPipeline_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetIndexBuffer(&ibv_);
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress()); // b0
    // 使わないスロットには距離Aをダミーで挿す（フラグ0なので読まれない。記述子は有効である必要がある）
    srv->SetGraphicsRootDescriptorTable(1, volA_.srv);                                          // t0 距離A
    srv->SetGraphicsRootDescriptorTable(2, volA_.hasColor ? volA_.colSrv : volA_.srv);          // t1 色A
    srv->SetGraphicsRootDescriptorTable(3, volB_.loaded ? volB_.srv : volA_.srv);               // t2 距離B
    srv->SetGraphicsRootDescriptorTable(4, volB_.hasColor ? volB_.colSrv : volA_.srv);          // t3 色B
    commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}
