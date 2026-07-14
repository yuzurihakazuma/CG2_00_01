#include "game/player/SwallowAbility.h"

#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/egg/EggSystem.h"
#include "game/combat/HitFeel.h"
#include "engine/audio/AudioManager.h"
#include "engine/base/Input.h"
#include "engine/math/VectorMath.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/graphics/DebugDraw.h"
#include "engine/sdf/SDFVolumeObject.h"

#include <cmath>
#include <algorithm>

using namespace VectorMath;

namespace {
    const float kShootDuration     = 0.12f; // 舌が伸びきるまでの秒数（ここが「動作」の見せ場）
    const float kRetractDuration   = 0.35f; // 捕獲時の引き込み秒数。Enemy::TickSwallow の dur と同じ値にする
    const float kMissRetractDuration = 0.15f; // 空振り時の引っ込み秒数（捕獲より短く、キビキビ戻す）
    const float kTongueHalfWidth   = 0.09f; // 舌の太さ(半幅)
    const Vector4 kTongueColor     = { 1.0f, 0.42f, 0.5f, 1.0f }; // ヨッシー舌のピンク
    const Vector4 kEatFxColor      = { 0.98f, 0.96f, 0.86f, 1.0f }; // カラーボリューム無い時の予備色
    const Vector4 kCatchableColor  = { 1.0f, 0.55f, 0.7f, 0.9f };  // 「今なら捕まえられる」ハイライト（舌と同系色）
}

SwallowAbility::SwallowAbility() = default;
SwallowAbility::~SwallowAbility() = default;

// 捕まえた敵をSDFで溶かして消す演出のセットアップ（enemyBall.sdf3d を先読み）
void SwallowAbility::InitializeEatFx(ID3D12GraphicsCommandList* commandList){
    eatFx_ = std::make_unique<SDFVolumeObject>();
    if ( !eatFx_->Load("resources/sdf3d/enemyBall.sdf3d", commandList) ) {
        eatFx_.reset(); // 読めなければ演出なし（従来の縮小アニメへ自動フォールバック）
        return;
    }
    eatFx_->SetColor(kEatFxColor);
}

void SwallowAbility::Reset(){
    state_       = State::Idle;
    target_      = nullptr;
    phaseT_      = 0.0f;
    eatFxActive_ = false;
}

// E=舌を伸ばして捕まえる / 左Ctrl=産卵 / F=吐き出す。
//   敵を知るのは EnemyManager、卵を知るのは EggSystem。ここは入力・対象選択・舌の演出を行う。
void SwallowAbility::Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel,
                            Camera* camera, float dt){
    Input* input = Input::GetInstance();
    Vector3 playerPos = player.GetPosition();
    Vector3 mouth = { playerPos.x, playerPos.y + 0.5f, playerPos.z }; // Enemy::TickSwallow と同じ口位置

    // クールタイムの経過（成功した時だけ充填される）
    if ( cooldownTimer_ > 0.0f ) { cooldownTimer_ -= dt; }

    // 現在の水平向き（毎フレーム取り直す。ターゲット探索と空振り中の狙い方向の両方で使う）
    float yaw = player.GetRotation().y;
    Vector3 facing = { std::sin(yaw), 0.0f, std::cos(yaw) };

    // 舌が届く範囲＆前方にいる敵を探す（E押下時の判定と、Idle中のプレビュー表示の両方で使う）
    auto findCatchable = [&]() -> Enemy* {
        Enemy* found = nullptr;
        float bestDist = swallowReach_; // 舌の届く範囲（ノードエディタから調整可）
        for ( auto& e : enemies.GetEnemies() ) {
            if ( !e->IsAlive() ) continue;
            Vector3 to = e->GetPosition() - playerPos;
            float d = Length(to);
            if ( d >= bestDist ) continue;

            // 前方チェック：水平方向の内積で「ほぼ横〜後ろ」を弾く（cos≒0.2 → 前方約±78°）
            float horiz = std::sqrt(to.x * to.x + to.z * to.z);
            if ( horiz > 1e-4f && ( to.x * facing.x + to.z * facing.z ) / horiz < 0.2f ) continue;

            bestDist = d; found = e.get();
        }
        return found;
        };

    // --- E：舌を伸ばす（Idle中のみ受け付ける。どこでも出せる＝対象がいなくても必ず伸びる）---
    //   届く範囲＆前方に敵がいれば、その位置へホーミングして捕まえる。
    //   いなければ正面へ届く距離ぶん伸びて、そのまま空振りで引っ込む。
    //   （以前は全方位の最近傍だったため、背後の敵まで吸えて違和感があった）
    if ( state_ == State::Idle ) {
        // 「今Eを押したら捕まえられる敵」を毎フレーム可視化する（脈動するリング。分かりづらさ対策）。
        //   クールタイム中は実際には捕まえられないので出さない
        if ( cooldownTimer_ <= 0.0f ) {
            Enemy* preview = findCatchable();
            if ( preview ) {
                highlightPulseT_ += dt;
                float pulse = 1.0f + 0.12f * std::sin(highlightPulseT_ * 8.0f);
                DebugDraw::GetInstance()->Sphere(preview->GetPosition(),
                    ( preview->GetRadius() + 0.18f ) * pulse, kCatchableColor, 16);
            } else {
                highlightPulseT_ = 0.0f; // 次に出た時パッと大きくならないようリセット
            }
        }

        if ( input->Triggerkey(DIK_E) && cooldownTimer_ <= 0.0f ) {
            target_ = findCatchable(); // 見つからなければ nullptr のまま＝空振りとして続行
            state_  = State::Shooting;
            phaseT_ = 0.0f;
            // 移動はロックしない：ヨッシー同様、走りながらでも舌を出せる
            // （口の位置は毎フレーム player.GetPosition() から取り直すので動いていても破綻しない）
            cooldownTimer_ = swallowCooldown_;
            AudioManager::GetInstance()->PlayWave("resources/se/eggThrow.wav", false, 0.35f); // 舌を伸ばす音（流用）
        }
    }
    // --- Shooting：狙い位置（敵 or 空振りの正面）まで舌が一直線に伸びる ---
    //   空振り時は「現在の口位置＋現在の向き」から毎フレーム再計算する。
    //   固定の到達点へ向かわせないことで、ジャンプで上下しても斜めにねじれず、
    //   途中で振り向けば舌もそのまま一緒に振り向く。
    else if ( state_ == State::Shooting ) {
        if ( target_ && !target_->IsAlive() ) {
            // 敵が別要因で消えた（踏まれた等）→ 以後は空振り扱いで続行
            target_ = nullptr;
        }
        Vector3 aimTip = target_ ? target_->GetPosition() : ( mouth + facing * swallowReach_ );

        phaseT_ += dt;
        float t = ( std::min )( phaseT_ / kShootDuration, 1.0f );
        Vector3 tip = mouth + ( aimTip - mouth ) * t;
        UpdateTongueMesh(mouth, tip, camera);

        if ( t >= 1.0f ) {
            if ( target_ ) {
                // 掴んだ瞬間：位置移動は Enemy 側の既存アニメ(TickSwallow)へそのまま委ねるが、
                // 見た目は実体メッシュを隠してSDF溶解に差し替える（読込済みの時だけ）。
                // target_ はここで手放す（EnemyManager が完了時に消すため、以後ダングリングになりうる）
                grabPos_   = target_->GetPosition();
                eatRadius_ = target_->GetRadius();
                eatFxActive_ = ( eatFx_ != nullptr );
                if ( eatFxActive_ ) { target_->SetVisualHidden(true); }
                target_->StartSwallow();
                AudioManager::GetInstance()->PlayWave("resources/se/eggHit.wav", false, 0.5f); // 捕獲音（流用）
                hitFeel.Trigger(0.03f, 0.12f); // 捕まえた手応え
                retractDuration_ = kRetractDuration;
                retractCaught_   = true;
            } else {
                // 空振り：掴めなかったのでそのまま引っ込めるだけ
                retractDuration_ = kMissRetractDuration;
                retractCaught_   = false;
            }
            state_  = State::Retracting;
            phaseT_ = 0.0f;
            target_ = nullptr;
        }
    }
    // --- Retracting：舌の先端が口元へ戻る ---
    //   捕獲時：Enemy::TickSwallow と同じ式（掴んだ位置→口）で、縮む敵にぴったり追従。
    //   空振り時：捕獲と違って追う対象が無いので、Shootingと同じく現在の向きへ毎フレーム
    //   再計算する（＝ジャンプでねじれない／振り向けば一緒に振り向く）。
    else {
        phaseT_ += dt;
        float t = ( std::min )( phaseT_ / retractDuration_, 1.0f );
        Vector3 tip = retractCaught_
            ? grabPos_ + ( mouth - grabPos_ ) * t
            : mouth + facing * ( swallowReach_ * ( 1.0f - t ) );
        UpdateTongueMesh(mouth, tip, camera);

        // 捕獲した敵：実体の代わりに、舌の先端と同じ位置でSDFボールを溶かしながら引き込む
        if ( retractCaught_ && eatFxActive_ && eatFx_ ) {
            float melt = t * t; // 2次イン：最初はゆっくり崩れ、後半で一気に消える
            eatFx_->SetScale(eatRadius_ * 2.0f); // 直径。EggSystem/CombatSystem と同じ換算
            eatFx_->SetTranslation(tip);
            eatFx_->SetErode(eatRadius_ * melt); // eatRadius_ に達すると完全に溶けきる
            eatFx_->SetColor(kEatFxColor);
            eatFx_->Update();
        }

        if ( t >= 1.0f ) {
            state_ = State::Idle;
        }
    }

    // --- 左Ctrl（しゃがみ）：お腹の敵を1匹、後ろに卵として産む ---
    if ( input->Triggerkey(DIK_LCONTROL) ) {
        float yaw = player.GetRotation().y;
        Vector3 behind = { playerPos.x - std::sin(yaw) * 0.8f, playerPos.y + 0.3f, playerPos.z - std::cos(yaw) * 0.8f };
        if ( eggs.LayEgg(behind) ) {
            eggs.SpawnLayFx(behind);        // 産まれた合図（白＆緑がぽわっと）
            hitFeel.Trigger(0.03f, 0.08f);
        }
    }

    // --- F：お腹の敵を吐き出す（プレイヤーの向いている方向へ発射。敵にぶつけて倒せる）---
    if ( input->Triggerkey(DIK_F) ) {
        float yaw = player.GetRotation().y;
        Vector3 facing = { std::sin(yaw), 0.0f, std::cos(yaw) };
        Vector3 spitMouth = { playerPos.x + facing.x * 0.6f, playerPos.y + 0.5f, playerPos.z + facing.z * 0.6f };
        if ( eggs.SpitOut(spitMouth, facing) ) {
            hitFeel.Trigger(0.03f, 0.1f);   // 吐き出しの軽い手応え
            AudioManager::GetInstance()->PlayWave("resources/se/eggThrow.wav", false, 0.5f); // 発射音
        }
    }
}

// 舌の見た目を口(mouth)〜先端(tip)の十字リボン（2枚の直交クアッド）として書き直す。
//   ワールド座標へ直接焼く動的メッシュ（RoadMesh/RailField と同じ方式）。
void SwallowAbility::UpdateTongueMesh(const Vector3& mouth, const Vector3& tip, Camera* camera){
    // 遅延生成：初めて必要になった時に固定容量の動的メッシュを1回だけ確保する
    if ( !tongueModel_ ) {
        tongueModel_ = std::make_unique<Model>();
        tongueModel_->InitializeDynamic(ModelManager::GetInstance()->GetModelCommon(),
                                        16, 24, "resources/block/white1x1.png");
        if ( tongueModel_->GetMaterial() ) {
            tongueModel_->GetMaterial()->color = kTongueColor;
            tongueModel_->GetMaterial()->enableLighting = 0; // 平坦な発色（薄いリボンなので陰影は不要）
        }
        tongueObj_ = std::make_unique<Obj3d>();
        tongueObj_->Initialize(tongueModel_.get());
        tongueObj_->SetPipelineType(PipelineType::Object3D_CullNone); // 裏から見ても消えない
        tongueObj_->SetTranslation({ 0.0f, 0.0f, 0.0f }); // 頂点はワールド座標で焼くので平行移動はしない
    }

    Vector3 dir = tip - mouth;
    float len = Length(dir);
    if ( len < 1e-4f ) {
        static const std::vector<Model::VertexData> kEmptyV;
        static const std::vector<uint32_t> kEmptyI;
        tongueModel_->UpdateMesh(kEmptyV, kEmptyI);
    } else {
        dir = dir / len;

        // 進行方向に直交する2軸（十字リボンの幅方向）。ほぼ真上/真下でも破綻しない保険つき
        Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
        Vector3 right = Cross(worldUp, dir);
        if ( Length(right) < 1e-4f ) { right = Cross({ 0.0f, 0.0f, 1.0f }, dir); }
        right = Normalize(right);
        Vector3 up = Normalize(Cross(dir, right));

        Model::ModelData data;
        auto quad = [&](const Vector3& widthAxis, const Vector3& n){
            uint32_t base = static_cast<uint32_t>( data.vertices.size() );
            Vector3 corners[4] = {
                mouth - widthAxis * kTongueHalfWidth, mouth + widthAxis * kTongueHalfWidth,
                tip   + widthAxis * kTongueHalfWidth, tip   - widthAxis * kTongueHalfWidth,
            };
            const Vector2 uv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
            for ( int k = 0; k < 4; ++k ) {
                Model::VertexData v {};
                v.position = { corners[k].x, corners[k].y, corners[k].z, 1.0f };
                v.normal = n;
                v.texcoord = uv[k];
                data.vertices.push_back(v);
            }
            data.indices.push_back(base); data.indices.push_back(base + 1); data.indices.push_back(base + 2);
            data.indices.push_back(base); data.indices.push_back(base + 2); data.indices.push_back(base + 3);
        };
        quad(right, up); // 縦リボン
        quad(up, right); // 横リボン（2枚で十字断面 → どの角度から見ても線に見えない）

        tongueModel_->UpdateMesh(data.vertices, data.indices);
    }

    tongueObj_->SetCamera(camera);
    tongueObj_->Update();
}

void SwallowAbility::Draw() const{
    if ( state_ != State::Idle && tongueObj_ ) { tongueObj_->Draw(); }
}

// 捕獲した敵のSDF溶解演出の描画（専用PSOのため、シーンMRTパスの最後で呼ばれる）
void SwallowAbility::DrawEatFx(ID3D12GraphicsCommandList* commandList) const{
    if ( state_ == State::Retracting && retractCaught_ && eatFxActive_ && eatFx_ ) {
        eatFx_->Draw(commandList);
    }
}
