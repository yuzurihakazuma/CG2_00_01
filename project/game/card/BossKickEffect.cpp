#include "BossKickEffect.h"
#include "game/player/Player.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/camera/Camera.h"
#include <cmath>

using namespace VectorMath;

static constexpr float kPi = 3.14159265f;

void BossKickEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
    (void)isPlayerCaster;

    casterBoss_ = casterBoss;
    camera_     = camera;
    casterYaw_  = casterYaw;
    spinYaw_    = casterYaw;
    timer_      = 0;
    isFinished_ = false;
    hasHit_     = false;

    startPos_ = casterPos;
    baseY_    = casterPos.y;
    pos_      = casterPos;

    obj_ = Obj3d::Create("kick_model");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation(pos_);
        obj_->SetRotation({ 0.0f, casterYaw_, 0.0f });

        Model* model = obj_->GetModel();
        if (model) {
            model->SetTexture("resources/white1x1.png");
            Model::Material* mat = model->GetMaterial();
            if (mat) {
                mat->color   = { 0.7f, 0.2f, 1.0f, 1.0f };
                mat->emissive = 2.0f;
            }
        }
        obj_->Update();
    }
}

void BossKickEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
    (void)enemyManager;
    (void)boss;
    (void)extraBoss;
    (void)bossPos;
    (void)level;

    if (isFinished_) return;
    if (!casterBoss_ || casterBoss_->IsDead()) {
        isFinished_ = true;
        return;
    }

    timer_++;

    // =========================================================
    // Phase 1: ため (0〜18f) — ボス静止・オーラ蓄積
    // =========================================================
    if (timer_ <= kWindupEnd) {
        float t = static_cast<float>(timer_) / kWindupEnd;

        // ボスはその場に静止
        if (casterBoss_) casterBoss_->SetPosition(startPos_);
        pos_ = startPos_;

        // 紫のオーラが収束してくる
        float orbitR = 4.0f * (1.0f - t * 0.6f);
        for (int i = 0; i < 4; i++) {
            float angle = (kPi * 2.0f / 4.0f) * i
                        + static_cast<float>(timer_) * 0.15f;
            Vector3 orbitPos = {
                startPos_.x + std::cosf(angle) * orbitR,
                startPos_.y + 0.8f,
                startPos_.z + std::sinf(angle) * orbitR
            };
            // コアへ向かう速度
            Vector3 toCore = {
                (startPos_.x - orbitPos.x) * 0.4f,
                0.1f,
                (startPos_.z - orbitPos.z) * 0.4f
            };
            GPUParticleManager::GetInstance()->Emit(
                orbitPos, toCore, 0.18f, 0.8f + t * 0.6f,
                { 0.7f + t * 0.2f, 0.1f, 1.0f, 0.6f + t * 0.3f });
        }
        // A. 稲妻エフェクト (2フレームおきにランダムな位置へ閃光)
        if (timer_ % 2 == 0) {
            for (int i = 0; i < 3; i++) {
                float lx = startPos_.x + (rand() % 11 - 5) * 0.4f;
                float lz = startPos_.z + (rand() % 11 - 5) * 0.4f;
                Vector3 boltVel = {
                    (rand() % 7 - 3) * 0.3f,
                    0.5f + (rand() % 5) * 0.1f,
                    (rand() % 7 - 3) * 0.3f
                };
                GPUParticleManager::GetInstance()->Emit(
                    { lx, startPos_.y + 0.5f, lz },
                    boltVel, 0.06f, 0.6f,
                    { 0.9f, 0.7f, 1.0f, 0.95f }); // 白紫の稲妻
            }
        }

        // B. 地面ひび割れ粉塵 (3フレームおき)
        if (timer_ % 3 == 0) {
            for (int i = 0; i < 10; i++) {
                float a = (kPi * 2.0f / 10.0f) * i;
                float sp = 0.3f + (rand() % 5) * 0.1f;
                Vector3 v = {
                    std::cosf(a) * sp,
                    0.2f + (rand() % 6) * 0.05f, // 上に跳ねる石くれ
                    std::sinf(a) * sp
                };
                float gray = 0.4f + (rand() % 5) * 0.06f;
                GPUParticleManager::GetInstance()->Emit(
                    { startPos_.x + (rand()%5-2)*0.2f,
                      startPos_.y + 0.05f,
                      startPos_.z + (rand()%5-2)*0.2f },
                    v, 0.2f, 0.35f, { gray, gray*0.8f, gray, 0.8f });
            }
        }

        // C. 引力エフェクト (遠くから粒子がボスに吸い込まれる)
        {
            float pullR = 5.0f + (rand() % 5) * 0.3f;
            float pullAngle = static_cast<float>(rand() % 628) * 0.01f;
            Vector3 pullStart = {
                startPos_.x + std::cosf(pullAngle) * pullR,
                startPos_.y + 0.5f + (rand() % 5) * 0.3f,
                startPos_.z + std::sinf(pullAngle) * pullR
            };
            Vector3 pullVel = {
                (startPos_.x - pullStart.x) * 0.35f,
                -0.05f,
                (startPos_.z - pullStart.z) * 0.35f
            };
            GPUParticleManager::GetInstance()->Emit(
                pullStart, pullVel, 0.15f, 0.5f + t * 0.5f,
                { 0.6f, 0.1f, 1.0f, 0.5f + t * 0.4f });
        }

        // 既存: 足元の地面エフェクト
        if (timer_ % 3 == 0) {
            for (int i = 0; i < 8; i++) {
                float a = (kPi * 2.0f / 8.0f) * i;
                Vector3 v = { std::cosf(a) * 0.4f, 0.05f, std::sinf(a) * 0.4f };
                GPUParticleManager::GetInstance()->Emit(
                    { startPos_.x, startPos_.y + 0.1f, startPos_.z },
                    v, 0.25f, 0.4f, { 0.5f, 0.0f, 0.9f, 0.5f });
            }
        }

        // モデルはボス位置で待機
        if (obj_) {
            obj_->SetTranslation(pos_);
            obj_->SetRotation({ 0.0f, casterYaw_, 0.0f });
            obj_->Update();
        }
    }
    // =========================================================
    // Phase 2: ジャンプ上昇 (18〜25f)
    // =========================================================
    else if (timer_ <= kJumpEnd) {
        float t = static_cast<float>(timer_ - kWindupEnd)
                / static_cast<float>(kJumpEnd - kWindupEnd);
        float height = std::sinf(t * kPi * 0.5f) * kJumpHeight; // 0→頂点

        Vector3 jumpPos = { startPos_.x, baseY_ + height, startPos_.z };
        if (casterBoss_) casterBoss_->SetPosition(jumpPos);
        pos_ = jumpPos;

        // D. 踏み切り爆発 (ジャンプ開始の1フレームのみ)
        if (timer_ == kWindupEnd + 1) {
            // 地面を蹴った土煙・爆風が外側に広がる
            for (int i = 0; i < 20; i++) {
                float a = (kPi * 2.0f / 20.0f) * i;
                float sp = 2.0f + (rand() % 8) * 0.3f;
                Vector3 v = { std::cosf(a) * sp, 0.3f + (rand()%5)*0.1f, std::sinf(a) * sp };
                float br = 0.45f + (rand()%6)*0.04f;
                GPUParticleManager::GetInstance()->Emit(
                    { startPos_.x, baseY_ + 0.1f, startPos_.z },
                    v, 0.35f, 1.2f, { br, br*0.7f, 0.1f, 0.9f }); // 土煙
            }
            // 中心の紫フラッシュ
            for (int i = 0; i < 10; i++) {
                Vector3 v = { (rand()%9-4)*0.4f, 1.5f+(rand()%5)*0.3f, (rand()%9-4)*0.4f };
                GPUParticleManager::GetInstance()->Emit(
                    startPos_, v, 0.2f, 1.5f, { 0.8f, 0.3f, 1.0f, 1.0f });
            }
        }

        // E. 上昇スピードライン (縦方向に速い細い軌跡)
        for (int i = 0; i < 6; i++) {
            float rx = startPos_.x + (rand()%7-3) * 0.3f;
            float rz = startPos_.z + (rand()%7-3) * 0.3f;
            Vector3 v = { 0.0f, -2.5f - (rand()%5)*0.3f, 0.0f }; // 下に流れる速い線
            GPUParticleManager::GetInstance()->Emit(
                { rx, pos_.y, rz }, v, 0.07f, 0.4f,
                { 0.9f, 0.8f, 1.0f, 0.7f });
        }

        // F. 風圧リング (地面から外側へ広がる空気の輪)
        if (timer_ % 2 == 0) {
            for (int i = 0; i < 16; i++) {
                float a = (kPi * 2.0f / 16.0f) * i;
                float sp = 3.5f + (rand()%5)*0.2f;
                Vector3 v = { std::cosf(a)*sp, 0.05f, std::sinf(a)*sp };
                GPUParticleManager::GetInstance()->Emit(
                    { startPos_.x, baseY_+0.05f, startPos_.z },
                    v, 0.12f, 0.3f, { 0.9f, 0.9f, 1.0f, 0.4f }); // 薄い白・空気感
            }
        }

        // 既存: 上昇中の白い残像
        for (int i = 0; i < 5; i++) {
            Vector3 v = {
                (rand() % 7 - 3) * 0.1f,
                -0.5f - (rand() % 5) * 0.1f,
                (rand() % 7 - 3) * 0.1f
            };
            GPUParticleManager::GetInstance()->Emit(
                pos_, v, 0.15f, 1.0f,
                { 0.8f, 0.4f, 1.0f, 0.6f });
        }

        if (obj_) {
            obj_->SetTranslation(pos_);
            // ジャンプ中に少し前傾
            obj_->SetRotation({ -0.4f * t, casterYaw_, 0.0f });
            obj_->Update();
        }
    }
    // =========================================================
    // Phase 3: 回し蹴り (25〜38f) — ヨー回転しながら蹴り足を振り回す
    // =========================================================
    else if (timer_ <= kSpinEnd) {
        float t = static_cast<float>(timer_ - kJumpEnd)
                / static_cast<float>(kSpinEnd - kJumpEnd);

        // 頂点から少し下がる放物線
        float height = kJumpHeight * (1.0f - t * 0.4f);
        Vector3 spinPos = { startPos_.x, baseY_ + height, startPos_.z };
        if (casterBoss_) casterBoss_->SetPosition(spinPos);

        // ヨーを 270度(3/4回転) 回す
        spinYaw_ = casterYaw_ + t * (kPi * 1.5f);

        // 蹴り足の位置：ボスを中心にkKickRadius先を回転
        Vector3 kickOffset = {
            std::sinf(spinYaw_) * kKickRadius,
            0.0f,
            std::cosf(spinYaw_) * kKickRadius
        };
        pos_ = { spinPos.x + kickOffset.x,
                 spinPos.y + 0.5f,
                 spinPos.z + kickOffset.z };

        if (obj_) {
            obj_->SetTranslation(pos_);
            obj_->SetRotation({ 0.0f, spinYaw_, 0.0f });
            obj_->Update();
        }

        // 既存: 紫の弧トレイル
        GPUParticleManager::GetInstance()->Emit(
            pos_, { 0,0,0 }, 0.2f, 1.4f,
            { 0.8f, 0.2f, 1.0f, 0.7f });
        for (int i = 0; i < 4; i++) {
            Vector3 sparkVel = {
                (rand() % 9 - 4) * 0.15f,
                (rand() % 6) * 0.05f,
                (rand() % 9 - 4) * 0.15f
            };
            GPUParticleManager::GetInstance()->Emit(
                pos_, sparkVel, 0.12f, 0.5f,
                { 0.9f, 0.3f, 1.0f, 0.8f });
        }

        // G. 衝撃波コーン (蹴り足の進行方向に扇形)
        {
            // spinYaw_ の接線方向 = 蹴り足が進む向き
            float tangentYaw = spinYaw_ + kPi * 0.5f;
            for (int i = 0; i < 6; i++) {
                float spread = (i - 2.5f) * 0.25f; // -0.625〜+0.625 rad の扇
                float a = tangentYaw + spread;
                float sp = 1.8f + (rand()%5)*0.2f;
                Vector3 v = { std::sinf(a)*sp, 0.05f, std::cosf(a)*sp };
                GPUParticleManager::GetInstance()->Emit(
                    pos_, v, 0.15f, 0.6f, { 1.0f, 0.6f, 1.0f, 0.75f });
            }
        }

        // H. 赤紫の飛沫 (蹴り跡に残る血しぶき風)
        for (int i = 0; i < 3; i++) {
            Vector3 splatVel = {
                -std::sinf(spinYaw_) * (1.0f + (rand()%5)*0.2f) + (rand()%5-2)*0.1f,
                0.1f + (rand()%4)*0.05f,
                -std::cosf(spinYaw_) * (1.0f + (rand()%5)*0.2f) + (rand()%5-2)*0.1f
            };
            GPUParticleManager::GetInstance()->Emit(
                pos_, splatVel, 0.25f, 0.45f,
                { 0.9f, 0.05f, 0.7f, 0.85f }); // 赤紫の飛沫
        }

        // I. 回転リング (ボス中心に水平の光の輪)
        {
            float ringAngle = static_cast<float>(timer_) * 0.35f;
            for (int i = 0; i < 12; i++) {
                float a = (kPi * 2.0f / 12.0f) * i + ringAngle;
                float r = kKickRadius * 0.85f;
                Vector3 ringPos = {
                    spinPos.x + std::cosf(a) * r,
                    spinPos.y + 0.3f,
                    spinPos.z + std::sinf(a) * r
                };
                // リングは速度ゼロで短命に光る
                GPUParticleManager::GetInstance()->Emit(
                    ringPos, {0,0,0}, 0.08f, 0.5f,
                    { 0.8f, 0.5f, 1.0f, 0.65f });
            }
        }

        // ヒット判定: 回し蹴り中は毎フレーム判定 (hasHit_で1回限り)
        if (!hasHit_ && player && !player->IsDead()) {
            Vector3 playerPos = player->GetPosition();
            Vector3 diff = {
                playerPos.x - pos_.x,
                0.0f,
                playerPos.z - pos_.z
            };
            if (Length(diff) < 2.5f) {
                int finalDamage = damage_;
                if (casterBoss_ && casterBoss_->IsAttackDebuffed()) {
                    finalDamage /= 2;
                }
                player->TakeDamage(finalDamage, pos_, 4.5f);
                hasHit_ = true;

                if (camera_) camera_->TriggerShake(0.25f, 12);

                // 命中エフェクト: 紫の爆発
                for (int i = 0; i < 24; i++) {
                    float a = (kPi * 2.0f / 24.0f) * i;
                    float sp = 0.8f + (rand() % 8) * 0.15f;
                    Vector3 sv = {
                        std::cosf(a) * sp,
                        0.3f + (rand() % 6) * 0.1f,
                        std::sinf(a) * sp
                    };
                    Vector4 sc = (rand() % 2 == 0)
                        ? Vector4{ 0.8f, 0.2f, 1.0f, 1.0f }
                        : Vector4{ 1.0f, 0.7f, 1.0f, 1.0f };
                    GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.3f, 0.5f, sc);
                }
            }
        }
    }
    // =========================================================
    // Phase 4: 着地 (38〜45f)
    // =========================================================
    else if (timer_ <= kLandEnd) {
        float t = static_cast<float>(timer_ - kSpinEnd)
                / static_cast<float>(kLandEnd - kSpinEnd);

        // 上から落下
        float height = kJumpHeight * 0.6f * (1.0f - t);
        Vector3 landPos = { startPos_.x, baseY_ + height, startPos_.z };
        if (casterBoss_) casterBoss_->SetPosition(landPos);
        pos_ = landPos;

        // 着地の瞬間 (timer_ == kSpinEnd + 1)
        if (timer_ == kSpinEnd + 1) {
            // 既存: 1段目衝撃波リング
            for (int i = 0; i < 20; i++) {
                float a = (kPi * 2.0f / 20.0f) * i;
                float sp = 2.5f + (rand() % 8) * 0.3f;
                Vector3 v = { std::cosf(a) * sp, 0.2f, std::sinf(a) * sp };
                GPUParticleManager::GetInstance()->Emit(
                    { startPos_.x, baseY_ + 0.1f, startPos_.z },
                    v, 0.3f, 0.8f, { 0.6f, 0.1f, 1.0f, 0.9f });
            }
            if (camera_) camera_->TriggerShake(0.2f, 8);

            // J. 地割れ放射 (8方向に石くれが飛ぶ)
            for (int i = 0; i < 8; i++) {
                float a = (kPi * 2.0f / 8.0f) * i;
                float sp = 3.5f + (rand()%8)*0.4f;
                Vector3 v = { std::cosf(a)*sp, 1.2f+(rand()%6)*0.2f, std::sinf(a)*sp };
                float gray = 0.45f + (rand()%6)*0.05f;
                GPUParticleManager::GetInstance()->Emit(
                    { startPos_.x, baseY_+0.1f, startPos_.z },
                    v, 0.4f, 1.0f, { gray, gray*0.75f, gray*0.5f, 1.0f }); // 岩石色
                // 各方向に小粒を追加で散らす
                for (int j = 0; j < 3; j++) {
                    Vector3 dv = { v.x+(rand()%5-2)*0.3f, v.y+(rand()%5)*0.1f, v.z+(rand()%5-2)*0.3f };
                    GPUParticleManager::GetInstance()->Emit(
                        { startPos_.x, baseY_+0.1f, startPos_.z },
                        dv, 0.2f, 0.5f, { gray, gray*0.6f, 0.2f, 0.8f });
                }
            }

            // K. 爆煙柱 (着地点に大きな煙が立ち上る)
            for (int i = 0; i < 16; i++) {
                float rx = startPos_.x + (rand()%9-4)*0.25f;
                float rz = startPos_.z + (rand()%9-4)*0.25f;
                float sp = 0.3f + (rand()%6)*0.1f;
                Vector3 v = { (rand()%5-2)*0.1f, sp, (rand()%5-2)*0.1f };
                float smoke = 0.25f + (rand()%5)*0.06f;
                GPUParticleManager::GetInstance()->Emit(
                    { rx, baseY_, rz }, v, 0.6f + (rand()%4)*0.1f, 2.0f,
                    { smoke+0.2f, smoke*0.7f, smoke+0.3f, 0.55f }); // 灰紫の煙
            }
        }

        // L. 二段衝撃波 (+5f 後に2波目)
        if (timer_ == kSpinEnd + 5) {
            for (int i = 0; i < 24; i++) {
                float a = (kPi * 2.0f / 24.0f) * i;
                float sp = 4.0f + (rand()%6)*0.25f; // 1波目より大きく
                Vector3 v = { std::cosf(a)*sp, 0.05f, std::sinf(a)*sp };
                GPUParticleManager::GetInstance()->Emit(
                    { startPos_.x, baseY_+0.05f, startPos_.z },
                    v, 0.2f, 0.5f, { 0.7f, 0.2f, 1.0f, 0.7f });
            }
        }

        // K続き: 煙が上に広がり続ける (着地後も数フレーム)
        if (timer_ <= kSpinEnd + 6) {
            for (int i = 0; i < 4; i++) {
                float rx = startPos_.x + (rand()%7-3)*0.3f;
                float rz = startPos_.z + (rand()%7-3)*0.3f;
                Vector3 v = { (rand()%5-2)*0.05f, 0.4f+(rand()%4)*0.08f, (rand()%5-2)*0.05f };
                GPUParticleManager::GetInstance()->Emit(
                    { rx, baseY_, rz }, v, 0.5f, 1.8f,
                    { 0.35f, 0.2f, 0.45f, 0.4f });
            }
        }

        if (obj_) {
            obj_->SetTranslation(pos_);
            obj_->SetRotation({ 0.0f, spinYaw_, 0.0f });
            obj_->Update();
        }
    }
    else {
        if (casterBoss_) casterBoss_->SetPosition(startPos_);
        isFinished_ = true;
    }
}

void BossKickEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }
}
