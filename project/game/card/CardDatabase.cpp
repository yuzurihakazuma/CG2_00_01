#include "game/card/CardDatabase.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>
#include <random>

std::unordered_map<int, Card> CardDatabase::database_;

namespace {
constexpr int kBossRoomExcludedCardId = 11;
constexpr int kBossRoomAttackCardWeight = 3;
}

void CardDatabase::Initialize(const std::string& filePath) {
    database_.clear();

    // ファイルを開く
    std::ifstream file(filePath);

    // 開けなかった場合のエラーチェック
    if (!file.is_open()) {
        return;
    }

    std::string line;
    bool isFirstLine = true;

    //ファイルを1行ずつ読み込む
    while (std::getline(file, line)) {
        //最初の1行目はデータじゃないので飛ばす
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }

        //空行あったら飛ばす
        if (line.empty()) continue;

        //読み込んだ1行を、カンマで分離する処理
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> columns;

        //カンマで区切りで文字を取り出し、columnsに保存
        while (std::getline(ss, token, ',')) {
            columns.push_back(token);
        }

        // attackRangeType追加で12列必要
        if (columns.size() >= 12) {
            Card card;
            card.id = std::stoi(columns[0]);
            card.name = columns[1];
            card.cost = std::stoi(columns[2]);
            card.effectType = static_cast<CardEffectType>(std::stoi(columns[3]));
            card.effectValue = std::stoi(columns[4]);
            card.description = columns[5];
            card.modelName = columns[6];
            card.effectName = columns[7];
            card.seName = columns[8];
            card.rarity = static_cast<CardRarity>(std::stoi(columns[9]));

            // CSVの11番目のデータ(インデックス10)が "0" じゃなければ true にする！
            card.canEnemyUse = (std::stoi(columns[10]) != 0);

            // 攻撃距離タイプ（0=近距離, 1=中距離, 2=遠距離）
            card.attackRangeType = static_cast<CardAttackRangeType>(std::stoi(columns[11]));

            //辞典に登録
            database_[card.id] = card;
        }
    }

    file.close();
}

void CardDatabase::LoadAdditionalCards(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    bool isFirstLine = true;

    while (std::getline(file, line)) {
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> columns;

        while (std::getline(ss, token, ',')) {
            columns.push_back(token);
        }

        // attackRangeType追加で12列必要
        if (columns.size() >= 12) {
            Card card;
            card.id = std::stoi(columns[0]);
            card.name = columns[1];
            card.cost = std::stoi(columns[2]);
            card.effectType = static_cast<CardEffectType>(std::stoi(columns[3]));
            card.effectValue = std::stoi(columns[4]);
            card.description = columns[5];
            card.modelName = columns[6];
            card.effectName = columns[7];
            card.seName = columns[8];
            card.rarity = static_cast<CardRarity>(std::stoi(columns[9]));

            // CSVの11番目のデータ(インデックス10)が "0" じゃなければ true にする！
            card.canEnemyUse = (std::stoi(columns[10]) != 0);

            // 攻撃距離タイプ
            card.attackRangeType = static_cast<CardAttackRangeType>(std::stoi(columns[11]));

            // 辞書に追加
            database_[card.id] = card;
        }
    }
}

Card CardDatabase::GetCardData(int id) {
    if (database_.find(id) != database_.end()) {
        return database_[id];
    }

    // エラー用（距離タイプも追加）
    return { -1, "Unknwon", 0, CardEffectType::Special, 0, "Error", "None", "None", "None", CardRarity::Common, false, CardAttackRangeType::Mid };
}

Card CardDatabase::GetRandomEnemyUsableCard() {
    std::vector<Card> usableCards;

    // データベース内の全カードをチェックして、雑魚敵が使えるやつだけリストに入れる
    // 100番台はボス専用カードとして管理しているため、ここでは候補から除外する
    for (const auto& pair : database_) {
        if (pair.second.canEnemyUse && pair.second.id > 0 && pair.second.id < 100) {
            usableCards.push_back(pair.second);
        }
    }

    // 万が一、使えるカードが1枚も設定されていなかった時の保険
    if (usableCards.empty()) {
        return GetCardData(1);
    }

    // ランダム選択
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, (int)usableCards.size() - 1);

    return usableCards[dist(gen)];
}

Card CardDatabase::GetRandomPlayerCard() {
    std::vector<Card> dropCards;

    // データベースの中身を全部チェック
    for (const auto& pair : database_) {
        if (pair.second.id > 1 && pair.second.id < 100) {
            dropCards.push_back(pair.second);
        }
    }

    // 保険
    if (dropCards.empty()) {
        return GetCardData(2);
    }

    int randomIndex = rand() % static_cast<int>(dropCards.size());
    return dropCards[randomIndex];
}

Card CardDatabase::GetRandomBossRoomPlayerCard() {
    std::vector<Card> weightedDropCards;

    for (const auto& pair : database_) {
        const Card& card = pair.second;
        if (card.id <= 1 || card.id >= 100 || card.id == kBossRoomExcludedCardId) {
            continue;
        }

        int weight = card.effectType == CardEffectType::Attack ? kBossRoomAttackCardWeight : 1;
        for (int i = 0; i < weight; ++i) {
            weightedDropCards.push_back(card);
        }
    }

    if (weightedDropCards.empty()) {
        return GetCardData(2);
    }

    int randomIndex = rand() % static_cast<int>(weightedDropCards.size());
    return weightedDropCards[randomIndex];
}
