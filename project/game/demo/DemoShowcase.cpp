#include "game/demo/DemoShowcase.h"

#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/2d/Sprite.h"
#include "engine/audio/AudioManager.h"
#include "engine/base/Input.h"
#include "engine/camera/Camera.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/particle/ParticleManager.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/sdf/SDFVolumeObject.h"
#include "engine/utils/EditorManager.h"
#include "engine/math/Matrix4x4.h"
#include "Bloom.h"
#include "HitEffect.h"
#include "InstancedGroup.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#include <cmath>

using namespace MatrixMath;

DemoShowcase::DemoShowcase() = default;
DemoShowcase::~DemoShowcase() = default;

void DemoShowcase::Initialize(ID3D12GraphicsCommandList* commandList,
                              std::unordered_map<std::string, TextureData>& textures){
    // --- 回転キューブ（ディゾルブ＋ブルーム。エディタのギズモ操作対象）---
    cubeAnimation_ = LoadAnimationFromFile("resources/AnimatedCube", "AnimatedCube.gltf");
    dissolveCube_ = Obj3d::Create("animatedCube");
    if ( dissolveCube_ ) {
        dissolveCube_->SetEnvironmentMap(textures["skybox"].srvIndex);
        dissolveCube_->SetPipelineType(PipelineType::Object3D_CullNone);
        dissolveCube_->SetTranslation({ 0.0f, 0.0f, 5.0f });
        dissolveCube_->SetNoiseTexture(textures["noise0"].srvIndex);
        dissolveCube_->SetDissolveThreshold(0.0f);
        Bloom::GetInstance()->SetTargetEmissivePower(&dissolveCube_->GetModel()->GetMaterial()->emissive);
        EditorManager::GetInstance()->SetGizmoTarget(dissolveCube_.get()); // ギズモ操作対象
    }

    // --- リングオーラ（地面に広がる魔法陣風）---
    ModelManager::GetInstance()->CreateRingModel("auraRing", 32, 1.0f, 0.2f);
    auraRing_ = Obj3d::Create("auraRing");
    if ( auraRing_ ) {
        auraRing_->GetModel()->SetTexture(textures["gradationLine"].srvIndex);
        auraRing_->SetNoiseTexture(textures["gradationLine"].srvIndex); // ディゾルブ無効化のダミー
        auraRing_->GetModel()->GetMaterial()->enableLighting = 0;
        auraRing_->SetRotation({ 1.5708f, 0.0f, 0.0f }); // X軸90度で地面に倒す
        auraRing_->SetTranslation({ 0.0f, 0.1f, 5.0f });
        auraRing_->SetPipelineType(PipelineType::Object3D_Additive);
    }

    // --- 円柱オーラ ---
    ModelManager::GetInstance()->CreateCylinderModel("auraCylinderModel", 32, 1.5f, 4.0f);
    auraCylinder_ = Obj3d::Create("auraCylinderModel");
    if ( auraCylinder_ ) {
        auraCylinder_->GetModel()->SetTexture(textures["gradationLine"].srvIndex);
        auraCylinder_->SetDissolveThreshold(-1.0f); // 透明化させない
        auraCylinder_->GetModel()->GetMaterial()->enableLighting = 0;
        auraCylinder_->SetTranslation({ -5.0f, 2.0f, 5.0f });
        auraCylinder_->SetPipelineType(PipelineType::Object3D_Additive);
    }

    // --- ブロックのインスタンシング一括描画 ---
    blockGroup_ = std::make_unique<InstancedGroup>();
    blockGroup_->Initialize("block", 10000);
    blockGroup_->SetNoiseTexture(textures["uvChecker"].srvIndex);

    // --- GPUパーティクルのエミッター（ノードエディタからも操作できるよう登録）---
    GPUParticleEmitterData emitterData;
    emitterData.position = { 0.0f, 0.0f, 0.0f };
    emitterData.emitRate = 20.0f;
    particleEmitter_.SetData(emitterData);
    EditorManager::GetInstance()->SetParticleEmitter(&particleEmitter_);

    // --- SDFボリュームの卵（メッシュ無しのレイマーチング表示デモ。看板の横に置く）---
    sdfEgg_ = std::make_unique<SDFVolumeObject>();
    if ( sdfEgg_->Load("resources/sdf3d/egg.sdf3d", commandList) ) {
        sdfEgg_->SetTranslation({ 4.0f, 3.0f, 3.0f });
        sdfEgg_->SetScale(2.0f);
        sdfEgg_->SetColor({ 0.98f, 0.96f, 0.86f, 1.0f }); // カラーボリュームが無い時の予備色
        // モーフのデモ：スライダーで卵⇔敵ボールに変形できる
        sdfEgg_->LoadMorphTarget("resources/sdf3d/enemyBall.sdf3d", commandList);
    }

    // --- uvChecker のスクリーンスプライト（更新のみ。描画は現状オフ）---
    screenSprite_ = Sprite::Create(textures["uvChecker"].srvIndex, { 100.0f, 100.0f });
}

// プレイ中のデモ入力＋HitEffect の進行
void DemoShowcase::UpdatePlay(Input* input, Camera* camera,
                              std::unordered_map<std::string, TextureData>& textures,
                              const std::string& bgmFile){
    // スペースキー：エフェクト発生＋BGM再生
    if ( input->Triggerkey(DIK_SPACE) ) {
        AudioManager::GetInstance()->PlayWave(bgmFile);
        auto newEffect = std::make_unique<HitEffect>();
        newEffect->Initialize({ 5.0f, 0.0f, 5.0f }, camera,
                              textures["circle2"].srvIndex, textures["skybox"].srvIndex);
        hitEffects_.push_back(std::move(newEffect));
    }
    // パーティクル発生
    if ( input->Triggerkey(DIK_P) ) {
        ParticleManager::GetInstance()->Emit("Circle", { 0.0f, 0.0f, 0.0f }, 10);
    }

    // エフェクトの更新と死んだものの削除
    for ( auto& effect : hitEffects_ ) { effect->Update(); }
    hitEffects_.remove_if([](const std::unique_ptr<HitEffect>& effect){ return effect->IsDead(); });
}

// モード問わず毎フレームの見た目更新
void DemoShowcase::UpdateVisuals(Input* input, Camera* camera){
    // 回転キューブ
    if ( dissolveCube_ ) { dissolveCube_->Update(); }

    // 円柱オーラ（UVスクロール）
    if ( auraCylinder_ ) {
        auraCylinderScroll_ += 1.0f * ( 1.0f / 60.0f );
        if ( auraCylinderScroll_ > 1.0f ) { auraCylinderScroll_ -= 1.0f; }
        auraCylinder_->GetModel()->GetMaterial()->uvTransform = MakeTranslate({ 0.0f, auraCylinderScroll_, 0.0f });
        auraCylinder_->Update();
    }

    // リングオーラ（UVスクロール）
    if ( auraRing_ ) {
        auraRingScroll_ += 1.0f * ( 1.0f / 60.0f );
        if ( auraRingScroll_ > 1.0f ) { auraRingScroll_ -= 1.0f; }
        auraRing_->GetModel()->GetMaterial()->uvTransform = MakeTranslate({ 0.0f, auraRingScroll_, 0.0f });
        auraRing_->Update();
    }

    // スクリーンスプライト
    if ( screenSprite_ ) { screenSprite_->Update(); }

    // SDF卵（カメラ追従のCB更新＋エロージョン自動デモ）
    if ( sdfEgg_ ) {
        if ( sdfErodeAuto_ ) {
            // 溶けて消える(erode→1.0) ⇔ 芯から生える(→0.0) をゆっくり繰り返す
            sdfErodeTime_ += 1.0f / 60.0f;
            float wave = 0.5f * ( 1.0f - std::cos(sdfErodeTime_ * 1.2f) ); // 0→1→0
            sdfEgg_->SetErode(wave);
        }
        sdfEgg_->Update();
    }

    // ブロック群＋GPUパーティクル
    for ( auto& block : blocks_ ) { block->Update(); }
    if ( input->Triggerkey(DIK_G) ) {
        GPUParticleManager::GetInstance()->Update(1.0f / 60.0f, camera);
    }
    particleEmitter_.Update(1.0f / 60.0f);
    if ( blockGroup_ ) { blockGroup_->Update(blocks_); }
}

void DemoShowcase::OnPlayStart(){
    hitEffects_.clear();
}

void DemoShowcase::DrawOpaque(){
    if ( dissolveCube_ ) { dissolveCube_->Draw(); }
}

void DemoShowcase::DrawInstanced(Camera* camera){
    if ( blockGroup_ ) { blockGroup_->Draw(camera); }
}

void DemoShowcase::DrawAdditive(){
    if ( auraCylinder_ ) { auraCylinder_->Draw(); }
    if ( auraRing_ )     { auraRing_->Draw(); }
    for ( auto& effect : hitEffects_ ) { effect->Draw(); }
}

void DemoShowcase::DrawSdf(ID3D12GraphicsCommandList* commandList){
    if ( sdfEgg_ ) { sdfEgg_->Draw(commandList); }
}

void DemoShowcase::DrawImGui(){
#ifdef USE_IMGUI
    ImGui::Begin("SDF卵 (エロージョン)");
    if ( sdfEgg_ && sdfEgg_->IsLoaded() ) {
        ImGui::TextDisabled("距離場に足す量で形が痩せる/太る（SDFだけの演出）");
        ImGui::Checkbox("自動アニメ（溶ける⇔生える）", &sdfErodeAuto_);
        if ( sdfErodeAuto_ ) {
            ImGui::TextDisabled("erode: %.2f m", sdfEgg_->RefErode());
        } else {
            ImGui::SliderFloat("erode (m)", &sdfEgg_->RefErode(), -0.3f, 1.2f);
        }
        ImGui::DragFloat3("位置", &sdfEgg_->RefTranslation().x, 0.1f);
        ImGui::DragFloat("スケール", &sdfEgg_->RefScale(), 0.05f, 0.2f, 10.0f);
        if ( sdfEgg_->HasMorph() ) {
            // 距離場のlerpによる連続変形（0=卵 / 1=敵ボール。中間も破綻しない）
            ImGui::SliderFloat("モーフ (卵→ボール)", &sdfEgg_->RefMorphT(), 0.0f, 1.0f);
        }
    } else {
        ImGui::TextDisabled("resources/sdf3d/egg.sdf3d が読み込まれていません");
    }
    ImGui::End();
#endif
}
