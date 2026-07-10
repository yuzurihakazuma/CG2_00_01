#pragma once
#include "engine/math/struct.h"
#include <vector>
#include <memory>

class Obj3d;
class Model;
class Camera;
class SplineRail;

// =====================================================================
//  RoadMesh：レールの下に敷く「クラフト風の道」の管理クラス（道システム設計書 §4-§5）。
//   ・レール1本 = 1メッシュ（= 1ドローコール）。SplineRail::FrameCache の
//     フレーム（位置+right/up/tangent）に12頂点の断面プロファイルを掃引する
//   ・曲率適応サンプリング／内側折返しの溶接／坂UV切替（ヒステリシス＋二重リング）
//   ・穴区間には面を張らず、切り口には「フタ」を生成（中の空洞が見えない）
//   ・交差点の自動ピース配置：溶接コーナー / T字分岐 / 十字交差を検出して
//     road_corner / road_t / road_cross を90°単位で置き、掃引側は開口の
//     1m手前で切り詰める（断面が同一なのでピッタリ繋がる。軸沿いレール限定）
//   ・道の自由端（未接続の端）にだけ road_end を自動配置
//   ・動くレールへは「基準位置 + animOffset」で毎フレーム追従（再生成不要）
// =====================================================================
class RoadMesh {
public:
    RoadMesh();
    ~RoadMesh(); // unique_ptr<Model>/<Obj3d> のため cpp 側で定義

    // レールに沿って道を生成し直す（RailField::Sync の直後に呼ぶ）
    void Build(const std::vector<SplineRail>& rails, Camera* camera);

    // 毎フレーム：動くレールへの追従＋カメラ行列の焼き直し（Edit/Play共通で呼ぶ）
    void Update(const std::vector<SplineRail>& rails);

    void Draw() const;

    int  TileCount() const { return ( int ) roadObjs_.size() + ( int ) capObjs_.size(); }
    bool IsVisible() const { return visible_; }
    void SetVisible(bool v){ visible_ = v; } // デバッグUIから道のON/OFFを切り替える用

private:
    // 掃引をスキップする区間（交差点ピースに譲る範囲）
    struct Cut { float s0, s1; };

    // 交差点（溶接コーナー/T字/十字）を検出してピースを配置し、各レールの切り詰め範囲を作る
    void CollectJunctions(const std::vector<SplineRail>& rails, Camera* camera,
                          std::vector<std::vector<Cut>>& cuts);

    // レール1本ぶんの掃引メッシュを生成して Model/Obj3d 化する（cuts の区間は張らない）
    void BuildRailMesh(const SplineRail& rail, int railIdx, Camera* camera, uint32_t atlasSrv,
                       const std::vector<Cut>& cuts);

    // ピース（終端キャップ/コーナー/T字/十字）を置く共通処理
    void PlacePiece(Model* model, int railIdx, const std::vector<SplineRail>& rails,
                    const Vector3& pos, float yaw, float pitch, Camera* camera);

    // 終端キャップ（road_end）を p0 から p1 の向きで置く
    void PlaceEndCap(Model* model, const std::vector<SplineRail>& rails, int railIdx,
                     const Vector3& p0, const Vector3& p1, Camera* camera);

    // --- 掃引メッシュ（レール1本 = Model 1個 + Obj3d 1個）---
    std::vector<std::unique_ptr<Model>> roadModels_;
    std::vector<std::unique_ptr<Obj3d>> roadObjs_;
    std::vector<int> roadRail_;                 // 対応レール番号（動くレール追従用）

    // --- ピース（終端キャップ・交差点）---
    std::vector<std::unique_ptr<Obj3d>> capObjs_;
    std::vector<int>     capRail_;
    std::vector<Vector3> capBase_;              // 基準位置（animOffset=0換算）

    bool visible_ = true;
};
