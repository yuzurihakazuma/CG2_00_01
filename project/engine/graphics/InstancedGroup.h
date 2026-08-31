#pragma once
#include <vector>
#include <memory>
#include <string>
#include <wrl.h>
#include <d3d12.h>
#include "Engine/3D/Obj/Obj3d.h"
#include "Engine/3D/model/Model.h"

// 前方宣言
class Model;
class Camera;

// 大量のオブジェクトを一括描画するための専用クラス
class InstancedGroup {
public:
	// 初期化（使うモデルの名前と、最大表示数を決める）
	void Initialize(const std::string& modelName, uint32_t maxInstanceCount = 10000);

	// 更新（ブロックたちの最新の座標データを集める）
	void Update(const std::vector<std::unique_ptr<Obj3d>>& objects);
	// 更新（非所有ポインタ版。プールした Obj3d をグループへ振り分けて使う側向け）
	void Update(const std::vector<Obj3d*>& objects);

	// 一括描画！
	void Draw(const Camera* camera);

	// ディゾルブエフェクトのパラメータをセットする関数
	void SetNoiseTexture(uint32_t index) { noiseTextureIndex_ = index; }

private:
	Model* model_ = nullptr;
	uint32_t maxInstanceCount_ = 0;
	uint32_t currentInstanceCount_ = 0;

	// GPUに一気に送るための「巨大な配列バッファ」
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
	Obj3d::TransformationMatrix* instancingData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> dissolveResource_;
	Obj3d::DissolveData* dissolveData_ = nullptr;
	uint32_t noiseTextureIndex_ = 0;

};