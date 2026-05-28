#pragma once

#include "engine/audio/AudioManager.h"

namespace GameSE {

	// ==============================================================================
	// サウンドファイルのパス定義
	// ※現在はすべて仮のアセットとして「中パンチ.mp3」が指定されています。
	// ==============================================================================

	// --- システム・UI系 ---
	inline constexpr const char *kCursorMove = "resources/Audio/カーソル移動8.mp3"; // カーソル移動
	inline constexpr const char *kConfirm = "resources/Audio/決定ボタンを押す23.mp3"; // 決定
	inline constexpr const char *kCancel = "resources/Audio/キャンセル4.mp3"; // キャンセル

	// --- カード操作系 ---
	inline constexpr const char *kCardPickup = "resources/Audio/決定ボタンを押す13.mp3"; // カードを拾う
	inline constexpr const char *kCardMove = "resources/Audio/カーソル移動2.mp3"; // カードを移動させる
	inline constexpr const char *kCardUseAttack = "resources/Audio/決定ボタンを押す43.mp3"; // 攻撃カードの使用
	inline constexpr const char *kCardUseMagic = "resources/Audio/中パンチ.mp3"; // 魔法カードの使用
	inline constexpr const char *kCardVanish = "resources/Audio/中パンチ.mp3"; // カードの消滅

	// --- バトル・進行状態系 ---
	inline constexpr const char *kAttackHit = "resources/Audio/打撃2.mp3"; // 攻撃のヒット
	inline constexpr const char *kPlayerDamage = "resources/Audio/小キック.mp3"; // プレイヤーの被弾
	inline constexpr const char *kDodge = "resources/Audio/ジャンプ.mp3"; // 回避アクション
	inline constexpr const char *kStairs = "resources/Audio/アスファルトの上を歩く2.mp3"; // 階段の昇降
	inline constexpr const char *kSceneTransition = "resources/Audio/中パンチ.mp3"; // シーン遷移
	inline constexpr const char *kCostShortage = "resources/Audio/ビープ音4.mp3"; // コスト不足エラー
	inline constexpr const char *kLevelUp = "resources/Audio/レベルアップ.mp3"; // レベルアップ
	inline constexpr const char *kEnemyDeath = "resources/Audio/倒れる.mp3"; // 敵の撃破
	inline constexpr const char *kBossAppear = "resources/Audio/中パンチ.mp3"; // ボスの出現
	inline constexpr const char *kBossDeath = "resources/Audio/大爆発1.mp3"; // ボスの撃破

	// --- 武器・物理攻撃系 ---
	inline constexpr const char *kFist = "resources/Audio/中パンチ.mp3"; // 素手・拳
	inline constexpr const char *kKick = "resources/Audio/中キック.mp3"; // キック
	inline constexpr const char *kSword = "resources/Audio/剣で斬る3.mp3"; // 剣
	inline constexpr const char *kSpear = "resources/Audio/槍.mp3"; // 槍
	inline constexpr const char *kHammer = "resources/Audio/打撃3.mp3"; // ハンマー
	inline constexpr const char *kClaw = "resources/Audio/剣で斬る2.mp3"; // クロー

	// --- 魔法・アイテム・スキル系 ---
	inline constexpr const char *kFireball = "resources/Audio/火炎魔法1.mp3"; // ファイアボール（炎魔法）
	inline constexpr const char *kIce = "resources/Audio/氷魔法で凍結.mp3"; // 氷魔法
	inline constexpr const char *kFang = "resources/Audio/ナイフで切る.mp3"; // トゲ
	inline constexpr const char *kPotion = "resources/Audio/ステータス上昇魔法2.mp3"; // ポーション使用（回復）
	inline constexpr const char *kSpeedUp = "resources/Audio/スピードアップ.mp3"; // スピードアップ（バフ）
	inline constexpr const char *kShield = "resources/Audio/ステータス治療1 (1).mp3"; // シールド展開（防御）
	inline constexpr const char *kCostBoost = "resources/Audio/回復魔法1.mp3"; // コスト回復・ブースト
	inline constexpr const char *kDecoy = "resources/Audio/ワープ.mp3"; // デコイ（囮）配置
	inline constexpr const char *kAtkDown = "resources/Audio/重力魔法1.mp3"; // 攻撃力ダウン（デバフ）
	inline constexpr const char *kScanner = "resources/Audio/魔法反射.mp3"; // マップ表示

	// --- ボス専用アクション系 ---
	inline constexpr const char *kBossCharge = "resources/Audio/中パンチ.mp3"; // ボスの溜め動作
	inline constexpr const char *kBossSummon = "resources/Audio/魔法陣を展開.mp3"; // ボスの手下召喚
	inline constexpr const char *kBossBeam = "resources/Audio/聖魔法.mp3"; // ボスのビーム攻撃


	// ==============================================================================
	// 汎用再生関数
	// ==============================================================================
	/**
	 * @brief SEを再生する基本関数
	 * @param soundPath      音声ファイルのパス
	 * @param volume         音量 (0.0f ~ 1.0f)
	 * @param pitchVariation ピッチ（音程）のランダム変動幅
	 * @param maxPolyphony   最大同時発音数（音が重なりすぎないようにする制限）
	 * @param cooldownSec    連続再生を防ぐためのクールダウン時間（秒）
	 */
	inline void Play(const char *soundPath, float volume, float pitchVariation, int maxPolyphony, float cooldownSec) {
		AudioManager::GetInstance()->PlaySE(soundPath, volume, pitchVariation, maxPolyphony, cooldownSec);
	}


	// ==============================================================================
	// 各SEの再生用ラッパー関数（音量・ピッチ・同時再生数・クールダウン設定済み）
	// ==============================================================================

	// --- システム・UI系 ---
	inline void CursorMove() { Play(kCursorMove, 0.45f, 0.05f, 3, 0.04f); }
	inline void Confirm() { Play(kConfirm, 0.65f, 0.03f, 3, 0.04f); }
	inline void Cancel() { Play(kCancel, 0.55f, 0.02f, 3, 0.08f); }

	// --- カード操作系 ---
	inline void CardPickup() { Play(kCardPickup, 0.70f, 0.05f, 4, 0.02f); }
	inline void CardMove() { Play(kCardMove, 0.45f, 0.05f, 4, 0.03f); }
	inline void CardUseAttack() { Play(kCardUseAttack, 0.80f, 0.04f, 6, 0.02f); }
	inline void CardUseMagic() { Play(kCardUseMagic, 0.78f, 0.08f, 6, 0.02f); }
	inline void CardVanish() { Play(kCardVanish, 0.60f, 0.08f, 4, 0.04f); }

	// --- バトル・進行状態系 ---
	inline void AttackHit() { Play(kAttackHit, 0.85f, 0.06f, 8, 0.015f); }
	inline void PlayerDamage() { Play(kPlayerDamage, 0.90f, 0.04f, 3, 0.08f); }
	inline void Dodge() { Play(kDodge, 0.65f, 0.08f, 3, 0.12f); }
	inline void Stairs() { Play(kStairs, 0.70f, 0.04f, 2, 0.20f); }
	inline void SceneTransition() { Play(kSceneTransition, 0.55f, 0.02f, 2, 0.20f); }
	inline void CostShortage() { Play(kCostShortage, 0.55f, 0.0f, 2, 0.15f); }
	inline void LevelUp() { Play(kLevelUp, 0.90f, 0.05f, 2, 0.10f); }
	inline void EnemyDeath() { Play(kEnemyDeath, 0.85f, 0.08f, 6, 0.03f); }
	inline void BossAppear() { Play(kBossAppear, 0.95f, 0.02f, 2, 0.50f); }
	inline void BossDeath() { Play(kBossDeath, 1.00f, 0.03f, 2, 0.50f); }

	// --- 武器・物理攻撃系 ---
	inline void Fist() { Play(kFist, 0.80f, 0.04f, 6, 0.02f); }
	inline void Kick() { Play(kKick, 0.80f, 0.04f, 6, 0.02f); }
	inline void Sword() { Play(kSword, 0.80f, 0.04f, 6, 0.02f); }
	inline void Spear() { Play(kSpear, 0.80f, 0.04f, 6, 0.02f); }
	inline void Hammer() { Play(kHammer, 0.80f, 0.04f, 6, 0.02f); }
	inline void Claw() { Play(kClaw, 0.80f, 0.04f, 6, 0.02f); }

	// --- 魔法・アイテム・スキル系 ---
	inline void Fireball() { Play(kFireball, 0.78f, 0.08f, 6, 0.02f); }
	inline void Ice() { Play(kIce, 0.78f, 0.08f, 6, 0.02f); }
	inline void Fang() { Play(kFang, 0.78f, 0.08f, 6, 0.02f); }
	inline void Potion() { Play(kPotion, 0.78f, 0.08f, 6, 0.02f); }
	inline void SpeedUp() { Play(kSpeedUp, 0.78f, 0.08f, 6, 0.02f); }
	inline void Shield() { Play(kShield, 0.78f, 0.08f, 6, 0.02f); }
	inline void CostBoost() { Play(kCostBoost, 0.78f, 0.08f, 6, 0.02f); }
	inline void Decoy() { Play(kDecoy, 0.78f, 0.08f, 6, 0.02f); }
	inline void AtkDown() { Play(kAtkDown, 0.78f, 0.08f, 6, 0.02f); }
	inline void Scanner() { Play(kScanner, 0.78f, 0.08f, 6, 0.02f); }

	// --- ボス専用アクション系 ---
	inline void BossCharge() { Play(kBossCharge, 0.85f, 0.08f, 6, 0.02f); }
	inline void BossSummon() { Play(kBossSummon, 0.85f, 0.08f, 6, 0.02f); }
	inline void BossBeam() { Play(kBossBeam, 0.85f, 0.08f, 6, 0.02f); }

}