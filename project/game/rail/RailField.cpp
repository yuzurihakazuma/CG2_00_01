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

    // フェーズ2：連結端点を中点へ溶接（隙間を物理的に詰める）。
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

    bool moved = false;
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( !orig[i].valid ) continue;
        if ( rails[i].frontConnIndex >= 0 ){
            int j = rails[i].frontConnIndex;
            if ( orig[j].valid ){
                Vector3 partner = rails[i].frontConnToFront ? orig[j].f : orig[j].b;
                rails[i].nodes.front() = mid(orig[i].f, partner);
                moved = true;
            }
        }
        if ( rails[i].backConnIndex >= 0 ){
            int j = rails[i].backConnIndex;
            if ( orig[j].valid ){
                Vector3 partner = rails[i].backConnToFront ? orig[j].f : orig[j].b;
                rails[i].nodes.back() = mid(orig[i].b, partner);
                moved = true;
            }
        }
    }

    // フェーズ3：ノードを動かしたので距離テーブルを作り直す。
    if ( moved ){
        for ( auto& r : rails ){ r.BuildDistanceTable(); }
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
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        if ( i < motions.size() ) {
            rails_[i].motionAmp    = { motions[i].x, motions[i].y, motions[i].z };
            rails_[i].motionPeriod = ( motions[i].w > 0.1f ) ? motions[i].w : 0.1f;
        }
        rails_[i].animOffset = { 0.0f, 0.0f, 0.0f };
    }

    BuildRailConnections(rails_); // 接続・分岐・ループ（動くレールは静的連結から除外）

    // タイプ割当：0/1 ならそれを使い、-1(自動)や未設定は主軸で自動判定
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        int t = ( i < types.size() ) ? types[i] : -1;
        if ( t == 0 )      rails_[i].type = SplineRail::RailType::Horizontal;
        else if ( t == 1 ) rails_[i].type = SplineRail::RailType::Vertical;
        else               rails_[i].AutoDetectType();
    }

    // 地面タイプ・穴・表示フラグを割当
    const auto& grounds = em->GetEditorRailGroundTypes();
    const auto& holes   = em->GetEditorRailNodeHoles();
    const auto& visible = em->GetEditorRailVisible();
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        int gt = ( i < grounds.size() ) ? grounds[i] : 0;
        rails_[i].groundType = static_cast<SplineRail::GroundType>(gt);
        if ( i < holes.size() ) rails_[i].nodeHole = holes[i];
        else                    rails_[i].nodeHole.clear();
        rails_[i].visible = ( i < visible.size() ) ? ( visible[i] != 0 ) : true;
    }
    animTime_ = 0.0f;

    BuildMarkers();
    lastVersion_ = em->GetRailEditVersion();
}

// 動くレールの時間を進めて animOffset を更新し、緑線マーカーも追従させる。
void RailField::UpdateMotion(float dt){
    animTime_ += dt;
    bool anyMotion = false;
    for ( auto& rail : rails_ ) {
        if ( !rail.HasMotion() ) continue;
        anyMotion = true;
        float period = ( rail.motionPeriod > 0.1f ) ? rail.motionPeriod : 0.1f;
        float phase = std::sin(animTime_ * 2.0f * 3.14159265f / period);
        rail.animOffset = { rail.motionAmp.x * phase, rail.motionAmp.y * phase, rail.motionAmp.z * phase };
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
