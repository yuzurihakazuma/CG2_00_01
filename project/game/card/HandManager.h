#pragma once
#include <vector>
#include <string>
#include <memory>
#include "game/card/CardDatabase.h"

#include "engine/3d/obj/Obj3d.h"
#include "engine/base/Input.h"

class Camera;
class Sprite;

class HandManager {
private:

	std::vector<Card> hand_;     //現在の手札
	std::vector<std::unique_ptr<Obj3d>> handModels_; //カードの見た目
	std::vector<std::unique_ptr<Sprite>> cooldownOverlays_;

	std::vector<bool> isDissolving_;         // ディゾルブ中か
	std::vector<float> dissolveThresholds_;  // ディゾルブ進行度

	int selectedCardIndex_ = 0;   //現在選んでいるカードの番号
	int maxHandSize_ = 3;  //最大手札枚数

	Camera* camera_ = nullptr;
	uint32_t noiseTextureIndex_ = 0; // ノイズテクスチャ番号
	int cooldownCardId_ = -1;
	int cooldownTimer_ = 0;
	int cooldownDuration_ = 0;
	bool swapModeVisual_ = false;

	bool IsCardCoolingDown(int index) const;
	float GetCardCooldownRatio(int index) const;
	bool IsCardSelectableInCurrentMode(int index) const;
	void MoveSelection(int direction);

public:
	//初期化
	void Initialize(Camera* camera, uint32_t noiseTextureIndex);

	//ダンジョンでカードを拾う
	bool AddCard(const Card& newCard);

	//更新
	void Update();

	//描画
	void Draw();
	void DrawCooldownOverlays();

	//指定した番号のカード情報を取得
	Card GetCard(int index)const;

	//カードを使った後に、手札から消す処理
	void RemoveCard(int index);

	//現在の手札の枚数を取得
	int GetHandSize() const;

	//手札の全情報を取得
	const std::vector<Card>& GetHandList();

	//今選んでいるカードを取得
	Card GetSelectedCard() const;
	void RemoveSelectedCard();//次のカードを選ぶ

	// 現在選んでいるカードと、新しいカードを入れ替える
	bool SwapSelectedCard(const Card& newCard);

	// 選択中カードのディゾルブ開始
	void StartDissolveSelectedCard();

	// 選択中カードがディゾルブ中か
	bool IsSelectedCardDissolving() const;

	// 今選んでいるカードのインデックスを取得する関数
	int GetSelectedCardIndex()const { return selectedCardIndex_; }

	// 手札の最大枚数を取得する
	int GetMaxHandSize() const { return maxHandSize_; }

	// 手札の最大枚数を増やす
	void IncreaseMaxHandSize(int amount = 1) { maxHandSize_ += amount; }

	// 上限を無視して末尾に追加と削除
	void AddPendingCard(const Card &pendingCard);

	// 指定したインデックスのカードを即座に消す
	void RemoveCardImmediate(int index);

	void SetCooldownDisplay(int cardId, int remainingFrames, int durationFrames);

	void SetSwapModeVisual(bool enabled) { swapModeVisual_ = enabled; }

	// カーソルの位置を強制的に移動させる関数
	void SetSelectedCardIndex(int index) {
		if (hand_.empty()) {
			selectedCardIndex_ = 0;
			return;
		}
		// 0未満や、手札の枚数以上の数字が入らないように安全対策
		if (index < 0) index = 0;
		if (index >= static_cast<int>(hand_.size())) index = static_cast<int>(hand_.size()) - 1;

		selectedCardIndex_ = index;
	}
};
