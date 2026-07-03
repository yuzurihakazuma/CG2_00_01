#pragma once

class Player;
class EnemyManager;
class EggSystem;
class HitFeel;

// =====================================================================
//  SwallowAbility：ヨッシーの「飲み込む／産む」アクション。
//   ・E      … 一番近い敵の飲み込みを開始（敵が縮みながらプレイヤーへ吸い込まれ→お腹へ）
//   ・左Ctrl … しゃがんでお腹の敵を1匹「卵」として後ろに産む
//  シーンから分離した理由：入力→対象選択→開始通知だけの独立したアクションで、
//  他シーンでも同じ組み合わせで再利用できる。
// =====================================================================
class SwallowAbility {
public:
    // 毎フレーム（Play中のみ）呼ぶ
    void Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel);

    // ノードエディタの「→ ゲーム値」用（舌の届く距離を外から調整できる）
    float* SwallowReachPtr(){ return &swallowReach_; }

private:
    float swallowReach_ = 2.0f; // 飲み込みの届く距離 (m)
};
