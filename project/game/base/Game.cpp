#include "Game.h"
// ---  ゲーム固有のファイル ---
#include "SceneFactory.h"
#include "GamePlayScene.h"
#include "TitleScene.h"

// ---  エンジン側のファイル ---
#include "engine/scene/SceneManager.h"
#include "engine/utils/EditorManager.h"

void Game::Initialize(){
	// 基盤システムの初期化 (Window, DirectX, Input, Common類, EditorManager)
	Framework::Initialize();

	// 1. ファクトリーの生成
	sceneFactory_ = std::make_unique<SceneFactory>();

	// 2. マネージャーにファクトリーを教える
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

	// 3. 最初のシーンをリクエストする
	SceneManager::GetInstance()->ChangeScene(std::make_unique<GamePlayScene>());
}

void Game::Update(){
	// 基盤更新（入力・ウィンドウ・リサイズ・ImGui フレーム開始）
	Framework::Update();

	// シーンの更新
	SceneManager::GetInstance()->Update();

	// パフォーマンス計測値をエディタに渡す
	EditorManager::GetInstance()->SetCpuTimes(
		SceneManager::GetInstance()->GetCpuUpdateTimeMs(),
		SceneManager::GetInstance()->GetCpuDrawTimeMs()
	);

	// エディタ UI の更新
	EditorManager::GetInstance()->Update();
}

void Game::DrawScene(){
	// シーン固有の描画（PreDraw / PostDraw / ImGui End は Framework::Draw() が管理する）
	SceneManager::GetInstance()->Draw();
}

Game::Game(){}

void Game::Finalize(){
	// 基盤終了（EditorManager の終了処理も Framework::Finalize() が行う）
	Framework::Finalize();
}
