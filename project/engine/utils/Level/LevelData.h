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


    // 必要ならタイル情報も残してOK
    int width = 10;
    int height = 10;
    float tileSize = 2.0f;
    float baseY = -2.0f;
    std::vector<std::vector<int>> tiles;
};