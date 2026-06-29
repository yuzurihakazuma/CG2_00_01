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
#include "game/rail/RailField.h"
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

private: // 初期化・更新の内部処理（べた書きを段階ごとに関数化したもの）

	// --- Initialize の分割 ---
	void LoadResources();      // BGM・モデル・テクスチャ・Skybox の読み込み
	void SetupCameras();       // メインカメラ／デバッグカメラの生成・登録
	void SetupDemoObjects();   // 装飾/デモ用オブジェクト（testObj・オーラ・スキンメッシュ）
	void SetupGameplay();      // プレイヤー・敵エディタ・レール・各種シーン部品

	// --- Update の分割 ---
	void UpdateHitStop();              // 踏みつけ等のヒットストップ（時間停止）
	void SyncFromEditors();            // エディタ編集（レール／敵／カメラ要求）をシーンへ反映
	void UpdateCameraAndPostEffect();  // カメラ更新＋シェイク＋踏みつけポストエフェクト
	void HandleModeTransition(EngineMode current); // Edit↔Play 切替時のリセット処理
	void UpdatePlayMode();             // プレイ中のゲーム進行（レール／プレイヤー／敵／踏みつけ／エフェクト）
	void UpdateStompCollision();       // プレイヤーと敵の当たり判定＋踏みつけ演出
	void UpdateSwallow();              // 近くの敵を飲み込んで卵にする（ヨッシー）
	void UpdateSceneVisuals();         // モード問わず毎フレーム行う描画用更新

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

	// --- レール実行時管理（レール本体・緑線マーカー・動くレールを RailField に集約）---
	RailField railField_;
	// エディタの最新レールから railField_ を作り直し、敵も配置し直す（緑線・プレイヤー一本化）
	void SyncRailsFromEditor();

	// --- 敵（レール上を動く。プレイヤーが踏める予定）---
	std::vector<std::unique_ptr<Enemy>> enemies_;
	std::unique_ptr<EnemyEditor>        enemyEditor_; // エネミーの配置テンプレートを管理するエディタ
	void SpawnEnemies();                              // 配置テンプレートを元に敵の実体を再構築する
	int  lastMapLoadVersion_ = -1;                    // マップ読込を検知して敵配置を復元するため

	// --- ヒット時の手応え（踏みつけ等）：ヒットストップ＋カメラシェイク ---
	float   hitStopTimer_  = 0.0f;              // >0 の間は時間を止める（ヒットストップ）
	float   camShakeTimer_ = 0.0f;              // >0 の間カメラを揺らす
	float   camShakeMag_   = 0.0f;              // 揺れの強さ (m)
	Vector3 camPrevShake_ { 0.0f, 0.0f, 0.0f }; // 前フレームに足した揺れ（自己相殺用）
	void TriggerHitFeel(float stopSeconds, float shakeMag); // ヒット時に呼ぶ

	// --- 踏みつけ時のポストエフェクト（A:踏んだ点中心の歪みリップル / D:スポットグロー）---
	float   fxTimer_    = 0.0f;                  // >0 の間だけ歪み＋グローを出す
	Vector3 fxWorldPos_ { 0.0f, 0.0f, 0.0f };    // 効果の中心（踏んだ敵のワールド座標）
	void UpdateStompPostEffect();                // 毎フレーム：中心をスクリーン投影し半径アニメ

	// デバッグ描画のグリッド表示ON/OFF
	bool showDebugGrid_ = true;

};