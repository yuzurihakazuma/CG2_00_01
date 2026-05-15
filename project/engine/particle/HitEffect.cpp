#include "HitEffect.h"
#include <random>
#include "engine/3d/model/Model.h"
#include "engine/graphics/PipelineType.h"

void HitEffect::Initialize(const Vector3& position, Camera* camera, uint32_t textureIndex, uint32_t envMapIndex) {
    std::random_device seed_gen;
    std::mt19937 engine(seed_gen());
    std::uniform_real_distribution<float> rotDist(0.0f, 3.141592f * 2.0f); // 0～360度
    std::uniform_real_distribution<float> scaleYDist(2.0f, 5.0f);         // 長さのランダム幅（ガチ小さめ）

    for (int i = 0; i < 8; ++i) {
        Spark spark;
        spark.obj = Obj3d::Create("plane");

        if (spark.obj) {
            spark.obj->SetCamera(camera);
            spark.obj->SetTranslation(position);

            // 【画像再現ポイント①】放射状に広がるランダム回転
            spark.obj->SetRotation({ 0.0f, 0.0f, rotDist(engine) });

            // 【画像再現ポイント②】ガチで細く、鋭いサイズ設定
            // X(幅)を極限まで細く(0.005)し、Y(長さ)にランダム性を持たせる
            Vector3 initScale = { 0.5f, scaleYDist(engine), 1.0f };
            spark.obj->SetScale(initScale);
            spark.baseScale = initScale;

            // オブジェクト自身に描画設定を持たせる
            spark.obj->SetPipelineType(PipelineType::Object3D_Additive);
            spark.obj->SetEnvironmentMap(envMapIndex);
            spark.obj->GetModel()->SetTexture(textureIndex);

            // 寿命設定（一瞬で消えるのが格好いい）
            spark.lifeTime = 0.15f;
            spark.currentTime = 0.0f;

            sparks_.push_back(std::move(spark));
        }
    }
}

void HitEffect::Update() {
    isDead_ = true;

    for (auto& spark : sparks_) {
        if (spark.currentTime < spark.lifeTime) {
            spark.currentTime += 1.0f / 60.0f;
            isDead_ = false;

            // 【消える演出】時間経過でサイズを 1.0 → 0.0 へ
            float aliveRatio = 1.0f - (spark.currentTime / spark.lifeTime);
            if (aliveRatio < 0.0f) aliveRatio = 0.0f;

            Vector3 newScale = spark.baseScale;
            newScale.x *= aliveRatio; // どんどん細く
            newScale.y *= aliveRatio; // どんどん短く
            spark.obj->SetScale(newScale);

            spark.obj->Update();
        }
    }
}

void HitEffect::Draw() {
    for (auto& spark : sparks_) {
        if (spark.currentTime < spark.lifeTime) {
            spark.obj->Draw();
        }
    }
}