#pragma once
#include "engine/math/struct.h"
#include <vector>
#include <memory>

class Obj3d;
class Model;
class Camera;
class SplineRail;

// =====================================================================
//  RoadMesh：レールの下に敷く「クラフト風の道メッシュ」の管理クラス。
//   ・resources/road/ の 4mタイル(roadSegment)と終端キャップ(roadEnd)を
//     レールに沿って並べる。緑線マーカーと違い Edit/Play どちらでも見せる
//     "本番の見た目"（Play中は緑線が消え、この道だけが残る）
//   ・穴区間のスロットには敷かない＝落とし穴がそのまま見た目に出る
//   ・非表示レール（連結用）はスキップ
//   ・動くレールへは「基準位置 + animOffset」方式で毎フレーム追従する
//  RailField から分離した理由：道は"見た目"の関心事で、レール実行データの
//  管理とは独立に育つ（テクスチャ替え・装飾・種類追加など）ため。
// =====================================================================
class RoadMesh {
public:
    RoadMesh();
    ~RoadMesh(); // unique_ptr<Obj3d> のため cpp 側で定義

    // レールに沿ってタイルを敷き直す（RailField::Sync の直後に呼ぶ）
    void Build(const std::vector<SplineRail>& rails, Camera* camera);

    // 毎フレーム：動くレールへの追従＋カメラ行列の焼き直し（Edit/Play共通で呼ぶ）
    void Update(const std::vector<SplineRail>& rails);

    void Draw() const;

    int  TileCount() const { return ( int ) tiles_.size(); }
    bool IsVisible() const { return visible_; }
    void SetVisible(bool v){ visible_ = v; } // デバッグUIから道のON/OFFを切り替える用

private:
    // タイルを1枚置く：p0（モデル原点＝接続面の下端中央）から p1 の方向へ向ける
    void PlaceTile(Model* model, const std::vector<SplineRail>& rails, int railIdx,
                   const Vector3& p0, const Vector3& p1, float zScale, Camera* camera);

    std::vector<std::unique_ptr<Obj3d>> tiles_; // 道タイルの実体
    std::vector<int>     tileRail_;             // 各タイルが属するレール番号（動くレール追従用）
    std::vector<Vector3> tileBase_;             // 各タイルの基準位置（animOffset=0換算）
    bool visible_ = true;                       // 道の表示ON/OFF
};
