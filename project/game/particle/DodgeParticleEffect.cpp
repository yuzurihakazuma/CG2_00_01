#include "DodgeParticleEffect.h"

void DodgeParticleEffect::Initialize(){
    // --------------------------------------------------------
    // 1. 回避を始めた瞬間の「ドカン！」（バースト）の設定
    // --------------------------------------------------------
    burstEmitter_ = std::make_unique<GPUParticleEmitter>();
    GPUParticleEmitterData burstData {};
    burstData.name = "DodgeBurst";
    burstData.burstCount = 20;
    burstData.lifeTimeMin = 0.3f;
    burstData.lifeTimeMax = 0.6f;
    burstData.scaleMin = 0.3f;
    burstData.scaleMax = 0.8f;
    burstData.startColor = { 0.2f, 0.9f, 1.0f, 1.0f };
    burstData.endColor = { 0.0f, 0.3f, 1.0f, 0.0f };
    burstData.velocity = { 0.0f, 1.5f, 0.0f };
    burstData.velocitySpread = 4.0f;
    burstEmitter_->SetData(burstData);

    // --------------------------------------------------------
    // 2. 転がっている最中の「キラキラ」（トレイル）の設定
    // --------------------------------------------------------
    trailEmitter_ = std::make_unique<GPUParticleEmitter>();
    GPUParticleEmitterData trailData {};
    trailData.name = "DodgeTrail";
    trailData.burstCount = 5;
    trailData.lifeTimeMin = 0.2f;
    trailData.lifeTimeMax = 0.5f;
    trailData.scaleMin = 0.1f;
    trailData.scaleMax = 0.3f;
    trailData.startColor = { 0.8f, 0.9f, 1.0f, 1.0f };
    trailData.endColor = { 0.2f, 0.8f, 1.0f, 0.0f };
    trailData.velocitySpread = 1.0f;
    trailEmitter_->SetData(trailData);
	// --------------------------------------------------------
    // 見づらくならないように、数は少なく、一瞬で消えるようにする
    // ========================================================
    coreEmitter_ = std::make_unique<GPUParticleEmitter>();
    GPUParticleEmitterData coreData {};
    coreData.name = "DodgeCore";
    coreData.burstCount = 2;                  //  数はたったの2個。これで白飛びを防ぐ。
    coreData.lifeTimeMin = 0.2f;             //  一瞬（0.2s）で消える。
    coreData.lifeTimeMax = 0.3f;
    coreData.scaleMin = 2.0f;                //  最初からキャラより大きいサイズ。
    coreData.scaleMax = 4.0f;                //  さらに大きく広がる。
    coreData.velocity = { 0.0f, 0.0f, 0.0f };  // その場に留まる
    coreData.velocitySpread = 1.0f;          // ゆっくりと外側に広がる動き
    coreData.startColor = { 0.1f, 0.6f, 1.0f, 0.3f }; //  調整：薄い青（アルファ 0.3f）。
    coreData.endColor = { 0.0f, 0.2f, 0.8f, 0.0f }; //  透明になって消える。
    coreEmitter_->SetData(coreData);

}

void DodgeParticleEffect::EmitBurst(const Vector3& pos){
    if ( burstEmitter_ ) {
        auto data = burstEmitter_->GetData();
        data.position = pos;
        data.position.y += 0.5f; // 足元から少し上
        burstEmitter_->SetData(data);
        burstEmitter_->Burst();
    }

    if ( coreEmitter_ ) {
        auto data = coreEmitter_->GetData();
        data.position = pos;
        data.position.y += 1.0f; // 🌟 調整：プレイヤーの中心（腰付近）から発生。
        coreEmitter_->SetData(data);
        coreEmitter_->Burst();
    }
}

void DodgeParticleEffect::EmitTrail(const Vector3& pos, const Vector3& dodgeDirection){
    if ( trailEmitter_ ) {
        auto data = trailEmitter_->GetData();
        data.position = pos;
        data.position.y += 1.0f; // 体の中心くらい
        // 後ろに向かって火花が飛ぶようにする
        data.velocity = { -dodgeDirection.x * 2.0f, 0.5f, -dodgeDirection.z * 2.0f };
        trailEmitter_->SetData(data);
        trailEmitter_->Burst();
    }
}