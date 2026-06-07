#include "Sprite.h"


// 標準ライブラリ
#include <cassert>
#include <windows.h>      
#include <algorithm> 
#include <utility>

// --- エンジン内部ヘッダー ---
#include "Engine/2D/SpriteCommon.h"
#include "Engine/Base/DirectXCommon.h"
#include "engine/utils/RenderStats.h"
#include "Engine/Math/Matrix4x4.h"
#include "Engine/Graphics/SrvManager.h"
#include "Engine/Graphics/TextureManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/base/WindowProc.h"


using namespace MatrixMath;


std::unique_ptr<Sprite> Sprite::Create(const std::string& textureName, Vector2 position, Vector4 color, Vector2 anchorpoint) {
	// コマンドリストが必要なら取得（TextureManagerの仕様による）
	auto commandList = DirectXCommon::GetInstance()->GetCommandList();

	// テクスチャをロードしてデータを受け取る
	TextureData data = TextureManager::GetInstance()->LoadTextureAndCreateSRV(textureName, commandList);

	// データの中にあるsrvIndex（番号）を使う
	return Create(data.srvIndex, position, color, anchorpoint);
}
// 静的生成関数
std::unique_ptr<Sprite> Sprite::Create(uint32_t textureIndex, Vector2 position, Vector4 color, Vector2 anchorpoint) {
	// 1. インスタンス生成
	std::unique_ptr<Sprite> sprite = std::make_unique<Sprite>();
	if (sprite == nullptr) {
		return nullptr;
	}

	// 2. 初期化
	sprite->Initialize();

	// 3. 各種設定を一括で行う
	sprite->SetTexture(textureIndex); // テクスチャセット
	sprite->SetPosition(position);    // 座標セット
	sprite->SetColor(color);          // 色セット
	sprite->SetAnchorPoint(anchorpoint); // アンカーセット

	return sprite;
}

void Sprite::SetTexture(uint32_t textureIndex) {
	textureIndex_ = textureIndex;
	textureHandle_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(textureIndex);
	
	//	テクスチャの幅と高さを取得して、スプライトのサイズに反映させる
	const TextureData& texData = TextureManager::GetInstance()->GetTextureDataBySrvIndex(textureIndex);

	// 幅か高さが0じゃない時だけ上書きする（安全対策）
	if ( texData.width > 0.0f && texData.height > 0.0f ) {
		size_ = { texData.width, texData.height };

		textureSize_ = { texData.width, texData.height };

	}
	// サイズが変わったので、頂点の形を再計算させる！
	UpdateVertexData();

}

void Sprite::SetAnchorPoint(const Vector2& anchorPoint) {
	anchorPoint_ = anchorPoint;
	isDirty_ = true;     // フラグを立てる
	UpdateVertexData();  // 頂点更新
}

void Sprite::SetColor(const Vector4& color) {
	materialData_->color = color;
}

void Sprite::Initialize(){
	spriteCommon_ = SpriteCommon::GetInstance();
	assert(spriteCommon_);

	// 1. 頂点バッファ作成
	CreateVertexBuffer();

	// 2. インデックスバッファ作成
	CreateIndexBuffer();

	// 3. マテリアルバッファ作成
	CreateMaterialBuffer();

	// 4. 座標変換行列バッファ作成
	CreateTransformationMatrixBuffer();

	// 5. 頂点データの初期値を書き込む
	UpdateVertexData();
	
}

void Sprite::Update(){

	// 位置、回転、スケールからワールド行列を作る
	transform.translate = { position_.x, position_.y, 0.0f };
	transform.rotate = { 0.0f, 0.0f, rotation_ };
	transform.scale = { 1.0f, 1.0f, 1.0f };

	// ワールド行列を作る
	Matrix4x4 worldMatrix = MakeAffine(transform.scale, transform.rotate, transform.translate);
	Matrix4x4 viewMatrix = MakeIdentity4x4();

	// 画面の幅と高さを取得
	float screenW = static_cast< float >( WindowProc::GetInstance()->GetClientWidth() );
	float screenH = static_cast< float >( WindowProc::GetInstance()->GetClientHeight() );


	Matrix4x4 projectionMatrix = Orthographic(0.0f, 0.0f, screenW, screenH, 0.0f, 100.0f);
	Matrix4x4 viewProjection = Multiply(viewMatrix, projectionMatrix);
	Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, viewProjection);
	transformationMatirxData_->WVP = worldViewProjectionMatrix;
	transformationMatirxData_->World = worldMatrix;



}

void Sprite::Draw(){

	// 描画統計に1回計上
	RenderStats::GetInstance()->AddDrawCall();

	assert(spriteCommon_);
	auto* dxCommon = spriteCommon_->GetDxCommon();
	assert(dxCommon);

	auto* commandList = dxCommon->GetCommandList();
	assert(commandList);
	
	SpriteCommon::GetInstance()->PreDraw(commandList); // 共通の描画設定


	// トポロジの設定
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// VBVを設定 : IBVを設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);

	// マテリアル定数バッファ
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// 行列定数バッファ
	commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource_->GetGPUVirtualAddress());

	// RootSignatureが共通である以上、バインドしないとエラーまたは不定動作になります。
	//commandList->SetGraphicsRootConstantBufferView(3, directionalResourceLight->GetGPUVirtualAddress());

	// ここで更新してSpriteの画像を変えないようにする
	if ( textureHandle_.ptr != 0 ) { // ハンドルがセットされているか念のため確認
		commandList->SetGraphicsRootDescriptorTable(2, textureHandle_);
	}

	//描画！(DraoCall/ドローコール)
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);




}


// 頂点バッファ作成
void Sprite::CreateVertexBuffer(){

	// 頂点リソースを作成（例: 4頂点）
	vertexResource_ = spriteCommon_->GetDxCommon()->GetResourceFactory()->CreateBufferResource(sizeof(VertexData) * 4);
	if ( !vertexResource_ ) {
		OutputDebugStringA("Sprite::Initialize: CreateBufferResource(vertex) failed\n");
		return; // またはエラーフラグをセットして処理中断
	}

	// ビューの設定
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// マップしてポインタ取得（HRESULTを確認）
	void* mappedPtr = nullptr;
	HRESULT hr = vertexResource_->Map(0, nullptr, &mappedPtr);
	if ( FAILED(hr) || mappedPtr == nullptr ) {
		OutputDebugStringA("Sprite::Initialize: vertexResource_->Map failed or returned null\n");
		return;
	}
	vertexData_ = reinterpret_cast<VertexData*>(mappedPtr);
}
// indexバッファ作成
void Sprite::CreateIndexBuffer(){

	indexResource_ = spriteCommon_->GetDxCommon()->GetResourceFactory()->CreateBufferResource(sizeof(uint32_t) * 6);

	// リソースの先頭のアドレスから使う
	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();

	//使用するリソースのサイズはindex6つ分のサイズ 
	indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;

	// indexはuint32_tとする
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

	indexResource_->Map(0, nullptr, reinterpret_cast< void** >( &indexData_ ));

	// 三角形1つ目 (左上 -> 右上 -> 左下) : 時計回り
	indexData_[0] = 0;
	indexData_[1] = 2;
	indexData_[2] = 1;

	// 三角形2つ目 (左下 -> 右上 -> 右下) : 時計回り
	indexData_[3] = 1;
	indexData_[4] = 2;
	indexData_[5] = 3;

	// マップ解除
	indexResource_->Unmap(0, nullptr);

}
// マテリアルバッファ作成
void Sprite::CreateMaterialBuffer(){

	// Sprite用のマテリアルリソースを作る
	materialResource_ = spriteCommon_->GetDxCommon()->GetResourceFactory()->CreateBufferResource(sizeof(Material));
	assert(materialResource_);
	// Mapしてデータを書き込む。色は白設定しておく
	materialResource_->Map(0, nullptr, reinterpret_cast< void** >( &materialData_ ));

	materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	// SpriteはLightingしないのでfalseを設定する
	materialData_->enableLighting = false;
	// UVTransformはSpriteでは使うので設定しておく。今回は単位行列を設定しておく
	materialData_->uvTransfrom = MakeIdentity4x4();
	// Shininessは使わないが一応0.0fを設定しておく
	materialData_->shininess = 0.0f;


}
// 変換行列バッファ作成
void Sprite::CreateTransformationMatrixBuffer(){

	transformationMatrixResource_ = spriteCommon_->GetDxCommon()->GetResourceFactory()->CreateBufferResource(sizeof(TransformationMatrix));

	// 書き込むためのアドレスを取得
	transformationMatrixResource_->Map(0, nullptr, reinterpret_cast< void** >( &transformationMatirxData_ ));
	// 単位行列を書き込んでおく
	transformationMatirxData_->WVP = MakeIdentity4x4();
	transformationMatirxData_->World = MakeIdentity4x4();
	transformationMatirxData_->WorldInverseTranspose = MakeIdentity4x4();

}
// 頂点データの計算・転送
void Sprite::UpdateVertexData(){

	float left = (0.0f - anchorPoint_.x) * size_.x;
	float right = (1.0f - anchorPoint_.x) * size_.x;
	float top = (0.0f - anchorPoint_.y) * size_.y;
	float bottom = (1.0f - anchorPoint_.y) * size_.y;
	// 左右反転
	if ( isFlipX_ ) {
		std::swap(left, right);
	}
	// 上下反転
	if ( isFlipY_ ) {
		std::swap(top, bottom);
	}

	// 1. TextureManagerから実際の画像のサイズを取得する
	const TextureData& texData = TextureManager::GetInstance()->GetTextureDataBySrvIndex(textureIndex_);
	float texWidth = texData.width;
	float texHeight = texData.height;

	// 2. ピクセル座標を画像全体のサイズで割って、UV座標（0.0～1.0）を計算する！
	float texLeft = textureLeftTop_.x / texWidth;
	float texRight = (textureLeftTop_.x + textureSize_.x) / texWidth;
	float texTop = textureLeftTop_.y / texHeight;
	float texBottom = (textureLeftTop_.y + textureSize_.y) / texHeight;


	// 0: 左上 (Top-Left)
	vertexData_[0].position = { left, top, 0.0f, 1.0f }; // Yは上(top)
	vertexData_[0].texcoord = { texLeft, texTop };            // UVは(0,0)
	vertexData_[0].normal = { 0.0f, 0.0f, -1.0f };

	// 1: 左下 (Bottom-Left)
	vertexData_[1].position = { left, bottom, 0.0f, 1.0f }; // Yは下(bottom)
	vertexData_[1].texcoord = { texLeft, texBottom };               // UVは(0,1)
	vertexData_[1].normal = { 0.0f, 0.0f, -1.0f };

	// 2: 右上 (Top-Right)
	vertexData_[2].position = { right, top, 0.0f, 1.0f };   // Yは上(top)
	vertexData_[2].texcoord = { texRight, texTop };               // UVは(1,0)
	vertexData_[2].normal = { 0.0f, 0.0f, -1.0f };

	// 3: 右下 (Bottom-Right)
	vertexData_[3].position = { right, bottom, 0.0f, 1.0f }; // Yは下(bottom)
	vertexData_[3].texcoord = { texRight, texBottom };                // UVは(1,1)
	vertexData_[3].normal = { 0.0f, 0.0f, -1.0f };

}


// 切り抜きを指定する関数
void Sprite::SetTextureRect(float startX, float startY, float width, float height) {
	// ピクセルとしてそのまま保存する！
	textureLeftTop_ = { startX, startY };
	textureSize_ = { width, height };

	UpdateVertexData();
}