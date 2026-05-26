#pragma once
#include <string>
#include <unordered_map>

//---------効果の種類---------
enum class CardEffectType {
	Attack,     //攻撃
	Heal,       //回復
	Defense,    //防御
	Special     //特殊
};

//---------レア度の種類---------
enum class CardRarity {
	Common = 0,    // 通常
	Rare = 1,      // レア
	Epic = 2,      // エピック
	Legendary = 3  // レジェンダリー
};

//---------攻撃距離の種類---------
enum class CardAttackRangeType {
	Melee = 0, // 近距離
	Mid = 1,   // 中距離
	Long = 2   // 遠距離
};

//---------カードのデータ構造---------
struct Card {
	int id;                    //ID
	std::string name;          //カード名
	int cost;                  //コスト
	CardEffectType effectType; // 効果の種類
	int effectValue;           // 効果の数値（ダメージ量など）
	std::string description;   // 説明文
	std::string modelName;     // 3Dモデルやテクスチャの名前
	std::string effectName;    // エフェクト名
	std::string seName;        // 効果音名
	CardRarity rarity;         // レア度
	bool canEnemyUse;          // 敵も使用可能か
	CardAttackRangeType attackRangeType; // 攻撃距離の種類 (近距離、中距離、遠距離)
};

class CardDatabase {
private:

	//全カードのデータを保存しておく
	static std::unordered_map<int, Card> database_;

public:

	//初期化
	static void Initialize(const std::string& filePath);

	// データを消さずに別のcsvを追加で読み込み
	static void LoadAdditionalCards(const std::string &filePath);

	//IDを指定してカードデータを取得
	static Card GetCardData(int id);

	// 現在登録されているカードの総数を返す
	static size_t GetCardCount() { return database_.size(); }

	// 敵が使えるカードの中からランダムに1枚取得する
	static Card GetRandomEnemyUsableCard();

	// プレイヤーが拾えるカードだけをランダムに取得
	static Card GetRandomPlayerCard();

	// ボス部屋でフィールドに出るプレイヤー用カードをランダムに取得
	static Card GetRandomBossRoomPlayerCard();

};
