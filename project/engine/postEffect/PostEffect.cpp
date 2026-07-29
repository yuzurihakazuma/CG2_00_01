#include "PostEffect.h"
// --- 標準ライブラリ ---
#include "externals/nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <utility> 

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// --- エンジン側のファイル ---
#include "engine/graphics/PipelineManager.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/base/DirectXCommon.h"
#include "Bloom.h"

using json = nlohmann::json;

void PostEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height) {
	
	dxCommon_ = dxCommon;

	// 配列にした2枚の画用紙を初期化
	for (int i = 0; i < 2; ++i) {
		renderTextures_[i] = std::make_unique<RenderTexture>();
		renderTextures_[i]->Initialize(dxCommon, srvManager, width, height);
	}
	

	maskTexture_ = std::make_unique<RenderTexture>();
	
	maskTexture_->SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	
	// ※背景は絶対に「真っ黒（フラグ0）」でクリアするため、Vector4{0,0,0,0}等を設定するように内部で調整するか、RenderTexture側で黒クリアになるようにします
	maskTexture_->Initialize(dxCommon, srvManager, width, height);
	
	// ポストエフェクト共通定数バッファ (b0)。先頭 time は従来通り、後続にティント等のパラメータを持つ
	timeResource_ = ResourceFactory::GetInstance()->CreateBufferResource(sizeof(PostEffectParams));
	timeResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramData_));
	*paramData_ = PostEffectParams{};

	// マスク系エフェクト用定数バッファ (b1)。既定はゼロ（無効）
	maskResource_ = ResourceFactory::GetInstance()->CreateBufferResource(sizeof(PostEffectMaskParams));
	maskResource_->Map(0, nullptr, reinterpret_cast<void**>(&maskData_));
	*maskData_ = PostEffectMaskParams{};

	Bloom::GetInstance()->Initialize(dxCommon, SrvManager::GetInstance(), width, height);
	Bloom::GetInstance()->Load("resources/bloom.json");
}


void PostEffect::Update() {
	if (isActive_) {
		time_ += timeSpeed_; // 自分の持っているスピード分だけ進める
		if (paramData_) {
			paramData_->time = time_;
		}
	}
}


uint32_t PostEffect::GetSrvIndex() const {
	return renderTextures_[finalResultIndex_]->GetSrvIndex();
}

void PostEffect::Finalize() {
	// スマートポインタを明示的にリセットして、GPUのメモリリークを防ぐ
	for (int i = 0; i < 2; ++i) {
		if (renderTextures_[i]) {
			renderTextures_[i].reset();
		}
	}
	if (timeResource_) {
		timeResource_.Reset();
	}
	if (maskResource_) {
		maskResource_.Reset();
	}

	Bloom::GetInstance()->Finalize();

	if ( maskTexture_ ) { maskTexture_.reset(); }
}

void PostEffect::PreDrawScene(ID3D12GraphicsCommandList* commandList) {


	renderTextures_[0]->PreDrawScene(commandList, dxCommon_);
}

void PostEffect::PostDrawScene(ID3D12GraphicsCommandList* commandList) {


	renderTextures_[0]->PostDrawScene(commandList, dxCommon_);
}

void PostEffect::Draw(ID3D12GraphicsCommandList* commandList, bool drawToScreen) {

	auto SetRenderTargetToSwapchain = [&]() {
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetBackBufferRtvHandle();
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDsvHandle();
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
		D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(dxCommon_->GetClientWidth()), static_cast<float>(dxCommon_->GetClientHeight()), 0.0f, 1.0f };
		D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(dxCommon_->GetClientWidth()), static_cast<LONG>(dxCommon_->GetClientHeight()) };
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);
		};


	// エフェクト全体がOFFなら、エフェクトなしで出力
	if (!isActive_) {
		finalResultIndex_ = 0; // 結果はそのまま 0番
		// drawToScreen に関わらず常にバックバッファをRTVとしてセットする
		// （スキップするとスプライト等の後続描画でリソース状態エラーが起きる）
		SetRenderTargetToSwapchain();
		if (drawToScreen) {
			PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, renderTextures_[0]->GetSrvIndex());
			commandList->DrawInstanced(3, 1, 0, 0);
		}
		return;
	}

	uint32_t src = 0;  // 最初の読み込み元は、ゲームが描かれた 0番
	uint32_t dest = 1; // 最初の書き込み先は 1番

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// -------------------------------------------------------------
	// ここにかけたいエフェクトを書き出す
	// -------------------------------------------------------------
	for (int i = 1; i < static_cast<int>(PostEffectType::Count); ++i) {
		// ON/OFFも指定できるバージョン
		ApplyEffect(commandList, dxCommon_, static_cast<PostEffectType>(i), activeEffects_[i], src, dest);
	}

	// -------------------------------------------------------------
	// 最終出力（すべてのバケツリレーが終わった最新の絵「src」を画面に出す）
	// -------------------------------------------------------------
	SetRenderTargetToSwapchain();
	// もしエフェクトが1つもかかっていないなら、最初から画面に出す（ピンポン描画をしない）
	finalResultIndex_ = src;

	/*PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, renderTextures_[src]->GetSrvIndex());
	commandList->DrawInstanced(3, 1, 0, 0);*/
}

// ウィンドウサイズが変わったときの処理（画用紙のサイズも変える）

void PostEffect::OnResize(uint32_t width, uint32_t height){

	SrvManager* srvMgr = SrvManager::GetInstance();

	// 2枚の画用紙のサイズを変える
	for ( int i = 0; i < 2; ++i ){
		renderTextures_[i]->Resize(dxCommon_, srvMgr, width, height);
	}
	maskTexture_->Resize(dxCommon_, srvMgr, width, height);

	Bloom::GetInstance()->OnResize(dxCommon_, srvMgr, width, height);
}


// =========================================================
// 3Dモデルを描く前に、キャンバスを2枚(色用とマスク用)セットする関数
// =========================================================
void PostEffect::PreDrawSceneMRT(ID3D12GraphicsCommandList* commandList){
	// 1. 2枚とも「書き込みモード（RTV）」にするためのバリアを張る（RenderTextureのPreDrawSceneの中身と同じ処理）
	// ※ここでは簡略化のため、手動で2枚分のバリアを張ります
	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = renderTextures_[0]->GetResource().Get();
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = maskTexture_->GetResource().Get();
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(2, barriers);

	// 2. RTVのハンドルを2つ配列にする！ここがMRTの要！
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = {
		renderTextures_[0]->GetRtvHandle(), // 1枚目：色
		maskTexture_->GetRtvHandle()        // 2枚目：マスク（フラグ）
	};
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDsvHandle();

	// 3. GPUに「2枚に同時に描く
	commandList->OMSetRenderTargets(2, rtvHandles, FALSE, &dsvHandle);

	auto c0 = renderTextures_[0]->GetClearColor();
	const float clearColor[4] = { c0.x, c0.y, c0.z, c0.w }; // 1枚目（色用）

	auto c1 = maskTexture_->GetClearColor();
	const float clearMask[4] = { c1.x, c1.y, c1.z, c1.w };  // 2枚目（マスク用）

	commandList->ClearRenderTargetView(rtvHandles[0], clearColor, 0, nullptr);
	commandList->ClearRenderTargetView(rtvHandles[1], clearMask, 0, nullptr);
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	// ビューポートとシザー
	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast< float >( dxCommon_->GetClientWidth() ), static_cast< float >( dxCommon_->GetClientHeight() ), 0.0f, 1.0f };
	D3D12_RECT scissorRect = { 0, 0, static_cast< LONG >( dxCommon_->GetClientWidth() ), static_cast< LONG >( dxCommon_->GetClientHeight() ) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

// 描画が終わったら「読み込みモード」に戻す関数
void PostEffect::PostDrawSceneMRT(ID3D12GraphicsCommandList* commandList){
	D3D12_RESOURCE_BARRIER barriers[2] = {}; // 2枚分のバリアを張る
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; // 1枚目：色用
	barriers[0].Transition.pResource = renderTextures_[0]->GetResource().Get(); // 2枚目：マスク用
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; // 1枚目：色用
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // 2枚目：マスク用
	
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; // 2枚目：マスク用
	barriers[1].Transition.pResource = maskTexture_->GetResource().Get(); // 2枚目：マスク用
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; // 2枚目：マスク用 
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // 2枚目：マスク用
	commandList->ResourceBarrier(2, barriers);
}

// ポストエフェクトの描画（巨大な三角形を描く）※エフェクトの種類を指定して、ON/OFFも指定できるバージョン
void PostEffect::ApplyEffect(
	ID3D12GraphicsCommandList* commandList, DirectXCommon* dxCommon,
	PostEffectType type, bool isEffectActive, uint32_t& src, uint32_t& dest)
{
	// チェックボックスがOFFなら何もしないで次へ！
	if (!isEffectActive) {
		return;
	}

	// 1. 書き込み先の画用紙をセット
	renderTextures_[dest]->PreDrawScene(commandList, dxCommon);

	// 2. パイプライン（シェーダー）をセット
	PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, type);

	// 3. 読み込み元の絵（前までの結果）をセット
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, renderTextures_[src]->GetSrvIndex());

	// 4. 特殊なパラメータ渡し（ノイズの時間・ティント色など b0）
	commandList->SetGraphicsRootConstantBufferView(1, timeResource_->GetGPUVirtualAddress());
	// 4-2. マスク系エフェクト用パラメータ (b1)
	commandList->SetGraphicsRootConstantBufferView(2, maskResource_->GetGPUVirtualAddress());

	// 5. 描画！
	commandList->DrawInstanced(3, 1, 0, 0);

	// 6. 書き込み終了
	renderTextures_[dest]->PostDrawScene(commandList, dxCommon);

	// 7. ピンポン！（次に向けて、読み書きの番号を入れ替える）
	std::swap(src, dest);
}



// 最終結果をバックバッファに書き出す（RTV リセット込み）
// Bloom など内部 PostDrawScene 後に RTV が RenderTexture のままになる問題を吸収する
void PostEffect::FinalBlit(ID3D12GraphicsCommandList* commandList, uint32_t finalSrv, bool editorActive){
	// Bloom 等の後処理で RTV が RenderTexture を指したままになるため、常にバックバッファへ切り替える
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetBackBufferRtvHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	D3D12_VIEWPORT viewport = { 0.0f, 0.0f,
		static_cast<float>(dxCommon_->GetClientWidth()),
		static_cast<float>(dxCommon_->GetClientHeight()), 0.0f, 1.0f };
	D3D12_RECT scissorRect = { 0, 0,
		static_cast<LONG>(dxCommon_->GetClientWidth()),
		static_cast<LONG>(dxCommon_->GetClientHeight()) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	// エディタ非アクティブ時のみ最終描画を実行（アクティブ時は Game View に表示する）
	if (!editorActive) {
		PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, finalSrv);
		commandList->DrawInstanced(3, 1, 0, 0);
	}
}

// デバッグ用UIの描画
// デバッグ用UIの描画
void PostEffect::DrawDebugUI(){
#ifdef USE_IMGUI
	if ( ImGui::Begin("インスペクター (詳細設定)") ) {

		static const char* effectNames[] = {
			"None (使わない)",
			"グレースケール (Grayscale)",
			"セピア (Sepia)",
			"ビネット・周辺減光 (Vignetting)",
			"ぼかし弱 (Box Filter)",
			"ぼかし強 (Box Filter 5x5)",
			"綺麗にぼかす (Gaussian Filter)",
			"アウトライン・輪郭抽出 (Outline)",
			"放射状ブラー (Radial Blur)",
			"ノイズ・砂嵐 (Random Noise)",
			"カラーティント (Color Tint)",
			"マスク歪み (Masked Distortion)",
			"マスクグロー (Masked Glow)",
			"マスクセピア (Masked Sepia)",
			"絵本風 (Picture Book)",
			"ティルトシフト・ジオラマ (Tilt Shift)",
			"紙の質感 (Paper Grain)",
			"アイリスワイプ (Iris Wipe)"
		};
		// --- ポストエフェクトのON/OFF設定 ---
		if ( ImGui::CollapsingHeader("ポストエフェクト設定 (Post Effect)", ImGuiTreeNodeFlags_DefaultOpen) ) {

			ImGui::Checkbox("エフェクト全体を有効化", &isActive_);
			ImGui::Separator();

			if ( isActive_ ) {
				// それぞれのエフェクトのON/OFFを切り替えるチェックボックスを出す
				for ( int i = 1; i < static_cast< int >(PostEffectType::Count); ++i ) {
					ImGui::Checkbox(effectNames[i], &activeEffects_[i]);

					// ノイズ(RandomNoise)がONの時だけ、スピード調整スライダーを出す
					if ( i == static_cast< int >(PostEffectType::RandomNoise) && activeEffects_[i] ) {
						ImGui::Indent(); // ちょっと右にずらす
						ImGui::SliderFloat("ノイズの速度", &timeSpeed_, 0.0f, 0.5f);
						if ( ImGui::Button("速度リセット") ) { timeSpeed_ = 0.05f; }
						ImGui::Unindent();
					}
					// ティルトシフトがONの時：強さ・ピント帯を直接いじれる
					if ( i == static_cast< int >(PostEffectType::TiltShift) && activeEffects_[i] ) {
						ImGui::Indent();
						ImGui::SliderFloat("ぼかしの強さ(px)", &paramData_->tiltStrength, 0.0f, 16.0f, "%.1f");
						ImGui::SliderFloat("ピント帯の中心", &paramData_->tiltCenterY, 0.2f, 0.8f, "%.2f");
						ImGui::SliderFloat("ピント帯の広さ", &paramData_->tiltHalfWidth, 0.05f, 0.4f, "%.2f");
						ImGui::Unindent();
					}
					// 紙の質感がONの時：濃さを直接いじれる
					if ( i == static_cast< int >(PostEffectType::PaperGrain) && activeEffects_[i] ) {
						ImGui::Indent();
						ImGui::SliderFloat("紙の質感の濃さ", &paramData_->grainStrength, 0.0f, 0.5f, "%.2f");
						ImGui::Unindent();
					}
					// 絵本風がONの時：色数と輪郭の濃さを直接いじれる
					if ( i == static_cast< int >(PostEffectType::PictureBook) && activeEffects_[i] ) {
						ImGui::Indent();
						ImGui::SliderFloat("色の段階数", &paramData_->posterLevels, 2.0f, 16.0f, "%.0f");
						ImGui::SliderFloat("輪郭線の濃さ", &paramData_->posterEdge, 0.0f, 1.0f, "%.2f");
						ImGui::Unindent();
					}
				}

				// ループの直後（他のエフェクトの下）にBloomを差し込む！
				Bloom::GetInstance()->DrawDebugUI();
			}

			ImGui::Separator();

			// 保存・読み込みボタンを押したときにBloomも一緒に処理させる！
			if ( ImGui::Button("設定を保存") ) {
				Save();
				Bloom::GetInstance()->Save("resources/bloom.json");
			}
			ImGui::SameLine();
			if ( ImGui::Button("設定を読み込む") ) {
				Load();
				Bloom::GetInstance()->Load("resources/bloom.json");
			}
		}

		// --- 現在適用中のエフェクト表示 ---
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[現在適用中のエフェクト]"); // 緑色で文字を表示
		bool hasActiveEffect = false;
		if ( isActive_ ) {
			for ( int i = 1; i < static_cast< int >(PostEffectType::Count); ++i ) {
				if ( activeEffects_[i] ) {
					ImGui::BulletText("%s", effectNames[i]);
					hasActiveEffect = true;
				}
			}

			// BloomがONの時は、適用中のリストにも表示する！
			if ( Bloom::GetInstance()->IsEnabled() ) {
				ImGui::BulletText("発光 (Bloom)");
				hasActiveEffect = true;
			}
		}

		// 1つもかかっていない場合の表示
		if ( !isActive_ || !hasActiveEffect ) {
			ImGui::Text("  (なし)");
		}

	}
	ImGui::End();
#endif
}


// 設定をJSONファイルに保存する関数
void PostEffect::Save(const std::string& filePath) {
	json j;

	// 大元スイッチと時間スピードを保存
	j["isActive"] = isActive_;
	j["timeSpeed"] = timeSpeed_;

	//  配列の中身をすべて保存 (例: "1": true, "2": false ...)
	for (int i = 1; i < static_cast<int>(PostEffectType::Count); ++i) {
		j["activeEffects"][std::to_string(i)] = activeEffects_[i];
	}

	std::ofstream file(filePath);
	if (file.is_open()) {
		file << j.dump(4);
		file.close();
	}
}
// JSONファイルから設定を読み込む関数
void PostEffect::Load(const std::string& filePath) {
	std::ifstream file(filePath);
	if (!file.is_open()) {
		return; // ファイルがなければ何もしない
	}

	json j;
	file >> j;

	if (j.contains("isActive")) {
		isActive_ = j["isActive"];
	}
	if (j.contains("timeSpeed")) {
		timeSpeed_ = j["timeSpeed"];
	}

	//  JSONから配列にON/OFFの設定を復元する
	if (j.contains("activeEffects")) {
		for (int i = 1; i < static_cast<int>(PostEffectType::Count); ++i) {
			std::string key = std::to_string(i);
			if (j["activeEffects"].contains(key)) {
				activeEffects_[i] = j["activeEffects"][key];
			}
		}
	}
}