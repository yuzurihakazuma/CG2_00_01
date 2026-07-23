#pragma once
#include <vector>
#include <memory>
#include "engine/math/struct.h"
#include "engine/utils/Level/LevelData.h" // CoinData

class Obj3d;
class SplineRail;

// =====================================================================
//  CoinSystem（収集物）
//   エディタで配置したコイン（レール上の距離＋高さ）を実体化し、
//   プレイ中に触れると取得する。エディット中も常に見える（配置確認のため）。
//   Play開始のたびに取得状態はリセットされる
// =====================================================================
class CoinSystem {
public:
    // エディタのコイン配置とレールから実体を作り直す（レール編集のたびに呼ぶ）
    void Sync(const std::vector<CoinData>& coinDatas, const std::vector<SplineRail>& rails);
    void ResetPlay();                                            // Play開始時：全コイン復活
    void Update(const Vector3& playerPos, bool playMode, float dt);
    void Draw() const;
    int CollectedCount() const{ return collected_; }
    int TotalCount() const{ return ( int ) coins_.size(); }

private:
    struct Coin {
        Vector3 pos {};
        std::unique_ptr<Obj3d> obj;
        bool taken = false;
    };
    std::vector<Coin> coins_;
    int   collected_ = 0;
    float spinT_ = 0.0f; // 全コイン共通の回転タイマー
};
