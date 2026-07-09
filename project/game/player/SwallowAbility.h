#pragma once

class Player;
class EnemyManager;
class EggSystem;
class HitFeel;

// =====================================================================
//  SwallowAbility：ヨッシーの「飲み込む／産む／吐き出す」アクション。
//   ・E      … 一番近い敵の飲み込みを開始（敵が縮みながらプレイヤーへ吸い込まれ→お腹へ）
//   ・左Ctrl … しゃがんでお腹の敵を1匹「卵」として後ろに産む
//   ・F      … お腹の敵を1匹、向いている方向へ吐き出す（敵にぶつけて倒せる）
//  シーンから分離した理由：入力→対象選択→開始通知だけの独立したアクションで、
//  他シーンでも同じ組み合わせで再利用できる。
// =====================================================================
class SwallowAbility {
public:
    // 毎フレーム（Play中のみ）呼ぶ。dt はクールタイムの経過用
    void Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel, float dt);

    // ノードエディタの「→ ゲーム値」用（舌の届く距離・再使用間隔を外から調整できる）
    float* SwallowReachPtr(){ return &swallowReach_; }
    float* SwallowCooldownPtr(){ return &swallowCooldown_; }

private:
    float swallowReach_    = 2.0f;  // 飲み込みの届く距離 (m)
    float swallowCooldown_ = 0.35f; // 飲み込み成功後の再使用間隔 (秒)。連打での吸い込み連発を防ぐ
    float cooldownTimer_   = 0.0f;  // 残りクールタイム
};
