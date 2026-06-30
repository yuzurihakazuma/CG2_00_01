#include "game/egg/Egg.h"
#include "engine/3d/obj/Obj3d.h"
#include <algorithm>
#include <cmath>

Egg::Egg(const Vector3& birthPos) : pos_(birthPos), target_(birthPos) {}
Egg::~Egg() = default; // unique_ptr<Obj3d> のため cpp 側で定義

void Egg::AttachVisual(std::unique_ptr<Obj3d> obj, const Vector3& baseScale){
    obj_ = std::move(obj);
    baseScale_ = baseScale;
}

void Egg::Update(float dt){
    switch ( state_ ) {
    case EggState::Held: {
        // 生まれた所(プレイヤー付近)から目標スロットへ滑らかに寄る＝「生まれて後ろに並ぶ＆追従」。
        //   状態を分けず、毎フレーム目標へイージングで近づくだけ（生まれた直後も移動中も同じ処理）。
        float k = ( std::min )( 8.0f * dt, 1.0f ); // ()でwindows.hのminマクロ展開を防ぐ
        pos_.x += ( target_.x - pos_.x ) * k;
        pos_.y += ( target_.y - pos_.y ) * k;
        pos_.z += ( target_.z - pos_.z ) * k;
        break;
    }
    case EggState::Flying:
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

    // --- 見た目の更新 ---
    if ( bornTimer_ > 0.0f ) { bornTimer_ -= dt; if ( bornTimer_ < 0.0f ) bornTimer_ = 0.0f; }
    spinAngle_ += dt * 3.0f;

    if ( obj_ ) {
        // 生まれた直後は小さく→通常サイズへ膨らむ（ポンッと出る演出）
        float born = 1.0f - ( bornTimer_ / 0.25f ); // 0→1
        float s = 0.3f + 0.7f * std::clamp(born, 0.0f, 1.0f);
        obj_->SetTranslation(pos_);
        obj_->SetScale({ baseScale_.x * s, baseScale_.y * s, baseScale_.z * s });
        obj_->SetRotation({ 0.0f, spinAngle_, 0.0f });
        obj_->Update();
    }
}

void Egg::Draw() const{
    if ( obj_ && !IsDead() ) { obj_->Draw(); }
}

// Held → Flying：指定方向へ初速を与える。
void Egg::Throw(const Vector3& dir, float speed){
    if ( state_ != EggState::Held ) return;
    vel_      = { dir.x * speed, dir.y * speed, dir.z * speed };
    flyTimer_ = 1.5f;
    state_    = EggState::Flying;
}

// → Broken：割れて、少ししてから消える。
void Egg::Break(){
    if ( state_ == EggState::Broken ) return;
    state_       = EggState::Broken;
    brokenTimer_ = 0.3f;
    justBroke_   = true; // 割れた瞬間（EggSystem が拾って星を弾けさせる）
}
