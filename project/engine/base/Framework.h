#pragma once
#include <Windows.h>
#include <memory>

// フレームワーククラス
class Framework{
public:
	// 仮想デストラクタ (継承する場合の必須マナー)
	virtual ~Framework() = default;

	// 初期化（エンジン基盤・EditorManager の初期化を行う）
	virtual void Initialize();

	// 終了（EditorManager の終了処理を含む）
	virtual void Finalize();

	// 毎フレーム更新（入力・ImGui フレーム開始・ウィンドウ処理を行う）
	virtual void Update();

	// 描画（PreDraw / DrawScene / EditorEnd / PostDraw をまとめて管理する）
	void Draw();

	// シーン固有の描画（継承先で必ず実装する）
	virtual void DrawScene() = 0;

	// 終了チェック
	virtual bool IsEndRequest();

	// 実行
	void Run();

protected:
	bool endRequest_ = false;
};