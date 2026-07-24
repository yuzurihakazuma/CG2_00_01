#include "LevelManager.h"
#include "externals/nlohmann/json.hpp"
#include <fstream>
#include <cassert>

using json = nlohmann::json;

void LevelManager::Save(const std::string& fileName, const LevelData& levelData){
    json rootJson;
    rootJson["name"] = levelData.name;

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
    rootJson["railLines"] = linesArray;

    // 各レールのタイプ（-1=自動 / 0=横 / 1=縦）。railLines と数を合わせて保存
    json typesArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int railType = ( i < levelData.railTypes.size() ) ? levelData.railTypes[i] : -1;
        typesArray.push_back(railType);
    }
    rootJson["railTypes"] = typesArray;

    // 各レールの動き [振幅x, 振幅y, 振幅z, 周期]。railLines と数を合わせて保存
    json motionsArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        Vector4 motion = ( i < levelData.railMotions.size() ) ? levelData.railMotions[i] : Vector4 { 0.0f, 0.0f, 0.0f, 2.0f };
        motionsArray.push_back({ motion.x, motion.y, motion.z, motion.w });
    }
    rootJson["railMotions"] = motionsArray;

    // 各レールの地面タイプ(0=Safe/1=Gap/2=NoGround)。railLines と数を合わせて保存
    json groundArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int groundType = ( i < levelData.railGroundTypes.size() ) ? levelData.railGroundTypes[i] : 0;
        groundArray.push_back(groundType);
    }
    rootJson["railGroundTypes"] = groundArray;

    // 各レールのノード単位の穴指定（外=レール / 内=ノード・1=穴）
    json holesArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        json holeArray = json::array();
        if ( i < levelData.railNodeHoles.size() ) {
            for ( int holeFlag : levelData.railNodeHoles[i] ) holeArray.push_back(holeFlag);
        }
        holesArray.push_back(holeArray);
    }
    rootJson["railNodeHoles"] = holesArray;

    // 各レールの表示フラグ(1=表示/0=非表示)。railLines と数を合わせて保存
    json visibleArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int visibleFlag = ( i < levelData.railVisible.size() ) ? levelData.railVisible[i] : 1;
        visibleArray.push_back(visibleFlag);
    }
    rootJson["railVisible"] = visibleArray;

    // 各レールの線のつなぎ方(0=スプライン/1=直線)。railLines と数を合わせて保存
    json lineModeArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int lineMode = ( i < levelData.railLineModes.size() ) ? levelData.railLineModes[i] : 0;
        lineModeArray.push_back(lineMode);
    }
    rootJson["railLineModes"] = lineModeArray;

    // 各レールの道生成モード(0=自動/1=なし)。railLines と数を合わせて保存
    json roadModeArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int roadMode = ( i < levelData.railRoadModes.size() ) ? levelData.railRoadModes[i] : 0;
        roadModeArray.push_back(roadMode);
    }
    rootJson["railRoadModes"] = roadModeArray;

    // 各レールの端の丸広場（bit0=始点/bit1=終点）。railLines と数を合わせて保存
    json endPlazaArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int endPlaza = ( i < levelData.railEndPlazas.size() ) ? levelData.railEndPlazas[i] : 0;
        endPlazaArray.push_back(endPlaza);
    }
    rootJson["railEndPlazas"] = endPlazaArray;

    // ガイドレール追従の対象レール番号（railLines と数を合わせて保存）
    json guideRailArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        int guideRail = ( i < levelData.railGuideRails.size() ) ? levelData.railGuideRails[i] : -1;
        guideRailArray.push_back(guideRail);
    }
    rootJson["railGuideRails"] = guideRailArray;

    // 路線のグループ名（railLines と数を合わせて保存）
    json groupArray = json::array();
    for ( size_t i = 0; i < levelData.railLines.size(); ++i ) {
        groupArray.push_back(( i < levelData.railGroups.size() ) ? levelData.railGroups[i] : "");
    }
    rootJson["railGroups"] = groupArray;

    // 収集物（コイン）の配置
    json coinArray = json::array();
    for ( const auto& coin : levelData.coins ) {
        coinArray.push_back({ { "rail", coin.rail }, { "dist", coin.dist }, { "height", coin.height } });
    }
    rootJson["coins"] = coinArray;

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
    rootJson["railMotionTypes"]  = motionTypeArray;
    rootJson["railMotionPhases"] = motionPhaseArray;
    rootJson["railOneWay"]       = oneWayArray;
    rootJson["railSpeedMuls"]    = speedMulArray;

    // スタート/ゴール地点
    rootJson["startRailIndex"] = levelData.startRailIndex;
    rootJson["startNodeIndex"] = levelData.startNodeIndex;
    rootJson["goalRailIndex"]  = levelData.goalRailIndex;
    rootJson["goalNodeIndex"]  = levelData.goalNodeIndex;

    // カメラ演出ゾーン
    json camArray = json::array();
    for ( const auto& zone : levelData.cameraZones ) {
        camArray.push_back({
            { "railIndex", zone.railIndex }, { "nodeIndex", zone.nodeIndex },
            { "radius", zone.radius },
            { "mode", zone.mode },
            { "offset", { zone.offset.x, zone.offset.y, zone.offset.z } },
            { "yawDeg", zone.yawDeg }, { "dist", zone.dist }, { "height", zone.height },
            { "revert", zone.revert }, { "freeze", zone.freeze },
            { "fovDeg", zone.fovDeg },
        });
    }
    rootJson["cameraZones"] = camArray;

    // 敵の配置 [type, railIndex, distance]
    json enemiesArray = json::array();
    for ( const auto& enemy : levelData.enemies ) {
        json enemyJson;
        enemyJson["type"]      = enemy.type;
        enemyJson["railIndex"] = enemy.railIndex;
        enemyJson["distance"]  = enemy.distance;
        enemyJson["patrol"]    = enemy.patrol;
        enemyJson["patrolMin"] = enemy.patrolMin;
        enemyJson["patrolMax"] = enemy.patrolMax;
        enemiesArray.push_back(enemyJson);
    }
    rootJson["enemies"] = enemiesArray;

    rootJson["objects"] = objectsArray;

    // ファイルに書き込み
    std::ofstream file(fileName);
    if ( file.is_open() ) {
        // dump(4) は見やすくするためにインデント（空白）を4つ入れる設定です
        file << rootJson.dump(4);
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

    json rootJson;
    file >> rootJson;

    // マップ名の読み込み（無い場合は "Unknown" にする）
    levelData.name = rootJson.value("name", "Unknown");

    // オブジェクトの配列を読み込む
    if ( rootJson.contains("objects") && rootJson["objects"].is_array() ) {
        for ( const auto& jsonObj : rootJson["objects"] ) {
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
    if ( rootJson.contains("railLines") && rootJson["railLines"].is_array() ) {
        for ( const auto& lineObj : rootJson["railLines"] ) {
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
    else if ( rootJson.contains("railNodes") && rootJson["railNodes"].is_array() ) {
        std::vector<Vector3> line;
        for ( const auto& posObj : rootJson["railNodes"] ) {
            Vector3 pos;
            pos.x = posObj[0]; pos.y = posObj[1]; pos.z = posObj[2];
            line.push_back(pos);
        }
        levelData.railLines.push_back(line);
    }

    // レールのタイプを読み込む（-1=自動 / 0=横 / 1=縦）
    if ( rootJson.contains("railTypes") && rootJson["railTypes"].is_array() ) {
        for ( const auto& railTypeJson : rootJson["railTypes"] ) {
            levelData.railTypes.push_back(railTypeJson.get<int>());
        }
    }

    // レールの動きを読み込む [振幅x, 振幅y, 振幅z, 周期]
    if ( rootJson.contains("railMotions") && rootJson["railMotions"].is_array() ) {
        for ( const auto& motionJson : rootJson["railMotions"] ) {
            Vector4 motion { 0.0f, 0.0f, 0.0f, 2.0f };
            if ( motionJson.is_array() && motionJson.size() >= 4 ) {
                motion.x = motionJson[0]; motion.y = motionJson[1]; motion.z = motionJson[2]; motion.w = motionJson[3];
            }
            levelData.railMotions.push_back(motion);
        }
    }

    // 地面タイプを読み込む（0=Safe/1=Gap/2=NoGround。無ければデフォルト0=Safe）
    if ( rootJson.contains("railGroundTypes") && rootJson["railGroundTypes"].is_array() ) {
        for ( const auto& groundTypeJson : rootJson["railGroundTypes"] ) {
            levelData.railGroundTypes.push_back(groundTypeJson.get<int>());
        }
    }

    // ノード単位の穴指定を読み込む（外=レール / 内=ノード）
    if ( rootJson.contains("railNodeHoles") && rootJson["railNodeHoles"].is_array() ) {
        for ( const auto& holeArrayJson : rootJson["railNodeHoles"] ) {
            std::vector<int> holes;
            if ( holeArrayJson.is_array() ) {
                for ( const auto& holeJson : holeArrayJson ) holes.push_back(holeJson.get<int>());
            }
            levelData.railNodeHoles.push_back(holes);
        }
    }

    // 表示フラグを読み込む（1=表示/0=非表示。無ければデフォルト1=表示）
    if ( rootJson.contains("railVisible") && rootJson["railVisible"].is_array() ) {
        for ( const auto& visibleJson : rootJson["railVisible"] ) {
            levelData.railVisible.push_back(visibleJson.get<int>());
        }
    }

    // 線のつなぎ方を読み込む（0=スプライン/1=直線。キーが無い旧JSONは後段の resize で全て0=スプライン）
    if ( rootJson.contains("railLineModes") && rootJson["railLineModes"].is_array() ) {
        for ( const auto& lineModeJson : rootJson["railLineModes"] ) {
            levelData.railLineModes.push_back(lineModeJson.get<int>());
        }
    }

    // 道生成モードを読み込む（0=自動/1=なし。キーが無い旧JSONは後段の resize で全て0=自動になる）
    if ( rootJson.contains("railGroups") && rootJson["railGroups"].is_array() ) {
        for ( const auto& groupJson : rootJson["railGroups"] ) {
            levelData.railGroups.push_back(groupJson.get<std::string>());
        }
    }
    if ( rootJson.contains("railGuideRails") && rootJson["railGuideRails"].is_array() ) {
        for ( const auto& guideRailJson : rootJson["railGuideRails"] ) {
            levelData.railGuideRails.push_back(guideRailJson.get<int>());
        }
    }
    if ( rootJson.contains("railEndPlazas") && rootJson["railEndPlazas"].is_array() ) {
        for ( const auto& endPlazaJson : rootJson["railEndPlazas"] ) {
            levelData.railEndPlazas.push_back(endPlazaJson.get<int>());
        }
    }
    // 収集物（コイン）を読み込む（キーが無い旧JSONはコインなし）
    if ( rootJson.contains("coins") && rootJson["coins"].is_array() ) {
        for ( const auto& coinJson : rootJson["coins"] ) {
            CoinData coin;
            coin.rail   = coinJson.value("rail", 0);
            coin.dist   = coinJson.value("dist", 0.0f);
            coin.height = coinJson.value("height", 1.0f);
            levelData.coins.push_back(coin);
        }
    }
    if ( rootJson.contains("railRoadModes") && rootJson["railRoadModes"].is_array() ) {
        for ( const auto& roadModeJson : rootJson["railRoadModes"] ) {
            levelData.railRoadModes.push_back(roadModeJson.get<int>());
        }
    }

    // 動き波形/位相・片方向・速度倍率を読み込む（無ければ後段の resize でデフォルトが入る）
    if ( rootJson.contains("railMotionTypes") && rootJson["railMotionTypes"].is_array() ) {
        for ( const auto& motionTypeJson : rootJson["railMotionTypes"] ) { levelData.railMotionTypes.push_back(motionTypeJson.get<int>()); }
    }
    if ( rootJson.contains("railMotionPhases") && rootJson["railMotionPhases"].is_array() ) {
        for ( const auto& motionPhaseJson : rootJson["railMotionPhases"] ) { levelData.railMotionPhases.push_back(motionPhaseJson.get<float>()); }
    }
    if ( rootJson.contains("railOneWay") && rootJson["railOneWay"].is_array() ) {
        for ( const auto& oneWayJson : rootJson["railOneWay"] ) { levelData.railOneWay.push_back(oneWayJson.get<int>()); }
    }
    if ( rootJson.contains("railSpeedMuls") && rootJson["railSpeedMuls"].is_array() ) {
        for ( const auto& speedMulJson : rootJson["railSpeedMuls"] ) { levelData.railSpeedMuls.push_back(speedMulJson.get<float>()); }
    }

    // スタート/ゴール地点（無ければデフォルト：スタート=レール0ノード0 / ゴール=未設定）
    levelData.startRailIndex = rootJson.value("startRailIndex", 0);
    levelData.startNodeIndex = rootJson.value("startNodeIndex", 0);
    levelData.goalRailIndex  = rootJson.value("goalRailIndex", -1);
    levelData.goalNodeIndex  = rootJson.value("goalNodeIndex", 0);

    // カメラ演出ゾーン
    if ( rootJson.contains("cameraZones") && rootJson["cameraZones"].is_array() ) {
        for ( const auto& zoneJson : rootJson["cameraZones"] ) {
            LevelCameraZone zone;
            zone.railIndex = zoneJson.value("railIndex", 0);
            zone.nodeIndex = zoneJson.value("nodeIndex", 0);
            zone.radius    = zoneJson.value("radius", 4.0f);
            zone.mode      = zoneJson.value("mode", 0); // 旧データ（modeなし）は固定カメラ扱い
            if ( zoneJson.contains("offset") && zoneJson["offset"].is_array() && zoneJson["offset"].size() >= 3 ) {
                zone.offset = { zoneJson["offset"][0], zoneJson["offset"][1], zoneJson["offset"][2] };
            }
            zone.yawDeg = zoneJson.value("yawDeg", 180.0f);
            zone.dist   = zoneJson.value("dist", 10.0f);
            zone.height = zoneJson.value("height", 3.5f);
            zone.revert = zoneJson.value("revert", 0); // 旧データは維持(0)扱い
            zone.freeze = zoneJson.value("freeze", 0); // 旧データは止めない(0)扱い
            zone.fovDeg = zoneJson.value("fovDeg", 45.0f);
            levelData.cameraZones.push_back(zone);
        }
    }

    // 敵の配置を読み込む [type, railIndex, distance]
    if ( rootJson.contains("enemies") && rootJson["enemies"].is_array() ) {
        for ( const auto& enemyJson : rootJson["enemies"] ) {
            LevelEnemyData enemy;
            enemy.type      = enemyJson.value("type", 0);
            enemy.railIndex = enemyJson.value("railIndex", 0);
            enemy.distance  = enemyJson.value("distance", 0.0f);
            enemy.patrol    = enemyJson.value("patrol", 0); // 旧データは0=動かない
            enemy.patrolMin = enemyJson.value("patrolMin", -1.0f); // 旧データは-1=レール全体
            enemy.patrolMax = enemyJson.value("patrolMax", -1.0f);
            levelData.enemies.push_back(enemy);
        }
    }

    // 万が一レールが1本もない場合は、空のレールを1つ追加しておく
    if ( levelData.railLines.empty() ) {
        levelData.railLines.push_back(std::vector<Vector3>());
    }

    // タイプ・動き・地面フラグ配列を railLines と同じ数に整える（足りない分はデフォルト）
    levelData.railTypes.resize(levelData.railLines.size(), -1);
    levelData.railMotions.resize(levelData.railLines.size(), Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
    levelData.railGroundTypes.resize(levelData.railLines.size(), 1); // 既定は Gap（端から落ちられる）
    levelData.railVisible.resize(levelData.railLines.size(), 1);
    levelData.railLineModes.resize(levelData.railLines.size(), 0);
    levelData.railRoadModes.resize(levelData.railLines.size(), 0);
    levelData.railEndPlazas.resize(levelData.railLines.size(), 0);
    levelData.railGuideRails.resize(levelData.railLines.size(), -1);
    levelData.railGroups.resize(levelData.railLines.size());
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