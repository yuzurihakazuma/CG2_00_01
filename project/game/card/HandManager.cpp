#include "HandManager.h"

#include <algorithm>

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
			// 再利用枠でもカード種別に合わせてディゾルブ色を更新する
			if (newCard.effectType == CardEffectType::Attack) {
				model->SetDissolveColor({ 1.0f, 0.2f, 0.05f });
			} else if (newCard.effectType == CardEffectType::Heal) {
				model->SetDissolveColor({ 0.1f, 1.0f, 0.2f });
			} else if (newCard.effectType == CardEffectType::Defense) {
				model->SetDissolveColor({ 0.0f, 0.5f, 1.0f });
			} else if (newCard.effectType == CardEffectType::Special) {
				model->SetDissolveColor({ 0.7f, 0.2f, 1.0f });
			}

			handModels_[i] = std::move(model);
			cooldownOverlays_[i] = CreateCooldownOverlay();

			// ディゾルブをキャンセルして、新しいカードを実体化させる
			isDissolving_[i] = false;
			dissolveThresholds_[i] = 0.0f;

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
		// 新規追加時もカード種別に応じた色でディゾルブさせる
		if (newCard.effectType == CardEffectType::Attack) {
			model->SetDissolveColor({ 1.0f, 0.2f, 0.05f });
		} else if (newCard.effectType == CardEffectType::Heal) {
			model->SetDissolveColor({ 0.1f, 1.0f, 0.2f });
		} else if (newCard.effectType == CardEffectType::Defense) {
			model->SetDissolveColor({ 0.0f, 0.5f, 1.0f });
		} else if (newCard.effectType == CardEffectType::Special) {
			model->SetDissolveColor({ 0.7f, 0.2f, 1.0f });
		}

		handModels_.push_back(std::move(model));
		cooldownOverlays_.push_back(CreateCooldownOverlay());
		isDissolving_.push_back(false);
		dissolveThresholds_.push_back(0.0f);

		return true; // 取得成功！
	}

	// 完全に満杯なら拾えない（交換フェイズへ）
	return false;
}

void HandManager::Update() {
	//手札がなければ何もしない
	if (hand_.empty()) {
		return;
	}
	auto input = Input::GetInstance();

	//左右キーで選んでいるカードの切り替え
	if (input->Triggerkey(DIK_RIGHT)) {
		selectedCardIndex_++;
		//一番右に行ったらループ
		if (selectedCardIndex_ >= static_cast<int>(hand_.size())) {
			selectedCardIndex_ = 0;
		}
	}

	if (input->Triggerkey(DIK_LEFT)) {
		selectedCardIndex_--;
		//一番左に行ったらループ
		if (selectedCardIndex_ < 0) {
			selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
		}
	}

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

		handModels_[i]->SetScale({ 0.3f,0.3f,0.3f });

		// モデルに座標と角度をセットして更新
		handModels_[i]->SetTranslation(pos);
		handModels_[i]->SetRotation(rot);
		handModels_[i]->Update();
	}

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
	if (cooldownTimer_ <= 0 || cooldownDuration_ <= 0) {
		return;
	}

	if (cooldownOverlays_.size() < hand_.size()) {
		cooldownOverlays_.resize(hand_.size());
	}

	for (int i = 0; i < static_cast<int>(hand_.size()); ++i) {
		if (!IsCardCoolingDown(i)) {
			continue;
		}

		if (!cooldownOverlays_[i]) {
			cooldownOverlays_[i] = CreateCooldownOverlay();
		}
		if (!cooldownOverlays_[i] || !handModels_[i]) {
			continue;
		}

		const float ratio = GetCardCooldownRatio(i);
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

		if (selectedCardIndex_ >= static_cast<int>(hand_.size())) {
			selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
		}
		if (selectedCardIndex_ < 0) {
			selectedCardIndex_ = 0;
		}
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

	if (handModels_[selectedCardIndex_]) {
		// 使用直前に選択中カードの色を再反映して、ブルーム色ずれを防ぐ
		if (hand_[selectedCardIndex_].effectType == CardEffectType::Attack) {
			handModels_[selectedCardIndex_]->SetDissolveColor({ 1.0f, 0.2f, 0.05f });
		} else if (hand_[selectedCardIndex_].effectType == CardEffectType::Heal) {
			handModels_[selectedCardIndex_]->SetDissolveColor({ 0.1f, 1.0f, 0.2f });
		} else if (hand_[selectedCardIndex_].effectType == CardEffectType::Defense) {
			handModels_[selectedCardIndex_]->SetDissolveColor({ 0.0f, 0.5f, 1.0f });
		} else if (hand_[selectedCardIndex_].effectType == CardEffectType::Special) {
			handModels_[selectedCardIndex_]->SetDissolveColor({ 0.7f, 0.2f, 1.0f });
		}
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

	// ★ 修正1：ここも selectedCardIndex_ に直しました
	if (selectedCardIndex_ >= static_cast<int>(hand_.size())) {
		selectedCardIndex_ = static_cast<int>(hand_.size()) - 1;
	}
	if (selectedCardIndex_ < 0) {
		selectedCardIndex_ = 0;
	}
}

void HandManager::SetCooldownDisplay(int cardId, int remainingFrames, int durationFrames) {
	cooldownCardId_ = cardId;
	cooldownTimer_ = (remainingFrames > 0) ? remainingFrames : 0;
	cooldownDuration_ = (durationFrames > 0) ? durationFrames : 0;
}

bool HandManager::IsCardCoolingDown(int index) const {
	if (index < 0 || index >= static_cast<int>(hand_.size())) {
		return false;
	}

	return cooldownTimer_ > 0 && cooldownDuration_ > 0 && hand_[index].id == cooldownCardId_;
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
#include "HandManager.h"

#include "Engine/3D/Model/ModelManager.h"

namespace {
// カード種別ごとのディゾルブ色をモデルへ反映する
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
