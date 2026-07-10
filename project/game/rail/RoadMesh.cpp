#include "game/rail/RoadMesh.h"

#include "engine/rail/SplineRail.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/graphics/TextureManager.h"
#include "engine/graphics/PipelineManager.h"

#include <cmath>
#include <algorithm>

namespace {

// --- ベクトル補助（このファイル内だけで使う）---
inline Vector3 RM_Add(const Vector3& a, const Vector3& b){ return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 RM_Scale(const Vector3& a, float s){ return { a.x * s, a.y * s, a.z * s }; }
inline float   RM_Dot(const Vector3& a, const Vector3& b){ return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vector3 RM_Normalize(const Vector3& v){
    float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if ( l < 1e-6f ) return { 0.0f, 1.0f, 0.0f };
    return { v.x / l, v.y / l, v.z / l };
}

// --- 断面プロファイル（設計書 §4 の12頂点。lat=right方向, h=up方向）---
//   同じ位置でも法線/UVが違う点は別頂点（ハードエッジ）。
struct ProfileV { float lat, h, v, nr, nu; };
const ProfileV kProfile[12] = {
    { -1.00f, 0.00f, 0.5352f, -1.00f, 0.00f }, // 0 左壁 下
    { -1.00f, 0.15f, 0.6992f, -1.00f, 0.00f }, // 1 左壁 上
    { -1.00f, 0.15f, 0.7070f, -0.64f, 0.77f }, // 2 左ベベル 下
    { -0.88f, 0.25f, 0.7422f, -0.64f, 0.77f }, // 3 左ベベル 上
    { -0.88f, 0.25f, 0.7500f,  0.00f, 1.00f }, // 4 上面 左（坂の時は v=0.0234）
    { +0.88f, 0.25f, 1.0000f,  0.00f, 1.00f }, // 5 上面 右（坂の時は v=0.2031）
    { +0.88f, 0.25f, 0.7422f,  0.64f, 0.77f }, // 6 右ベベル 上
    { +1.00f, 0.15f, 0.7070f,  0.64f, 0.77f }, // 7 右ベベル 下
    { +1.00f, 0.15f, 0.6992f,  1.00f, 0.00f }, // 8 右壁 上
    { +1.00f, 0.00f, 0.5352f,  1.00f, 0.00f }, // 9 右壁 下
    { -1.00f, 0.00f, 0.4727f,  0.00f, -1.00f },// 10 底 左
    { +1.00f, 0.00f, 0.5273f,  0.00f, -1.00f },// 11 底 右
};
const int   kStrips[6][2] = { { 0,1 }, { 2,3 }, { 4,5 }, { 6,7 }, { 8,9 }, { 10,11 } };
const float kSlopeV[2] = { 0.0234f, 0.2031f }; // 坂帯の上面UV(v)。頂点4/5用

// --- 生成パラメータ（設計書 §4 の確定値）---
const float kWalkStep   = 0.05f;                            // サンプリング歩幅(m)
const float kMinRingGap = 0.06f;                            // リング最小間隔(m)
const float kMaxRingGap = 1.00f;                            // リング最大間隔(m)
const float kCurveAngle = 3.0f * 3.14159265f / 180.0f;      // リングを置く累積角(3°)
const float kSlopeIn    = 0.45f;                            // 坂UVに入る |tangent.y|（ヒステリシス上側）
const float kSlopeOut   = 0.35f;                            // 坂UVから抜ける（下側）
const float kTopOffset  = -0.30f;                           // 断面高さへの加算。上面(0.25)がレールの5cm下に来る
const float kUvPerMeter = 1.0f / 2.0f;                      // u = 距離 / 2m（タイルとテクスチャ周期を共有）

} // namespace

RoadMesh::RoadMesh() = default;
RoadMesh::~RoadMesh() = default;

// レール1本ぶんの掃引メッシュを生成する
void RoadMesh::BuildRailMesh(const SplineRail& rail, int railIdx, Camera* camera, uint32_t atlasSrv){
    const float len = rail.GetLength();

    // --- 1. リング位置の決定：曲率適応サンプリング＋坂UVのヒステリシス ---
    struct Ring { float s; bool slope; };
    std::vector<Ring> rings;

    SplineRail::RailFrame f0 = rail.GetFrameAtDistance(0.0f);
    bool slope = std::abs(f0.tangent.y) > kSlopeIn;
    rings.push_back({ 0.0f, slope });

    Vector3 prevTan = f0.tangent;
    float sinceLast = 0.0f, accumAngle = 0.0f;

    for ( float s = kWalkStep; s < len; s += kWalkStep ) {
        SplineRail::RailFrame f = rail.GetFrameAtDistance(s);
        // 方向変化を累積（曲がりのきつい所ほど早くしきい値に達する）
        float c = std::clamp(RM_Dot(prevTan, f.tangent), -1.0f, 1.0f);
        accumAngle += std::acos(c);
        prevTan = f.tangent;
        sinceLast += kWalkStep;

        // 坂UVの切替（入り0.45 / 抜け0.35 のヒステリシスでチラつかない）
        float ty = std::abs(f.tangent.y);
        bool newSlope = slope ? ( ty > kSlopeOut ) : ( ty > kSlopeIn );
        if ( newSlope != slope ) {
            // 境目は同位置の二重リング：ここでテクスチャのvだけが切り替わる
            rings.push_back({ s, slope });
            rings.push_back({ s, newSlope });
            slope = newSlope;
            sinceLast = 0.0f;
            accumAngle = 0.0f;
            continue;
        }

        if ( ( accumAngle > kCurveAngle && sinceLast >= kMinRingGap ) || sinceLast >= kMaxRingGap ) {
            rings.push_back({ s, slope });
            sinceLast = 0.0f;
            accumAngle = 0.0f;
        }
    }
    rings.push_back({ len, slope });

    // --- 2. 頂点生成（フレーム×断面プロファイル。内側折返しは前リングへ溶接）---
    Model::ModelData data;
    data.vertices.reserve(rings.size() * 12);

    Vector3 prevPos[12] {};
    bool hasPrev = false;
    for ( const Ring& ring : rings ) {
        SplineRail::RailFrame f = rail.GetFrameAtDistance(ring.s);
        Vector3 curPos[12];
        for ( int k = 0; k < 12; ++k ) {
            const ProfileV& pv = kProfile[k];
            Vector3 pos = RM_Add(f.position,
                RM_Add(RM_Scale(f.right, pv.lat), RM_Scale(f.up, pv.h + kTopOffset)));

            // 内側折返しの溶接：急カーブの内側で「前のリングより後ろ」に来たら前の位置に留める
            if ( hasPrev && RM_Dot({ pos.x - prevPos[k].x, pos.y - prevPos[k].y, pos.z - prevPos[k].z },
                                   f.tangent) < 0.0f ) {
                pos = prevPos[k];
            }
            curPos[k] = pos;

            float v = pv.v;
            if ( ring.slope && ( k == 4 || k == 5 ) ) { v = kSlopeV[k - 4]; } // 坂帯へ切替

            Model::VertexData vert {};
            vert.position = { pos.x, pos.y, pos.z, 1.0f };
            vert.normal = RM_Normalize(RM_Add(RM_Scale(f.right, pv.nr), RM_Scale(f.up, pv.nu)));
            vert.texcoord = { ring.s * kUvPerMeter, v };
            data.vertices.push_back(vert);
        }
        for ( int k = 0; k < 12; ++k ) { prevPos[k] = curPos[k]; }
        hasPrev = true;
    }

    // --- 3. インデックス：リング間に6帯×2三角形。二重リング間と穴区間はスキップ ---
    for ( size_t i = 0; i + 1 < rings.size(); ++i ) {
        if ( rings[i + 1].s - rings[i].s < 1e-4f ) continue; // 二重リング（UV切替点）
        if ( rail.IsHoleAtDistance(( rings[i].s + rings[i + 1].s ) * 0.5f) ) continue; // 穴＝張らない

        uint32_t base0 = static_cast<uint32_t>( i ) * 12;
        uint32_t base1 = static_cast<uint32_t>( i + 1 ) * 12;
        for ( const auto& strip : kStrips ) {
            uint32_t a0 = base0 + strip[0], a1 = base0 + strip[1];
            uint32_t b0 = base1 + strip[0], b1 = base1 + strip[1];
            data.indices.push_back(a0); data.indices.push_back(b0); data.indices.push_back(b1);
            data.indices.push_back(a0); data.indices.push_back(b1); data.indices.push_back(a1);
        }
    }

    if ( data.vertices.empty() || data.indices.empty() ) return;

    // --- 4. Model + Obj3d 化（頂点はワールド座標なので配置は animOffset の平行移動だけ）---
    // ※テクスチャパスは必ず設定すること（CreateBuffers が無条件にパスを読むため、
    //   空だと空パスのロードで起動時 assert になる）
    data.material.textureFilePath = "resources/road/road_atlas.png";
    auto model = std::make_unique<Model>();
    model->InitializePrimitive(ModelManager::GetInstance()->GetModelCommon(), data);
    model->SetTexture(atlasSrv);

    auto obj = std::make_unique<Obj3d>();
    obj->Initialize(model.get());
    obj->SetCamera(camera);
    // カリング無し：巻き順に依存せず、ジャンプ中に下から見上げても道が消えない
    obj->SetPipelineType(PipelineType::Object3D_CullNone);
    obj->Update();

    roadModels_.push_back(std::move(model));
    roadObjs_.push_back(std::move(obj));
    roadRail_.push_back(railIdx);
}

// 終端キャップ（road_end ピース）を p0 から p1 の向きに置く（原点=接続面の下端中央）
void RoadMesh::PlaceEndCap(Model* model, const std::vector<SplineRail>& rails, int railIdx,
                           const Vector3& p0, const Vector3& p1, Camera* camera){
    Vector3 d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if ( dl < 1e-4f ) return;
    Vector3 dir = { d.x / dl, d.y / dl, d.z / dl };
    float yaw   = std::atan2(dir.x, dir.z);
    float pitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));

    auto obj = std::make_unique<Obj3d>();
    obj->Initialize(model);
    obj->SetCamera(camera);
    // モデルの高さは0〜0.25。掃引メッシュと同じく上面(0.25)がレールの5cm下に来る高さに置く
    Vector3 pos = { p0.x, p0.y + kTopOffset, p0.z };
    obj->SetTranslation(pos);
    obj->SetRotation({ pitch, yaw, 0.0f });
    obj->Update();
    capObjs_.push_back(std::move(obj));
    capRail_.push_back(railIdx);
    const Vector3& off = rails[railIdx].animOffset;
    capBase_.push_back({ pos.x - off.x, pos.y - off.y, pos.z - off.z });
}

// レールに沿って道を敷き直す
void RoadMesh::Build(const std::vector<SplineRail>& rails, Camera* camera){
    roadModels_.clear();
    roadObjs_.clear();
    roadRail_.clear();
    capObjs_.clear();
    capRail_.clear();
    capBase_.clear();

    // アトラス（GamePlayScene::LoadResources で先読み済み＝ここではキャッシュが返るだけ）
    uint32_t atlasSrv = TextureManager::GetInstance()->Load("resources/road/road_atlas.png").srvIndex;
    Model* endModel = ModelManager::GetInstance()->FindModel("roadEnd");

    for ( int railIdx = 0; railIdx < ( int ) rails.size(); ++railIdx ) {
        const SplineRail& rail = rails[railIdx];
        float len = rail.GetLength();
        if ( len <= 0.0f || rail.nodes.size() < 2 ) continue;
        if ( !rail.visible ) continue; // 連結用の見えないレールには道を敷かない

        // 掃引メッシュ本体
        BuildRailMesh(rail, railIdx, camera, atlasSrv);

        // 終端キャップ：非ループの両端の外側に向けて置き、開いた断面を閉じる
        if ( !rail.isLoop && endModel ) {
            const float kProbe = ( std::min )( 1.0f, len * 0.5f );
            Vector3 e1  = rail.GetPositionByDistance(len);
            Vector3 e1b = rail.GetPositionByDistance(len - kProbe);
            PlaceEndCap(endModel, rails, railIdx, e1,
                { e1.x * 2.0f - e1b.x, e1.y * 2.0f - e1b.y, e1.z * 2.0f - e1b.z }, camera);
            Vector3 e0  = rail.GetPositionByDistance(0.0f);
            Vector3 e0b = rail.GetPositionByDistance(kProbe);
            PlaceEndCap(endModel, rails, railIdx, e0,
                { e0.x * 2.0f - e0b.x, e0.y * 2.0f - e0b.y, e0.z * 2.0f - e0b.z }, camera);
        }
    }
}

// 毎フレーム：動くレールの animOffset に追従し、カメラ行列を焼き直す
void RoadMesh::Update(const std::vector<SplineRail>& rails){
    for ( size_t k = 0; k < roadObjs_.size(); ++k ) {
        int ri = ( k < roadRail_.size() ) ? roadRail_[k] : -1;
        if ( ri >= 0 && ri < ( int ) rails.size() ) {
            // 掃引メッシュはワールド座標で焼いてあるので、動くレールぶんだけ平行移動
            roadObjs_[k]->SetTranslation(rails[ri].animOffset);
        }
        roadObjs_[k]->Update();
    }
    for ( size_t k = 0; k < capObjs_.size(); ++k ) {
        int ri = ( k < capRail_.size() ) ? capRail_[k] : -1;
        if ( ri >= 0 && ri < ( int ) rails.size() && k < capBase_.size() ) {
            const Vector3& off  = rails[ri].animOffset;
            const Vector3& base = capBase_[k];
            capObjs_[k]->SetTranslation({ base.x + off.x, base.y + off.y, base.z + off.z });
        }
        capObjs_[k]->Update();
    }
}

void RoadMesh::Draw() const{
    if ( !visible_ ) return;
    for ( const auto& r : roadObjs_ ) { r->Draw(); }
    for ( const auto& c : capObjs_ )  { c->Draw(); }
}
