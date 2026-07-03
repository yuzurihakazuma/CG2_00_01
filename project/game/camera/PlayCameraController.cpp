#include "game/camera/PlayCameraController.h"

#include "engine/camera/Camera.h"
#include "engine/rail/SplineRail.h"
#include "engine/graphics/DebugDraw.h"
#include "engine/base/TimeManager.h" // 回転フリーズ（タイムスケール制御）用
#include "engine/math/VectorMath.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#include <algorithm>
#include <cmath>

using namespace VectorMath;

// マップのカメラゾーン設定を実行用へ変換（ノード番号→レール上の距離）
void PlayCameraController::Sync(const std::vector<LevelCameraZone>& zones, const std::vector<SplineRail>& rails){
    zones_.clear();
    for ( const auto& z : zones ) {
        if ( z.railIndex < 0 || z.railIndex >= ( int ) rails.size() ) continue;
        Zone cz;
        cz.rail   = z.railIndex;
        int maxN  = ( int ) rails[z.railIndex].nodes.size() - 1;
        int node  = std::clamp(z.nodeIndex, 0, ( std::max )( maxN, 0 ));
        cz.dist   = rails[z.railIndex].GetDistanceFromT(( float ) node);
        cz.radius = z.radius;
        cz.mode   = z.mode;
        cz.offset = z.offset;
        cz.yawRad    = z.yawDeg * 3.14159265f / 180.0f;
        cz.camDist   = z.dist;
        cz.camHeight = z.height;
        cz.revert    = ( z.revert != 0 );
        cz.freeze    = ( z.freeze != 0 );
        cz.fovRad = z.fovDeg * 3.14159265f / 180.0f;
        zones_.push_back(cz);
    }
    lastZone_ = -1;
}

// 追従の目標だけを通常（followOffset_ 基準）へ戻す。Cur はそのまま＝補間で滑らかに復帰する
void PlayCameraController::SetDefaultTargets(){
    float d = std::sqrt(followOffset_.x * followOffset_.x + followOffset_.z * followOffset_.z);
    camYawTgt_  = std::atan2(followOffset_.x, -followOffset_.z); // (0,3.5,-10)→0=後ろから
    camDistTgt_ = ( d > 0.5f ) ? d : 10.0f;
    camHgtTgt_  = followOffset_.y;
    camFovTgt_  = 0.78f;
}

// 追従カメラの向き/距離/高さ/画角を初期状態（followOffset_ 基準）へ戻す
void PlayCameraController::Reset(){
    SetDefaultTargets();
    camYawCur_  = camYawTgt_;
    camDistCur_ = camDistTgt_;
    camHgtCur_  = camHgtTgt_;
    camFovCur_  = camFovTgt_;
    lastZone_ = -1;
    // 回転フリーズ中にモードが切り替わっても時間が止まったままにならないよう必ず戻す
    if ( freezingRotation_ ) { Time::GetInstance()->SetTimeScale(1.0f); freezingRotation_ = false; }
}

void PlayCameraController::Update(Camera* camera, const Vector3& playerPos, const std::vector<SplineRail>& rails,
                                  bool debugCamActive, float dt){
    if ( !followCam_ || !camera ) return;
    if ( debugCamActive ) return; // デバッグカメラ優先

    // --- ゾーンの入退場判定（ヒステリシス付き）---
    //   入り=半径ちょうど / 出=半径×1.25。境界ぴったりに立った時に「入った/出た」が
    //   毎フレーム入れ替わり、目標角が交互に切り替わってカメラが激しく震えるのを防ぐ。
    //   さらに向き切替の適用は「入った瞬間だけ」にして、境界での再発火（フリーズ連打）も防ぐ。

    // 1) 今入っているゾーンからの離脱チェック
    if ( lastZone_ >= 0 && lastZone_ < ( int ) zones_.size() ) {
        const Zone& z = zones_[lastZone_];
        bool leave = true;
        if ( z.rail >= 0 && z.rail < ( int ) rails.size() ) {
            Vector3 a = rails[z.rail].GetPositionByDistance(z.dist);
            leave = ( Length(playerPos - a) > z.radius * 1.25f ); // 出る判定は少し外側
        }
        if ( leave ) {
            // 「戻る」設定の向き切替から出た → 追従の目標を通常へ（Cur は補間でゆっくり戻る）
            if ( z.mode == 1 && z.revert ) { SetDefaultTargets(); }
            lastZone_ = -1;
        }
    } else {
        lastZone_ = -1;
    }

    // 2) 未入場なら、新しく入ったゾーンを探す（一番近いもの）
    if ( lastZone_ < 0 ) {
        float bestD = 1e30f;
        for ( int i = 0; i < ( int ) zones_.size(); ++i ) {
            const Zone& z = zones_[i];
            if ( z.rail < 0 || z.rail >= ( int ) rails.size() ) continue;
            Vector3 a = rails[z.rail].GetPositionByDistance(z.dist); // 動くレールなら animOffset 込みで追従
            float d = Length(playerPos - a);
            if ( d < z.radius && d < bestD ) { bestD = d; lastZone_ = i; }
        }
        // 入った瞬間だけ、向き切替の目標を適用（毎フレーム適用しない）
        if ( lastZone_ >= 0 && zones_[lastZone_].mode == 1 ) {
            const Zone& z = zones_[lastZone_];
            camYawTgt_  = z.yawRad;
            camDistTgt_ = z.camDist;
            camHgtTgt_  = z.camHeight;
            camFovTgt_  = z.fovRad;

            // 「回転中は時間を止める」：まだ大きく回す必要があるならフリーズ開始。
            //   （角度を少し変えるだけの演出は freeze OFF でそのまま進ませる＝ゾーンごとに選べる）
            if ( z.freeze ) {
                float dy = camYawTgt_ - camYawCur_;
                while ( dy >  3.14159265f ) dy -= 2.0f * 3.14159265f;
                while ( dy < -3.14159265f ) dy += 2.0f * 3.14159265f;
                if ( std::abs(dy) > 0.26f ) { freezingRotation_ = true; } // 約15°以上回すなら停止演出
            }
        }
    }

    // 3) 固定カメラ(mode0)ゾーンに入っている間の情報（毎フレーム位置を追う）
    const Zone* active = nullptr;
    Vector3 anchor {};
    if ( lastZone_ >= 0 && zones_[lastZone_].mode == 0 ) {
        const Zone& z = zones_[lastZone_];
        if ( z.rail >= 0 && z.rail < ( int ) rails.size() ) {
            anchor = rails[z.rail].GetPositionByDistance(z.dist);
            active = &z;
        }
    }

    // 回転フリーズ中：時間を止める（毎フレーム上書き＝ヒットストップ終了で戻されても維持）。
    //   dt=0 になるので、カメラの補間だけリアル時間(1/60)で進めて回転を完了させる。
    if ( freezingRotation_ ) { Time::GetInstance()->SetTimeScale(0.0f); }
    float camDt = freezingRotation_ ? ( 1.0f / 60.0f ) : dt;
    float k = ( std::min )( 4.0f * camDt, 1.0f );

    // 目標のカメラ位置と視野角
    Vector3 targetPos;
    float   targetFov;
    if ( active && active->mode == 0 ) {
        // 固定カメラ：範囲内だけアンカー+オフセットに固定
        targetPos = { anchor.x + active->offset.x, anchor.y + active->offset.y, anchor.z + active->offset.z };
        targetFov = active->fovRad;
    } else {
        // 追従：向き(yaw)を最短経路で滑らかに回す → 180度切替は「回り込み」の演出になる
        float dyaw = camYawTgt_ - camYawCur_;
        while ( dyaw >  3.14159265f ) dyaw -= 2.0f * 3.14159265f;
        while ( dyaw < -3.14159265f ) dyaw += 2.0f * 3.14159265f;

        // 回転フリーズ：ほぼ回り終わったら時間を戻してゲーム再開
        if ( freezingRotation_ && std::abs(dyaw) < 0.05f ) {
            freezingRotation_ = false;
            Time::GetInstance()->SetTimeScale(1.0f);
        }

        camYawCur_  += dyaw * k;
        camDistCur_ += ( camDistTgt_ - camDistCur_ ) * k;
        camHgtCur_  += ( camHgtTgt_  - camHgtCur_ )  * k;

        targetPos = { playerPos.x + std::sin(camYawCur_) * camDistCur_,
                      playerPos.y + camHgtCur_,
                      playerPos.z - std::cos(camYawCur_) * camDistCur_ };
        targetFov = camFovTgt_;
    }

    // 位置・画角を滑らかに補間（急に切り替わらない＝演出として自然）
    Vector3 cur = camera->GetWorldPosition();
    Vector3 np = { cur.x + ( targetPos.x - cur.x ) * k,
                   cur.y + ( targetPos.y - cur.y ) * k,
                   cur.z + ( targetPos.z - cur.z ) * k };
    camera->SetTranslation(np);

    // プレイヤー（少し上）を見る向きへ
    Vector3 look = { playerPos.x - np.x, ( playerPos.y + 1.0f ) - np.y, playerPos.z - np.z };
    float horiz = std::sqrt(look.x * look.x + look.z * look.z);
    if ( horiz > 1e-4f || std::abs(look.y) > 1e-4f ) {
        float yaw   = std::atan2(look.x, look.z);
        float pitch = std::atan2(-look.y, horiz); // +で下を向く（行ベクトル×RotX の規約）
        camera->SetRotation({ pitch, yaw, 0.0f });
    }

    camFovCur_ += ( targetFov - camFovCur_ ) * k;
    camera->SetFovY(camFovCur_);
}

// カメラ演出ゾーンの可視化（球=発動範囲 / 白い箱=カメラ位置の目安 / 線=対応）
void PlayCameraController::DrawZoneMarkers(const std::vector<SplineRail>& rails) const{
    for ( const auto& z : zones_ ) {
        if ( z.rail < 0 || z.rail >= ( int ) rails.size() ) continue;
        Vector3 a = rails[z.rail].GetPositionByDistance(z.dist);
        // mode0=アンカー+オフセット / mode1=切替後の追従位置の目安（アンカーをプレイヤーと見なす）
        Vector3 c;
        if ( z.mode == 1 ) {
            c = { a.x + std::sin(z.yawRad) * z.camDist, a.y + z.camHeight, a.z - std::cos(z.yawRad) * z.camDist };
        } else {
            c = { a.x + z.offset.x, a.y + z.offset.y, a.z + z.offset.z };
        }
        // 向き切替はオレンジ / 固定カメラは水色で区別
        Vector4 col = ( z.mode == 1 ) ? Vector4{ 1.0f, 0.6f, 0.2f, 0.35f } : Vector4{ 0.3f, 0.8f, 1.0f, 0.35f };
        DebugDraw::GetInstance()->Sphere(a, z.radius, col, 16);
        DebugDraw::GetInstance()->Box(c, { 0.5f, 0.4f, 0.7f }, { 1.0f, 1.0f, 1.0f, 0.9f });
        DebugDraw::GetInstance()->Line(a, c, { col.x, col.y, col.z, 0.8f });
    }
}

void PlayCameraController::DrawDebugUI(){
#ifdef USE_IMGUI
    ImGui::Checkbox("プレイ中カメラ: プレイヤー追従", &followCam_);
    ImGui::DragFloat3("追従オフセット", &followOffset_.x, 0.1f);
    ImGui::TextDisabled("ゾーン数: %d（レールエディタの「カメラ演出」で追加）", ( int ) zones_.size());
#endif
}
