#include "HandManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/particle/GPUParticleManager.h"

#include "Engine/3D/Model/ModelManager.h"
#include "Engine/2D/Sprite.h"
#include "Engine/Base/WindowProc.h"
#include "Engine/Camera/Camera.h"
#include "engine/math/Matrix4x4.h"

using namespace MatrixMath;

namespace {
struct ScreenRect {
	Vector2 center;
	Vector2 size;
};

std::unique_ptr<Sprite> CreateCooldownOverlay() {
	auto overlay = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
	if (overlay) {
		overlay->SetAnchorPoint({ 0.5f, 1.0f });
		overlay->SetColor({ 0.0f, 0.0f, 0.0f, 0.55f });
	}
	return overlay;
}

Vector2 ProjectHandPosition(const Camera* camera, const Vector3& pos) {
	if (!camera) {
		return { -10000.0f, -10000.0f };
	}

	Matrix4x4 viewProjection = camera->GetViewProjectionMatrix();
	Vector4 clip{};
	clip.x = pos.x * viewProjection.m[0][0] + pos.y * viewProjection.m[1][0] + pos.z * viewProjection.m[2][0] + viewProjection.m[3][0];
	clip.y = pos.x * viewProjection.m[0][1] + pos.y * viewProjection.m[1][1] + pos.z * viewProjection.m[2][1] + viewProjection.m[3][1];
	clip.z = pos.x * viewProjection.m[0][2] + pos.y * viewProjection.m[1][2] + pos.z * viewProjection.m[2][2] + viewProjection.m[3][2];
	clip.w = pos.x * viewProjection.m[0][3] + pos.y * viewProjection.m[1][3] + pos.z * viewProjection.m[2][3] + viewProjection.m[3][3];

	if (clip.w == 0.0f) {
		return { -10000.0f, -10000.0f };
	}

	float invW = 1.0f / clip.w;
	float ndcX = clip.x * invW;
	float ndcY = clip.y * invW;
	float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
	float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());

	return {
		(ndcX * 0.5f + 0.5f) * screenW,
		(-ndcY * 0.5f + 0.5f) * screenH
	};
}

ScreenRect GetCardScreenRect(const Camera* camera, const Obj3d* cardObj) {
	if (!camera || !cardObj) {
		return { { -10000.0f, -10000.0f }, { 0.0f, 0.0f } };
	}

	const Matrix4x4 world = MakeAffine(
		cardObj->GetScale(),
		cardObj->GetRotation(),
		cardObj->GetTranslation()
	);

	const Vector3 localCorners[4] = {
		{ -0.74f, -1.0f, 0.0f },
		{ 0.74f, -1.0f, 0.0f },
		{ -0.74f, 1.0f, 0.0f },
		{ 0.74f, 1.0f, 0.0f }
	};

	Vector2 first = ProjectHandPosition(camera, Transforms(localCorners[0], world));
	float minX = first.x;
	float maxX = first.x;
	float minY = first.y;
	float maxY = first.y;

	for (int i = 1; i < 4; ++i) {
		Vector2 p = ProjectHandPosition(camera, Transforms(localCorners[i], world));
		if (p.x < minX) { minX = p.x; }
		if (p.x > maxX) { maxX = p.x; }
		if (p.y < minY) { minY = p.y; }
		if (p.y > maxY) { maxY = p.y; }
	}

	const float paddingX = -3.0f;
	const float paddingY = 8.0f;
	return {
		{ (minX + maxX) * 0.5f, (minY + maxY) * 0.5f },
		{ (maxX - minX) + paddingX, (maxY - minY) + paddingY }
	};
}

void ApplyCardDissolveColor(Obj3d* model, CardEffectType effectType) {
	if (model == nullptr) {
		return;
	}

	if (effectType == CardEffectType::Attack) {
		model->SetDissolveColor({ 1.0f, 0.2f, 0.05f });
	} else if (effectType == CardEffectType::Heal) {
		model->SetDissolveColor({ 0.1f, 1.0f, 0.2f });
	} else if (effectType == CardEffectType::Defense) {
		model->SetDissolveColor({ 0.0f, 0.5f, 1.0f });
	} else if (effectType == CardEffectType::Special) {
		model->SetDissolveColor({ 0.7f, 0.2f, 1.0f });
	}
}
}

void HandManager::Initialize(Camera* camera, uint32_t noiseTextureIndex) {
	hand_.clear();
	handModels_.clear();
	cooldownOverlays_.clear();
	isDissolving_.clear();
	dissolveThresholds_.clear();
	selectedCardIndex_ = 0;
	camera_ = camera;
	noiseTextureIndex_ = noiseTextureIndex;
	cooldownCardId_ = -1;
	cooldownTimer_ = 0;
	cooldownDuration_ = 0;
	castCardId_ = -1;
	castCardIndex_ = -1;
	castTimer_ = 0;
	castDuration_ = 0;
	swapModeVisual_ = false;
	pendingReturnToFist_ = false;
	manualSelectionAfterUse_ = false;
	prevSelectedIndex_  = 0;    // 初回フレームで変化扱いにしない
	selectionTickTimer_ = 6;    // 起動直後はすぐ出さない
	selectionPulseTime_ = 0.0f;
	drawSparkFrames_    = 0;
	drawSparkCardIdx_   = -1;
}

bool HandManager::AddCard(const Card& newCard) {
	// 1. まずモデルのデータを探す
	Model *cardModelData = ModelManager::GetInstance()->FindModel(newCard.modelName);

	// もしモデルが見つからなかったら弾く
	if (cardModelData == nullptr) {
		return false;
	}

	// =========================================================
	// ★ 修正ポイント①：消えかかっている（ディゾルブ中の）枠を再利用する！
	// =========================================================
	for (size_t i = 0; i < hand_.size(); ++i) {
		if (isDissolving_[i]) {
			// ディゾルブ中のカードを、拾った新しいカードで上書きする
			hand_[i] = newCard;

			auto model = std::make_unique<Obj3d>();
			model->Initialize(cardModelData);
			model->SetCamera(camera_);
			model->SetNoiseTexture(noiseTextureIndex_);
			model->SetDissolveThreshold(0.0f);
			ApplyCardDissolveColor(model.get(), newCard.effectType);

			handModels_[i] = std::move(model);
			cooldownOverlays_[i] = CreateCooldownOverlay();

			// ディゾルブをキャンセルして、新しいカードを実体化させる
			isDissolving_[i] = false;
			dissolveThresholds_[i] = 0.0f;

			// ドローきらめきフラグ
			drawSparkCardIdx_ = static_cast<int>(i);
			drawSparkFrames_  = 4;

			return true; // 取得成功！
		}
	}

	// =========================================================
	// ★ 修正ポイント②：ディゾルブ中の枠がなく、まだ上限に達していなければ普通に追加
	// =========================================================
	if (hand_.size() < maxHandSize_) {
		hand_.push_back(newCard);

		auto model = std::make_unique<Obj3d>();
		model->Initialize(cardModelData);
		model->SetCamera(camera_);
		model->SetNoiseTexture(noiseTextureIndex_);
		model->SetDissolveThreshold(0.0f);
		ApplyCardDissolveColor(model.get(), newCard.effectType);

		handModels_.push_back(std::move(model));
		cooldownOverlays_.push_back(CreateCooldownOverlay());
		isDissolving_.push_back(false);
		dissolveThresholds_.push_back(0.0f);

		// ドローきらめきフラグ
		drawSparkCardIdx_ = static_cast<int>(hand_.size()) - 1;
		drawSparkFrames_  = 4;

		return true; // 取得成功！
	}

	// 完全に満杯なら拾えない（交換フェイズへ）
	return false;
}

bool HandManager::IsCardSelectableInCurrentMode(int index) const {
	if (index < 0 || index >= static_cast<int>(hand_.size())) {
		return false;
	}

	return !(swapModeVisual_ && hand_[index].id == 1);
}

void HandManager::MoveSelection(int direction) {
	if (hand_.empty() || direction == 0) {
		return;
	}

	const int handSize = static_cast<int>(hand_.size());
	int nextIndex = selectedCardIndex_;

	for (int i = 0; i < handSize; ++i) {
		nextIndex += direction;
		if (nextIndex >= handSize) {
			nextIndex = 0;
		}
		if (nextIndex < 0) {
			nextIndex = handSize - 1;
		}

		if (IsCardSelectableInCurrentMode(nextIndex)) {
			selectedCardIndex_ = nextIndex;
			return;
		}
	}
}

void HandManager::ClampSelectedCardIndex() {
	if (hand_.empty()) {
		selectedCardIndex_ = 0;
		return;
	}

	if (selectedCardIndex_ < 0) {
		selectedCardIndex_ = 0;
	}
	if (selectedCardIndex_ >= static_cast<int>(hand_.size())) {
		selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
	}
}

bool HandManager::SelectCardById(int cardId) {
	for (int i = 0; i < static_cast<int>(hand_.size()); ++i) {
		if (hand_[i].id == cardId && !isDissolving_[i] && IsCardSelectableInCurrentMode(i)) {
			selectedCardIndex_ = i;
			return true;
		}
	}
	return false;
}

void HandManager::ApplyPostUseSelectionAfterRemove() {
	ClampSelectedCardIndex();

	if (!pendingReturnToFist_) {
		return;
	}

	if (!manualSelectionAfterUse_ && !swapModeVisual_) {
		SelectCardById(1);
	}

	pendingReturnToFist_ = false;
	manualSelectionAfterUse_ = false;
}

void HandManager::Update() {
	//手札がなければ何もしない
	if (hand_.empty()) {
		return;
	}
	auto input = Input::GetInstance();

	// 右スティック左右の倒し込みを1回入力として扱うための前フレーム保持
	static bool wasStickLeft = false;
	static bool wasStickRight = false;

	// 右スティックのX入力で左右選択する
	bool isStickLeft = input->GetRightStickX() < -0.5f;
	bool isStickRight = input->GetRightStickX() > 0.5f;

	const bool isCasting =
		!swapModeVisual_ &&
		castTimer_ > 0 &&
		castDuration_ > 0 &&
		castCardIndex_ >= 0 &&
		castCardIndex_ < static_cast<int>(hand_.size());

	//左右キーで選んでいるカードの切り替え
	if (isCasting) {
		selectedCardIndex_ = castCardIndex_;
	}
	// 右キーに加えて 右D-Pad / RB / 左スティック右でも右選択できるようにする
	else if (
		input->Triggerkey(DIK_RIGHT) ||
		input->TriggerJoystickButton(XINPUT_GAMEPAD_DPAD_RIGHT) ||
		input->TriggerJoystickButton(XINPUT_GAMEPAD_RIGHT_SHOULDER) ||
		(isStickRight && !wasStickRight)
		) {
		MoveSelection(1);
		if (pendingReturnToFist_ && !swapModeVisual_) {
			manualSelectionAfterUse_ = true;
		}
	}

	// 左キーに加えて 左D-Pad と LB でも左選択できるようにする
	// 左キーに加えて 左D-Pad / LB / 左スティック左でも左選択できるようにする
	if (
		!isCasting &&
		(
			input->Triggerkey(DIK_LEFT) ||
			input->TriggerJoystickButton(XINPUT_GAMEPAD_DPAD_LEFT) ||
			input->TriggerJoystickButton(XINPUT_GAMEPAD_LEFT_SHOULDER) ||
			(isStickLeft && !wasStickLeft)
			)
		) {
		MoveSelection(-1);
		if (pendingReturnToFist_ && !swapModeVisual_) {
			manualSelectionAfterUse_ = true;
		}
	}

	// 次フレーム用に保存する
	wasStickLeft = isStickLeft;
	wasStickRight = isStickRight;

	// カードとカードの間隔
	float spacing = 0.3f;

	// 手札が画面の「中央」に揃うように、最初のカードのX座標を計算する
	float totalWidth = (static_cast<float>(hand_.size()) - 1.0f) * spacing;
	float startX = -totalWidth / 2.0f;

	// 消すカードを後でまとめて処理するための配列
	std::vector<int> removeIndices;

	for (int i = 0; i < static_cast<int>(handModels_.size()); ++i) {

		// ディゾルブ中なら進行度を増やす
		if (isDissolving_[i]) {
			dissolveThresholds_[i] += 0.015f; // 消える速度

			if (dissolveThresholds_[i] > 1.0f) {
				dissolveThresholds_[i] = 1.0f;
			}

			handModels_[i]->SetDissolveThreshold(dissolveThresholds_[i]);

			// 完全に消えたら削除予約
			if (dissolveThresholds_[i] >= 1.0f) {
				removeIndices.push_back(i);
				continue;
			}
		}

		// 基本の位置
		Vector3 pos = { startX + (i * spacing), -1.0f, 3.0f };

		// 基本の角度：少しだけ上向き（顔に向けるように）傾ける
		Vector3 rot = { -0.2f, 0.0f, 0.0f };

		// 選択中のカードだけ特別扱い（上に浮いて、手前に来て、まっすぐ向く）
		if (i == selectedCardIndex_) {
			pos.y += 0.3f;  // 上に浮かせる
			pos.z -= 0.3f;  // 手前に出す
			rot.x = 0.0f;   // 角度をまっすぐ正面に向ける
		}

		float cardScale = 0.3f;
		Vector4 cardColor{ 1.0f, 1.0f, 1.0f, 1.0f };

		// モデルに座標と角度をセットして更新
		const bool cannotSwap = swapModeVisual_ && i < static_cast<int>(hand_.size()) && hand_[i].id == 1;
		const bool castingThisCard = IsCardCasting(i);
		if (cannotSwap) {
			cardColor = { 0.36f, 0.36f, 0.36f, 1.0f };
		}
		else if (isCasting && !castingThisCard) {
			cardColor = { 0.32f, 0.32f, 0.36f, 0.82f };
			cardScale = 0.26f;
			pos.y -= 0.12f;
			pos.z += 0.08f;
		}
		else if (castingThisCard) {
			const float pulse = 0.5f + 0.5f * std::sinf(static_cast<float>(castTimer_) * 0.22f);
			cardScale = 0.325f + 0.012f * pulse;
			pos.y += 0.04f + 0.02f * pulse;
			pos.z -= 0.035f;
			cardColor = { 1.0f, 1.0f, 0.78f + 0.12f * pulse, 1.0f };
		}
		else if ( i == selectedCardIndex_ ) {
			// 選択中カードのゴールド点滅パルス
			const float pulse = 0.5f + 0.5f * std::sinf(selectionPulseTime_);
			cardScale = 0.305f + 0.012f * pulse;           // 少し大きくなる
			cardColor = { 1.0f, 0.88f + 0.12f * pulse, 0.4f + 0.3f * pulse, 1.0f }; // 白→金色
		}

		handModels_[i]->SetScale({ cardScale, cardScale, cardScale });
		handModels_[i]->SetColor(cardColor);
		handModels_[i]->SetTranslation(pos);
		handModels_[i]->SetRotation(rot);
		handModels_[i]->Update();

		// ドロー時きらめき発行（プレイヤーのワールド座標で出す）
		if ( drawSparkFrames_ > 0 && i == drawSparkCardIdx_ ) {
			// Attack は距離タイプで色分け、それ以外は属性色を使用
			Vector4 sparkColor = { 1.0f, 1.0f, 0.6f, 1.0f }; // デフォルト：金色
			if ( i < static_cast<int>(hand_.size()) ) {
				const Card& c = hand_[i];
				switch ( c.effectType ) {
				case CardEffectType::Attack:
					switch ( c.attackRangeType ) {
					case CardAttackRangeType::Melee: sparkColor = { 1.0f, 0.18f, 0.12f, 1.0f }; break; // 近距離：赤
					case CardAttackRangeType::Mid:   sparkColor = { 1.0f, 0.9f,  0.12f, 1.0f }; break; // 中距離：黄
					case CardAttackRangeType::Long:  sparkColor = { 0.18f, 0.62f, 1.0f, 1.0f }; break; // 遠距離：青
					default: break;
					}
					break;
				case CardEffectType::Heal:    sparkColor = { 0.3f, 1.0f, 0.5f, 1.0f }; break; // 緑
				case CardEffectType::Defense: sparkColor = { 0.3f, 0.7f, 1.0f, 1.0f }; break; // 青
				case CardEffectType::Special: sparkColor = { 0.8f, 0.3f, 1.0f, 1.0f }; break; // 紫
				default: break;
				}
			}
			// 中心フラッシュ（大きめ）
			Vector3 emitPos = { playerWorldPos_.x, playerWorldPos_.y + 0.8f, playerWorldPos_.z };
			GPUParticleManager::GetInstance()->Emit(emitPos, { 0, 0, 0 }, 0.12f, 3.0f, sparkColor);

			// 周囲に散るきらめき
			for ( int s = 0; s < 10; s++ ) {
				Vector3 sv = {
					( rand() % 11 - 5 ) * 0.1f,
					0.1f + ( rand() % 8 ) * 0.05f,
					( rand() % 11 - 5 ) * 0.1f
				};
				float sc = 0.3f + ( rand() % 5 ) * 0.08f;
				GPUParticleManager::GetInstance()->Emit(emitPos, sv, 0.6f, sc, sparkColor);
			}
		}
	}

	// 選択パルスタイムを進める
	selectionPulseTime_ += 0.1f;

	// ドローきらめきタイマーを1フレーム減らす
	if ( drawSparkFrames_ > 0 ) drawSparkFrames_--;

	// ---- カード選択ハイライト ----
	if ( !hand_.empty() ) {
		Vector3 emitPos = { playerWorldPos_.x, playerWorldPos_.y + 0.8f, playerWorldPos_.z };
		bool selectionChanged = ( prevSelectedIndex_ != selectedCardIndex_ );

		// 選択が変わった瞬間：バーストを1回出す
		if ( selectionChanged && prevSelectedIndex_ >= 0 ) {
			// Attack は距離タイプで色分け、それ以外は属性色を使用
			Vector4 burstColor = { 1.0f, 1.0f, 0.6f, 1.0f };
			if ( selectedCardIndex_ < static_cast<int>(hand_.size()) ) {
				const Card& c = hand_[selectedCardIndex_];
				switch ( c.effectType ) {
				case CardEffectType::Attack:
					switch ( c.attackRangeType ) {
					case CardAttackRangeType::Melee: burstColor = { 1.0f, 0.18f, 0.12f, 1.0f }; break; // 近距離：赤
					case CardAttackRangeType::Mid:   burstColor = { 1.0f, 0.9f,  0.12f, 1.0f }; break; // 中距離：黄
					case CardAttackRangeType::Long:  burstColor = { 0.18f, 0.62f, 1.0f, 1.0f }; break; // 遠距離：青
					default: break;
					}
					break;
				case CardEffectType::Heal:    burstColor = { 0.3f, 1.0f, 0.5f, 1.0f }; break; // 緑
				case CardEffectType::Defense: burstColor = { 0.3f, 0.7f, 1.0f, 1.0f }; break; // 青
				case CardEffectType::Special: burstColor = { 0.8f, 0.3f, 1.0f, 1.0f }; break; // 紫
				default: break;
				}
			}
			// 切り替わりバースト（リング状に広がる）
			for ( int s = 0; s < 10; s++ ) {
				float angle = ( 3.14159f * 2.0f / 10.0f ) * s;
				float speed = 0.25f + ( rand() % 4 ) * 0.04f;
				Vector3 sv = { std::sinf(angle) * speed, 0.05f, std::cosf(angle) * speed };
				GPUParticleManager::GetInstance()->Emit(emitPos, sv, 0.35f, 0.5f, burstColor);
			}
			selectionTickTimer_ = 0; // バースト直後は継続きらめきをすぐ出さない
		}

		// 選択中の継続きらめき（6フレームに1回）
		if ( selectionTickTimer_ <= 0 ) {
			selectionTickTimer_ = 6;
			if ( selectedCardIndex_ < static_cast<int>(hand_.size()) ) {
				// Attack は距離タイプで色分け、それ以外は属性色を使用
				Vector4 tickColor = { 1.0f, 1.0f, 0.7f, 0.85f }; // デフォルト：金色
				{
					const Card& c = hand_[selectedCardIndex_];
					switch ( c.effectType ) {
					case CardEffectType::Attack:
						switch ( c.attackRangeType ) {
						case CardAttackRangeType::Melee: tickColor = { 1.0f, 0.18f, 0.12f, 0.85f }; break; // 近距離：赤
						case CardAttackRangeType::Mid:   tickColor = { 1.0f, 0.9f,  0.12f, 0.85f }; break; // 中距離：黄
						case CardAttackRangeType::Long:  tickColor = { 0.18f, 0.62f, 1.0f, 0.85f }; break; // 遠距離：青
						default: break;
						}
						break;
					case CardEffectType::Heal:    tickColor = { 0.3f, 1.0f, 0.5f,  0.85f }; break; // 緑
					case CardEffectType::Defense: tickColor = { 0.3f, 0.7f, 1.0f,  0.85f }; break; // 青
					case CardEffectType::Special: tickColor = { 0.8f, 0.3f, 1.0f,  0.85f }; break; // 紫
					default: break;
					}
				}
					// 中心のコア（小さく鋭く光る）
				GPUParticleManager::GetInstance()->Emit(
					emitPos,
					{ ( rand() % 5 - 2 ) * 0.03f, 0.06f + ( rand() % 3 ) * 0.02f, ( rand() % 5 - 2 ) * 0.03f },
					0.35f, 0.22f, { tickColor.x, tickColor.y, tickColor.z, 1.0f });

				// 周囲にふわっと散る小粒（2個）
				for ( int s = 0; s < 2; s++ ) {
					Vector3 sv = {
						( rand() % 11 - 5 ) * 0.07f,
						0.05f + ( rand() % 6 ) * 0.025f,
						( rand() % 11 - 5 ) * 0.07f
					};
					float sc = 0.12f + ( rand() % 4 ) * 0.05f;
					GPUParticleManager::GetInstance()->Emit(emitPos, sv, 0.6f, sc, tickColor);
				}
			}
		}
		if ( selectionTickTimer_ > 0 ) selectionTickTimer_--;
	}

	prevSelectedIndex_ = selectedCardIndex_;

	// 後ろから削除
	for (int i = static_cast<int>(removeIndices.size()) - 1; i >= 0; --i) {
		RemoveCard(removeIndices[i]);
	}
}

void HandManager::Draw() {
	// 3Dモデルを描画する
	for (auto& model : handModels_) {
		model->Draw();
	}
}

void HandManager::DrawCooldownOverlays() {
	if ((cooldownTimer_ <= 0 || cooldownDuration_ <= 0) &&
		(castTimer_ <= 0 || castDuration_ <= 0)) {
		return;
	}

	if (cooldownOverlays_.size() < hand_.size()) {
		cooldownOverlays_.resize(hand_.size());
	}

	for (int i = 0; i < static_cast<int>(hand_.size()); ++i) {
		if (!IsCardCoolingDown(i) && !IsCardCasting(i)) {
			continue;
		}

		if (!cooldownOverlays_[i]) {
			cooldownOverlays_[i] = CreateCooldownOverlay();
		}
		if (!cooldownOverlays_[i] || !handModels_[i]) {
			continue;
		}

		const float ratio = (std::max)(GetCardCooldownRatio(i), GetCardCastRatio(i));
		const ScreenRect rect = GetCardScreenRect(camera_, handModels_[i].get());

		cooldownOverlays_[i]->SetPosition({ rect.center.x, rect.center.y + rect.size.y * 0.5f });
		cooldownOverlays_[i]->SetSize({ rect.size.x, rect.size.y * ratio });
		cooldownOverlays_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 0.58f });
		cooldownOverlays_[i]->Update();
		cooldownOverlays_[i]->Draw();
	}
}

Card HandManager::GetSelectedCard() const {
	if (hand_.empty()) {
		// CardDatabase.hの構造体に合わせてエラーカードを返す
		return { -1, "Unknown", 0, CardEffectType::Special, 0, "Error", "None", "None", "None", CardRarity::Common, false };
	}

	// ディゾルブ中のカードは使えないようにする
	if (selectedCardIndex_ >= 0 &&
		selectedCardIndex_ < static_cast<int>(isDissolving_.size()) &&
		isDissolving_[selectedCardIndex_]) {
		return { -1, "Unknown", 0, CardEffectType::Special, 0, "Error", "None", "None", "None", CardRarity::Common, false };
	}

	return hand_[selectedCardIndex_];
}

void HandManager::RemoveSelectedCard() {
	if (hand_.empty()) return;

	// データと見た目の両方を消す！
	hand_.erase(hand_.begin() + selectedCardIndex_);
	handModels_.erase(handModels_.begin() + selectedCardIndex_);
	cooldownOverlays_.erase(cooldownOverlays_.begin() + selectedCardIndex_);
	isDissolving_.erase(isDissolving_.begin() + selectedCardIndex_);
	dissolveThresholds_.erase(dissolveThresholds_.begin() + selectedCardIndex_);

	// 消した後に、カーソルの位置がおかしくならないように調整する
	if (selectedCardIndex_ >= static_cast<int>(hand_.size())) {
		selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
	}
	if (selectedCardIndex_ < 0) {
		selectedCardIndex_ = 0;
	}
}

bool HandManager::SwapSelectedCard(const Card& newCard) {
	if (hand_.empty()) {
		return false;
	}

	//新しいカードの３Dモデルを探す
	Model* cardModelData = ModelManager::GetInstance()->FindModel(newCard.modelName);
	if (cardModelData == nullptr) {
		return false;
	}

	//カードを新しいカードに上書きできる
	hand_[selectedCardIndex_] = newCard;

	//見た目を作り直して入れ替える
	auto model = std::make_unique<Obj3d>();
	model->Initialize(cardModelData);
	model->SetCamera(camera_);
	model->SetNoiseTexture(noiseTextureIndex_);
	model->SetDissolveThreshold(0.0f);

	if (newCard.effectType == CardEffectType::Attack) {
		model->SetDissolveColor({ 1.0f, 0.2f, 0.05f });
	} else if (newCard.effectType == CardEffectType::Heal) {
		model->SetDissolveColor({ 0.1f, 1.0f, 0.2f });
	} else if (newCard.effectType == CardEffectType::Defense) {
		model->SetDissolveColor({ 0.0f, 0.5f, 1.0f });
	} else if (newCard.effectType == CardEffectType::Special) {
		model->SetDissolveColor({ 0.7f, 0.2f, 1.0f });
	}

	handModels_[selectedCardIndex_] = std::move(model);
	cooldownOverlays_[selectedCardIndex_] = CreateCooldownOverlay();
	isDissolving_[selectedCardIndex_] = false;
	dissolveThresholds_[selectedCardIndex_] = 0.0f;
	pendingReturnToFist_ = false;
	manualSelectionAfterUse_ = false;

	return true;
}

Card HandManager::GetCard(int index) const {
	if (index >= 0 && index < static_cast<int>(hand_.size())) {
		return hand_[index];
	}
	return Card{ -1, "Unknown", 0, CardEffectType::Special, 0, "Error", "None", "None", "None", CardRarity::Common, false }; //エラー回避用のダミーカード
}

void HandManager::RemoveCard(int index) {
	if (index >= 0 && index < static_cast<int>(hand_.size())) {
		hand_.erase(hand_.begin() + index);
		handModels_.erase(handModels_.begin() + index);
		cooldownOverlays_.erase(cooldownOverlays_.begin() + index);
		isDissolving_.erase(isDissolving_.begin() + index);
		dissolveThresholds_.erase(dissolveThresholds_.begin() + index);

		if (hand_.empty()) {
			selectedCardIndex_ = 0;
			pendingReturnToFist_ = false;
			manualSelectionAfterUse_ = false;
			return;
		}

		if (selectedCardIndex_ > index) {
			selectedCardIndex_--;
		}
		if (selectedCardIndex_ >= static_cast<int>(hand_.size())) {
			selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
		}
		if (selectedCardIndex_ < 0) {
			selectedCardIndex_ = 0;
		}
		ApplyPostUseSelectionAfterRemove();
	}
}

int HandManager::GetHandSize() const {
	return static_cast<int>(hand_.size());
}

const std::vector<Card>& HandManager::GetHandList() {
	return hand_;
}

void HandManager::StartDissolveSelectedCard() {
	if (hand_.empty()) {
		return;
	}

	if (selectedCardIndex_ < 0 || selectedCardIndex_ >= static_cast<int>(hand_.size())) {
		return;
	}

	// すでにディゾルブ中なら何もしない
	if (isDissolving_[selectedCardIndex_]) {
		return;
	}

	isDissolving_[selectedCardIndex_] = true;
	dissolveThresholds_[selectedCardIndex_] = 0.0f;
	pendingReturnToFist_ = hand_[selectedCardIndex_].id != 1;
	manualSelectionAfterUse_ = false;

	if (handModels_[selectedCardIndex_]) {
		ApplyCardDissolveColor(handModels_[selectedCardIndex_].get(), hand_[selectedCardIndex_].effectType);
		handModels_[selectedCardIndex_]->SetDissolveThreshold(0.0f);
	}
}

bool HandManager::IsSelectedCardDissolving() const {
	if (hand_.empty()) {
		return false;
	}

	if (selectedCardIndex_ < 0 || selectedCardIndex_ >= static_cast<int>(isDissolving_.size())) {
		return false;
	}

	return isDissolving_[selectedCardIndex_];
}

void HandManager::AddPendingCard(const Card &pendingCard) {

	Model *cardModelData = ModelManager::GetInstance()->FindModel(pendingCard.modelName);

	// モデルデータを探して作成
	if (cardModelData == nullptr)return;

	// 上限を無視して強制的に末尾に追加
	hand_.push_back(pendingCard);

	auto model = std::make_unique<Obj3d>();
	model->Initialize(cardModelData);
	model->SetCamera(camera_);
	model->SetNoiseTexture(noiseTextureIndex_);
	model->SetDissolveThreshold(0.0f);

	// カードの種類による色設定
	if (pendingCard.effectType == CardEffectType::Attack) {
		model->SetDissolveColor({ 1.0f,0.2f,0.05f });
	} else if (pendingCard.effectType == CardEffectType::Heal) {
		model->SetDissolveColor({ 0.1f, 1.0f, 0.2f });
	} else if (pendingCard.effectType == CardEffectType::Defense) {
		model->SetDissolveColor({ 0.0f, 0.5f, 1.0f });
	} else if (pendingCard.effectType == CardEffectType::Special) {
		model->SetDissolveColor({ 0.7f, 0.2f, 1.0f });
	}

	handModels_.push_back(std::move(model));
	cooldownOverlays_.push_back(CreateCooldownOverlay());
	isDissolving_.push_back(false);
	dissolveThresholds_.push_back(0.0f);
	pendingReturnToFist_ = false;
	manualSelectionAfterUse_ = false;

	// ★ 拾ったカード（一番右）にカーソルを強制的に合わせておく
	selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
}

	

void HandManager::RemoveCardImmediate(int index) {
	if (index < 0 || index >= static_cast<int>(hand_.size())) return;

	hand_.erase(hand_.begin() + index);
	handModels_.erase(handModels_.begin() + index);
	cooldownOverlays_.erase(cooldownOverlays_.begin() + index);
	isDissolving_.erase(isDissolving_.begin() + index);
	dissolveThresholds_.erase(dissolveThresholds_.begin() + index);

	if (hand_.empty()) {
		selectedCardIndex_ = 0;
		pendingReturnToFist_ = false;
		manualSelectionAfterUse_ = false;
		return;
	}

	if (selectedCardIndex_ > index) {
		selectedCardIndex_--;
	}
	if (selectedCardIndex_ >= static_cast<int>(hand_.size())) {
		selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
	}
	if (selectedCardIndex_ < 0) {
		selectedCardIndex_ = 0;
	}
	ApplyPostUseSelectionAfterRemove();
}

void HandManager::SetCooldownDisplay(int cardId, int remainingFrames, int durationFrames) {
	cooldownCardId_ = cardId;
	cooldownTimer_ = (remainingFrames > 0) ? remainingFrames : 0;
	cooldownDuration_ = (durationFrames > 0) ? durationFrames : 0;
}

void HandManager::SetCastDisplay(int cardId, int remainingFrames, int durationFrames, int cardIndex) {
	castCardId_ = cardId;
	castCardIndex_ = cardIndex;
	castTimer_ = (remainingFrames > 0) ? remainingFrames : 0;
	castDuration_ = (durationFrames > 0) ? durationFrames : 0;
	if (castTimer_ <= 0 || castDuration_ <= 0) {
		castCardIndex_ = -1;
	}
}

bool HandManager::IsCardCoolingDown(int index) const {
	if (index < 0 || index >= static_cast<int>(hand_.size())) {
		return false;
	}

	return cooldownTimer_ > 0 && cooldownDuration_ > 0 && hand_[index].id == cooldownCardId_;
}

bool HandManager::IsCardCasting(int index) const {
	if (index < 0 || index >= static_cast<int>(hand_.size())) {
		return false;
	}

	if (castTimer_ <= 0 || castDuration_ <= 0) {
		return false;
	}

	if (castCardIndex_ >= 0) {
		return index == castCardIndex_;
	}

	return hand_[index].id == castCardId_;
}

float HandManager::GetCardCooldownRatio(int index) const {
	if (!IsCardCoolingDown(index)) {
		return 0.0f;
	}

	return std::clamp(
		static_cast<float>(cooldownTimer_) / static_cast<float>(cooldownDuration_),
		0.0f,
		1.0f
	);
}

float HandManager::GetCardCastRatio(int index) const {
	if (!IsCardCasting(index)) {
		return 0.0f;
	}

	return std::clamp(
		1.0f - static_cast<float>(castTimer_) / static_cast<float>(castDuration_),
		0.0f,
		1.0f
	);
}
