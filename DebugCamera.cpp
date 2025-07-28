#include "DebugCamera.h"
#include <algorithm>






void DebugCamera::Initialize(){


	rotation_ = { 0.0f, 0.0f, 0.0f }; // 初期回転
	translation_ = { 0.0f, 0.0f, -10.0f }; // 初期位置
	UpdateViewMatrix();

}


void DebugCamera::Update(){
	InputUpdate();

	if ( !isDebugCamera_ ) return;

	static POINT prevMousePos = {};
	POINT mousePos;
	GetCursorPos(&mousePos);

	static bool first = true;
	if ( first ) {
		prevMousePos = mousePos;
		first = false;
	}

	float dx = float(mousePos.x - prevMousePos.x);
	float dy = float(mousePos.y - prevMousePos.y);

	MouseMove(dx, dy);

	prevMousePos = mousePos;

	UpdateViewMatrix();
}

// ビュー行列の更新
void DebugCamera::UpdateViewMatrix(){
	// ----------------------------------------
	// 回転角から回転行列を計算する（Z→X→Yの順）
	// ----------------------------------------
	Matrix4x4 rotateX = MatrixMath::MakeRotateX(rotation_.x);
	Matrix4x4 rotateY = MatrixMath::MakeRotateY(rotation_.y);
	Matrix4x4 rotateZ = MatrixMath::MakeRotateZ(rotation_.z);

	// ----------------------------------------
	// 累積回転行列を生成する（順序に注意）
	// ----------------------------------------
	Matrix4x4 rotationMatrix = MatrixMath::Multiply(rotateZ, MatrixMath::Multiply(rotateX, rotateY));

	// ----------------------------------------
	// 平行移動行列を計算する
	// ----------------------------------------
	Matrix4x4 translationMatrix = MatrixMath::MakeTranslate(translation_);

	// ----------------------------------------
	// ワールド行列を生成する（順序：回転 → 平行移動）
	// ----------------------------------------
	Matrix4x4 worldMatrix = MatrixMath::Multiply(rotationMatrix, translationMatrix);

	// ----------------------------------------
	// ビュー行列はワールド行列の逆行列
	// ----------------------------------------
	viewMatrix_ = MatrixMath::Inverse(worldMatrix);
}

void DebugCamera::InputUpdate(){
	// トグルキー（Enter）
	bool toggleKey = ( GetAsyncKeyState(VK_RETURN) & 0x8000 );

	if ( toggleKey && !prevToggleKey_ ) {
		isDebugCamera_ = !isDebugCamera_; // フラグ切り替え
	}

	prevToggleKey_ = toggleKey; // 前フレーム用

	
}


void DebugCamera::MouseMove(float dx, float dy){
	const float sensitivity = 0.005f;

	// --- 右クリック：回転 ---
	if ( GetAsyncKeyState(VK_RBUTTON) & 0x8000 ) {
		rotation_.y -= dx * sensitivity;
		rotation_.x -= dy * sensitivity;

		const float limit = 1.5f;
		rotation_.x = std::clamp(rotation_.x, -limit, limit);
	}

	// --- 左クリック：平行移動 ---
	if ( GetAsyncKeyState(VK_LBUTTON) & 0x8000 ) {
		const float moveSpeed = 0.1f;

		Vector3 right = {
			std::cos(rotation_.y),
			0.0f,
			std::sin(rotation_.y)
		};

		Matrix4x4 rotateX = MatrixMath::MakeRotateX(rotation_.x);
		Matrix4x4 rotateY = MatrixMath::MakeRotateY(rotation_.y);
		Matrix4x4 rotationMatrix = MatrixMath::Multiply(rotateX, rotateY);

		Vector3 up = MatrixMath::Transforms({ 0.0f, 1.0f, 0.0f }, rotationMatrix);

		translation_ = Add(translation_, Multiply(+dx * moveSpeed, right));
		translation_ = Add(translation_, Multiply(-dy * moveSpeed, up));
	}
}


void DebugCamera::Draw(){}
