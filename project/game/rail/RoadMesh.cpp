#include "game/rail/RoadMesh.h"

#include "engine/rail/SplineRail.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/ModelManager.h"

#include <cmath>
#include <algorithm>

RoadMesh::RoadMesh() = default;
RoadMesh::~RoadMesh() = default;

// タイルを1枚置く。向きの計算は RailField::BuildMarkers と同じ流儀
//   （yaw = atan2(dir.x, dir.z) / pitch = -asin(dir.y)。回転順は Rx→Ry の行ベクトル）。
void RoadMesh::PlaceTile(Model* model, const std::vector<SplineRail>& rails, int railIdx,
                         const Vector3& p0, const Vector3& p1, float zScale, Camera* camera){
    const float kThickness = 1.0f;  // 道の厚み。モデルの上面はローカル Y=+1（README_road.md）
    const float kTopGap    = 0.05f; // レール緑線とのZファイト回避（道の上面を少しだけ下げる）

    Vector3 d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if ( dl < 1e-4f ) return;
    Vector3 dir = { d.x / dl, d.y / dl, d.z / dl };
    float yaw   = std::atan2(dir.x, dir.z);
    float pitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));

    auto obj = std::make_unique<Obj3d>();
    obj->Initialize(model);
    obj->SetCamera(camera);
    obj->SetScale({ 1.0f, 1.0f, zScale }); // 長さ方向だけレールに合わせて伸縮
    Vector3 pos = { p0.x, p0.y - kThickness - kTopGap, p0.z };
    obj->SetTranslation(pos);
    obj->SetRotation({ pitch, yaw, 0.0f });
    obj->Update();

    tiles_.push_back(std::move(obj));
    tileRail_.push_back(railIdx);
    const Vector3& off = rails[railIdx].animOffset;
    tileBase_.push_back({ pos.x - off.x, pos.y - off.y, pos.z - off.z });
}

// レールに沿って道を敷き直す。
//   ・4mタイル(roadSegment)をレール全長の等分割で並べる
//     （1枚あたりの長さを微伸縮して端数を吸収 → 継ぎ目がぴったり一致する）
//   ・非ループのレールは両端の外側に終端キャップ(roadEnd, 2.6m)を付けて断面を閉じる
//   ・曲線は弦（区間の始点→終点の直線）で近似する
void RoadMesh::Build(const std::vector<SplineRail>& rails, Camera* camera){
    tiles_.clear();
    tileRail_.clear();
    tileBase_.clear();

    Model* segModel = ModelManager::GetInstance()->FindModel("roadSegment");
    Model* endModel = ModelManager::GetInstance()->FindModel("roadEnd");
    if ( segModel == nullptr ) { return; } // モデル未ロードなら何もしない（安全側）
    if ( endModel == nullptr ) { endModel = segModel; }

    const float kTileLen = 4.0f; // road_segment の長さ（resources/road/README_road.md 参照）

    for ( int railIdx = 0; railIdx < ( int ) rails.size(); ++railIdx ) {
        const SplineRail& rail = rails[railIdx];
        float len = rail.GetLength();
        if ( len <= 0.0f || rail.nodes.size() < 2 ) continue;
        if ( !rail.visible ) continue; // 連結用の見えないレールには道を敷かない

        // 4mタイルを等分割で敷く
        int   slots   = ( std::max )( 1, ( int ) std::lround(len / kTileLen) );
        float slotLen = len / slots;
        for ( int i = 0; i < slots; ++i ) {
            float s0 = slotLen * i;
            float s1 = slotLen * ( i + 1 );
            if ( rail.IsHoleAtDistance(( s0 + s1 ) * 0.5f) ) continue; // 穴の上には敷かない
            PlaceTile(segModel, rails, railIdx,
                      rail.GetPositionByDistance(s0), rail.GetPositionByDistance(s1),
                      slotLen / kTileLen, camera);
        }

        // 終端キャップ：非ループの両端の外側に向けて置き、開いている断面を閉じる。
        //   キャップの接続面はモデル原点側（README準拠）。端の向きは少し内側の点との延長で出す。
        if ( !rail.isLoop ) {
            const float kProbe = ( std::min )( 1.0f, len * 0.5f );
            Vector3 e1  = rail.GetPositionByDistance(len);
            Vector3 e1b = rail.GetPositionByDistance(len - kProbe);
            PlaceTile(endModel, rails, railIdx, e1,
                      { e1.x * 2.0f - e1b.x, e1.y * 2.0f - e1b.y, e1.z * 2.0f - e1b.z }, 1.0f, camera);
            Vector3 e0  = rail.GetPositionByDistance(0.0f);
            Vector3 e0b = rail.GetPositionByDistance(kProbe);
            PlaceTile(endModel, rails, railIdx, e0,
                      { e0.x * 2.0f - e0b.x, e0.y * 2.0f - e0b.y, e0.z * 2.0f - e0b.z }, 1.0f, camera);
        }
    }
}

// 毎フレーム：動くレールの animOffset に追従し、カメラ行列を焼き直す。
//   （Obj3d::Update は「呼ばれた時のカメラ行列」を使うため、毎フレーム呼ばないと
//     デバッグカメラを動かした時に道が画面へ貼り付いてしまう）
void RoadMesh::Update(const std::vector<SplineRail>& rails){
    for ( size_t k = 0; k < tiles_.size(); ++k ) {
        if ( k >= tileRail_.size() || k >= tileBase_.size() ) break;
        int ri = tileRail_[k];
        if ( ri >= 0 && ri < ( int ) rails.size() ) {
            const Vector3& off  = rails[ri].animOffset;
            const Vector3& base = tileBase_[k];
            tiles_[k]->SetTranslation({ base.x + off.x, base.y + off.y, base.z + off.z });
        }
        tiles_[k]->Update();
    }
}

void RoadMesh::Draw() const{
    if ( !visible_ ) return;
    for ( const auto& t : tiles_ ) { t->Draw(); }
}
