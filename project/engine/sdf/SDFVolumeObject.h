#pragma once
#include <string>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include "engine/math/struct.h"

// =====================================================================
//  SDFVolumeObject
//   SDF3DPrintf.py が出力した .sdf3d（3Dボリューム距離場）を読み込み、
//   レイマーチング（スフィアトレーシング）でメッシュ無しに立体を描画する。
//   ・ポリゴンはプロキシの箱1個ぶん（12三角形）だけ。形はシェーダーが作る
//   ・法線は距離場の勾配から求めるので、頂点データそのものが不要＝超軽量
//   ・描画はシーンMRTパスの最後（Obj3d等の後）に Draw を呼ぶ
//   ・パイプラインは全インスタンス共有（初回 Load で構築）
//  .sdf3d フォーマット: ヘッダ36バイト（w/h/d + boxMin + boxMax）+ float距離配列
// =====================================================================
class SDFVolumeObject {
public:
    // .sdf3d を読み込んで Texture3D を作る（コマンドリストは記録中であること）
    bool Load(const std::string& sdf3dPath, ID3D12GraphicsCommandList* commandList);

    void SetTranslation(const Vector3& t) { translation_ = t; }
    void SetScale(float s) { scale_ = s; }               // 一様スケール（距離値も同率で換算）
    void SetColor(const Vector4& c) { color_ = c; }
    void SetLightDir(const Vector3& d) { lightDir_ = d; }
    // エロージョン量(m)：+で表面が法線方向に痩せる（溶けて消える）/ -で太る。0=通常
    void SetErode(float e) { erode_ = e; }
    Vector3& RefTranslation() { return translation_; }   // ImGui編集用
    float&   RefScale() { return scale_; }               // ImGui編集用
    float&   RefErode() { return erode_; }               // ImGui編集用
    bool IsLoaded() const { return loaded_; }

    void Update();                                       // CB更新（毎フレーム。既定カメラ使用）
    void Draw(ID3D12GraphicsCommandList* commandList);   // 専用PSOを自分でセットして描く

    // 共有パイプラインの解放（リークチェッカーより先に。EditorManager::Finalize から呼ぶ）
    static void FinalizeShared();

private:
    static bool BuildPipeline();                         // 共有パイプライン構築（初回のみ）

    // .sdf3d のヘッダ（SDF3DPrintf.py の出力仕様と一致させること）
    struct Header {
        int32_t width = 0, height = 0, depth = 0;
        float boxMin[3] {};
        float boxMax[3] {};
    };

    // シェーダーの VolumeCB と同じ並び（float3 の後にパディング）
    struct VolumeCB {
        float viewProj[16];
        float boxMin[3]; float pad0;
        float boxMax[3]; float pad1;
        float cameraPos[3]; float distScale;
        float baseColor[4];
        float lightDir[3]; float erode;
        float useColorTex; float pad3[3]; // 1=カラーボリュームで着色 / 0=baseColor
    };

    Header header_ {};
    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;      // Texture3D<float>（距離）
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer_; // 転送完了まで生かしておく
    uint32_t srvIndex_ = 0;

    // カラーボリューム（.sdfcol があれば使う。無ければ baseColor の単色）
    Microsoft::WRL::ComPtr<ID3D12Resource> colorTexture_;      // Texture3D<RGBA8(sRGB)>
    Microsoft::WRL::ComPtr<ID3D12Resource> colorUploadBuffer_;
    uint32_t colorSrvIndex_ = 0;
    bool hasColorVolume_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_; // プロキシ箱（単位キューブ）
    D3D12_VERTEX_BUFFER_VIEW vbv_ {};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW ibv_ {};

    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    VolumeCB* cbData_ = nullptr;

    Vector3 translation_ { 0.0f, 0.0f, 0.0f };
    float   scale_ = 1.0f;
    Vector4 color_ { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector3 lightDir_ { 0.4f, -1.0f, 0.3f }; // 上からの光（正規化はシェーダー側）
    float   erode_ = 0.0f;                   // エロージョン量(m)
    bool    loaded_ = false;

    // 共有パイプライン（全インスタンスで1組）
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> sRootSignature_;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> sPipeline_;
};
