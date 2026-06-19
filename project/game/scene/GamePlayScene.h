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
#include "InstancedGroup.h"

#include "engine/rail/SplineRail.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/EnemyEditor.h"

#include "Skybox.h"
#include "HitEffect.h"
#include "StompEffect.h"

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

	// 3Dオブジェクト
	std::vector<std::unique_ptr<Obj3d>> object3ds_;

	// スプライト
	std::vector<std::unique_ptr<Sprite>> sprites_;
	std::unique_ptr<Sprite> sprite_ = nullptr;

	Vector2 spritePos_ = { 100.0f, 100.0f };

	// テクスチャデータ
	std::unordered_map<std::string, TextureData> textures_;

	// デプスステンシル
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

	std::list<std::unique_ptr<HitEffect>> hitEffects_;
	std::list<std::unique_ptr<StompEffect>> stompEffects_;

	std::string bgmFile_ = "resources/BGMDon.mp3";

	
	std::unique_ptr<Obj3d> testObj_ = nullptr;

	
	std::unique_ptr<Skybox> skybox_ = nullptr;
	
	
	float dissolveThreshold_ = 0.0f; // ディゾルブエフェクトの進行度（0.0で通常、1.0で完全に消える）
	
	Animation testAnimation_;
	// ブロックの一括描画用グループ
	std::unique_ptr<InstancedGroup> blockGroup_ = nullptr;
	std::vector<std::unique_ptr<Obj3d>> blocks_;

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
	

	EngineMode prevMode_ = EngineMode::Edit;

	// 一旦新しく作る円柱オーラ用オブジェクト
	std::unique_ptr<Obj3d> auraCylinderObj_ = nullptr;

	// 円柱オーラ用のUVスクロールタイマー
	float auraCylinderScroll_ = 0.0f;

	std::unique_ptr<Player> player_ = nullptr;
	std::vector<SplineRail> splineRails_; // スプラインレール本体

	// --- 敵（レール上を動く。プレイヤーが踏める予定）---
	std::vector<std::unique_ptr<Enemy>> enemies_;
	std::unique_ptr<EnemyEditor>        enemyEditor_; // エネミーの配置テンプレートを管理するエディタ
	void SpawnEnemies();                              // 配置テンプレートを元に敵の実体を再構築する

	// --- レール経路の可視化（プレイヤーが通る曲線を線で表示）---
	std::vector<std::unique_ptr<Obj3d>> railMarkers_;
	bool showRailMarkers_ = true;
	// splineRails_ を距離サンプルして線マーカーを作り直す
	void BuildRailMarkers();

	// --- 動くレール（ムービングプラットフォーム）---
	float railAnimTime_ = 0.0f;            // 動くレール用の経過時間
	std::vector<int>     railMarkerRail_;  // 各マーカーが属するレール番号（railMarkers_と同じ並び）
	std::vector<Vector3> railMarkerBase_;  // 各マーカーの基準位置（オフセット0の位置）
	void UpdateRailMotion(float dt);       // animOffset を進めてマーカーを追従させる
	void UpdateRailMarkerPositions();      // マーカー位置 = 基準位置 + animOffset

	// --- エディタのレール編集をゲーム側に同期する（緑線・プレイヤーを一本化）---
	int  lastRailVersion_ = -1;     // 直近に同期したエディタの編集世代
	void SyncRailsFromEditor();     // エディタの最新レールから splineRails_ を作り直す

	// デバッグ描画のグリッド表示ON/OFF
	bool showDebugGrid_ = true;

};