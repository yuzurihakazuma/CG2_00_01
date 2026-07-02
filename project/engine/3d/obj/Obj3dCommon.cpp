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

#include <fstream>
#include "externals/nlohmann/json.hpp"

using namespace VectorMath;
using namespace MatrixMath;
using json = nlohmann::json;

// ライティングのプリセット適用（0:昼 1:夕方 2:夜）
void Obj3dCommon::ApplyLightPreset(int index){
	if ( !directionalLightData_ ) return;
	auto& L = directionalLightData_->lights[0];
	directionalLightData_->activeCount = 1;
	switch ( index ) {
	case 0: // 昼：白くて強い光が上から
		L.color = { 1.0f, 1.0f, 0.98f, 1.0f };
		L.direction = Normalize(Vector3{ 0.3f, -1.0f, 0.4f });
		L.intensity = 1.0f;
		break;
	case 1: // 夕方：オレンジの低い光
		L.color = { 1.0f, 0.55f, 0.3f, 1.0f };
		L.direction = Normalize(Vector3{ 0.8f, -0.25f, 0.2f });
		L.intensity = 1.1f;
		break;
	case 2: // 夜：青白い弱い光
		L.color = { 0.45f, 0.55f, 0.95f, 1.0f };
		L.direction = Normalize(Vector3{ -0.2f, -1.0f, -0.3f });
		L.intensity = 0.35f;
		break;
	}
}

// ライティング設定の保存（平行光源・点光源・スポットライトをまとめてJSONへ）
void Obj3dCommon::SaveLighting(const std::string& path) const{
	if ( !directionalLightData_ || !pointLightData_ || !spotLightData_ ) return;
	json j;
	j["directional"] = json::array();
	j["activeCount"] = directionalLightData_->activeCount;
	for ( uint32_t i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i ) {
		const auto& L = directionalLightData_->lights[i];
		j["directional"].push_back({
			{ "color", { L.color.x, L.color.y, L.color.z, L.color.w } },
			{ "direction", { L.direction.x, L.direction.y, L.direction.z } },
			{ "intensity", L.intensity },
		});
	}
	const auto& P = *pointLightData_;
	j["point"] = {
		{ "color", { P.color.x, P.color.y, P.color.z, P.color.w } },
		{ "position", { P.position.x, P.position.y, P.position.z } },
		{ "intensity", P.intensity }, { "radius", P.radius }, { "decay", P.decay },
	};
	const auto& S = *spotLightData_;
	j["spot"] = {
		{ "color", { S.color.x, S.color.y, S.color.z, S.color.w } },
		{ "position", { S.position.x, S.position.y, S.position.z } },
		{ "direction", { S.direction.x, S.direction.y, S.direction.z } },
		{ "intensity", S.intensity }, { "distance", S.distance }, { "decay", S.decay },
		{ "cosAngle", S.cosAngle }, { "cosFalloffStart", S.cosFalloffStart },
	};
	std::ofstream f(path);
	if ( f ) { f << j.dump(4); }
}

void Obj3dCommon::LoadLighting(const std::string& path){
	if ( !directionalLightData_ || !pointLightData_ || !spotLightData_ ) return;
	std::ifstream f(path);
	if ( !f ) return;
	json j;
	f >> j;

	directionalLightData_->activeCount = j.value("activeCount", 1);
	if ( j.contains("directional") && j["directional"].is_array() ) {
		for ( uint32_t i = 0; i < MAX_DIRECTIONAL_LIGHTS && i < j["directional"].size(); ++i ) {
			auto& jl = j["directional"][i];
			auto& L = directionalLightData_->lights[i];
			if ( jl.contains("color") )     { L.color = { jl["color"][0], jl["color"][1], jl["color"][2], jl["color"][3] }; }
			if ( jl.contains("direction") ) { L.direction = { jl["direction"][0], jl["direction"][1], jl["direction"][2] }; }
			L.intensity = jl.value("intensity", 1.0f);
		}
	}
	if ( j.contains("point") ) {
		auto& jp = j["point"];
		auto& P = *pointLightData_;
		if ( jp.contains("color") )    { P.color = { jp["color"][0], jp["color"][1], jp["color"][2], jp["color"][3] }; }
		if ( jp.contains("position") ) { P.position = { jp["position"][0], jp["position"][1], jp["position"][2] }; }
		P.intensity = jp.value("intensity", P.intensity);
		P.radius = jp.value("radius", P.radius);
		P.decay = jp.value("decay", P.decay);
	}
	if ( j.contains("spot") ) {
		auto& js = j["spot"];
		auto& S = *spotLightData_;
		if ( js.contains("color") )     { S.color = { js["color"][0], js["color"][1], js["color"][2], js["color"][3] }; }
		if ( js.contains("position") )  { S.position = { js["position"][0], js["position"][1], js["position"][2] }; }
		if ( js.contains("direction") ) { S.direction = { js["direction"][0], js["direction"][1], js["direction"][2] }; }
		S.intensity = js.value("intensity", S.intensity);
		S.distance = js.value("distance", S.distance);
		S.decay = js.value("decay", S.decay);
		S.cosAngle = js.value("cosAngle", S.cosAngle);
		S.cosFalloffStart = js.value("cosFalloffStart", S.cosFalloffStart);
	}
}

void Obj3dCommon::DrawDebugUI(){
#ifdef USE_IMGUI
	// 他のツールと同じウィンドウにまとめる
	if ( ImGui::Begin("インスペクター (詳細設定)") ) {
		if ( ImGui::CollapsingHeader("ライティング設定 (Lighting)", ImGuiTreeNodeFlags_DefaultOpen) ) {

			// --- プリセット＆保存/読込 ---
			ImGui::TextDisabled("プリセット:");
			ImGui::SameLine();
			if ( ImGui::SmallButton("昼") )   { ApplyLightPreset(0); }
			ImGui::SameLine();
			if ( ImGui::SmallButton("夕方") ) { ApplyLightPreset(1); }
			ImGui::SameLine();
			if ( ImGui::SmallButton("夜") )   { ApplyLightPreset(2); }
			ImGui::SameLine();
			if ( ImGui::SmallButton("保存") ) { SaveLighting("resources/data/lighting.json"); }
			ImGui::SameLine();
			if ( ImGui::SmallButton("読込") ) { LoadLighting("resources/data/lighting.json"); }
			ImGui::Separator();

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
		directionalLightData_->lights[i].direction = Normalize({ 0.0f, -1.0f, 0.0f });
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
	spotLightData_->position = { 2.0f, 1.25f, 0.0f };
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
