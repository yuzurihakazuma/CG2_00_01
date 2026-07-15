#pragma once
#include "engine/math/struct.h"
#include "engine/3d/model/Model.h"
#include <vector>
#include <memory>

class Obj3d;
class Camera;
class SplineRail;

// =====================================================================
//  RoadMesh：レールの下に敷く「クラフト風の道」の管理クラス（道システム設計書 §4-§5）。
//   ・レール1本 = 1メッシュ（= 1ドローコール）。SplineRail::FrameCache の
//     フレーム（位置+right/up/tangent）に12頂点の断面プロファイルを掃引する
//   ・曲率適応サンプリング／内側折返しの溶接／坂UV切替（ヒステリシス＋二重リング）
//   ・穴区間は面を張らず、両端に road_end を自動配置。手前後 1m の上面は
//     危険帯テクスチャ（赤ストライプ）に切り替える（仕様書_穴区間 §2）
//   ・接続ノード（溶接コーナー/T字/十字）は任意角度の「ジャンクションパッチ」
//     （上面扇+ベベル+壁+底）をその場で生成して繋ぐ（GUIDE_ジャンクション生成）。
//     ゆるい2本溶接（150°以上）だけは掃引コネクタで連続的に繋ぐ
//   ・接続ノードにはプラレール風の road_joint を自動配置（保存しない派生データ）
//   ・roadMode=1（道なし）のレールはスキップし、接続相手からも除外する（§4）
//   ・GPUバッファはレール毎の固定容量スロットを使い回す（編集中の作り直しゼロ）。
//     simple=true の軽量ビルド（ドラッグ中用）はリング1m固定・交差点/キャップ省略
//   ・動くレールへは「基準位置 + animOffset」で毎フレーム追従（再生成不要）
// =====================================================================
class RoadMesh {
public:
    RoadMesh();
    ~RoadMesh(); // unique_ptr<Model>/<Obj3d> のため cpp 側で定義

    // レールに沿って道を生成し直す（RailField::Sync の直後に呼ぶ）。
    // simple=true はドラッグ中の軽量プレビュー（マウスアップ後に false で本生成する）
    void Build(const std::vector<SplineRail>& rails, Camera* camera, bool simple = false);

    // 毎フレーム：動くレールへの追従＋カメラ行列の焼き直し（Edit/Play共通で呼ぶ）
    void Update(const std::vector<SplineRail>& rails);

    void Draw() const;

    int  TileCount() const { return ( int ) ( slotsUsed_ + piecesUsed_ + jointsUsed_ ); }
    bool IsVisible() const { return visible_; }
    void SetVisible(bool v){ visible_ = v; } // デバッグUIから道のON/OFFを切り替える用

    // ジョイント（road_joint）の表示：0=エディタのみ / 1=常に / 2=非表示
    void SetJointVisible(int mode){ jointVisible_ = mode; }

    // 穴の警告帯（赤ストライプ）を敷く長さ(m)。変更後は Build で反映
    void  SetWarnLength(float m){ warnLength_ = m; }
    float GetWarnLength() const{ return warnLength_; }

    // 背面カリングの切替（true=両面描画・従来 / false=背面カリングでオーバードロー削減）。
    // 既存スロットにも即時反映される（再生成不要）
    void SetCullNone(bool cullNone);
    bool IsCullNone() const{ return cullNone_; }

private:
    // 掃引をスキップする区間（ジャンクションパッチに譲る範囲）
    struct Cut { float s0, s1; };

    // ジャンクション（共有ノード）の1本ぶんの腕
    struct Arm {
        int     rail = -1;
        float   nodeS = 0.0f;   // ノードのレール距離
        float   cutS = 0.0f;    // 道を切るレール距離（= 入口リングの位置）
        float   tCut = 0.35f;   // ノードから入口までの距離
        bool    forward = true; // true = ノードから +s 方向へ伸びる腕
        Vector3 dir {};         // ノードから出ていく方向（水平・正規化）
    };
    struct Junction {
        Vector3 center {};
        int     followRail = -1; // 動くレール追従用
        std::vector<Arm> arms;
    };

    // --- 動的メッシュスロット（VB/IB を使い回す。編集中の CreateBuffers ゼロ）---
    struct MeshSlot {
        std::unique_ptr<Model> model;
        std::unique_ptr<Obj3d> obj;
        int rail = -1;
        bool isJoint = false; // ジョイントのベイクメッシュ（表示モードで描画を切替）
    };
    // --- ピース（road_end / road_joint）スロット。Obj3d を使い回す ---
    struct PieceSlot {
        std::unique_ptr<Obj3d> obj;
        int rail = -1;
        Vector3 base {};
    };

    // 生成済みメッシュを空きスロットへ書き込む（スロットが足りなければ1個だけ確保）
    void EmitMesh(const Model::ModelData& data, int followRail, Camera* camera, uint32_t atlasSrv,
                  bool isJoint = false);

    // ピースモデルを回転+平行移動してベイク先メッシュへ焼き込む（静的レール用のDC削減。
    // 全ピースがアトラス共有なので、1つの動的メッシュにまとめて1ドローコールで描ける）
    void AppendPieceBake(Model* model, const Vector3& pos, float yaw, float pitch,
                         Model::ModelData& out) const;

    // ジャンクション検出（溶接/T字/十字）＋パッチ生成＋切り詰め範囲の登録
    void CollectJunctions(const std::vector<SplineRail>& rails, Camera* camera, uint32_t atlasSrv,
                          std::vector<std::vector<Cut>>& cuts);

    // 1ジャンクションの t_cut 計算（ウェッジのマイター交点）＋Cut登録
    void ComputeArmCuts(const std::vector<SplineRail>& rails, Junction& junc,
                        std::vector<std::vector<Cut>>& cuts) const;

    // 1ジャンクションのパッチ（上面扇+ベベル+壁+底）を生成する
    void BuildJunctionPatch(const std::vector<SplineRail>& rails, const Junction& junc,
                            Camera* camera, uint32_t atlasSrv);

    // レール1本ぶんの掃引メッシュを生成する（cuts の区間は張らない）。
    //   capFront/capBack: 自由端に平らな暗色フタを張る（丸い road_end の代わり）
    void BuildRailMesh(const SplineRail& rail, int railIdx, Camera* camera, uint32_t atlasSrv,
                       const std::vector<Cut>& cuts, bool simple,
                       bool capFront = false, bool capBack = false);

    // ピース（road_end / road_joint）を1個置く
    void PlacePiece(Model* model, std::vector<std::unique_ptr<PieceSlot>>& pool, size_t& used,
                    int railIdx, const std::vector<SplineRail>& rails,
                    const Vector3& pos, float yaw, float pitch, Camera* camera);

    // ジャンクションのジョイント（road_joint）配置
    void PlaceJoints(const std::vector<SplineRail>& rails, const Junction& junc, Camera* camera);
    void PlaceJointPiece(const std::vector<SplineRail>& rails, int railIdx,
                         const Vector3& railPos, float yaw, Camera* camera);

    // --- 掃引メッシュ/パッチのスロットプール ---
    std::vector<std::unique_ptr<MeshSlot>> slots_;
    size_t slotsUsed_ = 0;

    // --- ピースプール（終端キャップ＝road_end / ジョイント＝road_joint）---
    std::vector<std::unique_ptr<PieceSlot>> pieces_;
    size_t piecesUsed_ = 0;
    std::vector<std::unique_ptr<PieceSlot>> joints_;
    size_t jointsUsed_ = 0;

    bool visible_ = true;
    int  jointVisible_ = 1; // 0=エディタのみ / 1=常に / 2=非表示

    float warnLength_ = 1.0f; // 穴の手前後に危険帯（赤ストライプ・上面のみ）を敷く長さ(m)
    bool  cullNone_   = true; // true=両面描画（従来） / false=背面カリング

    // 静的レールのピースをまとめるベイク先（Build 中だけ使い、最後に EmitMesh する）
    Model::ModelData bakeCaps_;   // 終端キャップ（road_end）
    Model::ModelData bakeJoints_; // ジョイント（road_joint）
};
