#include "SDFManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif
#include "externals/nlohmann/json.hpp"
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/RootSignatureBuilder.h"
#include "engine/graphics/GraphicsPipelineBuilder.h"
#include "engine/graphics/RenderTexture.h"

using json = nlohmann::json;

void SDFManager::Initialize() {
    BuildPipelines();
    LoadScene(); // 前回配置したテキスト/スプライトを復元（アトラスは後から解決）
}

// 共有ルートシグネチャ＋PSO（b0:VS変換 / b1:PSパラメータ / t0:アトラス / s0:サンプラー）
void SDFManager::BuildPipelines() {
    auto dxCommon = DirectXCommon::GetInstance();
    auto device = dxCommon->GetDevice();

    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCBV(0, D3D12_SHADER_VISIBILITY_VERTEX);               // [0] b0
    rsBuilder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);                // [1] b1
    rsBuilder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [2] t0
    rsBuilder.AddDefaultSampler(0);                                    //     s0
    rsBuilder.Build(device, rootSignature_);

    static D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto& compiler = dxCommon->GetShaderCompiler();
    auto vsBlob = compiler.CompileShader(L"resources/shaders/SDF/SDFText.VS.hlsl", L"vs_6_0");
    auto psText = compiler.CompileShader(L"resources/shaders/SDF/SDFText.PS.hlsl", L"ps_6_0");
    auto psSprite = compiler.CompileShader(L"resources/shaders/SDF/SDFSprite.PS.hlsl", L"ps_6_0");

    // バックバッファ（sRGB）へ FinalBlit 後に直接描くので、フォーマットを合わせる
    GraphicsPipelineBuilder textBuilder;
    textBuilder
        .SetRootSignature(rootSignature_.Get())
        .SetShaders(vsBlob.Get(), psText.Get())
        .SetInputLayout(inputLayout, _countof(inputLayout))
        .SetBlendMode(BlendMode::kNormal)
        .SetCullMode(D3D12_CULL_MODE_NONE)
        .SetDepthStencil(false, false)
        .SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB });
    textBuilder.Build(device, textPipeline_);

    GraphicsPipelineBuilder spriteBuilder;
    spriteBuilder
        .SetRootSignature(rootSignature_.Get())
        .SetShaders(vsBlob.Get(), psSprite.Get())
        .SetInputLayout(inputLayout, _countof(inputLayout))
        .SetBlendMode(BlendMode::kNormal)
        .SetCullMode(D3D12_CULL_MODE_NONE)
        .SetDepthStencil(false, false)
        .SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB });
    spriteBuilder.Build(device, spritePipeline_);

    pipelineReady_ = true;
}

void SDFManager::Update() {
    // 約1秒に1回だけフォルダを見る（毎フレームのファイルアクセスを避ける）
    if ( ++scanCounter_ < 60 ) return;
    scanCounter_ = 0;
    ScanAndLoad();
}

// resources/sdf/（2Dアトラス）と resources/sdf3d/（3Dボリューム）を走査し、
// 新規分のロードと更新分のホットリロードを行う。
// テクスチャ転送を伴うため、専用のコマンド記録（BeginCommandRecording）で安全に実行する。
void SDFManager::ScanAndLoad() {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1. 2Dアトラス：ロード/リロードすべき対象を先に集める
    struct PendingLoad { std::string jsonPath, pngPath; };
    std::vector<PendingLoad> newAtlases;
    std::vector<SDFAtlas*> reloadAtlases;

    if ( fs::exists(watchDir_, ec) ) {
        for ( const auto& entry : fs::directory_iterator(watchDir_, ec) ) {
            if ( !entry.is_regular_file() ) continue;
            if ( entry.path().extension() != ".json" ) continue;

            std::string jsonPath = entry.path().generic_string();
            std::string pngPath = fs::path(entry.path()).replace_extension(".png").generic_string();
            if ( !fs::exists(pngPath, ec) ) continue; // PNGが無いJSONはアトラスではない（sdf_scene.json等）

            // 既にロード済みか？
            bool known = false;
            for ( auto& atlas : atlases_ ) {
                if ( atlas->GetJsonPath() == jsonPath ) {
                    known = true;
                    if ( atlas->IsFileModified() ) { reloadAtlases.push_back(atlas.get()); }
                    break;
                }
            }
            if ( !known ) { newAtlases.push_back({ jsonPath, pngPath }); }
        }
    }

    // 2. 3Dボリューム：利用可能な .sdf3d 一覧を更新し、やるべき仕事を集める
    volumeFiles_.clear();
    if ( fs::exists(volumeDir_, ec) ) {
        for ( const auto& entry : fs::directory_iterator(volumeDir_, ec) ) {
            if ( !entry.is_regular_file() ) continue;
            if ( entry.path().extension() != ".sdf3d" ) continue;
            volumeFiles_.push_back(entry.path().stem().string());
        }
        std::sort(volumeFiles_.begin(), volumeFiles_.end());
    }

    auto hasVolumeFile = [&](const std::string& name) {
        return std::find(volumeFiles_.begin(), volumeFiles_.end(), name) != volumeFiles_.end();
    };

    // 配置アイテムのうち「ファイルが届いて解決できるもの」「モーフ先が変わったもの」
    std::vector<VolumeItem*> resolveItems;
    for ( auto& v : volumes_ ) {
        if ( !v.vol && hasVolumeFile(v.fileName) ) { resolveItems.push_back(&v); }
        else if ( v.vol && v.morphFile != v.loadedMorph ) { resolveItems.push_back(&v); }
    }
    // 生存している全ボリューム（ゲームコード所有の卵なども含む）のファイル更新チェック
    std::vector<SDFVolumeObject*> reloadVolumes;
    for ( SDFVolumeObject* v : SDFVolumeObject::GetInstances() ) {
        if ( v->IsFileModified() ) { reloadVolumes.push_back(v); }
    }

    if ( newAtlases.empty() && reloadAtlases.empty()
        && resolveItems.empty() && reloadVolumes.empty() ) return;

    // 3. 安全なコマンド記録の中でまとめてロード（テクスチャ転送があるため）
    auto dx = DirectXCommon::GetInstance();
    dx->BeginCommandRecording();
    auto cmdList = dx->GetCommandList();

    for ( const auto& p : newAtlases ) {
        auto atlas = std::make_unique<SDFAtlas>();
        if ( atlas->Load(p.jsonPath, p.pngPath, cmdList) ) {
            status_ = "読み込み: " + atlas->GetName()
                + ( atlas->IsFont() ? " (フォント)" : " (画像)" );
            atlases_.push_back(std::move(atlas));
        }
    }
    for ( SDFAtlas* atlas : reloadAtlases ) {
        if ( atlas->Reload(cmdList) ) {
            status_ = "ホットリロード: " + atlas->GetName();
            // メトリクスが変わった可能性があるので、このアトラスを使うアイテムを再構築
            for ( auto& t : texts_ ) {
                if ( t.atlasName == atlas->GetName() ) { t.text->MarkDirty(); }
            }
            for ( auto& s : sprites_ ) {
                if ( s.atlasName == atlas->GetName() ) { s.sprite->MarkDirty(); }
            }
        }
    }
    for ( VolumeItem* v : resolveItems ) { ResolveVolumeItem(*v, cmdList); }
    for ( SDFVolumeObject* v : reloadVolumes ) {
        if ( v->Reload(cmdList) ) { status_ = "3Dホットリロード"; }
    }

    dx->EndCommandRecording(); // 実行＋GPU完了待ち（＝転送確定）

    // アトラス名順に整列（UIの並びを安定させる）
    std::sort(atlases_.begin(), atlases_.end(),
        [](const std::unique_ptr<SDFAtlas>& a, const std::unique_ptr<SDFAtlas>& b) {
            return a->GetName() < b->GetName();
        });
}

// ボリューム配置アイテムの実体化（新規ロード）とモーフ先の反映。
// コマンド記録中に呼ぶこと（ScanAndLoad の Begin/End ブロック内）
void SDFManager::ResolveVolumeItem(VolumeItem& item, ID3D12GraphicsCommandList* commandList) {
    const std::string path = volumeDir_ + "/" + item.fileName + ".sdf3d";
    if ( !item.vol ) {
        auto vol = std::make_unique<SDFVolumeObject>();
        if ( !vol->Load(path, commandList) ) {
            status_ = "3D読み込み失敗: " + item.fileName;
            return; // 生成途中の可能性があるので次のスキャンで再試行
        }
        item.vol = std::move(vol);
        status_ = "3D読み込み: " + item.fileName;
    }
    // モーフ先の反映（選び直し・解除も含む。ファイル未着なら次のスキャンで再試行）
    if ( item.morphFile != item.loadedMorph ) {
        if ( item.morphFile.empty() ) {
            item.vol->ClearMorphTarget();
            item.loadedMorph.clear();
        } else {
            const std::string morphPath = volumeDir_ + "/" + item.morphFile + ".sdf3d";
            std::error_code ec;
            if ( std::filesystem::exists(morphPath, ec)
                && item.vol->LoadMorphTarget(morphPath, commandList) ) {
                item.loadedMorph = item.morphFile;
            }
        }
    }
}

SDFAtlas* SDFManager::FindAtlas(const std::string& name) const {
    for ( auto& atlas : atlases_ ) {
        if ( atlas->GetName() == name ) return atlas.get();
    }
    return nullptr;
}

void SDFManager::Draw(ID3D12GraphicsCommandList* commandList) {
    if ( !pipelineReady_ ) return;
    DrawItems(commandList);
}

// シーン専用テキストを1個描く（タイトル・HUD等。現在のレンダーターゲットへ）
void SDFManager::DrawTextItem(ID3D12GraphicsCommandList* commandList, SDFText& text, const std::string& atlasName) {
    if ( !pipelineReady_ ) return;
    SDFAtlas* atlas = FindAtlas(atlasName);
    if ( !atlas || !atlas->IsFont() ) return;
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(textPipeline_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    text.Update(*atlas);
    text.Draw(commandList, *atlas);
}

// シーン専用スプライトを1個描く（タイトルロゴ等。現在のレンダーターゲットへ）
void SDFManager::DrawSpriteItem(ID3D12GraphicsCommandList* commandList, SDFSprite& sprite, const std::string& atlasName) {
    if ( !pipelineReady_ ) return;
    SDFAtlas* atlas = FindAtlas(atlasName);
    if ( !atlas || atlas->IsFont() ) return;
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(spritePipeline_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    sprite.Update(*atlas);
    sprite.Draw(commandList, *atlas);
}

// レンダーテクスチャへ焼き込む（Game View にも映るように最終画像へ合成する）
void SDFManager::DrawIntoTexture(ID3D12GraphicsCommandList* commandList, RenderTexture* target) {
    if ( !pipelineReady_ || !target ) return;
    if ( texts_.empty() && sprites_.empty() ) return;

    auto dxCommon = DirectXCommon::GetInstance();

    // 読み取り用 → 描画用 に遷移
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target->GetResource().Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);

    // 描画先を設定（クリアはしない＝既存の絵の上に重ねる）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = target->GetRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f,
        ( float ) dxCommon->GetClientWidth(), ( float ) dxCommon->GetClientHeight(), 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, ( LONG ) dxCommon->GetClientWidth(), ( LONG ) dxCommon->GetClientHeight() };
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &sc);

    DrawItems(commandList);

    // 描画用 → 読み取り用 に戻す（この後 FinalBlit / Game View がサンプルする）
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void SDFManager::DrawItems(ID3D12GraphicsCommandList* commandList) {
    // 近接表示のフェード係数：ビュワー（プレイヤー）との距離から求める。
    //   表示距離の外側25%（最低0.5m）でなめらかにフェードイン/アウトする。
    //   2Dアイテムや、ビュワー未設定（Edit中など）の時は常に全表示。
    auto proximityAlpha = [&](bool is3D, bool enabled, float radius, const Vector3& wp) -> float {
        if ( !enabled || !is3D || !viewerValid_ ) return 1.0f;
        float dx = wp.x - viewerPos_.x;
        float dy = wp.y - viewerPos_.y;
        float dz = wp.z - viewerPos_.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        float fade = ( std::max )( radius * 0.25f, 0.5f );
        return std::clamp(( radius - dist ) / fade, 0.0f, 1.0f);
    };

    // --- テキスト ---
    bool textBound = false;
    for ( auto& t : texts_ ) {
        SDFAtlas* atlas = FindAtlas(t.atlasName);
        if ( !atlas || !atlas->IsFont() ) continue;
        float alpha = proximityAlpha(t.text->Is3D(), t.text->IsProximityEnabled(),
                                     t.text->GetProximityRadius(), t.text->RefWorldPos());
        if ( alpha <= 0.0f ) continue; // 完全に見えないアイテムは描かない
        t.text->SetDrawAlpha(alpha);
        if ( !textBound ) {
            commandList->SetGraphicsRootSignature(rootSignature_.Get());
            commandList->SetPipelineState(textPipeline_.Get());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            textBound = true;
        }
        t.text->Update(*atlas);
        t.text->Draw(commandList, *atlas);
    }

    // --- スプライト ---
    bool spriteBound = false;
    for ( auto& s : sprites_ ) {
        SDFAtlas* atlas = FindAtlas(s.atlasName);
        if ( !atlas || atlas->IsFont() ) continue;
        float alpha = proximityAlpha(s.sprite->Is3D(), s.sprite->IsProximityEnabled(),
                                     s.sprite->GetProximityRadius(), s.sprite->RefWorldPos());
        if ( alpha <= 0.0f ) continue;
        s.sprite->SetDrawAlpha(alpha);
        if ( !spriteBound ) {
            commandList->SetGraphicsRootSignature(rootSignature_.Get());
            commandList->SetPipelineState(spritePipeline_.Get());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            spriteBound = true;
        }
        s.sprite->Update(*atlas);
        s.sprite->Draw(commandList, *atlas);
    }
}

// エディタで配置した3Dボリュームの描画。
// SDFVolumeObject は深度あり・シーンMRT用のPSOを自分で張るので、MRTパスの最後で呼ぶこと
void SDFManager::DrawVolumes(ID3D12GraphicsCommandList* commandList) {
    for ( auto& v : volumes_ ) {
        if ( !v.visible || !v.vol || !v.vol->IsLoaded() ) continue;
        // パラメータは VolumeItem が正。毎フレーム流し込んでから CB を更新する
        v.vol->SetTranslation(v.translation);
        v.vol->SetScale(v.scale);
        v.vol->SetColor(v.color);
        v.vol->SetErode(v.erode);
        v.vol->SetMorphT(v.morphT);
        v.vol->Update();
        v.vol->Draw(commandList);
    }
}

void SDFManager::Finalize() {
    volumes_.clear();
    texts_.clear();
    sprites_.clear();
    atlases_.clear();
    textPipeline_.Reset();
    spritePipeline_.Reset();
    rootSignature_.Reset();
    pipelineReady_ = false;
}

// ============================================================
// シーン（配置内容）の保存 / 復元
// ============================================================
void SDFManager::SaveScene() const {
    json j;
    j["texts"] = json::array();
    for ( const auto& t : texts_ ) {
        const SDFText& x = *t.text;
        Vector4 c = const_cast<SDFText&>( x ).RefColor();
        Vector4 oc = const_cast<SDFText&>( x ).RefOutlineColor();
        Vector3 wp = const_cast<SDFText&>( x ).RefWorldPos();
        j["texts"].push_back({
            { "atlas", t.atlasName },
            { "text", x.GetText() },
            { "x", x.GetX() }, { "y", x.GetY() },
            { "size", x.GetFontSize() },
            { "color", { c.x, c.y, c.z, c.w } },
            { "outlineWidth", const_cast<SDFText&>( x ).RefOutlineWidth() },
            { "outlineColor", { oc.x, oc.y, oc.z, oc.w } },
            { "thickness", const_cast<SDFText&>( x ).RefThickness() },
            { "use3D", x.Is3D() },
            { "worldPos", { wp.x, wp.y, wp.z } },
            { "worldScale", const_cast<SDFText&>( x ).RefWorldScale() },
            { "proximity", x.IsProximityEnabled() },
            { "proximityRadius", x.GetProximityRadius() },
        });
    }
    j["sprites"] = json::array();
    for ( const auto& s : sprites_ ) {
        SDFSprite& x = *s.sprite;
        Vector4 c = x.RefColor();
        Vector4 oc = x.RefOutlineColor();
        Vector4 gc = x.RefGlowColor();
        Vector3 wp = x.RefWorldPos();
        j["sprites"].push_back({
            { "atlas", s.atlasName },
            { "sprite", x.GetSpriteName() },
            { "x", x.GetX() }, { "y", x.GetY() },
            { "scale", x.RefScale() },
            { "color", { c.x, c.y, c.z, c.w } },
            { "outlineWidth", x.RefOutlineWidth() },
            { "outlineColor", { oc.x, oc.y, oc.z, oc.w } },
            { "glowWidth", x.RefGlowWidth() },
            { "glowColor", { gc.x, gc.y, gc.z, gc.w } },
            { "use3D", x.Is3D() },
            { "worldPos", { wp.x, wp.y, wp.z } },
            { "worldScale", x.RefWorldScale() },
            { "edgeBias", x.RefEdgeBias() },
            { "proximity", x.IsProximityEnabled() },
            { "proximityRadius", x.GetProximityRadius() },
        });
    }

    j["volumes"] = json::array();
    for ( const auto& v : volumes_ ) {
        j["volumes"].push_back({
            { "file", v.fileName },
            { "morphFile", v.morphFile },
            { "translation", { v.translation.x, v.translation.y, v.translation.z } },
            { "scale", v.scale },
            { "color", { v.color.x, v.color.y, v.color.z, v.color.w } },
            { "erode", v.erode },
            { "morphT", v.morphT },
            { "visible", v.visible },
        });
    }

    std::filesystem::create_directories(watchDir_);
    std::ofstream f(scenePath_);
    if ( f ) { f << j.dump(4); }
}

void SDFManager::LoadScene() {
    std::ifstream f(scenePath_);
    if ( !f ) return;
    json j;
    try { f >> j; } catch ( ... ) { return; }

    texts_.clear();
    sprites_.clear();
    volumes_.clear();

    for ( auto& jt : j.value("texts", json::array()) ) {
        TextItem item;
        item.atlasName = jt.value("atlas", "");
        item.text = std::make_unique<SDFText>();
        item.text->Initialize();
        item.text->SetText(jt.value("text", std::string("SDF Text")));
        item.text->SetPosition(jt.value("x", 100.0f), jt.value("y", 100.0f));
        item.text->SetFontSize(jt.value("size", 48.0f));
        if ( jt.contains("color") ) {
            item.text->SetColor({ jt["color"][0], jt["color"][1], jt["color"][2], jt["color"][3] });
        }
        item.text->SetOutlineWidth(jt.value("outlineWidth", 0.15f));
        if ( jt.contains("outlineColor") ) {
            item.text->SetOutlineColor({ jt["outlineColor"][0], jt["outlineColor"][1], jt["outlineColor"][2], jt["outlineColor"][3] });
        }
        item.text->SetThickness(jt.value("thickness", 0.0f));
        if ( jt.value("use3D", false) && jt.contains("worldPos") ) {
            item.text->SetTransform3D(
                { jt["worldPos"][0], jt["worldPos"][1], jt["worldPos"][2] },
                jt.value("worldScale", 1.0f));
        }
        item.text->SetProximity(jt.value("proximity", false), jt.value("proximityRadius", 8.0f));
        texts_.push_back(std::move(item));
    }
    for ( auto& js : j.value("sprites", json::array()) ) {
        SpriteItem item;
        item.atlasName = js.value("atlas", "");
        item.sprite = std::make_unique<SDFSprite>();
        item.sprite->Initialize();
        item.sprite->SetSpriteName(js.value("sprite", std::string()));
        item.sprite->SetPosition(js.value("x", 200.0f), js.value("y", 200.0f));
        item.sprite->SetScale(js.value("scale", 1.0f));
        if ( js.contains("color") ) {
            item.sprite->SetColor({ js["color"][0], js["color"][1], js["color"][2], js["color"][3] });
        }
        if ( js.contains("outlineColor") ) {
            item.sprite->SetOutline(js.value("outlineWidth", 0.0f),
                { js["outlineColor"][0], js["outlineColor"][1], js["outlineColor"][2], js["outlineColor"][3] });
        }
        if ( js.contains("glowColor") ) {
            item.sprite->SetGlow(js.value("glowWidth", 0.0f),
                { js["glowColor"][0], js["glowColor"][1], js["glowColor"][2], js["glowColor"][3] });
        }
        if ( js.value("use3D", false) && js.contains("worldPos") ) {
            item.sprite->SetTransform3D(
                { js["worldPos"][0], js["worldPos"][1], js["worldPos"][2] },
                js.value("worldScale", 5.0f));
        }
        item.sprite->RefEdgeBias() = js.value("edgeBias", 0.5f);
        item.sprite->SetProximity(js.value("proximity", false), js.value("proximityRadius", 8.0f));
        sprites_.push_back(std::move(item));
    }
    for ( auto& jv : j.value("volumes", json::array()) ) {
        VolumeItem item;
        item.fileName = jv.value("file", "");
        item.morphFile = jv.value("morphFile", "");
        if ( jv.contains("translation") ) {
            item.translation = { jv["translation"][0], jv["translation"][1], jv["translation"][2] };
        }
        item.scale = jv.value("scale", 1.0f);
        if ( jv.contains("color") ) {
            item.color = { jv["color"][0], jv["color"][1], jv["color"][2], jv["color"][3] };
        }
        item.erode = jv.value("erode", 0.0f);
        item.morphT = jv.value("morphT", 0.0f);
        item.visible = jv.value("visible", true);
        // vol はここでは作らない（ファイルが届いた時に ScanAndLoad が後から解決する）
        if ( !item.fileName.empty() ) { volumes_.push_back(std::move(item)); }
    }
    sceneLoaded_ = true;
}

// ============================================================
// エディタパネル
// ============================================================
void SDFManager::DrawDebugUI() {
#ifdef USE_IMGUI
    ImGui::Begin("SDF (フォント/画像)");

    // --- 状態 ---
    ImGui::TextDisabled("監視中: %s  (input/に素材を入れると自動で届きます)", watchDir_.c_str());
    if ( ImGui::Button("今すぐ再スキャン") ) { ScanAndLoad(); }
    ImGui::SameLine();
    if ( ImGui::Button("配置を保存") ) { SaveScene(); status_ = "配置を保存しました"; }
    ImGui::SameLine();
    ImGui::TextDisabled("アトラス %d 件", ( int ) atlases_.size());
    if ( !status_.empty() ) { ImGui::TextDisabled("%s", status_.c_str()); }
    ImGui::Separator();

    // --- アトラス一覧（プレビュー付き） ---
    if ( ImGui::CollapsingHeader("読み込み済みアトラス", ImGuiTreeNodeFlags_DefaultOpen) ) {
        if ( atlases_.empty() ) {
            ImGui::TextDisabled("（まだありません。SDFWatcher の input/ に\n"
                "  .ttf / .png を入れると自動で生成されて届きます）");
        }
        for ( auto& atlas : atlases_ ) {
            ImGui::PushID(atlas.get());
            std::string label = atlas->GetName()
                + ( atlas->IsFont() ? "  [フォント]" : "  [画像]" );
            if ( ImGui::TreeNode(label.c_str()) ) {
                if ( atlas->IsFont() ) {
                    ImGui::Text("文字数: %d / 基準サイズ: %.0fpx", atlas->GetGlyphCount(), atlas->GetBaseSize());
                } else {
                    ImGui::Text("画像数: %d / カラー保持: %s",
                        ( int ) atlas->GetSprites().size(), atlas->IsKeepColor() ? "あり" : "なし");
                }
                // アトラステクスチャのプレビュー
                float w = 192.0f;
                float h = w * ( atlas->GetAtlasHeight() / ( std::max )( atlas->GetAtlasWidth(), 1.0f ) );
                D3D12_GPU_DESCRIPTOR_HANDLE handle =
                    SrvManager::GetInstance()->GetGPUDescriptorHandle(atlas->GetSrvIndex());
                ImGui::Image(( ImTextureID ) ( uintptr_t ) handle.ptr, ImVec2(w, h));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    // --- テキスト配置 ---
    if ( ImGui::CollapsingHeader("テキスト配置", ImGuiTreeNodeFlags_DefaultOpen) ) {
        // フォントアトラスの一覧
        std::vector<SDFAtlas*> fonts;
        for ( auto& a : atlases_ ) { if ( a->IsFont() ) fonts.push_back(a.get()); }

        if ( fonts.empty() ) {
            ImGui::TextDisabled("フォントアトラスがありません（.ttf を input/ へ）");
        } else if ( ImGui::Button("＋ テキストを追加") ) {
            TextItem item;
            item.atlasName = fonts[0]->GetName();
            item.text = std::make_unique<SDFText>();
            item.text->Initialize();
            item.text->SetText("SDF Text");
            item.text->SetPosition(100.0f, 100.0f + 60.0f * ( float ) texts_.size());
            texts_.push_back(std::move(item));
        }

        int deleteIndex = -1;
        for ( int i = 0; i < ( int ) texts_.size(); ++i ) {
            ImGui::PushID(1000 + i);
            TextItem& item = texts_[i];
            std::string header = "文字 " + std::to_string(i) + " : " + item.text->GetText();
            if ( ImGui::TreeNode(header.c_str()) ) {
                // フォント選択
                if ( ImGui::BeginCombo("フォント", item.atlasName.c_str()) ) {
                    for ( SDFAtlas* f : fonts ) {
                        if ( ImGui::Selectable(f->GetName().c_str(), item.atlasName == f->GetName()) ) {
                            item.atlasName = f->GetName();
                            item.text->MarkDirty();
                        }
                    }
                    ImGui::EndCombo();
                }
                // 本文
                char buf[512];
                strcpy_s(buf, item.text->GetText().c_str());
                if ( ImGui::InputText("本文", buf, sizeof(buf)) ) { item.text->SetText(buf); }
                // 2D / 3D 切り替え（3D=ワールド空間に看板として置く）
                bool is3D = item.text->Is3D();
                if ( ImGui::Checkbox("3Dで表示（ワールドに看板として置く）", &is3D) ) {
                    if ( is3D ) { item.text->SetTransform3D(item.text->RefWorldPos(), item.text->RefWorldScale()); }
                    else        { item.text->SetScreenMode(); }
                }
                if ( is3D ) {
                    ImGui::DragFloat3("ワールド位置", &item.text->RefWorldPos().x, 0.1f);
                    ImGui::DragFloat("1行の高さ (m)", &item.text->RefWorldScale(), 0.05f, 0.05f, 20.0f);
                    // 近接表示：プレイ中、プレイヤーが近くに来た時だけフェードインする
                    ImGui::Checkbox("近づいた時だけ表示", &item.text->RefProximityEnabled());
                    if ( item.text->RefProximityEnabled() ) {
                        ImGui::DragFloat("表示距離 (m)", &item.text->RefProximityRadius(), 0.1f, 0.5f, 100.0f);
                    }
                } else {
                    float pos[2] = { item.text->GetX(), item.text->GetY() };
                    if ( ImGui::DragFloat2("位置", pos, 1.0f) ) { item.text->SetPosition(pos[0], pos[1]); }
                }
                float size = item.text->GetFontSize();
                if ( ImGui::DragFloat("サイズ", &size, 1.0f, 4.0f, 512.0f) ) { item.text->SetFontSize(size); }
                // 色・フチ・太さ
                ImGui::ColorEdit4("文字色", &item.text->RefColor().x);
                ImGui::SliderFloat("太さ (+太字/-細字)", &item.text->RefThickness(), -0.15f, 0.2f);
                ImGui::SliderFloat("フチ太さ", &item.text->RefOutlineWidth(), 0.0f, 0.4f);
                ImGui::ColorEdit4("フチ色", &item.text->RefOutlineColor().x);

                if ( ImGui::Button("削除") ) { deleteIndex = i; }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if ( deleteIndex >= 0 ) { texts_.erase(texts_.begin() + deleteIndex); }
    }

    // --- スプライト配置 ---
    if ( ImGui::CollapsingHeader("スプライト配置", ImGuiTreeNodeFlags_DefaultOpen) ) {
        std::vector<SDFAtlas*> spriteAtlases;
        for ( auto& a : atlases_ ) { if ( !a->IsFont() && a->GetType() != SDFAtlas::Type::Unknown ) spriteAtlases.push_back(a.get()); }

        if ( spriteAtlases.empty() ) {
            ImGui::TextDisabled("スプライトアトラスがありません（.png を input/ へ）");
        } else if ( ImGui::Button("＋ スプライトを追加") ) {
            SpriteItem item;
            item.atlasName = spriteAtlases[0]->GetName();
            item.sprite = std::make_unique<SDFSprite>();
            item.sprite->Initialize();
            if ( !spriteAtlases[0]->GetSprites().empty() ) {
                item.sprite->SetSpriteName(spriteAtlases[0]->GetSprites().begin()->first);
            }
            item.sprite->SetPosition(300.0f, 150.0f + 80.0f * ( float ) sprites_.size());
            sprites_.push_back(std::move(item));
        }

        int deleteIndex = -1;
        for ( int i = 0; i < ( int ) sprites_.size(); ++i ) {
            ImGui::PushID(2000 + i);
            SpriteItem& item = sprites_[i];
            std::string header = "画像 " + std::to_string(i) + " : "
                + item.atlasName + "/" + item.sprite->GetSpriteName();
            if ( ImGui::TreeNode(header.c_str()) ) {
                // アトラス選択
                if ( ImGui::BeginCombo("アトラス", item.atlasName.c_str()) ) {
                    for ( SDFAtlas* a : spriteAtlases ) {
                        if ( ImGui::Selectable(a->GetName().c_str(), item.atlasName == a->GetName()) ) {
                            item.atlasName = a->GetName();
                            // アトラスを変えたら最初のスプライトを選び直す
                            if ( !a->GetSprites().empty() ) {
                                item.sprite->SetSpriteName(a->GetSprites().begin()->first);
                            }
                            item.sprite->MarkDirty();
                        }
                    }
                    ImGui::EndCombo();
                }
                // スプライト選択（アトラス内）＋選択中画像のプレビュー
                if ( SDFAtlas* a = FindAtlas(item.atlasName) ) {
                    if ( ImGui::BeginCombo("画像", item.sprite->GetSpriteName().c_str()) ) {
                        for ( const auto& pair : a->GetSprites() ) {
                            if ( ImGui::Selectable(pair.first.c_str(),
                                item.sprite->GetSpriteName() == pair.first) ) {
                                item.sprite->SetSpriteName(pair.first);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    // プレビュー（アトラスから該当部分だけUVで切り出して表示）
                    const SDFSpriteInfo* info = a->GetSprite(item.sprite->GetSpriteName());
                    if ( !info && !a->GetSprites().empty() ) { info = &a->GetSprites().begin()->second; }
                    if ( info ) {
                        float pw = 96.0f;
                        float ph = ( info->width > 0.0f ) ? pw * ( info->height / info->width ) : pw;
                        D3D12_GPU_DESCRIPTOR_HANDLE h =
                            SrvManager::GetInstance()->GetGPUDescriptorHandle(a->GetSrvIndex());
                        ImGui::Image(( ImTextureID ) ( uintptr_t ) h.ptr, ImVec2(pw, ph),
                            ImVec2(info->u0, info->v0), ImVec2(info->u1, info->v1));
                        ImGui::SameLine();
                        ImGui::TextDisabled("%.0f x %.0f px\n(元 %.0f x %.0f)",
                            info->width, info->height, info->originalWidth, info->originalHeight);
                    }
                }
                // 2D / 3D 切り替え
                bool is3D = item.sprite->Is3D();
                if ( ImGui::Checkbox("3Dで表示（ワールドに板として置く）", &is3D) ) {
                    if ( is3D ) {
                        item.sprite->SetTransform3D(item.sprite->RefWorldPos(), item.sprite->RefWorldScale());
                    } else {
                        item.sprite->SetScreenMode();
                    }
                }
                if ( is3D ) {
                    if ( ImGui::DragFloat3("ワールド位置", &item.sprite->RefWorldPos().x, 0.1f) ) {
                        item.sprite->MarkDirty();
                    }
                    if ( ImGui::DragFloat("大きさ (高さ)", &item.sprite->RefWorldScale(), 0.1f, 0.1f, 100.0f) ) {
                        item.sprite->MarkDirty();
                    }
                    // 近接表示：プレイ中、プレイヤーが近くに来た時だけフェードインする
                    ImGui::Checkbox("近づいた時だけ表示", &item.sprite->RefProximityEnabled());
                    if ( item.sprite->RefProximityEnabled() ) {
                        ImGui::DragFloat("表示距離 (m)", &item.sprite->RefProximityRadius(), 0.1f, 0.5f, 100.0f);
                    }
                } else {
                    float pos[2] = { item.sprite->GetX(), item.sprite->GetY() };
                    if ( ImGui::DragFloat2("位置", pos, 1.0f) ) { item.sprite->SetPosition(pos[0], pos[1]); }
                    float scale = item.sprite->RefScale();
                    if ( ImGui::DragFloat("スケール", &scale, 0.01f, 0.05f, 20.0f) ) { item.sprite->SetScale(scale); }
                }
                // ティント・太さ・フチ・グロー
                ImGui::ColorEdit4("ティント", &item.sprite->RefColor().x);
                ImGui::SliderFloat("太さ調整 (小=太る/大=痩せる)", &item.sprite->RefEdgeBias(), 0.30f, 0.70f);
                ImGui::SliderFloat("フチ太さ", &item.sprite->RefOutlineWidth(), 0.0f, 0.4f);
                ImGui::ColorEdit4("フチ色", &item.sprite->RefOutlineColor().x);
                ImGui::SliderFloat("グロー幅", &item.sprite->RefGlowWidth(), 0.0f, 0.5f);
                ImGui::ColorEdit4("グロー色", &item.sprite->RefGlowColor().x);

                if ( ImGui::Button("削除") ) { deleteIndex = i; }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if ( deleteIndex >= 0 ) { sprites_.erase(sprites_.begin() + deleteIndex); }
    }

    ImGui::Separator();

    // --- 3Dボリューム配置 (.sdf3d) ---
    if ( ImGui::CollapsingHeader("3Dボリューム配置 (sdf3d)", ImGuiTreeNodeFlags_DefaultOpen) ) {
        ImGui::TextDisabled("監視中: %s  (input/に .obj を入れると自動で届きます)", volumeDir_.c_str());
        if ( volumeFiles_.empty() ) {
            ImGui::TextDisabled("（まだありません。SDFWatcher の input/ に\n"
                "  .obj を入れると自動で生成されて届きます）");
        } else if ( ImGui::Button("＋ ボリュームを追加") ) {
            VolumeItem item;
            item.fileName = volumeFiles_[0];
            volumes_.push_back(std::move(item));
            ScanAndLoad(); // その場でロード（「今すぐ再スキャン」と同じ安全経路）
        }

        int deleteIndex = -1;
        for ( int i = 0; i < ( int ) volumes_.size(); ++i ) {
            ImGui::PushID(3000 + i);
            VolumeItem& item = volumes_[i];
            std::string header = "ボリューム " + std::to_string(i) + " : " + item.fileName
                + ( item.vol ? "" : "  [ファイル待ち]" );
            if ( ImGui::TreeNode(header.c_str()) ) {
                // モデル選択（選び直したら作り直して即ロード）
                if ( ImGui::BeginCombo("モデル (.sdf3d)", item.fileName.c_str()) ) {
                    for ( const auto& f : volumeFiles_ ) {
                        if ( ImGui::Selectable(f.c_str(), item.fileName == f) && item.fileName != f ) {
                            item.fileName = f;
                            item.vol.reset();
                            item.loadedMorph.clear();
                            ScanAndLoad();
                        }
                    }
                    ImGui::EndCombo();
                }
                // モーフ先選択（距離場の lerp で形A→形Bへ連続変形）
                const char* morphLabel = item.morphFile.empty() ? "(なし)" : item.morphFile.c_str();
                if ( ImGui::BeginCombo("モーフ先", morphLabel) ) {
                    if ( ImGui::Selectable("(なし)", item.morphFile.empty()) ) {
                        item.morphFile.clear();
                        ScanAndLoad();
                    }
                    for ( const auto& f : volumeFiles_ ) {
                        if ( ImGui::Selectable(f.c_str(), item.morphFile == f) ) {
                            item.morphFile = f;
                            ScanAndLoad();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::Checkbox("表示", &item.visible);
                ImGui::DragFloat3("ワールド位置", &item.translation.x, 0.1f);
                ImGui::DragFloat("スケール", &item.scale, 0.05f, 0.01f, 100.0f);
                ImGui::ColorEdit4("単色 (カラーボリュームが無い時)", &item.color.x);
                ImGui::SliderFloat("エロージョン (+溶ける/-太る)", &item.erode, -0.3f, 0.5f);
                if ( item.vol && item.vol->HasMorph() ) {
                    ImGui::SliderFloat("モーフ (0=A / 1=B)", &item.morphT, 0.0f, 1.0f);
                }
                if ( ImGui::Button("削除") ) { deleteIndex = i; }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if ( deleteIndex >= 0 ) { volumes_.erase(volumes_.begin() + deleteIndex); }
    }

    ImGui::End();
#endif
}
