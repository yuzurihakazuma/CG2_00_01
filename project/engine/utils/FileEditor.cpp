#include "FileEditor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cctype>

#include "engine/utils/ImGuiManager.h" // ImGui
#include "engine/graphics/TextureManager.h"
#include "engine/graphics/SrvManager.h"
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/RenderTexture.h"
#include "engine/camera/Camera.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/3d/obj/Obj3d.h"

namespace {
    // 拡張子を小文字で取り出す
    std::string LowerExt(const std::string& name) {
        auto pos = name.find_last_of('.');
        if ( pos == std::string::npos ) return "";
        std::string ext = name.substr(pos);
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return ( char ) std::tolower(c); });
        return ext;
    }

    // ファイル種別
    enum class Kind { Folder, Image, Model, Json, Audio, Font, Text, Other };

    Kind Classify(const std::string& name, bool isDir) {
        if ( isDir ) return Kind::Folder;
        std::string e = LowerExt(name);
        if ( e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".dds" ) return Kind::Image;
        if ( e == ".obj" || e == ".gltf" || e == ".glb" || e == ".blend" || e == ".fbx" ) return Kind::Model;
        if ( e == ".json" || e == ".spritefont" ) return Kind::Json;
        if ( e == ".wav" || e == ".mp3" || e == ".ogg" ) return Kind::Audio;
        if ( e == ".ttf" || e == ".otf" ) return Kind::Font;
        if ( e == ".md" || e == ".txt" || e == ".hlsl" || e == ".glsl" || e == ".ini" || e == ".csv" ||
             e == ".cfg" || e == ".log" || e == ".xml" || e == ".yaml" || e == ".yml" || e == ".mtl" ||
             e == ".h" || e == ".hpp" || e == ".c" || e == ".cpp" || e == ".bat" || e == ".py" ) return Kind::Text;
        return Kind::Other;
    }

    // サムネイル画像として読み込める拡張子か（dds はキューブマップ等で危険なので除外）
    bool IsThumbable(const std::string& name) {
        std::string e = LowerExt(name);
        return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga";
    }

#ifdef USE_IMGUI
    // 種別ごとのアイコン色とラベル
    void KindStyle(Kind k, ImU32& col, const char*& tag) {
        switch ( k ) {
        case Kind::Folder: col = IM_COL32(240, 200, 90, 255);  tag = "DIR"; break;
        case Kind::Image:  col = IM_COL32(90, 200, 160, 255);  tag = "IMG"; break;
        case Kind::Model:  col = IM_COL32(90, 150, 255, 255);  tag = "3D";  break;
        case Kind::Json:   col = IM_COL32(255, 170, 70, 255);   tag = "{ }"; break;
        case Kind::Audio:  col = IM_COL32(200, 120, 230, 255);  tag = "SND"; break;
        case Kind::Font:   col = IM_COL32(230, 120, 170, 255);  tag = "F";   break;
        case Kind::Text:   col = IM_COL32(180, 185, 195, 255);  tag = "TXT"; break;
        default:           col = IM_COL32(150, 150, 155, 255);  tag = "?";   break;
        }
    }

    // フォルダ型アイコン（タブ付き黄色フォルダ）
    void DrawFolderIcon(ImDrawList* dl, const ImVec2& a, const ImVec2& b) {
        float w = b.x - a.x, h = b.y - a.y;
        ImU32 body = IM_COL32(245, 205, 95, 255);
        ImU32 dark = IM_COL32(222, 178, 68, 255);
        float mx = w * 0.14f;
        float top = a.y + h * 0.32f;
        float bot = b.y - h * 0.16f;
        dl->AddRectFilled(ImVec2(a.x + mx, top - h * 0.13f), ImVec2(a.x + mx + w * 0.34f, top + h * 0.06f), dark, 3.0f);
        dl->AddRectFilled(ImVec2(a.x + mx, top), ImVec2(b.x - mx, bot), body, 4.0f);
    }

    // 書類型アイコン（角折れの紙＋種別カラーの帯＋拡張子ラベル）
    void DrawDocIcon(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 accent, const char* tag) {
        float w = b.x - a.x, h = b.y - a.y;
        float mx = w * 0.20f;
        ImVec2 p0(a.x + mx, a.y + h * 0.10f);
        ImVec2 p1(b.x - mx, b.y - h * 0.10f);
        float fold = w * 0.18f;
        dl->AddRectFilled(p0, p1, IM_COL32(238, 239, 243, 255), 3.0f);
        dl->AddTriangleFilled(ImVec2(p1.x - fold, p0.y), ImVec2(p1.x, p0.y + fold),
            ImVec2(p1.x - fold, p0.y + fold), IM_COL32(205, 207, 214, 255));
        dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 60), 3.0f);
        float barH = h * 0.27f;
        dl->AddRectFilled(ImVec2(p0.x, p1.y - barH), p1, accent, 3.0f);
        ImVec2 ts = ImGui::CalcTextSize(tag);
        dl->AddText(ImVec2(( p0.x + p1.x ) * 0.5f - ts.x * 0.5f, p1.y - barH * 0.5f - ts.y * 0.5f),
            IM_COL32(35, 35, 40, 255), tag);
    }

    // 幅に収まるよう名前を末尾カット（UTF-8 の途中で切らない）
    std::string TruncateToWidth(const std::string& s, float maxW) {
        if ( ImGui::CalcTextSize(s.c_str()).x <= maxW ) return s;
        std::string out = s;
        while ( !out.empty() ) {
            out.pop_back();
            while ( !out.empty() && ( static_cast<unsigned char>( out.back() ) & 0xC0 ) == 0x80 ) out.pop_back();
            if ( ImGui::CalcTextSize(( out + "..." ).c_str()).x <= maxW ) break;
        }
        return out + "...";
    }

    // std::string を InputTextMultiline で可変長編集するためのリサイズコールバック
    int TextResizeCallback(ImGuiInputTextCallbackData* data) {
        if ( data->EventFlag == ImGuiInputTextFlags_CallbackResize ) {
            std::string* s = static_cast<std::string*>( data->UserData );
            s->resize(data->BufTextLen);
            data->Buf = s->data();
        }
        return 0;
    }
#endif
}

// フレーム先頭（コマンドリストが閉じていて安全）で、積まれた画像を読み込む。
// 専用のコマンド記録（BeginCommandRecording / EndCommandRecording）で転送するので
// 描画フレームを壊さない。1フレームあたりの枚数を絞ってカクつきを防ぐ。
void FileEditor::ProcessPendingThumbnails() {
    if ( pending_.empty() && pendingModels_.empty() ) return;

    auto dx = DirectXCommon::GetInstance();
    dx->BeginCommandRecording();   // コマンドリストを開く

    // --- 画像サムネイル（テクスチャ転送） ---
    int loaded = 0;
    for ( const std::string& path : pending_ ) {
        if ( loaded >= 8 ) break;                       // 1フレーム最大8枚
        if ( thumbCache_.count(path) ) continue;        // 既に読み込み済み
        if ( thumbCache_.size() >= 256 ) { thumbCache_[path] = kNoThumb; continue; } // SRV使いすぎ防止
        TextureData td = TextureManager::GetInstance()->Load(path);
        thumbCache_[path] = td.srvIndex;
        ++loaded;
    }

    // --- モデルの3Dサムネイル（RTへ描画） ---
    RenderModelThumbnails();

    dx->EndCommandRecording();     // 閉じて実行＋GPU完了待ち（＝転送/描画確定）

    // GPU が終わったので一時Obj3dを解放してよい
    thumbTempObjs_.clear();
    pending_.clear();              // 積みは毎フレーム DrawDebugUI で作り直す
}

FileEditor::~FileEditor() = default;

void FileEditor::Initialize() {
    currentDir_ = rootPath_;
    ScanDir();
}

// モデルサムネ用の深度バッファ・DSV・カメラを用意（初回のみ）
void FileEditor::SetupThumbResources() {
    if ( thumbSetupDone_ ) return;
    auto dx = DirectXCommon::GetInstance();

    // 128x128 の深度バッファ（RTと同サイズにして寸法不一致を避ける）
    thumbDepth_ = TextureManager::GetInstance()->CreateDepthStencilTextureResource(kThumbSize, kThumbSize);

    // 専用の DSV ヒープ（1個）
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    dx->GetDevice()->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&thumbDsvHeap_));
    thumbDsvHandle_ = thumbDsvHeap_->GetCPUDescriptorHandleForHeapStart();

    D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc{};
    viewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dx->GetDevice()->CreateDepthStencilView(thumbDepth_.Get(), &viewDesc, thumbDsvHandle_);

    // サムネ用カメラ（正面・原点を見る）
    thumbCamera_ = Camera::Create();
    thumbCamera_->SetAspectRatio(1.0f);
    thumbCamera_->SetTranslation({ 0.0f, 0.0f, -2.8f });
    thumbCamera_->SetRotation({ 0.0f, 0.0f, 0.0f });
    thumbCamera_->Update();

    // Object3D は MRT（色＋マスク）なので、2枚目に捨てマスクRTが要る（全モデルで共有）
    maskRT_ = std::make_unique<RenderTexture>();
    maskRT_->Initialize(dx, SrvManager::GetInstance(), kThumbSize, kThumbSize);

    thumbSetupDone_ = true;
}

// 積まれたモデルを小さなRTに3D描画する（ProcessPendingThumbnails のコマンド記録中に呼ぶ）
void FileEditor::RenderModelThumbnails() {
    if ( pendingModels_.empty() ) return;
    SetupThumbResources();

    auto dx = DirectXCommon::GetInstance();
    auto cmdList = dx->GetCommandList();

    // モデル描画はSRVテーブルを使うのでヒープをセットしておく
    SrvManager::GetInstance()->PreDraw();

    // 共有マスクRTを一度だけ RENDER_TARGET 状態にしておく（以後サンプルしないので戻さない）
    if ( !maskTransitioned_ ) {
        D3D12_RESOURCE_BARRIER mb{};
        mb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        mb.Transition.pResource = maskRT_->GetResource().Get();
        mb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        mb.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        mb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmdList->ResourceBarrier(1, &mb);
        maskTransitioned_ = true;
    }

    int done = 0;
    for ( const std::string& path : pendingModels_ ) {
        if ( done >= 1 ) break;                       // 1フレーム1体（重いので）
        if ( modelThumbs_.count(path) ) continue;     // 既に処理済み
        if ( modelThumbs_.size() >= kMaxModelThumbs ) break;

        namespace fs = std::filesystem;
        fs::path p(path);
        std::string name = p.stem().string();
        std::string dir  = p.parent_path().generic_string();
        std::string file = p.filename().string();

        if ( !ModelManager::GetInstance()->FindModel(name) ) {
            ModelManager::GetInstance()->LoadModel(name, dir, file);
        }
        Model* model = ModelManager::GetInstance()->FindModel(name);
        if ( model == nullptr ) { modelThumbs_[path] = nullptr; continue; } // 読めなかった

        // RT を用意
        auto rt = std::make_unique<RenderTexture>();
        rt->SetClearColor(0.16f, 0.16f, 0.19f, 1.0f);
        rt->Initialize(dx, SrvManager::GetInstance(), kThumbSize, kThumbSize);

        // Obj3d を用意（3/4 ビューになるようモデル自体を回す）
        auto obj = std::make_unique<Obj3d>();
        obj->Initialize(model);
        obj->SetCamera(thumbCamera_.get());
        obj->SetTranslation({ 0.0f, 0.0f, 0.0f });
        obj->SetRotation({ 0.5f, 0.7f, 0.0f });
        obj->SetScale({ 1.0f, 1.0f, 1.0f });
        obj->Update();

        // 描画先を RT へ（色RTのバリア＋クリアは PreDrawScene を利用）
        rt->PreDrawScene(cmdList, dx);
        // MRT（色＋マスク）＋自前DSV＋128x128ビューポートに上書き
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { rt->GetRtvHandle(), maskRT_->GetRtvHandle() };
        cmdList->OMSetRenderTargets(2, rtvs, FALSE, &thumbDsvHandle_);
        const float maskClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmdList->ClearRenderTargetView(rtvs[1], maskClear, 0, nullptr);
        cmdList->ClearDepthStencilView(thumbDsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        D3D12_VIEWPORT vp = { 0.0f, 0.0f, ( float ) kThumbSize, ( float ) kThumbSize, 0.0f, 1.0f };
        D3D12_RECT sc = { 0, 0, kThumbSize, kThumbSize };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sc);

        obj->Draw();

        rt->PostDrawScene(cmdList, dx); // RT を読み取り用に戻す

        modelThumbs_[path] = std::move(rt);
        thumbTempObjs_.push_back(std::move(obj)); // GPU完了までObj3dを生かす
        ++done;
    }
    pendingModels_.clear();
}

// currentDir_ の中身を列挙（フォルダを先、次にファイル。名前順）
void FileEditor::ScanDir() {
    entries_.clear();
    namespace fs = std::filesystem;
    std::error_code ec;
    if ( currentDir_.empty() ) currentDir_ = rootPath_;
    if ( !fs::exists(currentDir_, ec) ) { status_ = "フォルダが見つかりません: " + currentDir_; return; }

    for ( const auto& e : fs::directory_iterator(currentDir_, ec) ) {
        Entry en;
        en.name = e.path().filename().string();
        en.fullPath = e.path().generic_string();
        en.isDir = e.is_directory(ec);
        entries_.push_back(en);
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if ( a.isDir != b.isDir ) return a.isDir > b.isDir; // フォルダを上に
        return a.name < b.name;
    });
}

// テキストを読み込む（バイナリ/巨大ファイルは編集不可にする）
void FileEditor::OpenFile(const std::string& path) {
    namespace fs = std::filesystem;
    openedFile_ = path;
    dirty_ = false;
    isBinaryOpened_ = false;
    textBuffer_.clear();

    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if ( ec ) { isBinaryOpened_ = true; status_ = "開けません: " + path; return; }
    if ( sz > 1024u * 1024u ) { isBinaryOpened_ = true; status_ = "1MB超のため表示しません"; return; }

    std::ifstream f(path, std::ios::binary);
    if ( !f ) { isBinaryOpened_ = true; status_ = "開けません: " + path; return; }

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // NUL を含む＝バイナリとみなして編集させない
    if ( content.find('\0') != std::string::npos ) {
        isBinaryOpened_ = true;
        status_ = "バイナリ形式のため編集できません";
        return;
    }
    textBuffer_ = std::move(content);
    textBuffer_.reserve(textBuffer_.size() + 256);
    status_ = "読み込みました: " + path;
}

// 編集内容を実ファイルへ書き戻す（＝同期）
void FileEditor::SaveFile() {
    if ( openedFile_.empty() || isBinaryOpened_ ) return;
    std::ofstream f(openedFile_, std::ios::binary);
    if ( !f ) { status_ = "保存に失敗しました"; return; }
    f << textBuffer_;
    dirty_ = false;
    status_ = "保存しました: " + openedFile_;
}

void FileEditor::CreateNewFile(const std::string& name) {
    if ( name.empty() ) { status_ = "名前を入力してください"; return; }
    namespace fs = std::filesystem;
    std::string path = currentDir_ + "/" + name;
    if ( fs::exists(path) ) { status_ = "既に存在します: " + name; return; }

    std::ofstream f(path, std::ios::binary);
    if ( !f ) { status_ = "作成に失敗しました"; return; }
    // .md は見出しテンプレを入れておく
    if ( name.size() >= 3 && name.substr(name.size() - 3) == ".md" ) {
        f << "# " << name.substr(0, name.size() - 3) << "\n\n";
    }
    f.close();
    ScanDir();
    OpenFile(path);
    status_ = "作成しました: " + name;
}

void FileEditor::CreateFolder(const std::string& name) {
    if ( name.empty() ) { status_ = "名前を入力してください"; return; }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directory(currentDir_ + "/" + name, ec);
    ScanDir();
    status_ = ec ? "フォルダ作成に失敗しました" : ("フォルダを作成しました: " + name);
}

void FileEditor::DeleteOpened() {
    if ( openedFile_.empty() ) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove(openedFile_, ec);
    status_ = ec ? "削除に失敗しました" : ("削除しました: " + openedFile_);
    openedFile_.clear();
    textBuffer_.clear();
    dirty_ = false;
    ScanDir();
}

void FileEditor::DrawDebugUI() {
#ifdef USE_IMGUI
    ImGui::Begin("ファイルエディタ (Project)");

    // --- ルートフォルダ ---
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##root", rootBuf_, sizeof(rootBuf_));
    ImGui::SameLine();
    if ( ImGui::Button("ルート移動") ) {
        rootPath_ = rootBuf_;
        currentDir_ = rootPath_;
        openedFile_.clear();
        textBuffer_.clear();
        ScanDir();
    }
    ImGui::SameLine();
    if ( ImGui::Button("再スキャン") ) { ScanDir(); }

    ImGui::Text("現在: %s  (%d 個)", currentDir_.c_str(), ( int ) entries_.size());

    // --- 新規作成 ---
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##newname", newNameBuf_, sizeof(newNameBuf_));
    ImGui::SameLine();
    if ( ImGui::Button("ファイル作成") ) { CreateNewFile(newNameBuf_); }
    ImGui::SameLine();
    if ( ImGui::Button("フォルダ作成") ) { CreateFolder(newNameBuf_); }

    ImGui::Separator();

    // --- 左：ファイル一覧（アイコン／サムネイルのグリッド）/ 右：エディタ ---
    ImGui::BeginChild("filelist", ImVec2(380.0f, 0.0f), true);
    namespace fs = std::filesystem;

    // 上のフォルダへ
    if ( currentDir_ != rootPath_ ) {
        if ( ImGui::Button(".. 上のフォルダへ", ImVec2(-FLT_MIN, 0.0f)) ) {
            currentDir_ = fs::path(currentDir_).parent_path().generic_string();
            if ( currentDir_.empty() ) currentDir_ = rootPath_;
            openedFile_.clear();
            ScanDir();
        }
    }

    // クリック結果は走査を壊さないようループ後に適用する
    std::string enterDir;
    std::string openPath;

    const float thumb = 64.0f;
    const float cell  = 84.0f;
    const float pad   = 6.0f;
    const float tileH = thumb + ImGui::GetTextLineHeight() + 8.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    int cols = ( int ) ( availW / ( cell + pad ) );
    if ( cols < 1 ) cols = 1;

    pending_.clear(); // このフレームに見えている未ロード画像を集め直す
    int col = 0;

    for ( size_t i = 0; i < entries_.size(); ++i ) {
        const Entry& e = entries_[i];
        ImGui::PushID(( int ) i);
        ImGui::BeginGroup();

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("tile", ImVec2(cell, tileH));
        bool hovered = ImGui::IsItemHovered();
        bool selected = ( e.fullPath == openedFile_ );

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 tileEnd(p0.x + cell, p0.y + tileH);
        // カード風の下地（常時薄く、ホバー/選択で強調）
        ImU32 bg = IM_COL32(255, 255, 255, 10);
        if ( selected )     bg = IM_COL32(80, 130, 255, 70);
        else if ( hovered ) bg = IM_COL32(255, 255, 255, 28);
        dl->AddRectFilled(p0, tileEnd, bg, 5.0f);

        // サムネイル / アイコン領域
        float tx = p0.x + ( cell - thumb ) * 0.5f;
        ImVec2 a(tx, p0.y + 2.0f), b(tx + thumb, p0.y + 2.0f + thumb);

        Kind kind = Classify(e.name, e.isDir);
        // 画像はサムネイル表示。未ロードなら次フレーム読み込み用に積む
        uint32_t srv = kNoThumb;
        if ( kind == Kind::Image && IsThumbable(e.name) ) {
            auto it = thumbCache_.find(e.fullPath);
            if ( it != thumbCache_.end() ) srv = it->second;
            else                           pending_.push_back(e.fullPath);
        }
        // モデルは3Dサムネイル（RTのSRV）。未描画なら積む
        else if ( kind == Kind::Model ) {
            auto it = modelThumbs_.find(e.fullPath);
            if ( it != modelThumbs_.end() ) { if ( it->second ) srv = it->second->GetSrvIndex(); }
            else                            pendingModels_.push_back(e.fullPath);
        }

        if ( srv != kNoThumb ) {
            // 実画像サムネイル（市松の下地＋枠でアルファも見やすく）
            dl->AddRectFilled(a, b, IM_COL32(60, 60, 66, 255), 3.0f);
            D3D12_GPU_DESCRIPTOR_HANDLE h = SrvManager::GetInstance()->GetGPUDescriptorHandle(srv);
            dl->AddImage(( ImTextureID ) ( uintptr_t ) h.ptr, a, b);
            dl->AddRect(a, b, IM_COL32(0, 0, 0, 150), 3.0f, 0, 1.5f);
        } else if ( kind == Kind::Folder ) {
            DrawFolderIcon(dl, a, b);
        } else {
            // 書類型アイコン（種別カラーの帯＋拡張子ラベル）
            ImU32 col32; const char* tag;
            KindStyle(kind, col32, tag);
            DrawDocIcon(dl, a, b, col32, tag);
        }

        // 名前（中央寄せ・幅に合わせて末尾カット）
        std::string disp = TruncateToWidth(e.name, cell - 4.0f);
        ImVec2 ns = ImGui::CalcTextSize(disp.c_str());
        dl->AddText(ImVec2(p0.x + ( cell - ns.x ) * 0.5f, b.y + 3.0f),
            IM_COL32(230, 230, 235, 255), disp.c_str());

        ImGui::EndGroup();
        if ( hovered ) ImGui::SetTooltip("%s", e.name.c_str());
        if ( clicked ) {
            if ( e.isDir ) enterDir = e.fullPath;
            else           openPath = e.fullPath;
        }

        ImGui::PopID();
        if ( ++col < cols ) ImGui::SameLine();
        else col = 0;
    }

    if ( entries_.empty() ) ImGui::TextDisabled("(空のフォルダ)");
    ImGui::EndChild();

    // ループ後に適用（フォルダ移動は entries_ を作り直すため）
    if ( !enterDir.empty() ) {
        currentDir_ = enterDir;
        openedFile_.clear();
        ScanDir();
    } else if ( !openPath.empty() ) {
        OpenFile(openPath);
    }

    ImGui::SameLine();

    ImGui::BeginChild("editor", ImVec2(0.0f, 0.0f), true);
    if ( !openedFile_.empty() ) {
        ImGui::Text("編集中: %s", openedFile_.c_str());
        ImGui::SameLine();
        if ( dirty_ ) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[未保存]");
        else          ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "[保存済]");

        if ( ImGui::Button("保存") ) { SaveFile(); }
        ImGui::SameLine();
        if ( ImGui::Button("再読込") ) { OpenFile(openedFile_); }
        ImGui::SameLine();
        if ( ImGui::Button("削除") ) { ImGui::OpenPopup("delete_confirm"); }
        // Ctrl+S 保存
        if ( ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) ) { SaveFile(); }

        if ( ImGui::BeginPopup("delete_confirm") ) {
            ImGui::Text("本当に削除しますか？");
            if ( ImGui::Button("削除する") ) { DeleteOpened(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if ( ImGui::Button("やめる") ) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        if ( isBinaryOpened_ ) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                "バイナリ/未対応形式のため編集できません（画像・モデル等）");
        } else {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if ( ImGui::InputTextMultiline("##edit", textBuffer_.data(),
                    textBuffer_.capacity() + 1, avail,
                    ImGuiInputTextFlags_CallbackResize, TextResizeCallback, &textBuffer_) ) {
                dirty_ = true;
            }
        }
    } else {
        ImGui::TextDisabled("左のリストからテキストファイルを選ぶと編集できます");
    }
    ImGui::EndChild();

    if ( !status_.empty() ) {
        ImGui::TextDisabled("%s", status_.c_str());
    }

    ImGui::End();
#endif
}
