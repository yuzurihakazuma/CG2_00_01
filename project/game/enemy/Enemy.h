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
class SkinnedObj3d;
class SplineRail;
class BlockSystem;

// 敵の種類（エディタから選択可能）。保存データは int なので既存の並びは変えないこと
enum class EnemyType{
    Zako,   // 地上「ドングリン」：よちよち歩き（Idle/Walk）
    Strong, // 植物「カミバナ」：その場でゆらゆら、近づくと噛みつく（Idle/Bite）
    Air,    // 空中「フワリン」：レールの上を浮遊しながら羽ばたく（Fly）
};

class Enemy{
public:
    Enemy();
    ~Enemy();

    // 種類・乗せるレール番号・初期位置(レール上の距離)を指定して生成する。
    //   patrol=true の時だけレールを往復する（既定は置いた場所に留まる）。
    //   patrolMin/Max で往復範囲を距離(m)で絞れる（-1=レール全体）
    void Initialize(EnemyType type, int railIndex, float distance, bool patrol = false,
                    float patrolMin = -1.0f, float patrolMax = -1.0f);

    // レール上を移動（パトロールON時のみ往復）。位置・向き・コライダー・アニメを更新する。
    //   playerPos はカミバナ（植物）の噛みつき発動と向きの制御に使う。
    //   blocks を渡すと、進行方向にブロックがある時に引き返す（貫通防止。nullptr=判定なし）
    void Update(const std::vector<SplineRail>& rails, const Vector3& playerPos, float dt,
                const BlockSystem* blocks = nullptr);

    // 描画（Obj3dCommon::PreDraw 済みの状態で呼ぶ）
    void Draw();

    // --- 状態 ---
    bool IsAlive() const{ return alive_ && !swallowing_; } // 吸い込み中は踏み・ロック対象外
    void Defeat(){ alive_ = false; }   // 踏まれた時など

    // --- 飲み込み（ヨッシー）：その場から縮みながらプレイヤーへ吸い込まれる ---
    void StartSwallow();                                  // 吸い込み開始
    void TickSwallow(const Vector3& playerPos, float dt); // 吸い込み中の更新（縮小＋移動）
    bool IsSwallowing() const{ return swallowing_; }
    bool IsConsumed()  const{ return consumed_; }         // 吸い込み完了（消してお腹+1）

    // 見た目だけ非表示にする（舌で捕まえた瞬間、SDF溶解演出に差し替えるため使う。
    //   position_/collider_ 等の内部状態はそのまま動き続ける＝実体は生きている）
    void SetVisualHidden(bool hidden){ visualHidden_ = hidden; }

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
    bool      patrol_    = false;   // true=レールを往復 / false=その場に留まる（既定）
    float     patrolMin_ = -1.0f;   // 往復範囲の始点(m)。-1=レール全体
    float     patrolMax_ = -1.0f;   // 往復範囲の終点(m)。-1=レール全体
    bool      alive_     = true;

    // 飲み込みアニメ用
    bool      swallowing_ = false;        // 吸い込み中
    bool      consumed_   = false;        // 吸い込み完了（シーンが消してお腹を増やす）
    float     swallowT_   = 0.0f;         // 吸い込み経過(秒)
    Vector3   swallowStart_ { 0.0f, 0.0f, 0.0f }; // 吸い込み開始位置
    bool      visualHidden_ = false;      // true の間は Draw をスキップ（SDF演出に差し替え中）

    Vector3 position_ { 0.0f, 0.0f, 0.0f };
    Vector3 rotation_ { 0.0f, 0.0f, 0.0f };
    float   radius_   = 0.6f;       // 見た目＆当たり判定の半径

    std::unique_ptr<SkinnedObj3d> skinnedObj_; // 見た目（リグ+クリップ入りの敵モデル）
    std::unique_ptr<Obj3d> obj_;    // フォールバックの見た目（モデル未登録時のsphere）
    float biteTimer_ = 0.0f;        // カミバナ：噛みつきモーションの残り時間（0=Idle中）
    float turnCooldown_ = 0.0f;     // ブロックにぶつかって折り返した直後の再判定待ち（振動防止）
    Collider collider_;             // 当たり判定（球）
};
