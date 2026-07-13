#include "game/egg/EggSystem.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/audio/AudioManager.h"
#include "engine/sdf/SDFVolumeObject.h"
#include <algorithm>
#include <cstdlib>

EggSystem::~EggSystem() = default; // unique_ptr<Obj3d> のため cpp 側で定義

namespace {
    // -1.0〜1.0 の簡易乱数（トレイルのばらつき用）
    float Rand11(){ return ( ( float ) std::rand() / RAND_MAX ) * 2.0f - 1.0f; }

    // --- 産卵演出（卵が芯から育つ出現エロージョン＋クロスフェード）の定数 ---
    //   ※以前は「敵ボール→卵」のモーフを見せていたが、出現だけのシンプルな演出に変更
    //     （敵の変換は踏みつけ時のSDF消滅演出＝CombatSystem 側が担当する）
    const float kBirthFxDuration = 0.35f; // 卵が育ちきるまでの秒数
    const float kBirthFadeTime   = 0.15f; // 育ちきった後、実体メッシュへクロスフェードする秒数
    const float kBirthFxScale    = 0.91f; // メッシュ卵(モデル高さ1.3×スケール0.7=0.91m)と同じ大きさ
    const float kBirthFullErode  = 0.5f * kBirthFxScale; // これだけ痩せると完全に消える量
    const Vector4 kBirthFxColor  = { 0.98f, 0.96f, 0.86f, 1.0f }; // 予備色（αはフェードで使う）
}

void EggSystem::Initialize(){
    eggs_.clear();
    puffs_.clear();
    spits_.clear();
    stomach_    = 0;
    trailTimer_ = 0.0f;
    // 産卵演出の状態もリセット（birthFx_ 本体は読み込み済みのまま使い回す）
    birthEgg_   = nullptr;
    birthTimer_ = -1.0f;
}

// 産卵演出のセットアップ。
//   卵(egg.sdf3d)単体を読み込み、産む瞬間に「芯から育てて」出現させる。
//   （旧実装の敵ボール→卵モーフは廃止。モーフ用の2ボリューム目も読まないぶん軽い）
void EggSystem::InitializeBirthFx(ID3D12GraphicsCommandList* commandList){
    birthFx_ = std::make_unique<SDFVolumeObject>();
    if ( !birthFx_->Load("resources/sdf3d/egg.sdf3d", commandList) ) {
        birthFx_.reset(); // 無ければ演出なし（従来のポン出しに自動フォールバック）
        return;
    }
    birthFx_->SetScale(kBirthFxScale);
    birthFx_->SetColor({ 0.98f, 0.96f, 0.86f, 1.0f }); // カラーボリュームが無い時の予備色
}

// 演出中のSDF卵の描画（専用PSOに切り替えるためMRTパスの最後で呼ばれる）
void EggSystem::DrawBirthFx(ID3D12GraphicsCommandList* commandList){
    if ( birthTimer_ >= 0.0f && birthFx_ ) { birthFx_->Draw(commandList); }
}

// 実体パフを1個出す（fxSphere=色付きの粒／eggShell=殻の欠片）。縮みながら消えるので加算でなくても確実に見える。
void EggSystem::SpawnPuff(const Vector3& pos, const Vector3& vel, const Vector4& color, float scale, float life,
                          const std::string& modelName){
    TrailPuff p;
    p.obj = Obj3d::Create(modelName); // 既定カメラが自動バインド
    if ( p.obj ) {
        p.obj->SetScale({ scale, scale, scale });
        // 殻の欠片(eggShell)は自前の色をそのまま見せる。汎用の粒(fxSphere)だけ呼び出し側の色を付ける。
        if ( modelName == "fxSphere" && p.obj->GetModel() && p.obj->GetModel()->GetMaterial() ) {
            p.obj->GetModel()->GetMaterial()->color = color;
        }
        // 殻は薄い湾曲面なので両面表示（裏返っても消えない）
        if ( modelName == "eggShell" ) {
            p.obj->SetPipelineType(PipelineType::Object3D_CullNone);
        }
        p.obj->SetTranslation(pos);
        p.obj->Update();
    }
    p.pos = pos; p.vel = vel; p.life = life; p.maxLife = life; p.baseScale = scale;
    p.rot = { Rand11() * 3.14f, Rand11() * 3.14f, Rand11() * 3.14f };       // ランダムな初期姿勢
    p.rotSpeed = { Rand11() * 8.0f, Rand11() * 8.0f, Rand11() * 8.0f };     // くるくる回りながら舞う
    puffs_.push_back(std::move(p));
}

// 敵を飲み込んだ → お腹に1匹ためる（卵にするのは LayEgg）。
void EggSystem::AddToBelly(){
    if ( stomach_ < kMaxEggs ) { ++stomach_; }
}

// しゃがみ等で産む：お腹を1減らして、birthPos に卵(Held)を1個生む。
//   持っている卵が満杯なら一番古い卵を割って捨ててから生む。
bool EggSystem::LayEgg(const Vector3& birthPos){
    if ( stomach_ <= 0 ) return false;
    --stomach_;

    if ( HeldCount() >= kMaxEggs ) {
        for ( auto& e : eggs_ ) {        // 先頭から順＝一番古い保持卵を捨てる
            if ( e->IsHeld() ) { e->Break(); break; }
        }
    }

    auto egg = std::make_unique<Egg>(birthPos);

    // 実体（見た目）：専用の卵モデル（resources/egg/egg.obj）を使う。
    //   ※以前は "sphere" を潰して代用していたが、"sphere" は敵とも共有しているモデルのため
    //   色を変えると敵まで一緒に染まってしまっていた。専用モデルにしたことでその干渉も無くなる。
    const Vector3 baseScale = { 0.7f, 0.7f, 0.7f }; // モデル自体が卵の形なので均等スケールでOK
    if ( auto obj = Obj3d::Create("egg") ) { // 既定カメラが自動でバインドされる
        obj->SetScale(baseScale);
        egg->AttachVisual(std::move(obj), baseScale);
    }
    eggs_.push_back(std::move(egg));

    // 産卵エロージョン演出を開始：実体メッシュを隠し、SDFの卵を「芯から育てる」。
    //   育ちきったら Update が実体メッシュへバトンタッチする
    if ( birthFx_ ) {
        birthEgg_   = eggs_.back().get();
        birthTimer_ = 0.0f;
        birthEgg_->SetVisualHidden(true);
    }
    return true;
}

void EggSystem::Update(const Vector3& playerPos, const Vector3& facing, float dt){
    // 保持中の卵に「後ろのスロット」を割り当てる（増えるほど後ろへ一列に並ぶ）。
    //   後ろ = facing の逆方向。i 番目ほど遠くに置く → 各卵が自分でそこへ寄っていく（整列＆追従）。
    int held = 0;
    for ( auto& e : eggs_ ) {
        if ( !e->IsHeld() ) continue;
        Vector3 slot = {
            playerPos.x - facing.x * ( 0.8f + held * 0.6f ),
            playerPos.y + 0.3f,
            playerPos.z - facing.z * ( 0.8f + held * 0.6f )
        };
        e->SetTarget(slot);
        ++held;
    }

    // 飛行トレイル：一定間隔で、飛んでいる卵の位置に煙パフ（白い小球）を置く。
    trailTimer_ += dt;
    bool emitTrail = false;
    if ( trailTimer_ >= 0.03f ) { trailTimer_ -= 0.03f; emitTrail = true; }

    // 各卵の状態と見た目を進める＋トレイル／割れた瞬間の殻飛び散り
    for ( auto& e : eggs_ ) {
        if ( e->IsFlying() && emitTrail ) {
            Vector3 p = e->GetPosition();
            Vector3 jitter = { Rand11() * 0.1f, Rand11() * 0.1f, Rand11() * 0.1f };
            SpawnPuff({ p.x + jitter.x, p.y + jitter.y, p.z + jitter.z },
                      { Rand11() * 0.4f, 0.4f + Rand11() * 0.2f, Rand11() * 0.4f },
                      { 0.95f, 0.97f, 1.0f, 1.0f }, 0.28f, 0.4f); // 白い煙
        }
        e->Update(dt);
        if ( e->JustBroke() ) { // 着弾／時間切れ：殻の欠片が飛び散り、黄身が splash する
            AudioManager::GetInstance()->PlayWave("resources/se/eggBreak.wav", false, 0.4f); // 割れる音
            Vector3 p = e->GetPosition();
            for ( int i = 0; i < 6; ++i ) { // 殻の欠片（くるくる回りながら舞う）
                SpawnPuff(p, { Rand11() * 3.5f, 2.0f + Rand11() * 2.0f, Rand11() * 3.5f },
                          { 1.0f, 1.0f, 1.0f, 1.0f }, 0.22f, 0.45f, "eggShell");
            }
            for ( int i = 0; i < 6; ++i ) { // 黄身の飛沫（小さい黄色の粒）
                SpawnPuff(p, { Rand11() * 2.5f, 1.5f + Rand11() * 1.5f, Rand11() * 2.5f },
                          { 1.0f, 0.85f, 0.2f, 1.0f }, 0.12f, 0.35f);
            }
            e->ClearJustBroke();
        }
    }

    // --- 吐き出し弾の更新（軽い放物線＋転がり回転。地面/時間切れで煙になって消える）---
    for ( auto& s : spits_ ) {
        if ( s.dead ) continue;
        s.life  -= dt;
        s.vel.y -= 10.0f * dt; // 軽い重力＝少し先で落ちる放物線
        s.pos.x += s.vel.x * dt;
        s.pos.y += s.vel.y * dt;
        s.pos.z += s.vel.z * dt;
        s.spin  += 12.0f * dt;

        if ( s.pos.y - s.radius <= 0.0f || s.life <= 0.0f ) {
            // 地面(Y=0)に落ちた or 時間切れ → 白い煙を出して消える
            for ( int i = 0; i < 5; ++i ) {
                SpawnPuff(s.pos, { Rand11() * 1.5f, 0.8f + Rand11() * 0.6f, Rand11() * 1.5f },
                          { 0.95f, 0.97f, 1.0f, 1.0f }, 0.22f, 0.35f);
            }
            s.dead = true;
            continue;
        }
        if ( s.obj ) {
            s.obj->SetTranslation(s.pos);
            s.obj->SetRotation({ s.spin, 0.0f, 0.0f }); // 進行方向へ転がる見た目
            s.obj->Update();
        }
    }
    spits_.erase(std::remove_if(spits_.begin(), spits_.end(),
        [](const SpitBall& s){ return s.dead; }), spits_.end());

    // 実体パフの更新（移動＋軽い重力＋回転＋縮小、寿命切れで削除）
    for ( auto& p : puffs_ ) {
        p.life   -= dt;
        p.vel.y  -= 3.0f * dt;
        p.pos.x  += p.vel.x * dt;
        p.pos.y  += p.vel.y * dt;
        p.pos.z  += p.vel.z * dt;
        p.rot.x  += p.rotSpeed.x * dt;
        p.rot.y  += p.rotSpeed.y * dt;
        p.rot.z  += p.rotSpeed.z * dt;
        float r = ( p.maxLife > 0.0f ) ? ( p.life / p.maxLife ) : 0.0f;
        if ( r < 0.0f ) r = 0.0f;
        float s = p.baseScale * r;
        if ( p.obj ) {
            p.obj->SetTranslation(p.pos);
            p.obj->SetRotation(p.rot);
            p.obj->SetScale({ s, s, s });
            p.obj->Update();
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
        [](const TrailPuff& p){ return p.life <= 0.0f; }), puffs_.end());

    // 割れて消えてよくなった卵を後始末
    eggs_.erase(
        std::remove_if(eggs_.begin(), eggs_.end(),
            [](const std::unique_ptr<Egg>& e){ return e->IsDead(); }),
        eggs_.end());

    // --- 産卵演出の進行 ---
    //   ①SDFの卵が芯から育つ → ②実体メッシュを出した上に SDF卵を重ねたまま
    //   透明化（クロスフェード）→ 完全交代。
    //   パッと差し替えず重ねて溶かすので、質感の微差が切り替わりとして見えない。
    if ( birthTimer_ >= 0.0f && birthFx_ ) {
        // 対象の卵がまだ存在するか検証（満杯で割られた等でポインタが死ぬのを防ぐ）
        bool exists = false;
        for ( auto& e : eggs_ ) { if ( e.get() == birthEgg_ ) { exists = true; break; } }

        birthTimer_ += dt;
        float tGrow = birthTimer_ / kBirthFxDuration;                         // 0〜1: 出現（芯から育つ）
        float tFade = ( birthTimer_ - kBirthFxDuration ) / kBirthFadeTime;    // 0〜1: クロスフェード

        if ( !exists || !birthEgg_->IsHeld() || tFade >= 1.0f ) {
            // 演出終了（or 途中で投げられた/割られた）→ 完全に実体メッシュへ交代
            if ( exists ) { birthEgg_->SetVisualHidden(false); }
            birthEgg_   = nullptr;
            birthTimer_ = -1.0f;
        } else if ( tGrow >= 1.0f ) {
            // ②クロスフェード：実体メッシュを表示した上に、育ちきったSDF卵を重ねて透明化していく
            birthEgg_->SetVisualHidden(false);
            Vector4 c = kBirthFxColor;
            c.w = 1.0f - std::clamp(tFade, 0.0f, 1.0f);
            birthFx_->SetColor(c);
            birthFx_->SetErode(0.0f);
            birthFx_->SetTranslation(birthEgg_->GetPosition());
            birthFx_->Update();
        } else {
            // ①出現：エロージョンを解きながら卵を芯から育てる
            float grow = 1.0f - ( 1.0f - tGrow ) * ( 1.0f - tGrow ); // 2次アウト（最初勢いよく→ゆっくり整う）

            birthFx_->SetColor(kBirthFxColor); // α=1（次の産卵に備えて毎回リセット）
            birthFx_->SetTranslation(birthEgg_->GetPosition());
            birthFx_->SetErode(kBirthFullErode * ( 1.0f - grow ));
            birthFx_->Update(); // カメラ行列＋CBを毎フレーム焼き直す
        }
    }
}

void EggSystem::Draw() const{
    for ( const auto& e : eggs_ ) { e->Draw(); }
    for ( const auto& s : spits_ ) { if ( s.obj && !s.dead ) s.obj->Draw(); }        // 吐き出し弾
    for ( const auto& p : puffs_ ) { if ( p.obj && p.life > 0.0f ) p.obj->Draw(); } // 煙パフ
}

// 保持中の一番古い卵を指定方向へ投げる。
bool EggSystem::TryThrow(const Vector3& playerPos, const Vector3& dir, float speed){
    for ( auto& e : eggs_ ) {
        if ( !e->IsHeld() ) continue;
        Vector3 from = { playerPos.x, playerPos.y + 0.5f, playerPos.z };
        e->SetPosition(from);
        e->Throw(dir, speed);
        // 「ぽいっ」と投げる時の白い煙ひと吹き
        for ( int i = 0; i < 6; ++i ) {
            SpawnPuff(from, { Rand11() * 1.2f, 0.5f + Rand11() * 0.5f, Rand11() * 1.2f },
                      { 0.95f, 0.97f, 1.0f, 1.0f }, 0.25f, 0.35f);
        }
        return true;
    }
    return false;
}

// 飲み込み：口元で緑の小球がふわっと上へ吸い込まれる（リング無し＝踏みつけと別物）。
void EggSystem::SpawnSwallowFx(const Vector3& pos){
    for ( int i = 0; i < 9; ++i ) {
        SpawnPuff(pos, { Rand11() * 1.0f, 1.6f + Rand11() * 0.8f, Rand11() * 1.0f },
                  { 0.4f, 1.0f, 0.5f, 1.0f }, 0.2f, 0.32f); // 緑・上向き・速め
    }
}

// 産卵：白＆緑がぽわっと丸く広がる（生まれた感。ゆっくりめ）。
void EggSystem::SpawnLayFx(const Vector3& pos){
    for ( int i = 0; i < 11; ++i ) {
        Vector4 col = ( i % 2 ) ? Vector4{ 0.5f, 1.0f, 0.6f, 1.0f }   // 緑
                                : Vector4{ 0.95f, 1.0f, 0.95f, 1.0f }; // 白
        SpawnPuff(pos, { Rand11() * 1.5f, 0.4f + Rand11() * 0.5f, Rand11() * 1.5f },
                  col, 0.24f, 0.45f); // ゆっくり広がる
    }
}

// 命中：殻の欠片が勢いよく飛び散り、黄身が splash する（衝撃感。速い・多い）。
void EggSystem::SpawnHitFx(const Vector3& pos){
    for ( int i = 0; i < 8; ++i ) { // 殻の欠片（大きめ・速い・回転）
        SpawnPuff(pos, { Rand11() * 4.8f, 1.0f + Rand11() * 3.0f, Rand11() * 4.8f },
                  { 1.0f, 1.0f, 1.0f, 1.0f }, 0.24f, 0.4f, "eggShell");
    }
    for ( int i = 0; i < 8; ++i ) { // 黄身の飛沫
        SpawnPuff(pos, { Rand11() * 3.5f, 0.8f + Rand11() * 2.2f, Rand11() * 3.5f },
                  { 1.0f, 0.6f, 0.1f, 1.0f }, 0.14f, 0.32f);
    }
}

// お腹の敵を1匹、指定方向へ吐き出す（口元 from から dir 方向へ発射）
bool EggSystem::SpitOut(const Vector3& from, const Vector3& dir, float speed){
    if ( stomach_ <= 0 ) return false;
    --stomach_;

    SpitBall ball;
    ball.obj = Obj3d::Create("sphere"); // 敵と同じモンスターボール柄＝「飲んだ敵」がそのまま出てくる
    ball.pos = from;
    ball.vel = { dir.x * speed, 1.5f, dir.z * speed }; // 少し上向きに発射→軽い放物線を描く
    if ( ball.obj ) {
        ball.obj->SetScale({ ball.radius, ball.radius, ball.radius });
        ball.obj->SetTranslation(ball.pos);
        ball.obj->Update();
    }
    spits_.push_back(std::move(ball));

    // 「ぷはっ」と吐いた煙
    for ( int i = 0; i < 6; ++i ) {
        SpawnPuff(from, { dir.x * 1.5f + Rand11() * 0.8f, 0.6f + Rand11() * 0.5f, dir.z * 1.5f + Rand11() * 0.8f },
                  { 0.95f, 0.97f, 1.0f, 1.0f }, 0.24f, 0.35f);
    }
    return true;
}

// 飛行中の吐き出し弾を当たり判定にかけ、当たった弾を消す（敵側の処理は CombatSystem が行う）
void EggSystem::ResolveSpitHits(const std::function<bool(const Vector3&, float)>& onHit){
    for ( auto& s : spits_ ) {
        if ( s.dead ) continue;
        if ( onHit(s.pos, s.radius) ) {
            for ( int i = 0; i < 5; ++i ) { // ぶつかって弾けた煙
                SpawnPuff(s.pos, { Rand11() * 2.0f, 0.8f + Rand11() * 1.0f, Rand11() * 2.0f },
                          { 1.0f, 0.9f, 0.8f, 1.0f }, 0.2f, 0.3f);
            }
            s.dead = true;
        }
    }
}

// 飛行中の卵を当たり判定にかけ、当たった卵を割る（敵側の処理は onHit 内でシーンが行う）。
void EggSystem::ResolveHits(const std::function<bool(const Vector3&, float)>& onHit){
    for ( auto& e : eggs_ ) {
        if ( !e->IsFlying() ) continue;
        if ( onHit(e->GetPosition(), e->GetRadius()) ) {
            e->Break(); // 命中 → 割れる（星は次の Update が JustBroke を拾って出す）
        }
    }
}

int EggSystem::HeldCount() const{
    int n = 0;
    for ( const auto& e : eggs_ ) { if ( e->IsHeld() ) ++n; }
    return n;
}

int EggSystem::FlyingCount() const{
    int n = 0;
    for ( const auto& e : eggs_ ) { if ( e->IsFlying() ) ++n; }
    return n;
}
