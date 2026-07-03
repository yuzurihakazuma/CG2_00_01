#include "game/combat/HitFeel.h"

#include "engine/base/TimeManager.h"
#include "engine/base/WindowProc.h"
#include "engine/camera/Camera.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/math/Matrix4x4.h"

#include <algorithm>
#include <cmath>

void HitFeel::Trigger(float stopSeconds, float shakeMag){
    hitStopTimer_  = stopSeconds;
    camShakeTimer_ = 0.22f;
    camShakeMag_   = shakeMag;
}

void HitFeel::TriggerImpactFx(const Vector3& worldPos){
    fxTimer_    = 0.4f;
    fxWorldPos_ = worldPos;
}

// ヒットストップ（リアルなフレームで数える。タイムスケールを 0→1 と戻す）
void HitFeel::UpdateHitStop(){
    if ( hitStopTimer_ > 0.0f ) {
        hitStopTimer_ -= 1.0f / 60.0f;
        Time::GetInstance()->SetTimeScale(0.0f);
        if ( hitStopTimer_ <= 0.0f ) { Time::GetInstance()->SetTimeScale(1.0f); }
    }
}

// カメラシェイク：前フレームに足した揺れを引いてから今フレームの揺れを足す（基準位置を汚さない）
void HitFeel::ApplyCameraShake(Camera* camera){
    if ( !camera ) return;
    Vector3 shake { 0.0f, 0.0f, 0.0f };
    if ( camShakeTimer_ > 0.0f ) {
        camShakeTimer_ -= 1.0f / 60.0f;
        if ( camShakeTimer_ < 0.0f ) camShakeTimer_ = 0.0f;
        float m = camShakeMag_ * ( camShakeTimer_ / 0.22f ); // だんだん弱まる
        shake.x = std::sin(camShakeTimer_ * 95.0f) * m;
        shake.y = std::cos(camShakeTimer_ * 120.0f) * m;
    }
    Vector3 pos = camera->GetWorldPosition();
    camera->SetTranslation({ pos.x - camPrevShake_.x + shake.x,
                             pos.y - camPrevShake_.y + shake.y,
                             pos.z - camPrevShake_.z + shake.z });
    camPrevShake_ = shake;
}

// ヒット点中心のポストエフェクト：
//   fxWorldPos_ をスクリーンUVへ投影し、MaskedDistortion(slot0)＝衝撃波、MaskedGlow(slot1)＝閃光を出す。
void HitFeel::UpdateImpactPostEffect(Camera* camera){
    PostEffect* pe = PostEffect::GetInstance();
    const PostEffectType kDist = PostEffectType::MaskedDistortion;
    const PostEffectType kGlow = PostEffectType::MaskedGlow;

    auto disableFx = [&](){
        if ( pe->GetEffectActive(kDist) ) pe->SetEffectActive(kDist, false);
        if ( pe->GetEffectActive(kGlow) ) pe->SetEffectActive(kGlow, false);
        };

    if ( fxTimer_ <= 0.0f || !camera ) { disableFx(); return; }

    const float kDuration = 0.4f;
    fxTimer_ -= 1.0f / 60.0f; // リアル時間で進める（ヒットストップ中も波は走る）
    float progress = 1.0f - ( fxTimer_ / kDuration );
    progress = std::clamp(progress, 0.0f, 1.0f);

    // ワールド位置 → スクリーンUV（行ベクトル規約 v*VP）
    const Matrix4x4& vp = camera->GetViewProjectionMatrix();
    const Vector3& w = fxWorldPos_;
    float cw = w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3];
    if ( cw <= 0.0001f ) { disableFx(); return; } // カメラ後方なら出さない
    float cx = w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0];
    float cy = w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1];
    float uvX = cx / cw * 0.5f + 0.5f;
    float uvY = 1.0f - ( cy / cw * 0.5f + 0.5f );

    // 半径アニメ：歪みは外へ広がる衝撃波、グローはパッと出て消えるパルス
    float t = 1.0f - progress;
    float easeOut = 1.0f - t * t;
    float distRadius = 0.06f + ( 0.55f - 0.06f ) * easeOut;
    float glowRadius = 0.45f * std::sin(progress * 3.14159265f);

    // アスペクト比（正円補正）
    float aspect = 16.0f / 9.0f;
    if ( WindowProc* wp = WindowProc::GetInstance() ) {
        int h = wp->GetClientHeight();
        if ( h > 0 ) aspect = static_cast< float >( wp->GetClientWidth() ) / static_cast< float >( h );
    }

    PostEffectMaskParams mp {};
    mp.slot0X = uvX; mp.slot0Y = uvY; mp.slot0Radius = distRadius; // 歪み(MaskedDistortion)
    mp.slot1X = uvX; mp.slot1Y = uvY; mp.slot1Radius = glowRadius; // グロー(MaskedGlow)
    mp.aspectRatio = aspect;
    pe->SetMaskParams(mp);

    pe->SetEffectActive(kDist, true);
    pe->SetEffectActive(kGlow, true);
}
