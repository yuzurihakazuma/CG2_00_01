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

		// 色の変換 (RGBA配列 -> XMVECTOR型)
		DirectX::XMVECTOR color = DirectX::XMVectorSet(data.color[0], data.color[1], data.color[2], data.color[3]);

		// 文字列の変換 (std::string -> std::wstring)
		std::wstring wText = ConvertString(data.text);

		try {
			// 画面に描画！
			spriteFont_->DrawString(
				spriteBatch_.get(),
				wText.c_str(),
				DirectX::XMFLOAT2(data.x, data.y),
				color,
				0.0f,                          
				DirectX::XMFLOAT2(0.0f, 0.0f), 
				data.scale                     
			);
		} catch ( ... ) {
			// ⚠️未対応の文字（今回なら日本語など）が来たときにクラッシュするのを防ぐ
		}
	}

	// お絵かき終了！
	spriteBatch_->End();
	//	描画後の状態をリセット（SRVのバインド解除など）
	SrvManager::GetInstance()->PreDraw();

}

void TextManager::DrawDebugUI(){
#ifdef USE_IMGUI
	// 共有の「詳細設定」ウィンドウに折りたたみで合流（ウィンドウを増やさない）
	ImGui::Begin("インスペクター (詳細設定)");
	if ( ImGui::CollapsingHeader("テキスト (Text Manager)") ) {

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

	} // CollapsingHeader: テキスト
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
		j[key]["color"] = { data.color[0], data.color[1], data.color[2], data.color[3] };
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

		auto colorArray = it.value()["color"];
		data.color[0] = colorArray[0];
		data.color[1] = colorArray[1];
		data.color[2] = colorArray[2];
		data.color[3] = colorArray[3];

		texts_[key] = data;
	}
}

void TextManager::SetText(const std::string& key, const std::string& text){
	// キーが存在しない場合は新しいエントリーを作成
	texts_[key].text = text;
}

void TextManager::Finalize(){
	spriteFont_.reset();
	spriteBatch_.reset();
	resourceDescriptors_.reset();
	graphicsMemory_.reset();
}