#include "TextManager.h"

// --- 標準ライブラリ ---
#include <fstream>
#include <iostream>
#include <Windows.h> // 文字列変換用

// --- 外部ライブラリ ---
#include "externals/nlohmann/json.hpp"
#include <ResourceUploadBatch.h> // DirectXTK用
#include <RenderTargetState.h>

// --- エンジン側のファイル ---
#include "engine/scene/SceneManager.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/WindowProc.h"
#include "engine/graphics/SrvManager.h"

using json = nlohmann::json;

// --- UTF-8 から wstring(ワイド文字列) への変換ヘルパー関数 ---
// （日本語などのマルチバイト文字をDirectXTKで使える形式に変換します）
std::wstring ConvertString(const std::string& utf8){
	if ( utf8.empty() ) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8[0], ( int ) utf8.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &utf8[0], ( int ) utf8.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

namespace {
void DrawTextData(DirectX::SpriteFont* spriteFont, DirectX::SpriteBatch* spriteBatch, const TextData& data) {
	std::wstring wText = ConvertString(data.text);
	if (wText.empty()) {
		return;
	}

	DirectX::XMFLOAT2 origin = { 0.0f, 0.0f };
	if (data.isCentered) {
		DirectX::XMVECTOR measured = spriteFont->MeasureString(wText.c_str());
		DirectX::XMFLOAT2 measuredSize {};
		DirectX::XMStoreFloat2(&measuredSize, measured);
		origin = { measuredSize.x * 0.5f, measuredSize.y * 0.5f };
	}

	if (data.hasOutline && data.outlineOffset > 0.0f) {
		DirectX::XMVECTOR outlineColor = DirectX::XMVectorSet(
			data.outlineColor[0],
			data.outlineColor[1],
			data.outlineColor[2],
			data.outlineColor[3]
		);
		const float offset = data.outlineOffset;
		const DirectX::XMFLOAT2 offsets[] = {
			{ -offset, -offset },
			{  0.0f,  -offset },
			{  offset, -offset },
			{ -offset,  0.0f },
			{  offset,  0.0f },
			{ -offset,  offset },
			{  0.0f,   offset },
			{  offset,  offset },
		};

		for (const DirectX::XMFLOAT2& outlineOffset : offsets) {
			spriteFont->DrawString(
				spriteBatch,
				wText.c_str(),
				DirectX::XMFLOAT2(data.x + outlineOffset.x, data.y + outlineOffset.y),
				outlineColor,
				0.0f,
				origin,
				data.scale
			);
		}
	}

	DirectX::XMVECTOR color = DirectX::XMVectorSet(data.color[0], data.color[1], data.color[2], data.color[3]);
	spriteFont->DrawString(
		spriteBatch,
		wText.c_str(),
		DirectX::XMFLOAT2(data.x, data.y),
		color,
		0.0f,
		origin,
		data.scale
	);
}
}

void TextManager::Initialize(){
	// 2回初期化されるのを防ぐ安全装置
	if ( spriteBatch_ ){
		return;
	}

	auto dxCommon = DirectXCommon::GetInstance();
	auto device = dxCommon->GetDevice();

	// 1. DirectXTKの初期化（メモリとヒープの準備）
	graphicsMemory_ = std::make_unique<DirectX::GraphicsMemory>(device);
	resourceDescriptors_ = std::make_unique<DirectX::DescriptorHeap>(device, 1);

	// 2. フォント画像のGPUへの転送準備
	DirectX::ResourceUploadBatch resourceUpload(device);
	resourceUpload.Begin();

	// DirectX12では、画面のカラーフォーマットと深度フォーマットを指定する必要があります
	DirectX::RenderTargetState rtState(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_D24_UNORM_S8_UINT);
	DirectX::SpriteBatchPipelineStateDescription pd(rtState);

	// 設定を渡して SpriteBatch を作成！
	spriteBatch_ = std::make_unique<DirectX::SpriteBatch>(device, resourceUpload, pd);
	
	// 3. コマンドプロンプトで作った .spritefont を読み込む
	spriteFont_ = std::make_unique<DirectX::SpriteFont>(
		device,
		resourceUpload,
		L"resources/jpFont.spritefont",
		resourceDescriptors_->GetCpuHandle(0),
		resourceDescriptors_->GetGpuHandle(0)
	);

	// 4. 転送完了を待つ
	auto uploadResourcesFinished = resourceUpload.End(dxCommon->GetCommandQueue());
	uploadResourcesFinished.wait();

	// JSONから設定を読み込む（起動時に自動読み込み）
	Load();
}

void TextManager::Draw(){
	// テキストが1つもない場合は処理しない
	if ( texts_.empty() ){
		return;
	}
	
	auto dxCommon = DirectXCommon::GetInstance();
	auto commandList = dxCommon->GetCommandList();

	auto windowProc = WindowProc::GetInstance();
	// ビューポートの設定（画面全体をカバーするように）
	D3D12_VIEWPORT viewport {};
	viewport.Width = static_cast< float >( windowProc->GetClientWidth() );
	viewport.Height = static_cast< float >( windowProc->GetClientHeight() );
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	spriteBatch_->SetViewport(viewport);


	// DirectXTK12の描画ルール：専用のペン（ディスクリプタヒープ）をセットする
	ID3D12DescriptorHeap* heaps[] = { resourceDescriptors_->Heap() };
	commandList->SetDescriptorHeaps(1, heaps);

	// お絵かき開始！
	spriteBatch_->Begin(commandList);

	for ( auto& pair : texts_ ) {
		const TextData& data = pair.second;

		try {
			DrawTextData(spriteFont_.get(), spriteBatch_.get(), data);
		} catch ( ... ) {
			// ⚠️未対応の文字（今回なら日本語など）が来たときにクラッシュするのを防ぐ
		}
	}

	// お絵かき終了！
	spriteBatch_->End();

	// 描画に使ったグラフィックメモリをGPUに送る
	graphicsMemory_->Commit(dxCommon->GetCommandQueue());

	//	描画後の状態をリセット（SRVのバインド解除など）
	SrvManager::GetInstance()->PreDraw();

}

void TextManager::DrawText(const std::string& key) {
	auto it = texts_.find(key);
	if (it == texts_.end() || it->second.text.empty()) {
		return;
	}

	auto dxCommon = DirectXCommon::GetInstance();
	auto commandList = dxCommon->GetCommandList();

	auto windowProc = WindowProc::GetInstance();
	D3D12_VIEWPORT viewport {};
	viewport.Width = static_cast<float>(windowProc->GetClientWidth());
	viewport.Height = static_cast<float>(windowProc->GetClientHeight());
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	spriteBatch_->SetViewport(viewport);

	ID3D12DescriptorHeap* heaps[] = { resourceDescriptors_->Heap() };
	commandList->SetDescriptorHeaps(1, heaps);

	spriteBatch_->Begin(commandList);

	const TextData& data = it->second;

	try {
		DrawTextData(spriteFont_.get(), spriteBatch_.get(), data);
	} catch (...) {
		// 未対応文字が来たときにクラッシュするのを防ぐ
	}

	spriteBatch_->End();
	graphicsMemory_->Commit(dxCommon->GetCommandQueue());
	SrvManager::GetInstance()->PreDraw();
}

void TextManager::DrawDebugUI(){
#ifdef USE_IMGUI
	ImGui::Begin("Text Manager (JSON)");

	// 1. 新しいテキストの追加
	ImGui::Text("【新しいテキストの追加】");
	ImGui::InputText("管理キー (例: CardCost)", inputKey_, sizeof(inputKey_));
	ImGui::InputText("表示文字 (例: u8\"コスト: 3\")", inputText_, sizeof(inputText_));

	if ( ImGui::Button("追加 / 更新") ) {
		std::string key = inputKey_;
		if ( !key.empty() ) {
			texts_[key].text = inputText_;
		}
	}

	ImGui::Separator();

	// 2. 登録されているテキストのリストと編集
	ImGui::Text("【登録済みのテキスト】");
	// 削除用のキーを保持する変数
	std::string keyToDelete = "";

	for ( auto& pair : texts_ ) {
		std::string key = pair.first;
		TextData& data = pair.second;

		// 折りたたみメニューで個別の設定を表示
		if ( ImGui::TreeNode(key.c_str()) ) {
			// 座標調整
			ImGui::DragFloat("X座標", &data.x, 1.0f);
			ImGui::DragFloat("Y座標", &data.y, 1.0f);
			ImGui::DragFloat("大きさ", &data.scale, 0.05f, 0.1f, 10.0f);
			
			// 色調整
			ImGui::ColorEdit4("色", data.color);
			
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
			if ( ImGui::Button("このテキストを削除") ) {
				// ループ内で直接消すとクラッシュするので、名前だけ覚えておく
				keyToDelete = key;
			}
			ImGui::PopStyleColor();


			ImGui::TreePop();
		}
	}

	if ( !keyToDelete.empty() ) {
		texts_.erase(keyToDelete);
	}

	ImGui::Separator();

	// 3. セーブ＆ロードボタン
	if ( ImGui::Button("JSONに保存 (Save)") ) {
		Save();
	}
	ImGui::SameLine();
	if ( ImGui::Button("JSONから読込 (Load)") ) {
		Load();
	}

	ImGui::End();
#endif
}

void TextManager::Save(const std::string& filePath){
	json j;
	for ( const auto& pair : texts_ ) {
		const std::string& key = pair.first;
		const TextData& data = pair.second;

		j[key]["text"] = data.text;
		j[key]["x"] = data.x;
		j[key]["y"] = data.y;
		j[key]["scale"] = data.scale;
		j[key]["isCentered"] = data.isCentered;
		j[key]["color"] = { data.color[0], data.color[1], data.color[2], data.color[3] };
		j[key]["hasOutline"] = data.hasOutline;
		j[key]["outlineColor"] = { data.outlineColor[0], data.outlineColor[1], data.outlineColor[2], data.outlineColor[3] };
		j[key]["outlineOffset"] = data.outlineOffset;
	}

	std::ofstream file(filePath);
	if ( file.is_open() ) {
		file << std::setw(4) << j << std::endl;
	}
}

void TextManager::Load(const std::string& filePath){
	std::ifstream file(filePath);
	if ( !file.is_open() ) return;

	json j;
	file >> j;

	texts_.clear(); // 現在のリストをリセット

	for ( auto it = j.begin(); it != j.end(); ++it ) {
		std::string key = it.key();
		TextData data;

		data.text = it.value()["text"];
		data.x = it.value()["x"];
		data.y = it.value()["y"];

		if ( it.value().contains("scale") ) {
			data.scale = it.value()["scale"];
		} else {
			data.scale = 1.0f;
		}

		if ( it.value().contains("isCentered") ) {
			data.isCentered = it.value()["isCentered"];
		} else {
			data.isCentered = false;
		}

		auto colorArray = it.value()["color"];
		data.color[0] = colorArray[0];
		data.color[1] = colorArray[1];
		data.color[2] = colorArray[2];
		data.color[3] = colorArray[3];

		if ( it.value().contains("hasOutline") ) {
			data.hasOutline = it.value()["hasOutline"];
		}
		if ( it.value().contains("outlineColor") ) {
			auto outlineColorArray = it.value()["outlineColor"];
			data.outlineColor[0] = outlineColorArray[0];
			data.outlineColor[1] = outlineColorArray[1];
			data.outlineColor[2] = outlineColorArray[2];
			data.outlineColor[3] = outlineColorArray[3];
		}
		if ( it.value().contains("outlineOffset") ) {
			data.outlineOffset = it.value()["outlineOffset"];
		}

		texts_[key] = data;
	}
}

void TextManager::SetText(const std::string& key, const std::string& text){
	// キーが存在しない場合は新しいエントリーを作成
	texts_[key].text = text;
}

void TextManager::SetPosition(const std::string& key, float x, float y) {
	texts_[key].x = x;
	texts_[key].y = y;
}

void TextManager::SetCentered(const std::string& key, bool isCentered) {
	texts_[key].isCentered = isCentered;
}

void TextManager::SetScale(const std::string& key, float scale) {
	texts_[key].scale = scale;
}

void TextManager::SetColor(const std::string& key, float r, float g, float b, float a) {
	texts_[key].color[0] = r;
	texts_[key].color[1] = g;
	texts_[key].color[2] = b;
	texts_[key].color[3] = a;
}

void TextManager::SetOutline(const std::string& key, bool enabled, float r, float g, float b, float a, float offset) {
	texts_[key].hasOutline = enabled;
	texts_[key].outlineColor[0] = r;
	texts_[key].outlineColor[1] = g;
	texts_[key].outlineColor[2] = b;
	texts_[key].outlineColor[3] = a;
	texts_[key].outlineOffset = offset;
}

void TextManager::Finalize(){
	spriteFont_.reset();
	spriteBatch_.reset();
	resourceDescriptors_.reset();
	graphicsMemory_.reset();
}
