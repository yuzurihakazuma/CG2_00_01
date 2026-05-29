#include "Obj3dCommon.h"

// --- 標準ライブラリ ---
#include <cassert>
#include <string> // std::to_string用に追加
#include "externals/imgui/imgui.h"
#include <numbers>

// --- エンジン側のファイル ---
#include "engine/graphics/PipelineManager.h"
#include "engine/math/Matrix4x4.h"
#include "engine/base/DirectXCommon.h"
#include "engine/math/VectorMath.h"
#include "engine/graphics/ResourceFactory.h"

using namespace VectorMath;
using namespace MatrixMath;

void Obj3dCommon::DrawDebugUI(){
#ifdef USE_IMGUI
	// 他のツールと同じウィンドウにまとめる
	if ( ImGui::Begin("インスペクター (詳細設定)") ) {
		if ( ImGui::CollapsingHeader("ライティング設定 (Lighting)", ImGuiTreeNodeFlags_DefaultOpen) ) {

			// --- 平行光源 (Directional Light) ---
			if ( ImGui::TreeNode("平行光源 (Directional Light)") ) {

				// 有効なライトの数をスライダーで変更できるようにする
				ImGui::SliderInt("有効なライト数", &directionalLightData_->activeCount, 0, MAX_DIRECTIONAL_LIGHTS);

				// 有効な数だけ UI を表示する
				for ( int i = 0; i < directionalLightData_->activeCount; ++i ) {
					std::string label = "Light " + std::to_string(i); // "Light 0", "Light 1"...
					if ( ImGui::TreeNode(label.c_str()) ) {
						ImGui::DragFloat3("向き (Direction)", &directionalLightData_->lights[i].direction.x, 0.01f);
						ImGui::ColorEdit3("色 (Color)", &directionalLightData_->lights[i].color.x);
						ImGui::DragFloat("強度 (Intensity)", &directionalLightData_->lights[i].intensity, 0.01f, 0.0f, 10.0f);

						directionalLightData_->lights[i].direction = Normalize(directionalLightData_->lights[i].direction);

						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}

			// --- 点光源 (Point Light) ---
			if ( ImGui::TreeNode("点光源 (Point Light)") ) {
				ImGui::DragFloat3("座標 (Position)", &pointLightData_->position.x, 0.01f);
				ImGui::ColorEdit3("色 (Color)", &pointLightData_->color.x);
				ImGui::DragFloat("強度 (Intensity)", &pointLightData_->intensity, 0.01f, 0.0f, 10.0f);
				ImGui::DragFloat("影響半径 (Radius)", &pointLightData_->radius, 0.1f, 0.1f, 100.0f);
				ImGui::DragFloat("減衰率 (Decay)", &pointLightData_->decay, 0.1f, 0.1f, 10.0f);
				ImGui::TreePop();
			}

			// --- スポットライト (Spot Light) ---
			if ( ImGui::TreeNode("スポットライト (Spot Light)") ) {
				ImGui::DragFloat3("座標 (Position)", &spotLightData_->position.x, 0.01f);
				ImGui::DragFloat3("向き (Direction)", &spotLightData_->direction.x, 0.01f);
				spotLightData_->direction = Normalize(spotLightData_->direction); // 常に正規化
				ImGui::ColorEdit3("色 (Color)", &spotLightData_->color.x);
				ImGui::DragFloat("強度 (Intensity)", &spotLightData_->intensity, 0.01f, 0.0f, 10.0f);
				ImGui::DragFloat("影響距離 (Distance)", &spotLightData_->distance, 0.1f, 0.1f, 100.0f);
				ImGui::DragFloat("減衰率 (Decay)", &spotLightData_->decay, 0.1f, 0.1f, 10.0f);
				ImGui::DragFloat("照射角 (cosAngle)", &spotLightData_->cosAngle, 0.01f, -1.0f, 1.0f);
				ImGui::DragFloat("減衰開始角 (FalloffStart)", &spotLightData_->cosFalloffStart, 0.01f, -1.0f, 1.0f);
				ImGui::TreePop();
			}
		}
	}
	ImGui::End();
#endif
}


// 初期化
void Obj3dCommon::Initialize(DirectXCommon* dxCommon){
	// NULLチェック
	assert(dxCommon);
	// メンバ変数にセット
	this->dxCommon_ = dxCommon;
	// FactoryのNULLチェック
	assert(this->dxCommon_->GetResourceFactory() != nullptr && "SpriteCommon: Received dxCommon has NO Factory!");

	directionalLightResource_ =
		dxCommon_->GetResourceFactory()->CreateBufferResource(sizeof(DirectionalLightData));

	directionalLightResource_->Map(
		0, nullptr, reinterpret_cast< void** >( &directionalLightData_ )
	);

	directionalLightData_->activeCount = 1; // 初期状態では1つだけ光らせる

	for ( uint32_t i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i ) {
		directionalLightData_->lights[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
		directionalLightData_->lights[i].direction = Normalize({ -1.0f, 0.3f, 0.0f });
		// 最初のライトだけ強度を1、それ以外は0にしておく
		directionalLightData_->lights[i].intensity = ( i == 0 ) ? 1.0f : 0.0f;
	}

	// 点光源のバッファ作成
	pointLightResource_ = dxCommon_->GetResourceFactory()->CreateBufferResource(sizeof(PointLight));
	pointLightResource_->Map(0, nullptr, reinterpret_cast< void** >(&pointLightData_));

	// 初期値（スライド資料の通り、位置を(0,2,0)にしておく）
	pointLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	pointLightData_->position = { 0.0f, 2.0f, 0.0f };
	pointLightData_->intensity = 1.0f;
	pointLightData_->radius = 10.0f;
	pointLightData_->decay = 1.0f;

	// スポットライトのバッファ作成
	spotLightResource_ = dxCommon_->GetResourceFactory()->CreateBufferResource(sizeof(SpotLight));
	spotLightResource_->Map(0, nullptr, reinterpret_cast< void** >( &spotLightData_ ));

	spotLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	spotLightData_->position = { 0.0f, 1.25f, 0.0f };
	spotLightData_->distance = 7.0f;
	spotLightData_->direction = Normalize({ -1.0f, -1.0f, 0.0f });
	spotLightData_->intensity = 4.0f;
	spotLightData_->decay = 2.0f;
	spotLightData_->cosAngle = std::cos(std::numbers::pi_v<float> / 3.0f); // 約0.5 (60度)
	spotLightData_->cosFalloffStart = 1.0f; // 1.0なら最初から減衰が始まる
}

// 共通の描画設定
void Obj3dCommon::PreDraw(ID3D12GraphicsCommandList* commandList){
	// パイプラインセット
	PipelineManager::GetInstance()->SetPipeline(
		commandList,
		PipelineType::Object3D
	);
}


void Obj3dCommon::Finalize(){
	directionalLightResource_.Reset();

	pointLightResource_.Reset();
	spotLightResource_.Reset();
}
