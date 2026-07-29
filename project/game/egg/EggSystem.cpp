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
    // 表示中パフの Obj3d はプールへ返してから消す。捨てるとモード切替のたびに
    // プールが痩せていき、いずれ初回バーストのカクつきが再発してしまう
    for ( auto& puff : puffs_ ) {
        if ( puff.obj ) {
            auto& pool = puffPool_[puff.model];
            if ( pool.size() < 32 ) { pool.push_back(std::move(puff.obj)); }
        }
    }
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

// パフ用 Obj3d の事前生成：ロード時にプールへ積んでおく（初回バーストの固まり対策）
void EggSystem::PrewarmPuffPool(const std::string& modelName, int count){
    auto& pool = puffPool_[modelName];
    while ( ( int ) pool.size() < count ) {
        auto obj = Obj3d::Create(modelName);
        if ( !obj ) break; // モデル未登録なら何もしない（SpawnPuff 側と同じ扱い）
        pool.push_back(std::move(obj));
    }
}

// 実体パフを1個出す（fxSphere=色付きの粒／eggShell=殻の欠片）。縮みながら消えるので加算でなくても確実に見える。
void EggSystem::SpawnPuff(const Vector3& pos, const Vector3& vel, const Vector4& color, float scale, float life,
                          const std::string& modelName){
    TrailPuff puff;
    // 同じモデルの使い終わった Obj3d があれば使い回す（Create はGPUリソース確保を伴い、
    // 敵撃破や卵割れの一斉バーストでその瞬間だけ重くなるため）
    auto poolIt = puffPool_.find(modelName);
    if ( poolIt != puffPool_.end() && !poolIt->second.empty() ) {
        puff.obj = std::move(poolIt->second.back());
        poolIt->second.pop_back();
    } else {
        puff.obj = Obj3d::Create(modelName); // 既定カメラが自動バインド
    }
    puff.model = modelName;
    if ( puff.obj ) {
        puff.obj->SetScale({ scale, scale, scale });
        // 殻の欠片(eggShell)は自前の色をそのまま見せる。汎用の粒(fxSphere)だけ呼び出し側の色を付ける。
        if ( modelName == "fxSphere" && puff.obj->GetModel() && puff.obj->GetModel()->GetMaterial() ) {
            puff.obj->GetModel()->GetMaterial()->color = color;
        }
        // 殻は薄い湾曲面なので両面表示（裏返っても消えない）
        if ( modelName == "eggShell" ) {
            puff.obj->SetPipelineType(PipelineType::Object3D_CullNone);
        }
        puff.obj->SetTranslation(pos);
        puff.obj->Update();
    }
    puff.pos = pos; puff.vel = vel; puff.life = life; puff.maxLife = life; puff.baseScale = scale;
    puff.rot = { Rand11() * 3.14f, Rand11() * 3.14f, Rand11() * 3.14f };       // ランダムな初期姿勢
    puff.rotSpeed = { Rand11() * 8.0f, Rand11() * 8.0f, Rand11() * 8.0f };     // くるくる回りながら舞う
    puffs_.push_back(std::move(puff));
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
        for ( auto& heldEgg : eggs_ ) {        // 先頭から順＝一番古い保持卵を捨てる
            if ( heldEgg->IsHeld() ) { heldEgg->Break(); break; }
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
        // 連続産卵対策：前の演出対象がまだ非表示のままなら、先に実体表示へ戻す
        //   （戻さないと演出が新しい卵へ移った時、前の卵が永久に見えなくなる）
        if ( birthEgg_ ) {
            for ( auto& egg : eggs_ ) {
                if ( egg.get() == birthEgg_ ) { birthEgg_->SetVisualHidden(false); break; }
            }
        }
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
    for ( auto& egg : eggs_ ) {
        if ( !egg->IsHeld() ) continue;
        Vector3 slot = {
            playerPos.x - facing.x * ( 0.8f + held * 0.6f ),
            playerPos.y + 0.3f,
            playerPos.z - facing.z * ( 0.8f + held * 0.6f )
        };
        egg->SetTarget(slot);
        ++held;
    }

    // 飛行トレイル：一定間隔で、飛んでいる卵の位置に煙パフ（白い小球）を置く。
    trailTimer_ += dt;
    bool emitTrail = false;
    if ( trailTimer_ >= 0.03f ) { trailTimer_ -= 0.03f; emitTrail = true; }

    // 各卵の状態と見た目を進める＋トレイル／割れた瞬間の殻飛び散り
    for ( auto& egg : eggs_ ) {
        if ( egg->IsFlying() && emitTrail ) {
            Vector3 eggPos = egg->GetPosition();
            Vector3 jitter = { Rand11() * 0.1f, Rand11() * 0.1f, Rand11() * 0.1f };
            SpawnPuff({ eggPos.x + jitter.x, eggPos.y + jitter.y, eggPos.z + jitter.z },
                      { Rand11() * 0.4f, 0.4f + Rand11() * 0.2f, Rand11() * 0.4f },
                      { 0.95f, 0.97f, 1.0f, 1.0f }, 0.28f, 0.4f); // 白い煙
        }
        egg->Update(dt);
        if ( egg->JustBroke() ) { // 着弾／時間切れ：殻の欠片が飛び散り、黄身が splash する
            AudioManager::GetInstance()->PlayWave("resources/se/eggBreak.wav", false, 0.4f); // 割れる音
            Vector3 breakPos = egg->GetPosition();
            for ( int i = 0; i < 6; ++i ) { // 殻の欠片（くるくる回りながら舞う）
                SpawnPuff(breakPos, { Rand11() * 3.5f, 2.0f + Rand11() * 2.0f, Rand11() * 3.5f },
                          { 1.0f, 1.0f, 1.0f, 1.0f }, 0.22f, 0.45f, "eggShell");
            }
            for ( int i = 0; i < 6; ++i ) { // 黄身の飛沫（小さい黄色の粒）
                SpawnPuff(breakPos, { Rand11() * 2.5f, 1.5f + Rand11() * 1.5f, Rand11() * 2.5f },
                          { 1.0f, 0.85f, 0.2f, 1.0f }, 0.12f, 0.35f);
            }
            egg->ClearJustBroke();
        }
    }

    // --- 吐き出し弾の更新（軽い放物線＋転がり回転。地面/時間切れで煙になって消える）---
    for ( auto& spit : spits_ ) {
        if ( spit.dead ) continue;
        spit.life  -= dt;
        spit.vel.y -= 10.0f * dt; // 軽い重力＝少し先で落ちる放物線
        spit.pos.x += spit.vel.x * dt;
        spit.pos.y += spit.vel.y * dt;
        spit.pos.z += spit.vel.z * dt;
        spit.spin  += 12.0f * dt;

        if ( spit.pos.y - spit.radius <= 0.0f || spit.life <= 0.0f ) {
            // 地面(Y=0)に落ちた or 時間切れ → 白い煙を出して消える
            for ( int i = 0; i < 5; ++i ) {
                SpawnPuff(spit.pos, { Rand11() * 1.5f, 0.8f + Rand11() * 0.6f, Rand11() * 1.5f },
                          { 0.95f, 0.97f, 1.0f, 1.0f }, 0.22f, 0.35f);
            }
            spit.dead = true;
            continue;
        }
        if ( spit.obj ) {
            spit.obj->SetTranslation(spit.pos);
            spit.obj->SetRotation({ spit.spin, 0.0f, 0.0f }); // 進行方向へ転がる見た目
            spit.obj->Update();
        }
    }
    spits_.erase(std::remove_if(spits_.begin(), spits_.end(),
        [](const SpitBall& spit){ return spit.dead; }), spits_.end());

    // 実体パフの更新（移動＋軽い重力＋回転＋縮小、寿命切れで削除）
    for ( auto& puff : puffs_ ) {
        puff.life   -= dt;
        puff.vel.y  -= 3.0f * dt;
        puff.pos.x  += puff.vel.x * dt;
        puff.pos.y  += puff.vel.y * dt;
        puff.pos.z  += puff.vel.z * dt;
        puff.rot.x  += puff.rotSpeed.x * dt;
        puff.rot.y  += puff.rotSpeed.y * dt;
        puff.rot.z  += puff.rotSpeed.z * dt;
        float lifeRatio = ( puff.maxLife > 0.0f ) ? ( puff.life / puff.maxLife ) : 0.0f;
        if ( lifeRatio < 0.0f ) lifeRatio = 0.0f;
        float shrunkScale = puff.baseScale * lifeRatio;
        if ( puff.obj ) {
            puff.obj->SetTranslation(puff.pos);
            puff.obj->SetRotation(puff.rot);
            puff.obj->SetScale({ shrunkScale, shrunkScale, shrunkScale });
            puff.obj->Update();
        }
    }
    // 寿命切れのパフは Obj3d をプールへ返してから消す（次のバーストで使い回す）
    for ( auto& puff : puffs_ ) {
        if ( puff.life <= 0.0f && puff.obj ) {
            auto& pool = puffPool_[puff.model];
            if ( pool.size() < 32 ) { pool.push_back(std::move(puff.obj)); }
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
        [](const TrailPuff& puff){ return puff.life <= 0.0f; }), puffs_.end());

    // 割れて消えてよくなった卵を後始末
    eggs_.erase(
        std::remove_if(eggs_.begin(), eggs_.end(),
            [](const std::unique_ptr<Egg>& egg){ return egg->IsDead(); }),
        eggs_.end());

    // --- 産卵演出の進行 ---
    //   ①SDFの卵が芯から育つ → ②実体メッシュを出した上に SDF卵を重ねたまま
    //   透明化（クロスフェード）→ 完全交代。
    //   パッと差し替えず重ねて溶かすので、質感の微差が切り替わりとして見えない。
    if ( birthTimer_ >= 0.0f && birthFx_ ) {
        // 対象の卵がまだ存在するか検証（満杯で割られた等でポインタが死ぬのを防ぐ）
        bool exists = false;
        for ( auto& egg : eggs_ ) { if ( egg.get() == birthEgg_ ) { exists = true; break; } }

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
            Vector4 fadeColor = kBirthFxColor;
            fadeColor.w = 1.0f - std::clamp(tFade, 0.0f, 1.0f);
            birthFx_->SetColor(fadeColor);
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
    // 構え中は「次に投げる卵（列の先頭）」を1個描かない＝手に持っている扱い。
    // TryThrow も先頭の保持卵を投げるので、隠した卵と投げる卵は必ず一致する
    bool handEggSkipped = !aimHolding_;
    for ( const auto& egg : eggs_ ) {
        if ( !handEggSkipped && egg->IsHeld() ) { handEggSkipped = true; continue; }
        egg->Draw();
    }
    for ( const auto& spit : spits_ ) { if ( spit.obj && !spit.dead ) spit.obj->Draw(); }        // 吐き出し弾
    for ( const auto& puff : puffs_ ) { if ( puff.obj && puff.life > 0.0f ) puff.obj->Draw(); } // 煙パフ
}

// 保持中の一番古い卵を指定方向へ投げる。
bool EggSystem::TryThrow(const Vector3& playerPos, const Vector3& dir, float speed){
    for ( auto& egg : eggs_ ) {
        if ( !egg->IsHeld() ) continue;
        Vector3 from = { playerPos.x, playerPos.y + 0.5f, playerPos.z };
        egg->SetPosition(from);
        egg->Throw(dir, speed);
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
        Vector4 puffColor = ( i % 2 ) ? Vector4{ 0.5f, 1.0f, 0.6f, 1.0f }   // 緑
                                : Vector4{ 0.95f, 1.0f, 0.95f, 1.0f }; // 白
        SpawnPuff(pos, { Rand11() * 1.5f, 0.4f + Rand11() * 0.5f, Rand11() * 1.5f },
                  puffColor, 0.24f, 0.45f); // ゆっくり広がる
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
    for ( auto& spit : spits_ ) {
        if ( spit.dead ) continue;
        if ( onHit(spit.pos, spit.radius) ) {
            for ( int i = 0; i < 5; ++i ) { // ぶつかって弾けた煙
                SpawnPuff(spit.pos, { Rand11() * 2.0f, 0.8f + Rand11() * 1.0f, Rand11() * 2.0f },
                          { 1.0f, 0.9f, 0.8f, 1.0f }, 0.2f, 0.3f);
            }
            spit.dead = true;
        }
    }
}

// 飛行中の卵を当たり判定にかけ、当たった卵を割る（敵側の処理は onHit 内でシーンが行う）。
void EggSystem::ResolveHits(const std::function<bool(const Vector3&, float)>& onHit){
    for ( auto& egg : eggs_ ) {
        if ( !egg->IsFlying() ) continue;
        if ( onHit(egg->GetPosition(), egg->GetRadius()) ) {
            egg->Break(); // 命中 → 割れる（星は次の Update が JustBroke を拾って出す）
        }
    }
}

int EggSystem::HeldCount() const{
    int count = 0;
    for ( const auto& egg : eggs_ ) { if ( egg->IsHeld() ) ++count; }
    return count;
}

int EggSystem::FlyingCount() const{
    int count = 0;
    for ( const auto& egg : eggs_ ) { if ( egg->IsFlying() ) ++count; }
    return count;
}
