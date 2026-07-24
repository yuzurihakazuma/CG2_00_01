#pragma once
#include "game/enemy/Enemy.h"
#include <vector>

// 敵をレール上に配置するための情報（テンプレートデータ）
struct EnemySpawnData {
    EnemyType type;      // 敵の種類
    int railIndex;       // 配置先のレール番号
    float distance;      // レール上の初期位置(メートル)
    bool patrol = false; // true=レールを往復パトロール / false=置いた場所に留まる（既定）
    float patrolMin = -1.0f; // 巡回範囲の始点(m)。-1=レール全体を往復
    float patrolMax = -1.0f; // 巡回範囲の終点(m)。-1=レール全体を往復
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

    // ImGui ウィンドウの描画。
    //   pickRail/pickDist: レールエディタで選択中ノードのレール番号と距離（「選択ノードに配置」用）。
    //   hasPick=false なら未選択（ボタンはグレーアウト）
    void DrawWindow(const std::vector<SplineRail>& splineRails,
                    int pickRail = -1, float pickDist = 0.0f, bool hasPick = false);

    // Game View ハイライト用：一覧でホバー中/選択中のエントリ番号（-1=なし）
    int GetHoveredEntry() const { return hoveredEntry_; }
    int GetSelectedEntry() const { return selectedEntry_; }
    // Game View 直接ドラッグ用：選択の同期と、MutableSpawnDatas 直編集後の確定通知
    void SetSelectedEntry(int i) { selectedEntry_ = i; }
    void MarkChanged() { changed_ = true; }

    // 配置情報の取得
    const std::vector<EnemySpawnData>& GetSpawnDatas() const { return spawnDatas_; }

    // レール編集時の「距離の張り直し」用に直接書き換えるアクセサ。
    //   SetSpawnDatas と違い選択状態や changed_ フラグを触らない（UI の選択が飛ばない）。
    std::vector<EnemySpawnData>& MutableSpawnDatas() { return spawnDatas_; }

    // 配置情報の差し替え（マップ読込時に外部データで上書きする）
    void SetSpawnDatas(const std::vector<EnemySpawnData>& datas) {
        spawnDatas_ = datas;
        selectedEntry_ = -1;
        changed_ = true; // シーン側がリスポーンするよう変更フラグを立てる
    }

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
    bool  spawnPatrol_       = false; // 新規配置でパトロールを付けるか（既定=動かない）

    // 一覧で現在選択中のエントリ（インライン編集対象）
    int selectedEntry_ = -1;

    // 一覧でホバー中のエントリ（Game View ハイライト用。毎フレーム取り直す）
    int hoveredEntry_ = -1;

    // ドラッグ＆ドロップ用：ドラッグ中のエントリ番号（-1=ドラッグしていない）
    int dragSourceIdx_ = -1;

    // 敵タイプ名の取得ヘルパー（短縮版と表示版）
    static const char* GetTypeName(EnemyType type);
    static const char* GetTypeLabel(EnemyType type);
};
