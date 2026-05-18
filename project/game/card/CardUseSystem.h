#pragma once
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include "engine/math/VectorMath.h"
#include "game/card/HandManager.h"
#include "engine/utils/Level/LevelEditor.h"
#include "engine/collision/Collision.h"
#include "game/card/ICardEffect.h"


// 前方宣言
class Camera;
class Obj3d;
class Player;
class Enemy;
class Boss;
class EnemyManager;
class Minimap;

// カード使用システム
class CardUseSystem {
public:
	// 初期化
	void Initialize(Camera* camera);

	// 更新
	void Update(Player* player, EnemyManager *enemyManager, Boss* boss, Boss* extraBoss,
		const Vector3& playerPos,
		const Vector3& enemyPos,
		const Vector3& bossPos,
		const LevelData& level);

	// 描画
	void Draw();

	// カード使用（ここでは即発動ではなく詠唱開始）
	void UseCard(const Card& card, const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Player* player = nullptr, Boss* casterBoss = nullptr);

	// ★追加：ミニマップを外部（Scene）から受け取るための関数
	void SetMinimap(Minimap *minimap) { minimap_ = minimap; }

	// リセット
	void Reset();

	void CancelCasting();


	bool IsDecoyActive() const;
	Vector3 GetDecoyPosition() const;

private:
	// 実際のカード発動処理
	void ExecuteCard(const Card& card, const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Player* player, Boss* casterBoss);

	// カードごとの詠唱時間
	int GetCastTime(const Card& card) const;

	

	// 「カードID」と「魔法を生み出す関数」の辞書
	std::unordered_map<int, std::function<std::unique_ptr<ICardEffect>(const Card &)>> effectFactory_;

private:
	// -----------------------------
	// 詠唱状態
	// -----------------------------
	bool isCasting_ = false;
	Card castingCard_{ -1, "", 0 };
	Vector3 castPos_{ 0.0f, 0.0f, 0.0f };
	float castYaw_ = 0.0f;
	bool isPlayerCasting_ = true;
	int castTimer_ = 0;
	Player* castingPlayer_ = nullptr;
	// 詠唱中の発動元ボスを保持する
	// 分裂ボス時に左右どちらが使った攻撃かを失わないようにする
	Boss* castingBoss_ = nullptr;
	static constexpr int kCommonCastTime_ = 20;

	
	// 受け取ったミニマップを保持しておく変数
	Minimap *minimap_ = nullptr;

	// カメラを保存しておくための変数
	Camera *camera_ = nullptr;

	// 現在進行中の魔法リスト
	std::vector<std::unique_ptr<ICardEffect>> activeEffects_;

	// 詠唱中の発射元ボスを保持する
	// 分裂ボス時に左右どちらが使った攻撃かを失わないようにする
};
