#pragma once
// --- 標準・外部ライブラリ ---
#include <wrl/client.h>
#include <d3d12.h>
#include <memory>
// --- エンジン側のファイル ---
#include "engine/math/Matrix4x4.h"
#include "engine/math/struct.h"

// 前方宣言
class DirectXCommon;
struct ID3D12Resource;

// カメラクラス
class Camera{
public:
	// GPUに送るデータ構造体
	struct CameraForGPU{
		Matrix4x4 view;       // 追加：ビュー行列
		Matrix4x4 projection; // 追加：射影行列
		Vector3 worldPosition;
	};


	// コンストラクタ
	Camera(int windowWidth, int windowHeight, DirectXCommon* dxcmmon);

	// 簡易生成：ウィンドウサイズと DirectXCommon を自動取得して生成する
	// 例: camera_ = Camera::Create();
	static std::unique_ptr<Camera> Create();

	// カメラ更新
	void Update();
	// デバッグ用UIの描画
	void DrawDebugUI();

	// setter/getter
	void SetRotation(const Vector3& rotation){ transform.rotate = rotation; } // 回転をセット
	void SetTranslation(const Vector3& translation){ transform.translate = translation; } // 位置をセット
	void SetFovY(float fovY){ this->fovY = fovY; } // 視野角をセット
	void SetAspectRatio(float aspectRatio){ this->aspectRatio = aspectRatio; } // アスペクト比をセット
	void SetNearClip(float nearClip){ this->nearClip = nearClip; } // ニアクリップ距離をセット
	void SetFarClip(float farClip){ this->farClip = farClip; } // ファークリップ距離をセット

	const Matrix4x4& GetWorldMatrix() const{ return worldMatrix; } // ワールド行列のgetter
	const Matrix4x4& GetViewMatrix() const{ return viewMatrix; } // ビュー行列のgetter
	const Matrix4x4& GetProjectionMatrix() const{ return projectionMatrix; } // 射影行列のgetter
	const Matrix4x4& GetViewProjectionMatrix() const{ return viewProjectionMatrix; } // ビュー射影行列のgetter
	const Transform& GetTransform() const{ return transform; } // Transformのgetter
	const Vector3& GetRotation() const{ return transform.rotate; } // 回転のgetter
	const Vector3& GetWorldPosition() const{ return transform.translate; }
	ID3D12Resource* GetCameraResource() const{return cameraResource_.Get();}// カメラ用リソースのgetter

	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const{ return cameraResource_->GetGPUVirtualAddress(); }


private:
	
	DirectXCommon* dxCommon_ = nullptr; // DirectX共通クラスのポインタ
	
	// 変換行列
	Transform transform; // カメラの位置・回転・スケール
	Matrix4x4 viewMatrix; // ビュー行列
	Matrix4x4 worldMatrix; // ワールド行列

	Matrix4x4 projectionMatrix; // 射影行列
	Matrix4x4 viewProjectionMatrix; // ビュー射影行列
	float fovY = 0.78f; // 視野角(垂直方向)
	float aspectRatio = 1.0f; // アスペクト比
	float nearClip = 0.1f; // ニアクリップ距離
	float farClip = 1000.0f; // ファークリップ距離

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_; // カメラ用リソース
	CameraForGPU* cameraData_ = nullptr; // カメラデータのポインタ
	

};

