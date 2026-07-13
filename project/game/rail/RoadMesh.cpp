#include "game/rail/RoadMesh.h"

#include "engine/rail/SplineRail.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/graphics/TextureManager.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/utils/EditorManager.h"

#include <cmath>
#include <algorithm>
#include <array>

namespace {

// --- ベクトル補助（このファイル内だけで使う）---
inline Vector3 RM_Add(const Vector3& a, const Vector3& b){ return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 RM_Sub(const Vector3& a, const Vector3& b){ return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vector3 RM_Scale(const Vector3& a, float s){ return { a.x * s, a.y * s, a.z * s }; }
inline float   RM_Dot(const Vector3& a, const Vector3& b){ return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float   RM_Len(const Vector3& v){ return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline Vector3 RM_Normalize(const Vector3& v){
    float l = RM_Len(v);
    if ( l < 1e-6f ) return { 0.0f, 1.0f, 0.0f };
    return { v.x / l, v.y / l, v.z / l };
}
// 水平（XZ）へ射影して正規化。ほぼ真上/真下なら false
inline bool RM_Horizontal(const Vector3& v, Vector3& out){
    float l = std::sqrt(v.x * v.x + v.z * v.z);
    if ( l < 0.35f ) return false; // 急勾配すぎてジャンクションの水平方向が定まらない
    out = { v.x / l, 0.0f, v.z / l };
    return true;
}
// 水平方向 d の「左」（角度ソートのCCWと整合する側）
inline Vector3 RM_Left(const Vector3& d){ return { -d.z, 0.0f, d.x }; }
inline float   RM_Frac(float x){ return x - std::floor(x); }

// --- 断面プロファイル（設計書 §4 の12頂点。lat=right方向, h=up方向）---
//   同じ位置でも法線/UVが違う点は別頂点（ハードエッジ）。
struct ProfileV { float lat, h, v, nr, nu; };
const ProfileV kProfile[12] = {
    { -1.00f, 0.00f, 0.5352f, -1.00f, 0.00f }, // 0 左壁 下
    { -1.00f, 0.15f, 0.6992f, -1.00f, 0.00f }, // 1 左壁 上
    { -1.00f, 0.15f, 0.7070f, -0.64f, 0.77f }, // 2 左ベベル 下
    { -0.88f, 0.25f, 0.7422f, -0.64f, 0.77f }, // 3 左ベベル 上
    { -0.88f, 0.25f, 0.7500f,  0.00f, 1.00f }, // 4 上面 左（坂:0.0234 / 危険帯:0.2070）
    { +0.88f, 0.25f, 1.0000f,  0.00f, 1.00f }, // 5 上面 右（坂:0.2031 / 危険帯:0.2734）
    { +0.88f, 0.25f, 0.7422f,  0.64f, 0.77f }, // 6 右ベベル 上
    { +1.00f, 0.15f, 0.7070f,  0.64f, 0.77f }, // 7 右ベベル 下
    { +1.00f, 0.15f, 0.6992f,  1.00f, 0.00f }, // 8 右壁 上
    { +1.00f, 0.00f, 0.5352f,  1.00f, 0.00f }, // 9 右壁 下
    { -1.00f, 0.00f, 0.4727f,  0.00f, -1.00f },// 10 底 左
    { +1.00f, 0.00f, 0.5273f,  0.00f, -1.00f },// 11 底 右
};
const int   kStrips[6][2] = { { 0,1 }, { 2,3 }, { 4,5 }, { 6,7 }, { 8,9 }, { 10,11 } };
const float kSlopeV[2]  = { 0.0234f, 0.2031f }; // 坂帯の上面UV(v)。頂点4/5用
const float kDangerV[2] = { 0.2070f, 0.2734f }; // 危険帯（穴の手前後）の上面UV(v)。アトラスv5

// --- 生成パラメータ（設計書 §4 の確定値）---
const float kWalkStep   = 0.05f;                            // サンプリング歩幅(m)
const float kMinRingGap = 0.06f;                            // リング最小間隔(m)
const float kMaxRingGap = 1.00f;                            // リング最大間隔(m)
const float kCurveAngle = 3.0f * 3.14159265f / 180.0f;      // リングを置く累積角(3°)
const float kSlopeIn    = 0.45f;                            // 坂UVに入る |tangent.y|（ヒステリシス上側）
const float kSlopeOut   = 0.35f;                            // 坂UVから抜ける（下側）
const float kTopOffset  = -0.30f;                           // 断面高さへの加算。上面(0.25)がレールの5cm下に来る
const float kUvPerMeter = 1.0f / 2.0f;                      // u = 距離 / 2m（ピースとテクスチャ周期を共有）
const float kWarnLength = 1.0f;                             // 穴の手前後に危険帯を敷く長さ(m)
const float kSimpleStep = 1.0f;                             // 簡易ビルド（ドラッグ中）のリング間隔(m)

// --- ジャンクション（GUIDE_ジャンクション生成）---
const float kHalfWidth   = 1.00f;                 // 道の半幅 W
const float kInnerWidth  = 0.88f;                 // ベベル内側 W-B
const float kTopH        = 0.25f;                 // 上面の断面高さ
const float kBevelH      = 0.15f;                 // ベベル下端の断面高さ
const float kMiterMargin = 0.15f;                 // マイター交点から入口までの余白
const float kTCutMin     = 0.35f;                 // t_cut の最低値
const float kMiterMaxDeg = 175.0f;                // これ未満はマイター
const float kArcMinDeg   = 185.0f;                // これ超は外周円弧
const float kArcStepDeg  = 12.0f;                 // 円弧の刻み
const float kGentleDot   = -0.866f;               // 2本溶接: dot<=これ(150°以上) → 掃引コネクタ担当
const float kStraightDot = -0.996f;               // ほぼ一直線: 何もしない（突き合わせ）
const float kArm         = 1.0f;                  // 掃引コネクタが両側を削る長さ

// マイター交点：N + t*di + w*Li = N + s*dj - w*Lj を XZ で解く（t, s を返す）
bool SolveMiter(const Vector3& di, const Vector3& dj, float w, float& t, float& s){
    Vector3 Li = RM_Left(di), Lj = RM_Left(dj);
    float bx = -w * ( Li.x + Lj.x );
    float bz = -w * ( Li.z + Lj.z );
    float det = di.x * ( -dj.z ) - ( -dj.x ) * di.z;
    if ( std::abs(det) < 1e-4f ) return false;
    t = ( bx * ( -dj.z ) - ( -dj.x ) * bz ) / det;
    s = ( di.x * bz - di.z * bx ) / det;
    return true;
}

} // namespace

RoadMesh::RoadMesh() = default;
RoadMesh::~RoadMesh() = default;

// 生成済みメッシュを空きスロットへ書き込む（スロット不足時のみ新規確保）
void RoadMesh::EmitMesh(const Model::ModelData& data, int followRail, Camera* camera, uint32_t atlasSrv){
    if ( data.vertices.empty() || data.indices.empty() ) return;

    if ( slotsUsed_ == slots_.size() ) {
        // 新しいスロット：固定容量の動的バッファを一度だけ確保する（以後は memcpy のみ）
        auto slot = std::make_unique<MeshSlot>();
        slot->model = std::make_unique<Model>();
        slot->model->InitializeDynamic(ModelManager::GetInstance()->GetModelCommon(),
                                       4096, 24576, "resources/road/road_atlas.png");
        slot->model->SetTexture(atlasSrv);
        slot->obj = std::make_unique<Obj3d>();
        slot->obj->Initialize(slot->model.get());
        // カリング無し：巻き順に依存せず、ジャンプ中に下から見上げても道が消えない
        slot->obj->SetPipelineType(PipelineType::Object3D_CullNone);
        slots_.push_back(std::move(slot));
    }
    MeshSlot& s = *slots_[slotsUsed_++];
    s.model->UpdateMesh(data.vertices, data.indices);
    s.rail = followRail;
    s.obj->SetCamera(camera);
    s.obj->SetTranslation({ 0.0f, 0.0f, 0.0f });
    s.obj->Update();
}

// レール1本ぶんの掃引メッシュを生成する
void RoadMesh::BuildRailMesh(const SplineRail& rail, int railIdx, Camera* camera, uint32_t atlasSrv,
                             const std::vector<Cut>& cuts, bool simple){
    const float len = rail.GetLength();

    const std::vector<SplineRail::HoleInterval> holes =
        simple ? std::vector<SplineRail::HoleInterval>{} : rail.GetHoleIntervals();

    // 距離 s が危険帯（穴の手前後 kWarnLength）か
    auto dangerAt = [&](float s) -> bool{
        for ( const auto& h : holes ) {
            if ( ( s >= h.d0 - kWarnLength && s <= h.d0 + 1e-4f ) ||
                 ( s >= h.d1 - 1e-4f && s <= h.d1 + kWarnLength ) ) return true;
        }
        return false;
    };

    // --- 1. リング位置の決定 ---
    struct Ring { float s; bool slope; bool danger; };
    std::vector<Ring> rings;

    if ( simple ) {
        // 簡易ビルド：固定1m刻みの素のリボン（坂UV/穴/溶接なし）
        for ( float s = 0.0f; s < len; s += kSimpleStep ) { rings.push_back({ s, false, false }); }
        rings.push_back({ len, false, false });
    } else {
        // 曲率適応サンプリング＋坂UVのヒステリシス
        SplineRail::RailFrame f0 = rail.GetFrameAtDistance(0.0f);
        bool slope = std::abs(f0.tangent.y) > kSlopeIn;
        rings.push_back({ 0.0f, slope, dangerAt(0.0f) });

        Vector3 prevTan = f0.tangent;
        float sinceLast = 0.0f, accumAngle = 0.0f;

        for ( float s = kWalkStep; s < len; s += kWalkStep ) {
            SplineRail::RailFrame f = rail.GetFrameAtDistance(s);
            float c = std::clamp(RM_Dot(prevTan, f.tangent), -1.0f, 1.0f);
            accumAngle += std::acos(c);
            prevTan = f.tangent;
            sinceLast += kWalkStep;

            // 坂UVの切替（入り0.45 / 抜け0.35 のヒステリシスでチラつかない）
            float ty = std::abs(f.tangent.y);
            bool newSlope = slope ? ( ty > kSlopeOut ) : ( ty > kSlopeIn );
            if ( newSlope != slope ) {
                rings.push_back({ s, slope, dangerAt(s) });    // 境目は同位置の二重リング（vだけ切替）
                rings.push_back({ s, newSlope, dangerAt(s) });
                slope = newSlope;
                sinceLast = 0.0f;
                accumAngle = 0.0f;
                continue;
            }

            if ( ( accumAngle > kCurveAngle && sinceLast >= kMinRingGap ) || sinceLast >= kMaxRingGap ) {
                rings.push_back({ s, slope, dangerAt(s) });
                sinceLast = 0.0f;
                accumAngle = 0.0f;
            }
        }
        rings.push_back({ len, slope, dangerAt(len) });

        // --- 1b. 境界リングの強制挿入 ---
        //   穴の境界 d0/d1（キャップと隙間をピッタリ合わせる）、危険帯の境目（二重リングでvを切替）、
        //   ジャンクションの切り詰め位置（パッチの入口断面と一致させる）にリングを必ず置く
        struct Forced { float s; bool dual; };
        std::vector<Forced> forced;
        for ( const Cut& c : cuts ) {
            if ( c.s0 > 0.0f && c.s0 < len ) forced.push_back({ c.s0, false });
            if ( c.s1 > 0.0f && c.s1 < len ) forced.push_back({ c.s1, false });
        }
        for ( const auto& h : holes ) {
            float w0 = h.d0 - kWarnLength, w1 = h.d1 + kWarnLength;
            if ( w0 > 0.0f && w0 < len )     forced.push_back({ w0, true });  // 危険帯に入る（v切替）
            if ( h.d0 > 0.0f && h.d0 < len ) forced.push_back({ h.d0, false });
            if ( h.d1 > 0.0f && h.d1 < len ) forced.push_back({ h.d1, false });
            if ( w1 > 0.0f && w1 < len )     forced.push_back({ w1, true });  // 危険帯から出る
        }
        std::sort(forced.begin(), forced.end(), [](const Forced& a, const Forced& b){ return a.s < b.s; });

        for ( const Forced& fc : forced ) {
            // 挿入位置（s昇順を保つ）
            size_t at = 0;
            while ( at < rings.size() && rings[at].s < fc.s - 1e-4f ) { ++at; }
            bool slopeHere = ( at > 0 ) ? rings[at - 1].slope : rings.front().slope;
            if ( fc.dual ) {
                // 危険帯の境目：同位置の二重リング（手前側の状態 → 奥側の状態）
                rings.insert(rings.begin() + at, { fc.s, slopeHere, dangerAt(fc.s + 1e-3f) });
                rings.insert(rings.begin() + at, { fc.s, slopeHere, dangerAt(fc.s - 1e-3f) });
            } else {
                // 既に（ほぼ）同位置のリングがあれば挿入しない
                bool exists = ( at < rings.size() && std::abs(rings[at].s - fc.s) < 1e-3f ) ||
                              ( at > 0 && std::abs(rings[at - 1].s - fc.s) < 1e-3f );
                if ( !exists ) { rings.insert(rings.begin() + at, { fc.s, slopeHere, dangerAt(fc.s) }); }
            }
        }
    }

    // --- 2. 頂点生成（フレーム×断面プロファイル。内側折返しは前リングへ溶接）---
    Model::ModelData data;
    data.vertices.reserve(rings.size() * 12);

    Vector3 prevPos[12] {};
    bool hasPrev = false;
    for ( size_t ri = 0; ri < rings.size(); ++ri ) {
        const Ring& ring = rings[ri];
        SplineRail::RailFrame f = rail.GetFrameAtDistance(ring.s);
        for ( int k = 0; k < 12; ++k ) {
            const ProfileV& pv = kProfile[k];
            Vector3 pos = RM_Add(f.position,
                RM_Add(RM_Scale(f.right, pv.lat), RM_Scale(f.up, pv.h + kTopOffset)));

            // 内側折返しの溶接：急カーブの内側で「前のリングより後ろ」に来たら前の位置に留める
            if ( !simple && hasPrev &&
                 RM_Dot({ pos.x - prevPos[k].x, pos.y - prevPos[k].y, pos.z - prevPos[k].z },
                        f.tangent) < 0.0f ) {
                pos = prevPos[k];
            }
            prevPos[k] = pos;

            float v = pv.v;
            if ( k == 4 || k == 5 ) {
                if ( ring.danger )      { v = kDangerV[k - 4]; } // 危険帯（穴の手前後）が最優先
                else if ( ring.slope )  { v = kSlopeV[k - 4]; }  // 坂帯
            }

            Model::VertexData vert {};
            vert.position = { pos.x, pos.y, pos.z, 1.0f };
            vert.normal = RM_Normalize(RM_Add(RM_Scale(f.right, pv.nr), RM_Scale(f.up, pv.nu)));
            vert.texcoord = { ring.s * kUvPerMeter, v };
            data.vertices.push_back(vert);
        }
        hasPrev = true;
    }

    // --- 3. 区間の分類：描く / 穴 / ジャンクションの切り詰め / 二重リング ---
    enum class Seg { Draw, Hole, Cut, Degen };
    std::vector<Seg> segs(rings.size() - 1);
    for ( size_t i = 0; i + 1 < rings.size(); ++i ) {
        float mid = ( rings[i].s + rings[i + 1].s ) * 0.5f;
        if ( rings[i + 1].s - rings[i].s < 1e-4f ) { segs[i] = Seg::Degen; continue; }
        bool inCut = false;
        for ( const Cut& c : cuts ) {
            if ( mid >= c.s0 && mid <= c.s1 ) { inCut = true; break; }
        }
        bool inHole = false;
        for ( const auto& h : holes ) {
            if ( mid >= h.d0 && mid <= h.d1 ) { inHole = true; break; }
        }
        if ( inCut )       { segs[i] = Seg::Cut; }
        else if ( inHole ) { segs[i] = Seg::Hole; }
        else               { segs[i] = Seg::Draw; }
    }

    // --- 4. インデックス：Draw区間だけリング間に6帯×2三角形を張る ---
    for ( size_t i = 0; i + 1 < rings.size(); ++i ) {
        if ( segs[i] != Seg::Draw ) continue;
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
    EmitMesh(data, railIdx, camera, atlasSrv);
}

// ピース（road_end / road_joint）を1個置く。Obj3d はプールを使い回す
void RoadMesh::PlacePiece(Model* model, std::vector<std::unique_ptr<PieceSlot>>& pool, size_t& used,
                          int railIdx, const std::vector<SplineRail>& rails,
                          const Vector3& pos, float yaw, float pitch, Camera* camera){
    if ( !model ) return;
    if ( used == pool.size() ) {
        auto slot = std::make_unique<PieceSlot>();
        slot->obj = std::make_unique<Obj3d>();
        slot->obj->Initialize(model);
        pool.push_back(std::move(slot));
    }
    PieceSlot& s = *pool[used++];
    s.obj->SetModel(model);
    s.obj->SetCamera(camera);
    Vector3 p = { pos.x, pos.y + kTopOffset, pos.z }; // 上面(0.25)がレールの5cm下に来る高さ
    s.obj->SetScale({ 1.0f, 1.0f, 1.0f });
    s.obj->SetTranslation(p);
    s.obj->SetRotation({ pitch, yaw, 0.0f });
    s.obj->Update();
    s.rail = railIdx;
    const Vector3& off = ( railIdx >= 0 && railIdx < ( int ) rails.size() )
                       ? rails[railIdx].animOffset : Vector3 { 0.0f, 0.0f, 0.0f };
    s.base = { p.x - off.x, p.y - off.y, p.z - off.z };
}

// 終端キャップ（road_end ピース）を p0 から p1 の向きに置く（原点=接続面の下端中央）
void RoadMesh::PlaceEndCap(Model* model, const std::vector<SplineRail>& rails, int railIdx,
                           const Vector3& p0, const Vector3& p1, Camera* camera){
    Vector3 d = RM_Sub(p1, p0);
    float dl = RM_Len(d);
    if ( dl < 1e-4f ) return;
    Vector3 dir = RM_Scale(d, 1.0f / dl);
    float yaw   = std::atan2(dir.x, dir.z);
    float pitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    PlacePiece(model, pieces_, piecesUsed_, railIdx, rails, p0, yaw, pitch, camera);
}

// ジョイント（road_joint）を1個置く。railPos はレール高さの位置（道上面+5mm に載せる）
void RoadMesh::PlaceJointPiece(const std::vector<SplineRail>& rails, int railIdx,
                               const Vector3& railPos, float yaw, Camera* camera){
    Model* jointM = ModelManager::GetInstance()->FindModel("roadJoint");
    if ( !jointM ) return;
    // PlacePiece が kTopOffset を足すので、上面の高さ(0.25)+5mm を先に加算しておく
    Vector3 pos = { railPos.x, railPos.y + kTopH + 0.005f, railPos.z };
    PlacePiece(jointM, joints_, jointsUsed_, railIdx, rails, pos, yaw, 0.0f, camera);
}

// ジャンクションの各入口に凸を中心へ向けてジョイントを置く（2本溶接は中央に1個）
void RoadMesh::PlaceJoints(const std::vector<SplineRail>& rails, const Junction& junc, Camera* camera){
    if ( junc.arms.size() == 2 ) {
        // 2本の溶接：ノード中央に1個。向きは2方向の二等分線
        Vector3 bi = RM_Add(junc.arms[0].dir, junc.arms[1].dir);
        if ( RM_Len(bi) < 0.1f ) { bi = RM_Left(junc.arms[0].dir); } // ほぼ一直線→横向き
        bi = RM_Normalize(bi);
        PlaceJointPiece(rails, junc.followRail, junc.center, std::atan2(bi.x, bi.z), camera);
        return;
    }
    for ( const Arm& arm : junc.arms ) {
        // 入口（t_cut位置）に、凸(+Z)を交差点中心向きで置く
        SplineRail::RailFrame f = rails[arm.rail].GetFrameAtDistance(arm.cutS);
        float yaw = std::atan2(-arm.dir.x, -arm.dir.z);
        PlaceJointPiece(rails, junc.followRail, f.position, yaw, camera);
    }
}

// 1ジャンクションの t_cut を計算し、各レールの切り詰め範囲を登録する
//   隣り合う方向ペア（ウェッジ）ごとに、道の縁線（半幅Wオフセット）の交点から
//   マイター距離を求め、t_cut = max(候補) + 余白（最低 kTCutMin）
void RoadMesh::ComputeArmCuts(const std::vector<SplineRail>& rails, Junction& junc,
                              std::vector<std::vector<Cut>>& cuts) const{
    // 角度でソート（ウェッジ＝隣り合うペア）
    std::sort(junc.arms.begin(), junc.arms.end(), [](const Arm& a, const Arm& b){
        return std::atan2(a.dir.z, a.dir.x) < std::atan2(b.dir.z, b.dir.x);
    });

    const int n = static_cast<int>( junc.arms.size() );
    std::vector<float> tcut(n, kTCutMin);
    for ( int i = 0; i < n; ++i ) {
        int j = ( i + 1 ) % n;
        float angI = std::atan2(junc.arms[i].dir.z, junc.arms[i].dir.x);
        float angJ = std::atan2(junc.arms[j].dir.z, junc.arms[j].dir.x);
        float phi = angJ - angI;
        while ( phi <= 0.0f ) { phi += 2.0f * 3.14159265f; }
        float phiDeg = phi * 180.0f / 3.14159265f;
        if ( phiDeg >= kMiterMaxDeg ) continue; // 直進/円弧は t_cut に影響しない

        float t = 0.0f, s = 0.0f;
        if ( !SolveMiter(junc.arms[i].dir, junc.arms[j].dir, kHalfWidth, t, s) ) continue;
        if ( t > 0.0f ) { tcut[i] = ( std::max )( tcut[i], t + kMiterMargin ); }
        if ( s > 0.0f ) { tcut[j] = ( std::max )( tcut[j], s + kMiterMargin ); }
    }

    for ( int i = 0; i < n; ++i ) {
        Arm& arm = junc.arms[i];
        float len = rails[arm.rail].GetLength();
        // レールが短い場合は入り込みすぎない（反対側の端やノードを越えない）
        float avail = arm.forward ? ( len - arm.nodeS ) : arm.nodeS;
        arm.tCut = std::clamp(tcut[i], kTCutMin, ( std::max )( kTCutMin, avail * 0.45f ));
        arm.cutS = arm.forward ? ( arm.nodeS + arm.tCut ) : ( arm.nodeS - arm.tCut );

        float c0 = ( std::min )( arm.nodeS, arm.cutS );
        float c1 = ( std::max )( arm.nodeS, arm.cutS );
        cuts[arm.rail].push_back({ ( std::max )( 0.0f, c0 ), ( std::min )( len, c1 ) });
    }
}

// 1ジャンクションのパッチ（上面扇+ベベル+壁+底）を生成する（GUIDE_ジャンクション生成 §4）
void RoadMesh::BuildJunctionPatch(const std::vector<SplineRail>& rails, const Junction& junc,
                                  Camera* camera, uint32_t atlasSrv){
    const int n = static_cast<int>( junc.arms.size() );
    if ( n < 2 ) return;
    const Vector3& N = junc.center;

    // 各腕の入口フレームと「左側の符号」（フレームrightが世界の左とどちら向きで一致するか）
    struct ArmGeo { SplineRail::RailFrame frame; float leftSign; };
    std::vector<ArmGeo> geo(n);
    for ( int i = 0; i < n; ++i ) {
        const Arm& arm = junc.arms[i];
        geo[i].frame = rails[arm.rail].GetFrameAtDistance(arm.cutS);
        geo[i].leftSign = ( RM_Dot(geo[i].frame.right, RM_Left(arm.dir)) >= 0.0f ) ? 1.0f : -1.0f;
    }

    // 腕 i の入口断面上の点（lat = 左方向を+とした符号付き幅、h = 断面高さ）
    //   入口はレールの実フレームから取る → 掃引側の入口リングと完全一致（継ぎ目ゼロ）
    auto entryPoint = [&](int i, float lat, float h) -> Vector3{
        const ArmGeo& g = geo[i];
        return RM_Add(g.frame.position,
                      RM_Add(RM_Scale(g.frame.right, g.leftSign * lat),
                             RM_Scale(g.frame.up, h + kTopOffset)));
    };

    // ウェッジ (i → j) の境界点列。幅 w・高さ h。mode: 0=マイター / 1=直進 / 2=円弧
    auto wedgePath = [&](int i, int j, float w, float h, int mode, int arcSteps) -> std::vector<Vector3>{
        std::vector<Vector3> path;
        Vector3 a = entryPoint(i, +w, h);           // 道 i の（ウェッジ側=左）縁
        Vector3 b = entryPoint(j, -w, h);           // 道 j の（ウェッジ側=右）縁
        float y = N.y + h + kTopOffset;             // 内部点はノード高さ基準（付近は平坦に保つ運用）
        path.push_back(a);
        if ( mode == 0 ) {
            float t = 0.0f, s = 0.0f;
            if ( SolveMiter(junc.arms[i].dir, junc.arms[j].dir, w, t, s) && t > 0.0f && s > 0.0f ) {
                Vector3 C = RM_Add(N, RM_Add(RM_Scale(junc.arms[i].dir, t),
                                             RM_Scale(RM_Left(junc.arms[i].dir), w)));
                path.push_back({ C.x, y, C.z });
            }
        } else if ( mode == 2 ) {
            // aからbへNを中心に回る円弧（半径は端点間で線形補間）
            float angA = std::atan2(a.z - N.z, a.x - N.x);
            float angB = std::atan2(b.z - N.z, b.x - N.x);
            float delta = angB - angA;
            while ( delta <= 0.0f ) { delta += 2.0f * 3.14159265f; }
            float rA = std::sqrt(( a.x - N.x ) * ( a.x - N.x ) + ( a.z - N.z ) * ( a.z - N.z ));
            float rB = std::sqrt(( b.x - N.x ) * ( b.x - N.x ) + ( b.z - N.z ) * ( b.z - N.z ));
            for ( int k = 1; k < arcSteps; ++k ) {
                float u = static_cast<float>( k ) / arcSteps;
                float ang = angA + delta * u;
                float r = rA + ( rB - rA ) * u;
                path.push_back({ N.x + std::cos(ang) * r, y, N.z + std::sin(ang) * r });
            }
        }
        path.push_back(b);
        return path;
    };

    Model::ModelData data;
    auto pushV = [&](const Vector3& p, const Vector3& nrm, float u, float v) -> uint32_t{
        Model::VertexData vert {};
        vert.position = { p.x, p.y, p.z, 1.0f };
        vert.normal = nrm;
        vert.texcoord = { u, v };
        data.vertices.push_back(vert);
        return static_cast<uint32_t>( data.vertices.size() - 1 );
    };
    auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c){
        data.indices.push_back(a); data.indices.push_back(b); data.indices.push_back(c);
    };
    // 上面/底面の平面マッピング（アトラスの無地帯へ。REPEATで破綻しないよう帯内でwrap）
    auto topUV = [&](const Vector3& p, float& u, float& v){
        u = ( p.x - N.x ) * kUvPerMeter;
        v = 0.89f + RM_Frac(( p.z - N.z ) * kUvPerMeter) * 0.09f;
    };
    auto bottomUV = [&](const Vector3& p, float& u, float& v){
        u = ( p.x - N.x ) * kUvPerMeter;
        v = 0.4727f + RM_Frac(( p.z - N.z ) * kUvPerMeter) * ( 0.5273f - 0.4727f );
    };
    // 点pの「外向き水平方向」（ノード中心から離れる向き）
    auto outward = [&](const Vector3& p) -> Vector3{
        Vector3 o = { p.x - N.x, 0.0f, p.z - N.z };
        float l = RM_Len(o);
        if ( l < 1e-4f ) return { 1.0f, 0.0f, 0.0f };
        return RM_Scale(o, 1.0f / l);
    };

    // 各ウェッジのモードと円弧分割数を先に決める（内外の点列数を一致させるため）
    std::vector<int> wedgeMode(n), wedgeSteps(n);
    for ( int i = 0; i < n; ++i ) {
        int j = ( i + 1 ) % n;
        float angI = std::atan2(junc.arms[i].dir.z, junc.arms[i].dir.x);
        float angJ = std::atan2(junc.arms[j].dir.z, junc.arms[j].dir.x);
        float phi = angJ - angI;
        while ( phi <= 0.0f ) { phi += 2.0f * 3.14159265f; }
        float phiDeg = phi * 180.0f / 3.14159265f;
        wedgeMode[i]  = ( phiDeg < kMiterMaxDeg ) ? 0 : ( phiDeg > kArcMinDeg ? 2 : 1 );
        wedgeSteps[i] = ( std::max )( 2, static_cast<int>( phiDeg / kArcStepDeg ) );
    }

    // --- 上面：内側境界(W-B)のループをノード中心から扇状に張る ---
    {
        std::vector<Vector3> loop;
        for ( int i = 0; i < n; ++i ) {
            std::vector<Vector3> path = wedgePath(i, ( i + 1 ) % n, kInnerWidth, kTopH, wedgeMode[i], wedgeSteps[i]);
            loop.insert(loop.end(), path.begin(), path.end());
        }
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        float cu, cv;
        Vector3 centerP = { N.x, N.y + kTopH + kTopOffset, N.z };
        topUV(centerP, cu, cv);
        uint32_t c = pushV(centerP, up, cu, cv);
        std::vector<uint32_t> ids(loop.size());
        for ( size_t m = 0; m < loop.size(); ++m ) {
            float u, v; topUV(loop[m], u, v);
            ids[m] = pushV(loop[m], up, u, v);
        }
        for ( size_t m = 0; m < loop.size(); ++m ) {
            pushTri(c, ids[m], ids[( m + 1 ) % loop.size()]);
        }
    }

    // --- ベベル＋壁（ウェッジ部分のみ。入口には張らない＝道の断面がそのまま入る）---
    for ( int i = 0; i < n; ++i ) {
        int j = ( i + 1 ) % n;
        std::vector<Vector3> pin  = wedgePath(i, j, kInnerWidth, kTopH,   wedgeMode[i], wedgeSteps[i]);
        std::vector<Vector3> pout = wedgePath(i, j, kHalfWidth,  kBevelH, wedgeMode[i], wedgeSteps[i]);
        std::vector<Vector3> pbot = wedgePath(i, j, kHalfWidth,  0.0f,    wedgeMode[i], wedgeSteps[i]);
        size_t cnt = ( std::min )( { pin.size(), pout.size(), pbot.size() } );
        if ( cnt < 2 ) continue;

        // 累積距離を u に使う
        float accum = 0.0f;
        std::vector<float> us(cnt, 0.0f);
        for ( size_t m = 1; m < cnt; ++m ) {
            accum += RM_Len(RM_Sub(pout[m], pout[m - 1]));
            us[m] = accum * kUvPerMeter;
        }

        for ( size_t m = 0; m + 1 < cnt; ++m ) {
            // ベベル（上0.7422 / 下0.7070。法線= 外向き0.64 + 上0.77）
            Vector3 n0 = RM_Normalize(RM_Add(RM_Scale(outward(pout[m]), 0.64f), Vector3 { 0.0f, 0.77f, 0.0f }));
            Vector3 n1 = RM_Normalize(RM_Add(RM_Scale(outward(pout[m + 1]), 0.64f), Vector3 { 0.0f, 0.77f, 0.0f }));
            uint32_t i0 = pushV(pin[m],      n0, us[m],     0.7422f);
            uint32_t i1 = pushV(pin[m + 1],  n1, us[m + 1], 0.7422f);
            uint32_t o0 = pushV(pout[m],     n0, us[m],     0.7070f);
            uint32_t o1 = pushV(pout[m + 1], n1, us[m + 1], 0.7070f);
            pushTri(i0, o0, o1); pushTri(i0, o1, i1);

            // 壁（上0.6992 / 下0.5352。法線=外向き水平）
            Vector3 w0 = outward(pout[m]);
            Vector3 w1 = outward(pout[m + 1]);
            uint32_t t0 = pushV(pout[m],     w0, us[m],     0.6992f);
            uint32_t t1 = pushV(pout[m + 1], w1, us[m + 1], 0.6992f);
            uint32_t b0 = pushV(pbot[m],     w0, us[m],     0.5352f);
            uint32_t b1 = pushV(pbot[m + 1], w1, us[m + 1], 0.5352f);
            pushTri(t0, b0, b1); pushTri(t0, b1, t1);
        }
    }

    // --- 底面：外周(W)のループをノード中心（高さ0）から扇状に張る ---
    {
        std::vector<Vector3> loop;
        for ( int i = 0; i < n; ++i ) {
            std::vector<Vector3> path = wedgePath(i, ( i + 1 ) % n, kHalfWidth, 0.0f, wedgeMode[i], wedgeSteps[i]);
            loop.insert(loop.end(), path.begin(), path.end());
        }
        Vector3 dn = { 0.0f, -1.0f, 0.0f };
        Vector3 centerP = { N.x, N.y + kTopOffset, N.z };
        float cu, cv;
        bottomUV(centerP, cu, cv);
        uint32_t c = pushV(centerP, dn, cu, cv);
        std::vector<uint32_t> ids(loop.size());
        for ( size_t m = 0; m < loop.size(); ++m ) {
            float u, v; bottomUV(loop[m], u, v);
            ids[m] = pushV(loop[m], dn, u, v);
        }
        for ( size_t m = 0; m < loop.size(); ++m ) {
            pushTri(c, ids[( m + 1 ) % loop.size()], ids[m]);
        }
    }

    EmitMesh(data, junc.followRail, camera, atlasSrv);
}

// ジャンクション（溶接コーナー/T字/十字）の検出とパッチ生成（任意角度対応）
//   ・端点溶接（2本）… 150°以上のゆるい角は掃引コネクタ / 鋭い角はパッチ
//   ・T字分岐（branchPoints）と本体×本体の交差 … 常にパッチ
//   検出したジャンクションの t_cut 範囲は cuts に登録し、掃引側が面を張らない
void RoadMesh::CollectJunctions(const std::vector<SplineRail>& rails, Camera* camera, uint32_t atlasSrv,
                                std::vector<std::vector<Cut>>& cuts){
    const int n = static_cast<int>( rails.size() );
    // 道を敷く対象のレールか（§4：roadMode=1「道なし」は接続相手としても数えない）
    auto roadUsable = [&](int idx) -> bool{
        return idx >= 0 && idx < n && rails[idx].visible && rails[idx].roadMode == 0 &&
               rails[idx].nodes.size() >= 2 && rails[idx].GetLength() > 0.0f;
    };

    std::vector<Junction> juncs;

    // --- A) 端点溶接（2本の共有ノード）---
    for ( int i = 0; i < n; ++i ) {
        if ( !roadUsable(i) || rails[i].isLoop ) continue;
        for ( int side = 0; side < 2; ++side ) {
            const bool front = ( side == 0 );
            int conn = front ? rails[i].frontConnIndex : rails[i].backConnIndex;
            if ( conn < i ) continue; // 未接続(-1)と、相手側の走査で処理済みのペアを除外
            if ( !roadUsable(conn) || rails[conn].isLoop ) continue;

            const SplineRail& a = rails[i];
            const SplineRail& b = rails[conn];
            bool bFront = front ? a.frontConnToFront : a.backConnToFront;

            // ノードから出ていく方向（水平射影）
            Vector3 aDirRaw = front ? a.GetTangentByDistance(0.0f)
                                    : RM_Scale(a.GetTangentByDistance(a.GetLength()), -1.0f);
            Vector3 bDirRaw = bFront ? b.GetTangentByDistance(0.0f)
                                     : RM_Scale(b.GetTangentByDistance(b.GetLength()), -1.0f);
            Vector3 aDir, bDir;
            if ( !RM_Horizontal(aDirRaw, aDir) || !RM_Horizontal(bDirRaw, bDir) ) continue;

            float d = RM_Dot(aDir, bDir);
            if ( d <= kStraightDot ) continue; // ほぼ一直線＝突き合わせのままで綺麗

            if ( d <= kGentleDot ) {
                // --- ゆるい角（150°以上）：掃引コネクタで連続的に繋ぐ ---
                if ( a.GetLength() < 2.5f || b.GetLength() < 2.5f ) continue;
                float trimA = ( std::min )( kArm, a.GetLength() * 0.4f );
                float trimB = ( std::min )( kArm, b.GetLength() * 0.4f );

                Vector3 pPre  = a.GetPositionByDistance(front ? trimA * 2.0f : a.GetLength() - trimA * 2.0f);
                Vector3 p0    = a.GetPositionByDistance(front ? trimA        : a.GetLength() - trimA);
                Vector3 pc    = front ? a.GetPositionByDistance(0.0f) : a.GetPositionByDistance(a.GetLength());
                Vector3 p1    = b.GetPositionByDistance(bFront ? trimB        : b.GetLength() - trimB);
                Vector3 pPost = b.GetPositionByDistance(bFront ? trimB * 2.0f : b.GetLength() - trimB * 2.0f);

                // その場で小さなレール（Catmull-Rom）を合成して同じ断面で掃引する
                SplineRail connector;
                connector.nodes = { pPre, p0, pc, p1, pPost };
                connector.BuildDistanceTable(); // FrameCache もここで自動構築される

                // 助走区間（pPre〜p0 と p1〜pPost）は本線の掃引と重なるので張らない
                float s0 = connector.GetDistanceFromT(1.0f);
                float s1 = connector.GetDistanceFromT(3.0f);
                std::vector<Cut> connCuts = { { 0.0f, s0 }, { s1, connector.GetLength() } };
                BuildRailMesh(connector, i, camera, atlasSrv, connCuts, false);

                cuts[i].push_back(front ? Cut { 0.0f, trimA } : Cut { a.GetLength() - trimA, a.GetLength() });
                cuts[conn].push_back(bFront ? Cut { 0.0f, trimB } : Cut { b.GetLength() - trimB, b.GetLength() });

                // ゆるい角にもジョイントは置く（2本溶接＝中央1個）
                Junction jj;
                jj.center = pc;
                jj.followRail = i;
                jj.arms.push_back({ i, front ? 0.0f : a.GetLength(), 0.0f, trimA, front, aDir });
                jj.arms.push_back({ conn, bFront ? 0.0f : b.GetLength(), 0.0f, trimB, bFront, bDir });
                PlaceJoints(rails, jj, camera);
                continue;
            }

            // --- 鋭い角（150°未満）：ジャンクションパッチ ---
            Junction junc;
            junc.center = front ? a.GetPositionByDistance(0.0f) : a.GetPositionByDistance(a.GetLength());
            junc.followRail = i;
            junc.arms.push_back({ i, front ? 0.0f : a.GetLength(), 0.0f, kTCutMin, front, aDir });
            junc.arms.push_back({ conn, bFront ? 0.0f : b.GetLength(), 0.0f, kTCutMin, bFront, bDir });
            juncs.push_back(junc);
        }
    }

    // --- B) T字分岐（branchPoints）：本線2本＋支線1本の3方向パッチ ---
    for ( int i = 0; i < n; ++i ) {
        if ( !roadUsable(i) ) continue;
        for ( const auto& bp : rails[i].branchPoints ) {
            int j = bp.targetRail;
            if ( !roadUsable(j) ) continue;

            Vector3 m1raw = rails[i].GetTangentByDistance(bp.distance);
            bool tFront = bp.targetDist < rails[j].GetLength() * 0.5f;
            Vector3 brRaw = tFront ? rails[j].GetTangentByDistance(0.0f)
                                   : RM_Scale(rails[j].GetTangentByDistance(rails[j].GetLength()), -1.0f);
            Vector3 m1, br;
            if ( !RM_Horizontal(m1raw, m1) || !RM_Horizontal(brRaw, br) ) continue;
            if ( std::abs(RM_Dot(m1, br)) > 0.985f ) continue; // 支線が本線とほぼ平行→パッチが潰れる

            Junction junc;
            junc.center = rails[i].GetPositionByDistance(bp.distance);
            junc.followRail = i;
            junc.arms.push_back({ i, bp.distance, 0.0f, kTCutMin, true,  m1 });
            junc.arms.push_back({ i, bp.distance, 0.0f, kTCutMin, false, RM_Scale(m1, -1.0f) });
            junc.arms.push_back({ j, tFront ? 0.0f : rails[j].GetLength(), 0.0f, kTCutMin, tFront, br });
            juncs.push_back(junc);
        }
    }

    // --- C) 本体×本体の交差（乗り換えポイント）：4方向パッチ ---
    for ( int i = 0; i < n; ++i ) {
        if ( !roadUsable(i) ) continue;
        for ( int j = i + 1; j < n; ++j ) {
            if ( !roadUsable(j) ) continue;
            if ( rails[i].HasMotion() || rails[j].HasMotion() ) continue; // 動くレール同士の交差は追従できない

            float lenI = rails[i].GetLength(), lenJ = rails[j].GetLength();
            float lastPlaced = -1e9f;
            for ( float s = 0.0f; s <= lenI; s += 0.5f ) {
                Vector3 p = rails[i].GetPositionByDistance(s);
                float cd = rails[j].GetClosestDistance(p);
                Vector3 q = rails[j].GetPositionByDistance(cd);
                if ( RM_Len(RM_Sub(q, p)) > 0.9f ) continue;

                // 端の近くは溶接/分岐の領分（二重配置を防ぐ）
                if ( s < 1.5f || s > lenI - 1.5f || cd < 1.5f || cd > lenJ - 1.5f ) continue;
                if ( s - lastPlaced < 2.0f ) continue; // 同じ交差のクラスタ

                // 交差中心を細かく詰める（±0.6mを0.1刻みで最短距離の点へ）
                float bestS = s, bestD = 1e9f;
                for ( float t = s - 0.6f; t <= s + 0.6f; t += 0.1f ) {
                    if ( t < 0.0f || t > lenI ) continue;
                    Vector3 pp = rails[i].GetPositionByDistance(t);
                    float ccd = rails[j].GetClosestDistance(pp);
                    float dd = RM_Len(RM_Sub(rails[j].GetPositionByDistance(ccd), pp));
                    if ( dd < bestD ) { bestD = dd; bestS = t; }
                }
                float sC = bestS;
                float cdC = rails[j].GetClosestDistance(rails[i].GetPositionByDistance(sC));

                Vector3 A1, B1;
                if ( !RM_Horizontal(rails[i].GetTangentByDistance(sC), A1) ) continue;
                if ( !RM_Horizontal(rails[j].GetTangentByDistance(cdC), B1) ) continue;
                if ( std::abs(RM_Dot(A1, B1)) > 0.95f ) continue; // ほぼ平行＝交差にならない

                Vector3 pi = rails[i].GetPositionByDistance(sC);
                Vector3 qj = rails[j].GetPositionByDistance(cdC);

                Junction junc;
                junc.center = { ( pi.x + qj.x ) * 0.5f, ( pi.y + qj.y ) * 0.5f, ( pi.z + qj.z ) * 0.5f };
                junc.followRail = i;
                junc.arms.push_back({ i, sC,  0.0f, kTCutMin, true,  A1 });
                junc.arms.push_back({ i, sC,  0.0f, kTCutMin, false, RM_Scale(A1, -1.0f) });
                junc.arms.push_back({ j, cdC, 0.0f, kTCutMin, true,  B1 });
                junc.arms.push_back({ j, cdC, 0.0f, kTCutMin, false, RM_Scale(B1, -1.0f) });
                juncs.push_back(junc);
                lastPlaced = sC;
            }
        }
    }

    // --- t_cut 計算 → パッチ生成 → ジョイント配置 ---
    for ( Junction& junc : juncs ) {
        ComputeArmCuts(rails, junc, cuts);
        BuildJunctionPatch(rails, junc, camera, atlasSrv);
        PlaceJoints(rails, junc, camera);
    }
}

// レールに沿って道を敷き直す
void RoadMesh::Build(const std::vector<SplineRail>& rails, Camera* camera, bool simple){
    slotsUsed_ = 0;
    piecesUsed_ = 0;
    jointsUsed_ = 0;

    // アトラス（GamePlayScene::LoadResources で先読み済み＝ここではキャッシュが返るだけ）
    uint32_t atlasSrv = TextureManager::GetInstance()->Load("resources/road/road_atlas.png").srvIndex;
    Model* endModel = ModelManager::GetInstance()->FindModel("roadEnd");

    const int n = static_cast<int>( rails.size() );
    auto roadUsable = [&](int idx) -> bool{
        return idx >= 0 && idx < n && rails[idx].visible && rails[idx].roadMode == 0 &&
               rails[idx].nodes.size() >= 2 && rails[idx].GetLength() > 0.0f;
    };

    // ジャンクションの検出＋パッチ配置。各レールの「掃引しない区間」も決まる
    std::vector<std::vector<Cut>> cuts(rails.size());
    if ( !simple ) { CollectJunctions(rails, camera, atlasSrv, cuts); }

    for ( int railIdx = 0; railIdx < n; ++railIdx ) {
        if ( !roadUsable(railIdx) ) continue;
        const SplineRail& rail = rails[railIdx];
        float len = rail.GetLength();

        // 掃引メッシュ本体（ジャンクションの切り詰め範囲は張らない）
        BuildRailMesh(rail, railIdx, camera, atlasSrv, cuts[railIdx], simple);

        if ( simple ) continue; // 簡易ビルドはキャップ類を省略（マウスアップ後の本生成で付く）

        const std::vector<SplineRail::HoleInterval> holes = rail.GetHoleIntervals();
        auto endInCut = [&](float s) -> bool{
            for ( const Cut& c : cuts[railIdx] ) {
                if ( s >= c.s0 - 0.01f && s <= c.s1 + 0.01f ) return true;
            }
            return false;
        };
        auto endInHole = [&](float s) -> bool{
            for ( const auto& h : holes ) {
                if ( s >= h.d0 - 0.01f && s <= h.d1 + 0.01f ) return true;
            }
            return false;
        };

        // --- 穴の両端に road_end を自動配置（仕様書_穴区間 §2-2）---
        //   手前側(d0)は +tangent 向き / 奥側(d1)は -tangent 向き（開口が道側を向く）
        if ( endModel ) {
            for ( const auto& h : holes ) {
                if ( h.d0 > 0.05f && !endInCut(h.d0) ) {
                    SplineRail::RailFrame f = rail.GetFrameAtDistance(h.d0);
                    PlaceEndCap(endModel, rails, railIdx, f.position, RM_Add(f.position, f.tangent), camera);
                }
                if ( h.d1 < len - 0.05f && !endInCut(h.d1) ) {
                    SplineRail::RailFrame f = rail.GetFrameAtDistance(h.d1);
                    PlaceEndCap(endModel, rails, railIdx, f.position, RM_Sub(f.position, f.tangent), camera);
                }
            }
        }

        // --- 終端キャップ：非ループの「自由端」だけに置く ---
        //   溶接済みの端は隣の道と繋がるのでキャップ不要。ただし相手が「道なし」レールなら
        //   実質自由端なのでキャップを付ける（§4）。穴に食われた端にも付けない
        if ( !rail.isLoop && endModel ) {
            const float kProbe = ( std::min )( 1.0f, len * 0.5f );
            bool backFree  = ( rail.backConnIndex < 0 || !roadUsable(rail.backConnIndex) );
            bool frontFree = ( rail.frontConnIndex < 0 || !roadUsable(rail.frontConnIndex) );
            if ( backFree && !endInCut(len) && !endInHole(len) ) {
                Vector3 e1  = rail.GetPositionByDistance(len);
                Vector3 e1b = rail.GetPositionByDistance(len - kProbe);
                PlaceEndCap(endModel, rails, railIdx, e1,
                    { e1.x * 2.0f - e1b.x, e1.y * 2.0f - e1b.y, e1.z * 2.0f - e1b.z }, camera);
            }
            if ( frontFree && !endInCut(0.0f) && !endInHole(0.0f) ) {
                Vector3 e0  = rail.GetPositionByDistance(0.0f);
                Vector3 e0b = rail.GetPositionByDistance(kProbe);
                PlaceEndCap(endModel, rails, railIdx, e0,
                    { e0.x * 2.0f - e0b.x, e0.y * 2.0f - e0b.y, e0.z * 2.0f - e0b.z }, camera);
            }
        }
    }

    // 余ったスロットは空メッシュにして描かない（バッファは保持＝次の編集で使い回す）
    static const std::vector<Model::VertexData> kEmptyV;
    static const std::vector<uint32_t> kEmptyI;
    for ( size_t k = slotsUsed_; k < slots_.size(); ++k ) {
        slots_[k]->model->UpdateMesh(kEmptyV, kEmptyI);
    }
}

// 毎フレーム：動くレールの animOffset に追従し、カメラ行列を焼き直す
void RoadMesh::Update(const std::vector<SplineRail>& rails){
    for ( size_t k = 0; k < slotsUsed_; ++k ) {
        MeshSlot& s = *slots_[k];
        if ( s.rail >= 0 && s.rail < ( int ) rails.size() ) {
            // 掃引メッシュはワールド座標で焼いてあるので、動くレールぶんだけ平行移動
            s.obj->SetTranslation(rails[s.rail].animOffset);
        }
        s.obj->Update();
    }
    auto updatePieces = [&](std::vector<std::unique_ptr<PieceSlot>>& pool, size_t used){
        for ( size_t k = 0; k < used; ++k ) {
            PieceSlot& s = *pool[k];
            if ( s.rail >= 0 && s.rail < ( int ) rails.size() ) {
                const Vector3& off = rails[s.rail].animOffset;
                s.obj->SetTranslation({ s.base.x + off.x, s.base.y + off.y, s.base.z + off.z });
            }
            s.obj->Update();
        }
    };
    updatePieces(pieces_, piecesUsed_);
    updatePieces(joints_, jointsUsed_);
}

void RoadMesh::Draw() const{
    if ( !visible_ ) return;
    for ( size_t k = 0; k < slotsUsed_; ++k )  { slots_[k]->obj->Draw(); }
    for ( size_t k = 0; k < piecesUsed_; ++k ) { pieces_[k]->obj->Draw(); }

    // ジョイントは表示モードに従う（0=エディタのみ / 1=常に / 2=非表示）
    bool showJoints = ( jointVisible_ == 1 ) ||
                      ( jointVisible_ == 0 && EditorManager::GetInstance()->GetMode() == EngineMode::Edit );
    if ( showJoints ) {
        for ( size_t k = 0; k < jointsUsed_; ++k ) { joints_[k]->obj->Draw(); }
    }
}
