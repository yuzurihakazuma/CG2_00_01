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

    // 各レールの地面タイプ(0=Safe/1=Gap/2=NoGround)。railLines と数を合わせて保存
    json groundArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int g = ( i < levelData.railGroundTypes.size() ) ? levelData.railGroundTypes[i] : 0;
        groundArray.push_back(g);
    }
    j["railGroundTypes"] = groundArray;

    // 各レールのノード単位の穴指定（外=レール / 内=ノード・1=穴）
    json holesArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        json rh = json::array();
        if ( i < levelData.railNodeHoles.size() ) {
            for ( int v : levelData.railNodeHoles[i] ) rh.push_back(v);
        }
        holesArray.push_back(rh);
    }
    j["railNodeHoles"] = holesArray;

    // 各レールの表示フラグ(1=表示/0=非表示)。railLines と数を合わせて保存
    json visibleArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int v = ( i < levelData.railVisible.size() ) ? levelData.railVisible[i] : 1;
        visibleArray.push_back(v);
    }
    j["railVisible"] = visibleArray;

    // 各レールの線のつなぎ方(0=スプライン/1=直線)。railLines と数を合わせて保存
    json lineModeArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int v = ( i < levelData.railLineModes.size() ) ? levelData.railLineModes[i] : 0;
        lineModeArray.push_back(v);
    }
    j["railLineModes"] = lineModeArray;

    // 各レールの道生成モード(0=自動/1=なし)。railLines と数を合わせて保存
    json roadModeArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int v = ( i < levelData.railRoadModes.size() ) ? levelData.railRoadModes[i] : 0;
        roadModeArray.push_back(v);
    }
    j["railRoadModes"] = roadModeArray;

    // 各レールの動き波形/位相・片方向・速度倍率（railLines と数を合わせて保存）
    json motionTypeArray = json::array();
    json motionPhaseArray = json::array();
    json oneWayArray = json::array();
    json speedMulArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        motionTypeArray.push_back(( i < levelData.railMotionTypes.size() ) ? levelData.railMotionTypes[i] : 0);
        motionPhaseArray.push_back(( i < levelData.railMotionPhases.size() ) ? levelData.railMotionPhases[i] : 0.0f);
        oneWayArray.push_back(( i < levelData.railOneWay.size() ) ? levelData.railOneWay[i] : 0);
        speedMulArray.push_back(( i < levelData.railSpeedMuls.size() ) ? levelData.railSpeedMuls[i] : 1.0f);
    }
    j["railMotionTypes"]  = motionTypeArray;
    j["railMotionPhases"] = motionPhaseArray;
    j["railOneWay"]       = oneWayArray;
    j["railSpeedMuls"]    = speedMulArray;

    // スタート/ゴール地点
    j["startRailIndex"] = levelData.startRailIndex;
    j["startNodeIndex"] = levelData.startNodeIndex;
    j["goalRailIndex"]  = levelData.goalRailIndex;
    j["goalNodeIndex"]  = levelData.goalNodeIndex;

    // カメラ演出ゾーン
    json camArray = json::array();
    for ( const auto& z : levelData.cameraZones ) {
        camArray.push_back({
            { "railIndex", z.railIndex }, { "nodeIndex", z.nodeIndex },
            { "radius", z.radius },
            { "mode", z.mode },
            { "offset", { z.offset.x, z.offset.y, z.offset.z } },
            { "yawDeg", z.yawDeg }, { "dist", z.dist }, { "height", z.height },
            { "revert", z.revert }, { "freeze", z.freeze },
            { "fovDeg", z.fovDeg },
        });
    }
    j["cameraZones"] = camArray;

    // 敵の配置 [type, railIndex, distance]
    json enemiesArray = json::array();
    for ( const auto& e : levelData.enemies ) {
        json je;
        je["type"]      = e.type;
        je["railIndex"] = e.railIndex;
        je["distance"]  = e.distance;
        je["patrol"]    = e.patrol;
        enemiesArray.push_back(je);
    }
    j["enemies"] = enemiesArray;

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

    // 地面タイプを読み込む（0=Safe/1=Gap/2=NoGround。無ければデフォルト0=Safe）
    if ( j.contains("railGroundTypes") && j["railGroundTypes"].is_array() ) {
        for ( const auto& g : j["railGroundTypes"] ) {
            levelData.railGroundTypes.push_back(g.get<int>());
        }
    }

    // ノード単位の穴指定を読み込む（外=レール / 内=ノード）
    if ( j.contains("railNodeHoles") && j["railNodeHoles"].is_array() ) {
        for ( const auto& rh : j["railNodeHoles"] ) {
            std::vector<int> holes;
            if ( rh.is_array() ) {
                for ( const auto& v : rh ) holes.push_back(v.get<int>());
            }
            levelData.railNodeHoles.push_back(holes);
        }
    }

    // 表示フラグを読み込む（1=表示/0=非表示。無ければデフォルト1=表示）
    if ( j.contains("railVisible") && j["railVisible"].is_array() ) {
        for ( const auto& v : j["railVisible"] ) {
            levelData.railVisible.push_back(v.get<int>());
        }
    }

    // 線のつなぎ方を読み込む（0=スプライン/1=直線。キーが無い旧JSONは後段の resize で全て0=スプライン）
    if ( j.contains("railLineModes") && j["railLineModes"].is_array() ) {
        for ( const auto& v : j["railLineModes"] ) {
            levelData.railLineModes.push_back(v.get<int>());
        }
    }

    // 道生成モードを読み込む（0=自動/1=なし。キーが無い旧JSONは後段の resize で全て0=自動になる）
    if ( j.contains("railRoadModes") && j["railRoadModes"].is_array() ) {
        for ( const auto& v : j["railRoadModes"] ) {
            levelData.railRoadModes.push_back(v.get<int>());
        }
    }

    // 動き波形/位相・片方向・速度倍率を読み込む（無ければ後段の resize でデフォルトが入る）
    if ( j.contains("railMotionTypes") && j["railMotionTypes"].is_array() ) {
        for ( const auto& v : j["railMotionTypes"] ) { levelData.railMotionTypes.push_back(v.get<int>()); }
    }
    if ( j.contains("railMotionPhases") && j["railMotionPhases"].is_array() ) {
        for ( const auto& v : j["railMotionPhases"] ) { levelData.railMotionPhases.push_back(v.get<float>()); }
    }
    if ( j.contains("railOneWay") && j["railOneWay"].is_array() ) {
        for ( const auto& v : j["railOneWay"] ) { levelData.railOneWay.push_back(v.get<int>()); }
    }
    if ( j.contains("railSpeedMuls") && j["railSpeedMuls"].is_array() ) {
        for ( const auto& v : j["railSpeedMuls"] ) { levelData.railSpeedMuls.push_back(v.get<float>()); }
    }

    // スタート/ゴール地点（無ければデフォルト：スタート=レール0ノード0 / ゴール=未設定）
    levelData.startRailIndex = j.value("startRailIndex", 0);
    levelData.startNodeIndex = j.value("startNodeIndex", 0);
    levelData.goalRailIndex  = j.value("goalRailIndex", -1);
    levelData.goalNodeIndex  = j.value("goalNodeIndex", 0);

    // カメラ演出ゾーン
    if ( j.contains("cameraZones") && j["cameraZones"].is_array() ) {
        for ( const auto& jz : j["cameraZones"] ) {
            LevelCameraZone z;
            z.railIndex = jz.value("railIndex", 0);
            z.nodeIndex = jz.value("nodeIndex", 0);
            z.radius    = jz.value("radius", 4.0f);
            z.mode      = jz.value("mode", 0); // 旧データ（modeなし）は固定カメラ扱い
            if ( jz.contains("offset") && jz["offset"].is_array() && jz["offset"].size() >= 3 ) {
                z.offset = { jz["offset"][0], jz["offset"][1], jz["offset"][2] };
            }
            z.yawDeg = jz.value("yawDeg", 180.0f);
            z.dist   = jz.value("dist", 10.0f);
            z.height = jz.value("height", 3.5f);
            z.revert = jz.value("revert", 0); // 旧データは維持(0)扱い
            z.freeze = jz.value("freeze", 0); // 旧データは止めない(0)扱い
            z.fovDeg = jz.value("fovDeg", 45.0f);
            levelData.cameraZones.push_back(z);
        }
    }

    // 敵の配置を読み込む [type, railIndex, distance]
    if ( j.contains("enemies") && j["enemies"].is_array() ) {
        for ( const auto& je : j["enemies"] ) {
            LevelEnemyData e;
            e.type      = je.value("type", 0);
            e.railIndex = je.value("railIndex", 0);
            e.distance  = je.value("distance", 0.0f);
            e.patrol    = je.value("patrol", 0); // 旧データは0=動かない
            levelData.enemies.push_back(e);
        }
    }

    // 万が一レールが1本もない場合は、空のレールを1つ追加しておく
    if ( levelData.railLines.empty() ) {
        levelData.railLines.push_back(std::vector<Vector3>());
    }

    // タイプ・動き・地面フラグ配列を railLines と同じ数に整える（足りない分はデフォルト）
    levelData.railTypes.resize(levelData.railLines.size(), -1);
    levelData.railMotions.resize(levelData.railLines.size(), Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
    levelData.railGroundTypes.resize(levelData.railLines.size(), 0);
    levelData.railVisible.resize(levelData.railLines.size(), 1);
    levelData.railLineModes.resize(levelData.railLines.size(), 0);
    levelData.railRoadModes.resize(levelData.railLines.size(), 0);
    levelData.railMotionTypes.resize(levelData.railLines.size(), 0);
    levelData.railMotionPhases.resize(levelData.railLines.size(), 0.0f);
    levelData.railOneWay.resize(levelData.railLines.size(), 0);
    levelData.railSpeedMuls.resize(levelData.railLines.size(), 1.0f);
    levelData.railNodeHoles.resize(levelData.railLines.size());
    // 各レールの穴配列をノード数に合わせる（足りない分は0=穴なし）
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        levelData.railNodeHoles[i].resize(levelData.railLines[i].size(), 0);
    }

    return levelData;
}