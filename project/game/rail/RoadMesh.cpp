#include "game/rail/RoadMesh.h"

#include "engine/rail/SplineRail.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/graphics/TextureManager.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/utils/EditorManager.h"
#include "engine/math/VectorMath.h"

#include <cmath>
#include <algorithm>
#include <array>

using namespace VectorMath;

namespace {

// --- ベクトル補助 ---
//   汎用の加減算/内積/長さ/正規化は VectorMath に一本化した。ここに残すのは
//   道生成に固有の「水平射影」「左方向」「小数部」の3つだけ
// 水平（XZ）へ射影して正規化。ほぼ真上/真下なら false
inline bool ToHorizontal(const Vector3& v, Vector3& out){
    float l = std::sqrt(v.x * v.x + v.z * v.z);
    if ( l < 0.35f ) return false; // 急勾配すぎてジャンクションの水平方向が定まらない
    out = { v.x / l, 0.0f, v.z / l };
    return true;
}
// 水平方向 d の「左」（角度ソートのCCWと整合する側）
inline Vector3 LeftOf(const Vector3& d){ return { -d.z, 0.0f, d.x }; }
inline float   Fract(float x){ return x - std::floor(x); }

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
const float kTopOffset  = -0.25f;                           // 断面高さへの加算。上面(0.25)がレール線ぴったりに来る
                                                            // （レール線＝歩行面＝ブロックの底面基準。ズレ補正が要らなくなる）
const float kUvPerMeter = 1.0f / 2.0f;                      // u = 距離 / 2m（ピースとテクスチャ周期を共有）
const float kSimpleStep = 1.0f;                             // 簡易ビルド（ドラッグ中）のリング間隔(m)
const float kDarkV      = 0.465f;                           // アトラスの黒帯の中央（穴の奈落フタ用）

// 切り口フタ用：断面の外周をなす6頂点（プロファイル番号。凸六角形）
const int kOutline[6] = { 0, 1, 3, 5, 7, 9 };

// --- ジャンクション（GUIDE_ジャンクション生成）---
const float kHalfWidth   = 1.00f;                 // 道の半幅 W
const float kInnerWidth  = 0.88f;                 // ベベル内側 W-B
const float kTopH        = 0.25f;                 // 上面の断面高さ
const float kBevelH      = 0.15f;                 // ベベル下端の断面高さ
const float kMiterMargin = 0.15f;                 // マイター交点から入口までの余白
const float kTCutMin     = 0.35f;                 // t_cut の最低値
const float kTCutMax     = 2.0f;                  // t_cut の絶対上限。角度が175°に近づくとマイター交点距離が
                                                  // 数学的に爆発する（t=W/tan((180°-φ)/2)）ため、巨大パッチが
                                                  // 道の下や横にはみ出すのを物理的に止める
const float kMiterMaxDeg = 165.0f;                // これ未満はマイター（165°〜195°は「ほぼ直進」でパッチ不要）
const float kArcMinDeg   = 195.0f;                // これ超は外周円弧
const float kArcStepDeg  = 12.0f;                 // 円弧の刻み
const float kGentleDot   = -0.866f;               // 2本溶接: dot<=これ(150°以上) → 掃引コネクタ担当
// （旧 kStraightDot は廃止：ほぼ一直線でも折れ目に隙間が出るためコネクタを常に掃引する）
const float kArm         = 1.0f;                  // 掃引コネクタが両側を削る長さ

// マイター交点：N + t*di + w*Li = N + s*dj - w*Lj を XZ で解く（t, s を返す）
bool SolveMiter(const Vector3& di, const Vector3& dj, float w, float& t, float& s){
    Vector3 Li = LeftOf(di), Lj = LeftOf(dj);
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
void RoadMesh::EmitMesh(const Model::ModelData& data, int followRail, Camera* camera, uint32_t atlasSrv,
                        bool isJoint){
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
        slots_.push_back(std::move(slot));
    }
    MeshSlot& s = *slots_[slotsUsed_++];
    s.model->UpdateMesh(data.vertices, data.indices);
    s.rail = followRail;
    s.isJoint = isJoint;
    lastVertexCount_   += data.vertices.size();
    lastTriangleCount_ += data.indices.size() / 3;
    // カリング無し（既定）：巻き順に依存せず、下から見上げても道が消えない。
    // 背面カリング（オプション）：オーバードロー削減。UIから即時切替できる
    s.obj->SetPipelineType(cullNone_ ? PipelineType::Object3D_CullNone : PipelineType::Object3D);
    s.obj->SetCamera(camera);
    s.obj->SetTranslation({ 0.0f, 0.0f, 0.0f });
    s.obj->Update();
}

// 背面カリングの切替（既存スロットにも即時反映）
void RoadMesh::SetCullNone(bool cullNone){
    if ( cullNone_ == cullNone ) return;
    cullNone_ = cullNone;
    for ( auto& slot : slots_ ) {
        slot->obj->SetPipelineType(cullNone_ ? PipelineType::Object3D_CullNone : PipelineType::Object3D);
    }
}

// レール1本ぶんの掃引メッシュを生成する
void RoadMesh::BuildRailMesh(const SplineRail& rail, int railIdx, Camera* camera, uint32_t atlasSrv,
                             const std::vector<Cut>& cuts, bool simple, bool capFront, bool capBack){
    const float len = rail.GetLength();

    const std::vector<SplineRail::HoleInterval> holes =
        simple ? std::vector<SplineRail::HoleInterval>{} : rail.GetHoleIntervals();

    // 距離 s が危険帯（穴の手前後 warnLength_）か
    auto dangerAt = [&](float s) -> bool{
        for ( const auto& h : holes ) {
            if ( ( s >= h.d0 - warnLength_ && s <= h.d0 + 1e-4f ) ||
                 ( s >= h.d1 - 1e-4f && s <= h.d1 + warnLength_ ) ) return true;
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
            float c = std::clamp(Dot(prevTan, f.tangent), -1.0f, 1.0f);
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
            float w0 = h.d0 - warnLength_, w1 = h.d1 + warnLength_;
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
    std::vector<std::array<Vector3, 12>> ringPos(rings.size()); // 奈落フタ生成用に溶接後の位置を保存

    Vector3 prevPos[12] {};
    bool hasPrev = false;
    for ( size_t ri = 0; ri < rings.size(); ++ri ) {
        const Ring& ring = rings[ri];
        SplineRail::RailFrame f = rail.GetFrameAtDistance(ring.s);
        for ( int k = 0; k < 12; ++k ) {
            const ProfileV& pv = kProfile[k];
            Vector3 pos = Add(f.position,
                Add(Multiply(f.right, pv.lat), Multiply(f.up, pv.h + kTopOffset)));

            // 内側折返しの溶接：急カーブの内側で「前のリングより後ろ」に来たら前の位置に留める
            if ( !simple && hasPrev &&
                 Dot({ pos.x - prevPos[k].x, pos.y - prevPos[k].y, pos.z - prevPos[k].z },
                        f.tangent) < 0.0f ) {
                pos = prevPos[k];
            }
            prevPos[k] = pos;
            ringPos[ri][k] = pos;

            float v = pv.v;
            if ( k == 4 || k == 5 ) {
                if ( ring.danger )      { v = kDangerV[k - 4]; } // 危険帯（穴の手前後・上面のみ）
                else if ( ring.slope )  { v = kSlopeV[k - 4]; }  // 坂帯
            }

            Model::VertexData vert {};
            vert.position = { pos.x, pos.y, pos.z, 1.0f };
            vert.normal = Normalize(Add(Multiply(f.right, pv.nr), Multiply(f.up, pv.nu)));
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

    // --- 5. 穴の切り口に「奈落フタ」を張る（アトラスの黒帯＝暗色）---
    //   以前は road_end の丸キャップで塞いでいたが、緑の丸い端が「地面が続いている」ように
    //   見えて穴と認識できなかった。切り口を暗くすることで「落ちる」と直感できるようにする
    auto emitDarkCap = [&](size_t ringIdx, float facing){
        SplineRail::RailFrame f = rail.GetFrameAtDistance(rings[ringIdx].s);
        Vector3 nrm = Multiply(f.tangent, facing);
        uint32_t base = static_cast<uint32_t>( data.vertices.size() );
        for ( int k = 0; k < 6; ++k ) {
            const ProfileV& pv = kProfile[kOutline[k]];
            Model::VertexData vert {};
            const Vector3& pos = ringPos[ringIdx][kOutline[k]];
            vert.position = { pos.x, pos.y, pos.z, 1.0f };
            vert.normal = nrm;
            vert.texcoord = { 0.10f + ( pv.lat + 1.0f ) * 0.15f, kDarkV }; // v固定＝黒帯（奈落色）
            data.vertices.push_back(vert);
        }
        for ( uint32_t t = 1; t <= 4; ++t ) {
            data.indices.push_back(base);
            data.indices.push_back(base + t);
            data.indices.push_back(base + t + 1);
        }
    };
    if ( !simple ) {
        // 穴区間の始まり/終わりのリング（Degenを透過して隣がDrawのところ）にフタ
        for ( size_t i = 0; i < segs.size(); ++i ) {
            if ( segs[i] != Seg::Hole ) continue;
            size_t prev = i;
            while ( prev > 0 && segs[prev - 1] == Seg::Degen ) { --prev; }
            if ( prev > 0 && segs[prev - 1] == Seg::Draw ) { emitDarkCap(i, +1.0f); }
            size_t next = i;
            while ( next + 1 < segs.size() && segs[next + 1] == Seg::Degen ) { ++next; }
            if ( next + 1 < segs.size() ? segs[next + 1] == Seg::Draw : false ) { emitDarkCap(i + 1, -1.0f); }
        }
        // レールの自由端（未接続の端）にも同じ平らなフタ。
        //   端の区間が穴/ジャンクション(Cut)なら面が無いのでフタも不要（自動スキップ）
        if ( capFront && !segs.empty() && segs.front() == Seg::Draw ) {
            emitDarkCap(0, -1.0f); // 始端：外向き＝-tangent
        }
        if ( capBack && !segs.empty() && segs.back() == Seg::Draw ) {
            emitDarkCap(rings.size() - 1, +1.0f); // 終端：外向き＝+tangent
        }
    }

    if ( data.vertices.empty() || data.indices.empty() ) return;
    EmitMesh(data, railIdx, camera, atlasSrv);
}

// ピースモデルの頂点を 回転(pitch→yaw)＋平行移動 してベイク先へ焼き込む。
//   全ピースがアトラス共有なので、1つの動的メッシュにまとめれば合計1ドローコールで済む。
//   回転は Obj3d の MakeAffine（行ベクトル・Rx→Ry 順）と同じ結果になるよう展開している
void RoadMesh::AppendPieceBake(Model* model, const Vector3& pos, float yaw, float pitch,
                               Model::ModelData& out) const{
    if ( !model ) return;
    const Model::ModelData& src = model->GetModelData();
    const float cy = std::cos(yaw),  sy = std::sin(yaw);
    const float cx = std::cos(pitch), sx = std::sin(pitch);
    auto rotate = [&](const Vector3& v) -> Vector3{
        // Rx(pitch): (x, y·c - z·s, y·s + z·c) → Ry(yaw): (x·c + z·s, y, -x·s + z·c)
        Vector3 a = { v.x, v.y * cx - v.z * sx, v.y * sx + v.z * cx };
        return { a.x * cy + a.z * sy, a.y, -a.x * sy + a.z * cy };
    };
    uint32_t base = static_cast<uint32_t>( out.vertices.size() );
    for ( const auto& sv : src.vertices ) {
        Model::VertexData v = sv;
        Vector3 p = rotate({ sv.position.x, sv.position.y, sv.position.z });
        v.position = { p.x + pos.x, p.y + pos.y, p.z + pos.z, 1.0f };
        v.normal = rotate(sv.normal);
        out.vertices.push_back(v);
    }
    for ( uint32_t idx : src.indices ) { out.indices.push_back(base + idx); }
}

// ピース（road_end / road_joint）を1個置く。
//   静的レール → ベイク先メッシュへ焼き込み（DC削減。動かないので焼いて問題ない）
//   動くレール → Obj3d プールを使い回し（animOffset 追従が必要なため個別のまま）
void RoadMesh::PlacePiece(Model* model, std::vector<std::unique_ptr<PieceSlot>>& pool, size_t& used,
                          int railIdx, const std::vector<SplineRail>& rails,
                          const Vector3& pos, float yaw, float pitch, Camera* camera){
    if ( !model ) return;
    Vector3 p = { pos.x, pos.y + kTopOffset, pos.z }; // 上面(0.25)がレール線ぴったりに来る高さ

    const bool moving = ( railIdx >= 0 && railIdx < ( int ) rails.size() && rails[railIdx].HasMotion() );
    if ( !moving ) {
        // 静的：まとめメッシュへベイク（ジョイントは表示切替があるので別メッシュ）
        AppendPieceBake(model, p, yaw, pitch, ( &pool == &joints_ ) ? bakeJoints_ : bakeCaps_);
        return;
    }

    if ( used == pool.size() ) {
        auto slot = std::make_unique<PieceSlot>();
        slot->obj = std::make_unique<Obj3d>();
        slot->obj->Initialize(model);
        pool.push_back(std::move(slot));
    }
    PieceSlot& s = *pool[used++];
    s.obj->SetModel(model);
    s.obj->SetCamera(camera);
    s.obj->SetScale({ 1.0f, 1.0f, 1.0f });
    s.obj->SetTranslation(p);
    s.obj->SetRotation({ pitch, yaw, 0.0f });
    s.obj->Update();
    s.rail = railIdx;
    const Vector3& off = rails[railIdx].animOffset;
    s.base = { p.x - off.x, p.y - off.y, p.z - off.z };
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
        Vector3 bi = Add(junc.arms[0].dir, junc.arms[1].dir);
        if ( Length(bi) < 0.1f ) { bi = LeftOf(junc.arms[0].dir); } // ほぼ一直線→横向き
        bi = Normalize(bi);
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
        // レールが短い場合は入り込みすぎない（反対側の端やノードを越えない）＋
        // 絶対上限 kTCutMax で巨大パッチを禁止（ゆるい角度でのマイター爆発対策）
        float avail = arm.forward ? ( len - arm.nodeS ) : arm.nodeS;
        float upper = ( std::min )( kTCutMax, ( std::max )( kTCutMin, avail * 0.45f ) );
        arm.tCut = std::clamp(tcut[i], kTCutMin, upper);
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
    if ( n < 1 ) return; // n==1 は「端の丸広場」（1本腕＝入口1つ＋360°の円弧ウェッジ）
    const Vector3& N = junc.center;

    // 各腕の入口フレームと「左側の符号」（フレームrightが世界の左とどちら向きで一致するか）
    struct ArmGeo { SplineRail::RailFrame frame; float leftSign; };
    std::vector<ArmGeo> geo(n);
    for ( int i = 0; i < n; ++i ) {
        const Arm& arm = junc.arms[i];
        geo[i].frame = rails[arm.rail].GetFrameAtDistance(arm.cutS);
        geo[i].leftSign = ( Dot(geo[i].frame.right, LeftOf(arm.dir)) >= 0.0f ) ? 1.0f : -1.0f;
    }

    // 腕 i の入口断面上の点（lat = 左方向を+とした符号付き幅、h = 断面高さ）
    //   入口はレールの実フレームから取る → 掃引側の入口リングと完全一致（継ぎ目ゼロ）
    auto entryPoint = [&](int i, float lat, float h) -> Vector3{
        const ArmGeo& g = geo[i];
        return Add(g.frame.position,
                      Add(Multiply(g.frame.right, g.leftSign * lat),
                             Multiply(g.frame.up, h + kTopOffset)));
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
                Vector3 C = Add(N, Add(Multiply(junc.arms[i].dir, t),
                                             Multiply(LeftOf(junc.arms[i].dir), w)));
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
        v = 0.89f + Fract(( p.z - N.z ) * kUvPerMeter) * 0.09f;
    };
    auto bottomUV = [&](const Vector3& p, float& u, float& v){
        u = ( p.x - N.x ) * kUvPerMeter;
        v = 0.4727f + Fract(( p.z - N.z ) * kUvPerMeter) * ( 0.5273f - 0.4727f );
    };
    // 点pの「外向き水平方向」（ノード中心から離れる向き）
    auto outward = [&](const Vector3& p) -> Vector3{
        Vector3 o = { p.x - N.x, 0.0f, p.z - N.z };
        float l = Length(o);
        if ( l < 1e-4f ) return { 1.0f, 0.0f, 0.0f };
        return Multiply(o, 1.0f / l);
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
        // エディタの「曲がり角の形」設定で上書き：1=いつも丸広場（ヨッシー風）/ 2=丸なし
        //   （165°〜195°の「ほぼ直進」だけは、丸を強制すると逆に不自然なので自動のまま）
        if ( cornerStyle_ == 1 && wedgeMode[i] == 0 ) { wedgeMode[i] = 2; }
        if ( cornerStyle_ == 2 && wedgeMode[i] == 2 ) { wedgeMode[i] = 0; }
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
            accum += Length(Subtract(pout[m], pout[m - 1]));
            us[m] = accum * kUvPerMeter;
        }

        for ( size_t m = 0; m + 1 < cnt; ++m ) {
            // ベベル（上0.7422 / 下0.7070。法線= 外向き0.64 + 上0.77）
            Vector3 n0 = Normalize(Add(Multiply(outward(pout[m]), 0.64f), Vector3 { 0.0f, 0.77f, 0.0f }));
            Vector3 n1 = Normalize(Add(Multiply(outward(pout[m + 1]), 0.64f), Vector3 { 0.0f, 0.77f, 0.0f }));
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

// 端の丸広場：レールの始点/終点に円形の広場（上面扇+ベベル+壁+底）を敷く。
//   エディタの「始点/終点に丸広場」チェックで指定（endPlaza bit0=始点/bit1=終点）。
//   道は広場の中心まで走り込むので、道の端の断面は広場の内部に隠れて継ぎ目が出ない
void RoadMesh::BuildEndPlaza(const std::vector<SplineRail>& rails, int railIdx, bool front,
                             Camera* camera, uint32_t atlasSrv){
    const SplineRail& rail = rails[railIdx];
    float endS = front ? 0.0f : rail.GetLength();
    Vector3 N = rail.GetPositionByDistance(endS);

    const float kPlazaR     = kHalfWidth * 1.8f;                      // 広場の外周半径
    const float kPlazaInner = kPlazaR - ( kHalfWidth - kInnerWidth ); // ベベル幅は道と同じ
    const float kPlazaTopH  = kTopH - 0.004f; // 道が広場に乗り上げる重なり部分でチラつかないよう僅かに低く
    const int   kSteps      = 28;
    const float kTwoPi      = 2.0f * 3.14159265f;

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
    auto topUV = [&](const Vector3& p, float& u, float& v){
        u = ( p.x - N.x ) * kUvPerMeter;
        v = 0.89f + Fract(( p.z - N.z ) * kUvPerMeter) * 0.09f;
    };
    auto bottomUV = [&](const Vector3& p, float& u, float& v){
        u = ( p.x - N.x ) * kUvPerMeter;
        v = 0.4727f + Fract(( p.z - N.z ) * kUvPerMeter) * ( 0.5273f - 0.4727f );
    };
    auto ringPoint = [&](int k, float radius, float h) -> Vector3{
        float ang = kTwoPi * static_cast<float>( k ) / kSteps;
        return { N.x + std::cos(ang) * radius, N.y + h + kTopOffset, N.z + std::sin(ang) * radius };
    };
    auto ringOutward = [&](int k) -> Vector3{
        float ang = kTwoPi * static_cast<float>( k ) / kSteps;
        return { std::cos(ang), 0.0f, std::sin(ang) };
    };

    // 上面：中心から扇（ジャンクションパッチの上面と同じ張り方/UV）
    {
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        Vector3 centerP = { N.x, N.y + kPlazaTopH + kTopOffset, N.z };
        float cu, cv; topUV(centerP, cu, cv);
        uint32_t c = pushV(centerP, up, cu, cv);
        std::vector<uint32_t> ids(kSteps);
        for ( int k = 0; k < kSteps; ++k ) {
            Vector3 p = ringPoint(k, kPlazaInner, kPlazaTopH);
            float u, v; topUV(p, u, v);
            ids[k] = pushV(p, up, u, v);
        }
        for ( int k = 0; k < kSteps; ++k ) { pushTri(c, ids[k], ids[( k + 1 ) % kSteps]); }
    }

    // ベベル＋壁（一周。v値/法線はジャンクションパッチと同じ規約）
    {
        float segLen = kTwoPi * kPlazaR / kSteps;
        for ( int k = 0; k < kSteps; ++k ) {
            int k1 = ( k + 1 ) % kSteps;
            float u0 = segLen * k * kUvPerMeter;
            float u1 = segLen * ( k + 1 ) * kUvPerMeter;
            Vector3 n0 = Normalize(Add(Multiply(ringOutward(k),  0.64f), Vector3 { 0.0f, 0.77f, 0.0f }));
            Vector3 n1 = Normalize(Add(Multiply(ringOutward(k1), 0.64f), Vector3 { 0.0f, 0.77f, 0.0f }));
            uint32_t i0 = pushV(ringPoint(k,  kPlazaInner, kPlazaTopH), n0, u0, 0.7422f);
            uint32_t i1 = pushV(ringPoint(k1, kPlazaInner, kPlazaTopH), n1, u1, 0.7422f);
            uint32_t o0 = pushV(ringPoint(k,  kPlazaR, kBevelH),   n0, u0, 0.7070f);
            uint32_t o1 = pushV(ringPoint(k1, kPlazaR, kBevelH),   n1, u1, 0.7070f);
            pushTri(i0, o0, o1); pushTri(i0, o1, i1);

            Vector3 w0 = ringOutward(k);
            Vector3 w1 = ringOutward(k1);
            uint32_t t0 = pushV(ringPoint(k,  kPlazaR, kBevelH), w0, u0, 0.6992f);
            uint32_t t1 = pushV(ringPoint(k1, kPlazaR, kBevelH), w1, u1, 0.6992f);
            uint32_t b0 = pushV(ringPoint(k,  kPlazaR, 0.0f),    w0, u0, 0.5352f);
            uint32_t b1 = pushV(ringPoint(k1, kPlazaR, 0.0f),    w1, u1, 0.5352f);
            pushTri(t0, b0, b1); pushTri(t0, b1, t1);
        }
    }

    // 底面：中心から扇（裏向き）
    {
        Vector3 dn = { 0.0f, -1.0f, 0.0f };
        Vector3 centerP = { N.x, N.y + kTopOffset, N.z };
        float cu, cv; bottomUV(centerP, cu, cv);
        uint32_t c = pushV(centerP, dn, cu, cv);
        std::vector<uint32_t> ids(kSteps);
        for ( int k = 0; k < kSteps; ++k ) {
            Vector3 p = ringPoint(k, kPlazaR, 0.0f);
            float u, v; bottomUV(p, u, v);
            ids[k] = pushV(p, dn, u, v);
        }
        for ( int k = 0; k < kSteps; ++k ) { pushTri(c, ids[( k + 1 ) % kSteps], ids[k]); }
    }

    EmitMesh(data, rail.HasMotion() ? railIdx : -1, camera, atlasSrv);
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

    // --- 端の丸広場（エディタ指定）：「腕1本のジャンクション」としてパッチ生成する ---
    //   入口リングはレールの実断面から取られるので継ぎ目ゼロで道と一体化し、
    //   外周は360°の円弧ウェッジ＝ヨッシー風の丸い広場になる。道はカットして重ねない
    if ( ( int ) cuts.size() < n ) { cuts.resize(n); }
    {
        const float kPlazaTCut = 1.1f; // 端から入口までの距離（広場の大きさ）
        for ( int r = 0; r < n; ++r ) {
            if ( !roadUsable(r) || rails[r].isLoop ) continue; // ループは端が無い
            float len = rails[r].GetLength();
            for ( int side = 0; side < 2; ++side ) {
                bool atFront = ( side == 0 );
                if ( !( rails[r].endPlaza & ( atFront ? 1 : 2 ) ) ) continue;
                float nodeS = atFront ? 0.0f : len;
                Vector3 dirRaw = atFront ? rails[r].GetTangentByDistance(0.0f)
                                         : Multiply(rails[r].GetTangentByDistance(len), -1.0f);
                Vector3 dir;
                if ( !ToHorizontal(dirRaw, dir) ) continue;

                Junction junc;
                junc.center = rails[r].GetPositionByDistance(nodeS);
                junc.followRail = r;
                float tCut = std::clamp(kPlazaTCut, kTCutMin, ( std::max )( kTCutMin, len * 0.45f ));
                float cutS = atFront ? tCut : len - tCut;
                junc.arms.push_back({ r, nodeS, cutS, tCut, atFront, dir });
                cuts[r].push_back({ ( std::min )( nodeS, cutS ), ( std::max )( nodeS, cutS ) });
                BuildJunctionPatch(rails, junc, camera, atlasSrv);
            }
        }
    }

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
                                    : Multiply(a.GetTangentByDistance(a.GetLength()), -1.0f);
            Vector3 bDirRaw = bFront ? b.GetTangentByDistance(0.0f)
                                     : Multiply(b.GetTangentByDistance(b.GetLength()), -1.0f);
            Vector3 aDir, bDir;
            if ( !ToHorizontal(aDirRaw, aDir) || !ToHorizontal(bDirRaw, bDir) ) continue;

            float d = Dot(aDir, bDir);
            // ※以前は「ほぼ一直線（±5°）は突き合わせのまま」でスキップしていたが、
            //   判定が水平のみのため平地→スロープの折れ目もスキップされ、断面の傾きが
            //   違う2本の間に隙間/段差が見えていた。緩い継ぎ目も必ずコネクタで滑らかに繋ぐ

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
                                   : Multiply(rails[j].GetTangentByDistance(rails[j].GetLength()), -1.0f);
            Vector3 m1, br;
            if ( !ToHorizontal(m1raw, m1) || !ToHorizontal(brRaw, br) ) continue;
            if ( std::abs(Dot(m1, br)) > 0.985f ) continue; // 支線が本線とほぼ平行→パッチが潰れる

            Junction junc;
            junc.center = rails[i].GetPositionByDistance(bp.distance);
            junc.followRail = i;
            junc.arms.push_back({ i, bp.distance, 0.0f, kTCutMin, true,  m1 });
            junc.arms.push_back({ i, bp.distance, 0.0f, kTCutMin, false, Multiply(m1, -1.0f) });
            junc.arms.push_back({ j, tFront ? 0.0f : rails[j].GetLength(), 0.0f, kTCutMin, tFront, br });
            juncs.push_back(junc);
        }
    }

    // --- C) 本体×本体の交差（乗り換えポイント）：4方向パッチ ---
    //   総当たり（レール対 × 0.5m歩き × 最近点計算）は本数×総延長で急激に重くなるため、
    //   レール毎のAABB（交差しきい値ぶん膨らませたもの）で二段階に間引く：
    //     1. AABB同士が重ならないレール対は丸ごとスキップ
    //     2. 歩いている点が相手のAABBの外なら最近点計算をスキップ
    struct RailBox { Vector3 mn, mx; bool valid = false; };
    std::vector<RailBox> boxes(n);
    for ( int i = 0; i < n; ++i ) {
        if ( !roadUsable(i) || rails[i].HasMotion() ) continue;
        RailBox& b = boxes[i];
        b.mn = { 1e30f, 1e30f, 1e30f };
        b.mx = { -1e30f, -1e30f, -1e30f };
        float len = rails[i].GetLength();
        for ( float s = 0.0f; ; s += 1.0f ) {
            Vector3 p = rails[i].GetPositionByDistance(( std::min )( s, len ));
            b.mn.x = ( std::min )( b.mn.x, p.x ); b.mx.x = ( std::max )( b.mx.x, p.x );
            b.mn.y = ( std::min )( b.mn.y, p.y ); b.mx.y = ( std::max )( b.mx.y, p.y );
            b.mn.z = ( std::min )( b.mn.z, p.z ); b.mx.z = ( std::max )( b.mx.z, p.z );
            if ( s >= len ) break;
        }
        const float kInflate = 1.0f; // 交差しきい値0.9m＋余白
        b.mn.x -= kInflate; b.mn.y -= kInflate; b.mn.z -= kInflate;
        b.mx.x += kInflate; b.mx.y += kInflate; b.mx.z += kInflate;
        b.valid = true;
    }
    auto boxOverlap = [](const RailBox& a, const RailBox& b) -> bool{
        return a.mn.x <= b.mx.x && a.mx.x >= b.mn.x &&
               a.mn.y <= b.mx.y && a.mx.y >= b.mn.y &&
               a.mn.z <= b.mx.z && a.mx.z >= b.mn.z;
    };
    auto inBox = [](const RailBox& b, const Vector3& p) -> bool{
        return p.x >= b.mn.x && p.x <= b.mx.x &&
               p.y >= b.mn.y && p.y <= b.mx.y &&
               p.z >= b.mn.z && p.z <= b.mx.z;
    };

    for ( int i = 0; i < n; ++i ) {
        if ( !roadUsable(i) ) continue;
        for ( int j = i + 1; j < n; ++j ) {
            if ( !roadUsable(j) ) continue;
            if ( rails[i].HasMotion() || rails[j].HasMotion() ) continue; // 動くレール同士の交差は追従できない
            if ( !boxes[i].valid || !boxes[j].valid ) continue;
            if ( !boxOverlap(boxes[i], boxes[j]) ) continue; // 遠いレール対は歩かずスキップ

            float lenI = rails[i].GetLength(), lenJ = rails[j].GetLength();
            float lastPlaced = -1e9f;
            for ( float s = 0.0f; s <= lenI; s += 0.5f ) {
                Vector3 p = rails[i].GetPositionByDistance(s);
                if ( !inBox(boxes[j], p) ) continue; // 相手の範囲外なら最近点計算もしない
                float cd = rails[j].GetClosestDistance(p);
                Vector3 q = rails[j].GetPositionByDistance(cd);
                if ( Length(Subtract(q, p)) > 0.9f ) continue;

                // 端の近くは溶接/分岐の領分（二重配置を防ぐ）
                if ( s < 1.5f || s > lenI - 1.5f || cd < 1.5f || cd > lenJ - 1.5f ) continue;
                if ( s - lastPlaced < 2.0f ) continue; // 同じ交差のクラスタ

                // 交差中心を細かく詰める（±0.6mを0.1刻みで最短距離の点へ）
                float bestS = s, bestD = 1e9f;
                for ( float t = s - 0.6f; t <= s + 0.6f; t += 0.1f ) {
                    if ( t < 0.0f || t > lenI ) continue;
                    Vector3 pp = rails[i].GetPositionByDistance(t);
                    float ccd = rails[j].GetClosestDistance(pp);
                    float dd = Length(Subtract(rails[j].GetPositionByDistance(ccd), pp));
                    if ( dd < bestD ) { bestD = dd; bestS = t; }
                }
                float sC = bestS;
                float cdC = rails[j].GetClosestDistance(rails[i].GetPositionByDistance(sC));

                Vector3 A1, B1;
                if ( !ToHorizontal(rails[i].GetTangentByDistance(sC), A1) ) continue;
                if ( !ToHorizontal(rails[j].GetTangentByDistance(cdC), B1) ) continue;
                if ( std::abs(Dot(A1, B1)) > 0.95f ) continue; // ほぼ平行＝交差にならない

                Vector3 pi = rails[i].GetPositionByDistance(sC);
                Vector3 qj = rails[j].GetPositionByDistance(cdC);

                Junction junc;
                junc.center = { ( pi.x + qj.x ) * 0.5f, ( pi.y + qj.y ) * 0.5f, ( pi.z + qj.z ) * 0.5f };
                junc.followRail = i;
                junc.arms.push_back({ i, sC,  0.0f, kTCutMin, true,  A1 });
                junc.arms.push_back({ i, sC,  0.0f, kTCutMin, false, Multiply(A1, -1.0f) });
                junc.arms.push_back({ j, cdC, 0.0f, kTCutMin, true,  B1 });
                junc.arms.push_back({ j, cdC, 0.0f, kTCutMin, false, Multiply(B1, -1.0f) });
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
    lastVertexCount_ = 0;
    lastTriangleCount_ = 0;
    // 静的ピースのまとめメッシュ（容量は使い回す＝clearのみでヒープ再確保しない）
    bakeCaps_.vertices.clear();  bakeCaps_.indices.clear();
    bakeJoints_.vertices.clear(); bakeJoints_.indices.clear();

    // アトラス（GamePlayScene::LoadResources で先読み済み＝ここではキャッシュが返るだけ）
    uint32_t atlasSrv = TextureManager::GetInstance()->Load("resources/road/road_atlas.png").srvIndex;

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

        // 自由端（未接続 or 相手が道なしレール）はフタが必要。
        //   ※丸い road_end モデルの配置は廃止：端だけ形が変わって違和感が出るため、
        //     穴の切り口と同じ「平らな暗色フタ」で統一する（BuildRailMesh 内で生成。
        //     端がジャンクションや穴に食われている場合は自動でスキップされる）
        bool capFront = false, capBack = false;
        if ( !simple && !rail.isLoop ) {
            capFront = ( rail.frontConnIndex < 0 || !roadUsable(rail.frontConnIndex) );
            capBack  = ( rail.backConnIndex < 0  || !roadUsable(rail.backConnIndex) );
        }

        // 掃引メッシュ本体（ジャンクションの切り詰め範囲は張らない）
        BuildRailMesh(rail, railIdx, camera, atlasSrv, cuts[railIdx], simple, capFront, capBack);
    }

    // 静的レールのピース（キャップ/ジョイント）をそれぞれ1メッシュにまとめて出す（DC削減）
    if ( !simple ) {
        EmitMesh(bakeCaps_,   -1, camera, atlasSrv, false);
        EmitMesh(bakeJoints_, -1, camera, atlasSrv, true);
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
    // 「後から出現する道」はプレイ中だけ隠す/せり上げる（エディタでは普通に見せて編集できるように）
    const bool playMode = ( EditorManager::GetInstance()->GetMode() == EngineMode::Play );
    // レール r の出現状態 → 隠すか(true/false) と 追加の沈み込みY を返す
    auto appearOffset = [&](int r, bool& outHidden) -> float {
        outHidden = false;
        if ( !playMode || r < 0 || r >= ( int ) rails.size() ) return 0.0f;
        const SplineRail& rail = rails[r];
        if ( rail.appearTrigger < 0 ) return 0.0f;
        if ( rail.appearAnim <= 0.001f ) { outHidden = true; return 0.0f; }
        return -( 1.0f - rail.appearAnim ) * 2.5f; // 出現中：下からせり上がる
    };
    for ( size_t k = 0; k < slotsUsed_; ++k ) {
        MeshSlot& s = *slots_[k];
        if ( s.rail >= 0 && s.rail < ( int ) rails.size() ) {
            // 掃引メッシュはワールド座標で焼いてあるので、動くレールぶんだけ平行移動。
            // 列車式（animYaw）は「pivot中心の回転」＝回転＋(pivot - pivot*Ry) の平行移動で表す
            const SplineRail& followRail = rails[s.rail];
            Vector3 off = followRail.animOffset;
            off.y += appearOffset(s.rail, s.hiddenNow);
            if ( followRail.animYaw != 0.0f ) {
                float sinYaw = std::sin(followRail.animYaw), cosYaw = std::cos(followRail.animYaw);
                const Vector3& pivot = followRail.animPivot;
                Vector3 rotatedPivot { pivot.x * cosYaw + pivot.z * sinYaw, pivot.y,
                                       -pivot.x * sinYaw + pivot.z * cosYaw };
                s.obj->SetRotation({ 0.0f, followRail.animYaw, 0.0f });
                s.obj->SetTranslation({ pivot.x - rotatedPivot.x + off.x, off.y,
                                        pivot.z - rotatedPivot.z + off.z });
            } else {
                s.obj->SetRotation({ 0.0f, 0.0f, 0.0f });
                s.obj->SetTranslation(off);
            }
        } else {
            s.hiddenNow = false;
        }
        s.obj->Update();
    }
    auto updatePieces = [&](std::vector<std::unique_ptr<PieceSlot>>& pool, size_t used){
        for ( size_t k = 0; k < used; ++k ) {
            PieceSlot& s = *pool[k];
            if ( s.rail >= 0 && s.rail < ( int ) rails.size() ) {
                const SplineRail& followRail = rails[s.rail];
                Vector3 off = followRail.animOffset;
                off.y += appearOffset(s.rail, s.hiddenNow);
                Vector3 basePos = s.base;
                if ( followRail.animYaw != 0.0f ) {
                    // ピース（端キャップ等）は位置だけpivot中心で公転させる（自身の回転は据え置き）
                    float sinYaw = std::sin(followRail.animYaw), cosYaw = std::cos(followRail.animYaw);
                    const Vector3& pivot = followRail.animPivot;
                    float dx = basePos.x - pivot.x, dz = basePos.z - pivot.z;
                    basePos.x = pivot.x + dx * cosYaw + dz * sinYaw;
                    basePos.z = pivot.z - dx * sinYaw + dz * cosYaw;
                }
                s.obj->SetTranslation({ basePos.x + off.x, basePos.y + off.y, basePos.z + off.z });
            } else {
                s.hiddenNow = false;
            }
            s.obj->Update();
        }
    };
    updatePieces(pieces_, piecesUsed_);
    updatePieces(joints_, jointsUsed_);
}

void RoadMesh::Draw() const{
    if ( !visible_ ) return;

    // ジョイントは表示モードに従う（0=エディタのみ / 1=常に / 2=非表示）
    bool showJoints = ( jointVisible_ == 1 ) ||
                      ( jointVisible_ == 0 && EditorManager::GetInstance()->GetMode() == EngineMode::Edit );

    for ( size_t k = 0; k < slotsUsed_; ++k ) {
        if ( slots_[k]->isJoint && !showJoints ) continue; // ジョイントのまとめメッシュ
        if ( slots_[k]->hiddenNow ) continue;              // 出現前の道（プレイ中のみ）
        slots_[k]->obj->Draw();
    }
    for ( size_t k = 0; k < piecesUsed_; ++k ) {
        if ( pieces_[k]->hiddenNow ) continue;
        pieces_[k]->obj->Draw();
    }
    if ( showJoints ) {
        for ( size_t k = 0; k < jointsUsed_; ++k ) {
            if ( joints_[k]->hiddenNow ) continue;
            joints_[k]->obj->Draw();
        }
    }
}
