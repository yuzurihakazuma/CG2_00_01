#pragma once
// --- エンジン側のファイル ---
#include "Engine/Scene/IScene.h"
#include "Engine/Math/Matrix4x4.h"
#include "Engine/graphics/TextureManager.h"
#include "engine/3d/animation/Animation.h"
#include "engine/3d/animation/CustomAnimation.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/utils/EditorManager.h"  // EngineMode のために必要
#include "game/demo/DemoShowcase.h"

#include "engine/rail/SplineRail.h"
#include "game/rail/RailField.h"
#include "game/rail/RoadMesh.h"
#include "game/rail/DissolveRoad.h"
#include "game/camera/PlayCameraController.h"
#include "game/combat/HitFeel.h"
#include "game/combat/CombatSystem.h"
#include "game/player/Player.h"
#include "game/player/SwallowAbility.h"
#include "game/player/AimThrowController.h"
#include "game/stage/StageFlow.h"
#include "game/stage/CoinSystem.h"
#include "game/stage/BlockSystem.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/EnemyEditor.h"
#include "game/enemy/EnemyManager.h"
#include "game/egg/EggSystem.h"

#include "Skybox.h"
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
class SDFVolumeObject;
class SDFText;
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
	void SyncFromEditors();            // エディタ編集（レール／敵／カメラ要求）をシーンへ反映
	void UpdateCameraAndPostEffect();  // カメラ更新＋シェイク＋踏みつけポストエフェクト
	void HandleModeTransition(EngineMode current); // Edit↔Play 切替時のリセット処理
	void UpdatePlayMode();             // プレイ中のゲーム進行（レール／プレイヤー／敵／踏みつけ／エフェクト）

	// --- 飲み込み/産卵アクション（E / 左Ctrl。処理は SwallowAbility へ分離）---
	SwallowAbility swallow_;

	// --- 卵の投擲（Q構え→矢印で狙う→離して投げる。処理は AimThrowController へ分離）---
	AimThrowController aimThrow_;

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

	// 卵保持数HUD（画面左上）：保持卵のスロット6個＋お腹にためた数の小さい丸（T-07）
	std::vector<std::unique_ptr<Sprite>> eggHudSlots_;
	std::vector<std::unique_ptr<Sprite>> bellyHudIcons_;

	// テクスチャデータ
	std::unordered_map<std::string, TextureData> textures_;

	// デプスステンシル
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

	std::string bgmFile_ = "resources/BGMDon.mp3";

	std::unique_ptr<Skybox> skybox_ = nullptr;

	// --- エンジン機能の展示（回転キューブ・オーラ・ブロック群・SDF卵デモ・Space/Pデモ）---
	//   ゲーム本編と無関係の見本一式。DemoShowcase に分離した（不要になったら丸ごと外せる）
	DemoShowcase demo_;

	std::unique_ptr<SkinnedObj3d> skinnedObj_ = nullptr;

	// アニメーション再生用（編集なし・読み込みのみ）
	CustomAnimationTrack skinnedAnimTrack_;
	float skinnedAnimTime_ = 0.0f;

	// --- プレイヤーの見た目（リグ付きマスコット resources/player/player.gltf）---
	//   Play中はプレイヤーに追従。Idle/Walk/TongueOut のクリップを状況で切り替える
	//   （歩けば Walk、止まれば Idle、ベロ動作中は TongueOut）。Edit中はスタート地点プレビュー
	std::unique_ptr<SkinnedObj3d> playerObj_ = nullptr;
	Vector3 playerPrevPos_ {};          // 前フレームの見た目位置（移動速度の算出用）
	bool    playerPrevPosValid_ = false;
	float   playerModelYOffset_ = 0.0f; // モデル原点（足元）を道の上面に乗せる補正（道の上面＝レール線に統一したので既定0）
	bool    playerTongueSeeked_ = false;  // 捕獲時の収納パートへのシークを1回だけ行うためのフラグ
	float   playerThrowTimer_ = 0.0f;     // 投げリリース後の復帰モーション再生の残り時間
	std::unique_ptr<Obj3d> heldEggObj_ = nullptr; // 構え中に Item ジョイントへ持たせる卵の見た目
	bool    heldEggVisible_ = false;

	EngineMode prevMode_ = EngineMode::Edit;

	std::unique_ptr<Player> player_ = nullptr;

	// --- レール実行時管理（レール本体・緑線マーカー・動くレールを RailField に集約）---
	RailField railField_;

	// --- レール下の道メッシュ（クラフト風の地面。緑線と違い Play 中も見せる本番の見た目）---
	RoadMesh roadMesh_;

	// --- SDF溶け道（道の種類=SDF溶け道のレール。近づくと現れ、離れると溶けて消える）---
	DissolveRoad dissolveRoad_;

	// --- ステージ進行（ゴール判定・表示。処理は StageFlow へ分離）---
	StageFlow stageFlow_;

	// --- 収集物（コイン）。エディタで配置され、プレイ中に触れると取得 ---
	CoinSystem coinSystem_;

	// --- ブロック（乗れる/ぶつかる1m角。マリオメーカー風配置）---
	BlockSystem blockSystem_;
	int lastBlockVersion_ = -1; // ブロック編集のライブ同期用（レール全体は作り直さない）

	// --- プレイ中カメラ（プレイヤー追従＋カメラ演出ゾーン。処理は PlayCameraController へ分離）---
	PlayCameraController camCtrl_;
	// エディタの最新レールから railField_ を作り直し、敵も配置し直す（緑線・プレイヤー一本化）
	//   simple=true はドラッグ中の軽量同期（道は簡易リボン・敵の再配置なし）
	void SyncRailsFromEditor(bool simple = false);

	// --- 敵（生成・更新・描画は EnemyManager へ分離）---
	EnemyManager                 enemyMgr_;
	std::unique_ptr<EnemyEditor> enemyEditor_; // エネミーの配置テンプレートを管理するエディタ
	void SpawnEnemies();                       // 配置テンプレートを元に敵の実体を再構築する

	// --- ヨッシーの卵（敵を飲み込む→保持→投げる。今は状態管理のみ）---
	EggSystem eggSystem_;
	int  lastMapLoadVersion_ = -1;                    // マップ読込を検知して敵配置を復元するため
	float railSyncTimer_ = 0.0f;                      // ドラッグ中の道再生成を10Hzに間引くタイマー（§1）
	bool  railFullSyncPending_ = false;               // ドラッグ終了後に本同期を1回行うフラグ（§1）

	// --- ヒット時の手応え（ヒットストップ/シェイク/ポストエフェクト。処理は HitFeel へ分離）---
	HitFeel hitFeel_;

	// --- 戦闘（踏みつけ/卵命中の判定＋StompEffect の管理。処理は CombatSystem へ分離）---
	CombatSystem combat_;

	// デバッグ描画のグリッド表示ON/OFF
	bool showDebugGrid_ = true;

	// Game View で直接ドラッグ中の敵（-1=なし。エディット中の敵つまみ移動）
	int enemyDragIdx_ = -1;

	// ブロック配置モードのペイント状態（押した瞬間に置く/消すを決めて塗り続ける）
	bool  blockPaintErasing_ = false;
	int   lastPaintRail_  = -1;
	float lastPaintDist_  = 0.0f;
	int   lastPaintLevel_ = -1;
	float lastPaintSide_  = 0.0f;

	// 範囲フィル（塗り方=矩形）のドラッグ状態（始点〜終点をボタンを離した時に一括適用）
	bool  rectFillActive_   = false;
	bool  rectFillErase_    = false;
	int   rectFillRail_     = -1;
	float rectFillDist_     = 0.0f;
	int   rectFillLevel_    = 0;
	float rectFillSide_     = 0.0f;
	float rectFillEndDist_  = 0.0f;
	int   rectFillEndLevel_ = 0;

	// --- 卵保持数の数字表示（SDFText。HUDアイコン列の右に「×N」）---
	std::unique_ptr<SDFText> eggCountText_;

	// --- コイン取得数の表示（SDFText。「コイン n/全体」）---
	std::unique_ptr<SDFText> coinCountText_;

};