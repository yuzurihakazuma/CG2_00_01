#include "game/rail/RailField.h"

#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/camera/Camera.h"
#include "engine/utils/EditorManager.h"
#include "engine/graphics/PipelineManager.h"

#include <cmath>
#include <algorithm>

RailField::RailField() = default;
RailField::~RailField() = default; // unique_ptr<Obj3d> のため cpp 側で定義

// =====================================================================
//  レール間の接続情報をロード時に1回だけ計算する。
//   端点(front/back)同士が近いレールを「連結」とみなし、最も近い端点を選ぶ（交差点で誤接続しない）。
//   連結端点は中点へ"溶接"してノードを一致させる → 隙間があっても乗り継ぎでワープしない。
//   ※動くレール(HasMotion)は静的連結しない（位置が変わるため。動的ドッキングに任せる）。
// =====================================================================
namespace {
void BuildRailConnections(std::vector<SplineRail>& rails){
    for ( auto& r : rails ){
        r.frontConnIndex = -1;
        r.backConnIndex  = -1;
        r.branchPoints.clear();
    }

    auto endDist = [](const Vector3& a, const Vector3& b) -> float{
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
    auto midPoint = [](const Vector3& a, const Vector3& b) -> Vector3{
        return { ( a.x + b.x ) * 0.5f, ( a.y + b.y ) * 0.5f, ( a.z + b.z ) * 0.5f };
        };

    const float kConnThreshold = 0.7f; // エディタの吸着半径と同じ（吸着済みノードは動かない）

    // フェーズ0：ループ（円状レール）の検出。front と back が近ければ閉じたレール。
    for ( auto& r : rails ){
        r.isLoop = false;
        if ( r.nodes.size() >= 3 ){
            if ( endDist(r.nodes.front(), r.nodes.back()) < kConnThreshold ){
                Vector3 m = midPoint(r.nodes.front(), r.nodes.back());
                r.nodes.front() = m;
                r.nodes.back()  = m;
                r.isLoop = true;
                r.BuildDistanceTable();
            }
        }
    }

    // フェーズ1：最も近い端点同士で連結関係を決める（動くレールは除外）。
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( rails[i].isLoop ) continue;
        if ( rails[i].HasMotion() ) continue;
        if ( rails[i].nodes.size() < 2 ) continue;
        const Vector3& iFront = rails[i].nodes.front();
        const Vector3& iBack  = rails[i].nodes.back();

        float bestFront = kConnThreshold;
        float bestBack  = kConnThreshold;

        for ( int j = 0; j < ( int ) rails.size(); ++j ){
            if ( i == j ) continue;
            if ( rails[j].HasMotion() ) continue;
            if ( rails[j].nodes.size() < 2 ) continue;
            const Vector3& jFront = rails[j].nodes.front();
            const Vector3& jBack  = rails[j].nodes.back();

            float ff = endDist(iFront, jFront);
            float fb = endDist(iFront, jBack);
            if ( ff < bestFront ){ bestFront = ff; rails[i].frontConnIndex = j; rails[i].frontConnToFront = true; }
            if ( fb < bestFront ){ bestFront = fb; rails[i].frontConnIndex = j; rails[i].frontConnToFront = false; }

            float bf = endDist(iBack, jFront);
            float bb = endDist(iBack, jBack);
            if ( bf < bestBack ){ bestBack = bf; rails[i].backConnIndex = j; rails[i].backConnToFront = true; }
            if ( bb < bestBack ){ bestBack = bb; rails[i].backConnIndex = j; rails[i].backConnToFront = false; }
        }
    }

    // フェーズ2：連結端点を溶接（隙間を物理的に詰める）。
    //   【相互合意チェック】i→j と j→i が互いに選び合っている組だけ「中点」へ溶接する。
    //   片思い（jは別のレールkの方が近い）の場合、双方を別々の中点へ動かすと
    //   数cmの隙間/段差が残るバグがあった。片思い側は相手の"今の"端点位置へ
    //   ぴったりスナップさせることで、必ず一致させる。
    struct Ends { Vector3 f, b; bool valid; };
    std::vector<Ends> orig(rails.size());
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( rails[i].nodes.size() < 2 ){ orig[i].valid = false; continue; }
        orig[i].f = rails[i].nodes.front();
        orig[i].b = rails[i].nodes.back();
        orig[i].valid = true;
    }
    auto mid = [](const Vector3& a, const Vector3& b) -> Vector3{
        return { ( a.x + b.x ) * 0.5f, ( a.y + b.y ) * 0.5f, ( a.z + b.z ) * 0.5f };
        };
    // レール j の端(front/back)が「レール i の端 e」を選び返しているか
    auto mutualBack = [&](int i, bool iIsFront, int j, bool jEndIsFront) -> bool{
        int  jConn      = jEndIsFront ? rails[j].frontConnIndex   : rails[j].backConnIndex;
        bool jConnFront = jEndIsFront ? rails[j].frontConnToFront : rails[j].backConnToFront;
        return jConn == i && jConnFront == iIsFront;
        };

    bool moved = false;
    // パスA：相互ペアは中点へ（両者同じ点になる）
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( !orig[i].valid ) continue;
        if ( rails[i].frontConnIndex >= 0 ){
            int j = rails[i].frontConnIndex;
            if ( orig[j].valid && mutualBack(i, true, j, rails[i].frontConnToFront) ){
                Vector3 partner = rails[i].frontConnToFront ? orig[j].f : orig[j].b;
                rails[i].nodes.front() = mid(orig[i].f, partner);
                moved = true;
            }
        }
        if ( rails[i].backConnIndex >= 0 ){
            int j = rails[i].backConnIndex;
            if ( orig[j].valid && mutualBack(i, false, j, rails[i].backConnToFront) ){
                Vector3 partner = rails[i].backConnToFront ? orig[j].f : orig[j].b;
                rails[i].nodes.back() = mid(orig[i].b, partner);
                moved = true;
            }
        }
    }
    // パスB：片思いの端は、相手の「今の（溶接後の）」端点位置へぴったりスナップ
    for ( int i = 0; i < ( int ) rails.size(); ++i ){
        if ( !orig[i].valid ) continue;
        if ( rails[i].frontConnIndex >= 0 ){
            int j = rails[i].frontConnIndex;
            if ( orig[j].valid && !mutualBack(i, true, j, rails[i].frontConnToFront) ){
                rails[i].nodes.front() = rails[i].frontConnToFront ? rails[j].nodes.front() : rails[j].nodes.back();
                moved = true;
            }
        }
        if ( rails[i].backConnIndex >= 0 ){
            int j = rails[i].backConnIndex;
            if ( orig[j].valid && !mutualBack(i, false, j, rails[i].backConnToFront) ){
                rails[i].nodes.back() = rails[i].backConnToFront ? rails[j].nodes.front() : rails[j].nodes.back();
                moved = true;
            }
        }
    }

    // フェーズ3：ノードを動かしたので距離テーブルを作り直す。
    if ( moved ){
        for ( auto& r : rails ){ r.BuildDistanceTable(); }
    }

    // フェーズ4：途中分岐（branchPoints）の構築。
    //   レール j の端点が、レール i の「途中（端から離れた本体）」に接している T字路 を検出し、
    //   レール i 側に分岐点として登録する。同タイプ同士のT字路でも乗り換えできるようになる。
    for ( int j = 0; j < ( int ) rails.size(); ++j ){
        const SplineRail& rj = rails[j];
        if ( rj.nodes.size() < 2 || rj.isLoop || rj.HasMotion() ) continue;

        struct EndInfo { Vector3 pos; bool isFront; int connIdx; };
        const EndInfo ends[2] = {
            { rj.nodes.front(), true,  rj.frontConnIndex },
            { rj.nodes.back(),  false, rj.backConnIndex  },
        };
        for ( const auto& e : ends ){
            if ( e.connIdx >= 0 ) continue; // 端点同士で連結済みなら分岐扱いにしない
            for ( int i = 0; i < ( int ) rails.size(); ++i ){
                if ( i == j ) continue;
                SplineRail& ri = rails[i];
                if ( ri.nodes.size() < 2 || ri.HasMotion() ) continue;
                float len = ri.GetLength();
                if ( len < 1.5f ) continue;

                float cd = ri.GetClosestDistance(e.pos);
                if ( cd < 0.6f || cd > len - 0.6f ) continue; // 端付近は端点連結の領分
                Vector3 cp = ri.GetPositionByDistance(cd);
                float dx = cp.x - e.pos.x, dy = cp.y - e.pos.y, dz = cp.z - e.pos.z;
                if ( std::sqrt(dx * dx + dy * dy + dz * dz) > kConnThreshold ) continue;

                // 分岐キーの向き：接合点から j 側へ少し入った点の「交差軸」変位で決める
                //   i が横レール → W/S(±Z) で分岐 / i が縦レール → D/A(±X) で分岐
                float probe = ( std::min )( 1.0f, rj.GetLength() * 0.5f );
                Vector3 into = rj.GetPositionByDistance(e.isFront ? probe : rj.GetLength() - probe);
                float cross = ( ri.type == SplineRail::RailType::Horizontal ) ? ( into.z - cp.z )
                                                                              : ( into.x - cp.x );
                if ( std::abs(cross) < 0.3f ) continue; // 交差方向が曖昧（ほぼ平行）→キーに割当不能

                SplineRail::BranchPoint bp;
                bp.distance   = cd;
                bp.targetRail = j;
                bp.targetDist = e.isFront ? 0.0f : rj.GetLength();
                bp.zSign      = ( cross >= 0.0f ) ? +1 : -1;
                ri.branchPoints.push_back(bp);
                break; // この端は登録済み。他のレールへの多重登録はしない
            }
        }
    }
}
} // namespace

// エディタ保持の最新レール節点から rails_ を作り直す。
void RailField::Sync(Camera* camera, uint32_t whiteTexIndex){
    camera_ = camera;
    whiteTexIndex_ = whiteTexIndex;

    EditorManager* em = EditorManager::GetInstance();
    const auto& lines   = em->GetEditorRailLines();
    const auto& types   = em->GetEditorRailTypes();
    const auto& motions = em->GetEditorRailMotions();

    // 線のつなぎ方（0=スプライン/1=直線）は距離テーブルの計測結果を変えるので、
    // 必ず BuildDistanceTable より先に割り当てる
    const auto& lineModes = em->GetEditorRailLineModes();

    rails_.clear();
    for ( size_t i = 0; i < lines.size(); ++i ) {
        SplineRail rail;
        rail.nodes = lines[i];
        rail.lineMode = ( i < lineModes.size() ) ? lineModes[i] : 0;
        rail.BuildDistanceTable();
        rails_.push_back(rail);
    }

    // 動きを「連結処理より先」に割り当てる（連結処理が HasMotion を判定できるように）。
    const auto& motionTypes  = em->GetEditorRailMotionTypes();
    const auto& guideRails   = em->GetEditorRailGuideRails();
    const auto& motionPhases = em->GetEditorRailMotionPhases();
    const auto& guideStarts  = em->GetEditorRailGuideStarts();
    const auto& guideEnds    = em->GetEditorRailGuideEnds();
    const auto& guideModes   = em->GetEditorRailGuideModes();
    const auto& motionTriggers = em->GetEditorRailMotionTriggers();
    const auto& appearTriggers = em->GetEditorRailAppearTriggers();
    const auto& guideAligns    = em->GetEditorRailGuideAligns();
    const auto& guideDwells    = em->GetEditorRailGuideDwells();
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        if ( i < motions.size() ) {
            rails_[i].motionAmp    = { motions[i].x, motions[i].y, motions[i].z };
            rails_[i].motionPeriod = ( motions[i].w > 0.1f ) ? motions[i].w : 0.1f;
        }
        rails_[i].motionType  = ( i < motionTypes.size() )  ? motionTypes[i]  : 0;
        rails_[i].guideRail   = ( i < guideRails.size() )   ? guideRails[i]   : -1;
        rails_[i].motionPhase = ( i < motionPhases.size() ) ? motionPhases[i] : 0.0f;
        rails_[i].guideStart  = ( i < guideStarts.size() )  ? guideStarts[i]  : 0.0f;
        rails_[i].guideEnd    = ( i < guideEnds.size() )    ? guideEnds[i]    : -1.0f;
        rails_[i].guideMode   = ( i < guideModes.size() )   ? guideModes[i]   : 0;
        rails_[i].guideDwell  = ( i < guideDwells.size() )  ? guideDwells[i]  : 0.0f;
        rails_[i].motionTrigger = ( i < motionTriggers.size() ) ? motionTriggers[i] : 0;
        rails_[i].animOffset = { 0.0f, 0.0f, 0.0f };
        rails_[i].motionTime = 0.0f;
        rails_[i].motionStarted = false;
        rails_[i].appearTrigger = ( i < appearTriggers.size() ) ? appearTriggers[i] : -1;
        rails_[i].appeared   = ( rails_[i].appearTrigger < 0 );
        rails_[i].appearAnim = rails_[i].appeared ? 1.0f : 0.0f;
        // 列車式回転の基準：基準位置でのレール中点と向きを1回だけ計測（この時点で回転・移動はゼロ）
        rails_[i].guideAlign = ( i < guideAligns.size() ) ? guideAligns[i] : 0;
        rails_[i].animYaw = 0.0f;
        if ( rails_[i].nodes.size() >= 2 && rails_[i].GetLength() > 0.0f ) {
            float mid = rails_[i].GetLength() * 0.5f;
            rails_[i].animPivot = rails_[i].GetPositionByDistance(mid);
            Vector3 midTan = rails_[i].GetTangentByDistance(mid);
            float horizLen = std::sqrt(midTan.x * midTan.x + midTan.z * midTan.z);
            rails_[i].restYaw = ( horizLen > 1e-4f ) ? std::atan2(midTan.x, midTan.z) : 0.0f;
        }
    }

    // タイプ割当は「連結処理より先」に行う（分岐点のキー割当が type を参照するため）。
    //   0/1 ならそれを使い、-1(自動)や未設定は主軸で自動判定。
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        int t = ( i < types.size() ) ? types[i] : -1;
        if ( t == 0 )      rails_[i].type = SplineRail::RailType::Horizontal;
        else if ( t == 1 ) rails_[i].type = SplineRail::RailType::Vertical;
        else               rails_[i].AutoDetectType();
    }

    BuildRailConnections(rails_); // 接続・分岐・ループ（動くレールは静的連結から除外）

    // 地面タイプ・穴・表示フラグ・道モード・片方向・速度倍率を割当
    const auto& grounds  = em->GetEditorRailGroundTypes();
    const auto& holes    = em->GetEditorRailNodeHoles();
    const auto& visible  = em->GetEditorRailVisible();
    const auto& roadMode = em->GetEditorRailRoadModes();
    const auto& endPlazas = em->GetEditorRailEndPlazas();
    const auto& oneWays  = em->GetEditorRailOneWay();
    const auto& spdMuls  = em->GetEditorRailSpeedMuls();
    for ( size_t i = 0; i < rails_.size(); ++i ) {
        int gt = ( i < grounds.size() ) ? grounds[i] : 0;
        rails_[i].groundType = static_cast<SplineRail::GroundType>(gt);
        if ( i < holes.size() ) rails_[i].nodeHole = holes[i];
        else                    rails_[i].nodeHole.clear();
        rails_[i].visible  = ( i < visible.size() ) ? ( visible[i] != 0 ) : true;
        rails_[i].roadMode = ( i < roadMode.size() ) ? roadMode[i] : 0;
        rails_[i].endPlaza = ( i < endPlazas.size() ) ? endPlazas[i] : 0;
        rails_[i].oneWay   = ( i < oneWays.size() ) ? oneWays[i] : 0;
        rails_[i].speedMul = ( i < spdMuls.size() && spdMuls[i] > 0.05f ) ? spdMuls[i] : 1.0f;
    }

    // スタート/ゴール地点（レール番号＋ノード番号 → レール上の距離へ変換）
    auto nodeToDist = [&](int rail, int node, int& outRail, float& outDist){
        outRail = 0; outDist = 0.0f;
        if ( rail < 0 || rail >= ( int ) rails_.size() ) { outRail = -1; return; }
        outRail = rail;
        int maxNode = ( int ) rails_[rail].nodes.size() - 1;
        int n = std::clamp(node, 0, ( std::max )( maxNode, 0 ));
        outDist = rails_[rail].GetDistanceFromT(( float ) n);
    };
    nodeToDist(em->GetEditorStartRail(), em->GetEditorStartNode(), startRail_, startDist_);
    if ( startRail_ < 0 ) { startRail_ = 0; startDist_ = 0.0f; } // 未設定はレール0の先頭
    nodeToDist(em->GetEditorGoalRail(), em->GetEditorGoalNode(), goalRail_, goalDist_);

    animTime_ = 0.0f;

    BuildMarkers();
    lastVersion_ = em->GetRailEditVersion();
}

// 動くレールの時間を進めて animOffset を更新し、緑線マーカーも追従させる。
//   波形は motionType で選択（0=サイン往復 / 1=端で一時停止つき往復 / 2=円運動）。
//   motionPhase(0〜1)で複数レールの動きをずらせる（0.5=半周期ずれ）。
//   motionTrigger=1（乗ったら動き出す）のレールは、プレイヤーが乗るまで基準位置で待機する
void RailField::UpdateMotion(float dt, int ridingRail){
    animTime_ += dt;
    const float kTwoPi = 2.0f * 3.14159265f;
    bool anyMotion = false;
    for ( size_t railIndex = 0; railIndex < rails_.size(); ++railIndex ) {
        auto& rail = rails_[railIndex];
        // 後から出現する道：発動レールに乗ったら出現（以後は下からせり上がるアニメで現れる）
        if ( rail.appearTrigger >= 0 ) {
            if ( !rail.appeared
                && ( ridingRail == rail.appearTrigger || ridingRail == kMotionStartAll ) ) {
                rail.appeared = true;
            }
            if ( rail.appeared && rail.appearAnim < 1.0f ) {
                rail.appearAnim = ( std::min )( 1.0f, rail.appearAnim + dt / 0.6f );
            }
        }
        if ( !rail.HasMotion() ) continue;
        anyMotion = true;
        // 乗ったら動き出す：発動するまで待機（animOffsetは基準位置のまま）。
        //   一度発動したら降りても動き続ける（ヨッシーの列車と同じ）。
        //   エディタのプレビュー（kMotionStartAll）では全レール強制発動＝動きを確認できる
        if ( rail.motionTrigger == 1 && !rail.motionStarted ) {
            if ( ridingRail == ( int ) railIndex || ridingRail == kMotionStartAll ) {
                rail.motionStarted = true;
            } else {
                continue;
            }
        }
        rail.motionTime += dt; // 発動してからの経過時間（最初から動くレールはPlay開始からの時間）
        float period = ( rail.motionPeriod > 0.1f ) ? rail.motionPeriod : 0.1f;
        float u = rail.motionTime / period + rail.motionPhase;
        u -= std::floor(u); // 0〜1 の周期位置

        const Vector3& amp = rail.motionAmp;
        switch ( rail.motionType ) {
        case 1: { // 端で一時停止つき往復（各端で周期の15%停止。移動はコサインで滑らか）
            const float dwell = 0.15f;
            const float move  = 0.5f - dwell;
            float w;
            if      ( u < move )        { w = -std::cos(( u / move ) * 3.14159265f); }          // -1 → +1
            else if ( u < 0.5f )        { w = 1.0f; }                                            // +端で停止
            else if ( u < 0.5f + move ) { w =  std::cos(( ( u - 0.5f ) / move ) * 3.14159265f); }// +1 → -1
            else                        { w = -1.0f; }                                           // -端で停止
            rail.animOffset = { amp.x * w, amp.y * w, amp.z * w };
            break;
        }
        case 2: { // 円運動（XZ楕円＋Y。半径は amp の各成分。amp.x と amp.z で円になる）
            float th = u * kTwoPi;
            rail.animOffset = { amp.x * std::cos(th), amp.y * std::sin(th), amp.z * std::sin(th) };
            break;
        }
        case 3: { // ガイドレール追従：別レールの経路に沿って動く（ヨッシー1-1の列車式）。
                  //   周期=1周期の秒数（一周ループ=1周 / 往復=行って帰る1往復）/ 位相=スタート位置 /
                  //   区間=ガイドのここからここまで。
                  //   guideMode 0=一周ループ（終点→始点へ戻る） / 1=往復（コサイン緩急で滑らかに折り返す）
            int g = rail.guideRail;
            if ( g >= 0 && g < ( int ) rails_.size() && &rails_[g] != &rail && rails_[g].GetLength() > 0.0f ) {
                const SplineRail& guide = rails_[g];
                const float guideLen = guide.GetLength();
                float s0 = std::clamp(rail.guideStart, 0.0f, guideLen);
                float s1 = ( rail.guideEnd < 0.0f ) ? guideLen : std::clamp(rail.guideEnd, 0.0f, guideLen);
                if ( s1 < s0 ) { std::swap(s0, s1); }
                if ( s1 - s0 < 0.01f ) { s0 = 0.0f; s1 = guideLen; } // 区間が潰れていたら全区間
                float d;
                const float dwell = ( std::max )( rail.guideDwell, 0.0f );
                if ( rail.guideMode == 1 ) {
                    // 往復：行き→(停車)→帰り→(停車)。移動はコサイン緩急＝両端で速度0。
                    //   周期=1往復の移動秒数（停車時間は別枠で足される）
                    float half = period * 0.5f;
                    float cycle = period + dwell * 2.0f;
                    float t = std::fmod(rail.motionTime + rail.motionPhase * cycle, cycle);
                    float w;
                    if      ( t < half )           { w = 0.5f - 0.5f * std::cos(( t / half ) * 3.14159265f); }               // 行き 0→1
                    else if ( t < half + dwell )   { w = 1.0f; }                                                              // 終点で停車
                    else if ( t < period + dwell ) { w = 0.5f + 0.5f * std::cos((( t - half - dwell ) / half ) * 3.14159265f); } // 帰り 1→0
                    else                           { w = 0.0f; }                                                              // 始点で停車
                    d = s0 + ( s1 - s0 ) * w;
                } else if ( rail.guideMode == 2 ) {
                    // 片道：出発まで dwell 秒待ち→走行→到着したら止まる（出発・到着ともコサイン緩急）。
                    //   周期=片道の秒数。u は周回でラップしているため経過時間から直接進行度を求める
                    float progress = std::clamp(( rail.motionTime - dwell ) / period + rail.motionPhase, 0.0f, 1.0f);
                    float w = 0.5f - 0.5f * std::cos(progress * 3.14159265f);
                    d = s0 + ( s1 - s0 ) * w;
                } else {
                    // 一周ループ：区間の端まで行くと始点へ戻って回り続ける（閉じたガイド向け）
                    d = s0 + ( s1 - s0 ) * u;
                }
                Vector3 p  = guide.GetPositionByDistance(d);
                Vector3 p0 = guide.GetPositionByDistance(s0);
                rail.animOffset = { p.x - p0.x, p.y - p0.y, p.z - p0.z };
                // 列車式：ガイドの進行方向に足場の向きを合わせて転回（縦向き⇔横向きも滑らか）。
                //   ほぼ真上/真下の区間では向きを保つ（水平成分が無いとヨーが定まらないため）
                if ( rail.guideAlign == 1 ) {
                    Vector3 guideTan = guide.GetTangentByDistance(d);
                    float horizLen = std::sqrt(guideTan.x * guideTan.x + guideTan.z * guideTan.z);
                    if ( horizLen > 0.15f ) {
                        float targetYaw = std::atan2(guideTan.x, guideTan.z) - rail.restYaw;
                        // 最短角で連続に追従（±180°の境目でクルッと一回転しない）
                        float deltaYaw = targetYaw - rail.animYaw;
                        while ( deltaYaw >  3.14159265f ) { deltaYaw -= 6.2831853f; }
                        while ( deltaYaw < -3.14159265f ) { deltaYaw += 6.2831853f; }
                        rail.animYaw += deltaYaw;
                    }
                }
            } else {
                rail.animOffset = { 0.0f, 0.0f, 0.0f };
            }
            break;
        }
        default: { // 0: サイン往復（従来どおり）
            float w = std::sin(u * kTwoPi);
            rail.animOffset = { amp.x * w, amp.y * w, amp.z * w };
            break;
        }
        }
    }
    if ( anyMotion ) { UpdateMarkerPositions(); }
}

// 編集モードへ戻った時：動くレールを基準位置へ戻す（発動状態・出現状態もリセット）。
void RailField::ResetMotion(){
    animTime_ = 0.0f;
    for ( auto& r : rails_ ) {
        r.animOffset = { 0.0f, 0.0f, 0.0f };
        r.motionTime = 0.0f;
        r.motionStarted = false;
        r.appeared   = ( r.appearTrigger < 0 );
        r.appearAnim = r.appeared ? 1.0f : 0.0f;
        r.animYaw = 0.0f; // 列車式の回転も基準向きへ戻す
    }
    UpdateMarkerPositions();
}

// マーカー位置 = リボンはワールド座標で焼いてあるので、レールの animOffset ぶんだけ平行移動。
//   列車式（animYaw）はpivot中心回転＝回転＋(pivot - pivot*Ry)の平行移動（道メッシュと同じ式）
void RailField::UpdateMarkerPositions(){
    for ( size_t k = 0; k < markerSlotsUsed_; ++k ) {
        MarkerSlot& s = *markerSlots_[k];
        if ( s.rail < 0 || s.rail >= ( int ) rails_.size() ) continue;
        const SplineRail& rail = rails_[s.rail];
        if ( rail.animYaw != 0.0f ) {
            float sinYaw = std::sin(rail.animYaw), cosYaw = std::cos(rail.animYaw);
            const Vector3& pivot = rail.animPivot;
            Vector3 rotatedPivot { pivot.x * cosYaw + pivot.z * sinYaw, pivot.y,
                                   -pivot.x * sinYaw + pivot.z * cosYaw };
            s.obj->SetRotation({ 0.0f, rail.animYaw, 0.0f });
            s.obj->SetTranslation({ pivot.x - rotatedPivot.x + rail.animOffset.x, rail.animOffset.y,
                                    pivot.z - rotatedPivot.z + rail.animOffset.z });
        } else {
            s.obj->SetRotation({ 0.0f, 0.0f, 0.0f });
            s.obj->SetTranslation(rail.animOffset);
        }
    }
}

void RailField::UpdateMarkers(){
    for ( size_t k = 0; k < markerSlotsUsed_; ++k ) { markerSlots_[k]->obj->Update(); }
}

void RailField::DrawMarkers() const{
    if ( !showMarkers_ ) return;
    for ( size_t k = 0; k < markerSlotsUsed_; ++k ) { markerSlots_[k]->obj->Draw(); }
}

void RailField::RebuildMarkers(){
    BuildMarkers();
}

// 生成したリボンを空きスロットへ書き込む（スロット不足時のみ新規確保）
void RailField::EmitMarker(const Model::ModelData& data, int railIdx, const Vector4& color){
    if ( data.vertices.empty() || data.indices.empty() ) return;

    if ( markerSlotsUsed_ == markerSlots_.size() ) {
        auto slot = std::make_unique<MarkerSlot>();
        slot->model = std::make_unique<Model>();
        // 白テクスチャ（GamePlayScene が先読み済み）＋マテリアル色で単色の線にする
        slot->model->InitializeDynamic(ModelManager::GetInstance()->GetModelCommon(),
                                       4096, 12288, "resources/block/white1x1.png");
        slot->obj = std::make_unique<Obj3d>();
        slot->obj->Initialize(slot->model.get());
        slot->obj->SetPipelineType(PipelineType::Object3D_CullNone);
        markerSlots_.push_back(std::move(slot));
    }
    MarkerSlot& s = *markerSlots_[markerSlotsUsed_++];
    s.model->UpdateMesh(data.vertices, data.indices);
    if ( whiteTexIndex_ != 0 ) { s.model->SetTexture(whiteTexIndex_); }
    if ( s.model->GetMaterial() ) {
        s.model->GetMaterial()->color = color;
        s.model->GetMaterial()->enableLighting = 0;
    }
    s.rail = railIdx;
    s.obj->SetCamera(camera_);
    s.obj->SetTranslation({ 0.0f, 0.0f, 0.0f });
    s.obj->Update();
}

// rails_ を距離でサンプルし、レール1本＝1本の細いリボンで経路を可視化する。
//   通常区間=緑リボン / 穴区間=赤リボン（レール毎に最大2メッシュ・2ドローコール）。
//   断面は「横リボン＋縦リボン」の十字型で、どの方向から見ても線に見える。
void RailField::BuildMarkers(){
    markerSlotsUsed_ = 0;

    const float spacing   = 0.5f;
    const float thickness = 0.08f;
    const float half      = thickness * 0.5f;
    const Vector4 lineColor = { 0.2f, 1.0f, 0.35f, 1.0f }; // 通常区間：明るい緑
    const Vector4 holeColor = { 1.0f, 0.2f, 0.15f, 1.0f }; // 穴区間：赤

    for ( int railIdx = 0; railIdx < ( int ) rails_.size(); ++railIdx ) {
        const SplineRail& rail = rails_[railIdx];
        float len = rail.GetLength();
        if ( len <= 0.0f || rail.nodes.size() < 2 ) { continue; }
        if ( !rail.visible ) { continue; } // 非表示レール（連結用）は緑線を描かない

        const auto holes = rail.GetHoleIntervals();
        auto isHoleMid = [&](float s) -> bool{
            for ( const auto& h : holes ) { if ( s >= h.d0 && s <= h.d1 ) return true; }
            return false;
        };

        Model::ModelData lineData, holeData;

        // 1セグメント（十字リボン2枚=4三角形）を対象メッシュへ追加する
        auto emitSeg = [&](Model::ModelData& out, const SplineRail::RailFrame& f0,
                           const SplineRail::RailFrame& f1){
            auto quad = [&](const Vector3& a0, const Vector3& a1, const Vector3& b1, const Vector3& b0,
                            const Vector3& nrm){
                uint32_t base = static_cast<uint32_t>( out.vertices.size() );
                const Vector3 ps[4] = { a0, a1, b1, b0 };
                const Vector2 uv[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
                for ( int k = 0; k < 4; ++k ) {
                    Model::VertexData v {};
                    v.position = { ps[k].x, ps[k].y, ps[k].z, 1.0f };
                    v.normal = nrm;
                    v.texcoord = uv[k];
                    out.vertices.push_back(v);
                }
                out.indices.push_back(base); out.indices.push_back(base + 1); out.indices.push_back(base + 2);
                out.indices.push_back(base); out.indices.push_back(base + 2); out.indices.push_back(base + 3);
            };
            auto off = [&](const SplineRail::RailFrame& f, const Vector3& axis, float sgn) -> Vector3{
                return { f.position.x + axis.x * sgn, f.position.y + axis.y * sgn, f.position.z + axis.z * sgn };
            };
            Vector3 r0 = { f0.right.x * half, f0.right.y * half, f0.right.z * half };
            Vector3 r1 = { f1.right.x * half, f1.right.y * half, f1.right.z * half };
            Vector3 u0 = { f0.up.x * half, f0.up.y * half, f0.up.z * half };
            Vector3 u1 = { f1.up.x * half, f1.up.y * half, f1.up.z * half };
            // 横リボン（法線=up）と縦リボン（法線=right）の十字
            quad(off(f0, r0, -1.0f), off(f0, r0, +1.0f), off(f1, r1, +1.0f), off(f1, r1, -1.0f), f0.up);
            quad(off(f0, u0, -1.0f), off(f0, u0, +1.0f), off(f1, u1, +1.0f), off(f1, u1, -1.0f), f0.right);
        };

        SplineRail::RailFrame prev = rail.GetFrameAtDistance(0.0f);
        float prevS = 0.0f;
        for ( float s = spacing; s <= len + 0.001f; s += spacing ) {
            float ss = ( s > len ) ? len : s;
            SplineRail::RailFrame cur = rail.GetFrameAtDistance(ss);
            if ( ss - prevS > 1e-4f ) {
                float midS = ( prevS + ss ) * 0.5f;
                emitSeg(isHoleMid(midS) ? holeData : lineData, prev, cur);
            }
            prev = cur;
            prevS = ss;
        }

        EmitMarker(lineData, railIdx, lineColor);
        EmitMarker(holeData, railIdx, holeColor);
    }

    // 余ったスロットは空にして描かない（バッファは使い回す）
    static const std::vector<Model::VertexData> kEmptyV;
    static const std::vector<uint32_t> kEmptyI;
    for ( size_t k = markerSlotsUsed_; k < markerSlots_.size(); ++k ) {
        markerSlots_[k]->model->UpdateMesh(kEmptyV, kEmptyI);
    }
}
