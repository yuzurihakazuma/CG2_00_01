#include "StompEffect.h"
#include <random>
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/graphics/PipelineType.h"
#include "engine/graphics/TextureManager.h"

void StompEffect::Initialize(const Vector3& position, Camera* camera, uint32_t textureIndex, uint32_t envMapIndex) {
    std::random_device seed_gen;
    std::mt19937 engine(seed_gen());
    
    // ランダム生成器の設定
    std::uniform_real_distribution<float> rotDist(0.0f, 3.141592f * 2.0f);
    std::uniform_real_distribution<float> velXDist(-3.0f, 3.0f);
    std::uniform_real_distribution<float> velYDist(6.0f, 11.0f);   // 画面上で適度に弾む速度
    std::uniform_real_distribution<float> velZDist(-1.5f, 1.5f);   // Z軸（奥行き）移動をマイルドに適用
    std::uniform_real_distribution<float> scaleDist(0.2f, 0.35f);   // 星のサイズ
    std::uniform_real_distribution<float> lifeDist(0.45f, 0.75f);
    
    // 煙用のランダム
    std::uniform_real_distribution<float> smokeOffsetDist(-0.4f, 0.4f);
    std::uniform_real_distribution<float> smokeScaleDist(0.5f, 1.0f);
    std::uniform_real_distribution<float> smokeRotSpeedDist(-2.0f, 2.0f);

    // 星の自転回転速度用ランダム
    std::uniform_real_distribution<float> rotSpeedDist(-5.0f, 5.0f);

    ModelManager* modelManager = ModelManager::GetInstance();
    
    // 1. 各種専用モデルの登録 (干渉防止)
    if (!modelManager->FindModel("stompCoreSphere")) {
        modelManager->CreateSphereModel("stompCoreSphere", 16);
    }
    if (!modelManager->FindModel("stompCylinder")) {
        // 衝撃波用の円柱モデル (リング状に広げる)
        modelManager->CreateCylinderModel("stompCylinder", 32, 1.0f, 1.0f);
    }
    if (!modelManager->FindModel("stompStar")) {
        // 【新規追加】3D星型モデルの生成 (外径0.4f, 内径0.17f, 厚み0.12f)
        modelManager->CreateStarModel("stompStar", 0.4f, 0.17f, 0.12f);
    }
    if (!modelManager->FindModel("stompSmoke")) {
        modelManager->CreatePlaneModel("stompSmoke");
    }

    // 2. 予兆のコア球体 (Core) の生成 (純白ソリッド光球)
    {
        Core core;
        core.obj = Obj3d::Create("stompCoreSphere");
        if (core.obj) {
            core.obj->SetCamera(camera);
            core.obj->SetTranslation(position + Vector3{0.0f, 0.3f, 0.0f});
            core.obj->SetScale({ 0.1f, 0.1f, 0.1f });

            core.obj->SetPipelineType(PipelineType::Object3D_Additive);
            core.obj->SetEnvironmentMap(envMapIndex);
            
            if (auto model = core.obj->GetModel()) {
                model->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                if (auto mat = model->GetMaterial()) {
                    mat->enableLighting = 0;
                    mat->emissive = 1.0f;
                }
            }

            core.lifeTime = 0.08f;
            core.currentTime = 0.0f;
            core.startScale = 0.1f;
            core.endScale = 1.2f;

            cores_.push_back(std::move(core));
        }
    }

    // 3. 円柱型衝撃波の生成 (プレスされ平たく潰れながら広がるリング演出)
    {
        Cylinder cyl;
        cyl.obj = Obj3d::Create("stompCylinder");
        if (cyl.obj) {
            cyl.obj->SetCamera(camera);
            // 地面よりほんの少し浮かせて描画のチラつき(Zファイティング)を防ぐ
            cyl.obj->SetTranslation(position + Vector3{0.0f, 0.02f, 0.0f});
            cyl.obj->SetScale({ 0.1f, 0.1f, 0.1f });

            cyl.obj->SetPipelineType(PipelineType::Object3D_Additive);
            cyl.obj->SetEnvironmentMap(envMapIndex);

            // ノイズテクスチャとディゾルブしきい値の設定
            uint32_t noiseTexIndex = TextureManager::GetInstance()->Load("Resources/noise0.png").srvIndex;
            cyl.obj->SetNoiseTexture(noiseTexIndex);
            cyl.obj->SetDissolveThreshold(0.0f); // 初期はディゾルブしない

            if (auto model = cyl.obj->GetModel()) {
                // テクスチャをノイズ画像に設定してエフェクトらしい質感にする
                model->SetTexture(noiseTexIndex);

                model->SetColor({ 0.4f, 0.85f, 1.0f, 0.8f }); // 水色
                if (auto mat = model->GetMaterial()) {
                    mat->enableLighting = 0;
                    mat->emissive = 1.0f;
                }
            }

            cyl.lifeTime = 0.55f;
            cyl.currentTime = 0.0f;
            cyl.startScale = 0.1f;
            cyl.endScale = 2.0f;       // 横方向の最大半径 (星の輪と調和するサイズ)
            cyl.heightScale = 1.0f;

            cylinders_.push_back(std::move(cyl));
        }
    }

    // 4. 【新規星型演出】放射状に広がる星型の輪 (StarRing) の生成 (ヨッシー風王道エフェクト)
    // 8つの星を円状に配置し、外周に向かって弾け飛ぶようにする
    for (int i = 0; i < 8; ++i) {
        StarRing star;
        star.obj = Obj3d::Create("stompStar");
        if (star.obj) {
            star.obj->SetCamera(camera);
            star.obj->SetIsBillboard(true); // カメラに正対させて視認性100%に

            // 放射状の方向ベクトル (X-Y平面)
            float angle = i * (2.0f * 3.14159265f / 8.0f);
            star.direction = { std::sin(angle), std::cos(angle), 0.0f };

            // 初期位置を少し外側に配置
            star.obj->SetTranslation(position + Vector3{0.0f, 0.3f, 0.0f} + star.direction * 0.2f);
            
            // 初期回転
            star.rotation = { 0.0f, 0.0f, rotDist(engine) };
            star.obj->SetRotation(star.rotation);

            // Z軸自転速度
            star.rotationSpeed = { 0.0f, 0.0f, rotSpeedDist(engine) };

            // サイズ
            star.baseScale = 0.6f;
            star.obj->SetScale({ star.baseScale, star.baseScale, star.baseScale });

            star.obj->SetPipelineType(PipelineType::Object3D_Additive);
            star.obj->SetEnvironmentMap(envMapIndex);

            // ソリッドなイエローで発光
            if (auto model = star.obj->GetModel()) {
                model->SetColor({ 1.0f, 0.9f, 0.1f, 1.0f });
                if (auto mat = model->GetMaterial()) {
                    mat->enableLighting = 0;
                    mat->emissive = 1.0f;
                }
            }

            star.lifeTime = 0.35f; // すばやく広がる
            star.currentTime = 0.0f;
            star.startRadius = 0.2f;
            star.endRadius = 1.3f; // 雑魚敵の少し外側まで広がる

            starRings_.push_back(std::move(star));
        }
    }

    // 5. 飛び散るポップな3D星 (PopParticle) の生成 (くるくる回るカラフルな星ブロック)
    Vector4 colors[] = {
        { 1.0f, 0.35f, 0.6f, 1.0f }, // ポップピンク
        { 1.0f, 0.85f, 0.1f, 1.0f }, // ポップイエロー
        { 0.2f, 0.95f, 0.5f, 1.0f }  // ポップグリーン
    };
    std::uniform_int_distribution<int> colorChoice(0, 2);

    for (int i = 0; i < 8; ++i) {
        PopParticle part;
        part.obj = Obj3d::Create("stompStar");

        if (part.obj) {
            part.obj->SetCamera(camera);
            part.obj->SetTranslation(position + Vector3{0.0f, 0.2f, 0.0f});

            // 初速（2.5D的に手前奥もマイルドに混ぜつつ、画面内の放物線を描かせる）
            part.velocity = { velXDist(engine), velYDist(engine), velZDist(engine) };

            // 初期回転 (3軸ランダム)
            part.rotation = { rotDist(engine), rotDist(engine), rotDist(engine) };
            part.obj->SetRotation(part.rotation);

            // 自転回転速度
            part.rotationSpeed = { rotSpeedDist(engine), rotSpeedDist(engine), rotSpeedDist(engine) };

            // サイズ
            float s = scaleDist(engine);
            Vector3 initScale = { s, s, s };
            part.obj->SetScale(initScale);
            part.baseScale = initScale;

            // 色とマテリアル (ソリッド発光)
            part.color = colors[colorChoice(engine)];
            part.obj->SetPipelineType(PipelineType::Object3D_Additive);
            part.obj->SetEnvironmentMap(envMapIndex);
            
            if (auto model = part.obj->GetModel()) {
                model->SetColor(part.color);
                if (auto mat = model->GetMaterial()) {
                    mat->enableLighting = 0;
                    mat->emissive = 1.0f;
                }
            }

            part.lifeTime = lifeDist(engine);
            part.currentTime = 0.0f;

            popParticles_.push_back(std::move(part));
        }
    }

    // 6. 足元のモコモコ煙 (Smoke) の生成
    for (int i = 0; i < 5; ++i) {
        Smoke smoke;
        smoke.obj = Obj3d::Create("stompSmoke");

        if (smoke.obj) {
            smoke.obj->SetCamera(camera);
            smoke.obj->SetIsBillboard(true);
            
            smoke.offset = { 
                smokeOffsetDist(engine), 
                smokeOffsetDist(engine) * 0.4f, 
                smokeOffsetDist(engine) * 0.25f 
            };
            smoke.obj->SetTranslation(position + smoke.offset);

            smoke.baseScale = smokeScaleDist(engine);
            smoke.obj->SetScale({ smoke.baseScale, smoke.baseScale, 1.0f });

            smoke.obj->SetPipelineType(PipelineType::Object3D_Additive);
            smoke.obj->SetEnvironmentMap(envMapIndex);
            
            if (auto model = smoke.obj->GetModel()) {
                model->SetTexture(textureIndex);
                model->SetColor({ 0.9f, 0.92f, 0.95f, 0.55f });
                if (auto mat = model->GetMaterial()) {
                    mat->enableLighting = 0;
                }
            }

            smoke.lifeTime = lifeDist(engine) * 0.85f;
            smoke.currentTime = 0.0f;
            smoke.rotationSpeed = smokeRotSpeedDist(engine);
            smoke.currentRotation = rotDist(engine);

            smokes_.push_back(std::move(smoke));
        }
    }
}

void StompEffect::Update() {
    isDead_ = true;

    // 1. コア球体の更新
    for (auto& core : cores_) {
        if (core.currentTime < core.lifeTime) {
            core.currentTime += 1.0f / 60.0f;
            isDead_ = false;

            float progress = core.currentTime / core.lifeTime;
            if (progress > 1.0f) progress = 1.0f;

            float curScale = core.startScale + (core.endScale - core.startScale) * progress;
            core.obj->SetScale({ curScale, curScale, curScale });

            float alpha = 1.0f - progress;
            if (alpha < 0.0f) alpha = 0.0f;
            if (core.obj->GetModel()) {
                core.obj->GetModel()->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
            }

            core.obj->Update();
        }
    }

    // 2. 円柱型（リング）衝撃波の更新 (上から叩きつけられて潰れながら広がるエネルギーリング、その後ディゾルブ消滅)
    for (auto& cyl : cylinders_) {
        if (cyl.currentTime < cyl.lifeTime) {
            cyl.currentTime += 1.0f / 60.0f;
            isDead_ = false;

            float progress = cyl.currentTime / cyl.lifeTime;
            if (progress > 1.0f) progress = 1.0f;

            float scaleRatio = 0.45f;
            float curScale = 0.0f;
            float curHeight = 0.0f;
            float dissolveThreshold = 0.0f;

            if (progress < scaleRatio) {
                // 1. 叩きつけ＆プレス膨張フェーズ (縦に長い円柱から、横へ極めて平たく押し潰された薄いリングへと広がる)
                float scaleProgress = progress / scaleRatio;
                float t = 1.0f - scaleProgress;
                float easeOut = 1.0f - t * t;

                // 横幅 (X, Z) は急速に拡大
                curScale = cyl.startScale + (cyl.endScale - cyl.startScale) * easeOut;
                // 高さ (Y) は最初は 1.2f (縦長の筒) ➔ 着地衝撃で急激に押しつぶされて極薄リング (0.05f) へ収縮
                curHeight = (1.2f * (1.0f - scaleProgress)) + (0.05f * scaleProgress);
                
                dissolveThreshold = 0.0f;
            } else {
                // 2. ディゾルブ消滅フェーズ (サイズは最大を維持し、ザラザラと消える)
                float dissolveProgress = (progress - scaleRatio) / (1.0f - scaleRatio);
                curScale = cyl.endScale + (cyl.endScale * 0.05f) * dissolveProgress;
                curHeight = 0.05f;
                
                dissolveThreshold = dissolveProgress;
            }

            cyl.obj->SetScale({ curScale, curHeight, curScale });
            cyl.obj->SetDissolveThreshold(dissolveThreshold);

            if (cyl.obj->GetModel()) {
                // 色の変化: 最初は「真っ白な着地フラッシュ(白熱)」、広がるにつれて「水色」に冷えながらフェードアウト
                float colorLerp = progress;
                float r = 1.0f + (0.4f - 1.0f) * colorLerp;
                float g = 1.0f + (0.85f - 1.0f) * colorLerp;
                float b = 1.0f; // 1.0f は維持
                
                // アルファ値の計算 (ディゾルブが映えるよう、消滅フェーズに入ってからマイルドに下げる)
                float alpha = 0.9f;
                if (progress >= scaleRatio) {
                    float dissolveProgress = (progress - scaleRatio) / (1.0f - scaleRatio);
                    alpha = 0.9f * (1.0f - dissolveProgress * 0.5f);
                }

                cyl.obj->GetModel()->SetColor({ r, g, b, alpha });
            }

            cyl.obj->Update();
        }
    }

    // 2-B. 星型衝撃波リングの更新
    for (auto& star : starRings_) {
        if (star.currentTime < star.lifeTime) {
            star.currentTime += 1.0f / 60.0f;
            isDead_ = false;

            float progress = star.currentTime / star.lifeTime;
            if (progress > 1.0f) progress = 1.0f;

            // イージングアウトで広げる半径
            float t = 1.0f - progress;
            float easeOut = 1.0f - t * t;
            float curRadius = star.startRadius + (star.endRadius - star.startRadius) * easeOut;

            // 中心からの位置を設定
            // Y軸に少し浮かせる
            Vector3 basePos = star.obj->GetTranslation(); // 前フレームの位置から移動させるか、初期位置基準で計算
            // ここでは初期中心位置から放射状に移動させるのが最も安定するため
            // star.obj に position 自体は持っていないため、directionを用いて毎回計算する
            // 敵の足元中心は position (Initializeの引数) なので、それをベースにする
            // ただし StompEffect には position メンバがない。
            // そこで、star.objの初期位置（Translate）から、毎フレーム速度的に移動させるアプローチを取る
            Vector3 pos = star.obj->GetTranslation();
            // 毎フレーム一定の勢いで direction に沿って広がる
            // (endRadius - startRadius) / lifeTime が大体の移動速度
            float speed = (star.endRadius - star.startRadius) / star.lifeTime;
            // イージングをかけるため、速度は徐々に減衰
            float curSpeed = speed * (1.0f - progress) * 1.5f; // イージング感覚
            pos += star.direction * curSpeed * (1.0f / 60.0f);
            star.obj->SetTranslation(pos);

            // 自転
            star.rotation += star.rotationSpeed * (1.0f / 60.0f);
            star.obj->SetRotation(star.rotation);

            // スケール縮小
            float aliveRatio = 1.0f - progress;
            float curScale = star.baseScale * aliveRatio;
            star.obj->SetScale({ curScale, curScale, curScale });

            // フェードアウト
            if (star.obj->GetModel()) {
                star.obj->GetModel()->SetColor({ 1.0f, 0.9f, 0.1f, aliveRatio });
            }

            star.obj->Update();
        }
    }

    // 3. ポップ3D星パーティクルの更新
    for (auto& part : popParticles_) {
        if (part.currentTime < part.lifeTime) {
            part.currentTime += 1.0f / 60.0f;
            isDead_ = false;

            // 重力
            part.velocity.y -= 12.0f * (1.0f / 60.0f);
            
            // 移動
            Vector3 pos = part.obj->GetTranslation();
            pos += part.velocity * (1.0f / 60.0f);
            part.obj->SetTranslation(pos);

            // 3軸自転
            part.rotation += part.rotationSpeed * (1.0f / 60.0f);
            part.obj->SetRotation(part.rotation);

            // 縮小
            float aliveRatio = 1.0f - (part.currentTime / part.lifeTime);
            if (aliveRatio < 0.0f) aliveRatio = 0.0f;

            Vector3 newScale = part.baseScale * aliveRatio;
            part.obj->SetScale(newScale);

            // フェードアウト
            if (part.obj->GetModel()) {
                Vector4 curColor = part.color;
                curColor.w = aliveRatio;
                part.obj->GetModel()->SetColor(curColor);
            }

            part.obj->Update();
        }
    }

    // 4. モコモコ煙の更新
    for (auto& smoke : smokes_) {
        if (smoke.currentTime < smoke.lifeTime) {
            smoke.currentTime += 1.0f / 60.0f;
            isDead_ = false;

            float progress = smoke.currentTime / smoke.lifeTime;
            if (progress > 1.0f) progress = 1.0f;

            float curScale = smoke.baseScale * (1.0f + progress * 0.7f);
            smoke.obj->SetScale({ curScale, curScale, 1.0f });

            smoke.currentRotation += smoke.rotationSpeed * (1.0f / 60.0f);
            smoke.obj->SetRotation({ 0.0f, 0.0f, smoke.currentRotation });

            Vector3 pos = smoke.obj->GetTranslation();
            pos.y += 0.8f * (1.0f / 60.0f);
            smoke.obj->SetTranslation(pos);

            float alpha = 0.55f * (1.0f - progress);
            if (alpha < 0.0f) alpha = 0.0f;
            if (smoke.obj->GetModel()) {
                smoke.obj->GetModel()->SetColor({ 0.9f, 0.92f, 0.95f, alpha });
            }

            smoke.obj->Update();
        }
    }
}

void StompEffect::Draw() {
    for (auto& core : cores_) {
        if (core.currentTime < core.lifeTime) {
            core.obj->Draw();
        }
    }
    for (auto& cyl : cylinders_) {
        if (cyl.currentTime < cyl.lifeTime) {
            cyl.obj->Draw();
        }
    }
    for (auto& star : starRings_) {
        if (star.currentTime < star.lifeTime) {
            star.obj->Draw();
        }
    }
    for (auto& smoke : smokes_) {
        if (smoke.currentTime < smoke.lifeTime) {
            smoke.obj->Draw();
        }
    }
    for (auto& part : popParticles_) {
        if (part.currentTime < part.lifeTime) {
            part.obj->Draw();
        }
    }
}
