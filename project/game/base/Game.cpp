#include "Game.h"
// ---  ゲーム固有のファイル ---
#include "scene/SceneFactory.h"      
#include "scene/GamePlayScene.h"     
#include "scene/TitleScene.h"

// ---  エンジン側のファイル ---
#include "engine/scene/SceneManager.h"   
#include "engine/base/DirectXCommon.h"   
#include "engine/graphics/SrvManager.h"
#include "engine/utils/EditorManager.h"
#include "engine/audio/AudioManager.h"

void Game::Initialize(){
	// 基盤システムの初期化 (Window, DirectX, Input, Common類)
	Framework::Initialize();

	

	// 1. ファクトリーの生成
	sceneFactory_ = std::make_unique<SceneFactory>();

	

	// 2. マネージャーにファクトリーを教える
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

	// 3. 最初のシーンを文字列でリクエストする
	SceneManager::GetInstance()->SetFirstScene(std::make_unique<TitleScene>());


}

void Game::Update(){
	// 基盤更新
	Framework::Update();
	AudioManager::GetInstance()->UpdateVoices();

	// エディタ更新
	EditorManager::GetInstance()->Begin();


	// シーンの更新
	SceneManager::GetInstance()->Update();

	// パフォーマンス計測値をエディタに渡す
	EditorManager::GetInstance()->SetCpuTimes(
		SceneManager::GetInstance()->GetCpuUpdateTimeMs(),
		SceneManager::GetInstance()->GetCpuDrawTimeMs()
	);

	// エディタUIの更新・描画
	EditorManager::GetInstance()->Update();

}

void Game::Draw(){
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	// 描画前処理
	dxCommon->PreDraw();
	// SRVマネージャーの描画前処理
	SrvManager::GetInstance()->PreDraw();

	
	// シーンの描画
	SceneManager::GetInstance()->Draw();

	


	// エディタの描画前処理
	EditorManager::GetInstance()->End();


	// 描画後処理
	dxCommon->PostDraw();
}

Game::Game(){}


void Game::Finalize(){

	
	// 基盤終了
	Framework::Finalize();
}
