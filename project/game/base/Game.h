#pragma once
#include "engine/Base/Framework.h"
#include <memory>

class SceneFactory;

// ゲームクラス
class Game : public Framework{
public:
	// オーバーライド
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void DrawScene() override; // Framework::Draw() から呼ばれるシーン固有の描画


public: // コンストラクタ・デストラクタ

	Game();

	~Game() = default;

private:
	// シーンファクトリー
	std::unique_ptr<SceneFactory> sceneFactory_ = nullptr;


};