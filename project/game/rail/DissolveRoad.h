#pragma once
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include "engine/math/struct.h"

class SplineRail;

// =====================================================================
//  DissolveRoad（SDF溶け道・チェーン融合方式）
//   道の種類=「溶け道」のレールに沿って 0.7m 間隔のチェーン点を打ち、
//   最大16点ずつのチャンクに分けて「角丸断面の線分SDF + smooth min 融合」を
//   1ドローのレイマーチングで描く。円盤を並べる方式と違い、
//   融合されて継ぎ目のない1本の道になる（重なりの縁・チラつきが出ない）。
//   ・エロージョンは点ごと（プレイヤー距離で決定）→ シェーダーが線分上で補間
//     ＝「近い所は実体・遠い所は溶けている」前線が道に沿ってなめらかに動く
//   ・溶けムラ（ノイズ）＋焦げ→赤熱の着色はシェーダー側（段ボールを熱した風）
//   ・形は解析SDF（式）なのでテクスチャ不要。何チャンク融合しても計算量が読める
// =====================================================================
class DissolveRoad {
public:
    // 専用パイプラインとプロキシ箱を構築する（テクスチャ読み込みは無い）
    void Initialize(ID3D12GraphicsCommandList* commandList);

    // 道の種類=溶け道（roadMode==2）のレールからチェーン点とチャンクを作り直す
    void Build(const std::vector<SplineRail>& rails);

    // プレイヤーとの距離を点ごとのエロージョン量へ反映（毎フレーム）
    void Update(const Vector3& playerPos, float dt);

    // SDF描画（シーンMRTパスの最後、他のSDF演出と同じ場所で呼ぶ）
    void Draw(ID3D12GraphicsCommandList* commandList);

    // 消え方の調整UI（SDFパネル内に追記する形で呼ぶ。形状系の変更は内部でAABBも更新）
    void DrawImGui();

    int ActiveCount() const;                              // 今描いているチャンク数
    int PieceCount() const { return ( int ) points_.size(); } // チェーン点の総数

    static const int kMaxChainPoints = 16; // 1チャンクの最大点数（シェーダーの MAX_POINTS と一致）

private:
    // シェーダーの MeltRoadCB と同じ並び
    struct ChunkCB {
        float viewProj[16];
        float rayBoxMin[3]; float pad0;
        float rayBoxMax[3]; float pad1;
        float cameraPos[3]; float pointCount;
        float baseColor[4];
        float lightDir[3]; float noiseFreq;
        float halfWidth; float halfThick; float roundR; float blendK;
        float erodeBand; float chainOffset; float spacing; float pad4; // chainOffset=チャンク先頭の通し番号（波板の位相を連続させる）
        float pts[kMaxChainPoints][4]; // xyz=点 / w=エロージョン量
    };

    struct ChainPoint {
        Vector3 pos {};        // チェーン点のワールド位置
        float   erode = 0.0f;  // 現在のエロージョン量（0=実体 / kErodeGone=消滅）
        int     rail  = -1;    // 属するレール番号（「後から出現する道」の判定用）
    };
    struct Chunk {
        int first = 0, count = 0;                          // points_ 内の範囲（隣と1点共有）
        Vector3 boxMin {}, boxMax {};                      // プロキシAABB（余白込み・固定）
        Microsoft::WRL::ComPtr<ID3D12Resource> cb;
        ChunkCB* mapped = nullptr;
        bool active = false;                               // このフレーム描くか
    };

    bool BuildPipeline();
    void RecomputeChunkBounds();   // 幅/厚み/融合幅の変更をプロキシAABBへ反映
    float ErodeGone() const;       // 完全消滅のエロージョン量（断面の厚みから自動算出）
    void SaveConfig() const;       // resources/dissolve_road.json へ保存
    void LoadConfig();             // 起動時に自動読込（無ければ既定値のまま）

    // --- 消え方の調整パラメータ（エディタのSDFパネルから変更・保存できる）---
    float nearDist_  = 3.0f;   // これより近い：完全に現れる
    float farDist_   = 7.0f;   // これより遠い：完全に溶けて消える
    float easeSpeed_ = 4.0f;   // 現れる/溶ける追従の速さ(1/s)
    float noiseFreq_ = 9.0f;   // 溶けムラの細かさ(1/m)
    float blendK_    = 0.35f;  // smooth min の融合幅(m)
    float halfWidth_ = 0.80f;  // 道の半幅(m)
    float halfThick_ = 0.18f;  // 道の半厚(m)

    std::vector<ChainPoint> points_;
    std::vector<Chunk>      chunks_;
    // Build 時のレール配列（借り物。「後から出現する道」の appeared 判定に使う。
    //   RailField が Sync のたびに作り直すため、必ず Build とセットで更新される）
    const std::vector<SplineRail>* railsRef_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;  // プロキシ箱（単位キューブ）
    D3D12_VERTEX_BUFFER_VIEW vbv_ {};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW ibv_ {};
    bool ready_ = false;
};
