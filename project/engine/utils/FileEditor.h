#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <wrl.h>
#include <d3d12.h>

class RenderTexture;
class Camera;
class Obj3d;

// =====================================================================
//  FileEditor
//   Unity の Project ウィンドウ風のファイルブラウザ＋テキストエディタ。
//   実ファイルを走査し、テキスト（.md / .txt / .json / .hlsl など）を
//   その場で編集して保存＝実ファイルに同期する。
//   ファイル/フォルダの新規作成・削除にも対応。
// =====================================================================
class FileEditor {
public:
    FileEditor() = default;
    ~FileEditor(); // unique_ptr<前方宣言型> のため cpp で定義

    void Initialize();
    void DrawDebugUI();

    // フレーム先頭（コマンドリストが閉じていて安全なタイミング）で呼ぶ。
    // DrawDebugUI 中に積んだ画像サムネイルを、専用のコマンド記録でまとめて読み込む。
    void ProcessPendingThumbnails();

private:
    struct Entry {
        std::string name;      // 表示名（ファイル/フォルダ名）
        std::string fullPath;  // ルートからのフルパス（generic 形式）
        bool isDir = false;
    };

    void ScanDir();                          // currentDir_ の中身を列挙
    void OpenFile(const std::string& path);  // テキストを読み込む
    void SaveFile();                         // 編集内容を実ファイルへ書き戻す
    void CreateNewFile(const std::string& name);
    void CreateFolder(const std::string& name);
    void DeleteOpened();

    std::string rootPath_    = "resources"; // 参照ルート（UIで変更可）
    std::string currentDir_  = "resources"; // 現在のフォルダ
    std::vector<Entry> entries_;            // currentDir_ の中身
    std::string openedFile_;                // 編集中ファイルのフルパス
    std::string textBuffer_;                // 編集内容
    bool dirty_ = false;                    // 未保存の変更
    bool isBinaryOpened_ = false;           // 編集不可（バイナリ/巨大）
    std::string status_;                    // ステータス表示

    char newNameBuf_[128] = "new_file.md";  // 新規作成名
    char rootBuf_[260]    = "resources";    // ルート入力欄

    // 画像サムネイルのキャッシュ（フルパス → SRV番号）
    std::unordered_map<std::string, uint32_t> thumbCache_;
    static constexpr uint32_t kNoThumb = 0xffffffffu; // サムネイル無し

    // このフレームに表示された未ロード画像（次フレーム先頭で読み込む）
    std::vector<std::string> pending_;

    // ===== モデルの3Dサムネイル =====
    void SetupThumbResources();     // 深度・DSV・カメラの用意（初回のみ）
    void RenderModelThumbnails();   // 積まれたモデルをRTに描画（コマンド記録中に呼ぶ）

    std::unordered_map<std::string, std::unique_ptr<RenderTexture>> modelThumbs_; // path -> RT
    std::vector<std::string> pendingModels_;         // 今フレームに見えた未描画モデル
    std::vector<std::unique_ptr<Obj3d>> thumbTempObjs_; // 描画確定まで生かす一時Obj3d
    std::unique_ptr<Camera> thumbCamera_;
    std::unique_ptr<RenderTexture> maskRT_; // Object3DはMRT(色+マスク)なので2枚目の捨てRT
    Microsoft::WRL::ComPtr<ID3D12Resource> thumbDepth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> thumbDsvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE thumbDsvHandle_{};
    bool thumbSetupDone_ = false;
    bool maskTransitioned_ = false;
    static constexpr int kThumbSize = 128;
    static constexpr int kMaxModelThumbs = 24; // RTVヒープ節約のため上限
};
