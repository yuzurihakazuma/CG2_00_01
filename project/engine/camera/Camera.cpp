#include "Camera.h"
// --- 標準ライブラリ ---
#include <d3d12.h>

// --- エンジン側のファイル ---
#include "engine/base/WindowProc.h"
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/ResourceFactory.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif
using namespace MatrixMath;

// 簡易生成：ウィンドウサイズと DirectXCommon を自動取得して生成
std::unique_ptr<Camera> Camera::Create(){
	WindowProc* window = WindowProc::GetInstance();
	return std::make_unique<Camera>(
		window->GetClientWidth(),
		window->GetClientHeight(),
		DirectXCommon::GetInstance());
}


void Camera::DrawDebugUI(){
#ifdef USE_IMGUI
	// "Inspector" などのウィンドウ名にすると、Unity風のインスペクターにまとめられます
	if ( ImGui::Begin("インスペクター (詳細設定)") ) {
		if ( ImGui::CollapsingHeader("カメラ設定 (Camera Settings)", ImGuiTreeNodeFlags_DefaultOpen) ) {
			// transform は Camera クラスのメンバ変数なので、直接編集できます！
			ImGui::DragFloat3("座標 (Position)", &transform.translate.x, 0.1f);
			ImGui::DragFloat3("回転 (Rotation)", &transform.rotate.x, 0.01f);

			// おまけ：視野角やクリップ距離も調整できるようにしておくと便利です
			ImGui::DragFloat("視野角 (FOV)", &fovY, 0.01f);
			ImGui::DragFloat("ニアクリップ (Near Clip)", &nearClip, 0.01f);
			ImGui::DragFloat("ファークリップ (Far Clip)", &farClip, 1.0f);
		}
	}
	ImGui::End();
#endif
}


Camera::Camera(int windowWidth, int windowHeight, DirectXCommon* dxcmmon)
	: dxCommon_(dxcmmon)
	,transform({ 1.0f,1.0f,1.0f, }, { 0.0f,0.0f,0.0f }, { 0.0f,0.0f,0.0f }) // 初期化
	, fovY(0.78f) // 約45度
	, nearClip(0.1f) // ニアクリップ距離
	, farClip(100.0f) // ファークリップ距離
	, worldMatrix(MakeAffine(transform.scale, transform.rotate, transform.translate)) // ワールド行列の初期化
	, viewMatrix(Inverse(worldMatrix)) // ビュー行列の初期化
	, projectionMatrix(PerspectiveFov(fovY, aspectRatio, nearClip, farClip)) // プロジェクション行列の初期化
	, viewProjectionMatrix(Multiply(viewMatrix, projectionMatrix)) // ビュー射影行列の初期化
{

	// アスペクト比を計算
	aspectRatio = float(windowWidth) / float(windowHeight);

	cameraResource_ =
		dxCommon_->GetResourceFactory()->CreateBufferResource(sizeof(CameraForGPU));

	cameraResource_->Map(
		0, nullptr, reinterpret_cast< void** >( &cameraData_ )
	);


	// 初期化時点でも一度計算しておく
	Update();


}

void Camera::Update(){

	// アスペクト比を計算
	aspectRatio = static_cast< float >( WindowProc::GetInstance()->GetClientWidth() ) /
		static_cast< float >( WindowProc::GetInstance()->GetClientHeight() );


	// 1. ワールド行列の更新 (カメラの場所・角度)
	worldMatrix = MakeAffine(transform.scale, transform.rotate, transform.translate);

	// 2. ビュー行列の更新 (ワールドの逆行列)
	viewMatrix = Inverse(worldMatrix);

	// 3. プロジェクション行列の更新 (画角など)
	projectionMatrix = PerspectiveFov(fovY, aspectRatio, nearClip, farClip);

	// 4. 合成行列の更新
	viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

	cameraData_->view = viewMatrix;             // ビュー行列を送る
	cameraData_->projection = projectionMatrix; // 射影行列を送る

	// 5. GPU転送用データの更新
	cameraData_->worldPosition = transform.translate;


}
