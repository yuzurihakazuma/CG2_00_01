#pragma once
#include "game/card/CardDatabase.h"
#include <memory>

class PlayerManager;
class HandManager;
class Input;
class Sprite;

// レベルアップ時の結果をシーンに伝えるための構造体
struct LevelUpResult {
    bool needCardSwap;     // 手札がいっぱいで交換モードへの移行が必要か
    Card droppedCard;      // 溢れた（引いた）カード
};

class LevelUpBonusManager {
public:

    // 選択肢のID
    enum class Choice {
        IncreaseMaxHandSize = 0, // 手札上限アップ
        GetRandomCard,          // ランダムカード獲得
        Count
    };

    // 初期化
    void Initialize();

    // リセット（デバッグやリトライ時）
    void Reset(int currentLevel);

    // 毎フレーム呼んで、レベルアップを監視・処理する
    LevelUpResult Update(PlayerManager *playerManager, HandManager *handManager, Input *input);

    // 描画処理
    void Draw();

    // 現在選択中かどうかを取得
    bool IsSelecting() const { return isSelecting_; }

private:
    bool isSelecting_ = false; // 選択画面表示中か
    int previousPlayerLevel_ = 1;
    Choice currentSelectedChoice_ = Choice::IncreaseMaxHandSize; // 現在カーソルが合っている選択肢
    bool canIncreaseMaxHandSize_ = true; // 手札上限アップを選べるか

    // 画面を少し暗くする半透明の黒スプライト
    std::unique_ptr<Sprite> blackOverlay_;
    // 2つの選択肢のスプライト（[手札上限], [カード獲得]）
    std::unique_ptr<Sprite> choiceVisuals_[(int)Choice::Count];
    // UI
    std::unique_ptr<Sprite> UISprite_;
    std::unique_ptr<Sprite> choiceArrowSprite_ = nullptr; // 現在選択中のボーナスを指す矢印
    std::unique_ptr<Sprite> leftTextSprite_ = nullptr;  // 左の選択肢のテキスト画像
    std::unique_ptr<Sprite> rightTextSprite_ = nullptr; // 右の選択肢のテキスト画像

    // ボーナスの実処理
    LevelUpResult ApplyBonus(HandManager *handManager, Choice choice);

    // 連打による誤爆防止用タイマー
    int inputDelayTimer_ = 0;
    bool wasRightStickLeft_ = false;
    bool wasRightStickRight_ = false;

    // パネルを左右にどれくらい離すかの距離
    float panelOffsetX_ = 200.0f;
};

