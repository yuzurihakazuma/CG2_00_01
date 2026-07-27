#include "BlockSystem.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/graphics/InstancedGroup.h"
#include "engine/rail/SplineRail.h"
#include <algorithm>
#include <cmath>

namespace {
    const float kHalf = BlockSystem::kSize * 0.5f;   // ブロック半幅 (0.5m)
    const float kPlayerRadius = 0.30f;               // プレイヤーの横当たり半径
    const float kStepTolerance = 0.25f;              // これ以下の段差は支持面として拾う（小さなガタつき吸収）
    const uint32_t kMaxPerLook = 4000;               // 見た目グループごとの最大インスタンス数

    // (レール, 1mセル) を1つの整数キーにまとめる
    uint64_t CellKey(int rail, int cell){
        return ( static_cast< uint64_t >( static_cast< uint32_t >( rail ) ) << 32 )
             | static_cast< uint32_t >( cell );
    }
    // 距離 → 1mセル番号
    int CellOf(float dist){ return ( int ) std::lround(dist); }

    // プレイヤーは道の中心線上を進むので、横へずらしたブロック（壁の飾り等）は当たらない。
    //   side=0 のみ当たる（1mずらすと 1.0 > 0.8 で判定から外れる）
    bool OverlapsCenterLine(float side){ return std::abs(side) < kHalf + kPlayerRadius; }
}

BlockSystem::BlockSystem() = default;
BlockSystem::~BlockSystem() = default;

void BlockSystem::Initialize(uint32_t noiseSrvIndex){
    // 見た目2種それぞれに一括描画グループを用意（モデルは GamePlayScene が先に作る）
    topLook_.batch = std::make_unique<InstancedGroup>();
    topLook_.batch->Initialize("blockTop", kMaxPerLook);
    topLook_.batch->SetNoiseTexture(noiseSrvIndex);

    fillLook_.batch = std::make_unique<InstancedGroup>();
    fillLook_.batch->Initialize("blockFill", kMaxPerLook);
    fillLook_.batch->SetNoiseTexture(noiseSrvIndex);
}

void BlockSystem::Sync(const std::vector<BlockData>& blockDatas, const std::vector<SplineRail>* rails){
    rails_ = rails;
    blocks_.clear();
    if ( rails_ ) {
        for ( const auto& data : blockDatas ) {
            if ( data.rail < 0 || data.rail >= ( int ) rails_->size() ) continue;
            if ( ( *rails_ )[data.rail].nodes.size() < 2 ) continue;
            blocks_.push_back({ data.rail, data.dist, data.level, data.side });
        }
    }
    RebuildCellMap();
    BuildLookGroups();
    Update(); // 生成直後に位置を確定（原点に一瞬出るのを防ぐ）
}

// (レール, セル) の索引を作り直す。当たり判定・隣接判定はここから引く
void BlockSystem::RebuildCellMap(){
    cellMap_.clear();
    for ( int i = 0; i < ( int ) blocks_.size(); ++i ) {
        cellMap_[CellKey(blocks_[i].rail, CellOf(blocks_[i].dist))].push_back(i);
    }
}

bool BlockSystem::HasBlockAt(int rail, int cell, int level, float side) const{
    auto found = cellMap_.find(CellKey(rail, cell));
    if ( found == cellMap_.end() ) return false;
    for ( int idx : found->second ) {
        const Block& block = blocks_[idx];
        if ( block.level == level && std::abs(block.side - side) < 0.51f ) return true;
    }
    return false;
}

// 隣接に応じて見た目を振り分ける：上に何も無ければ「乗る面（明るい）」、
// 上が塞がっていれば「内部（暗い）」。積み上げた時に一番上だけ色が変わって地形らしく見える
void BlockSystem::BuildLookGroups(){
    topLook_.objs.clear();  topLook_.blockIndices.clear();
    fillLook_.objs.clear(); fillLook_.blockIndices.clear();

    for ( int i = 0; i < ( int ) blocks_.size(); ++i ) {
        const Block& block = blocks_[i];
        bool covered = HasBlockAt(block.rail, CellOf(block.dist), block.level + 1, block.side);
        LookGroup& look = covered ? fillLook_ : topLook_;
        auto obj = Obj3d::Create(covered ? "blockFill" : "blockTop");
        if ( obj ) {
            // block.obj は ±1 の 2m 立方体 → 0.5 倍で 1m 角にする
            obj->SetScale({ kHalf, kHalf, kHalf });
        }
        look.objs.push_back(std::move(obj));
        look.blockIndices.push_back(i);
    }
}

// ブロックの中心ワールド座標。動くレールの現在位置（animOffset込み）と
// 接線から求めた道幅方向（右）を使うので、カーブでも道と平行に並ぶ
bool BlockSystem::BlockWorldPos(const Block& block, Vector3& outPos, float& outYaw) const{
    if ( !rails_ ) return false;
    if ( block.rail < 0 || block.rail >= ( int ) rails_->size() ) return false;
    const SplineRail& rail = ( *rails_ )[block.rail];
    if ( rail.nodes.size() < 2 ) return false;

    float dist = std::clamp(block.dist, 0.0f, rail.GetLength());
    Vector3 base    = rail.GetPositionByDistance(dist);
    Vector3 tangent = rail.GetTangentByDistance(dist);

    // 道幅方向（水平の右）＝ up × tangent。接線がほぼ真上を向く場合はずらさない
    float horizLen = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
    Vector3 right { 0.0f, 0.0f, 0.0f };
    if ( horizLen > 1e-4f ) { right = { tangent.z / horizLen, 0.0f, -tangent.x / horizLen }; }

    outPos = { base.x + right.x * block.side,
               base.y + ( float ) block.level * kSize + kHalf,
               base.z + right.z * block.side };
    outYaw = ( horizLen > 1e-4f ) ? std::atan2(tangent.x, tangent.z) : 0.0f;
    return true;
}

void BlockSystem::Update(){
    if ( !rails_ ) return;
    auto updateLook = [&](LookGroup& look){
        for ( size_t i = 0; i < look.objs.size(); ++i ) {
            Obj3d* obj = look.objs[i].get();
            if ( !obj ) continue;
            int idx = look.blockIndices[i];
            Vector3 pos {}; float yaw = 0.0f;
            if ( !BlockWorldPos(blocks_[idx], pos, yaw) ) continue;
            obj->SetTranslation(pos);
            obj->SetRotation({ 0.0f, yaw, 0.0f });
            obj->Update();
        }
        // 計算済みの行列をまとめてGPU送信用配列へ写す
        if ( look.batch ) { look.batch->Update(look.objs); }
    };
    updateLook(topLook_);
    updateLook(fillLook_);
}

void BlockSystem::Draw(const Camera* camera){
    // 見た目グループごとに1ドローコール（何百個置いても計2回）
    if ( topLook_.batch )  { topLook_.batch->Draw(camera); }
    if ( fillLook_.batch ) { fillLook_.batch->Draw(camera); }
}

// 足元の支持面（レール空間）。footY より少し上までの上面を「乗れる面」として拾う
float BlockSystem::GroundHeightAt(int rail, float dist, float footY) const{
    float best = 0.0f; // レール面
    int center = CellOf(dist);
    for ( int cell = center - 1; cell <= center + 1; ++cell ) { // 判定半径0.8m＝隣接セルまで
        auto found = cellMap_.find(CellKey(rail, cell));
        if ( found == cellMap_.end() ) continue;
        for ( int idx : found->second ) {
            const Block& block = blocks_[idx];
            if ( !OverlapsCenterLine(block.side) ) continue;
            if ( std::abs(block.dist - dist) > kHalf + kPlayerRadius ) continue;
            float top = ( float ) block.level * kSize + kSize;
            if ( top <= footY + kStepTolerance && top > best ) { best = top; }
        }
    }
    return best;
}

// 体の高さ帯がブロックへ横から重なるか（支持面として乗っている場合は重ならない）
bool BlockSystem::BlockedAt(int rail, float dist, float bodyBottom, float bodyTop,
                            float* outMin, float* outMax) const{
    int center = CellOf(dist);
    for ( int cell = center - 1; cell <= center + 1; ++cell ) {
        auto found = cellMap_.find(CellKey(rail, cell));
        if ( found == cellMap_.end() ) continue;
        for ( int idx : found->second ) {
            const Block& block = blocks_[idx];
            if ( !OverlapsCenterLine(block.side) ) continue;
            float reach = kHalf + kPlayerRadius;
            if ( std::abs(block.dist - dist) > reach ) continue;
            float bottom = ( float ) block.level * kSize;
            float top    = bottom + kSize;
            // 面ぴったり（乗っている/頭が触れているだけ）は重なり扱いしない
            if ( bodyBottom < top - 0.05f && bodyTop > bottom + 0.05f ) {
                if ( outMin ) { *outMin = block.dist - reach; }
                if ( outMax ) { *outMax = block.dist + reach; }
                return true;
            }
        }
    }
    return false;
}

// 頭上の天井（footY より上にあるブロックの底面のうち一番低いもの）
float BlockSystem::CeilingHeightAt(int rail, float dist, float footY) const{
    float best = 1e9f;
    int center = CellOf(dist);
    for ( int cell = center - 1; cell <= center + 1; ++cell ) {
        auto found = cellMap_.find(CellKey(rail, cell));
        if ( found == cellMap_.end() ) continue;
        for ( int idx : found->second ) {
            const Block& block = blocks_[idx];
            if ( !OverlapsCenterLine(block.side) ) continue;
            if ( std::abs(block.dist - dist) > kHalf + kPlayerRadius ) continue;
            float bottom = ( float ) block.level * kSize;
            if ( bottom > footY + 0.1f && bottom < best ) { best = bottom; }
        }
    }
    return best;
}
