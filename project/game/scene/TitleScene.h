#pragma once
// --- エンジン側のファイル ---
#include "Engine/Scene/IScene.h"
#include "Engine/Math/Matrix4x4.h"
#include "Engine/graphics/TextureManager.h"

// --- 標準ライブラリ ---
#include <vector>
#include <memory>

// 前方宣言
class DebugCamera;
class Camera;
class Sprite;
class Obj3d;
class DirectXCommon;
class Input;
class RenderTexture;
class PostEffect;
class LevelEditor;




	// ゲームプレイシーン
class TitleScene : public IScene{
public:
	// 初期化
	void Initialize() override;
	// 終了
	void Finalize() override;
	// 更新
	void Update() override;
	// 描画
	void Draw() override;

	void Reload() override;

	// デバッグ用UIの描画
	void DrawDebugUI() override;

	TitleScene();

	~TitleScene();

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


	std::string bgmFile_ = "resources/BGMDon.mp3";

	// 描画先を切り替えるためのRenderTexture
	std::unique_ptr<PostEffect> postEffect_ = nullptr;


	// マップエディタ
	std::unique_ptr<LevelEditor> levelEditor_;

	bool isEditorActive_ = true;

	


};