#include "LevelManager.h"
#include "externals/nlohmann/json.hpp"
#include <fstream>
#include <cassert>

using json = nlohmann::json;

void LevelManager::Save(const std::string& fileName, const LevelData& levelData){
    json j;
    j["name"] = levelData.name;

    // オブジェクトのリストをJSONの配列に変換する
    json objectsArray = json::array();
    for ( const auto& obj : levelData.objects ) {
        json jsonObj;
        jsonObj["type"] = obj.type;
        // Vector3を配列 [x, y, z] の形で保存
        jsonObj["translation"] = { obj.translation.x, obj.translation.y, obj.translation.z };
        jsonObj["rotation"] = { obj.rotation.x, obj.rotation.y, obj.rotation.z };
        jsonObj["scale"] = { obj.scale.x, obj.scale.y, obj.scale.z };

        objectsArray.push_back(jsonObj);
    }

	// レールノードのリストも同様に配列に変換する
    json railArray = json::array();
    for ( const auto& nodePos : levelData.railNodes ) {
        // [x, y, z] の配列形式で保存
        railArray.push_back({ nodePos.x, nodePos.y, nodePos.z });
    }
    j["railNodes"] = railArray;


    j["objects"] = objectsArray;

    // ファイルに書き込み
    std::ofstream file(fileName);
    if ( file.is_open() ) {
        // dump(4) は見やすくするためにインデント（空白）を4つ入れる設定です
        file << j.dump(4);
        file.close();
    }
}

LevelData LevelManager::Load(const std::string& fileName){
    LevelData levelData;

    std::ifstream file(fileName);
    if ( !file.is_open() ) {
        // ファイルがない場合は空のデータを返す
        return levelData;
    }

    json j;
    file >> j;

    // マップ名の読み込み（無い場合は "Unknown" にする）
    levelData.name = j.value("name", "Unknown");

    // オブジェクトの配列を読み込む
    if ( j.contains("objects") && j["objects"].is_array() ) {
        for ( const auto& jsonObj : j["objects"] ) {
            LevelObjectData obj;
            obj.type = jsonObj.value("type", "unknown");

            // XYZの座標データを取り出して Vector3 に入れる
            if ( jsonObj.contains("translation") ) {
                obj.translation.x = jsonObj["translation"][0];
                obj.translation.y = jsonObj["translation"][1];
                obj.translation.z = jsonObj["translation"][2];
            }
            if ( jsonObj.contains("rotation") ) {
                obj.rotation.x = jsonObj["rotation"][0];
                obj.rotation.y = jsonObj["rotation"][1];
                obj.rotation.z = jsonObj["rotation"][2];
            }
            if ( jsonObj.contains("scale") ) {
                obj.scale.x = jsonObj["scale"][0];
                obj.scale.y = jsonObj["scale"][1];
                obj.scale.z = jsonObj["scale"][2];
            }
            // 読み込んだオブジェクトをリストに追加
            levelData.objects.push_back(obj);
        }

    }

    // レールノードの配列も同様に読み込む
    if ( j.contains("railNodes") && j["railNodes"].is_array() ) {
        for ( const auto& posObj : j["railNodes"] ) {
            Vector3 pos;
            pos.x = posObj[0]; pos.y = posObj[1]; pos.z = posObj[2];
            levelData.railNodes.push_back(pos);
        }
    }

    return levelData;
}