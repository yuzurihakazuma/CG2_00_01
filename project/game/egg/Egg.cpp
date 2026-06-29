#include "game/egg/Egg.h"

void Egg::Update(float dt){
    switch ( state_ ) {
    case EggState::Held:
        // お腹/後ろで待機。位置は EggSystem がヨッシーに追従させるのでここでは何もしない。
        break;

    case EggState::Flying:
        // 飛行：今は等速直線（重力や見た目は実体化の手順で足す）。
        pos_.x += vel_.x * dt;
        pos_.y += vel_.y * dt;
        pos_.z += vel_.z * dt;
        flyTimer_ -= dt;
        if ( flyTimer_ <= 0.0f ) { Break(); } // 何にも当たらず時間切れ → 割れる
        break;

    case EggState::Broken:
        if ( brokenTimer_ > 0.0f ) { brokenTimer_ -= dt; }
        break;
    }
}

// Held → Flying：指定方向へ初速を与える。
void Egg::Throw(const Vector3& dir, float speed){
    if ( state_ != EggState::Held ) return;
    vel_   = { dir.x * speed, dir.y * speed, dir.z * speed };
    flyTimer_ = 1.5f; // この時間だけ飛べる（敵に当たらなければ割れる）
    state_ = EggState::Flying;
}

// → Broken：割れて、少ししてから消える。
void Egg::Break(){
    if ( state_ == EggState::Broken ) return;
    state_       = EggState::Broken;
    brokenTimer_ = 0.3f;
}
