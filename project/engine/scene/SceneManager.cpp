#include "SceneManager.h"
// --- 標準ライブラリ ---
#include <cassert>
// --- エンジン側のファイル ---
#include "engine/scene/IScene.h"
#include "engine/scene/AbstractSceneFactory.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/Input.h"



// シングルトンクラスの実装
SceneManager* SceneManager::GetInstance(){
	static SceneManager instance;
	return &instance;
}
// デストラクタ
SceneManager::~SceneManager(){
	// 現在のシーンを終了
	if ( currentScene_ ) {
		// 現在のシーンを終了
		currentScene_->Finalize();
		// シーン情報をクリア
		currentScene_ = nullptr;
	}
}
// シーンマネージャーの更新
void SceneManager::Update(){

#ifdef _DEBUG
	if ( Input::GetInstance()->Triggerkey(DIK_R) ) {
		if ( currentScene_ ) {
			currentScene_->Reload(); // 現在のシーンが何であれリロードを叩く！
		}
	}
#endif

	// 次のシーンへの切り替え処理
	if ( nextScene_ ) {
		// 現在のシーンを終了
		if ( currentScene_ ) {
			currentScene_->Finalize();
		}
		// シーンを切り替え
		currentScene_ = std::move(nextScene_);
		// 次のシーン情報をクリア
		nextScene_ = nullptr;
		DirectXCommon* dxCommon = DirectXCommon::GetInstance();

		dxCommon->BeginCommandRecording();

		currentScene_->Initialize();
		dxCommon->EndCommandRecording();
	}
	if (currentScene_) {
		auto updateStartTime = std::chrono::steady_clock::now();
		
		currentScene_->Update();

		auto updateEndTime = std::chrono::steady_clock::now();
		cpuUpdateTimeMs_ = std::chrono::duration<float, std::milli>(updateEndTime - updateStartTime).count();
		

	}


}
// シーンマネージャーの描画
void SceneManager::Draw(){
	if ( currentScene_ ) {

		auto drawStartTime = std::chrono::steady_clock::now();
		// 現在のシーンを描画
		currentScene_->Draw();

		auto drawEndTime = std::chrono::steady_clock::now();
		cpuDrawTimeMs_ = std::chrono::duration<float, std::milli>(drawEndTime - drawStartTime).count();
	}
}

// 文字列を使ってファクトリーにシーンを作ってもらう
void SceneManager::ChangeScene(const std::string& sceneName) {
	assert(sceneFactory_ && "SceneManager: SceneFactory is not set!");

	// ファクトリーが unique_ptr を作ってくれるので、そのまま受け取る
	nextScene_ = sceneFactory_->CreateScene(sceneName);
}

// シーン変更（シーン名で指定）
void SceneManager::ChangeScene(std::unique_ptr<IScene> nextScene){
	nextScene_ = std::move(nextScene);
}


void SceneManager::Finalize() {
	// 現在のシーンを破棄する
	if (currentScene_) {	
		currentScene_->Finalize();
		currentScene_.reset();    
	}
}

void SceneManager::DrawCurrentSceneDebugUI(){
	if ( currentScene_ ) {
		currentScene_->DrawDebugUI();
	}
}