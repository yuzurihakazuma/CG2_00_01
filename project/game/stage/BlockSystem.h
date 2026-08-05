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
    // 道の上面の高さ（レール線からの差）。RoadMesh 側を「上面＝レール線ぴったり」に
    // 揃えたので 0（レール線＝歩行面＝ブロック底面。全システムで基準が一致する）
    static constexpr float kSurfaceY = 0.0f;
    static constexpr int kTypeCount = 9; // ブロックの種類数（BlockData::type と対応）
    static constexpr int kTypeSpring = 4; // ジャンプ台（上に飛び乗ると大きく跳ねる）
    static constexpr int kTypeHatena = 5; // ？ブロック（下から頭突きするとコインが出る。1回きり）
    static constexpr int kTypeCloud  = 6; // すり抜け床（上には乗れる/下と横からは通り抜ける）
    static constexpr int kTypeWide     = 7; // 横長ブロック（進行方向2m。長い足場を1個で作れる）
    static constexpr int kTypePedestal = 8; // 台座ブロック（2×2m。道幅いっぱいの土台）

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
    //   重なったブロックの占有区間 [outMin, outMax]（プレイヤー半径ぶん拡張済み）と
    //   上面の高さ outTop（埋まった時の押し出し先）を返す
    bool  BlockedAt(int rail, float dist, float bodyBottom, float bodyTop,
                    float* outMin, float* outMax, float* outTop = nullptr) const;
    // 頭上の天井：dist に重なるブロックの底面のうち、footY より上で一番低いもの（無ければ大きな値）
    float CeilingHeightAt(int rail, float dist, float footY) const;
    // 今の足元を支えているブロックの種類（-1=レール面/ブロックなし）。ジャンプ台の判定に使う
    int   SupportTypeAt(int rail, float dist, float footY) const;

    // --- ？ブロック（頭突きでコイン）---
    // 頭をぶつけた時に Player が呼ぶ：ぶつけた先が未使用の？ブロックならコインを出す
    void  NotifyHeadBump(int rail, float dist, float headY);
    // 頭突きで出たコインのワールド位置（1フレーム分。シーンが取り出して演出とカウントに使う）
    bool  ConsumeBumpCoin(Vector3& outPos);
    // Play開始時：？ブロックを全部未使用に戻す
    void  ResetPlay();

private:
    struct Block {
        int   rail  = 0;
        float dist  = 0.0f; // レール上の距離(m)。1m刻み
        int   level = 0;    // 段数（1段=1m）
        float side  = 0.0f; // 道幅方向のずれ(m)。0=中心線
        int   type  = 0;    // 見た目の種類（0=スポンジ/1=段ボール層/2=斜面45°/3=ゆるい斜面/4=バネ/5=？/6=すり抜け/7=横長2m/8=台座2×2m）
        int   ascend = 1;   // 斜面の登り方向（+1=dist増加側が高い/-1=逆。隣のブロックから自動決定）
        bool  used  = false; // ？ブロック：このPlayで既にコインを出したか
    };
    static bool IsSlopeType(int type){ return type == 2 || type == 3; }
    std::vector<Block> blocks_;
    const std::vector<SplineRail>* rails_ = nullptr; // シーンの RailField が所有（借り物）

    // 種類ごとの一括描画グループ（クラフトブロック4種）＋自動デコの紙花2種。
    //   Obj3d は行列計算専用のプール（伸びるだけ）から借り、グループは非所有ポインタで持つ。
    //   ペイントのたびに Obj3d（GPUリソース2本持ち）を作り直さないための構造
    struct LookGroup {
        std::vector<Obj3d*> objs;      // プールから借りた行列計算機（非所有）
        std::vector<int> blockIndices; // objs[i] が対応する blocks_ の番号（花は -1）
        std::unique_ptr<InstancedGroup> batch;
    };
    LookGroup typeLooks_[kTypeCount]; // 種類別のブロック
    LookGroup usedHatenaLook_;        // 使用済みの？ブロック（灰色）
    LookGroup flowerLooks_[2];        // 花（0=オレンジ / 1=白い顔つき）。継ぎ目に自動で咲く

    // 頭突きで出たコインの位置（シーンが ConsumeBumpCoin で取り出す）
    std::vector<Vector3> bumpCoinQueue_;

    // 花の配置（Sync時に計算。動くレール追従のため rail/dist/高さで持つ）
    struct FlowerSpot {
        int   rail = 0;
        float dist = 0.0f;   // 継ぎ目の位置（セルの中間）
        float side = 0.0f;
        float topY = 0.0f;   // 咲かせる高さ（ブロック上面。レール面からの相対）
        float yawOffset = 0.0f; // 少しランダムに回す
        int   kind = 0;      // 0=オレンジ / 1=白
    };
    std::vector<FlowerSpot> flowers_;

    std::vector<std::unique_ptr<Obj3d>> objPool_; // 行列計算用プール（必要数まで伸びるだけ）
    void EnsurePool(size_t count);                // プールを count 個まで育てる

    // (レール, 1mセル) → そのセルにあるブロック番号。当たり判定と隣接判定の高速検索用
    std::unordered_map<uint64_t, std::vector<int>> cellMap_;

    void RebuildCellMap();                    // blocks_ から cellMap_ を作り直す
    bool HasBlockAt(int rail, int cell, int level, float side) const; // 隣接判定
    int  FindBlockIndexAt(int rail, int cell, int level, float side) const; // ブロック番号（-1=なし）
    void BuildLookGroups();                   // 種類ごとの見た目グループへ振り分け＋花の自動配置
    // このブロックの dist 位置での表面高さ（レール面からの相対）。斜面は位置で変わる
    float SurfaceHeightAt(const Block& block, float dist) const;
    // このブロックが dist 方向に当たりを持つ半幅（斜面26°・横長・台座は2mぶん）
    static float FootprintHalf(int type){
        return ( type == 3 || type == kTypeWide || type == kTypePedestal ) ? 1.0f : 0.5f;
    }
    // ブロックの中心ワールド座標（動くレールの現在位置・接線の横方向を反映）。
    //   outPitch を渡すと道の勾配（レール接線のピッチ角）も返す。
    //   ブロックはこの角度で傾けて描く＝坂の上でも底面が道にぴったり付く
    bool BlockWorldPos(const Block& block, Vector3& outPos, float& outYaw, float* outPitch = nullptr) const;
};
