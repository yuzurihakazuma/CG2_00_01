#pragma once
#include <string>
#include <vector>

// 使っているVector3の定義に合わせて include を調整してください
#include "engine/math/struct.h"

// 配置オブジェクト1個分のデータ
struct LevelObjectData{
    std::string type;
    Vector3 translation { 0.0f, 0.0f, 0.0f };
    Vector3 rotation { 0.0f, 0.0f, 0.0f };
    Vector3 scale { 1.0f, 1.0f, 1.0f };
};

// 敵の配置1体分のデータ（マップ保存用）。
//   type は game 側 EnemyType と対応する整数(0=Zako, 1=Strong ...)。
//   engine が game の enum に依存しないよう、ここでは int で持つ。
struct LevelEnemyData{
    int   type = 0;
    int   railIndex = 0;
    float distance = 0.0f;
};

// マップ全体のデータ
struct LevelData{
    std::string name;

    // オブジェクト配置型レベルデータ
    std::vector<LevelObjectData> objects;

	// レール型レベルデータ
    std::vector<std::vector<Vector3>> railLines;

    // 各レールのタイプ（railLines と同じ並び・同じ要素数を維持する）
    //   -1 = 自動判定 / 0 = 横(A/D移動) / 1 = 縦(W/S移動)
    std::vector<int> railTypes;

    // 各レールの動き（railLines と同じ並び・同じ要素数を維持する）
    //   x,y,z = sin波の振幅(m)（全て0なら動かない） / w = 周期(秒)
    std::vector<Vector4> railMotions;

    // 各レールの地面フラグ（railLines と同じ並び・同じ要素数を維持する）
    //   true = 端で落ちない安全レール / false = 端から飛び出せるアクションレール
    std::vector<bool> railHasGround;

    // 敵の配置（レール上に置く敵。マップと一緒に保存/読込する）
    std::vector<LevelEnemyData> enemies;


    // 必要ならタイル情報も残してOK
    int width = 10;
    int height = 10;
    float tileSize = 2.0f;
    float baseY = -2.0f;
    std::vector<std::vector<int>> tiles;
};