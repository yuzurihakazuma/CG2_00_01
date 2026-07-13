# 仕様書: 残タスク levelEditor_main版 (実コード準拠マスター)

照合表_levelEditor_main照合.md の「B. 未実装」だけを埋める仕様書。
[実名: …] はリポジトリで確認済みの現行コード。
断面表・UV値・アルゴリズム詳細は resources/road 内の既存ドキュメントと
guides/ を参照 (実装済み部分の仕様は変更しない)。

---

## §1 編集パフォーマンス修正 (最優先)

### 原因 (実コードで特定済み)
編集のたびに [実名: GamePlayScene.cpp:283-284]
`railField_.Sync(...)` → `roadMesh_.Build(全レール)` が走り、その中で
(a) [RailField.cpp:357〜] マーカーを **0.5m毎のObj3dとして全破棄・全再生成** (≈220個)
(b) [RoadMesh.cpp:246-249] レール毎に `Model::InitializePrimitive` で
    **頂点リソースを毎回作り直し** (CreateBuffers)
ドラッグ中はこれが連打されるため重い。

### 修正
1. **差分再構築**: RailEditor に `dirtyRails_`(std::set<int>) を追加し、
   SetRailNodePos / InsertRailNode / DeleteRailNode / TranslateSelection 等で
   該当レール番号を積む。`RailField::Sync` と `RoadMesh::Build` に
   「dirty のレールだけ作り直す」オーバーロードを追加
   (交差点は dirty レールが参加するノードのみ再配置)
2. **RoadMesh の固定容量バッファ化**: レール毎に UPLOAD ヒープ VB/IB
   (既定 頂点4096 / インデックス24576) を初回だけ
   [実名: ResourceFactory::CreateBufferResource] で確保し Map 保持。
   再構築は memcpy + 描画数更新のみ。`Model::InitializePrimitive` を
   編集中に呼ばない (容量超過時のみ2倍で作り直し)
   ※Model/Obj3d の描画経路を保つなら「Modelに動的更新APIを足す」でもよい。
     どちらにするかは実装時に相談
3. **マーカーの脱Obj3d**: railLineCube のObj3d群をやめ、
   レール1本=1本の細いリボンメッシュ (2の方式を流用、緑/穴赤は頂点色 or
   白テクスチャ+マテリアル色2分割)。ドローコール 220→レール数
4. **ドラッグ中の間引き**: dirty 再構築は最大10回/秒。ドラッグ中は簡易モード
   (リング1m固定・CollectJunctions/フタ/キャップ省略)、マウスアップで本生成

受け入れ条件:
- [ ] ノードドラッグ中 FPS55以上 / Update増分2ms未満
- [ ] ドラッグ中の Obj3d 生成・CreateBuffers 呼び出しが 0回
- [ ] マーカー由来のドローコールがレール本数まで減る
- [ ] マウスアップ後1フレームで最終形 (交差点・フタ含む)

## §2 任意角度ジャンクション (「仮置き」の解消)

現状 [実名: RoadMesh::CollectJunctions] は roadT / roadCross の90°軸沿い配置。
**Cut機構 (掃引の切り詰め) はそのまま流用**し、ピース配置を
「パッチ生成」に差し替える:
- ノードごとの方向リスト作成 → guides/GUIDE_ジャンクション生成.md の手順で
  t_cut と パッチ (上面扇+ベベル+壁+底) を生成 (2本曲がり角/T/十字を同一コード、
  検証済み: 継ぎ目誤差0 / 180-250三角形)
- 生成メッシュは §1-2 と同じ動的バッファでまとめて1ドローコール
- 既存の掃引コーナーコネクタ (A節) は「2本・ゆるい角度」の担当として残してよい
  (パッチは鋭角と3本以上を担当)
- **代替案**: 斜め連結を当面使わないなら現状維持+レールを90°で引く運用でも可。
  ただし「仮置き」の見た目問題は残る

## §3 v5アセット差し替え + 危険帯 (穴の警告)

1. resources/road の road_atlas.png と road_*.obj を zip の v5 一式へ
   **同時に**差し替える (v4→v5でピース用UV領域が移動しているため片方だけはNG。
   掃引側が参照する帯のv値 [実名: kSlopeV ほか] は不変なのでコード変更なし)
2. 危険帯: [実名: BuildRailMesh のリング決定部 RoadMesh.cpp:107〜] に
   穴の手前後1.0m判定を追加し、上面頂点4/5の v を 0.2070/0.2734 へ。
   坂と同じ二重リング機構 (RoadMesh.cpp:128-131) を流用
3. (任意) 穴の切り口: 現状のフタ [RoadMesh.cpp:208-235] を road_end の
   丸キャップに置換すると見た目が可愛くなる。フタのままでも機能上は問題なし

## §4 レール毎の道ON/OFF

- [実名: LevelData] に `std::vector<int> railRoadModes;` (0=自動/1=なし) を追加。
  railNodeHoles と同じ並び規約・保存/読込経路
- [実名: RoadMesh::Build / CollectJunctions] で「なし」レールをスキップ。
  接続ノードで道あり方向が 2本以上→交差点 / 1本→road_end / 0本→無し
- UI: RailEditor::DrawWindow の路線リスト行にトグル追加
- 既存の RoadMesh::SetVisible (全体デバッグ用) はそのまま残す

## §5 自動スナップ + ジョイント表示

- 自動スナップ: ノードドラッグ中 (端点ノードのみ)、
  (a)他レール端点 (b)他レール本体 [実名: SplineRail::GetClosestDistance]
  を半径1.2m [実名: kJoin, RailEditor.cpp:298と同値] で検索 → 緑プレビュー →
  マウスアップで既存処理を自動実行:
  (a)→「端点を溶接」[RailEditor.cpp:1109付近] のロジックを関数化して呼ぶ
  (b)→ [実名: ConnectNearbyLines] を対象限定で実行
- 履歴は既存 [実名: CommitIfStable] に自然に乗る
- ジョイント: road_joint.obj (同梱・OBJなのでそのまま置ける) を接続ノードから
  毎回導出配置 (2本溶接=中央1個 / 交差点=各入口、凸を中心向き)。
  保存しない派生データ。描画は §1-3 のマーカー統合と同じ仕組みに相乗り可
- 調整項目: autoSnap / snapDistance(1.2) / jointVisible(editor/both/off)

## §6 実装順と Claude Code への投げ方

順: §1 (開発の邪魔を先に除去) → §3 (差し替えと危険帯は小さい) →
§2 → §4 → §5

```
照合表_levelEditor_main照合.md と 仕様書_残タスク_levelEditor_main版.md を
読んでください。実装済みの部分 (照合表A) は変更しません。
対象: RoadMesh / RailField / RailEditor / SplineRail / LevelData / GamePlayScene。
まず §1-1,2 (差分再構築 + 固定容量バッファ) から。
コードは提案ベースで進めて、各ステップで私が確認します。
各節の [実名: ファイル:行] を必ず現物と突き合わせてから変更してください。
```
