#pragma once
#include "engine/math/struct.h"
#include <vector>
#include <memory>

class Obj3d;
class Model;
class Camera;
class SplineRail;

// =====================================================================
//  RoadMesh：レールの下に敷く「クラフト風の道」の管理クラス（道システム設計書 §4）。
//   タイルを並べる方式から「掃引（スイープ）生成」へ刷新：
//   ・レール1本 = 1メッシュ（= 1ドローコール）。SplineRail::FrameCache の
//     フレーム（位置+right/up/tangent）に12頂点の断面プロファイルを掃引する
//   ・曲率適応サンプリング：曲がりのきつい所だけリングを増やす
//   ・内側折返しの溶接：急カーブの内側で面が裏返らない
//   ・坂UV切替：|tangent.y|>0.40 で上面テクスチャを坂帯へ（ヒステリシス＋二重リング）
//   ・穴区間には面を張らない＝落とし穴がそのまま見える
//   ・非ループの両端に road_end ピースを自動配置
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
    // レール1本ぶんの掃引メッシュを生成して Model/Obj3d 化する
    void BuildRailMesh(const SplineRail& rail, int railIdx, Camera* camera, uint32_t atlasSrv);

    // 終端キャップ（road_end ピース）を p0 から p1 の向きで置く
    void PlaceEndCap(Model* model, const std::vector<SplineRail>& rails, int railIdx,
                     const Vector3& p0, const Vector3& p1, Camera* camera);

    // --- 掃引メッシュ（レール1本 = Model 1個 + Obj3d 1個）---
    std::vector<std::unique_ptr<Model>> roadModels_;
    std::vector<std::unique_ptr<Obj3d>> roadObjs_;
    std::vector<int> roadRail_;                 // 対応レール番号（動くレール追従用）

    // --- 終端キャップ（モデルピース）---
    std::vector<std::unique_ptr<Obj3d>> capObjs_;
    std::vector<int>     capRail_;
    std::vector<Vector3> capBase_;              // 基準位置（animOffset=0換算）

    bool visible_ = true;
};
