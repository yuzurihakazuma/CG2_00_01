#pragma once
#include "game/enemy/Enemy.h"
#include <vector>

// 敵をレール上に配置するための情報（テンプレートデータ）
struct EnemySpawnData {
    EnemyType type;     // 敵の種類
    int railIndex;      // 配置先のレール番号
    float distance;     // レール上の初期位置(メートル)
};

class SplineRail;

// =====================================================================
//  EnemyEditor : エネミーのレール配置データを管理し、ImGui UI を提供する
// =====================================================================
class EnemyEditor {
public:
    EnemyEditor();
    ~EnemyEditor();

    // 初期化：デフォルトの敵データを登録する
    void Initialize();

    // ImGui ウィンドウの描画
    void DrawWindow(const std::vector<SplineRail>& splineRails);

    // 配置情報の取得
    const std::vector<EnemySpawnData>& GetSpawnDatas() const { return spawnDatas_; }

    // 配置データに変更があったかどうか（取得するとフラグはリセットされる）
    bool ConsumeChanged() { bool c = changed_; changed_ = false; return c; }

private:
    std::vector<EnemySpawnData> spawnDatas_; // 登録されたエネミー配置データの配列

    // 配置データが変更されたことを示すフラグ（シーン側が検知してリスポーンする）
    bool changed_ = false;

    // ImGui UI 操作用の一時的な入力値バッファ
    int   spawnEnemyTypeIdx_ = 0; // 選択中の敵タイプインデックス (0: Zako, 1: Strong)
    int   spawnRailIndex_    = 0; // 選択中のレールインデックス
    float spawnDistance_     = 0.0f; // 選択中のレール上距離(m)

    // 一覧で現在選択中のエントリ（インライン編集対象）
    int selectedEntry_ = -1;

    // ドラッグ＆ドロップ用：ドラッグ中のエントリ番号（-1=ドラッグしていない）
    int dragSourceIdx_ = -1;

    // 敵タイプ名の取得ヘルパー（短縮版と表示版）
    static const char* GetTypeName(EnemyType type);
    static const char* GetTypeLabel(EnemyType type);
};
