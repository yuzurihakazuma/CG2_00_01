#include "game/rail/RailField.h"

#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/camera/Camera.h"
#include "engine/utils/EditorManager.h"

#include <cmath>
#include <algorithm>

RailField::RailField() = default;
RailField::~RailField() = default; // unique_ptr<Obj3d> のため cpp 側で定義

// =====================================================================
//  レール間の接続情報をロード時に1回だけ計算する。
//   端点(front/back)同士が近いレールを「連結」とみなし、最も近い端点を選ぶ（交差点で誤接続しない）。
//   連結端点は中点へ"溶接"してノードを一致させる → 隙間があっても乗り継ぎでワープしない。
//   ※動くレール(HasMotion)は静的連結しない（位置が変わるため。動的ドッキングに任せる）。
// =====================================================================
namespace {
void BuildRailConnections(std::vector<SplineRail>& rails){
    for ( auto& r : rails ){
        r.frontConnIndex = -1;
        r.backConnIndex  = -1;
        r.branchPoints.clear();
    }

    auto endDist = [](const Vector3& a, const Vector3& b) -> float{
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
    auto midPoint = [](const Vector3& a, const Vector3& b) -> Vector3{
        return { ( a.x + b.x ) * 0.5f, ( a.y + b.y ) * 0.5f, ( a.z + b.z ) * 0.5f };
        };

    const float kConnThreshold = 0.7f; // エディタの吸着半径と同じ（吸着済みノードは動かない）

    // フェーズ0：ループ（円状レール）の検出。front と back が近ければ閉じたレール。
    for ( auto& r : rails ){
        r.isLoop = false;
        if ( r.nodes.size() >= 3 ){
            if ( endDist(r.nodes.front(), r.nodes.back()) < kConnThreshold ){
                Vector3 m = midPoint(r.nodes.front(), r.nodes.back());
                r.nodes.front() = m;
                r.nodes.back()  = m;
                r.isLoop = true;
                r.BuildDistanceTable();
            }
        }
    }

    // フェーズ1：最も近い端点同士で連結関係を決める（動くレールは除外）。
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( rails[i].isLoop ) continue;
        if ( rails[i].HasMotion() ) continue;
        if ( rails[i].nodes.size() < 2 ) continue;
        const Vector3& iFront = rails[i].nodes.front();
        const Vector3& iBack  = rails[i].nodes.back();

        float bestFront = kConnThreshold;
        float bestBack  = kConnThreshold;

        for ( int j = 0; j < ( int ) rails.size(); ++j ){
            if ( i == j ) continue;
            if ( rails[j].HasMotion() ) continue;
            if ( rails[j].nodes.size() < 2 ) continue;
            const Vector3& jFront = rails[j].nodes.front();
            const Vector3& jBack  = rails[j].nodes.back();

            float ff = endDist(iFront, jFront);
            float fb = endDist(iFront, jBack);
            if ( ff < bestFront ){ bestFront = ff; rails[i].frontConnIndex = j; rails[i].frontConnToFront = true; }
            if ( fb < bestFront ){ bestFront = fb; rails[i].frontConnIndex = j; rails[i].frontConnToFront = false; }

            float bf = endDist(iBack, jFront);
            float bb = endDist(iBack, jBack);
            if ( bf < bestBack ){ bestBack = bf; rails[i].backConnIndex = j; rails[i].backConnToFront = true; }
            if ( bb < bestBack ){ bestBack = bb; rails[i].backConnIndex = j; rails[i].backConnToFront = false; }
        }
    }

    // フェーズ2：連結端点を溶接（隙間を物理的に詰める）。
    //   【相互合意チェック】i→j と j→i が互いに選び合っている組だけ「中点」へ溶接する。
    //   片思い（jは別のレールkの方が近い）の場合、双方を別々の中点へ動かすと
    //   数cmの隙間/段差が残るバグがあった。片思い側は相手の"今の"端点位置へ
    //   ぴったりスナップさせることで、必ず一致させる。
    struct Ends { Vector3 f, b; bool valid; };
    std::vector<Ends> orig(rails.size());
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( rails[i].nodes.size() < 2 ){ orig[i].valid = false; continue; }
        orig[i].f = rails[i].nodes.front();
        orig[i].b = rails[i].nodes.back();
        orig[i].valid = true;
    }
    auto mid = [](const Vector3& a, const Vector3& b) -> Vector3{
        return { ( a.x + b.x ) * 0.5f, ( a.y + b.y ) * 0.5f, ( a.z + b.z ) * 0.5f };
        };
    // レール j の端(front/back)が「レール i の端 e」を選び返しているか
    auto mutualBack = [&](int i, bool iIsFront, int j, bool jEndIsFront) -> bool{
        int  jConn      = jEndIsFront ? rails[j].frontConnIndex   : rails[j].backConnIndex;
        bool jConnFront = jEndIsFront ? rails[j].frontConnToFront : rails[j].backConnToFront;
        return jConn == i && jConnFront == iIsFront;
        };

    bool moved = false;
    // パスA：相互ペアは中点へ（両者同じ点になる）
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( !orig[i].valid ) continue;
        if ( rails[i].frontConnIndex >= 0 ){
            int j = rails[i].frontConnIndex;
            if ( orig[j].valid && mutualBack(i, true, j, rails[i].frontConnToFront) ){
                Vector3 partner = rails[i].frontConnToFront ? orig[j].f : orig[j].b;
                rails[i].nodes.front() = mid(orig[i].f, partner);
                moved = true;
            }
        }
        if ( rails[i].backConnIndex >= 0 ){
            int j = rails[i].backConnIndex;
            if ( orig[j].valid && mutualBack(i, false, j, rails[i].backConnToFront) ){
                Vector3 partner = rails[i].backConnToFront ? orig[j].f : orig[j].b;
                rails[i].nodes.back() = mid(orig[i].b, partner);
                moved = true;
            }
        }
    }
    // パスB：片思いの端は、相手の「今の（溶接後の）」端点位置へぴったりスナップ
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( !orig[i].valid ) continue;
        if ( rails[i].frontConnIndex >= 0 ){
            int j = rails[i].frontConnIndex;
            if ( orig[j].valid && !mutualBack(i, true, j, rails[i].frontConnToFront) ){
                rails[i].nodes.front() = rails[i].frontConnToFront ? rails[j].nodes.front() : rails[j].nodes.back();
                moved = true;
            }
        }
        if ( rails[i].backConnIndex >= 0 ){
            int j = rails[i].backConnIndex;
            if ( orig[j].valid && !mutualBack(i, false, j, rails[i].backConnToFront) ){
                rails[i].nodes.back() = rails[i].backConnToFront ? rails[j].nodes.front() : rails[j].nodes.back();
                moved = true;
            }
        }
    }

    // フェーズ3：ノードを動かしたので距離テーブルを作り直す。
    if ( moved ){
        for ( auto& r : rails ){ r.BuildDistanceTable(); }
    }

    // フェーズ4：途中分岐（branchPoints）の構築。
    //   レール j の端点が、レール i の「途中（端から離れた本体）」に接している T字路 を検出し、
    //   レール i 側に分岐点として登録する。同タイプ同士のT字路でも乗り換えできるようになる。
    for ( int j = 0; j < ( int ) rails.size(); ++j ){
        const SplineRail& rj = rails[j];
        if ( rj.nodes.size() < 2 || rj.isLoop || rj.HasMotion() ) continue;

        struct EndInfo { Vector3 pos; bool isFront; int connIdx; };
        const EndInfo ends[2] = {
            { rj.nodes.front(), true,  rj.frontConnIndex },
            { rj.nodes.back(),  false, rj.backConnIndex  },
        };
        for ( const auto& e : ends ){
            if ( e.connIdx >= 0 ) continue; // 端点同士で連結済みなら分岐扱いにしない
            for ( int i = 0; i < ( int ) rails.size(); ++i ){
                if ( i == j ) continue;
                SplineRail& ri = rails[i];
                if ( ri.nodes.size() < 2 || ri.HasMotion() ) continue;
                float len = ri.GetLength();
                if ( len < 1.5f ) continue;

                float cd = ri.GetClosestDistance(e.pos);
                if ( cd < 0.6f || cd > len - 0.6f ) continue; // 端付近は端点連結の領分
                Vector3 cp = ri.GetPositionByDistance(cd);
                float dx = cp.x - e.pos.x, dy = cp.y - e.pos.y, dz = cp.z - e.pos.z;
                if ( std::sqrt(dx * dx + dy * dy + dz * dz) > kConnThreshold ) continue;

                // 分岐キーの向き：接合点から j 側へ少し入った点の「交差軸」変位で決める
                //   i が横レール → W/S(±Z) で分岐 / i が縦レール → D/A(±X) で分岐
                float probe = ( std::min )( 1.0f, rj.GetLength() * 0.5f );
                Vector3 into = rj.GetPositionByDistance(e.isFront ? probe : rj.GetLength() - probe);
                float cross = ( ri.type == SplineRail::RailType::Horizontal ) ? ( into.z - cp.z )
                                                                              : ( into.x - cp.x );
                if ( std::abs(cross) < 0.3f ) continue; // 交差方向が曖昧（ほぼ平行）→キーに割当不能

                SplineRail::BranchPoint bp;
                bp.distance   = cd;
                bp.targetRail = j;
                bp.targetDist = e.isFront ? 0.0f : rj.GetLength();
                bp.zSign      = ( cross >= 0.0f ) ? +1 : -1;
                ri.branchPoints.push_back(bp);
                break; // この端は登録済み。他のレールへの多重登録はしない
            }
        }
    }
}
} // namespace

// エディタ保持の最新レール節点から rails_ を作り直す。
void RailField::Sync(Camera* camera, uint32_t whiteTexIndex){
    camera_ = camera;
    whiteTexIndex_ = whiteTexIndex;

    EditorManager* em = EditorManager::GetInstance();
    const auto& lines   = em->GetEditorRailLines();
    const auto& types   = em->GetEditorRailTypes();
    const auto& motions = em->GetEditorRailMotions();

    rails_.clear();
    for ( const auto& line : lines ) {
        SplineRail rail;
        rail.nodes = line;
        rail.BuildDistanceTable();
        rails_.push_back(rail);
    }

    // 動きを「連結処理より先」に割り当てる（連結処理が HasMotion を判定できるように）。
    const auto& motionTypes  = em->GetEditorRailMotionTypes();
    const auto& motionPhases = em->GetEditorRailMotionPhases();
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        if ( i < motions.size() ) {
            rails_[i].motionAmp    = { motions[i].x, motions[i].y, motions[i].z };
            rails_[i].motionPeriod = ( motions[i].w > 0.1f ) ? motions[i].w : 0.1f;
        }
        rails_[i].motionType  = ( i < motionTypes.size() )  ? motionTypes[i]  : 0;
        rails_[i].motionPhase = ( i < motionPhases.size() ) ? motionPhases[i] : 0.0f;
        rails_[i].animOffset = { 0.0f, 0.0f, 0.0f };
    }

    // タイプ割当は「連結処理より先」に行う（分岐点のキー割当が type を参照するため）。
    //   0/1 ならそれを使い、-1(自動)や未設定は主軸で自動判定。
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        int t = ( i < types.size() ) ? types[i] : -1;
        if ( t == 0 )      rails_[i].type = SplineRail::RailType::Horizontal;
        else if ( t == 1 ) rails_[i].type = SplineRail::RailType::Vertical;
        else               rails_[i].AutoDetectType();
    }

    BuildRailConnections(rails_); // 接続・分岐・ループ（動くレールは静的連結から除外）

    // 地面タイプ・穴・表示フラグ・片方向・速度倍率を割当
    const auto& grounds  = em->GetEditorRailGroundTypes();
    const auto& holes    = em->GetEditorRailNodeHoles();
    const auto& visible  = em->GetEditorRailVisible();
    const auto& oneWays  = em->GetEditorRailOneWay();
    const auto& spdMuls  = em->GetEditorRailSpeedMuls();
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        int gt = ( i < grounds.size() ) ? grounds[i] : 0;
        rails_[i].groundType = static_cast<SplineRail::GroundType>(gt);
        if ( i < holes.size() ) rails_[i].nodeHole = holes[i];
        else                    rails_[i].nodeHole.clear();
        rails_[i].visible  = ( i < visible.size() ) ? ( visible[i] != 0 ) : true;
        rails_[i].oneWay   = ( i < oneWays.size() ) ? oneWays[i] : 0;
        rails_[i].speedMul = ( i < spdMuls.size() && spdMuls[i] > 0.05f ) ? spdMuls[i] : 1.0f;
    }

    // スタート/ゴール地点（レール番号＋ノード番号 → レール上の距離へ変換）
    auto nodeToDist = [&](int rail, int node, int& outRail, float& outDist){
        outRail = 0; outDist = 0.0f;
        if ( rail < 0 || rail >= ( int ) rails_.size() ) { outRail = -1; return; }
        outRail = rail;
        int maxNode = ( int ) rails_[rail].nodes.size() - 1;
        int n = std::clamp(node, 0, ( std::max )( maxNode, 0 ));
        outDist = rails_[rail].GetDistanceFromT(( float ) n);
    };
    nodeToDist(em->GetEditorStartRail(), em->GetEditorStartNode(), startRail_, startDist_);
    if ( startRail_ < 0 ) { startRail_ = 0; startDist_ = 0.0f; } // 未設定はレール0の先頭
    nodeToDist(em->GetEditorGoalRail(), em->GetEditorGoalNode(), goalRail_, goalDist_);

    animTime_ = 0.0f;

    BuildMarkers();
    lastVersion_ = em->GetRailEditVersion();
}

// 動くレールの時間を進めて animOffset を更新し、緑線マーカーも追従させる。
//   波形は motionType で選択（0=サイン往復 / 1=端で一時停止つき往復 / 2=円運動）。
//   motionPhase(0〜1)で複数レールの動きをずらせる（0.5=半周期ずれ）。
void RailField::UpdateMotion(float dt){
    animTime_ += dt;
    const float kTwoPi = 2.0f * 3.14159265f;
    bool anyMotion = false;
    for ( auto& rail : rails_ ) {
        if ( !rail.HasMotion() ) continue;
        anyMotion = true;
        float period = ( rail.motionPeriod > 0.1f ) ? rail.motionPeriod : 0.1f;
        float u = animTime_ / period + rail.motionPhase;
        u -= std::floor(u); // 0〜1 の周期位置

        const Vector3& amp = rail.motionAmp;
        switch ( rail.motionType ) {
        case 1: { // 端で一時停止つき往復（各端で周期の15%停止。移動はコサインで滑らか）
            const float dwell = 0.15f;
            const float move  = 0.5f - dwell;
            float w;
            if      ( u < move )        { w = -std::cos(( u / move ) * 3.14159265f); }          // -1 → +1
            else if ( u < 0.5f )        { w = 1.0f; }                                            // +端で停止
            else if ( u < 0.5f + move ) { w =  std::cos(( ( u - 0.5f ) / move ) * 3.14159265f); }// +1 → -1
            else                        { w = -1.0f; }                                           // -端で停止
            rail.animOffset = { amp.x * w, amp.y * w, amp.z * w };
            break;
        }
        case 2: { // 円運動（XZ楕円＋Y。半径は amp の各成分。amp.x と amp.z で円になる）
            float th = u * kTwoPi;
            rail.animOffset = { amp.x * std::cos(th), amp.y * std::sin(th), amp.z * std::sin(th) };
            break;
        }
        default: { // 0: サイン往復（従来どおり）
            float w = std::sin(u * kTwoPi);
            rail.animOffset = { amp.x * w, amp.y * w, amp.z * w };
            break;
        }
        }
    }
    if ( anyMotion ) { UpdateMarkerPositions(); }
}

// 編集モードへ戻った時：動くレールを基準位置へ戻す。
void RailField::ResetMotion(){
    animTime_ = 0.0f;
    for ( auto& r : rails_ ) { r.animOffset = { 0.0f, 0.0f, 0.0f }; }
    UpdateMarkerPositions();
}

// マーカー位置 = 基準位置 + そのレールの animOffset
void RailField::UpdateMarkerPositions(){
    for ( size_t k = 0; k < markers_.size(); ++k ) {
        if ( k >= markerRail_.size() || k >= markerBase_.size() ) break;
        int ri = markerRail_[k];
        if ( ri < 0 || ri >= ( int ) rails_.size() ) continue;
        const Vector3& off  = rails_[ri].animOffset;
        const Vector3& base = markerBase_[k];
        markers_[k]->SetTranslation({ base.x + off.x, base.y + off.y, base.z + off.z });
    }
}

void RailField::UpdateMarkers(){
    for ( auto& marker : markers_ ) { marker->Update(); }
}

void RailField::DrawMarkers() const{
    if ( !showMarkers_ ) return;
    for ( const auto& marker : markers_ ) { marker->Draw(); }
}

void RailField::RebuildMarkers(){
    BuildMarkers();
}

// rails_ を距離で細かくサンプルし、隣り合う点を「細いバー」で繋いで経路を可視化する。
//   通常区間=緑モデル / 穴区間=赤モデル（マテリアルはモデル単位で共有のため色はモデルに1回設定）。
void RailField::BuildMarkers(){
    markers_.clear();
    markerRail_.clear();
    markerBase_.clear();

    Model* segModel  = ModelManager::GetInstance()->FindModel("railLineCube");
    Model* holeModel = ModelManager::GetInstance()->FindModel("railLineCubeHole");
    if ( segModel == nullptr ) { return; }
    if ( holeModel == nullptr ) { holeModel = segModel; }

    const float spacing   = 0.5f;
    const float thickness = 0.08f;
    const Vector4 lineColor = { 0.2f, 1.0f, 0.35f, 1.0f }; // 通常区間：明るい緑
    const Vector4 holeColor = { 1.0f, 0.2f, 0.15f, 1.0f }; // 穴区間：赤

    if ( whiteTexIndex_ != 0 ) { segModel->SetTexture(whiteTexIndex_); holeModel->SetTexture(whiteTexIndex_); }
    if ( segModel->GetMaterial() )  { segModel->GetMaterial()->color = lineColor;  segModel->GetMaterial()->enableLighting = 0; }
    if ( holeModel->GetMaterial() ) { holeModel->GetMaterial()->color = holeColor; holeModel->GetMaterial()->enableLighting = 0; }

    for ( int railIdx = 0; railIdx < ( int ) rails_.size(); ++railIdx ) {
        const SplineRail& rail = rails_[railIdx];
        float len = rail.GetLength();
        if ( len <= 0.0f || rail.nodes.size() < 2 ) { continue; }
        if ( !rail.visible ) { continue; } // 非表示レール（連結用）は緑線を描かない

        Vector3 prev = rail.GetPositionByDistance(0.0f);
        for ( float s = spacing; s <= len + 0.001f; s += spacing ) {
            float ss = ( s > len ) ? len : s;
            Vector3 cur = rail.GetPositionByDistance(ss);

            Vector3 d = { cur.x - prev.x, cur.y - prev.y, cur.z - prev.z };
            float segLen = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if ( segLen > 1e-4f ) {
                Vector3 dir = { d.x / segLen, d.y / segLen, d.z / segLen };
                float yaw   = std::atan2(dir.x, dir.z);
                float dyC   = std::clamp(dir.y, -1.0f, 1.0f);
                float pitch = -std::asin(dyC);

                float midS = ss - spacing * 0.5f;
                if ( midS < 0.0f ) midS = 0.0f;
                bool isHole = rail.IsHoleAtDistance(midS);

                auto box = std::make_unique<Obj3d>();
                box->Initialize(isHole ? holeModel : segModel);
                box->SetCamera(camera_);
                box->SetScale({ thickness, thickness, segLen });
                Vector3 markerPos = { ( prev.x + cur.x ) * 0.5f, ( prev.y + cur.y ) * 0.5f, ( prev.z + cur.z ) * 0.5f };
                box->SetTranslation(markerPos);
                box->SetRotation({ pitch, yaw, 0.0f });
                box->Update();
                markers_.push_back(std::move(box));
                markerRail_.push_back(railIdx);
                markerBase_.push_back({ markerPos.x - rail.animOffset.x,
                                        markerPos.y - rail.animOffset.y,
                                        markerPos.z - rail.animOffset.z });
            }
            prev = cur;
        }
    }
}
