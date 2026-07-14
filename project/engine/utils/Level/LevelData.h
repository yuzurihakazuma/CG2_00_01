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
    int   patrol = 0; // 1=レールを往復パトロール（旧データはフィールド無し=0で動かない）
};

// レール上のカメラ演出ゾーン1個分のデータ（マップ保存用）。
//   mode=0（固定カメラ）: 半径内にいる間だけ「アンカー+offset」からプレイヤーを見る。離れると戻る。
//   mode=1（向き切替）  : 通過した瞬間に追従カメラの向き(yaw)・距離・高さが切り替わり、
//                         次のトリガーまで【維持】される（180度回してそのままゲーム続行、等）。
struct LevelCameraZone{
    int     railIndex = 0;              // アンカーのレール番号
    int     nodeIndex = 0;              // アンカーのノード番号
    float   radius    = 4.0f;           // 発動する半径 (m)
    int     mode      = 1;              // 0=固定カメラ / 1=追従の向き切替（維持）
    // --- mode=0 用 ---
    Vector3 offset { 0.0f, 3.0f, -6.0f }; // アンカーからのカメラ位置オフセット
    // --- mode=1 用 ---
    float   yawDeg    = 180.0f;         // カメラの向き（0=後ろから / 180=正面から / ±90=横から）
    float   dist      = 10.0f;          // プレイヤーからの距離 (m)
    float   height    = 3.5f;           // プレイヤーからの高さ (m)
    int     revert    = 0;              // 1=半径から出たら通常の向きへ戻る / 0=次のトリガーまで維持
    int     freeze    = 0;              // 1=回転が終わるまで時間を止める / 0=動きながら回す
    // --- 共通 ---
    float   fovDeg    = 45.0f;          // 視野角（度）。45=標準
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

    // 各レールの地面タイプ（railLines と同じ並び・同じ要素数を維持する）
    //   0 = Safe（安全） / 1 = Gap（穴：飛び出し可） / 2 = NoGround（地面なし：即落下）
    std::vector<int> railGroundTypes;

    // 各レールの「ノード単位の穴指定」（外= railLines と同じ並び / 内= 各レールのノードと同じ並び・1=穴）
    std::vector<std::vector<int>> railNodeHoles;

    // 各レールの表示フラグ（railLines と同じ並び）。1=表示 / 0=非表示（連結用の見えないレール）
    std::vector<int> railVisible;

    // 各レールの線のつなぎ方（railLines と同じ並び）。0=スプライン(なめらか) / 1=直線(カクカク)
    std::vector<int> railLineModes;

    // 各レールの動きの波形（railLines と同じ並び）
    //   0=サイン往復 / 1=端で一時停止つき往復 / 2=円運動
    std::vector<int> railMotionTypes;
    // 各レールの動きの位相（0〜1。複数レールの動きをずらす）
    std::vector<float> railMotionPhases;

    // 各レールの片方向設定（0=両方向 / 1=正方向のみ(front→back) / 2=逆方向のみ）
    std::vector<int> railOneWay;

    // 各レールの移動速度倍率（1=通常。2で加速レール、0.5で減速レール）
    std::vector<float> railSpeedMuls;

    // スタート地点（レール番号＋ノード番号。プレイヤーの開始・リスポーン先）
    int startRailIndex = 0;
    int startNodeIndex = 0;

    // ゴール地点（レール番号＋ノード番号）。railIndex=-1 なら未設定
    int goalRailIndex = -1;
    int goalNodeIndex = 0;

    // レール上のカメラ演出ゾーン（プレイヤーが近づくとカメラが指定位置・画角になる）
    std::vector<LevelCameraZone> cameraZones;

    // 敵の配置（レール上に置く敵。マップと一緒に保存/読込する）
    std::vector<LevelEnemyData> enemies;


    // 必要ならタイル情報も残してOK
    int width = 10;
    int height = 10;
    float tileSize = 2.0f;
    float baseY = -2.0f;
    std::vector<std::vector<int>> tiles;
};