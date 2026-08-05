#include "DissolveRoad.h"
#include "engine/rail/SplineRail.h"
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/RootSignatureBuilder.h"
#include "engine/graphics/GraphicsPipelineBuilder.h"
#include "engine/3d/obj/Obj3dCommon.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"
#include "externals/imgui/imgui.h"
#include "externals/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace {
    // --- 敷設 ---
    const float kSpacing = 0.7f;    // チェーン点の間隔(m)
    // チェーン点はレール線そのものに置く。断面の中心はシェーダー側で halfThick ぶん下げるので、
    // 道の上面は厚みスライダーをどう変えても常に「レール線ぴったり」になる
    // （以前は固定オフセット-0.37mで、厚み0.18の現在は上面がレール線の0.19m下＝
    //   ブロックや敵が浮いて見える/カメラ角度によっては埋まって見えるずれの原因だった）
    const float kRoundR  = 0.08f;   // 断面の角丸半径(m)
    // --- 見た目 ---
    const Vector4 kRoadColor { 0.72f, 0.55f, 0.34f, 1.0f };  // クラフト紙（段ボール）色
    const Vector3 kLightDir { 0.4f, -1.0f, 0.3f };
    // 消え方パラメータの保存先（エディタで調整→保存、起動時に自動読込）
    const char* kConfigPath = "resources/dissolve_road.json";

    // プレイヤーから点までの「体感距離」。水平基本＋高さの離れは強めに罰する
    float EffectiveDistance(const Vector3& playerPos, const Vector3& pointPos){
        float dx = pointPos.x - playerPos.x;
        float dy = pointPos.y - playerPos.y;
        float dz = pointPos.z - playerPos.z;
        float horizontal = std::sqrt(dx * dx + dz * dz);
        float vertical   = ( std::max )( 0.0f, std::abs(dy) - 2.0f ) * 2.0f;
        return horizontal + vertical;
    }
}

// 専用パイプライン：シーンMRT（色＋マスク、深度あり）へ融合レイマーチ結果を描く
bool DissolveRoad::BuildPipeline(){
    auto dxCommon = DirectXCommon::GetInstance();
    auto device = dxCommon->GetDevice();

    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCBV(0, D3D12_SHADER_VISIBILITY_ALL); // b0（VS/PS共用。テクスチャは使わない）
    rsBuilder.Build(device, rootSignature_);
    if ( !rootSignature_ ) return false;

    static D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto& compiler = dxCommon->GetShaderCompiler();
    auto vsBlob = compiler.CompileShader(L"resources/shaders/SDF/MeltRoad.VS.hlsl", L"vs_6_0");
    auto psBlob = compiler.CompileShader(L"resources/shaders/SDF/MeltRoad.PS.hlsl", L"ps_6_0");
    if ( !vsBlob || !psBlob ) return false;

    GraphicsPipelineBuilder builder;
    builder
        .SetRootSignature(rootSignature_.Get())
        .SetShaders(vsBlob.Get(), psBlob.Get())
        .SetInputLayout(inputLayout, _countof(inputLayout))
        .SetBlendMode(BlendMode::kNormal)
        .SetCullMode(D3D12_CULL_MODE_NONE)
        .SetDepthStencil(true, true)
        .SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB });
    builder.Build(device, pipeline_);
    return pipeline_ != nullptr;
}

// 完全消滅のエロージョン量：溶けムラ最小(0.45倍)でも断面の最深部を削り切る値を自動算出
float DissolveRoad::ErodeGone() const{
    float maxDepth = ( std::min )( halfWidth_, halfThick_ );
    return ( maxDepth + 0.02f ) / 0.45f;
}

void DissolveRoad::Initialize(ID3D12GraphicsCommandList*){
    ready_ = false;
    LoadConfig(); // 前回エディタで保存した消え方設定を復元
    if ( !BuildPipeline() ) return;

    // プロキシ箱（単位キューブ 0〜1。VSでチャンクAABBへ展開する）
    const float cubeVerts[8][3] = {
        { 0,0,0 }, { 1,0,0 }, { 0,1,0 }, { 1,1,0 },
        { 0,0,1 }, { 1,0,1 }, { 0,1,1 }, { 1,1,1 },
    };
    const uint32_t cubeIndices[36] = {
        0,2,1, 1,2,3,   4,5,6, 5,7,6,
        0,4,2, 2,4,6,   1,3,5, 3,7,5,
        0,1,4, 1,5,4,   2,6,3, 3,6,7,
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

    ready_ = true;
}

// 道の種類=溶け道のレールからチェーン点を打ち直し、16点区切りのチャンクへ分ける
void DissolveRoad::Build(const std::vector<SplineRail>& rails){
    points_.clear();
    chunks_.clear();
    if ( !ready_ ) return;

    auto factory = ResourceFactory::GetInstance();

    // 1本の連続チェーン（穴・レール境界で切れる）を16点以下のチャンク列に変換する
    auto flushChain = [&](int chainFirst, int chainCount){
        if ( chainCount < 2 ) return;
        int start = 0;
        while ( start + 1 < chainCount ) {
            int count = ( std::min )( kMaxChainPoints, chainCount - start );
            Chunk chunk;
            chunk.first = chainFirst + start;
            chunk.count = count;
            // プロキシAABB：点の範囲＋（半幅＋融合幅＋余白）
            Vector3 mn = points_[chunk.first].pos, mx = mn;
            for ( int i = 1; i < count; ++i ) {
                const Vector3& p = points_[chunk.first + i].pos;
                mn.x = ( std::min )( mn.x, p.x ); mx.x = ( std::max )( mx.x, p.x );
                mn.y = ( std::min )( mn.y, p.y ); mx.y = ( std::max )( mx.y, p.y );
                mn.z = ( std::min )( mn.z, p.z ); mx.z = ( std::max )( mx.z, p.z );
            }
            float marginXZ = halfWidth_ + blendK_ + 0.15f;
            float marginY  = halfThick_ * 2.0f + blendK_ + 0.15f; // 断面はレール線から下へ2×halfThick伸びる
            chunk.boxMin = { mn.x - marginXZ, mn.y - marginY, mn.z - marginXZ };
            chunk.boxMax = { mx.x + marginXZ, mx.y + marginY, mx.z + marginXZ };
            chunk.cb = factory->CreateBufferResource(sizeof(ChunkCB));
            chunk.cb->Map(0, nullptr, reinterpret_cast<void**>( &chunk.mapped ));
            chunks_.push_back(std::move(chunk));
            // 次のチャンクは最後の点を共有して続きから（道が途切れないように）
            start += count - 1;
        }
    };

    for ( const auto& rail : rails ) {
        if ( rail.roadMode != 2 ) continue;      // 溶け道のレールだけ
        if ( !rail.visible ) continue;
        if ( rail.nodes.size() < 2 ) continue;
        float length = rail.GetLength();
        if ( length <= 0.0f ) continue;

        int chainFirst = ( int ) points_.size();
        int chainCount = 0;
        for ( float d = 0.0f; d <= length + 0.001f; d += kSpacing ) {
            float dist = ( std::min )( d, length );
            if ( rail.IsHoleAtDistance(dist) ) {
                // 穴：チェーンを切る（穴は溶け道でも穴のまま）
                flushChain(chainFirst, chainCount);
                chainFirst = ( int ) points_.size();
                chainCount = 0;
                continue;
            }
            ChainPoint point;
            point.pos = rail.GetPositionByDistance(dist); // レール線＝道の上面の高さ
            point.erode = ErodeGone(); // 最初は完全に溶けて消えている
            points_.push_back(point);
            ++chainCount;
        }
        flushChain(chainFirst, chainCount);
    }
}

void DissolveRoad::Update(const Vector3& playerPos, float dt){
    if ( !ready_ || points_.empty() ) return;

    // 1) 点ごと：距離→目標エロージョンへなめらかに追従
    float erodeGone = ErodeGone();
    float easeFactor = ( std::min )( easeSpeed_ * dt, 1.0f );
    float farRange = ( std::max )( farDist_ - nearDist_, 0.1f );
    for ( auto& point : points_ ) {
        float dist = EffectiveDistance(playerPos, point.pos);
        float t = std::clamp(( dist - nearDist_ ) / farRange, 0.0f, 1.0f);
        float target = erodeGone * t;
        point.erode += ( target - point.erode ) * easeFactor;
    }

    // 2) チャンクごと：1点でも生きていれば CB を書いて描画対象にする
    const Camera* camera = Obj3dCommon::GetInstance()->GetDefaultCamera();
    if ( !camera ) return;
    Matrix4x4 viewProj = camera->GetViewProjectionMatrix();
    Vector3 camPos = camera->GetWorldPosition();

    for ( auto& chunk : chunks_ ) {
        bool anyAlive = false;
        for ( int i = 0; i < chunk.count; ++i ) {
            if ( points_[chunk.first + i].erode < erodeGone * 0.98f ) { anyAlive = true; break; }
        }
        chunk.active = anyAlive;
        if ( !anyAlive || !chunk.mapped ) continue;

        ChunkCB* cb = chunk.mapped;
        std::memcpy(cb->viewProj, &viewProj, sizeof(float) * 16);
        cb->rayBoxMin[0] = chunk.boxMin.x; cb->rayBoxMin[1] = chunk.boxMin.y; cb->rayBoxMin[2] = chunk.boxMin.z;
        cb->rayBoxMax[0] = chunk.boxMax.x; cb->rayBoxMax[1] = chunk.boxMax.y; cb->rayBoxMax[2] = chunk.boxMax.z;
        cb->pad0 = cb->pad1 = 0.0f;
        cb->cameraPos[0] = camPos.x; cb->cameraPos[1] = camPos.y; cb->cameraPos[2] = camPos.z;
        cb->pointCount = ( float ) chunk.count;
        cb->baseColor[0] = kRoadColor.x; cb->baseColor[1] = kRoadColor.y;
        cb->baseColor[2] = kRoadColor.z; cb->baseColor[3] = kRoadColor.w;
        cb->lightDir[0] = kLightDir.x; cb->lightDir[1] = kLightDir.y; cb->lightDir[2] = kLightDir.z;
        cb->noiseFreq = noiseFreq_;
        cb->halfWidth = halfWidth_; cb->halfThick = halfThick_;
        cb->roundR = kRoundR; cb->blendK = blendK_;
        cb->erodeBand = erodeGone;
        cb->chainOffset = ( float ) chunk.first; // 点の通し番号 → 波板の位相が道に沿って連続する
        cb->spacing = kSpacing;
        cb->pad4 = 0.0f;
        for ( int i = 0; i < kMaxChainPoints; ++i ) {
            const ChainPoint& point = points_[chunk.first + ( std::min )( i, chunk.count - 1 )];
            cb->pts[i][0] = point.pos.x;
            cb->pts[i][1] = point.pos.y;
            cb->pts[i][2] = point.pos.z;
            cb->pts[i][3] = point.erode;
        }
    }
}

void DissolveRoad::Draw(ID3D12GraphicsCommandList* commandList){
    if ( !ready_ || chunks_.empty() ) return;
    bool anyActive = false;
    for ( const auto& chunk : chunks_ ) { if ( chunk.active ) { anyActive = true; break; } }
    if ( !anyActive ) return;

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipeline_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetIndexBuffer(&ibv_);
    for ( const auto& chunk : chunks_ ) {
        if ( !chunk.active ) continue;
        commandList->SetGraphicsRootConstantBufferView(0, chunk.cb->GetGPUVirtualAddress());
        commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }
}

int DissolveRoad::ActiveCount() const{
    int count = 0;
    for ( const auto& chunk : chunks_ ) { if ( chunk.active ) ++count; }
    return count;
}

// 幅/厚み/融合幅の変更をプロキシAABBへ反映（点の打ち直しは不要なので Build より軽い）
void DissolveRoad::RecomputeChunkBounds(){
    float marginXZ = halfWidth_ + blendK_ + 0.15f;
    float marginY  = halfThick_ + blendK_ + 0.15f;
    for ( auto& chunk : chunks_ ) {
        if ( chunk.count <= 0 ) continue;
        Vector3 mn = points_[chunk.first].pos, mx = mn;
        for ( int i = 1; i < chunk.count; ++i ) {
            const Vector3& p = points_[chunk.first + i].pos;
            mn.x = ( std::min )( mn.x, p.x ); mx.x = ( std::max )( mx.x, p.x );
            mn.y = ( std::min )( mn.y, p.y ); mx.y = ( std::max )( mx.y, p.y );
            mn.z = ( std::min )( mn.z, p.z ); mx.z = ( std::max )( mx.z, p.z );
        }
        chunk.boxMin = { mn.x - marginXZ, mn.y - marginY, mn.z - marginXZ };
        chunk.boxMax = { mx.x + marginXZ, mx.y + marginY, mx.z + marginXZ };
    }
}

// 消え方の調整UI。SDFパネル（"SDF (フォント/画像)"）の下部へ追記する形で呼ばれる
void DissolveRoad::DrawImGui(){
    if ( !ImGui::CollapsingHeader("SDF溶け道（消え方の調整）") ) return;

    ImGui::TextDisabled("チェーン点 %d / 描画チャンク %d", PieceCount(), ActiveCount());

    // --- 消え方（その場で即反映）---
    ImGui::SliderFloat("現れる距離(m)", &nearDist_, 0.5f, 15.0f, "%.1f");
    if ( ImGui::IsItemHovered() ) ImGui::SetTooltip("プレイヤーからこの距離より近い道は完全に実体化する");
    ImGui::SliderFloat("消える距離(m)", &farDist_, 1.0f, 20.0f, "%.1f");
    if ( ImGui::IsItemHovered() ) ImGui::SetTooltip("この距離より遠い道は完全に溶けて消える。近い〜遠いの間は溶けかけになる");
    if ( farDist_ < nearDist_ + 0.5f ) farDist_ = nearDist_ + 0.5f;
    ImGui::SliderFloat("変化の速さ", &easeSpeed_, 0.5f, 12.0f, "%.1f");
    if ( ImGui::IsItemHovered() ) ImGui::SetTooltip("大きいほど現れる/溶けるの切り替わりがキビキビする（小さい=ねっとり）");
    ImGui::SliderFloat("溶けムラの細かさ", &noiseFreq_, 2.0f, 20.0f, "%.1f");
    if ( ImGui::IsItemHovered() ) ImGui::SetTooltip("溶ける時の穴あき・焼け崩れの細かさ。大きいほど細かくボロボロ崩れる");

    // --- 形（プロキシAABBの再計算が必要な項目）---
    bool geometryChanged = false;
    geometryChanged |= ImGui::SliderFloat("融合幅(m)", &blendK_, 0.05f, 0.8f, "%.2f");
    if ( ImGui::IsItemHovered() ) ImGui::SetTooltip("道同士がロウのように溶け合う幅。大きいほどトロッと繋がる");
    geometryChanged |= ImGui::SliderFloat("道の半幅(m)", &halfWidth_, 0.3f, 2.0f, "%.2f");
    geometryChanged |= ImGui::SliderFloat("道の半厚(m)", &halfThick_, 0.08f, 0.5f, "%.2f");
    if ( geometryChanged ) RecomputeChunkBounds();

    if ( ImGui::Button("溶け道設定を保存") ) SaveConfig();
    ImGui::SameLine();
    ImGui::TextDisabled("→ %s（起動時に自動読込）", kConfigPath);
}

void DissolveRoad::SaveConfig() const{
    nlohmann::json j;
    j["nearDist"]  = nearDist_;
    j["farDist"]   = farDist_;
    j["easeSpeed"] = easeSpeed_;
    j["noiseFreq"] = noiseFreq_;
    j["blendK"]    = blendK_;
    j["halfWidth"] = halfWidth_;
    j["halfThick"] = halfThick_;
    std::ofstream file(kConfigPath);
    if ( file ) file << j.dump(2);
}

void DissolveRoad::LoadConfig(){
    std::ifstream file(kConfigPath);
    if ( !file ) return; // まだ保存していなければ既定値のまま
    try {
        nlohmann::json j;
        file >> j;
        nearDist_  = j.value("nearDist",  nearDist_);
        farDist_   = j.value("farDist",   farDist_);
        easeSpeed_ = j.value("easeSpeed", easeSpeed_);
        noiseFreq_ = j.value("noiseFreq", noiseFreq_);
        blendK_    = j.value("blendK",    blendK_);
        halfWidth_ = j.value("halfWidth", halfWidth_);
        halfThick_ = j.value("halfThick", halfThick_);
    } catch ( ... ) {
        // 壊れた設定ファイルは無視して既定値で続行
    }
}
