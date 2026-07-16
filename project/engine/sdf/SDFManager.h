#pragma once
#include <string>
#include <vector>
#include <memory>
#include <d3d12.h>
#include <wrl.h>
#include "engine/sdf/SDFAtlas.h"
#include "engine/sdf/SDFText.h"
#include "engine/sdf/SDFSprite.h"
#include "engine/sdf/SDFVolumeObject.h"

// =====================================================================
//  SDFManager（シングルトン）
//   SDF システム全体のまとめ役。
//   ・resources/sdf/ を監視し、JSON+PNG のペアを自動ロード
//   ・resources/sdf3d/ も監視し、.sdf3d（3Dボリューム）を自動検知
//     （エディタ配置ぶんは新規ロード、ゲームコード所有ぶんも含め更新は
//       全インスタンスへホットリロード）
//   ・ファイル更新を検知したらホットリロード（SDFWatcher と連携：
//     input/ に画像やフォント、.obj を入れる → 自動生成 → エンジンに即反映）
//   ・エディタパネルからテキスト/スプライト/3Dボリュームを配置・編集
//   ・配置内容は resources/sdf/sdf_scene.json に保存/復元
//
//   接続方法:
//     EditorManager::Initialize → Initialize()
//     EditorManager::Begin      → Update()        ※安全なタイミングでロードする
//     EditorManager::Update(UI) → DrawDebugUI()
//     GamePlayScene::Draw(最後) → Draw(commandList)
//     GamePlayScene::Draw(MRT内)→ DrawVolumes(commandList) ※3Dボリュームは深度あり
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

    // エディタで配置した3Dボリュームを描画（シーンMRTパスの最後＝深度ありの中で呼ぶ）
    void DrawVolumes(ID3D12GraphicsCommandList* commandList);

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

    // --- シーン専用アイテムの単発描画 ---
    //   sdf_scene.json の配置とは別に、シーンが自分で持つ SDFText / SDFSprite を描く入口
    //   （タイトルロゴ・HUDの数字など）。パイプラインの張り替え込みで、
    //   「現在セットされているレンダーターゲット」へそのまま描く。
    void DrawTextItem(ID3D12GraphicsCommandList* commandList, SDFText& text, const std::string& atlasName);
    void DrawSpriteItem(ID3D12GraphicsCommandList* commandList, SDFSprite& sprite, const std::string& atlasName);

    // アトラス参照（画像サイズの取得などシーン側のレイアウト計算用）
    SDFAtlas* GetAtlas(const std::string& name) const { return FindAtlas(name); }

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
    // 3Dボリューム配置：パラメータはここが正で、描画時に vol へ流し込む。
    // vol はファイルが届いた時に後から解決される（アトラスの遅延解決と同じ流儀）
    struct VolumeItem {
        std::string fileName;            // resources/sdf3d/ 内の .sdf3d 名（拡張子なし）
        std::string morphFile;           // モーフ先（拡張子なし。空=なし）
        Vector3 translation { 0.0f, 3.0f, 0.0f };
        float scale = 1.0f;
        Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f }; // カラーボリュームが無い時の単色
        float erode = 0.0f;
        float morphT = 0.0f;
        bool visible = true;
        std::unique_ptr<SDFVolumeObject> vol;     // 実体（ファイル未着なら nullptr）
        std::string loadedMorph;         // vol に実際に読み込んだモーフ先（変更検知用）
    };

    void ResolveVolumeItem(VolumeItem& item, ID3D12GraphicsCommandList* commandList);

    std::vector<std::unique_ptr<SDFAtlas>> atlases_;
    std::vector<TextItem> texts_;
    std::vector<SpriteItem> sprites_;
    std::vector<VolumeItem> volumes_;

    // --- 共有パイプライン ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> textPipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipeline_;
    bool pipelineReady_ = false;

    // --- 監視 ---
    std::string watchDir_ = "resources/sdf";
    std::string volumeDir_ = "resources/sdf3d";
    std::vector<std::string> volumeFiles_; // 監視フォルダで見つかった .sdf3d（拡張子なし名）
    int scanCounter_ = 0;                // 毎フレーム走査しないためのカウンタ
    std::string status_;

    std::string scenePath_ = "resources/sdf/sdf_scene.json";
    bool sceneLoaded_ = false;

    // --- 近接表示の基準（プレイヤー位置。Play中のみ有効）---
    Vector3 viewerPos_ { 0.0f, 0.0f, 0.0f };
    bool viewerValid_ = false;
};
