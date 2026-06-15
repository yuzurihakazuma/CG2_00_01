#include "GlobalVariables.h"

#include <fstream>
#include <filesystem>

#include "externals/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

using json = nlohmann::json;

GlobalVariables* GlobalVariables::GetInstance(){
    static GlobalVariables instance;
    return &instance;
}

void GlobalVariables::CreateGroup(const std::string& groupName){
    datas_[groupName]; // 無ければ空グループを作る
}

// --- AddItem：未登録の時だけ既定値を入れる（保存・復元値を尊重）---
void GlobalVariables::AddItem(const std::string& group, const std::string& key, int32_t value){
    Group& g = datas_[group];
    if ( g.find(key) == g.end() ) { g[key] = value; }
}
void GlobalVariables::AddItem(const std::string& group, const std::string& key, float value){
    Group& g = datas_[group];
    if ( g.find(key) == g.end() ) { g[key] = value; }
}
void GlobalVariables::AddItem(const std::string& group, const std::string& key, bool value){
    Group& g = datas_[group];
    if ( g.find(key) == g.end() ) { g[key] = value; }
}
void GlobalVariables::AddItem(const std::string& group, const std::string& key, const Vector3& value){
    Group& g = datas_[group];
    if ( g.find(key) == g.end() ) { g[key] = value; }
}

void GlobalVariables::SetValue(const std::string& group, const std::string& key, int32_t value){ datas_[group][key] = value; }
void GlobalVariables::SetValue(const std::string& group, const std::string& key, float value){ datas_[group][key] = value; }
void GlobalVariables::SetValue(const std::string& group, const std::string& key, bool value){ datas_[group][key] = value; }
void GlobalVariables::SetValue(const std::string& group, const std::string& key, const Vector3& value){ datas_[group][key] = value; }

int32_t GlobalVariables::GetInt(const std::string& group, const std::string& key) const{
    auto gi = datas_.find(group);
    if ( gi == datas_.end() ) return 0;
    auto ki = gi->second.find(key);
    if ( ki == gi->second.end() ) return 0;
    if ( std::holds_alternative<int32_t>(ki->second) ) return std::get<int32_t>(ki->second);
    return 0;
}
float GlobalVariables::GetFloat(const std::string& group, const std::string& key) const{
    auto gi = datas_.find(group);
    if ( gi == datas_.end() ) return 0.0f;
    auto ki = gi->second.find(key);
    if ( ki == gi->second.end() ) return 0.0f;
    if ( std::holds_alternative<float>(ki->second) ) return std::get<float>(ki->second);
    return 0.0f;
}
bool GlobalVariables::GetBool(const std::string& group, const std::string& key) const{
    auto gi = datas_.find(group);
    if ( gi == datas_.end() ) return false;
    auto ki = gi->second.find(key);
    if ( ki == gi->second.end() ) return false;
    if ( std::holds_alternative<bool>(ki->second) ) return std::get<bool>(ki->second);
    return false;
}
Vector3 GlobalVariables::GetVector3(const std::string& group, const std::string& key) const{
    auto gi = datas_.find(group);
    if ( gi == datas_.end() ) return { 0.0f, 0.0f, 0.0f };
    auto ki = gi->second.find(key);
    if ( ki == gi->second.end() ) return { 0.0f, 0.0f, 0.0f };
    if ( std::holds_alternative<Vector3>(ki->second) ) return std::get<Vector3>(ki->second);
    return { 0.0f, 0.0f, 0.0f };
}

void GlobalVariables::Update(){
#ifdef USE_IMGUI
    ImGui::Begin("調整項目 (GlobalVariables)");

    if ( datas_.empty() ) {
        ImGui::TextDisabled("(登録された項目がありません)");
    }

    for ( auto& [groupName, group] : datas_ ) {
        if ( !ImGui::CollapsingHeader(groupName.c_str()) ) continue;
        ImGui::PushID(groupName.c_str());

        for ( auto& [key, item] : group ) {
            if ( std::holds_alternative<int32_t>(item) ) {
                int32_t v = std::get<int32_t>(item);
                if ( ImGui::DragInt(key.c_str(), &v) ) item = v;
            } else if ( std::holds_alternative<float>(item) ) {
                float v = std::get<float>(item);
                if ( ImGui::DragFloat(key.c_str(), &v, 0.05f) ) item = v;
            } else if ( std::holds_alternative<bool>(item) ) {
                bool v = std::get<bool>(item);
                if ( ImGui::Checkbox(key.c_str(), &v) ) item = v;
            } else if ( std::holds_alternative<Vector3>(item) ) {
                Vector3 v = std::get<Vector3>(item);
                if ( ImGui::DragFloat3(key.c_str(), &v.x, 0.05f) ) item = v;
            }
        }

        ImGui::Separator();
        if ( ImGui::Button("このグループを保存") ) { SaveFile(groupName); }
        ImGui::PopID();
    }

    ImGui::End();
#endif
}

void GlobalVariables::SaveFile(const std::string& group){
    auto gi = datas_.find(group);
    if ( gi == datas_.end() ) return;

    json root = json::object();
    json& jGroup = root[group];

    for ( auto& [key, item] : gi->second ) {
        if ( std::holds_alternative<int32_t>(item) ) {
            jGroup[key] = std::get<int32_t>(item);
        } else if ( std::holds_alternative<float>(item) ) {
            jGroup[key] = std::get<float>(item);
        } else if ( std::holds_alternative<bool>(item) ) {
            jGroup[key] = std::get<bool>(item);
        } else if ( std::holds_alternative<Vector3>(item) ) {
            Vector3 v = std::get<Vector3>(item);
            jGroup[key] = json::array({ v.x, v.y, v.z });
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(kDirectoryPath, ec);

    std::ofstream file(kDirectoryPath + group + ".json");
    if ( file.is_open() ) { file << root.dump(4); }
}

void GlobalVariables::LoadFiles(){
    namespace fs = std::filesystem;
    std::error_code ec;
    if ( !fs::exists(kDirectoryPath, ec) ) return;

    for ( const auto& entry : fs::directory_iterator(kDirectoryPath, ec) ) {
        if ( entry.is_regular_file() && entry.path().extension() == ".json" ) {
            LoadFile(entry.path().stem().string());
        }
    }
}

void GlobalVariables::LoadFile(const std::string& group){
    std::ifstream file(kDirectoryPath + group + ".json");
    if ( !file.is_open() ) return;

    json root;
    try { file >> root; } catch ( ... ) { return; }

    if ( !root.contains(group) ) return;
    const json& jGroup = root[group];

    Group& g = datas_[group];
    for ( auto it = jGroup.begin(); it != jGroup.end(); ++it ) {
        const std::string& key = it.key();
        const json& v = it.value();

        if ( v.is_number_integer() ) {
            g[key] = v.get<int32_t>();
        } else if ( v.is_number_float() ) {
            g[key] = v.get<float>();
        } else if ( v.is_boolean() ) {
            g[key] = v.get<bool>();
        } else if ( v.is_array() && v.size() >= 3 ) {
            g[key] = Vector3{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
        }
    }
}
