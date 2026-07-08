#pragma once
// --- エンジン側のファイル ---
#include "Engine/Scene/IScene.h"
#include "Engine/Math/Matrix4x4.h"
#include "Engine/graphics/TextureManager.h"
#include "engine/3d/animation/Animation.h"
#include "engine/3d/animation/CustomAnimation.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/particle/GPUParticleEmitter.h"
#include "engine/utils/EditorManager.h"  // EngineMode のために必要
#include "Skybox.h"
#include "HitEffect.h"

// --- 標準ライブラリ ---
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <list>

// 前方宣言
class DebugCamera;
class Camera;
class Sprite;
class Obj3d;
class DirectXCommon;
class Input;
class RenderTexture;
class PostEffect;
class Player;

	// ゲームプレイシーン
class GamePlayScene : public IScene {
public:
	// 初期化
	void Initialize() override;
	// 終了
	void Finalize() override;
	// 更新
	void Update() override;
	// 描画
	void Draw() override;

	// デバッグ用UIの描画
	void DrawDebugUI() override;
	void ReloadMap();

	GamePlayScene();

	~GamePlayScene();

private: // メンバ変数

	// カメラ
	std::unique_ptr<Camera> camera_ = nullptr;
	// デバッグカメラ
	std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

	// スプライト
	std::unique_ptr<Sprite> sprite_ = nullptr;

	Vector2 spritePos_ = { 100.0f, 100.0f };

	// テクスチャデータ
	std::unordered_map<std::string, TextureData> textures_;

	// デプスステンシル
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

	std::list<std::unique_ptr<HitEffect>> hitEffects_;

	std::string bgmFile_ = "resources/BGMDon.mp3";

	
	std::unique_ptr<Obj3d> testObj_ = nullptr;

	
	std::unique_ptr<Skybox> skybox_ = nullptr;
	
	
	float dissolveThreshold_ = 0.0f; // ディゾルブエフェクトの進行度（0.0で通常、1.0で完全に消える）
	
	Animation testAnimation_;

	std::unique_ptr<SkinnedObj3d> skinnedObj_ = nullptr;

	// アニメーション再生用（編集なし・読み込みのみ）
	CustomAnimationTrack skinnedAnimTrack_;
	float skinnedAnimTime_ = 0.0f;

	
	// GPUパーティクルエミッター
	GPUParticleEmitter emitter_;

	// オーラ用のテクスチャやモデルを管理するオブジェクト
	std::unique_ptr<Obj3d> auraObj_ = nullptr;

	// UVスクロール用のタイマー変数
	float auraUvScrollOffset_ = 0.0f;

	// 一旦新しく作る円柱オーラ用オブジェクト
	std::unique_ptr<Obj3d> auraCylinderObj_ = nullptr;

	// 円柱オーラ用のUVスクロールタイマー
	float auraCylinderScroll_ = 0.0f;

	// デバッグ描画のグリッド表示ON/OFF
	bool showDebugGrid_ = true;

	// --- 実行中に投げるブロック ---
	struct ThrownBlock {
		std::unique_ptr<Obj3d> obj;
		Vector3 pos{ 0.0f, 0.0f, 0.0f };
		Vector3 vel{ 0.0f, 0.0f, 0.0f };
		float life = 8.0f; // 秒。0で消える
	};
	std::list<ThrownBlock> thrownBlocks_;
	void UpdateThrownBlocks(); // 生成・物理・寿命管理

};