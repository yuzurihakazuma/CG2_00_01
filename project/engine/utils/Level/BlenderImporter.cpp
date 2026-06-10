#include "BlenderImporter.h"

// --- 標準ライブラリ ---
#include <fstream>
#include <cmath>
#include <ctime>
#include <cctype>
#include <algorithm>

// --- 外部ライブラリ ---
#include "externals/nlohmann/json.hpp"

// --- エンジン側のファイル ---
#include "engine/utils/Level/LevelEditor.h"
#include "engine/utils/ImGuiManager.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/math/Matrix4x4.h"

using json = nlohmann::json;
using namespace MatrixMath;

namespace{

    // ------------------------------------------------------------
    // JSONから Vector3 を読む（キー名のゆらぎ対応: key1 → key2 の順で探す）
    // ------------------------------------------------------------
    bool ReadVec3(const json& j, const char* key1, const char* key2, Vector3& out){
        const char* keys[] = { key1, key2 };
        for ( const char* k : keys ) {
            if ( j.contains(k) && j[k].is_array() && j[k].size() >= 3 ) {
                out.x = j[k][0].get<float>();
                out.y = j[k][1].get<float>();
                out.z = j[k][2].get<float>();
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------
    // 回転行列（正規化済み）からオイラー角(XYZ順・エンジン規約 R = Rx*Ry*Rz)を抽出
    //   エンジンの行ベクトル規約での合成行列:
    //   R[0] = [ cy*cz,            cy*sz,            -sy   ]
    //   R[1] = [ sx*sy*cz - cx*sz, cx*cz + sx*sy*sz, sx*cy ]
    //   R[2] = [ sx*sz + cx*sy*cz, cx*sy*sz - sx*cz, cx*cy ]
    // ------------------------------------------------------------
    Vector3 ExtractEulerXYZ(const Matrix4x4& m){
        Vector3 e;
        float sy = std::clamp(-m.m[0][2], -1.0f, 1.0f);
        e.y = std::asin(sy);
        float cy = std::cos(e.y);
        if ( std::abs(cy) > 1e-4f ) {
            e.x = std::atan2(m.m[1][2], m.m[2][2]);
            e.z = std::atan2(m.m[0][1], m.m[0][0]);
        } else {
            // ジンバルロック時は rz=0 として rx に集約
            e.x = std::atan2(-m.m[2][1], m.m[1][1]);
            e.z = 0.0f;
        }
        return e;
    }

    // ------------------------------------------------------------
    // アフィン行列を 平行移動・回転・スケール に分解
    // （親子合成後のワールド行列をフラットな TRS に戻すために使う）
    // ------------------------------------------------------------
    void DecomposeAffine(const Matrix4x4& m, Vector3& outT, Vector3& outR, Vector3& outS){
        outT = { m.m[3][0], m.m[3][1], m.m[3][2] };

        auto rowLen = [&](int i) -> float{
            return std::sqrt(m.m[i][0] * m.m[i][0] + m.m[i][1] * m.m[i][1] + m.m[i][2] * m.m[i][2]);
            };
        outS = { rowLen(0), rowLen(1), rowLen(2) };

        // スケールを取り除いて純粋な回転行列にする
        Matrix4x4 rot = MakeIdentity4x4();
        const float s[3] = { outS.x, outS.y, outS.z };
        for ( int i = 0; i < 3; ++i ) {
            float inv = ( s[i] > 1e-8f ) ? 1.0f / s[i] : 0.0f;
            for ( int jc = 0; jc < 3; ++jc ) {
                rot.m[i][jc] = m.m[i][jc] * inv;
            }
        }
        outR = ExtractEulerXYZ(rot);
    }

    // 再帰パースで持ち回る情報
    struct ParseContext{
        bool convertAxes;
        bool rotationInDegrees;
        float importScale;
        LevelData* out;
        BlenderImporter* importer;                // EnsureModelLoaded 用
        std::vector<std::string>* missingModels;
        std::vector<std::string>* skippedTypes;
        bool (BlenderImporter::* ensureModel)(const std::string&);
        // CAMERA 検出用（最初に見つけた1台を記録）
        bool*    hasCamera;
        Vector3* cameraPos;
        Vector3* cameraRot;
    };

    void PushUnique(std::vector<std::string>& v, const std::string& s){
        if ( std::find(v.begin(), v.end(), s) == v.end() ) v.push_back(s);
    }

    // ------------------------------------------------------------
    // オブジェクト1個を再帰的にパース（children は親のワールド行列を引き継ぐ）
    // ------------------------------------------------------------
    void ParseObjectRecursive(const json& obj, const Matrix4x4& parentWorld, ParseContext& ctx){
        if ( !obj.is_object() ) return;

        // --- ローカルTRSを読む（"transform" の中 or 直下、どちらでも）---
        const json& tr = ( obj.contains("transform") && obj["transform"].is_object() )
            ? obj["transform"] : obj;

        Vector3 pos { 0.0f, 0.0f, 0.0f };
        Vector3 rot { 0.0f, 0.0f, 0.0f };
        Vector3 scl { 1.0f, 1.0f, 1.0f };
        ReadVec3(tr, "translation", "translate", pos);
        ReadVec3(tr, "rotation", "rotate", rot);
        ReadVec3(tr, "scaling", "scale", scl);

        // 度→ラジアン
        if ( ctx.rotationInDegrees ) {
            const float deg2rad = 3.14159265358979323846f / 180.0f;
            rot.x *= deg2rad; rot.y *= deg2rad; rot.z *= deg2rad;
        }

        // Blender(Z-up右手) → エンジン(Y-up左手)
        //   位置: (x, y, z) → (x, z, y)
        //   回転: 軸の入替＋鏡映で角度の符号が反転 → (-rx, -rz, -ry)
        //   スケール: (sx, sz, sy)
        if ( ctx.convertAxes ) {
            pos = { pos.x, pos.z, pos.y };
            rot = { -rot.x, -rot.z, -rot.y };
            scl = { scl.x, scl.z, scl.y };
        }

        // --- 親子合成（行ベクトル規約: world = local * parentWorld）---
        Matrix4x4 local = MakeAffine(scl, rot, pos);
        Matrix4x4 world = Multiply(local, parentWorld);

        std::string type = obj.value("type", "MESH");
        std::string name = obj.value("name", "");

        if ( type == "MESH" ) {
            Vector3 t, r, s;
            DecomposeAffine(world, t, r, s);
            t.x *= ctx.importScale; t.y *= ctx.importScale; t.z *= ctx.importScale;

            LevelObjectData od;
            od.type = obj.value("file_name", "");
            if ( od.type.empty() ) od.type = name.empty() ? "block" : name;
            od.translation = t;
            od.rotation = r;
            od.scale = s;
            ctx.out->objects.push_back(od);

            // モデルの自動ロード（失敗したら警告リストへ）
            if ( !( ctx.importer->*( ctx.ensureModel ) )( od.type ) ) {
                PushUnique(*ctx.missingModels, od.type);
            }
        } else if ( type == "CAMERA" ) {
            // 最初に見つけたカメラの位置・回転を記録（「カメラに適用」で使う）
            if ( !( *ctx.hasCamera ) ) {
                Vector3 t, r, s;
                DecomposeAffine(world, t, r, s);
                t.x *= ctx.importScale; t.y *= ctx.importScale; t.z *= ctx.importScale;
                *ctx.cameraPos = t;
                *ctx.cameraRot = r;
                *ctx.hasCamera = true;
            }
            PushUnique(*ctx.skippedTypes, type + ( name.empty() ? "" : " (" + name + ")" ));
        } else {
            // EMPTY / LIGHT 等は配置しない（記録だけ残し、childrenは処理継続）
            PushUnique(*ctx.skippedTypes, type + ( name.empty() ? "" : " (" + name + ")" ));
        }

        // --- children を再帰処理 ---
        if ( obj.contains("children") && obj["children"].is_array() ) {
            for ( const auto& child : obj["children"] ) {
                ParseObjectRecursive(child, world, ctx);
            }
        }
    }

} // namespace

// =====================================================================
//  public
// =====================================================================

void BlenderImporter::Initialize(LevelEditor* levelEditor){
    levelEditor_ = levelEditor;

    // 前回の設定（フォルダ・オプション・最後のファイル）を復元
    LoadSettings();

    // エクスポート先フォルダが無ければ作っておく（Blender側がすぐ出力できるように）
    std::error_code ec;
    std::filesystem::create_directories(folder_, ec);

    ScanFolder();

    // 起動時に前回のシーンを自動インポート
    if ( autoImportOnStartup_ && !importedPath_.empty() ) {
        std::error_code ec2;
        if ( std::filesystem::exists(importedPath_, ec2) ) {
            Import(importedPath_);
        }
    }
}

// ------------------------------------------------------------
// 設定の保存・復元
// ------------------------------------------------------------
namespace{
    constexpr const char* kSettingsPath = "resources/blender_importer_settings.json";
}

void BlenderImporter::SaveSettings() const{
    json j;
    j["folder"] = folder_;
    j["convertAxes"] = convertAxes_;
    j["rotationInDegrees"] = rotationInDegrees_;
    j["importScale"] = importScale_;
    j["additive"] = additive_;
    j["hotReload"] = hotReload_;
    j["autoImportOnStartup"] = autoImportOnStartup_;
    j["cameraCorrection"] = cameraCorrection_;
    j["lastImportedPath"] = importedPath_;

    std::ofstream file(kSettingsPath);
    if ( file.is_open() ) { file << j.dump(4); }
}

void BlenderImporter::LoadSettings(){
    std::ifstream file(kSettingsPath);
    if ( !file.is_open() ) return;

    json j;
    try { file >> j; } catch ( ... ) { return; }

    folder_ = j.value("folder", folder_);
    convertAxes_ = j.value("convertAxes", convertAxes_);
    rotationInDegrees_ = j.value("rotationInDegrees", rotationInDegrees_);
    importScale_ = j.value("importScale", importScale_);
    additive_ = j.value("additive", additive_);
    hotReload_ = j.value("hotReload", hotReload_);
    autoImportOnStartup_ = j.value("autoImportOnStartup", autoImportOnStartup_);
    cameraCorrection_ = j.value("cameraCorrection", cameraCorrection_);
    importedPath_ = j.value("lastImportedPath", std::string());
}

// ------------------------------------------------------------
// エクスプローラーからのD&D取込
// ------------------------------------------------------------
void BlenderImporter::HandleDroppedFile(const std::string& path){
    namespace fs = std::filesystem;
    fs::path p(path);

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c){ return ( char ) std::tolower(c); });

    if ( ext == ".json" ) {
        // シーンとしてインポート（以降ホットリロードの監視対象にもなる）
        if ( Import(path) ) {
            dropMessage_ = "D&D取込: " + p.filename().string() + " をシーンとして読込";
        } else {
            dropMessage_ = "D&D取込失敗: " + lastError_;
        }
    } else if ( ext == ".obj" || ext == ".gltf" ) {
        // モデルとしてロード（モデル名はファイル名から拡張子を除いたもの）
        std::string name = p.stem().string();
        if ( !ModelManager::GetInstance()->FindModel(name) ) {
            ModelManager::GetInstance()->LoadModel(name, p.parent_path().string(), p.filename().string());
        }
        if ( ModelManager::GetInstance()->FindModel(name) ) {
            dropMessage_ = "D&D取込: モデル '" + name + "' をロードしました";
        } else {
            dropMessage_ = "D&D取込失敗: " + p.filename().string();
        }
    } else {
        dropMessage_ = "未対応の拡張子です: " + p.filename().string();
    }
}

// ------------------------------------------------------------
// 「カメラに適用」要求の受け渡し（シーン側から毎フレーム呼ばれる）
// ------------------------------------------------------------
bool BlenderImporter::ConsumeCameraRequest(Vector3& outPos, Vector3& outRot){
    if ( !cameraApplyRequested_ ) return false;
    cameraApplyRequested_ = false;

    outPos = cameraPos_;
    outRot = cameraRot_;
    if ( cameraCorrection_ ) {
        // Blenderカメラはローカル -Z が前方（rx=90°で水平を向く）。
        // 座標変換で rx → -rx になっているので +90° すると エンジンの前方 +Z と一致する
        outRot.x += 3.14159265358979323846f * 0.5f;
    }
    return true;
}

void BlenderImporter::ScanFolder(){
    std::string prevSelected = ( selectedFile_ >= 0 && selectedFile_ < ( int ) files_.size() )
        ? files_[selectedFile_] : "";

    files_.clear();
    selectedFile_ = -1;

    std::error_code ec;
    if ( !std::filesystem::exists(folder_, ec) ) return;

    for ( const auto& entry : std::filesystem::directory_iterator(folder_, ec) ) {
        if ( entry.is_regular_file() && entry.path().extension() == ".json" ) {
            files_.push_back(entry.path().filename().string());
        }
    }
    std::sort(files_.begin(), files_.end());

    // 以前選んでいたファイルが残っていれば選択を維持する
    for ( int i = 0; i < ( int ) files_.size(); ++i ) {
        if ( files_[i] == prevSelected ) { selectedFile_ = i; break; }
    }
}

bool BlenderImporter::EnsureModelLoaded(const std::string& modelName){
    if ( modelName.empty() ) return false;
    if ( ModelManager::GetInstance()->FindModel(modelName) ) return true;

    namespace fs = std::filesystem;
    // 探索順: resources/<名前>/<名前>.obj → .gltf → resources/<名前>.obj → .gltf
    const std::pair<std::string, std::string> candidates[] = {
        { "resources/" + modelName, modelName + ".obj" },
        { "resources/" + modelName, modelName + ".gltf" },
        { "resources",              modelName + ".obj" },
        { "resources",              modelName + ".gltf" },
    };
    for ( const auto& c : candidates ) {
        std::error_code ec;
        if ( fs::exists(fs::path(c.first) / c.second, ec) ) {
            ModelManager::GetInstance()->LoadModel(modelName, c.first, c.second);
            if ( ModelManager::GetInstance()->FindModel(modelName) ) return true;
        }
    }
    return false;
}

bool BlenderImporter::Import(const std::string& path){
    lastError_.clear();
    missingModels_.clear();
    skippedTypes_.clear();
    hasCamera_ = false;

    std::ifstream file(path);
    if ( !file.is_open() ) {
        lastError_ = "ファイルを開けません: " + path;
        return false;
    }

    json j;
    try {
        file >> j;
    } catch ( const std::exception& ex ) {
        // Blenderが書き込み中の可能性もある（次の更新検知で再試行される）
        lastError_ = std::string("JSONパース失敗: ") + ex.what();
        return false;
    }

    LevelData data;
    data.name = j.value("name", "");

    ParseContext ctx {};
    ctx.convertAxes = convertAxes_;
    ctx.rotationInDegrees = rotationInDegrees_;
    ctx.importScale = importScale_;
    ctx.out = &data;
    ctx.importer = this;
    ctx.missingModels = &missingModels_;
    ctx.skippedTypes = &skippedTypes_;
    ctx.ensureModel = &BlenderImporter::EnsureModelLoaded;
    ctx.hasCamera = &hasCamera_;
    ctx.cameraPos = &cameraPos_;
    ctx.cameraRot = &cameraRot_;

    if ( j.contains("objects") && j["objects"].is_array() ) {
        Matrix4x4 identity = MakeIdentity4x4();
        for ( const auto& obj : j["objects"] ) {
            ParseObjectRecursive(obj, identity, ctx);
        }
    }

    // --- LevelEditor へ反映 ---
    if ( levelEditor_ ) {
        levelEditor_->ApplyImportedData(data, additive_);
    }

    importedObjectCount_ = ( int ) data.objects.size();
    importedPath_ = path;
    std::error_code ec;
    importedTime_ = std::filesystem::last_write_time(path, ec);
    ++importCount_;

    // 読込時刻の文字列を作る
    std::time_t now = std::time(nullptr);
    std::tm tmv {};
    localtime_s(&tmv, &now);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
    lastImportTimeStr_ = buf;

    // 次回起動時に復元できるよう設定を保存
    SaveSettings();

    return true;
}

void BlenderImporter::DrawDebugUI(){
#ifdef USE_IMGUI
    ImGui::Begin("Blenderインポート (Blender)");

    // =========================================================
    // 1. フォルダとファイル一覧
    // =========================================================
    char folderBuf[256];
    strcpy_s(folderBuf, folder_.c_str());
    if ( ImGui::InputText("フォルダ", folderBuf, sizeof(folderBuf)) ) {
        folder_ = folderBuf;
    }
    ImGui::SameLine();
    if ( ImGui::Button("再スキャン") ) { ScanFolder(); }

    if ( ImGui::BeginListBox("##BlenderFileList", ImVec2(-FLT_MIN, 80.0f)) ) {
        for ( int i = 0; i < ( int ) files_.size(); ++i ) {
            if ( ImGui::Selectable(files_[i].c_str(), selectedFile_ == i) ) {
                selectedFile_ = i;
            }
        }
        if ( files_.empty() ) {
            ImGui::TextDisabled("(.json がありません。Blenderからこのフォルダへ出力してください)");
        }
        ImGui::EndListBox();
    }

    // =========================================================
    // 2. インポートオプション（変更したら設定ファイルに保存）
    // =========================================================
    ImGui::Separator();
    bool optionChanged = false;
    optionChanged |= ImGui::Checkbox("座標系を変換 (Blender Z-up → Y-up)", &convertAxes_);
    optionChanged |= ImGui::Checkbox("回転を度として解釈 (OFF=ラジアン)", &rotationInDegrees_);
    ImGui::SetNextItemWidth(120.0f);
    optionChanged |= ImGui::DragFloat("配置スケール", &importScale_, 0.05f, 0.01f, 100.0f);
    if ( ImGui::RadioButton("置換 (マップを置き換え)", !additive_) ) { additive_ = false; optionChanged = true; }
    ImGui::SameLine();
    if ( ImGui::RadioButton("追記 (既存マップに追加)", additive_) ) { additive_ = true; optionChanged = true; }
    optionChanged |= ImGui::Checkbox("起動時に前回のシーンを自動読込", &autoImportOnStartup_);

    // =========================================================
    // 3. 実行ボタン
    // =========================================================
    ImGui::Separator();
    bool canImport = ( selectedFile_ >= 0 && selectedFile_ < ( int ) files_.size() );
    if ( !canImport ) ImGui::BeginDisabled();
    if ( ImGui::Button("インポート実行", ImVec2(150.0f, 0.0f)) ) {
        Import(folder_ + "/" + files_[selectedFile_]);
    }
    if ( !canImport ) ImGui::EndDisabled();
    ImGui::SameLine();
    if ( importedPath_.empty() ) ImGui::BeginDisabled();
    if ( ImGui::Button("再読込") ) {
        Import(importedPath_);
    }
    if ( importedPath_.empty() ) ImGui::EndDisabled();

    // =========================================================
    // 4. ホットリロード（ファイル更新を検知して自動再インポート）
    // =========================================================
    optionChanged |= ImGui::Checkbox("自動リロード（Blenderで保存すると即反映）", &hotReload_);
    if ( hotReload_ && !importedPath_.empty() ) {
        pollTimer_ += ImGui::GetIO().DeltaTime;
        if ( pollTimer_ >= 0.5f ) { // 0.5秒間隔でタイムスタンプを確認
            pollTimer_ = 0.0f;
            std::error_code ec;
            auto t = std::filesystem::last_write_time(importedPath_, ec);
            if ( !ec && t != importedTime_ ) {
                importedTime_ = t; // パース失敗時に連打しないよう先に更新しておく
                Import(importedPath_);
            }
        }
        ImGui::TextDisabled("監視中: %s", importedPath_.c_str());
    }

    // =========================================================
    // 4.5 Blenderカメラの反映
    // =========================================================
    if ( hasCamera_ ) {
        ImGui::Separator();
        ImGui::Text("Blenderカメラ: 検出済み");
        optionChanged |= ImGui::Checkbox("カメラ向き補正 (+90°ピッチ)", &cameraCorrection_);
        if ( ImGui::Button("カメラに適用") ) {
            cameraApplyRequested_ = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Blenderの構図をエンジンのカメラへ)");
    }

    // オプションが変わったら設定ファイルへ保存
    if ( optionChanged ) { SaveSettings(); }

    // =========================================================
    // 5. 結果サマリ
    // =========================================================
    ImGui::Separator();
    if ( importCount_ > 0 ) {
        ImGui::Text("読込回数: %d 回 / オブジェクト数: %d", importCount_, importedObjectCount_);
        ImGui::Text("最終読込: %s", lastImportTimeStr_.c_str());
    } else {
        ImGui::TextDisabled("まだインポートしていません");
    }
    if ( !dropMessage_.empty() ) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", dropMessage_.c_str());
    }
    if ( !lastError_.empty() ) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "エラー: %s", lastError_.c_str());
    }
    if ( !missingModels_.empty() ) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "モデルが見つかりません（代替表示）:");
        for ( const auto& m : missingModels_ ) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "  ・%s", m.c_str());
        }
    }
    if ( !skippedTypes_.empty() ) {
        ImGui::TextDisabled("配置をスキップした type:");
        for ( const auto& s : skippedTypes_ ) {
            ImGui::TextDisabled("  ・%s", s.c_str());
        }
    }

    ImGui::End();
#endif
}
