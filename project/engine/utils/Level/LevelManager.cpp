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

    // レールノードのリストも同様に配列に変換する（複数レール対応）
    json linesArray = json::array();
    for ( const auto& line : levelData.railLines ) {
        json railArray = json::array();
        for ( const auto& nodePos : line ) {
            // [x, y, z] の配列形式で保存
            railArray.push_back({ nodePos.x, nodePos.y, nodePos.z });
        }
        linesArray.push_back(railArray);
    }
    j["railLines"] = linesArray;

    // 各レールのタイプ（-1=自動 / 0=横 / 1=縦）。railLines と数を合わせて保存
    json typesArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int t = ( i < levelData.railTypes.size() ) ? levelData.railTypes[i] : -1;
        typesArray.push_back(t);
    }
    j["railTypes"] = typesArray;

    // 各レールの動き [振幅x, 振幅y, 振幅z, 周期]。railLines と数を合わせて保存
    json motionsArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        Vector4 m = ( i < levelData.railMotions.size() ) ? levelData.railMotions[i] : Vector4 { 0.0f, 0.0f, 0.0f, 2.0f };
        motionsArray.push_back({ m.x, m.y, m.z, m.w });
    }
    j["railMotions"] = motionsArray;

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

    // ★複数レールの配列を読み込む
    if ( j.contains("railLines") && j["railLines"].is_array() ) {
        for ( const auto& lineObj : j["railLines"] ) {
            std::vector<Vector3> line;
            for ( const auto& posObj : lineObj ) {
                Vector3 pos;
                pos.x = posObj[0]; pos.y = posObj[1]; pos.z = posObj[2];
                line.push_back(pos);
            }
            levelData.railLines.push_back(line);
        }
    }
    // ★古いセーブデータ互換用（昔の railNodes があった場合）
    else if ( j.contains("railNodes") && j["railNodes"].is_array() ) {
        std::vector<Vector3> line;
        for ( const auto& posObj : j["railNodes"] ) {
            Vector3 pos;
            pos.x = posObj[0]; pos.y = posObj[1]; pos.z = posObj[2];
            line.push_back(pos);
        }
        levelData.railLines.push_back(line);
    }

    // レールのタイプを読み込む（-1=自動 / 0=横 / 1=縦）
    if ( j.contains("railTypes") && j["railTypes"].is_array() ) {
        for ( const auto& t : j["railTypes"] ) {
            levelData.railTypes.push_back(t.get<int>());
        }
    }

    // レールの動きを読み込む [振幅x, 振幅y, 振幅z, 周期]
    if ( j.contains("railMotions") && j["railMotions"].is_array() ) {
        for ( const auto& m : j["railMotions"] ) {
            Vector4 motion { 0.0f, 0.0f, 0.0f, 2.0f };
            if ( m.is_array() && m.size() >= 4 ) {
                motion.x = m[0]; motion.y = m[1]; motion.z = m[2]; motion.w = m[3];
            }
            levelData.railMotions.push_back(motion);
        }
    }

    // 万が一レールが1本もない場合は、空のレールを1つ追加しておく
    if ( levelData.railLines.empty() ) {
        levelData.railLines.push_back(std::vector<Vector3>());
    }

    // タイプ・動き配列を railLines と同じ数に整える（足りない分はデフォルト）
    levelData.railTypes.resize(levelData.railLines.size(), -1);
    levelData.railMotions.resize(levelData.railLines.size(), Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });

    return levelData;
}