#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include "engine/math/struct.h"
#include "engine/utils/Level/LevelData.h" // BlockData

class Obj3d;
class SplineRail;
class Camera;
class InstancedGroup;

// =====================================================================
//  BlockSystem（乗れる/ぶつかる1m角ブロック）
//   エディタでレール上に置いたブロック（距離・段数・横ずれ）を実体化する。
//
//   ・座標系：レール空間（レール上の距離 × 上方向の段 × 道幅方向の横ずれ）。
//     プレイヤーも距離＋高さで動くので、当たり判定が変換なしでそのまま噛み合い、
//     動くレール（リフト等）に乗せたブロックも一緒に動く。
//   ・描画：InstancedGroup で一括描画。見た目は隣接で2種類に分かれるが、
//     グループごとに1ドローコールなので、何百個置いても2回で描き切る。
//   ・当たり判定：(レール, 1mセル) のハッシュで引くので個数が増えても速度が落ちない。
// =====================================================================
class BlockSystem {
public:
    static constexpr float kSize = 1.0f; // 1ブロック = 1m角

    BlockSystem();
    ~BlockSystem(); // InstancedGroup を前方宣言で持つため cpp 側で定義

    // 一括描画の準備（モデル生成後に1回だけ呼ぶ）。noiseSrv はディゾルブ用の
    // ダミーテクスチャ（閾値0なので見た目には影響しないが、束縛先として必要）
    void Initialize(uint32_t noiseSrvIndex);

    // エディタのブロック配置とレールから実体を作り直す
    void Sync(const std::vector<BlockData>& blockDatas, const std::vector<SplineRail>* rails);
    void Update();                 // 位置・向きの更新（動くレール追従＋インスタンス行列の収集）
    void Draw(const Camera* camera); // 一括描画（見た目グループごとに1ドローコール）
    int  Count() const{ return ( int ) blocks_.size(); }

    // --- レール空間の当たり判定（Player が使う）---
    //   いずれも「道の中心線に重なるブロック」だけを対象にする（横へずらした飾りは当たらない）
    // 足元の支持面：dist に重なるブロックのうち、上面が footY+0.25 以下で一番高いもの（無ければ 0=レール面）
    float GroundHeightAt(int rail, float dist, float footY) const;
    // 体の高さ帯 [bodyBottom, bodyTop] が dist のブロックに横から重なるか。
    //   重なったブロックの占有区間 [outMin, outMax]（プレイヤー半径ぶん拡張済み）を返す
    bool  BlockedAt(int rail, float dist, float bodyBottom, float bodyTop,
                    float* outMin, float* outMax) const;
    // 頭上の天井：dist に重なるブロックの底面のうち、footY より上で一番低いもの（無ければ大きな値）
    float CeilingHeightAt(int rail, float dist, float footY) const;

private:
    struct Block {
        int   rail  = 0;
        float dist  = 0.0f; // レール上の距離(m)。1m刻み
        int   level = 0;    // 段数（1段=1m）
        float side  = 0.0f; // 道幅方向のずれ(m)。0=中心線
    };
    std::vector<Block> blocks_;
    const std::vector<SplineRail>* rails_ = nullptr; // シーンの RailField が所有（借り物）

    // 見た目2種：上が空いている＝乗る面（明るい） / 上が塞がっている＝内部（暗い）。
    //   グループごとに Obj3d の配列を持ち、InstancedGroup へまとめて渡す
    struct LookGroup {
        std::vector<std::unique_ptr<Obj3d>> objs; // インスタンスの見た目（行列計算用）
        std::vector<int> blockIndices;            // objs[i] が対応する blocks_ の番号
        std::unique_ptr<InstancedGroup> batch;
    };
    LookGroup topLook_;  // 上に何も無い（乗れる面）
    LookGroup fillLook_; // 上にブロックがある（積み上げた内部）

    // (レール, 1mセル) → そのセルにあるブロック番号。当たり判定と隣接判定の高速検索用
    std::unordered_map<uint64_t, std::vector<int>> cellMap_;

    void RebuildCellMap();                    // blocks_ から cellMap_ を作り直す
    bool HasBlockAt(int rail, int cell, int level, float side) const; // 隣接判定
    void BuildLookGroups();                   // 隣接に応じて見た目グループへ振り分け
    // ブロックの中心ワールド座標（動くレールの現在位置・接線の横方向を反映）
    bool BlockWorldPos(const Block& block, Vector3& outPos, float& outYaw) const;
};
