#pragma once
#include "engine/math/struct.h"
#include "engine/utils/Level/LevelData.h" // LevelCameraZone
#include <vector>

class Camera;
class SplineRail;

// =====================================================================
//  PlayCameraController：プレイ中のカメラ制御。
//   ・基本：プレイヤーを「向き(yaw)・距離・高さ」で追従（滑らかに寄る）
//   ・向き切替トリガー(mode=1)：通過した瞬間に追従の向きが切り替わり、
//     プレイヤーの周りを回り込んで新しい向きへ（180度なら正面へ回転）。次のトリガーまで維持
//   ・固定カメラ(mode=0)：半径内にいる間だけ「アンカー+オフセット」に固定。離れると追従へ戻る
//  シーンから分離した理由：カメラ演出はゲーム進行と独立した関心事で、他シーンでも再利用できる。
// =====================================================================
class PlayCameraController {
public:
    // マップのカメラゾーン設定を実行用へ変換して受け取る（ノード番号→レール上の距離）
    void Sync(const std::vector<LevelCameraZone>& zones, const std::vector<SplineRail>& rails);

    // 追従の向き/距離/画角を初期状態へ戻す（プレイ開始時に呼ぶ。前回の向きを持ち越さない）
    void Reset();

    // 毎フレーム（Play中のみ）：追従＋ゾーン切替でカメラを動かす。
    //   debugCamActive=true（デバッグカメラ操作中）は何もしない。
    void Update(Camera* camera, const Vector3& playerPos, const std::vector<SplineRail>& rails,
                bool debugCamActive, float dt);

    // ゾーンの可視化（DebugDraw に積む。編集中も見えるよう毎フレーム呼んでよい）
    void DrawZoneMarkers(const std::vector<SplineRail>& rails) const;

    // デバッグUI（追従ON/OFF・オフセット。呼び出し側のウィンドウ内に描く）
    void DrawDebugUI();

    int ZoneCount() const { return ( int ) zones_.size(); }

private:
    // 追従のON/OFFと基準（yaw0/距離/高さの初期値になる）
    bool    followCam_ = true;
    Vector3 followOffset_ { 0.0f, 3.5f, -10.0f };

    // 追従カメラの「向き」の状態。向き切替トリガーで Tgt が変わり Cur が滑らかに追いつく
    float camYawCur_ = 0.0f,   camYawTgt_ = 0.0f;   // カメラの向き(rad)。0=後ろから
    float camDistCur_ = 10.0f, camDistTgt_ = 10.0f; // プレイヤーからの距離
    float camHgtCur_ = 3.5f,   camHgtTgt_ = 3.5f;   // プレイヤーからの高さ
    float camFovCur_ = 0.78f,  camFovTgt_ = 0.78f;  // 視野角(rad)

    // 実行用カメラゾーン（Sync 時にノード番号→レール上の距離へ変換して保持）
    struct Zone {
        int   rail = 0; float dist = 0.0f; float radius = 4.0f;
        int   mode = 1;                          // 0=固定カメラ / 1=向き切替
        Vector3 offset { 0.0f, 3.0f, -6.0f };    // mode0: アンカーからのカメラ位置
        float yawRad = 3.14159f; float camDist = 10.0f; float camHeight = 3.5f; // mode1
        bool  revert = false;                    // mode1: 半径から出たら通常の向きへ戻る
        bool  freeze = false;                    // mode1: 回転が終わるまで時間を止める
        float fovRad = 0.785f;
    };
    std::vector<Zone> zones_;

    // 回転フリーズ：向き切替（freeze指定）の回転が終わるまで時間を止める。
    //   時間停止中も dt=0 になるため、回転の補間はリアル時間(1/60)で進める。
    bool freezingRotation_ = false;

    // 今「入っている」扱いのゾーン番号（-1=なし）。
    //   入り=半径ちょうど / 出=半径×1.25 のヒステリシス付き。境界に立った時に
    //   入退場が毎フレーム入れ替わってカメラが震えるのを防ぐ。切替の適用は入った瞬間だけ。
    int lastZone_ = -1;

    // 追従の目標を通常（followOffset_ 基準）へ戻す（Cur は補間で滑らかに追いつく）
    void SetDefaultTargets();
};
