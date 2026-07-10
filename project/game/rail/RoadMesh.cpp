#include "game/rail/RoadMesh.h"

#include "engine/rail/SplineRail.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/graphics/TextureManager.h"
#include "engine/graphics/PipelineManager.h"

#include <cmath>
#include <algorithm>
#include <array>

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

// 切り口フタ用：断面の外周をなす6頂点（プロファイル番号。凸六角形）
const int kOutline[6] = { 0, 1, 3, 5, 7, 9 };

// --- 生成パラメータ（設計書 §4 の確定値）---
const float kWalkStep   = 0.05f;                            // サンプリング歩幅(m)
const float kMinRingGap = 0.06f;                            // リング最小間隔(m)
const float kMaxRingGap = 1.00f;                            // リング最大間隔(m)
const float kCurveAngle = 3.0f * 3.14159265f / 180.0f;      // リングを置く累積角(3°)
const float kSlopeIn    = 0.45f;                            // 坂UVに入る |tangent.y|（ヒステリシス上側）
const float kSlopeOut   = 0.35f;                            // 坂UVから抜ける（下側）
const float kTopOffset  = -0.30f;                           // 断面高さへの加算。上面(0.25)がレールの5cm下に来る
const float kUvPerMeter = 1.0f / 2.0f;                      // u = 距離 / 2m（ピースとテクスチャ周期を共有）

// --- 交差点まわり ---
const float kArm = 1.0f;  // 2x2ピースの中心から開口までの距離。掃引はここまでで止める

// 水平方向を最も近い±X/±Z軸へスナップ（約20°以内。外れたら false）
bool SnapAxis(const Vector3& d, Vector3& out){
    float l = std::sqrt(d.x * d.x + d.z * d.z);
    if ( l < 1e-4f ) return false;
    float x = d.x / l, z = d.z / l;
    if ( std::abs(x) > 0.94f ) { out = { x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f }; return true; }
    if ( std::abs(z) > 0.94f ) { out = { 0.0f, 0.0f, z > 0.0f ? 1.0f : -1.0f }; return true; }
    return false;
}

// ローカル方向を yaw で回す（エンジンの行ベクトルRyと同じ向き：+Z が yaw=atan2(x,z) 方向へ）
Vector3 RotY(const Vector3& v, float yaw){
    float c = std::cos(yaw), s = std::sin(yaw);
    return { v.x * c + v.z * s, 0.0f, v.z * c - v.x * s };
}

// ピースのローカル開口方向群が、実際のアーム方向群と一致する yaw(90°単位) を探す
bool MatchYaw(const std::vector<Vector3>& localOpenings, const std::vector<Vector3>& arms, float& outYaw){
    if ( localOpenings.size() != arms.size() ) return false;
    const float yaws[4] = { 0.0f, 1.57079633f, 3.14159265f, 4.71238898f };
    for ( float y : yaws ) {
        bool all = true;
        for ( const Vector3& lo : localOpenings ) {
            Vector3 w = RotY(lo, y);
            bool hit = false;
            for ( const Vector3& a : arms ) {
                if ( RM_Dot(w, a) > 0.9f ) { hit = true; break; }
            }
            if ( !hit ) { all = false; break; }
        }
        if ( all ) { outYaw = y; return true; }
    }
    return false;
}

} // namespace

RoadMesh::RoadMesh() = default;
RoadMesh::~RoadMesh() = default;

// レール1本ぶんの掃引メッシュを生成する
void RoadMesh::BuildRailMesh(const SplineRail& rail, int railIdx, Camera* camera, uint32_t atlasSrv,
                             const std::vector<Cut>& cuts){
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
        float c = std::clamp(RM_Dot(prevTan, f.tangent), -1.0f, 1.0f);
        accumAngle += std::acos(c);
        prevTan = f.tangent;
        sinceLast += kWalkStep;

        // 坂UVの切替（入り0.45 / 抜け0.35 のヒステリシスでチラつかない）
        float ty = std::abs(f.tangent.y);
        bool newSlope = slope ? ( ty > kSlopeOut ) : ( ty > kSlopeIn );
        if ( newSlope != slope ) {
            rings.push_back({ s, slope });      // 境目は同位置の二重リング（vだけ切替）
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
    std::vector<std::array<Vector3, 12>> ringPos(rings.size()); // フタ生成用に溶接後の位置を保存

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
            if ( hasPrev && RM_Dot({ pos.x - prevPos[k].x, pos.y - prevPos[k].y, pos.z - prevPos[k].z },
                                   f.tangent) < 0.0f ) {
                pos = prevPos[k];
            }
            ringPos[ri][k] = pos;

            float v = pv.v;
            if ( ring.slope && ( k == 4 || k == 5 ) ) { v = kSlopeV[k - 4]; } // 坂帯へ切替

            Model::VertexData vert {};
            vert.position = { pos.x, pos.y, pos.z, 1.0f };
            vert.normal = RM_Normalize(RM_Add(RM_Scale(f.right, pv.nr), RM_Scale(f.up, pv.nu)));
            vert.texcoord = { ring.s * kUvPerMeter, v };
            data.vertices.push_back(vert);
        }
        for ( int k = 0; k < 12; ++k ) { prevPos[k] = ringPos[ri][k]; }
        hasPrev = true;
    }

    // --- 3. 区間の分類：描く / 穴 / 交差点の切り詰め / 二重リング ---
    enum class Seg { Draw, Hole, Cut, Degen };
    std::vector<Seg> segs(rings.size() - 1);
    for ( size_t i = 0; i + 1 < rings.size(); ++i ) {
        float mid = ( rings[i].s + rings[i + 1].s ) * 0.5f;
        if ( rings[i + 1].s - rings[i].s < 1e-4f ) { segs[i] = Seg::Degen; continue; }
        bool inCut = false;
        for ( const Cut& c : cuts ) {
            if ( mid >= c.s0 && mid <= c.s1 ) { inCut = true; break; }
        }
        if ( inCut )                          { segs[i] = Seg::Cut; }
        else if ( rail.IsHoleAtDistance(mid) ){ segs[i] = Seg::Hole; }
        else                                  { segs[i] = Seg::Draw; }
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

    // --- 5. 穴の切り口にフタを張る（中の空洞が見えないように。交差点のCutはピースが塞ぐので不要）---
    //   断面の外周（凸六角形）を扇状に三角形化。UVはアトラスの壁の帯（無地寄り）から取る
    auto emitCap = [&](size_t ringIdx, float facing){
        SplineRail::RailFrame f = rail.GetFrameAtDistance(rings[ringIdx].s);
        Vector3 n = RM_Scale(f.tangent, facing);
        uint32_t base = static_cast<uint32_t>( data.vertices.size() );
        for ( int k = 0; k < 6; ++k ) {
            const ProfileV& pv = kProfile[kOutline[k]];
            Model::VertexData vert {};
            const Vector3& pos = ringPos[ringIdx][kOutline[k]];
            vert.position = { pos.x, pos.y, pos.z, 1.0f };
            vert.normal = n;
            vert.texcoord = { 0.10f + ( pv.lat + 1.0f ) * 0.15f, 0.62f - pv.h * 0.30f };
            data.vertices.push_back(vert);
        }
        for ( uint32_t t = 1; t <= 4; ++t ) {
            data.indices.push_back(base);
            data.indices.push_back(base + t);
            data.indices.push_back(base + t + 1);
        }
    };
    for ( size_t i = 0; i < segs.size(); ++i ) {
        if ( segs[i] != Seg::Hole ) continue;
        // 穴区間の始まり：直前（Degenは透過）が Draw ならリング i にフタ（穴側を向く）
        size_t prev = i;
        while ( prev > 0 && segs[prev - 1] == Seg::Degen ) { --prev; }
        if ( prev > 0 && segs[prev - 1] == Seg::Draw ) { emitCap(i, +1.0f); }
        // 穴区間の終わり：直後（Degenは透過）が Draw ならリング i+1 にフタ（穴側を向く）
        size_t next = i;
        while ( next + 1 < segs.size() && segs[next + 1] == Seg::Degen ) { ++next; }
        if ( next + 1 < segs.size() ? segs[next + 1] == Seg::Draw : false ) { emitCap(i + 1, -1.0f); }
    }

    if ( data.vertices.empty() || data.indices.empty() ) return;

    // --- 6. Model + Obj3d 化（頂点はワールド座標なので配置は animOffset の平行移動だけ）---
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

// ピースを1個置く共通処理（動くレール追従用の基準位置も記録）
void RoadMesh::PlacePiece(Model* model, int railIdx, const std::vector<SplineRail>& rails,
                          const Vector3& pos, float yaw, float pitch, Camera* camera){
    if ( !model ) return;
    auto obj = std::make_unique<Obj3d>();
    obj->Initialize(model);
    obj->SetCamera(camera);
    Vector3 p = { pos.x, pos.y + kTopOffset, pos.z }; // 上面(0.25)がレールの5cm下に来る高さ
    obj->SetTranslation(p);
    obj->SetRotation({ pitch, yaw, 0.0f });
    obj->Update();
    capObjs_.push_back(std::move(obj));
    capRail_.push_back(railIdx);
    const Vector3& off = rails[railIdx].animOffset;
    capBase_.push_back({ p.x - off.x, p.y - off.y, p.z - off.z });
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
    PlacePiece(model, railIdx, rails, p0, yaw, pitch, camera);
}

// 交差点の検出とピース配置（設計書 §5。90°グリッド沿いのレール限定）
//   ・溶接コーナー（端点同士が直角）→ road_corner
//   ・T字分岐（branchPoints）      → road_t
//   ・本体×本体の十字交差          → road_cross
//   置いた交差点の周囲1mは cuts に登録し、掃引メッシュ側が面を張らない
void RoadMesh::CollectJunctions(const std::vector<SplineRail>& rails, Camera* camera,
                                std::vector<std::vector<Cut>>& cuts){
    ModelManager* mm = ModelManager::GetInstance();
    Model* tM     = mm->FindModel("roadT");
    Model* crossM = mm->FindModel("roadCross");
    // ※コーナーはピース(road_corner)ではなく掃引コネクタで滑らかに繋ぐ（下のA節）

    auto addCut = [&](int railIdx, float s0, float s1){
        float len = rails[railIdx].GetLength();
        cuts[railIdx].push_back({ ( std::max )( 0.0f, s0 ), ( std::min )( len, s1 ) });
    };
    const int n = static_cast<int>( rails.size() );
    auto usable = [&](int idx) -> bool {
        return idx >= 0 && idx < n && rails[idx].visible && rails[idx].nodes.size() >= 2;
    };

    // --- A) 端点溶接のコーナー → なめらか接続（掃引コネクタ）---
    //   ピースを置くのではなく、両レールの端1mを削って、その間を通る小さな
    //   スプラインをその場で合成し「同じ断面で掃引」する。90°に限らず
    //   どんな角度でも・高低差があっても「ぐにゃっ」と滑らかに曲がる。
    uint32_t atlasSrv = TextureManager::GetInstance()->Load("resources/road/road_atlas.png").srvIndex;
    for ( int i = 0; i < n; ++i ) {
        if ( !usable(i) || rails[i].isLoop ) continue;
        for ( int side = 0; side < 2; ++side ) {
            const bool front = ( side == 0 );
            int conn = front ? rails[i].frontConnIndex : rails[i].backConnIndex;
            if ( conn < i ) continue; // 未接続(-1)と、相手側の走査で処理済みのペアを除外
            if ( !usable(conn) || rails[conn].isLoop ) continue;

            const SplineRail& a = rails[i];
            const SplineRail& b = rails[conn];
            bool bFront = front ? a.frontConnToFront : a.backConnToFront;

            // アーム＝交点から各レールの内側へ向かう方向
            Vector3 aDir = RM_Normalize(front ? a.GetTangentByDistance(0.0f)
                                              : RM_Scale(a.GetTangentByDistance(a.GetLength()), -1.0f));
            Vector3 bDir = RM_Normalize(bFront ? b.GetTangentByDistance(0.0f)
                                               : RM_Scale(b.GetTangentByDistance(b.GetLength()), -1.0f));
            if ( RM_Dot(aDir, bDir) < -0.7f ) continue; // ほぼ一直線＝突き合わせのままで綺麗

            // 短すぎるレールはコネクタを作れない（削る余白が無い）
            if ( a.GetLength() < 2.5f || b.GetLength() < 2.5f ) continue;
            float trimA = ( std::min )( kArm, a.GetLength() * 0.4f );
            float trimB = ( std::min )( kArm, b.GetLength() * 0.4f );

            // コネクタの通過点：手前側の余韻(pPre/pPost)は接線を合わせるための助走
            Vector3 pPre  = a.GetPositionByDistance(front ? trimA * 2.0f : a.GetLength() - trimA * 2.0f);
            Vector3 p0    = a.GetPositionByDistance(front ? trimA        : a.GetLength() - trimA);
            Vector3 pc    = front ? a.GetPositionByDistance(0.0f) : a.GetPositionByDistance(a.GetLength());
            Vector3 p1    = b.GetPositionByDistance(bFront ? trimB        : b.GetLength() - trimB);
            Vector3 pPost = b.GetPositionByDistance(bFront ? trimB * 2.0f : b.GetLength() - trimB * 2.0f);

            // その場で小さなレール（Catmull-Romスプライン）を合成して掃引する。
            //   前後の助走ノードで両レールと接線がほぼ一致し、継ぎ目が目立たない
            SplineRail connector;
            connector.nodes = { pPre, p0, pc, p1, pPost };
            connector.BuildDistanceTable(); // FrameCache もここで自動構築される

            // 助走区間（pPre〜p0 と p1〜pPost）は本線の掃引と重なるので張らない
            float s0 = connector.GetDistanceFromT(1.0f);
            float s1 = connector.GetDistanceFromT(3.0f);
            std::vector<Cut> connCuts = { { 0.0f, s0 }, { s1, connector.GetLength() } };
            BuildRailMesh(connector, i, camera, atlasSrv, connCuts);

            // 本線側はコーナーのぶんだけ切り詰める
            addCut(i, front ? 0.0f : a.GetLength() - trimA, front ? trimA : a.GetLength());
            addCut(conn, bFront ? 0.0f : b.GetLength() - trimB, bFront ? trimB : b.GetLength());
        }
    }

    // --- B) T字分岐（branchPoints → road_t）---
    for ( int i = 0; i < n; ++i ) {
        if ( !usable(i) ) continue;
        for ( const auto& bp : rails[i].branchPoints ) {
            int j = bp.targetRail;
            if ( !usable(j) ) continue;

            Vector3 m1raw = rails[i].GetTangentByDistance(bp.distance);
            bool tFront = bp.targetDist < rails[j].GetLength() * 0.5f;
            Vector3 brRaw = tFront ? rails[j].GetTangentByDistance(0.0f)
                                   : RM_Scale(rails[j].GetTangentByDistance(rails[j].GetLength()), -1.0f);
            Vector3 m1, br;
            if ( !SnapAxis(m1raw, m1) || !SnapAxis(brRaw, br) ) continue;
            if ( std::abs(RM_Dot(m1, br)) > 0.5f ) continue; // 分岐が本線と平行→ピースが合わない
            Vector3 m2 = RM_Scale(m1, -1.0f);

            float yaw = 0.0f;
            if ( !tM || !MatchYaw({ { 1,0,0 }, { -1,0,0 }, { 0,0,1 } }, { m1, m2, br }, yaw) ) continue;

            Vector3 pos = rails[i].GetPositionByDistance(bp.distance);
            PlacePiece(tM, i, rails, pos, yaw, 0.0f, camera);
            addCut(i, bp.distance - kArm, bp.distance + kArm);
            addCut(j, tFront ? 0.0f : rails[j].GetLength() - kArm, tFront ? kArm : rails[j].GetLength());
        }
    }

    // --- C) 本体×本体の十字交差（乗り換えポイント → road_cross）---
    for ( int i = 0; i < n; ++i ) {
        if ( !usable(i) ) continue;
        for ( int j = i + 1; j < n; ++j ) {
            if ( !usable(j) ) continue;
            if ( rails[i].HasMotion() || rails[j].HasMotion() ) continue; // 動くレールはピースが追従できない

            float lenI = rails[i].GetLength(), lenJ = rails[j].GetLength();
            float lastPlaced = -1e9f;
            for ( float s = 0.0f; s <= lenI; s += 0.5f ) {
                Vector3 p = rails[i].GetPositionByDistance(s);
                float cd = rails[j].GetClosestDistance(p);
                Vector3 q = rails[j].GetPositionByDistance(cd);
                float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                if ( std::sqrt(dx * dx + dy * dy + dz * dz) > 0.9f ) continue;

                // 端の近くは溶接/分岐の領分（二重配置を防ぐ）
                if ( s < 1.5f || s > lenI - 1.5f || cd < 1.5f || cd > lenJ - 1.5f ) continue;
                if ( s - lastPlaced < 2.0f ) continue; // 同じ交差のクラスタ

                // 交差中心を細かく詰める（±0.6mを0.1刻みで最短距離の点へ）
                float bestS = s, bestD = 1e9f;
                for ( float t = s - 0.6f; t <= s + 0.6f; t += 0.1f ) {
                    if ( t < 0.0f || t > lenI ) continue;
                    Vector3 pp = rails[i].GetPositionByDistance(t);
                    float ccd = rails[j].GetClosestDistance(pp);
                    Vector3 qq = rails[j].GetPositionByDistance(ccd);
                    float ddx = qq.x - pp.x, ddy = qq.y - pp.y, ddz = qq.z - pp.z;
                    float dd = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
                    if ( dd < bestD ) { bestD = dd; bestS = t; }
                }
                float sC = bestS;
                float cdC = rails[j].GetClosestDistance(rails[i].GetPositionByDistance(sC));

                Vector3 A1, B1;
                if ( !SnapAxis(rails[i].GetTangentByDistance(sC), A1) ) continue;
                if ( !SnapAxis(rails[j].GetTangentByDistance(cdC), B1) ) continue;
                if ( std::abs(RM_Dot(A1, B1)) > 0.5f ) continue; // ほぼ平行＝十字にならない

                Vector3 pi = rails[i].GetPositionByDistance(sC);
                Vector3 qj = rails[j].GetPositionByDistance(cdC);
                Vector3 pos = { ( pi.x + qj.x ) * 0.5f, ( pi.y + qj.y ) * 0.5f, ( pi.z + qj.z ) * 0.5f };

                float yaw = 0.0f;
                Vector3 A2 = RM_Scale(A1, -1.0f), B2 = RM_Scale(B1, -1.0f);
                if ( !crossM || !MatchYaw({ { 1,0,0 }, { -1,0,0 }, { 0,0,1 }, { 0,0,-1 } },
                                          { A1, A2, B1, B2 }, yaw) ) continue;

                PlacePiece(crossM, i, rails, pos, yaw, 0.0f, camera);
                addCut(i, sC - kArm, sC + kArm);
                addCut(j, cdC - kArm, cdC + kArm);
                lastPlaced = sC;
            }
        }
    }
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

    // 交差点の検出＋ピース配置。各レールの「掃引しない区間」も決まる
    std::vector<std::vector<Cut>> cuts(rails.size());
    CollectJunctions(rails, camera, cuts);

    for ( int railIdx = 0; railIdx < ( int ) rails.size(); ++railIdx ) {
        const SplineRail& rail = rails[railIdx];
        float len = rail.GetLength();
        if ( len <= 0.0f || rail.nodes.size() < 2 ) continue;
        if ( !rail.visible ) continue; // 連結用の見えないレールには道を敷かない

        // 掃引メッシュ本体（交差点の切り詰め範囲は張らない）
        BuildRailMesh(rail, railIdx, camera, atlasSrv, cuts[railIdx]);

        // 終端キャップ：非ループの「自由端」（未接続かつ交差点に食われていない端）だけに置く。
        //   溶接済みの端は隣の道と断面同士で突き合うのでキャップ不要（付けると出っ張る）
        if ( !rail.isLoop && endModel ) {
            auto endInCut = [&](float s) -> bool {
                for ( const Cut& c : cuts[railIdx] ) {
                    if ( s >= c.s0 - 0.01f && s <= c.s1 + 0.01f ) return true;
                }
                return false;
            };
            const float kProbe = ( std::min )( 1.0f, len * 0.5f );
            if ( rail.backConnIndex < 0 && !endInCut(len) ) {
                Vector3 e1  = rail.GetPositionByDistance(len);
                Vector3 e1b = rail.GetPositionByDistance(len - kProbe);
                PlaceEndCap(endModel, rails, railIdx, e1,
                    { e1.x * 2.0f - e1b.x, e1.y * 2.0f - e1b.y, e1.z * 2.0f - e1b.z }, camera);
            }
            if ( rail.frontConnIndex < 0 && !endInCut(0.0f) ) {
                Vector3 e0  = rail.GetPositionByDistance(0.0f);
                Vector3 e0b = rail.GetPositionByDistance(kProbe);
                PlaceEndCap(endModel, rails, railIdx, e0,
                    { e0.x * 2.0f - e0b.x, e0.y * 2.0f - e0b.y, e0.z * 2.0f - e0b.z }, camera);
            }
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
