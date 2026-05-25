#include "game/player/Player.h"

#include "Engine/Base/Input.h"
#include "Engine/Camera/Camera.h"
#include "engine/math/VectorMath.h"
#include "externals/imgui/imgui.h"
#include "externals/nlohmann/json.hpp"
#include "engine/particle/GPUParticleManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <particle/StunEffectManager.h>
#include "engine/postEffect/PostEffect.h"


using namespace VectorMath;
using json = nlohmann::json;

namespace {
bool ContainsIgnoreCase(const std::string& text, const std::string& pattern) {
    if (pattern.empty()) {
        return true;
    }

    std::string lowerText = text;
    std::string lowerPattern = pattern;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerPattern.begin(), lowerPattern.end(), lowerPattern.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return lowerText.find(lowerPattern) != std::string::npos;
}

bool IsNearlyZero(float value, float epsilon = 0.0001f) {
    return std::fabs(value) <= epsilon;
}

bool IsNearlyZeroVector(const Vector3& value, float epsilon = 0.0001f) {
    return IsNearlyZero(value.x, epsilon) &&
        IsNearlyZero(value.y, epsilon) &&
        IsNearlyZero(value.z, epsilon);
}

void CopyText(char* dst, size_t size, const std::string& src) {
    if (size == 0) {
        return;
    }

    strncpy_s(dst, size, src.c_str(), _TRUNCATE);
}

json Vector3ToJson(const Vector3& value) {
    return json::array({ value.x, value.y, value.z });
}

Vector3 JsonToVector3(const json& value) {
    if (!value.is_array() || value.size() < 3) {
        return { 0.0f, 0.0f, 0.0f };
    }

    return {
        value[0].get<float>(),
        value[1].get<float>(),
        value[2].get<float>()
    };
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Vector3 LerpVector3(const Vector3& a, const Vector3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}
}

void Player::Initialize() {
    pos_ = { 0.0f, 0.0f, 0.0f };
    rot_ = { 0.0f, 0.0f, 0.0f };
    scale_ = { 1.0f, 1.0f, 1.0f };

    isDodging_ = false;
    dodgeTimer_ = 0;
    dodgeInvincibleTimer_ = 0;
    dodgeCooldownTimer_ = 0;
    dodgeDirection_ = { 0.0f, 0.0f, 0.0f };

    isActionLocked_ = false;  // 行動ロック初期化
    actionLockTimer_ = 0;

    level_ = 1;               // レベル初期化
    exp_ = 0;                 // 経験値初期化
    nextLevelExp_ = 3;        // 次レベル必要経験値初期化

    cost_ = 5 ;                // 現在コスト初期化
    maxCost_ = 5;             // 最大コスト初期化
    costRecoveryTimer_ = 0;   // コスト回復タイマー初期化
    costRecoveryInterval_ = 120; // コスト回復速度初期化

    maxCost_ = 5; // 初期最大コスト
    cost_ = maxCost_;
    costRecoveryMultiplier_ = 1.0f; // バフをリセット

    hp_ = 10;                  // 現在HP初期化
    maxHp_ = 10;               // 最大HP初期化
    isDead_ = false;          // 死亡状態リセット
    deathAnimationTimer_ = 0; // 死亡演出タイマー初期化
    poseBlendTimer_ = 0;      // ポーズ補間タイマー初期化
    poseBlendJoints_.clear(); // 補間中ジョイント情報をクリア

    isInvincible_ = false;    // 無敵状態リセット
    invincibleTimer_ = 0;     // 無敵時間リセット

    isHit_ = false;           // 被弾状態リセット
    hitTimer_ = 0;            // 被弾時間リセット

    // ノックバック初期化
    isKnockback_ = false;                       // ノックバック状態リセット
    knockbackTimer_ = 0;                        // ノックバック時間リセット
    knockbackVelocity_ = { 0.0f, 0.0f, 0.0f }; // ノックバック速度リセット

    // 速度
    speedMultiplier_ = 1.0f;
    speedBuffTimer_ = 0;

    // シールド
    isShieldActive_ = false;
    shieldHitCount_ = 0;

    isStunned_ = false;
    stunTimer_ = 0;
    stunResistTimer_ = 0;

    afterimageLife_ = 10;           //  寿命を20→10に短縮（回避が終わる前に消える）
    afterimageSpawnInterval_ = 5;   //  発生間隔を少し広げて、重なりすぎを防


    // プレイヤーのアニメーションモデルを生成
    // ※ モデル名とファイル名は GamePlayScene 側の LoadModel と合わせる
    model_ = SkinnedObj3d::Create(
        "player",
        "resources/player",
        "player.gltf"
    );

    if (model_) {
        model_->SetName("Player");
        model_->SetTranslation(pos_);
        model_->SetRotation(rot_);
        model_->SetScale(scale_);
        model_->SetLoopAnimation(true);

        if (camera_) {
            model_->SetCamera(camera_);
        }

        // ポーズ編集GUIの初期状態を準備する
        RefreshJointList();
        LoadPoseFile();
        ApplyPoseByName(idlePoseNameBuffer_);
    }
    for (int i = 0; i < maxAfterimages_; i++) {
        Afterimage ai;
        ai.obj = SkinnedObj3d::Create("player", "resources/player", "player.gltf");
        if (ai.obj) {
            ai.obj->SetLoopAnimation(false);
            if (camera_) {
                ai.obj->SetCamera(camera_);
            }
        }
        ai.isActive = false;
        afterimages_.push_back(std::move(ai));
    }

    // DodgeParticle は Initialize で1回だけ生成する（Update で毎フレーム再生成しない）
    dodgeParticle_ = std::make_unique<DodgeParticleEffect>();
    dodgeParticle_->Initialize();
}

void Player::Update() {
   
    // ---- ポストエフェクト（Player 管轄分）----
    {
        auto* pe = PostEffect::GetInstance();

        // 死亡フラッシュタイマー更新
        if ( deathFlashTimer_ > 0 ) deathFlashTimer_--;
        bool isDeathFlash = deathFlashTimer_ > 0;

        // 死亡時 → グレースケール（白→赤フラッシュ中はColorTintを優先してグレースケールを抑制）
        pe->SetEffectActive(PostEffectType::Grayscale, isDead_ && !isDeathFlash);

        // スピードバフ中 → ラジアルブラー（回避中は視認性のため無効）
        bool hasSpeedBuff = speedMultiplier_ > 1.0f && speedBuffTimer_ > 0;
        pe->SetEffectActive(PostEffectType::RadialBlur, hasSpeedBuff);
        if ( hasSpeedBuff ) {
            pe->SetRadialBlurStrength(0.35f);
        } else {
            pe->SetRadialBlurStrength(1.0f);
        }

        // シールドフラッシュタイマー更新
        if ( shieldFlashTimer_ > 0 ) shieldFlashTimer_--;

        // 低HP（残り3以下）→ 赤いビネット / 被弾直後 → 赤フラッシュ（死亡時はどちらも消す）
        bool isLowHp    = !isDead_ && hp_ > 0 && hp_ <= 3;
        bool isHitFlash = !isDead_ && isHit_ && hitTimer_ > hitDuration_ - 4;
        bool isShieldFlash = shieldFlashTimer_ > 0;
        pe->SetEffectActive(PostEffectType::Vignetting, isLowHp || isHitFlash || isShieldFlash);
        if ( isShieldFlash ) {
            // 青いビネット（シールドが弾いた瞬間）
            float ratio = static_cast<float>(shieldFlashTimer_) / static_cast<float>(shieldFlashDuration_);
            pe->SetVignetteParams(0.6f + ratio * 0.5f, 0.15f, 0.6f, 1.0f);
        } else if ( isHitFlash ) {
            float ratio = static_cast<float>(hitTimer_ - (hitDuration_ - 4)) / 4.0f;
            pe->SetVignetteParams(0.7f + ratio * 0.5f, 1.0f, 0.05f, 0.05f);
        } else if ( isLowHp ) {
            pe->SetVignetteParams(0.75f, 0.9f, 0.0f, 0.0f);
        }

        // 死亡フラッシュ → 白→赤のフルスクリーンColorTint
        pe->SetEffectActive(PostEffectType::ColorTint, isDeathFlash);
        if ( isDeathFlash ) {
            // ratio: 1.0=発生直後(白), 0.0=終わり(赤)
            float ratio = static_cast<float>(deathFlashTimer_) / static_cast<float>(deathFlashDuration_);
            float g = ratio * 0.85f;
            float b = ratio * 0.85f;
            float alpha = 0.55f + ratio * 0.4f; // 直後ほど強く、だんだん薄れる
            pe->SetColorTint(alpha, 1.0f, g, b);
        }

        // プレイヤー周囲のスポットライトグロー
        pe->SetEffectActive(PostEffectType::MaskedGlow, !isDead_);
        if ( !isDead_ && camera_ ) {
            const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
            float cx = pos_.x * vp.m[0][0] + pos_.y * vp.m[1][0] + pos_.z * vp.m[2][0] + vp.m[3][0];
            float cy = pos_.x * vp.m[0][1] + pos_.y * vp.m[1][1] + pos_.z * vp.m[2][1] + vp.m[3][1];
            float cw = pos_.x * vp.m[0][3] + pos_.y * vp.m[1][3] + pos_.z * vp.m[2][3] + vp.m[3][3];
            if ( cw > 0.001f ) {
                float uvX = cx / cw * 0.5f + 0.5f;
                float uvY = -cy / cw * 0.5f + 0.5f;
                pe->SetPlayerScreenUV(uvX, uvY, 0.15f);
            }
        }
    }

    // デバッグGUIから開始したポーズ補間も毎フレーム進める
    if (IsPoseBlendPlaying()) {
        UpdatePoseBlend();
    }

    if (isDead_) {
        // 死亡中は入力処理を止め、死亡ポーズの表示だけ維持する
        if (deathAnimationTimer_ > 0) {
            deathAnimationTimer_--;
        }

        if (model_) {
            model_->SetIsWalking(false);
            model_->SetTranslation(pos_);
            model_->SetRotation(rot_);
            model_->SetScale(scale_);
            model_->Update();
        }
        return;
    }

    // プレイヤーのスタン処理
    if (isStunned_) {
        stunTimer_--;

        StunEffectManager::Update(pos_, rot_, stunTimer_);
        if (stunTimer_ <= 0) {
            isStunned_ = false;
            stunTimer_ = 0;
            stunResistTimer_ = stunResistDuration_;
            rot_.x = 0.0f;
        }

        // スタン中はモデルの歩きアニメを止める
        if (model_) {
            model_->SetIsWalking(false);
            model_->SetTranslation(pos_);
            model_->SetRotation(rot_);
            model_->Update();
        }
        return; // WASD入力や回避などの処理を全てスキップ
    }

    if (stunResistTimer_ > 0) {
        stunResistTimer_--;
    }

    Input* input = Input::GetInstance();
    Vector3 move{ 0.0f, 0.0f, 0.0f };

    UpdateCost(); // コスト自然回復

    // 被弾後無敵時間の更新
    if (isInvincible_) {
        invincibleTimer_--;
        if (invincibleTimer_ <= 0) {
            isInvincible_ = false;
            invincibleTimer_ = 0;
        }
    }

    // 回避専用の無敵時間は通常の被弾後無敵とは分けて管理する
    if (dodgeInvincibleTimer_ > 0) {
        dodgeInvincibleTimer_--;
    }

    // 回避の連打を抑えるためにクールタイムを進める
    if (dodgeCooldownTimer_ > 0) {
        dodgeCooldownTimer_--;
    }

    // 被弾演出時間の更新
    if (isHit_) {
        hitTimer_--;
        if (hitTimer_ <= 0) {
            isHit_ = false;
            hitTimer_ = 0;
        }
    }

    // ノックバック中の更新
    if (isKnockback_) {
        pos_ += knockbackVelocity_;
        knockbackVelocity_ *= 0.85f;

        knockbackTimer_--;
        if (knockbackTimer_ <= 0) {
            isKnockback_ = false;
            knockbackTimer_ = 0;
            knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
        }
    }

    // 行動ロック中は操作不可
    if (!isInputEnabled_ || isActionLocked_) {
        if (isActionLocked_) {
            actionLockTimer_--;
            if (actionLockTimer_ <= 0) {
                isActionLocked_ = false;
            }
        }

        if (model_) {
            model_->SetIsWalking(false); // カード使用などのロック中は歩きモーションを止める
            model_->SetTranslation(pos_);
            model_->SetRotation(rot_);
            model_->SetScale(scale_);
            model_->Update();
        }

        return; // ここでリターンすればWASD入力は処理されない
    }

    // キーボード移動入力
    if (input->Pushkey(DIK_W)) { move.z += 1.0f; }
    if (input->Pushkey(DIK_S)) { move.z -= 1.0f; }
    if (input->Pushkey(DIK_A)) { move.x -= 1.0f; }
    if (input->Pushkey(DIK_D)) { move.x += 1.0f; }

    // 左スティック移動入力を加算する
    move.x += input->GetLeftStickX();
    move.z += input->GetLeftStickY();



    // 移動方向を正規化
    if (Length(move) > 0.0f) {
        move = Normalize(move);
    }

    // 回避開始
   // Shift に加えて B ボタンでも回避できるようにする
    if (!isDodging_ && dodgeCooldownTimer_ <= 0 &&
        (input->Triggerkey(DIK_LSHIFT) || input->TriggerJoystickButton(XINPUT_GAMEPAD_B))) {
        isDodging_ = true;
        dodgeTimer_ = dodgeDuration_;
        dodgeInvincibleTimer_ = dodgeInvincibleDuration_;
        dodgeCooldownTimer_ = dodgeCooldownDuration_;

        if (Length(move) > 0.0f) {
            dodgeDirection_ = move;
            rot_.y = std::atan2f(move.x, move.z);
        } else {
            dodgeDirection_ = {
                std::sinf(rot_.y),
                0.0f,
                std::cosf(rot_.y)
            };
        }
		// 回避開始時の位置を記録して、後で分身を出すときの基準にする（転がりエフェクトのため）
        lastAfterimagePos_ = { 9999.0f, 9999.0f, 9999.0f };

        if ( dodgeParticle_ ) {
            dodgeParticle_->EmitBurst(pos_);
        }
    }

    if ( isDodging_ ) {
        //  追加1：回避中はプレイヤー本体も少し青白く・半透明にする
        if ( model_ ) {
            model_->SetColor({ 0.5f, 0.8f, 1.0f, 0.8f });
        }

        // ダクソ系に寄せて、前半だけ強く進み後半は失速する回避にする (元のコード維持！)
        float progress = 1.0f - ( static_cast< float >( dodgeTimer_ ) / static_cast< float >( dodgeDuration_ ) );
        progress = Clamp01(progress);
        float dodgeSpeed = dodgeStartSpeed_ + ( dodgeEndSpeed_ - dodgeStartSpeed_ ) * progress;
        pos_ += dodgeDirection_ * dodgeSpeed;

        // 回避中の「くるっ」とした見た目だけは残す
        rot_.x = progress * 6.28318f;
        // 体を少し丸めるように傾けて、転がっている感じを足す
        float curl = std::sinf(progress * 3.14159f);
        rot_.z = curl * 0.45f;
        afterimageSpawnTimer_--;

		// くるっと回る距離が一定になるように、回避開始からの移動距離を計測して、分身を出すタイミングを決める
        float moveDist = Length(pos_ - lastAfterimagePos_);

        if ( afterimageSpawnTimer_ <= 0 && progress < 0.7f ) {
            // 「一定の距離（例：0.5f）以上動いている場合」だけ残像を置く！
            if ( moveDist > 0.8f ) {
                // スタンバイ中の「未使用(!isActive)」の分身を探して、その場に置く
                for ( auto& ai : afterimages_ ) {
                    if ( !ai.isActive ) {
                        ai.isActive = true; // ここで「出動」させる

                        if ( ai.obj && model_ ) {
                            ai.obj->SetTranslation(pos_);
                            ai.obj->SetRotation(rot_);
                            ai.obj->SetScale(scale_);
                            ai.obj->CopyPoseFrom(*model_);
                            ai.obj->SetColor({ 0.2f, 0.8f, 1.0f, 0.4f });
                            ai.obj->Update();
                        }

                        // 次に備えて、今残像を置いた座標を記録しておく！
                        lastAfterimagePos_ = pos_;


                        // 呼ぶだけで後ろにキラキラが出る！
                        if ( dodgeParticle_ ) {
                            dodgeParticle_->EmitTrail(pos_, dodgeDirection_);
                        }

                        ai.lifeTimer = afterimageLife_;
                        ai.maxLife = afterimageLife_;
                        afterimageSpawnTimer_ = afterimageSpawnInterval_;
                        break;
                    }
                }
            } else {
                // （壁からズルッと滑って動いた瞬間にすぐ出せるようにするため）
                afterimageSpawnTimer_ = 0;
            }
        }

        dodgeTimer_--;
        if ( dodgeTimer_ <= 0 ) {
            isDodging_ = false;
            dodgeTimer_ = 0;
            rot_.x = 0.0f;
            rot_.z = 0.0f;

            // 回避の終わり際に少しだけ後隙を作る (元のコード維持！)
            isActionLocked_ = true;
            actionLockTimer_ = dodgeRecoveryDuration_;

          for (auto& ai : afterimages_) {
                ai.isActive = false; 
            }


            //  追加2：終了時、回避が終わったら本体の色を完全に元に戻す！
            if ( model_ ) {
                model_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            }
        }
    } else {
        //  追加3：回避していない時は常に白にする（念のため）
        if ( model_ ) {
            model_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }

        if ( Length(move) > 0.0f ) {
            pos_ += move * ( moveSpeed_ * speedMultiplier_ );
            rot_.y = std::atan2f(move.x, move.z);

            // 足元ほこり
            footDustTimer_--;
            if ( footDustTimer_ <= 0 ) {
                footDustTimer_ = footDustInterval_;
                for ( int i = 0; i < 2; i++ ) {
                    Vector3 dp = {
                        pos_.x + ( rand() % 7 - 3 ) * 0.08f,
                        pos_.y + 0.05f,
                        pos_.z + ( rand() % 7 - 3 ) * 0.08f
                    };
                    // 移動方向の逆へ少し流れる
                    Vector3 dv = {
                        -move.x * 0.03f + ( rand() % 5 - 2 ) * 0.015f,
                        0.03f + ( rand() % 4 ) * 0.01f,
                        -move.z * 0.03f + ( rand() % 5 - 2 ) * 0.015f
                    };
                    float sc = 0.22f + ( rand() % 4 ) * 0.06f;
                    float br = 0.55f + ( rand() % 5 ) * 0.04f; // グレー
                    GPUParticleManager::GetInstance()->Emit(dp, dv, 0.5f, sc, { br, br, br, 0.5f });
                }
            }
        } else {
            footDustTimer_ = 0; // 止まったらリセット（再び動き出した直後に即出る）
        }
    }

    // スピードバフの更新
    if (speedBuffTimer_ > 0) {
        speedBuffTimer_--;
        if (speedBuffTimer_ <= 0) {
            speedMultiplier_ = 1.0f;
        }
    }

    float speed = Length(move);

    // モデルに現在のTransformを反映して更新
    if (model_) {
        model_->SetIsWalking(speed > 0.0f);
        model_->SetTranslation(pos_);
        model_->SetRotation(rot_);
        model_->SetScale(scale_);
        model_->Update();
    }

    for (auto& ai : afterimages_) {
        if (ai.isActive) {
            ai.lifeTimer--;
            if (ai.lifeTimer <= 0) {
                ai.isActive = false;
            }
            else {
                // 寿命に合わせて透明度を下げる
                float alpha = static_cast<float>(ai.lifeTimer) / static_cast<float>(ai.maxLife);
                float a = alpha * 0.2f; // 最大の濃さを 0.3 にする

                if (ai.obj) {
                    // 🌟 色自体にも 'a' を掛けて、だんだん暗く(黒く)しながら消す！
                    ai.obj->SetColor({ 0.2f, 0.8f, 1.0f, a });
                    ai.obj->Update();
                }
            }
        }
    }


}

void Player::Draw() {
    if (!model_) {
        return;
    }

    if (!IsVisible()) {
        return;
    }

    // プレイヤー本体を描画
    model_->Draw();

    

}


void Player::DrawAfterimage() {
    for (auto& ai : afterimages_) {
        if (ai.isActive && ai.obj) {
            ai.obj->Draw();
        }
    }
}

// プレイヤーアニメGUIの描画
void Player::DrawAnimationDebugUI() {
#ifdef USE_IMGUI
    if (!model_) {
        return;
    }

    // モデル側のジョイント一覧をGUI用に取り直す
    RefreshJointList();

    ImGui::Begin("プレイヤーアニメーション");

    ImGui::InputText("ジョイント検索", jointSearchText_, sizeof(jointSearchText_));
    ImGui::Checkbox("編集中のみ表示", &showEditedOnlyJoints_);
    RefreshFilteredJointIndices();

    if (filteredJointIndices_.empty()) {
        ImGui::Text("表示できるジョイントがありません");
    } else {
        if (selectedJointIndex_ < 0) {
            selectedJointIndex_ = filteredJointIndices_.front();
        }

        bool selectedJointExists = std::find(
            filteredJointIndices_.begin(),
            filteredJointIndices_.end(),
            selectedJointIndex_) != filteredJointIndices_.end();
        if (!selectedJointExists) {
            selectedJointIndex_ = filteredJointIndices_.front();
        }

        std::string selectedJointName = jointNames_[selectedJointIndex_];
        if (ImGui::BeginCombo("ジョイント", selectedJointName.c_str())) {
            for (int jointIndex : filteredJointIndices_) {
                bool isSelected = (selectedJointIndex_ == jointIndex);
                if (ImGui::Selectable(jointNames_[jointIndex].c_str(), isSelected)) {
                    selectedJointIndex_ = jointIndex;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // 現在のジョイント値を毎フレームGUIへ同期する
        SyncSelectedJointFromModel();

        if (ImGui::DragFloat3("ジョイント回転", &selectedJointRotation_.x, 0.01f)) {
            ApplySelectedJointToModel(); // 回転変更を即時反映
        }
        if (ImGui::DragFloat3("ジョイント移動", &selectedJointTranslation_.x, 0.01f)) {
            ApplySelectedJointToModel(); // 移動変更を即時反映
        }

        if (ImGui::Button("選択ジョイントを戻す")) {
            model_->ClearJointOffset(jointNames_[selectedJointIndex_]);
        }
        ImGui::SameLine();
        if (ImGui::Button("全ジョイントを戻す")) {
            model_->ClearJointOffsets();
        }
    }

    ImGui::Separator();
    ImGui::InputText("ファイルパス", poseFilePathBuffer_, sizeof(poseFilePathBuffer_));

    const char* selectedPoseLabel =
        (selectedPoseIndex_ >= 0 && selectedPoseIndex_ < static_cast<int>(savedPoses_.size()))
        ? savedPoses_[selectedPoseIndex_].name.c_str()
        : "(未選択)";

    if (ImGui::BeginCombo("保存済みポーズ", selectedPoseLabel)) {
        for (int i = 0; i < static_cast<int>(savedPoses_.size()); ++i) {
            bool isSelected = (selectedPoseIndex_ == i);
            if (ImGui::Selectable(savedPoses_[i].name.c_str(), isSelected)) {
                selectedPoseIndex_ = i;
                CopyText(poseNameBuffer_, sizeof(poseNameBuffer_), savedPoses_[i].name);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::InputText("ポーズ名", poseNameBuffer_, sizeof(poseNameBuffer_));

    ImGui::InputText("通常姿勢名", idlePoseNameBuffer_, sizeof(idlePoseNameBuffer_));
    if (ImGui::Button("通常姿勢へ反映")) {
        SavePoseFile(); // 通常姿勢スロット名を保存
        StartPoseBlendByName(idlePoseNameBuffer_, poseBlendDuration_); // 現在姿勢から通常姿勢へ寄せる
    }

    ImGui::InputText("カード使用姿勢名", cardUsePoseNameBuffer_, sizeof(cardUsePoseNameBuffer_));
    if (ImGui::Button("カード使用へ反映")) {
        SavePoseFile(); // カード使用スロット名を保存
    }
    ImGui::SameLine();
    if (ImGui::Button("カード使用ポーズ確認")) {
        PreviewPoseBlend(blendStartPoseNameBuffer_, cardUsePoseNameBuffer_, poseBlendDuration_);
    }

    ImGui::InputText("被弾姿勢名", hitPoseNameBuffer_, sizeof(hitPoseNameBuffer_));
    if (ImGui::Button("被弾ポーズ確認")) {
        PreviewPoseBlend(blendStartPoseNameBuffer_, hitPoseNameBuffer_, poseBlendDuration_);
    }

    ImGui::InputText("死亡姿勢名", deathPoseNameBuffer_, sizeof(deathPoseNameBuffer_));
    if (ImGui::Button("死亡ポーズ確認")) {
        PreviewPoseBlend(blendStartPoseNameBuffer_, deathPoseNameBuffer_, poseBlendDuration_);
    }

    ImGui::Separator();
    ImGui::Text("ポーズ補間プレビュー");
    ImGui::InputText("補間開始姿勢名", blendStartPoseNameBuffer_, sizeof(blendStartPoseNameBuffer_));
    ImGui::DragInt("補間フレーム", &poseBlendDuration_, 1.0f, 1, 120);

    if (ImGui::Button("開始姿勢確認")) {
        ApplyPoseByName(blendStartPoseNameBuffer_);
    }
    ImGui::SameLine();
    if (ImGui::Button("ポーズ補間確認")) {
        PreviewPoseBlend(blendStartPoseNameBuffer_, poseNameBuffer_, poseBlendDuration_);
    }

    if (IsPoseBlendPlaying()) {
        ImGui::Text("補間再生中: %d frame", poseBlendTimer_);
    } else {
        ImGui::Text("補間停止中");
    }

    ImGui::Separator();

    if (ImGui::Button("ポーズ保存")) {
        SaveCurrentPose(poseNameBuffer_);
        SavePoseFile();
    }
    ImGui::SameLine();
    if (ImGui::Button("ポーズ読込")) {
        StartPoseBlendByName(poseNameBuffer_, poseBlendDuration_);
    }
    ImGui::SameLine();
    if (ImGui::Button("一覧更新")) {
        LoadPoseFile(); // JSONを読み直して一覧更新
    }

    ImGui::End();
#endif
}

void Player::SetCamera(Camera* camera) {
    camera_ = camera;

    if (model_) {
        model_->SetCamera(camera_);
    }
    
    for (auto& ai : afterimages_) {
        if (ai.obj) {
            ai.obj->SetCamera(camera_);
        }
    }

}

// 指定フレームの間プレイヤー操作をロック
void Player::LockAction(int frame) {
    isActionLocked_ = true;
    actionLockTimer_ = frame;
}

void Player::AddExp(int value) {
    if (isDead_) {
        return;
    }

    // 経験値加算
    exp_ += value;

    while (exp_ >= nextLevelExp_) {
        exp_ -= nextLevelExp_;
        LevelUp();
    }
}

bool Player::CanUseCost(int value) const {
    return cost_ >= value;
}

void Player::UseCost(int value) {
    if (cost_ < value) {
        return;
    }

    cost_ -= value;
    if (cost_ < 0) {
        cost_ = 0;
    }
}

void Player::AddMaxCost(int amount) {
    maxCost_ += amount;
    // 上限が増えた分、現在のコストも即座に回復してあげる（使い勝手が良くなります）
    if (amount > 0) {
        cost_ += amount;
    }
    // 上限が減った時に、現在のコストが上限をオーバーしないようにする安全対策
    if (cost_ > maxCost_) {
        cost_ = maxCost_;
    }
}

void Player::SetCostRecoveryMultiplier(float multiplier) {
    costRecoveryMultiplier_ = multiplier;
}

void Player::Heal(int amount) {
    if (isDead_) {
        return; // 死亡中は回復しない
    }

    // HP回復
    hp_ += amount;
    if (hp_ > maxHp_) {
        hp_ = maxHp_;
    }
}

void Player::SetStun(int durationFrames) {
    // シールド展開中はスタン無効
    if (shieldHitCount_ > 0) {
        return;
    }
    if (isStunned_ || stunResistTimer_ > 0 || durationFrames <= 0) {
        return;
    }
    isStunned_ = true;
    stunTimer_ = durationFrames;
}

void Player::LevelUp() {
    if (isDead_) {
        return;
    }

    level_++;
    nextLevelExp_ += 2;
    maxHp_ += 3;
    hp_ = maxHp_;
    maxCost_ += 1;
    cost_ = maxCost_;

    if (costRecoveryInterval_ > 60) {
        costRecoveryInterval_ -= 15;
    }

    // レベルアップ：金色の光が下から上へ舞い上がるバースト
    for (int i = 0; i < 40; i++) {
        float angle = static_cast<float>(rand() % 628) * 0.01f;
        float radius = static_cast<float>(rand() % 8) * 0.15f;
        Vector3 emitPos = {
            pos_.x + std::sinf(angle) * radius,
            pos_.y,
            pos_.z + std::cosf(angle) * radius
        };
        Vector3 vel = {
            (rand() % 11 - 5) * 0.04f,
            0.3f + static_cast<float>(rand() % 15) * 0.06f,
            (rand() % 11 - 5) * 0.04f
        };
        GPUParticleManager::GetInstance()->Emit(
            emitPos, vel, 0.6f, 0.3f + (rand() % 3) * 0.1f, { 1.0f, 0.9f, 0.2f, 1.0f });
    }
}

void Player::UpdateCost() {
    if (cost_ >= maxCost_) {
        return;
    }

    costRecoveryTimer_ += static_cast<int>(1.0f * costRecoveryMultiplier_);

    if (costRecoveryTimer_ >= costRecoveryInterval_) {
        costRecoveryTimer_ = 0;
        cost_ += 1;

        if (cost_ > maxCost_) {
            cost_ = maxCost_;
        }

        // コスト回復きらめき
        Vector3 cc = { pos_.x, pos_.y + 0.4f, pos_.z };

        // 中心フラッシュ
        GPUParticleManager::GetInstance()->Emit(cc, { 0, 0, 0 }, 0.12f, 1.2f, { 0.4f, 0.85f, 1.0f, 1.0f });

        // 放射リング
        for ( int i = 0; i < 8; i++ ) {
            float a = ( 3.14159f * 2.0f / 8.0f ) * i;
            float spd = 0.18f + ( rand() % 4 ) * 0.03f;
            Vector3 rv = { std::sinf(a) * spd, 0.02f, std::cosf(a) * spd };
            GPUParticleManager::GetInstance()->Emit(cc, rv, 0.4f, 0.4f, { 0.3f, 0.8f, 1.0f, 1.0f });
        }

        // 上へ舞うスパーク
        for ( int i = 0; i < 8; i++ ) {
            Vector3 sp = {
                pos_.x + ( rand() % 9 - 4 ) * 0.1f,
                pos_.y + 0.1f + ( rand() % 4 ) * 0.15f,
                pos_.z + ( rand() % 9 - 4 ) * 0.1f
            };
            Vector3 sv = {
                ( rand() % 7 - 3 ) * 0.04f,
                0.15f + ( rand() % 6 ) * 0.03f,
                ( rand() % 7 - 3 ) * 0.04f
            };
            float sc = 0.28f + ( rand() % 4 ) * 0.07f;
            GPUParticleManager::GetInstance()->Emit(sp, sv, 0.55f, sc, { 0.35f, 0.82f, 1.0f, 0.95f });
        }
    }
}

// ダメージ処理
void Player::TakeDamage(int damage, const Vector3& attackFrom, float knockbackScale){
    if ( isDead_ || dodgeInvincibleTimer_ > 0 || isInvincible_ || isDebugInvincible_ ) {
        return;
    }

    if ( shieldHitCount_ > 0 ) {
        shieldHitCount_--;

        // 画面全体の青いビネットフラッシュ
        shieldFlashTimer_ = shieldFlashDuration_;

        // シールド吸収：青いバースト（カメラが高いので大きめサイズで）
        Vector3 sc = { pos_.x, pos_.y + 0.5f, pos_.z };

        // 中心フラッシュ（大きく）
        GPUParticleManager::GetInstance()->Emit(sc, { 0, 0, 0 }, 0.07f, 7.0f, { 0.5f, 0.9f, 1.0f, 1.0f });
        GPUParticleManager::GetInstance()->Emit(sc, { 0, 0, 0 }, 0.15f, 4.5f, { 0.3f, 0.7f, 1.0f, 0.85f });

        // 放射リング（大きく・速く）
        for ( int i = 0; i < 20; i++ ) {
            float a = ( 3.14159f * 2.0f / 20.0f ) * i;
            float speed = 0.7f + ( rand() % 6 ) * 0.05f;
            Vector3 v = { std::sinf(a) * speed, 0.02f, std::cosf(a) * speed };
            GPUParticleManager::GetInstance()->Emit(sc, v, 0.25f, 2.5f, { 0.2f, 0.75f, 1.0f, 1.0f });
        }

        // 上方向へ散る青いスパーク
        for ( int i = 0; i < 14; i++ ) {
            Vector3 sv = {
                ( rand() % 11 - 5 ) * 0.1f,
                0.25f + ( rand() % 8 ) * 0.06f,
                ( rand() % 11 - 5 ) * 0.1f
            };
            float scale = 0.5f + ( rand() % 5 ) * 0.1f;
            GPUParticleManager::GetInstance()->Emit(sc, sv, 0.5f, scale, { 0.5f, 0.9f, 1.0f, 0.9f });
        }

        return;
    }

    if ( isEnemyAtkDebuffed_ ) {
        damage /= 2;
    }

    int nextHp = hp_ - damage; // ダメージ適用後のHPを先に計算する

    // チュートリアル中は必ずHPを1残して死亡しないようにする
    if ( isTutorialNoDeath_ && nextHp <= 0 ) {
        hp_ = 1;
    } else {
        hp_ = nextHp;
        if ( hp_ < 0 ) {
            hp_ = 0;
        }
    }


    isHit_ = true;
    hitTimer_ = hitDuration_;

    isInvincible_ = true;
    invincibleTimer_ = invincibleDuration_;

    Vector3 hitDir = {
        pos_.x - attackFrom.x,
        0.0f,
        pos_.z - attackFrom.z
    };

    if ( Length(hitDir) > 0.01f ) {
        hitDir = Normalize(hitDir);
        knockbackVelocity_ = hitDir * ( 0.55f * knockbackScale );
        isKnockback_ = true;
        knockbackTimer_ = knockbackDuration_;
    }

    // 被弾エフェクト
    Vector3 particleDir = ( Length(hitDir) > 0.01f ) ? Normalize(hitDir) : Vector3 { 0, 0, 1 };

    // ① メインの衝撃飛び散り（赤〜オレンジ）
    for ( int i = 0; i < 20; i++ ) {
        Vector3 sparkVel = {
            particleDir.x * ( 0.5f + ( rand() % 10 ) * 0.08f ) + ( rand() % 11 - 5 ) * 0.12f,
            0.15f + ( rand() % 10 ) * 0.08f,
            particleDir.z * ( 0.5f + ( rand() % 10 ) * 0.08f ) + ( rand() % 11 - 5 ) * 0.12f
        };
        Vector3 sparkPos = {
            pos_.x + ( rand() % 7 - 3 ) * 0.12f,
            pos_.y + 0.6f + ( rand() % 6 ) * 0.1f,
            pos_.z + ( rand() % 7 - 3 ) * 0.12f
        };
        float scale = 0.3f + ( rand() % 5 ) * 0.08f;
        float orange = 0.15f + ( rand() % 10 ) * 0.04f; // 0.15〜0.55 でオレンジ〜赤
        GPUParticleManager::GetInstance()->Emit(sparkPos, sparkVel, 0.35f, scale, { 1.0f, orange, 0.05f, 1.0f });
    }

    // ② 衝撃波リング（暗い赤）
    for ( int i = 0; i < 12; i++ ) {
        float ringAngle = ( 3.14159f * 2.0f / 12.0f ) * i;
        float speed = 0.35f;
        Vector3 ringVel = { std::sinf(ringAngle) * speed, 0.0f, std::cosf(ringAngle) * speed };
        GPUParticleManager::GetInstance()->Emit(
            { pos_.x, pos_.y + 0.5f, pos_.z },
            ringVel, 0.2f, 0.9f, { 1.0f, 0.12f, 0.05f, 0.9f }
        );
    }

    // ③ 大きな赤フラッシュ
    GPUParticleManager::GetInstance()->Emit(
        { pos_.x, pos_.y + 0.8f, pos_.z },
        { 0, 0, 0 }, 0.1f, 2.5f, { 1.0f, 0.25f, 0.08f, 0.95f }
    );

    // ④ オレンジコアフラッシュ（少し長く残る）
    GPUParticleManager::GetInstance()->Emit(
        { pos_.x, pos_.y + 0.7f, pos_.z },
        { 0, 0, 0 }, 0.2f, 1.8f, { 1.0f, 0.45f, 0.1f, 0.8f }
    );

    // ⑤ ふわっと上に浮かぶ残滓（薄い赤）
    for ( int i = 0; i < 8; i++ ) {
        Vector3 driftPos = {
            pos_.x + ( rand() % 9 - 4 ) * 0.15f,
            pos_.y + 0.3f,
            pos_.z + ( rand() % 9 - 4 ) * 0.15f
        };
        GPUParticleManager::GetInstance()->Emit(
            driftPos,
            { ( rand() % 7 - 3 ) * 0.03f, 0.08f + ( rand() % 5 ) * 0.02f, ( rand() % 7 - 3 ) * 0.03f },
            0.6f, 0.35f, { 1.0f, 0.22f, 0.08f, 0.7f }
        );
    }

    // カメラシェイク
    if ( camera_ ) {
        camera_->TriggerShake(0.12f, 8);
    }

    if ( hp_ <= 0 ) {
        isDead_ = true;
        deathAnimationTimer_ = deathAnimationDuration_;
        isActionLocked_ = true;
        actionLockTimer_ = deathAnimationDuration_;
        isDodging_ = false;
        isKnockback_ = false;
        knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };

        // 死亡フラッシュ（白→赤 ColorTint）を起動
        deathFlashTimer_ = deathFlashDuration_;

        // カメラシェイク（死亡時）
        if ( camera_ ) {
            camera_->TriggerShake(0.18f, 12);
        }

        // ========== 死亡時パーティクル（大幅強化） ==========
        Vector3 dc = { pos_.x, pos_.y + 0.6f, pos_.z };

        // ① 超巨大白フラッシュ（爆発の一瞬）
        GPUParticleManager::GetInstance()->Emit(dc, { 0, 0, 0 }, 0.05f, 12.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
        GPUParticleManager::GetInstance()->Emit(dc, { 0, 0, 0 }, 0.12f, 8.5f,  { 1.0f, 0.95f, 0.85f, 1.0f });

        // ② 赤コアフラッシュ
        GPUParticleManager::GetInstance()->Emit(dc, { 0, 0, 0 }, 0.2f,  6.0f,  { 1.0f, 0.2f, 0.05f, 1.0f });
        GPUParticleManager::GetInstance()->Emit(dc, { 0, 0, 0 }, 0.35f, 4.0f,  { 1.0f, 0.35f, 0.1f, 0.9f });

        // ③ 爆発リング①（速い・大きい）
        for ( int i = 0; i < 32; i++ ) {
            float angle = ( 3.14159f * 2.0f / 32.0f ) * i;
            float speed = 0.8f + ( rand() % 8 ) * 0.06f;
            Vector3 rv = { std::sinf(angle) * speed, 0.02f + (rand() % 4) * 0.01f, std::cosf(angle) * speed };
            float gc = 0.08f + ( rand() % 5 ) * 0.07f;
            GPUParticleManager::GetInstance()->Emit(dc, rv, 0.4f, 3.0f, { 1.0f, gc, 0.05f, 1.0f });
        }

        // ④ 爆発リング②（少し遅く・白オレンジ）
        for ( int i = 0; i < 24; i++ ) {
            float angle = ( 3.14159f * 2.0f / 24.0f ) * i + 0.2f;
            float speed = 0.45f + ( rand() % 6 ) * 0.05f;
            Vector3 rv = { std::sinf(angle) * speed, 0.12f, std::cosf(angle) * speed };
            GPUParticleManager::GetInstance()->Emit(dc, rv, 0.65f, 2.0f, { 1.0f, 0.55f, 0.15f, 0.9f });
        }

        // ⑤ 赤い爆散（大量・広範囲）
        for ( int i = 0; i < 70; i++ ) {
            Vector3 ev = {
                ( rand() % 21 - 10 ) * 0.28f,
                ( rand() % 16 ) * 0.13f + 0.05f,
                ( rand() % 21 - 10 ) * 0.28f
            };
            Vector3 ep = {
                pos_.x + ( rand() % 17 - 8 ) * 0.1f,
                pos_.y + 0.2f + ( rand() % 15 ) * 0.1f,
                pos_.z + ( rand() % 17 - 8 ) * 0.1f
            };
            float gc = 0.05f + ( rand() % 6 ) * 0.06f;
            float sc = 0.5f + ( rand() % 8 ) * 0.12f;
            GPUParticleManager::GetInstance()->Emit(ep, ev, 0.65f, sc, { 1.0f, gc, 0.05f, 1.0f });
        }

        // ⑥ 白い散乱（爆発コアの残骸）
        for ( int i = 0; i < 18; i++ ) {
            Vector3 wv = {
                ( rand() % 21 - 10 ) * 0.18f,
                0.35f + ( rand() % 12 ) * 0.1f,
                ( rand() % 21 - 10 ) * 0.18f
            };
            float sc = 1.5f + ( rand() % 6 ) * 0.2f;
            GPUParticleManager::GetInstance()->Emit(dc, wv, 0.22f, sc, { 1.0f, 0.92f, 0.85f, 1.0f });
        }

        // ⑦ 暗い煙（大量・大きく・長く残る）
        for ( int i = 0; i < 22; i++ ) {
            Vector3 sv = { ( rand() % 11 - 5 ) * 0.07f, 0.08f + ( rand() % 10 ) * 0.04f, ( rand() % 11 - 5 ) * 0.07f };
            float sc = 1.4f + ( rand() % 7 ) * 0.25f;
            float br = 0.08f + ( rand() % 3 ) * 0.04f;
            GPUParticleManager::GetInstance()->Emit(dc, sv, 1.8f, sc, { br, br, br, 0.88f });
        }

        // 死亡時は現在姿勢から death ポーズへ少しずつ遷移する
        StartPoseBlendByName(deathPoseNameBuffer_, poseBlendDuration_);

        if ( model_ ) {
            model_->SetIsWalking(false);
            model_->SetTranslation(pos_);
            model_->SetRotation(rot_);
            model_->SetScale(scale_);
            model_->Update();
        }
    }
}

// 継続ダメージ処理
void Player::TakeContinuousDamage(int damage) {
    if (isDead_ || dodgeInvincibleTimer_ > 0 || isDebugInvincible_) {
        return;
    }

    if (shieldHitCount_ > 0) {
        shieldHitCount_--;
        return;
    }

    if (isEnemyAtkDebuffed_) {
        damage /= 2;
    }
    if (damage <= 0) {
        damage = 1;
    }

    int nextHp = hp_ - damage;
    if (isTutorialNoDeath_ && nextHp <= 0) {
        hp_ = 1;
    } else {
        hp_ = nextHp;
        if (hp_ < 0) {
            hp_ = 0;
        }
    }

    isHit_ = true;
    hitTimer_ = (std::min)(hitDuration_, 8);

    // 継続ダメージ（毒・炎など）の小さな赤いスパーク
    for (int i = 0; i < 8; i++) {
        Vector3 sparkPos = {
            pos_.x + (rand() % 7 - 3) * 0.1f,
            pos_.y + 0.3f + (rand() % 6) * 0.15f,
            pos_.z + (rand() % 7 - 3) * 0.1f
        };
        Vector3 sparkVel = {
            (rand() % 11 - 5) * 0.05f,
            0.1f + (rand() % 5) * 0.04f,
            (rand() % 11 - 5) * 0.05f
        };
        GPUParticleManager::GetInstance()->Emit(
            sparkPos, sparkVel, 0.5f, 0.2f, { 1.0f, 0.3f, 0.1f, 0.85f });
    }

    if (hp_ <= 0) {
        isDead_ = true;
        deathAnimationTimer_ = deathAnimationDuration_;
        isActionLocked_ = true;
        actionLockTimer_ = deathAnimationDuration_;
        isDodging_ = false;
        isKnockback_ = false;
        knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };

        // 継続ダメージ死亡でも同じ派手な演出を出す
        deathFlashTimer_ = deathFlashDuration_;
        if ( camera_ ) {
            camera_->TriggerShake(0.6f, 35);
        }

        Vector3 dc2 = { pos_.x, pos_.y + 0.6f, pos_.z };
        GPUParticleManager::GetInstance()->Emit(dc2, { 0, 0, 0 }, 0.05f, 12.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
        GPUParticleManager::GetInstance()->Emit(dc2, { 0, 0, 0 }, 0.2f,  6.0f,  { 1.0f, 0.2f, 0.05f, 1.0f });
        for ( int i = 0; i < 28; i++ ) {
            float angle = ( 3.14159f * 2.0f / 28.0f ) * i;
            float speed = 0.75f + ( rand() % 7 ) * 0.06f;
            Vector3 rv = { std::sinf(angle) * speed, 0.02f, std::cosf(angle) * speed };
            GPUParticleManager::GetInstance()->Emit(dc2, rv, 0.4f, 2.8f, { 1.0f, 0.08f + (rand()%5)*0.07f, 0.05f, 1.0f });
        }
        for ( int i = 0; i < 40; i++ ) {
            Vector3 ev = { (rand()%21-10)*0.25f, (rand()%14)*0.12f+0.05f, (rand()%21-10)*0.25f };
            Vector3 ep = { pos_.x+(rand()%15-7)*0.1f, pos_.y+0.2f+(rand()%12)*0.1f, pos_.z+(rand()%15-7)*0.1f };
            GPUParticleManager::GetInstance()->Emit(ep, ev, 0.6f, 0.5f+(rand()%7)*0.1f, { 1.0f, 0.06f+(rand()%5)*0.06f, 0.05f, 1.0f });
        }

        StartPoseBlendByName(deathPoseNameBuffer_, poseBlendDuration_);

        if (model_) {
            model_->SetIsWalking(false);
            model_->SetTranslation(pos_);
            model_->SetRotation(rot_);
            model_->SetScale(scale_);
            model_->Update();
        }
    }
}

// カード使用ポーズを再生する
void Player::PlayCardUsePose(int durationFrames) {
    if (isDead_) {
        return; // 死亡中はカード使用ポーズへ遷移させない
    }
    StartPoseBlendByName(cardUsePoseNameBuffer_, durationFrames);
}

// 通常姿勢へ戻す
void Player::PlayIdlePose(int durationFrames) {
    if (isDead_) {
        return; // 死亡中は通常姿勢へ戻さない
    }
    StartPoseBlendByName(idlePoseNameBuffer_, durationFrames);
}

void Player::ApplySpeedBuff(float multiplier, int durationFrames) {
    speedMultiplier_ = multiplier;
    speedBuffTimer_ = durationFrames;
}

// 描画するか判定
bool Player::IsVisible() const {
    if (!isHit_) {
        return true;
    }

    return (hitTimer_ % 2) == 0;
}

// モデルからジョイント一覧を取り直す
void Player::RefreshJointList() {
    if (!model_) {
        return;
    }

    jointNames_ = model_->GetJointNames();
    if (selectedJointIndex_ >= static_cast<int>(jointNames_.size())) {
        selectedJointIndex_ = jointNames_.empty() ? -1 : 0;
    }
}

// 検索条件に合うジョイント一覧を作る
void Player::RefreshFilteredJointIndices() {
    filteredJointIndices_.clear();

    for (int i = 0; i < static_cast<int>(jointNames_.size()); ++i) {
        const std::string& jointName = jointNames_[i];
        if (!ContainsIgnoreCase(jointName, jointSearchText_)) {
            continue;
        }

        if (showEditedOnlyJoints_) {
            Vector3 rotation = model_->GetJointRotationOffset(jointName);
            Vector3 translation = model_->GetJointTranslationOffset(jointName);
            if (IsNearlyZeroVector(rotation) && IsNearlyZeroVector(translation)) {
                continue;
            }
        }

        filteredJointIndices_.push_back(i);
    }
}

// 選択中ジョイントの値をモデルから読む
void Player::SyncSelectedJointFromModel() {
    if (!model_ || selectedJointIndex_ < 0 || selectedJointIndex_ >= static_cast<int>(jointNames_.size())) {
        return;
    }

    const std::string& jointName = jointNames_[selectedJointIndex_];
    selectedJointRotation_ = model_->GetJointRotationOffset(jointName);
    selectedJointTranslation_ = model_->GetJointTranslationOffset(jointName);
}

// GUIで編集した値をモデルへ反映する
void Player::ApplySelectedJointToModel() {
    if (!model_ || selectedJointIndex_ < 0 || selectedJointIndex_ >= static_cast<int>(jointNames_.size())) {
        return;
    }

    const std::string& jointName = jointNames_[selectedJointIndex_];
    model_->SetJointRotationOffset(jointName, selectedJointRotation_);
    model_->SetJointTranslationOffset(jointName, selectedJointTranslation_);
}

// 保存済みポーズをモデルへ反映する
void Player::ApplyPose(const NamedPoseData& pose) {
    if (!model_) {
        return;
    }

    model_->ClearJointOffsets();
    for (const JointPoseData& joint : pose.joints) {
        model_->SetJointRotationOffset(joint.jointName, joint.rotation);
        model_->SetJointTranslationOffset(joint.jointName, joint.translation);
    }
}

// 名前指定でポーズを反映する
void Player::ApplyPoseByName(const std::string& poseName) {
    if (!model_) {
        return;
    }

    poseBlendTimer_ = 0;
    poseBlendJoints_.clear();

    int poseIndex = FindPoseIndex(poseName);
    if (poseIndex < 0) {
        model_->ClearJointOffsets();
        return;
    }

    ApplyPose(savedPoses_[poseIndex]);
}

// 現在姿勢から指定ポーズへの補間を開始する
void Player::StartPoseBlendByName(const std::string& poseName, int duration) {
    if (isDead_ && poseName != deathPoseNameBuffer_) {
        return; // 死亡後は death ポーズ以外で上書きさせない
    }

    poseBlendJoints_.clear();

    if (!model_) {
        poseBlendTimer_ = 0;
        return;
    }

    int poseIndex = FindPoseIndex(poseName);
    if (poseIndex < 0 || duration <= 0) {
        poseBlendTimer_ = 0;
        ApplyPoseByName(poseName);
        return;
    }

    poseBlendDuration_ = duration;
    poseBlendTimer_ = duration;
    const NamedPoseData& pose = savedPoses_[poseIndex];

    for (const std::string& jointName : jointNames_) {
        PoseBlendJointData blendJoint{};
        blendJoint.jointName = jointName;
        blendJoint.startRotation = model_->GetJointRotationOffset(jointName);
        blendJoint.startTranslation = model_->GetJointTranslationOffset(jointName);
        blendJoint.targetRotation = { 0.0f, 0.0f, 0.0f };
        blendJoint.targetTranslation = { 0.0f, 0.0f, 0.0f };

        for (const JointPoseData& poseJoint : pose.joints) {
            if (poseJoint.jointName != jointName) {
                continue;
            }

            blendJoint.targetRotation = poseJoint.rotation;
            blendJoint.targetTranslation = poseJoint.translation;
            break;
        }

        poseBlendJoints_.push_back(blendJoint);
    }

    UpdatePoseBlend();
}

// 開始姿勢を揃えてから補間を再生する
void Player::PreviewPoseBlend(const std::string& startPoseName, const std::string& targetPoseName, int duration) {
    ApplyPoseByName(startPoseName);
    StartPoseBlendByName(targetPoseName, duration);
}

// 補間中のポーズを更新する
void Player::UpdatePoseBlend() {
    if (!model_ || poseBlendJoints_.empty()) {
        return;
    }

    float t = 1.0f;
    if (poseBlendDuration_ > 0) {
        t = 1.0f - (static_cast<float>(poseBlendTimer_) / static_cast<float>(poseBlendDuration_));
        t = Clamp01(t);
    }

    model_->ClearJointOffsets();
    for (const PoseBlendJointData& blendJoint : poseBlendJoints_) {
        model_->SetJointRotationOffset(
            blendJoint.jointName,
            LerpVector3(blendJoint.startRotation, blendJoint.targetRotation, t));
        model_->SetJointTranslationOffset(
            blendJoint.jointName,
            LerpVector3(blendJoint.startTranslation, blendJoint.targetTranslation, t));
    }

    if (poseBlendTimer_ > 0) {
        poseBlendTimer_--;
    }

    if (poseBlendTimer_ <= 0) {
        for (const PoseBlendJointData& blendJoint : poseBlendJoints_) {
            // 最終姿勢がゼロ差分なら編集オフセット自体を消して、歩きアニメをそのまま通す
            if (IsNearlyZeroVector(blendJoint.targetRotation) && IsNearlyZeroVector(blendJoint.targetTranslation)) {
                model_->ClearJointOffset(blendJoint.jointName);
                continue;
            }

            model_->SetJointRotationOffset(blendJoint.jointName, blendJoint.targetRotation);
            model_->SetJointTranslationOffset(blendJoint.jointName, blendJoint.targetTranslation);
        }
        poseBlendJoints_.clear();
    }
}

// 現在のジョイント状態をポーズとして保存する
void Player::SaveCurrentPose(const std::string& poseName) {
    if (!model_ || poseName.empty()) {
        return;
    }

    NamedPoseData pose;
    pose.name = poseName;

    for (const std::string& jointName : jointNames_) {
        Vector3 rotation = model_->GetJointRotationOffset(jointName);
        Vector3 translation = model_->GetJointTranslationOffset(jointName);
        if (IsNearlyZeroVector(rotation) && IsNearlyZeroVector(translation)) {
            continue;
        }

        pose.joints.push_back({ jointName, rotation, translation });
    }

    int poseIndex = FindPoseIndex(poseName);
    if (poseIndex >= 0) {
        savedPoses_[poseIndex] = pose;
        selectedPoseIndex_ = poseIndex;
    } else {
        savedPoses_.push_back(pose);
        selectedPoseIndex_ = static_cast<int>(savedPoses_.size()) - 1;
    }
}

// ポーズJSONを書き出す
void Player::SavePoseFile() const {
    json root;
    root["slots"]["idle"] = idlePoseNameBuffer_;
    root["slots"]["card_use"] = cardUsePoseNameBuffer_;
    root["slots"]["hit"] = hitPoseNameBuffer_;
    root["slots"]["death"] = deathPoseNameBuffer_;

    json poses = json::array();
    for (const NamedPoseData& pose : savedPoses_) {
        json poseJson;
        poseJson["name"] = pose.name;
        poseJson["joints"] = json::array();

        for (const JointPoseData& joint : pose.joints) {
            json jointJson;
            jointJson["joint"] = joint.jointName;
            jointJson["rotation"] = Vector3ToJson(joint.rotation);
            jointJson["translation"] = Vector3ToJson(joint.translation);
            poseJson["joints"].push_back(jointJson);
        }

        poses.push_back(poseJson);
    }

    root["savedPoses"] = poses;

    std::filesystem::path filePath = poseFilePathBuffer_;
    std::filesystem::create_directories(filePath.parent_path());

    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        return;
    }

    ofs << root.dump(2);
}

// ポーズJSONを読み込む
void Player::LoadPoseFile() {
    savedPoses_.clear();
    poseFilePath_ = poseFilePathBuffer_;

    std::ifstream ifs(poseFilePath_);
    if (!ifs.is_open()) {
        EnsureDefaultPoseEntries();
        return;
    }

    json root;
    ifs >> root;

    if (root.contains("slots")) {
        const json& slots = root["slots"];
        if (slots.contains("idle")) { CopyText(idlePoseNameBuffer_, sizeof(idlePoseNameBuffer_), slots["idle"].get<std::string>()); }
        if (slots.contains("card_use")) { CopyText(cardUsePoseNameBuffer_, sizeof(cardUsePoseNameBuffer_), slots["card_use"].get<std::string>()); }
        if (slots.contains("hit")) { CopyText(hitPoseNameBuffer_, sizeof(hitPoseNameBuffer_), slots["hit"].get<std::string>()); }
        if (slots.contains("death")) { CopyText(deathPoseNameBuffer_, sizeof(deathPoseNameBuffer_), slots["death"].get<std::string>()); }
    }

    if (root.contains("savedPoses") && root["savedPoses"].is_array()) {
        for (const json& poseJson : root["savedPoses"]) {
            NamedPoseData pose;
            pose.name = poseJson.value("name", "");

            if (poseJson.contains("joints") && poseJson["joints"].is_array()) {
                for (const json& jointJson : poseJson["joints"]) {
                    JointPoseData joint;
                    joint.jointName = jointJson.value("joint", "");
                    joint.rotation = JsonToVector3(jointJson["rotation"]);
                    joint.translation = jointJson.contains("translation")
                        ? JsonToVector3(jointJson["translation"])
                        : Vector3{ 0.0f, 0.0f, 0.0f };
                    pose.joints.push_back(joint);
                }
            }

            if (!pose.name.empty()) {
                savedPoses_.push_back(pose);
            }
        }
    }

    EnsureDefaultPoseEntries();
    selectedPoseIndex_ = savedPoses_.empty() ? -1 : 0;
    if (selectedPoseIndex_ >= 0) {
        CopyText(poseNameBuffer_, sizeof(poseNameBuffer_), savedPoses_[selectedPoseIndex_].name);
    }
}

// スロット名に対応する空ポーズを補完する
void Player::EnsureDefaultPoseEntries() {
    const std::vector<std::string> requiredNames = {
        idlePoseNameBuffer_,
        cardUsePoseNameBuffer_,
        hitPoseNameBuffer_,
        deathPoseNameBuffer_
    };

    for (const std::string& poseName : requiredNames) {
        if (poseName.empty()) {
            continue;
        }

        if (FindPoseIndex(poseName) >= 0) {
            continue;
        }

        savedPoses_.push_back({ poseName, {} });
    }
}

// 名前から保存済みポーズを探す
int Player::FindPoseIndex(const std::string& poseName) const {
    for (int i = 0; i < static_cast<int>(savedPoses_.size()); ++i) {
        if (savedPoses_[i].name == poseName) {
            return i;
        }
    }

    return -1;
}
