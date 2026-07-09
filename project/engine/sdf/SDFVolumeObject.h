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
//   ・同名の .sdfcol（カラーボリューム）があれば元テクスチャと同じ模様で着色
//   ・エロージョン：距離場に定数を足して「溶ける/生える」（SetErode）
//   ・モーフィング：2つ目のボリューム(B)を読むと、距離場の lerp で
//     形Aから形Bへ連続変形できる（SetMorphT。メッシュでは不可能な芸当）
//   ・描画はシーンMRTパスの最後（Obj3d等の後）に Draw を呼ぶ
//  .sdf3d フォーマット: ヘッダ36バイト（w/h/d + boxMin + boxMax）+ float距離配列
//  .sdfcol フォーマット: 同ヘッダ + RGBA8配列（最寄り表面のテクスチャ色）
// =====================================================================
class SDFVolumeObject {
public:
    // ベース形状(A)の読み込み（コマンドリストは記録中であること）
    bool Load(const std::string& sdf3dPath, ID3D12GraphicsCommandList* commandList);

    // モーフ先形状(B)の読み込み（Load 成功後に呼ぶ。SetMorphT で A→B へ変形）
    bool LoadMorphTarget(const std::string& sdf3dPath, ID3D12GraphicsCommandList* commandList);

    void SetTranslation(const Vector3& t) { translation_ = t; }
    void SetScale(float s) { scale_ = s; }               // 一様スケール（距離値も同率で換算）
    void SetColor(const Vector4& c) { color_ = c; }      // カラーボリュームが無い時の単色
    void SetLightDir(const Vector3& d) { lightDir_ = d; }
    // エロージョン量(m)：+で表面が法線方向に痩せる（溶けて消える）/ -で太る。0=通常
    void SetErode(float e) { erode_ = e; }
    // モーフ係数：0=形A / 1=形B。中間は「AとBの中間の形」になる
    void SetMorphT(float t) { morphT_ = t; }

    Vector3& RefTranslation() { return translation_; }   // ImGui編集用
    float&   RefScale() { return scale_; }               // ImGui編集用
    float&   RefErode() { return erode_; }               // ImGui編集用
    float&   RefMorphT() { return morphT_; }             // ImGui編集用
    bool IsLoaded() const { return volA_.loaded; }
    bool HasMorph() const { return volB_.loaded; }

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

    // 距離＋カラーのボリューム1組（A=ベース / B=モーフ先）
    struct VolumeSet {
        Header header {};
        Microsoft::WRL::ComPtr<ID3D12Resource> tex;        // Texture3D<float>（距離）
        Microsoft::WRL::ComPtr<ID3D12Resource> upload;     // 転送完了まで生かす
        Microsoft::WRL::ComPtr<ID3D12Resource> colTex;     // Texture3D<RGBA8 sRGB>（色）
        Microsoft::WRL::ComPtr<ID3D12Resource> colUpload;
        uint32_t srv = 0, colSrv = 0;
        bool hasColor = false;
        bool loaded = false;
    };

    // シェーダーの VolumeCB と同じ並び（float3 の後にパディング）
    struct VolumeCB {
        float viewProj[16];
        float rayBoxMin[3]; float pad0;  // レイ/プロキシ箱（モーフ時は A∪B）
        float rayBoxMax[3]; float pad1;
        float boxMinA[3]; float pad2;    // ボリュームAのワールドAABB（サンプル座標変換用）
        float boxMaxA[3]; float pad3;
        float boxMinB[3]; float pad4;    // ボリュームBのワールドAABB
        float boxMaxB[3]; float pad5;
        float cameraPos[3]; float distScale;
        float baseColor[4];
        float lightDir[3]; float erode;
        float useColorTexA; float useColorTexB; float useMorph; float morphT;
    };

    // .sdf3d（＋あれば .sdfcol）を1組読み込んで GPU リソース化する共通処理
    bool LoadVolumeSet(const std::string& sdf3dPath, ID3D12GraphicsCommandList* commandList,
                       VolumeSet& out);

    VolumeSet volA_; // ベース形状
    VolumeSet volB_; // モーフ先（未ロードなら単体表示）

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
    float   erode_  = 0.0f;                  // エロージョン量(m)
    float   morphT_ = 0.0f;                  // モーフ係数（0=A / 1=B）

    // 共有パイプライン（全インスタンスで1組）
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> sRootSignature_;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> sPipeline_;
};
