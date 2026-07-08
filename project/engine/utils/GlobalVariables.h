#pragma once
// =====================================================================
//  GlobalVariables : 調整項目マネージャ
//   「ImGuiで数値を編集 → JSONに保存 → 起動時に自動復元」を一元化する。
//   チーム制作の定番。各クラスが個別にやっていた調整UI/保存を集約する。
//
//   使い方（典型）:
//     auto* gv = GlobalVariables::GetInstance();
//     const char* g = "Player";
//     gv->AddItem(g, "speed", 5.0f);     // 既に保存値があればそれが優先される
//     gv->AddItem(g, "jump",  8.0f);
//     ...
//     speed_ = gv->GetFloat(g, "speed"); // 毎フレーム取得（エディタ変更が反映される）
//
//   ・AddItem は「未登録の時だけ」既定値を入れる（保存値を上書きしない）
//   ・Update() を毎フレーム呼ぶと "調整項目" ウィンドウが出て、編集＆グループ保存できる
//   ・保存先: resources/GlobalVariables/<group>.json
// =====================================================================
#include <string>
#include <map>
#include <variant>
#include "engine/math/struct.h"

class GlobalVariables{
public:
    static GlobalVariables* GetInstance();

    // 値1つ分（対応型: int / float / bool / Vector3）
    using Item = std::variant<int32_t, float, bool, Vector3>;

    // グループを作る（無ければ）
    void CreateGroup(const std::string& groupName);

    // 項目を登録（未登録の時だけ既定値をセット＝保存値を尊重）
    void AddItem(const std::string& group, const std::string& key, int32_t value);
    void AddItem(const std::string& group, const std::string& key, float value);
    void AddItem(const std::string& group, const std::string& key, bool value);
    void AddItem(const std::string& group, const std::string& key, const Vector3& value);

    // 値をコードから設定（型は登録時に合わせること）
    void SetValue(const std::string& group, const std::string& key, int32_t value);
    void SetValue(const std::string& group, const std::string& key, float value);
    void SetValue(const std::string& group, const std::string& key, bool value);
    void SetValue(const std::string& group, const std::string& key, const Vector3& value);

    // 値取得（未登録なら 0 / false / 原点）
    int32_t GetInt(const std::string& group, const std::string& key) const;
    float   GetFloat(const std::string& group, const std::string& key) const;
    bool    GetBool(const std::string& group, const std::string& key) const;
    Vector3 GetVector3(const std::string& group, const std::string& key) const;

    // ImGui 描画（毎フレーム呼ぶ：編集＋グループ保存ボタン）
    void Update();

    // 保存・読込
    void SaveFile(const std::string& group);  // 指定グループを保存
    void LoadFiles();                          // 保存フォルダ内の全 .json を読み込む
    void LoadFile(const std::string& group);   // 指定グループを読み込む

private:
    GlobalVariables() = default;
    ~GlobalVariables() = default;
    GlobalVariables(const GlobalVariables&) = delete;
    GlobalVariables& operator=(const GlobalVariables&) = delete;

    // グループ = 項目名 → 値
    using Group = std::map<std::string, Item>;
    std::map<std::string, Group> datas_;

    const std::string kDirectoryPath = "resources/GlobalVariables/";
};
