#include "LevelUpBonusManager.h"
#include "game/player/PlayerManager.h"
#include "game/card/HandManager.h"
#include "engine/base/Input.h"
#include "game/audio/GameSE.h"
#include "engine/base/WindowProc.h"
#include "engine/2d/Sprite.h"
#include "engine/utils/TextManager.h"
#include <stdlib.h>
#include <WindowProc.h>

void LevelUpBonusManager::Initialize() {
    isSelecting_ = false;
	previousPlayerLevel_ = 1;
    currentSelectedChoice_ = Choice::IncreaseMaxHandSize;
    wasRightStickLeft_ = false;
    wasRightStickRight_ = false;
    const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
    const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());

    //  暗転用の黒スプライトを作成（背景）
    blackOverlay_ = Sprite::Create("resources/white1x1.png", { screenW * 0.5f, screenH * 0.5f });
    if (blackOverlay_) {
        // 画面全体を覆うサイズ（適当に大きく設定）
        blackOverlay_->SetSize({ screenW, screenH });
        blackOverlay_->SetColor({ 0.0f, 0.0f, 0.0f, 0.7f }); // 半透明の黒
    }
    // --- 選択肢1: 手札上限アップ (画面左側) ---
    // 画像がある場合は "resources/bonus_limit.png" などに書き換えてください。
    choiceVisuals_[(int)Choice::IncreaseMaxHandSize] = Sprite::Create("resources/UI/Change.png", { 0.0f, 0.0f });
    if (choiceVisuals_[(int)Choice::IncreaseMaxHandSize]) {
        choiceVisuals_[(int)Choice::IncreaseMaxHandSize]->SetAnchorPoint({ 0.5f, 0.5f }); // 中心基準
        choiceVisuals_[(int)Choice::IncreaseMaxHandSize]->SetPosition({ 340.0f, 250.0f }); // 中央から少し左
    }
    // --- 選択肢2: カード獲得 (画面右側) ---
    // 画像がある場合は "resources/bonus_card.png" などに書き換えてください。
    choiceVisuals_[(int)Choice::GetRandomCard] = Sprite::Create("resources/UI/LevelC.png", { 0.0f, 0.0f });
    if (choiceVisuals_[(int)Choice::GetRandomCard]) {
        choiceVisuals_[(int)Choice::GetRandomCard]->SetAnchorPoint({ 0.5f, 0.5f });
        choiceVisuals_[(int)Choice::GetRandomCard]->SetPosition({ 740.0f, 210.0f }); // 中央から少し右
    }

   // テキストUI
    UISprite_ = Sprite::Create("resources/UI/LevelUpUi.png", { 0.0f,0.0f });
    if (UISprite_) {
        UISprite_->SetAnchorPoint({ 0.5f,0.5f });
        UISprite_->SetPosition({ screenW * 0.5f, screenH * 0.5f }); // 画面中央
    }

    // 左の選択肢の文字画像（実際のファイル名に合わせてください！）
    leftTextSprite_ = Sprite::Create("resources/UI/LevelUpHandUI.png", { screenW * 0.5f, screenH * 0.5f });
    if (leftTextSprite_) {
        leftTextSprite_->SetAnchorPoint({ 0.5f, 0.5f });
    }

    // 右の選択肢の文字画像（例：Change.png）
    rightTextSprite_ = Sprite::Create("resources/UI/LevelUpCardUi.png", { screenW * 0.5f, screenH * 0.5f });
    if (rightTextSprite_) {
        rightTextSprite_->SetAnchorPoint({ 0.5f, 0.5f });
    }

    choiceArrowSprite_ = Sprite::Create("resources/pose/poseChoice.png", { 0.0f, 0.0f });
    if (choiceArrowSprite_) {
        choiceArrowSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        choiceArrowSprite_->SetTextureRect(914.0f, 1052.0f, 40.0f, 76.0f);
        choiceArrowSprite_->SetSize({ 36.0f, 68.0f });
        choiceArrowSprite_->SetRotation(1.570796f);
    }
   
}

void LevelUpBonusManager::Reset(int currentLevel) {
    isSelecting_ = false;
	previousPlayerLevel_ = currentLevel;
    wasRightStickLeft_ = false;
    wasRightStickRight_ = false;
}

LevelUpResult LevelUpBonusManager::Update(PlayerManager *playerManager, HandManager *handManager, Input *input) {
    LevelUpResult finalResult = { false, { -1, "", 0 } };

    if (!playerManager || !handManager) {
        return finalResult;
    }

    int currentLevel = playerManager->GetLevel();

    if (!isSelecting_) {
        // 通常時：レベルアップを検知
        if (currentLevel > previousPlayerLevel_) {
            isSelecting_ = true;
            wasRightStickLeft_ = false;
            wasRightStickRight_ = false;
            currentSelectedChoice_ = Choice::IncreaseMaxHandSize; // デフォルトは枠増加

            // 画面が出た直後、30フレーム(0.5秒)は入力を受け付けない！
            inputDelayTimer_ = 30;

            // 記憶しているレベルを更新（ApplyBonus後に再度Updateが呼ばれた際に、まだレベルが残っていれば再度Selectingに移行する）
            previousPlayerLevel_++;
        }
    } else {

        // タイマーが残っている間は入力を無視してリターンする
        if (inputDelayTimer_ > 0) {
            wasRightStickLeft_ = input && input->GetRightStickX() < -0.5f;
            wasRightStickRight_ = input && input->GetRightStickX() > 0.5f;
            inputDelayTimer_--;
            return finalResult;
        }

        const bool isRightStickLeft = input->GetRightStickX() < -0.5f;
        const bool isRightStickRight = input->GetRightStickX() > 0.5f;
        const Choice previousChoice = currentSelectedChoice_;

        // 選択画面表示中：入力処理（A/Dキー または 左右矢印キー）
       // Aキーと左キーに加えて 左D-Pad でも左選択できるようにする
        if (
            input->Triggerkey(DIK_A) ||
            input->Triggerkey(DIK_LEFT) ||
            input->TriggerJoystickButton(XINPUT_GAMEPAD_DPAD_LEFT) ||
            (isRightStickLeft && !wasRightStickLeft_)
            ) {
            currentSelectedChoice_ = Choice::IncreaseMaxHandSize;
        }
        // Dキーと右キーに加えて 右D-Pad でも右選択できるようにする
        if (
            input->Triggerkey(DIK_D) ||
            input->Triggerkey(DIK_RIGHT) ||
            input->TriggerJoystickButton(XINPUT_GAMEPAD_DPAD_RIGHT) ||
            (isRightStickRight && !wasRightStickRight_)
            ) {
            currentSelectedChoice_ = Choice::GetRandomCard;
        }
        if (currentSelectedChoice_ != previousChoice) {
            GameSE::CursorMove();
        }

        // 決定（スペースキー）
        // SPACE に加えて A ボタンでも決定できるようにする
        wasRightStickLeft_ = isRightStickLeft;
        wasRightStickRight_ = isRightStickRight;

        if (input->Triggerkey(DIK_SPACE) || input->TriggerJoystickButton(XINPUT_GAMEPAD_A)) {
            GameSE::Confirm();
            // 選択されたボーナスを適用
            finalResult = ApplyBonus(handManager, currentSelectedChoice_);

            // 選択画面を閉じる
            isSelecting_ = false;
        }
    }

    return finalResult;
}

void LevelUpBonusManager::Draw() {
    if (!isSelecting_) {
        return;
    }

    // 今の画面サイズを取得する
    float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
    float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
    float centerX = screenW * 0.5f;
    float centerY = screenH * 0.5f;

    // 1. 暗転幕（背景）を描画
    if (blackOverlay_) {
        blackOverlay_->SetPosition({ centerX, centerY });
        blackOverlay_->SetSize({ screenW, screenH });
        blackOverlay_->Update();
        blackOverlay_->Draw();
    }

    // 選択肢の描画
    for (int i = 0; i < (int)Choice::Count; ++i) {
        if (choiceVisuals_[i]) {
            // 画面横幅の 20% 程度を中央からオフセットさせる
            float offsetX = screenW * 0.15f;
            Vector2 basePos;

            if (i == (int)Choice::IncreaseMaxHandSize) {
                basePos = { centerX - offsetX, centerY }; // 左側
            } else {
                basePos = { centerX + offsetX, centerY }; // 右側
            }

            // 選択中の演出（大きさや位置の微調整）
            if (i == (int)currentSelectedChoice_) {
                choiceVisuals_[i]->SetSize({ 264.0f, 396.0f });
                choiceVisuals_[i]->SetPosition({ basePos.x, basePos.y - 10.0f });
            } else {
                choiceVisuals_[i]->SetSize({ 240.0f, 360.0f });
                choiceVisuals_[i]->SetPosition(basePos);
            }

           

            choiceVisuals_[i]->Update();
            choiceVisuals_[i]->Draw();
        }
    }

    // UIスプライトの描画
    if (UISprite_) {
        UISprite_->SetPosition({ centerX, screenH * 0.5f });
        UISprite_->Update();
        UISprite_->Draw();
    }

    if (choiceArrowSprite_) {
        const float offsetX = screenW * 0.15f;
        Vector2 selectedBasePos = { centerX - offsetX, centerY };
        if (currentSelectedChoice_ == Choice::GetRandomCard) {
            selectedBasePos = { centerX + offsetX, centerY };
        }

        choiceArrowSprite_->SetPosition({ selectedBasePos.x, selectedBasePos.y - 250.0f });
        choiceArrowSprite_->Update();
        choiceArrowSprite_->Draw();
    }

    

    // 左のテキスト画像の描画（左パネルの下）
    if (leftTextSprite_) {
        // centerX - panelOffsetX で、左パネルの真下にピタッと合わせます
        leftTextSprite_->SetPosition({ centerX + panelOffsetX_, centerY  });
        leftTextSprite_->Update();
        leftTextSprite_->Draw();
    }

    // 右のテキスト画像の描画（右パネルの下）
    if (rightTextSprite_) {
        // centerX + panelOffsetX で、右パネルの真下にピタッと合わせます
        rightTextSprite_->SetPosition({ centerX - panelOffsetX_, centerY });
        rightTextSprite_->Update();
        rightTextSprite_->Draw();
    }
    
}

LevelUpResult LevelUpBonusManager::ApplyBonus(HandManager *handManager, Choice choice) {
    LevelUpResult result = { false, { -1, "", 0 } };

    if (choice == Choice::IncreaseMaxHandSize) {
        // ① 手札の最大枚数を1増やす (安全のために上限10までにする)
        if (handManager->GetMaxHandSize() < 10) {
            handManager->IncreaseMaxHandSize(1);
        } else {
            // 上限に達していたら、代わりにカード獲得にする、などの処理。
            // 選択肢に出ている以上、無効にするのは不親切なので、強制的にカード獲得にする例
            choice = Choice::GetRandomCard;
        }
    }

    // ①の処理でchoiceがGetRandomCardに変わった場合も含めて処理する
    if (choice == Choice::GetRandomCard) {
        // ② ランダムなカードを1枚獲得する
        int randomId = 2 + (rand() % 10);
        Card randomCard = CardDatabase::GetCardData(randomId);

        // 手札に追加してみて、失敗したら交換モード用のフラグを立てる
        if (!handManager->AddCard(randomCard)) {
            result.needCardSwap = true;
            result.droppedCard = randomCard;
        } else {
            GameSE::CardPickup();
        }
    }

    return result;
}
