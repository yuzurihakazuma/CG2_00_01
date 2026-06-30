#pragma once
#include "engine/3d/obj/Obj3d.h"
#include "engine/math/VectorMath.h"
#include "engine/math/struct.h"
#include <vector>
#include <memory>

class Camera;

// ヒットの種類（state で見た目を切り替える）。同じ立体エフェクトを色違いで使い分ける。
enum class StompEffectType {
    Stomp,   // 踏みつけ：水色の衝撃波＋黄色い星（既存のノリ）
    Swallow, // 飲み込み：ヨッシー緑のやわらかい吸い込み演出
    EggHit,  // 卵が命中：黄＋緑の「ぱしゃっ」とした着弾
};

// =====================================================================
//  StompEffect : エネミーへのヒット用3Dエフェクト (2.5D・立体クラフト版)
//  予兆のコア球体、中空円柱ドーム衝撃波、くるくる回るカラフル3Dキューブ、モコモコ煙
//  StompEffectType で色を切り替え、踏みつけ／飲み込み／卵命中を区別する。
// =====================================================================
class StompEffect {
public:
    // 発生位置、カメラ、テクスチャ、環境マップ、種類を渡して初期化
    void Initialize(const Vector3& position, Camera* camera, uint32_t textureIndex, uint32_t envMapIndex,
                    StompEffectType type = StompEffectType::Stomp);

    void Update(float dt);   // dt = 経過時間(秒)。timeScale 適用済みを渡すとヒットストップで止まる
    void Draw();

    // 全てのエフェクトパーツが消滅したら true を返す（削除判定用）
    bool IsDead() const { return isDead_; }

private:
    // 1. 予兆のコア球体 (一瞬で膨らんで消える)
    struct Core {
        std::unique_ptr<Obj3d> obj;
        float lifeTime;
        float currentTime;
        float startScale;
        float endScale;
    };

    // 2. 中空円柱（シリンダー）型衝撃波 (真横から見ても厚みがある)
    struct Cylinder {
        std::unique_ptr<Obj3d> obj;
        float lifeTime;
        float currentTime;
        float startScale;
        float endScale;
        float heightScale;
        Vector4 colorEnd { 0.4f, 0.85f, 1.0f, 1.0f }; // 白フラッシュ→この色へ冷えていく
    };

    // 2-B. 放射状に広がる星型の輪 (StarRing)
    struct StarRing {
        std::unique_ptr<Obj3d> obj;
        Vector3 direction; // 広がる方向ベクトル
        Vector3 rotation;
        Vector3 rotationSpeed;
        float lifeTime;
        float currentTime;
        float startRadius;
        float endRadius;
        float baseScale;
        Vector4 color { 1.0f, 0.9f, 0.1f, 1.0f }; // 星の色
    };

    // 3. 飛び散るポップな3D星 (ヨッシーの工作風)
    struct PopParticle {
        std::unique_ptr<Obj3d> obj;
        Vector3 velocity;
        Vector3 rotation;
        Vector3 rotationSpeed;
        float lifeTime;
        float currentTime;
        Vector3 baseScale;
        Vector4 color;
    };

    // 4. 足元のモコモコ煙
    struct Smoke {
        std::unique_ptr<Obj3d> obj;
        Vector3 offset;
        float lifeTime;
        float currentTime;
        float baseScale;
        float rotationSpeed;
        float currentRotation;
        Vector4 color { 0.9f, 0.92f, 0.95f, 0.55f }; // 煙の色（wが基準アルファ）
    };

    std::vector<Core> cores_;
    std::vector<Cylinder> cylinders_;
    std::vector<StarRing> starRings_;
    std::vector<PopParticle> popParticles_;
    std::vector<Smoke> smokes_;

    bool isDead_ = false;
};
