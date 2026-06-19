#pragma once
// =====================================================================
//  Enemy : レール上を動く敵（プレイヤーが上から踏んで倒せる予定）。
//   ・railIndex のレール上を distance(進んだ距離) ベースで往復する
//   ・位置はレール上に乗せる（GetPositionByDistance + 半径ぶん上へ）
//   ・当たり判定用に Collider を持つ（踏みつけ判定は後でシーン側で行う）
// =====================================================================
#include "engine/math/struct.h"
#include "engine/collision/Collider.h"

#include <memory>
#include <vector>

class Obj3d;
class SplineRail;

// 敵の種類（エディタから選択可能）
enum class EnemyType{
    Zako,   // 雑魚：レール上を往復するだけ
    Strong, // 強敵：少しサイズが大きく、見た目が異なる
};

class Enemy{
public:
    Enemy();
    ~Enemy();

    // 種類・乗せるレール番号・初期位置(レール上の距離)を指定して生成する
    void Initialize(EnemyType type, int railIndex, float distance);

    // レール上を移動（往復）。位置・向き・コライダーを更新する
    void Update(const std::vector<SplineRail>& rails, float dt);

    // 描画（Obj3dCommon::PreDraw 済みの状態で呼ぶ）
    void Draw();

    // --- 状態 ---
    bool IsAlive() const{ return alive_; }
    void Defeat(){ alive_ = false; }   // 踏まれた時など

    const Vector3& GetPosition() const{ return position_; }
    float     GetRadius() const{ return radius_; }
    Collider* GetCollider(){ return &collider_; }
    EnemyType GetType() const{ return type_; }
    int       GetRailIndex() const{ return railIndex_; }
    float     GetDistance() const{ return distance_; }

private:
    EnemyType type_     = EnemyType::Zako;
    int       railIndex_ = 0;       // 乗っているレール番号
    float     distance_  = 0.0f;    // レール上を進んだ距離 (m)
    float     dir_       = 1.0f;    // 進行方向(+1/-1)
    float     speed_     = 2.0f;    // 移動速度 (m/s)
    bool      alive_     = true;

    Vector3 position_ { 0.0f, 0.0f, 0.0f };
    Vector3 rotation_ { 0.0f, 0.0f, 0.0f };
    float   radius_   = 0.6f;       // 見た目＆当たり判定の半径

    std::unique_ptr<Obj3d> obj_;    // 見た目
    Collider collider_;             // 当たり判定（球）
};
