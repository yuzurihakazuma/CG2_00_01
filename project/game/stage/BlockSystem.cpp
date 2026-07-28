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
    // 支持面（乗れる範囲）はブロックの実寸＋わずかな縁の許容だけ。
    //   壁当たり半径(0.8m)をそのまま使うと1マス空きの落とし穴を歩いて渡れてしまう
    const float kSupportReach = kHalf + 0.10f;
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

namespace {
    // 種類 → モデル名（GamePlayScene::LoadResources が先に読み込む）
    const char* kTypeModelNames[BlockSystem::kTypeCount] = {
        "craftSponge",   // 0: 1m角スポンジ
        "craftLayer",    // 1: 1m角の段ボール層
        "craftSlope45",  // 2: 斜面45°（巻き段ボール筒）
        "craftSlope26",  // 3: ゆるい斜面26.6°
        "craftSpring",   // 4: ジャンプ台（緑のスポンジ。上に飛び乗ると跳ねる）
        "craftHatena",   // 5: ？ブロック（金色。下から頭突きでコイン）
        "craftCloud",    // 6: すり抜け床（白。上にだけ乗れる薄い板）
    };
    const char* kFlowerModelNames[2] = { "flowerOrange", "flowerWhite" };

    // セル座標から決まる擬似乱数（花の有無・向きに使う。毎回同じ結果＝チラつかない）
    uint32_t CellHash(int rail, int cell, int level, int side){
        uint32_t h = ( uint32_t ) ( rail * 73856093 ) ^ ( uint32_t ) ( cell * 19349663 )
                   ^ ( uint32_t ) ( level * 83492791 ) ^ ( uint32_t ) ( ( side + 8 ) * 2971215073u );
        h ^= h >> 13; h *= 0x85ebca6bu; h ^= h >> 16;
        return h;
    }
}

BlockSystem::BlockSystem() = default;
BlockSystem::~BlockSystem() = default;

void BlockSystem::Initialize(uint32_t noiseSrvIndex){
    // 種類ごと＋花2種の一括描画グループを用意（モデルは GamePlayScene が先に作る）
    for ( int t = 0; t < kTypeCount; ++t ) {
        typeLooks_[t].batch = std::make_unique<InstancedGroup>();
        typeLooks_[t].batch->Initialize(kTypeModelNames[t], kMaxPerLook);
        typeLooks_[t].batch->SetNoiseTexture(noiseSrvIndex);
    }
    usedHatenaLook_.batch = std::make_unique<InstancedGroup>();
    usedHatenaLook_.batch->Initialize("craftHatenaUsed", kMaxPerLook);
    usedHatenaLook_.batch->SetNoiseTexture(noiseSrvIndex);
    for ( int f = 0; f < 2; ++f ) {
        flowerLooks_[f].batch = std::make_unique<InstancedGroup>();
        flowerLooks_[f].batch->Initialize(kFlowerModelNames[f], kMaxPerLook);
        flowerLooks_[f].batch->SetNoiseTexture(noiseSrvIndex);
    }
}

void BlockSystem::Sync(const std::vector<BlockData>& blockDatas, const std::vector<SplineRail>* rails){
    rails_ = rails;
    blocks_.clear();
    if ( rails_ ) {
        for ( const auto& data : blockDatas ) {
            if ( data.rail < 0 || data.rail >= ( int ) rails_->size() ) continue;
            const SplineRail& rail = ( *rails_ )[data.rail];
            if ( rail.nodes.size() < 2 ) continue;
            // レールが短くなって範囲外に残ったブロックは末尾セルへ寄せる。
            //   見た目と当たり判定が同じ dist を使う（食い違うと「見えるのに当たらない」が起きる）
            float dist = data.dist;
            if ( dist > rail.GetLength() ) { dist = std::floor(rail.GetLength()); }
            if ( dist < 0.0f ) { dist = 0.0f; }
            blocks_.push_back({ data.rail, dist, data.level, data.side, data.type });
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
    return FindBlockIndexAt(rail, cell, level, side) >= 0;
}

int BlockSystem::FindBlockIndexAt(int rail, int cell, int level, float side) const{
    auto found = cellMap_.find(CellKey(rail, cell));
    if ( found == cellMap_.end() ) return -1;
    for ( int idx : found->second ) {
        const Block& block = blocks_[idx];
        if ( block.level == level && std::abs(block.side - side) < 0.51f ) return idx;
    }
    return -1;
}

// このブロックの dist 位置での表面高さ（レール面からの相対）。
//   矩形は上面一定、斜面はセル内で線形に上がる（登り方向は ascend が持つ）
float BlockSystem::SurfaceHeightAt(const Block& block, float dist) const{
    float bottom = ( float ) block.level * kSize;
    if ( IsSlopeType(block.type) ) {
        float run = FootprintHalf(block.type) * 2.0f; // 45°=1m / 26°=2m
        float t = std::clamp(( dist - ( block.dist - run * 0.5f ) ) / run, 0.0f, 1.0f);
        if ( block.ascend < 0 ) { t = 1.0f - t; }
        return bottom + t * kSize;
    }
    return bottom + kSize;
}

// 行列計算用プールを count 個まで育てる（縮めない）。
//   Obj3d は行列計算にしか使わない（描画は InstancedGroup）ので、モデルは何でもよく、
//   使い回す＝ペイントのたびにGPUリソースを作り直さない
void BlockSystem::EnsurePool(size_t count){
    while ( objPool_.size() < count ) {
        auto obj = Obj3d::Create("craftSponge");
        objPool_.push_back(std::move(obj));
    }
}

// 種類ごとの見た目グループへ振り分け、さらに「並んだブロックのつなぎ目」へ紙花を自動配置する。
//   本家（ヨッシークラフトワールド）の「継ぎ目を花で隠す」演出：2個以上並べると勝手に咲く
void BlockSystem::BuildLookGroups(){
    for ( int t = 0; t < kTypeCount; ++t ) { typeLooks_[t].objs.clear(); typeLooks_[t].blockIndices.clear(); }
    usedHatenaLook_.objs.clear(); usedHatenaLook_.blockIndices.clear();
    for ( int f = 0; f < 2; ++f )          { flowerLooks_[f].objs.clear(); flowerLooks_[f].blockIndices.clear(); }
    flowers_.clear();

    // --- 斜面の登り方向を隣のブロックから自動決定：高い側が隣のブロックへ向く ---
    //   （例：ブロックの手前に斜面を置くと、そのブロックへ登るスロープになる）
    for ( Block& block : blocks_ ) {
        if ( !IsSlopeType(block.type) ) continue;
        int cell = CellOf(block.dist);
        if      ( HasBlockAt(block.rail, cell + 1, block.level, block.side) ) { block.ascend = +1; }
        else if ( HasBlockAt(block.rail, cell - 1, block.level, block.side) ) { block.ascend = -1; }
        else { block.ascend = +1; } // 隣がいなければ進行方向へ登る向き
    }

    // --- 花の自動配置：右隣（dist+1）に同じ段・同じ横位置のブロックがあり、
    //     両方とも上が空いている継ぎ目に、セルハッシュで約半分だけ咲かせる ---
    for ( const Block& block : blocks_ ) {
        int cell = CellOf(block.dist);
        int sideKey = ( int ) std::lround(block.side);
        if ( IsSlopeType(block.type) ) continue; // 斜面の上面は斜めなので花は咲かせない
        int rightIdx = FindBlockIndexAt(block.rail, cell + 1, block.level, block.side);
        if ( rightIdx < 0 || IsSlopeType(blocks_[rightIdx].type) ) continue; // 右隣なし/斜面
        if ( HasBlockAt(block.rail, cell, block.level + 1, block.side) ) continue;     // 自分の上が塞がり
        if ( HasBlockAt(block.rail, cell + 1, block.level + 1, block.side) ) continue; // 隣の上が塞がり
        uint32_t hash = CellHash(block.rail, cell, block.level, sideKey);
        if ( ( hash & 3 ) == 0 ) continue; // 1/4は咲かせない（並びすぎ防止のゆらぎ）
        FlowerSpot flower;
        flower.rail = block.rail;
        flower.dist = block.dist + 0.5f; // 継ぎ目（セルの中間）
        flower.side = block.side;
        flower.topY = ( float ) block.level * kSize + kSize;
        flower.yawOffset = ( ( float ) ( hash % 628 ) ) * 0.01f; // 0〜2π
        flower.kind = ( hash >> 4 ) & 1;
        flowers_.push_back(flower);
    }

    EnsurePool(blocks_.size() + flowers_.size());

    // ブロック本体を種類ごとに振り分け（使用済みの？ブロックは灰色グループへ）
    for ( int i = 0; i < ( int ) blocks_.size(); ++i ) {
        int type = std::clamp(blocks_[i].type, 0, kTypeCount - 1);
        LookGroup& look = ( type == kTypeHatena && blocks_[i].used ) ? usedHatenaLook_ : typeLooks_[type];
        look.objs.push_back(objPool_[i].get());
        look.blockIndices.push_back(i);
    }
    // 花はプールの後半を使う（blockIndices は花配列の番号を負で持たず、-1-花番号で区別）
    for ( int f = 0; f < ( int ) flowers_.size(); ++f ) {
        LookGroup& look = flowerLooks_[flowers_[f].kind];
        look.objs.push_back(objPool_[blocks_.size() + f].get());
        look.blockIndices.push_back(-1 - f);
    }
}

// レール上 (rail, dist) の基準位置・道幅方向（右）・向き(yaw)を求める。
// 動くレールの現在位置（animOffset込み）を毎フレーム反映するので、リフト上のブロックも一緒に動く
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

    // モデルは原点が底面中心・実寸1m角なので、底面の高さ（レール面＋段数）に置く
    outPos = { base.x + right.x * block.side,
               base.y + ( float ) block.level * kSize,
               base.z + right.z * block.side };
    outYaw = ( horizLen > 1e-4f ) ? std::atan2(tangent.x, tangent.z) : 0.0f;
    return true;
}

void BlockSystem::Update(){
    if ( !rails_ ) return;
    auto updateLook = [&](LookGroup& look){
        for ( size_t i = 0; i < look.objs.size(); ++i ) {
            Obj3d* obj = look.objs[i];
            if ( !obj ) continue;
            int idx = look.blockIndices[i];
            if ( idx >= 0 ) {
                // ブロック本体
                const Block& block = blocks_[idx];
                Vector3 pos {}; float yaw = 0.0f;
                if ( !BlockWorldPos(block, pos, yaw) ) continue;
                if ( block.type == kTypeCloud ) {
                    // すり抜け床：セル上端に薄い板として置く（当たりの支持面＝セル上端と一致）
                    pos.y += 0.7f;
                    obj->SetScale({ 1.0f, 0.3f, 1.0f });
                } else {
                    obj->SetScale({ 1.0f, 1.0f, 1.0f }); // プール使い回しなので毎回戻す
                }
                if ( IsSlopeType(block.type) ) {
                    // 斜面モデルは「高い側の縁が原点」なので、走行方向へ半分ずらしてセル中央に合わせ、
                    // 登り方向が -側 なら180°回して高い側を隣のブロックへ向ける
                    const float kPi = 3.14159265f;
                    float run = FootprintHalf(block.type) * 2.0f;
                    float forward = ( float ) block.ascend * run * 0.5f;
                    Vector3 tangentDir = { std::sin(yaw), 0.0f, std::cos(yaw) };
                    pos.x += tangentDir.x * forward;
                    pos.z += tangentDir.z * forward;
                    if ( block.ascend < 0 ) { yaw += kPi; }
                }
                obj->SetTranslation(pos);
                obj->SetRotation({ 0.0f, yaw, 0.0f });
            } else {
                // 花（-1-花番号 で持っている）。継ぎ目のブロック上面に立てる
                const FlowerSpot& flower = flowers_[-1 - idx];
                Block asBlock { flower.rail, flower.dist, 0, flower.side, 0 };
                Vector3 pos {}; float yaw = 0.0f;
                if ( !BlockWorldPos(asBlock, pos, yaw) ) continue;
                pos.y += flower.topY;
                obj->SetTranslation(pos);
                obj->SetRotation({ 0.0f, yaw + flower.yawOffset, 0.0f });
            }
            obj->Update();
        }
        // 計算済みの行列をまとめてGPU送信用配列へ写す
        if ( look.batch ) { look.batch->Update(look.objs); }
    };
    for ( int t = 0; t < kTypeCount; ++t ) { updateLook(typeLooks_[t]); }
    updateLook(usedHatenaLook_);
    for ( int f = 0; f < 2; ++f )          { updateLook(flowerLooks_[f]); }
}

// 頭をぶつけた時に Player が呼ぶ。ぶつけた先が未使用の？ブロックならコインを出して使用済みにする
void BlockSystem::NotifyHeadBump(int rail, float dist, float headY){
    bool changed = false;
    int center = CellOf(dist);
    for ( int cell = center - 1; cell <= center + 1; ++cell ) {
        auto found = cellMap_.find(CellKey(rail, cell));
        if ( found == cellMap_.end() ) continue;
        for ( int idx : found->second ) {
            Block& block = blocks_[idx];
            if ( block.type != kTypeHatena || block.used ) continue;
            if ( !OverlapsCenterLine(block.side) ) continue;
            if ( std::abs(block.dist - dist) > kHalf + kPlayerRadius ) continue;
            float bottom = ( float ) block.level * kSize;
            if ( std::abs(bottom - headY) > 0.15f ) continue; // 頭が当たった面のブロックだけ
            block.used = true;
            changed = true;
            // コインの飛び出し位置＝ブロックの上面中央
            Vector3 pos {}; float yaw = 0.0f;
            if ( BlockWorldPos(block, pos, yaw) ) {
                bumpCoinQueue_.push_back({ pos.x, pos.y + kSize + 0.3f, pos.z });
            }
        }
    }
    if ( changed ) { BuildLookGroups(); } // 使用済みの見た目（灰色）へ差し替え
}

bool BlockSystem::ConsumeBumpCoin(Vector3& outPos){
    if ( bumpCoinQueue_.empty() ) return false;
    outPos = bumpCoinQueue_.back();
    bumpCoinQueue_.pop_back();
    return true;
}

// Play開始時：？ブロックを全部未使用へ戻す
void BlockSystem::ResetPlay(){
    bool changed = false;
    for ( auto& block : blocks_ ) {
        if ( block.used ) { block.used = false; changed = true; }
    }
    bumpCoinQueue_.clear();
    if ( changed ) { BuildLookGroups(); }
}

void BlockSystem::Draw(const Camera* camera){
    // 見た目グループごとに1ドローコール（何百個置いても種類数ぶんだけ）
    for ( int t = 0; t < kTypeCount; ++t ) {
        if ( typeLooks_[t].batch ) { typeLooks_[t].batch->Draw(camera); }
    }
    if ( usedHatenaLook_.batch ) { usedHatenaLook_.batch->Draw(camera); }
    for ( int f = 0; f < 2; ++f ) {
        if ( flowerLooks_[f].batch ) { flowerLooks_[f].batch->Draw(camera); }
    }
}

// 足元の支持面（レール空間）。footY より少し上までの上面を「乗れる面」として拾う。
//   斜面は位置に応じた表面高さ（＝そのまま歩いて登り降りできる）
float BlockSystem::GroundHeightAt(int rail, float dist, float footY) const{
    float best = 0.0f; // レール面
    int center = CellOf(dist);
    for ( int cell = center - 2; cell <= center + 2; ++cell ) { // 斜面26°(半幅1m)も拾える範囲
        auto found = cellMap_.find(CellKey(rail, cell));
        if ( found == cellMap_.end() ) continue;
        for ( int idx : found->second ) {
            const Block& block = blocks_[idx];
            if ( !OverlapsCenterLine(block.side) ) continue;
            float reach = FootprintHalf(block.type) + 0.10f; // 乗れるのは実寸＋縁だけ
            if ( std::abs(block.dist - dist) > reach ) continue;
            float top = SurfaceHeightAt(block, dist);
            if ( top <= footY + kStepTolerance && top > best ) { best = top; }
        }
    }
    return best;
}

// 体の高さ帯がブロックへ横から重なるか（支持面として乗っている場合は重ならない）
bool BlockSystem::BlockedAt(int rail, float dist, float bodyBottom, float bodyTop,
                            float* outMin, float* outMax, float* outTop) const{
    int center = CellOf(dist);
    for ( int cell = center - 1; cell <= center + 1; ++cell ) {
        auto found = cellMap_.find(CellKey(rail, cell));
        if ( found == cellMap_.end() ) continue;
        for ( int idx : found->second ) {
            const Block& block = blocks_[idx];
            if ( !OverlapsCenterLine(block.side) ) continue;
            if ( IsSlopeType(block.type) ) continue;      // 斜面は壁にならない（歩いて登る）
            if ( block.type == kTypeCloud ) continue;     // すり抜け床は横から通り抜けられる
            float reach = kHalf + kPlayerRadius;
            if ( std::abs(block.dist - dist) > reach ) continue;
            float bottom = ( float ) block.level * kSize;
            float top    = bottom + kSize;
            // 面ぴったり（乗っている/頭が触れているだけ）は重なり扱いしない
            if ( bodyBottom < top - 0.05f && bodyTop > bottom + 0.05f ) {
                if ( outMin ) { *outMin = block.dist - reach; }
                if ( outMax ) { *outMax = block.dist + reach; }
                if ( outTop ) { *outTop = top; }
                return true;
            }
        }
    }
    return false;
}

// 今の足元を支えているブロックの種類（GroundHeightAt と同じ条件で「一番高い支持面」の持ち主を返す）
int BlockSystem::SupportTypeAt(int rail, float dist, float footY) const{
    float best = 0.0f;
    int bestType = -1; // レール面
    int center = CellOf(dist);
    for ( int cell = center - 2; cell <= center + 2; ++cell ) {
        auto found = cellMap_.find(CellKey(rail, cell));
        if ( found == cellMap_.end() ) continue;
        for ( int idx : found->second ) {
            const Block& block = blocks_[idx];
            if ( !OverlapsCenterLine(block.side) ) continue;
            float reach = FootprintHalf(block.type) + 0.10f;
            if ( std::abs(block.dist - dist) > reach ) continue;
            float top = SurfaceHeightAt(block, dist);
            if ( top <= footY + kStepTolerance && top > best ) { best = top; bestType = block.type; }
        }
    }
    return bestType;
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
            if ( IsSlopeType(block.type) ) continue;      // 斜面の下は薄いので頭ぶつけ無し
            if ( block.type == kTypeCloud ) continue;     // すり抜け床は下から通り抜けられる
            if ( std::abs(block.dist - dist) > kHalf + kPlayerRadius ) continue;
            float bottom = ( float ) block.level * kSize;
            if ( bottom > footY + 0.1f && bottom < best ) { best = bottom; }
        }
    }
    return best;
}
