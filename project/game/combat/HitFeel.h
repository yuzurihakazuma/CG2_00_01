#pragma once
#include "engine/math/struct.h"

class Camera;

// =====================================================================
//  HitFeel：ヒット時の「手応え」演出をまとめたクラス。
//   ・ヒットストップ（一瞬だけ時間を止める）
//   ・カメラシェイク（基準位置を汚さない自己相殺方式）
//   ・踏んだ点中心のポストエフェクト（歪みリップル＋スポットグロー）
//  シーンから分離した理由：踏みつけ/卵命中/飲み込み等あらゆるヒットで共通に使う
//  演出であり、他シーンでも再利用できる。
// =====================================================================
class HitFeel {
public:
    // ヒットの手応え（stopSeconds=時間停止 / shakeMag=揺れの強さm）
    void Trigger(float stopSeconds, float shakeMag);

    // 踏んだ点中心のポストエフェクト（歪み＋グロー。約0.4秒）を起動
    void TriggerImpactFx(const Vector3& worldPos);

    // 毎フレーム先頭で呼ぶ：ヒットストップ（Time のタイムスケール制御）
    void UpdateHitStop();

    // カメラ更新の前に呼ぶ：前フレームの揺れを引いて今フレームの揺れを足す
    void ApplyCameraShake(Camera* camera);

    // カメラ確定後に呼ぶ：中心をスクリーン投影し、半径アニメでポストエフェクトを出す
    void UpdateImpactPostEffect(Camera* camera);

    // 追従カメラ等がカメラ位置を上書きした時に呼ぶ（揺れの自己相殺をリセット）
    void NotifyCameraOverridden(){ camPrevShake_ = { 0.0f, 0.0f, 0.0f }; }

private:
    float   hitStopTimer_  = 0.0f;              // >0 の間は時間を止める
    float   camShakeTimer_ = 0.0f;              // >0 の間カメラを揺らす
    float   camShakeMag_   = 0.0f;              // 揺れの強さ (m)
    Vector3 camPrevShake_ { 0.0f, 0.0f, 0.0f }; // 前フレームに足した揺れ（自己相殺用）

    float   fxTimer_    = 0.0f;                 // >0 の間だけ歪み＋グローを出す
    Vector3 fxWorldPos_ { 0.0f, 0.0f, 0.0f };   // 効果の中心（ヒット点のワールド座標）
};
