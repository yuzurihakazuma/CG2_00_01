#include "RootSignatureBuilder.h"
// --- 標準ライブラリ ---
#include <cassert>

// CBVは直接ルートパラメータで追加
void RootSignatureBuilder::AddCBV(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility) {
	D3D12_ROOT_PARAMETER param = {}; // ルートパラメータを初期化
	param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
	param.ShaderVisibility = visibility; // 指定されたシェーダーで使う
	param.Descriptor.ShaderRegister = shaderRegister; // レジスタ番号を指定
	param.Descriptor.RegisterSpace = 0; // レジスタスペースは0
	parameters_.push_back(param); // ルートパラメータのリストに追加
}

void RootSignatureBuilder::AddSRV(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility){

	D3D12_ROOT_PARAMETER param = {};
	param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; // 直接SRV(配列)を受け取る
	param.ShaderVisibility = visibility;
	param.Descriptor.ShaderRegister = shaderRegister;
	param.Descriptor.RegisterSpace = 0;
	parameters_.push_back(param);

}

// SRVはDescriptorTableで追加
void RootSignatureBuilder::AddDescriptorTableSRV(UINT baseShaderRegister, D3D12_SHADER_VISIBILITY visibility) {
	auto range = std::make_unique<D3D12_DESCRIPTOR_RANGE>(); // ディスクリプトレンジを作成
	range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	range->NumDescriptors = 1; // 数は1つ
	range->BaseShaderRegister = baseShaderRegister; // ベースのレジスタ番号を指定
	range->RegisterSpace = 0; // レジスタスペースは0
	range->OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // offsetを自動計算

	D3D12_ROOT_PARAMETER param = {}; // ルートパラメータを初期化
	param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
	param.ShaderVisibility = visibility; // 指定されたシェーダーで使う
	param.DescriptorTable.NumDescriptorRanges = 1; // Tableで利用する数
	param.DescriptorTable.pDescriptorRanges = range.get(); // Tableの中身の配列を指定

	ranges_.push_back(std::move(range)); // ディスクリプトレンジのリストに追加
	parameters_.push_back(param); // ルートパラメータのリストに追加
}
// サンプラーはStaticSamplerで追加
void RootSignatureBuilder::AddDefaultSampler(UINT shaderRegister) {
	D3D12_STATIC_SAMPLER_DESC sampler = {}; // サンプラーを初期化
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイナリアフィルタ
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0∼1の範囲側をリピート
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; // 0∼1の範囲側をリピート
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0∼1の範囲側をリピート
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // 比較しない
	sampler.MaxLOD = D3D12_FLOAT32_MAX; // ありったけのMipmap
	sampler.ShaderRegister = shaderRegister; // レジスタ番号を指定
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	samplers_.push_back(sampler); // サンプラーのリストに追加
}
// ルートシグネチャを構築して生成
void RootSignatureBuilder::Build(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12RootSignature>& outRootSig) {
	D3D12_ROOT_SIGNATURE_DESC desc = {}; // ルートシグネチャの説明を初期化
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; // 入力レイアウトを許可
	desc.NumParameters = static_cast<UINT>(parameters_.size()); // ルートパラメータの数を指定
	desc.pParameters = parameters_.data(); // ルートパラメータの配列を指定
	desc.NumStaticSamplers = static_cast<UINT>(samplers_.size()); // サンプラーの数を指定
	desc.pStaticSamplers = samplers_.data(); // サンプラーの配列を指定

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob; // シリアライズ後のバイナリ
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob; // エラー情報
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob); // シリアライズ
	// エラーならログ出力
    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false);
    }
	// 生成
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&outRootSig));
    assert(SUCCEEDED(hr));
}

void RootSignatureBuilder::BuildForCompute(
	ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12RootSignature>& outRootSig){
	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE; // IAフラグ不要
	desc.NumParameters = static_cast<UINT>(parameters_.size());
	desc.pParameters = parameters_.data();
	desc.NumStaticSamplers = static_cast<UINT>(samplers_.size());
	desc.pStaticSamplers = samplers_.data();

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(
		&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		assert(false);
	}
	hr = device->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(), IID_PPV_ARGS(&outRootSig));
	assert(SUCCEEDED(hr));
}


// Computeシェーダー用にUAVをDescriptorTableで追加
void RootSignatureBuilder::AddDescriptorTableUAV(
	UINT baseShaderRegister, D3D12_SHADER_VISIBILITY visibility){
	auto range = std::make_unique<D3D12_DESCRIPTOR_RANGE>();
	range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; // UAV
	range->NumDescriptors = 1;
	range->BaseShaderRegister = baseShaderRegister;
	range->RegisterSpace = 0;
	range->OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER param = {};
	param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param.ShaderVisibility = visibility;
	param.DescriptorTable.NumDescriptorRanges = 1;
	param.DescriptorTable.pDescriptorRanges = range.get();

	ranges_.push_back(std::move(range));
	parameters_.push_back(param);
}
