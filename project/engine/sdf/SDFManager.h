#pragma once
#include <string>
#include <vector>
#include <memory>
#include <d3d12.h>
#include <wrl.h>
#include "engine/sdf/SDFAtlas.h"
#include "engine/sdf/SDFText.h"
#include "engine/sdf/SDFSprite.h"

// =====================================================================
//  SDFManager（シングルトン）
//   SDF システム全体のまとめ役。
//   ・resources/sdf/ を監視し、JSON+PNG のペアを自動ロード
//   ・ファイル更新を検知したらホットリロード（SDFWatcher と連携：
//     input/ に画像やフォントを入れる → 自動生成 → エンジンに即反映）
//   ・エディタパネルからテキスト/スプライトを配置・編集
//   ・配置内容は resources/sdf/sdf_scene.json に保存/復元
//
//   接続方法:
//     EditorManager::Initialize → Initialize()
//     EditorManager::Begin      → Update()        ※安全なタイミングでロードする
//     EditorManager::Update(UI) → DrawDebugUI()
//     GamePlayScene::Draw(最後) → Draw(commandList)
//     EditorManager::Finalize   → Finalize()
// =====================================================================
class SDFManager {
public:
    static SDFManager* GetInstance() {
        static SDFManager instance;
        return &instance;
    }

    void Initialize();

    // フレーム先頭（コマンドリストが閉じている安全な瞬間）に呼ぶ。
    // フォルダ監視・新規ロード・ホットリロードをここでまとめて行う。
    void Update();

    // 配置アイテムを描画（バックバッファに最終画が出た後＝FinalBlit後に呼ぶ）
    void Draw(ID3D12GraphicsCommandList* commandList);

    // 配置アイテムをレンダーテクスチャへ焼き込む。
    // Bloom合成結果に描き込めば、エディタの Game View にも SDF が映る。
    // （target を RT 状態へ遷移→描画→PSR に戻すところまで面倒を見る）
    void DrawIntoTexture(ID3D12GraphicsCommandList* commandList, class RenderTexture* target);

    // エディタパネル
    void DrawDebugUI();

    // 終了処理（D3D12リソースを持つため、リークチェッカーより先に必ず呼ぶ）
    void Finalize();

    // --- シーン（配置内容）の保存/復元 ---
    void SaveScene() const;
    void LoadScene();

    // --- 近接表示の基準位置（プレイヤー）---
    //   Play中にシーンから毎フレーム渡す。「近づいた時だけ表示」の3Dアイテムは
    //   この位置との距離でフェードする。Edit中などは Clear で全表示に戻す。
    void SetViewerPosition(const Vector3& pos) { viewerPos_ = pos; viewerValid_ = true; }
    void ClearViewerPosition() { viewerValid_ = false; }

private:
    SDFManager() = default;
    ~SDFManager() = default;
    SDFManager(const SDFManager&) = delete;
    SDFManager& operator=(const SDFManager&) = delete;

    void BuildPipelines();               // 共有ルートシグネチャ＋PSO（文字用/画像用）
    void ScanAndLoad();                  // フォルダ走査→新規/更新分をロード
    void DrawItems(ID3D12GraphicsCommandList* commandList); // アイテム描画の共通部
    SDFAtlas* FindAtlas(const std::string& name) const;

    // --- 配置アイテム ---
    struct TextItem {
        std::string atlasName;           // 使うフォントアトラス名
        std::unique_ptr<SDFText> text;
    };
    struct SpriteItem {
        std::string atlasName;           // 使うスプライトアトラス名
        std::unique_ptr<SDFSprite> sprite;
    };

    std::vector<std::unique_ptr<SDFAtlas>> atlases_;
    std::vector<TextItem> texts_;
    std::vector<SpriteItem> sprites_;

    // --- 共有パイプライン ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> textPipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipeline_;
    bool pipelineReady_ = false;

    // --- 監視 ---
    std::string watchDir_ = "resources/sdf";
    int scanCounter_ = 0;                // 毎フレーム走査しないためのカウンタ
    std::string status_;

    std::string scenePath_ = "resources/sdf/sdf_scene.json";
    bool sceneLoaded_ = false;

    // --- 近接表示の基準（プレイヤー位置。Play中のみ有効）---
    Vector3 viewerPos_ { 0.0f, 0.0f, 0.0f };
    bool viewerValid_ = false;
};
