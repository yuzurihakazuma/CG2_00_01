#include "Enemy.h"

#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/obj/Obj3dCommon.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/rail/SplineRail.h"
#include "game/stage/BlockSystem.h"

#include <algorithm>
#include <cmath>

namespace {
    // フワリン（空中）がレール線から浮く高さ(m)。踏みつけはジャンプで届く範囲に
    constexpr float kAirHoverHeight = 1.4f;

    // 種類ごとの体の高さ(m)。ブロック貫通チェックの「体の高さ帯」に使う
    float BodyHeightOf(EnemyType type){
        switch ( type ) {
        case EnemyType::Strong: return 1.12f; // カミバナ
        case EnemyType::Air:    return 0.70f; // フワリン
        default:                return 0.74f; // ドングリン
        }
    }
}

Enemy::Enemy() = default;
Enemy::~Enemy() = default;

void Enemy::Initialize(EnemyType type, int railIndex, float distance, bool patrol,
                       float patrolMin, float patrolMax){
    patrolMin_ = patrolMin;
    patrolMax_ = patrolMax;
    type_      = type;
    railIndex_ = railIndex;
    distance_  = distance;
    dir_       = 1.0f;
    patrol_    = patrol;
    alive_     = true;

    // 種類ごとのパラメータとモデル（リグ+クリップ入りglb。原点=足元 / +Z前方）
    const char* modelName = nullptr;
    const char* modelFile = nullptr;
    const char* startClip = nullptr;
    if ( type_ == EnemyType::Zako ) {           // 地上「ドングリン」（高さ約0.74）
        radius_ = 0.5f;
        speed_  = 2.0f;
        modelName = "enemyGround"; modelFile = "enemy_ground.glb";
        startClip = patrol_ ? "Walk" : "Idle";
    } else if ( type_ == EnemyType::Strong ) {  // 植物「カミバナ」（高さ約1.12。基本その場）
        radius_ = 0.6f;
        speed_  = 1.0f;
        modelName = "enemyPlant"; modelFile = "enemy_plant.glb";
        startClip = "Idle";
    } else {                                    // 空中「フワリン」（高さ約0.70。浮遊）
        radius_ = 0.45f;
        speed_  = 1.6f;
        modelName = "enemyAir"; modelFile = "enemy_air.glb";
        startClip = "Fly";
    }

    // 見た目：リグ付きモデルを生成（未登録などで失敗したら従来のsphereへフォールバック）
    skinnedObj_ = SkinnedObj3d::Create(modelName, "resources/enemy", modelFile);
    if ( skinnedObj_ ) {
        // 環境マップは必ず束縛する（未設定だとslot0の2Dが刺さりGPU検証で落ちる）
        skinnedObj_->SetEnvironmentMap(Obj3dCommon::GetInstance()->GetEnvironmentTextureSrvIndex());
        skinnedObj_->LoadClips("resources/enemy", modelFile);
        skinnedObj_->SetClip(startClip, true);
        // 個体ごとにアニメの位相をずらす（並んだ敵が同じタイミングで動かないように）
        skinnedObj_->SetAnimationTime(std::fmod(distance_ * 0.37f, 1.0f));
    } else {
        obj_ = Obj3d::Create("sphere");
        if ( obj_ ) {
            obj_->SetScale({ radius_, radius_, radius_ });
            if ( type_ != EnemyType::Zako ) {
                auto model = obj_->GetModel();
                if ( model && model->GetMaterial() ) {
                    model->GetMaterial()->color = { 1.0f, 0.4f, 0.4f, 1.0f };
                }
            }
        }
    }

    // 当たり判定用コライダー（球体）の初期設定
    collider_.SetSphere(position_, radius_);
    collider_.SetOwner(this);
}

void Enemy::Update(const std::vector<SplineRail>& rails, const Vector3& playerPos, float dt,
                   const BlockSystem* blocks){
    if ( !alive_ ) return;
    if ( swallowing_ ) return; // 吸い込み中は TickSwallow が動かすのでレール移動はしない
    if ( railIndex_ < 0 || railIndex_ >= ( int ) rails.size() ) return;
    const SplineRail& rail = rails[railIndex_];
    if ( rail.nodes.size() < 2 ) return;

    const float railLength = rail.GetLength();

    // パトロールONの敵だけレール上を往復（端で折り返す）。OFFは置いた場所に留まる。
    if ( patrol_ && dt > 0.0f ) {
        if ( turnCooldown_ > 0.0f ) { turnCooldown_ -= dt; }
        float nextDist = std::clamp(distance_ + dir_ * speed_ * dt, 0.0f, railLength);
        // 進行方向にブロックがあれば引き返す（プレイヤーと同じ BlockedAt 判定＝貫通しない）。
        //   体の高さ帯で見るので、フワリン（浮遊）は低いブロックの上をそのまま飛び越えられる
        bool hitBlock = false;
        if ( blocks ) {
            const float hoverY = ( type_ == EnemyType::Air ) ? kAirHoverHeight : 0.0f;
            Vector3 nextRailPos = rail.GetPositionByDistance(nextDist);
            float bodyBottom = nextRailPos.y + hoverY + 0.05f; // 足元すれすれは接地扱いにしない
            float bodyTop    = nextRailPos.y + hoverY + BodyHeightOf(type_);
            float blockMin = 0.0f, blockMax = 0.0f;
            hitBlock = blocks->BlockedAt(railIndex_, nextDist, bodyBottom, bodyTop, &blockMin, &blockMax);
        }
        if ( hitBlock ) {
            if ( turnCooldown_ <= 0.0f ) {
                dir_ = -dir_;          // 壁に当たった：向きを変えて引き返す
                turnCooldown_ = 0.3f;  // 両側が塞がっている時にクルクル震えないための猶予
            }
            // その場に留まる（めり込まない）
        } else {
            distance_ = nextDist;
        }
    }
    // 往復の折り返し範囲：巡回範囲が指定されていればそこで折り返す（-1=レール全体）。
    // レール編集で全長が縮んだ時も範囲内に収める（OFFの敵も対象）
    float lo = ( patrol_ && patrolMin_ >= 0.0f ) ? ( std::min )( patrolMin_, railLength ) : 0.0f;
    float hi = ( patrol_ && patrolMax_ >= 0.0f ) ? std::clamp(patrolMax_, lo, railLength) : railLength;
    if ( distance_ > hi ) { distance_ = hi; dir_ = -1.0f; }
    if ( distance_ < lo ) { distance_ = lo; dir_ = 1.0f; }

    // 位置：モデルの原点は足元。フワリン（空中）はレールから浮かせる。
    //   コライダー中心(position_)は従来どおり「足元＋半径」＝踏みつけ/卵の判定は同じ感覚のまま
    const float hover = ( type_ == EnemyType::Air ) ? kAirHoverHeight : 0.0f;
    Vector3 railPos = rail.GetPositionByDistance(distance_);
    Vector3 footPos = { railPos.x, railPos.y + hover, railPos.z };
    position_ = { footPos.x, footPos.y + radius_, footPos.z };

    // 進行方向を向く
    Vector3 tangent = rail.GetTangentByDistance(distance_);
    if ( std::abs(tangent.x) > 1e-4f || std::abs(tangent.z) > 1e-4f ) {
        rotation_.y = std::atan2(tangent.x * dir_, tangent.z * dir_);
    }

    // --- 種類ごとのアニメ制御 ---
    if ( skinnedObj_ ) {
        if ( type_ == EnemyType::Zako ) {
            // 歩いている時だけ Walk、それ以外は Idle（SetClipは同名なら何もしないので毎フレーム呼んでよい）
            skinnedObj_->SetClip(patrol_ ? "Walk" : "Idle", true);
        } else if ( type_ == EnemyType::Strong && dt > 0.0f ) {
            // カミバナ：プレイヤーが近づくと噛みつく（終わったらIdleへ戻る）。近くでは体も向ける
            float dx = playerPos.x - position_.x;
            float dz = playerPos.z - position_.z;
            float distXZ = std::sqrt(dx * dx + dz * dz);
            float dy = std::abs(playerPos.y - position_.y);
            if ( biteTimer_ > 0.0f ) {
                biteTimer_ -= dt;
                if ( biteTimer_ <= 0.0f ) { skinnedObj_->SetClip("Idle", true); }
            } else if ( distXZ < 2.6f && dy < 2.0f ) {
                skinnedObj_->SetClip("Bite", false);
                skinnedObj_->SetAnimationTime(0.0f);
                biteTimer_ = 1.5f; // Bite(24f)＋ひと呼吸
            }
            if ( distXZ < 4.0f && ( std::abs(dx) > 1e-4f || std::abs(dz) > 1e-4f ) ) {
                rotation_.y = std::atan2(dx, dz); // プレイヤーの方を向く（鉢は不動でも上体が向く）
            }
        }
    }

    // 当たり判定と見た目を追従（モデルは足元原点なので footPos に置く）
    collider_.SetCenter(position_);
    if ( skinnedObj_ ) {
        skinnedObj_->SetTranslation(footPos);
        skinnedObj_->SetRotation(rotation_);
        skinnedObj_->Update();
    } else if ( obj_ ) {
        obj_->SetTranslation(position_);
        obj_->SetRotation(rotation_);
        obj_->Update();
    }
}

void Enemy::Draw(){
    if ( !alive_ || visualHidden_ ) return; // 吸い込み中も alive_ は true のまま → 縮む様子を描画する
    if ( skinnedObj_ )   { skinnedObj_->Draw(); }
    else if ( obj_ )     { obj_->Draw(); }
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
    const float duration = 0.06f; // 食ったら即回収（掴んだ瞬間ほぼその場で消えてお腹に入る）
    swallowT_ += dt;
    float t = swallowT_ / duration;
    if ( t > 1.0f ) t = 1.0f;

    // 口元へ近づく（少し上）
    Vector3 mouth = { playerPos.x, playerPos.y + 0.5f, playerPos.z };
    position_ = {
        swallowStart_.x + ( mouth.x - swallowStart_.x ) * t,
        swallowStart_.y + ( mouth.y - swallowStart_.y ) * t,
        swallowStart_.z + ( mouth.z - swallowStart_.z ) * t
    };

    collider_.SetCenter(position_);
    if ( skinnedObj_ ) {
        float shrink = 1.0f - t; // リグ付きモデルは基準スケール1から縮める
        skinnedObj_->SetTranslation({ position_.x, position_.y - radius_ * shrink, position_.z }); // 足元原点ぶん下げる
        skinnedObj_->SetScale({ shrink, shrink, shrink });
        skinnedObj_->SetRotation(rotation_);
        skinnedObj_->Update();
    } else if ( obj_ ) {
        float shrinkScale = radius_ * ( 1.0f - t ); // どんどん小さく
        obj_->SetTranslation(position_);
        obj_->SetScale({ shrinkScale, shrinkScale, shrinkScale });
        obj_->SetRotation(rotation_);
        obj_->Update();
    }

    if ( t >= 1.0f ) { // 吸い込み完了
        swallowing_ = false;
        consumed_   = true;
        alive_      = false;
    }
}
