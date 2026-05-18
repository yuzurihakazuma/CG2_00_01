#pragma once

// シーンのインターフェース
class IScene{
public:
	// 仮想デストラクタ
	virtual ~IScene() = default;

	// シーンの基本操作
	
	// 初期化
	virtual void Initialize() = 0;
	// 終了
	virtual void Finalize() = 0;
	// 更新
	virtual void Update() = 0;
	// 描画
	virtual void Draw() = 0;

	virtual void Reload(){}
	// デバッグ用UIの描画
	virtual void DrawDebugUI() {}

};