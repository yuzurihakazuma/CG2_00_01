#include "Enemy.h"

#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/rail/SplineRail.h"

#include <cmath>

Enemy::Enemy() = default;
Enemy::~Enemy() = default;

void Enemy::Initialize(EnemyType type, int railIndex, float distance, bool patrol){
    type_      = type;
    railIndex_ = railIndex;
    distance_  = distance;
    dir_       = 1.0f;
    patrol_    = patrol;
    alive_     = true;

    // エネミーの種類（タイプ）に応じてパラメータ（半径、移動速度）を設定
    if ( type_ == EnemyType::Zako ) {
        radius_ = 0.6f;
        speed_  = 2.0f; // 雑魚敵は中速移動
    } else if ( type_ == EnemyType::Strong ) {
        radius_ = 0.9f; // 強敵はサイズを少し大きくする
        speed_  = 1.0f; // 強敵はゆっくり移動
    }

    // 見た目：強敵と雑魚で共通で sphere モデルを使用するが、スケールを変更する
    obj_ = Obj3d::Create("sphere");
    if ( obj_ ) {
        obj_->SetScale({ radius_, radius_, radius_ });
        
        // 強敵の場合は視覚的な違いを出すために色を赤色に変更する
        if ( type_ == EnemyType::Strong ) {
            auto model = obj_->GetModel();
            if ( model && model->GetMaterial() ) {
                model->GetMaterial()->color = { 1.0f, 0.4f, 0.4f, 1.0f }; // 赤っぽく着色する
            }
        }
    }

    // 当たり判定用コライダー（球体）の初期設定
    collider_.SetSphere(position_, radius_);
    collider_.SetOwner(this);
}

void Enemy::Update(const std::vector<SplineRail>& rails, float dt){
    if ( !alive_ ) return;
    if ( swallowing_ ) return; // 吸い込み中は TickSwallow が動かすのでレール移動はしない
    if ( railIndex_ < 0 || railIndex_ >= ( int ) rails.size() ) return;
    const SplineRail& rail = rails[railIndex_];
    if ( rail.nodes.size() < 2 ) return;

    const float len = rail.GetLength();

    // パトロールONの敵だけレール上を往復（端で折り返す）。OFFは置いた場所に留まる。
    if ( patrol_ ) {
        distance_ += dir_ * speed_ * dt;
    }
    // レール編集で全長が縮んだ時も範囲内に収める（OFFの敵も対象）
    if ( distance_ > len )  { distance_ = len;  dir_ = -1.0f; }
    if ( distance_ < 0.0f ) { distance_ = 0.0f; dir_ = 1.0f; }

    // 位置：レール面の上に半径ぶん乗せる
    Vector3 p = rail.GetPositionByDistance(distance_);
    position_ = { p.x, p.y + radius_, p.z };

    // 進行方向を向く
    Vector3 tan = rail.GetTangentByDistance(distance_);
    if ( std::abs(tan.x) > 1e-4f || std::abs(tan.z) > 1e-4f ) {
        rotation_.y = std::atan2(tan.x * dir_, tan.z * dir_);
    }

    // 当たり判定と見た目を追従
    collider_.SetCenter(position_);
    if ( obj_ ) {
        obj_->SetTranslation(position_);
        obj_->SetRotation(rotation_);
        obj_->Update();
    }
}

void Enemy::Draw(){
    if ( alive_ && obj_ ) { // 吸い込み中も alive_ は true のまま → 縮む様子を描画する
        obj_->Draw();
    }
}

// 飲み込み開始：今いる場所を起点に、縮みながらプレイヤーへ吸い込まれる。
void Enemy::StartSwallow(){
    if ( swallowing_ || !alive_ ) return;
    swallowing_   = true;
    swallowT_     = 0.0f;
    swallowStart_ = position_;
}

// 吸い込み中の更新：プレイヤーの口元へ寄りながらスケールを 1→0 へ縮める。
void Enemy::TickSwallow(const Vector3& playerPos, float dt){
    if ( !swallowing_ ) return;
    const float dur = 0.35f;
    swallowT_ += dt;
    float t = swallowT_ / dur;
    if ( t > 1.0f ) t = 1.0f;

    // 口元へ近づく（少し上）
    Vector3 mouth = { playerPos.x, playerPos.y + 0.5f, playerPos.z };
    position_ = {
        swallowStart_.x + ( mouth.x - swallowStart_.x ) * t,
        swallowStart_.y + ( mouth.y - swallowStart_.y ) * t,
        swallowStart_.z + ( mouth.z - swallowStart_.z ) * t
    };

    float s = radius_ * ( 1.0f - t ); // どんどん小さく
    collider_.SetCenter(position_);
    if ( obj_ ) {
        obj_->SetTranslation(position_);
        obj_->SetScale({ s, s, s });
        obj_->SetRotation(rotation_);
        obj_->Update();
    }

    if ( t >= 1.0f ) { // 吸い込み完了
        swallowing_ = false;
        consumed_   = true;
        alive_      = false;
    }
}
